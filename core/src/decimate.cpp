// SPDX-License-Identifier: MIT
#include "cruth/decimate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace cruth {

namespace {

// Symmetric 4x4 quadric stored as its upper triangle.
struct Quadric {
    double a[10] = {};

    static Quadric plane(double nx, double ny, double nz, double d, double weight) {
        Quadric q;
        q.a[0] = nx * nx; q.a[1] = nx * ny; q.a[2] = nx * nz; q.a[3] = nx * d;
        q.a[4] = ny * ny; q.a[5] = ny * nz; q.a[6] = ny * d;
        q.a[7] = nz * nz; q.a[8] = nz * d;
        q.a[9] = d * d;
        for (double& v : q.a) v *= weight;
        return q;
    }
    Quadric& operator+=(const Quadric& o) {
        for (int i = 0; i < 10; ++i) a[i] += o.a[i];
        return *this;
    }
    double error(Vec3 p) const {
        const double x = p.x, y = p.y, z = p.z;
        return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z +
               2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
    }
    // Minimiser of the quadric, if the 3x3 system is well conditioned.
    bool optimum(Vec3& out) const {
        const double m00 = a[0], m01 = a[1], m02 = a[2], m11 = a[4], m12 = a[5], m22 = a[7];
        const double det = m00 * (m11 * m22 - m12 * m12) - m01 * (m01 * m22 - m12 * m02) + m02 * (m01 * m12 - m11 * m02);
        if (std::abs(det) < 1e-12) return false;
        const double b0 = -a[3], b1 = -a[6], b2 = -a[8];
        const double inv = 1.0 / det;
        const double x = (b0 * (m11 * m22 - m12 * m12) - m01 * (b1 * m22 - m12 * b2) + m02 * (b1 * m12 - m11 * b2)) * inv;
        const double y = (m00 * (b1 * m22 - m12 * b2) - b0 * (m01 * m22 - m12 * m02) + m02 * (m01 * b2 - b1 * m02)) * inv;
        const double z = (m00 * (m11 * b2 - b1 * m12) - m01 * (m01 * b2 - b1 * m02) + b0 * (m01 * m12 - m11 * m02)) * inv;
        out = {float(x), float(y), float(z)};
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }
};

struct Candidate {
    double cost;
    Index a, b;
    std::uint32_t va, vb;  // vertex versions when queued
    Vec3 target;
    bool operator>(const Candidate& o) const { return cost > o.cost; }
};

std::uint64_t edge_key(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | b;
}

}  // namespace

Mesh decimate(const Mesh& mesh, const DecimateOptions& options, DecimateStats* stats) {
    std::vector<Vec3> pos = mesh.positions;
    std::vector<std::array<Index, 3>> tris;
    {
        auto idx = mesh.triangulated_indices();
        tris.reserve(idx.size() / 3);
        for (std::size_t i = 0; i < idx.size(); i += 3) tris.push_back({idx[i], idx[i + 1], idx[i + 2]});
    }
    const std::size_t nv = pos.size();
    std::vector<char> face_alive(tris.size(), 1);
    std::vector<char> vert_alive(nv, 1);
    std::vector<std::uint32_t> version(nv, 0);
    std::vector<std::vector<Index>> vf(nv);
    for (Index f = 0; f < tris.size(); ++f) {
        for (Index v : tris[f]) vf[v].push_back(f);
    }

    // Quadrics: area-weighted face planes plus boundary penalty planes.
    std::vector<Quadric> Q(nv);
    std::unordered_map<std::uint64_t, int> edge_faces;
    edge_faces.reserve(tris.size() * 3);
    for (Index f = 0; f < tris.size(); ++f) {
        const auto& t = tris[f];
        Vec3 n = cross(pos[t[1]] - pos[t[0]], pos[t[2]] - pos[t[0]]);
        double area = 0.5 * length(n);
        n = normalize(n);
        Quadric q = Quadric::plane(n.x, n.y, n.z, -double(dot(n, pos[t[0]])), area);
        for (Index v : t) Q[v] += q;
        for (int i = 0; i < 3; ++i) ++edge_faces[edge_key(t[i], t[(i + 1) % 3])];
    }
    if (options.preserve_boundary) {
        for (Index f = 0; f < tris.size(); ++f) {
            const auto& t = tris[f];
            Vec3 n = normalize(cross(pos[t[1]] - pos[t[0]], pos[t[2]] - pos[t[0]]));
            for (int i = 0; i < 3; ++i) {
                Index a = t[i], b = t[(i + 1) % 3];
                if (edge_faces[edge_key(a, b)] != 1) continue;
                Vec3 e = pos[b] - pos[a];
                Vec3 side = normalize(cross(e, n));
                double w = 1000.0 * length_squared(e);
                Quadric q = Quadric::plane(side.x, side.y, side.z, -double(dot(side, pos[a])), w);
                Q[a] += q;
                Q[b] += q;
            }
        }
    }

    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> heap;
    auto push = [&](Index a, Index b) {
        Quadric q = Q[a];
        q += Q[b];
        Vec3 target;
        if (!q.optimum(target)) {
            // Fall back to the best of the endpoints and the midpoint.
            Vec3 options3[3] = {pos[a], pos[b], (pos[a] + pos[b]) * 0.5f};
            double best = 1e300;
            for (Vec3 c : options3) {
                double e = q.error(c);
                if (e < best) {
                    best = e;
                    target = c;
                }
            }
        }
        heap.push({std::max(0.0, q.error(target)), a, b, version[a], version[b], target});
    };
    for (const auto& [key, count] : edge_faces) {
        (void)count;
        push(Index(key >> 32), Index(key & 0xFFFFFFFFu));
    }

    std::size_t live = tris.size();
    std::size_t collapses = 0;
    std::vector<Index> na, nb;
    auto neighbours = [&](Index v, std::vector<Index>& out) {
        out.clear();
        for (Index f : vf[v]) {
            if (!face_alive[f]) continue;
            for (Index u : tris[f]) {
                if (u != v) out.push_back(u);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };

    while (live > options.target_triangles && !heap.empty()) {
        Candidate c = heap.top();
        heap.pop();
        if (!vert_alive[c.a] || !vert_alive[c.b] || version[c.a] != c.va || version[c.b] != c.vb) continue;

        // Link condition: shared neighbours must be exactly the edge's opposite vertices.
        neighbours(c.a, na);
        neighbours(c.b, nb);
        if (!std::binary_search(na.begin(), na.end(), c.b)) continue;
        std::size_t shared_faces = 0;
        for (Index f : vf[c.a]) {
            if (!face_alive[f]) continue;
            const auto& t = tris[f];
            if (t[0] == c.b || t[1] == c.b || t[2] == c.b) ++shared_faces;
        }
        std::size_t common = 0;
        for (Index u : na) common += std::binary_search(nb.begin(), nb.end(), u);
        if (common != shared_faces || shared_faces == 0) continue;

        // Reject collapses that flip or degenerate any surviving face.
        bool ok = true;
        for (Index v : {c.a, c.b}) {
            for (Index f : vf[v]) {
                if (!face_alive[f] || !ok) continue;
                const auto& t = tris[f];
                bool has_a = t[0] == c.a || t[1] == c.a || t[2] == c.a;
                bool has_b = t[0] == c.b || t[1] == c.b || t[2] == c.b;
                if (has_a && has_b) continue;
                Vec3 p[3], q[3];
                for (int i = 0; i < 3; ++i) {
                    p[i] = pos[t[i]];
                    q[i] = (t[i] == c.a || t[i] == c.b) ? c.target : p[i];
                }
                Vec3 n0 = cross(p[1] - p[0], p[2] - p[0]);
                Vec3 n1 = cross(q[1] - q[0], q[2] - q[0]);
                float l1 = length(n1);
                if (l1 < 1e-12f || dot(normalize(n0), n1 / l1) < options.min_normal_cosine) ok = false;
            }
        }
        if (!ok) continue;

        // Collapse b into a.
        for (Index f : vf[c.b]) {
            if (!face_alive[f]) continue;
            auto& t = tris[f];
            bool has_a = t[0] == c.a || t[1] == c.a || t[2] == c.a;
            if (has_a) {
                face_alive[f] = 0;
                --live;
            } else {
                for (Index& v : t) {
                    if (v == c.b) v = c.a;
                }
                vf[c.a].push_back(f);
            }
        }
        vert_alive[c.b] = 0;
        vf[c.b].clear();
        pos[c.a] = c.target;
        Q[c.a] += Q[c.b];
        ++version[c.a];
        ++collapses;

        // Compact a's face list and requeue its edges.
        auto& list = vf[c.a];
        list.erase(std::remove_if(list.begin(), list.end(), [&](Index f) { return !face_alive[f]; }), list.end());
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        // Old entries touching a are now stale (version bump); queue fresh ones.
        neighbours(c.a, na);
        for (Index u : na) push(c.a, u);
    }

    Mesh out;
    std::vector<Index> remap(nv, kInvalidIndex);
    for (Index f = 0; f < tris.size(); ++f) {
        if (!face_alive[f]) continue;
        Index c[3];
        for (int i = 0; i < 3; ++i) {
            Index v = tris[f][i];
            if (remap[v] == kInvalidIndex) remap[v] = out.add_vertex(pos[v]);
            c[i] = remap[v];
        }
        out.add_face({c[0], c[1], c[2]});
    }
    if (stats) {
        stats->collapses = collapses;
        stats->triangles_before = tris.size();
        stats->triangles_after = out.face_count();
    }
    return out;
}

}  // namespace cruth
