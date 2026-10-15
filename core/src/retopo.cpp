// SPDX-License-Identifier: MIT
#include "cruth/retopo.hpp"

#include "cruth/half_edge.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace cruth {

namespace {

std::uint64_t edge_key(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | b;
}

float polyline_length(std::span<const Vec3> pts, bool closed) {
    float len = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) len += distance(pts[i], pts[i + 1]);
    if (closed && pts.size() > 1) len += distance(pts.back(), pts.front());
    return len;
}

}  // namespace

std::vector<Vec3> resample_polyline(std::span<const Vec3> pts, int count, bool closed) {
    std::vector<Vec3> out;
    if (pts.empty() || count <= 0) return out;
    if (pts.size() == 1) return std::vector<Vec3>(std::size_t(count), pts[0]);
    const float total = polyline_length(pts, closed);
    const int steps = closed ? count : std::max(1, count - 1);
    const float spacing = total / float(steps);
    std::size_t seg = 0;
    float seg_start = 0;
    const std::size_t nseg = closed ? pts.size() : pts.size() - 1;
    for (int i = 0; i < count; ++i) {
        const float target = std::min(total, spacing * float(i));
        while (seg + 1 < nseg) {
            const float l = distance(pts[seg], pts[(seg + 1) % pts.size()]);
            if (seg_start + l >= target) break;
            seg_start += l;
            ++seg;
        }
        const Vec3 a = pts[seg], b = pts[(seg + 1) % pts.size()];
        const float l = distance(a, b);
        out.push_back(l > 0 ? lerp(a, b, std::clamp((target - seg_start) / l, 0.0f, 1.0f)) : a);
    }
    return out;
}

std::vector<Polyline> slice_mesh(const Mesh& mesh, Vec3 n, float d) {
    // One crossing point per cut edge; triangles link their two crossings.
    std::unordered_map<std::uint64_t, Vec3> point;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> links;
    const auto tris = mesh.triangulated_indices();
    for (std::size_t t = 0; t < tris.size(); t += 3) {
        std::uint64_t cut[3];
        int k = 0;
        for (int e = 0; e < 3; ++e) {
            const Index a = tris[t + e], b = tris[t + (e + 1) % 3];
            const float sa = dot(n, mesh.positions[a]) - d, sb = dot(n, mesh.positions[b]) - d;
            if ((sa < 0) == (sb < 0)) continue;
            const std::uint64_t key = edge_key(a, b);
            point.emplace(key, lerp(mesh.positions[a], mesh.positions[b], sa / (sa - sb)));
            cut[k++] = key;
        }
        if (k == 2) {
            links[cut[0]].push_back(cut[1]);
            links[cut[1]].push_back(cut[0]);
        }
    }
    std::vector<Polyline> loops;
    std::unordered_set<std::uint64_t> used;
    // Start open chains at their ends first, then closed loops anywhere.
    std::vector<std::uint64_t> starts;
    for (const auto& [key, nb] : links) {
        if (nb.size() == 1) starts.push_back(key);
    }
    for (const auto& [key, nb] : links) {
        if (nb.size() != 1) starts.push_back(key);
    }
    for (std::uint64_t start : starts) {
        if (used.count(start)) continue;
        Polyline loop;
        std::uint64_t cur = start, prev = ~std::uint64_t(0);
        while (true) {
            used.insert(cur);
            loop.points.push_back(point[cur]);
            std::uint64_t next = ~std::uint64_t(0);
            for (std::uint64_t c : links[cur]) {
                if (c != prev && !used.count(c)) {
                    next = c;
                    break;
                }
            }
            if (next == ~std::uint64_t(0)) {
                // Closed when the last crossing links back to the first.
                const auto& nb = links[cur];
                loop.closed = loop.points.size() > 2 && std::find(nb.begin(), nb.end(), start) != nb.end();
                break;
            }
            prev = cur;
            cur = next;
        }
        if (loop.points.size() >= 2) loops.push_back(std::move(loop));
    }
    return loops;
}

RetopoMesh::RetopoMesh(const Mesh& reference) : reference_(reference) {
    weld_radius = 0.01f * length(reference.bounds().size());
}

RetopoMesh::RetopoMesh(const Mesh& reference, Mesh start) : RetopoMesh(reference) { mesh_ = std::move(start); }

Vec3 RetopoMesh::snap(Vec3 p) const {
    auto cp = reference_.closest_point(p);
    return cp ? cp->point : p;
}

Vec3 RetopoMesh::normal_at(Vec3 p) const {
    auto cp = reference_.closest_point(p);
    if (!cp) return {0, 1, 0};
    const auto& t = reference_.triangle(cp->triangle);
    const auto& P = reference_.positions();
    return normalize(cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]));
}

Index RetopoMesh::add_vertex_welded(Vec3 p) {
    p = snap(p);
    const float r2 = weld_radius * weld_radius;
    Index best = kInvalidIndex;
    float best_d = r2;
    for (Index v = 0; v < mesh_.vertex_count(); ++v) {
        const float d = length_squared(mesh_.positions[v] - p);
        if (d <= best_d) {
            best_d = d;
            best = v;
        }
    }
    return best != kInvalidIndex ? best : mesh_.add_vertex(p);
}

// Adds a face wound so its normal agrees with the reference surface.
void RetopoMesh::add_face_outward(std::vector<Index> c) {
    c.erase(std::unique(c.begin(), c.end()), c.end());
    while (c.size() > 1 && c.front() == c.back()) c.pop_back();
    if (c.size() < 3) return;
    Mesh probe;
    for (Index v : c) probe.add_vertex(mesh_.positions[v]);
    std::vector<Index> local(c.size());
    for (Index i = 0; i < c.size(); ++i) local[i] = i;
    probe.add_face(local);
    if (dot(probe.face_normal(0), normal_at(probe.face_centroid(0))) < 0) std::reverse(c.begin(), c.end());
    mesh_.add_face(c);
}

Status RetopoMesh::add_strip(std::span<const Vec3> stroke, float width, int segments) {
    if (stroke.size() < 2 || segments < 1 || !(width > 0)) {
        return Error{ErrorCode::InvalidArgument, "strip needs a stroke of 2+ points, segments >= 1 and width > 0"};
    }
    const auto spine = resample_polyline(stroke, segments + 1, false);
    std::vector<Index> left, right;
    for (std::size_t i = 0; i < spine.size(); ++i) {
        const Vec3 p = snap(spine[i]);
        const Vec3 n = normal_at(p);
        const Vec3 along = normalize(spine[std::min(i + 1, spine.size() - 1)] - spine[i > 0 ? i - 1 : 0]);
        Vec3 side = normalize(cross(n, along));
        if (length_squared(side) == 0) side = normalize(cross(n, Vec3{1, 0, 0}));
        left.push_back(add_vertex_welded(p + side * (width * 0.5f)));
        right.push_back(add_vertex_welded(p - side * (width * 0.5f)));
    }
    for (int i = 0; i < segments; ++i) add_face_outward({left[i], left[i + 1], right[i + 1], right[i]});
    return Status::ok();
}

Result<std::vector<Index>> RetopoMesh::add_contour(std::span<const Vec3> stroke, int count, Vec3 view_direction,
                                                    bool bridge_previous) {
    if (stroke.size() < 2 || count < 3) return Error{ErrorCode::InvalidArgument, "contour needs a stroke and count >= 3"};
    const Vec3 s0 = snap(stroke.front()), s1 = snap(stroke.back());
    const Vec3 mid = snap(stroke[stroke.size() / 2]);
    const Vec3 across = s1 - s0;
    if (length_squared(across) == 0) return Error{ErrorCode::InvalidArgument, "contour stroke has no length"};
    // The cutting plane holds the stroke and the line of sight, so it runs
    // around the form the stroke crosses.
    const Vec3 sight = length_squared(view_direction) > 0 ? normalize(view_direction) : normal_at(mid);
    Vec3 plane_n = normalize(cross(across, sight));
    if (length_squared(plane_n) == 0) return Error{ErrorCode::InvalidArgument, "contour stroke is parallel to the surface normal"};
    const float plane_d = dot(plane_n, mid);

    Mesh ref;
    ref.positions = reference_.positions();
    for (Index t = 0; t < reference_.triangle_count(); ++t) {
        const auto& tri = reference_.triangle(t);
        ref.add_face({tri[0], tri[1], tri[2]});
    }
    auto loops = slice_mesh(ref, plane_n, plane_d);
    if (loops.empty()) return Error{ErrorCode::InvalidArgument, "contour plane does not cut the surface"};
    // The loop passing nearest the stroke's middle.
    std::size_t best = 0;
    float best_d = 3.4e38f;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        for (const auto& p : loops[i].points) {
            const float dd = distance(p, mid);
            if (dd < best_d) {
                best_d = dd;
                best = i;
            }
        }
    }
    std::vector<Vec3> loop = std::move(loops[best].points);
    const bool closed = loops[best].closed;
    // Start the loop at the point nearest the stroke start for stable seams.
    std::size_t start = 0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        if (distance(loop[i], s0) < distance(loop[start], s0)) start = i;
    }
    std::rotate(loop.begin(), loop.begin() + std::ptrdiff_t(start), loop.end());
    const auto samples = resample_polyline(loop, count, closed);

    std::vector<Index> ring;
    for (const Vec3& p : samples) ring.push_back(mesh_.add_vertex(snap(p)));
    if (bridge_previous && last_contour_.size() == ring.size()) {
        auto s = bridge(last_contour_, ring, closed);
        if (!s) return s.error();
    }
    last_contour_ = ring;
    return ring;
}

Status RetopoMesh::bridge(std::span<const Index> a, std::span<const Index> b, bool closed) {
    if (a.size() != b.size() || a.size() < 2) return Error{ErrorCode::InvalidArgument, "bridge needs two chains of equal length >= 2"};
    const std::size_t n = a.size();
    std::vector<Index> bb(b.begin(), b.end());
    if (closed) {
        // Choose the rotation and direction of b that lines up best with a.
        double best = 1e300;
        std::vector<Index> best_order = bb;
        for (int dir = 0; dir < 2; ++dir) {
            std::vector<Index> cand = bb;
            if (dir) std::reverse(cand.begin(), cand.end());
            for (std::size_t r = 0; r < n; ++r) {
                double cost = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    cost += length_squared(mesh_.positions[a[i]] - mesh_.positions[cand[(i + r) % n]]);
                }
                if (cost < best) {
                    best = cost;
                    best_order.clear();
                    for (std::size_t i = 0; i < n; ++i) best_order.push_back(cand[(i + r) % n]);
                }
            }
        }
        bb = best_order;
    } else {
        // Open chains: reverse b if its ends are closer to a's ends that way.
        const double same = distance(mesh_.positions[a.front()], mesh_.positions[bb.front()]) +
                            distance(mesh_.positions[a.back()], mesh_.positions[bb.back()]);
        const double flip = distance(mesh_.positions[a.front()], mesh_.positions[bb.back()]) +
                            distance(mesh_.positions[a.back()], mesh_.positions[bb.front()]);
        if (flip < same) std::reverse(bb.begin(), bb.end());
    }
    const std::size_t quads = closed ? n : n - 1;
    for (std::size_t i = 0; i < quads; ++i) {
        const std::size_t j = (i + 1) % n;
        add_face_outward({a[i], a[j], bb[j], bb[i]});
    }
    return Status::ok();
}

Status RetopoMesh::add_quad(const std::array<Vec3, 4>& corners) {
    std::vector<Index> c;
    for (const Vec3& p : corners) c.push_back(add_vertex_welded(p));
    std::vector<Index> unique = c;
    std::sort(unique.begin(), unique.end());
    if (std::unique(unique.begin(), unique.end()) != unique.end()) {
        return Error{ErrorCode::InvalidArgument, "quad corners weld onto the same vertex"};
    }
    add_face_outward(c);
    return Status::ok();
}

std::pair<Index, Index> RetopoMesh::nearest_edge(Vec3 p) const {
    std::pair<Index, Index> best{kInvalidIndex, kInvalidIndex};
    float best_d = 3.4e38f;
    for (std::size_t f = 0; f < mesh_.face_count(); ++f) {
        auto fv = mesh_.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            const Vec3 a = mesh_.positions[fv[i]], b = mesh_.positions[fv[(i + 1) % fv.size()]];
            const Vec3 ab = b - a;
            const float t = std::clamp(dot(p - a, ab) / std::max(dot(ab, ab), 1e-20f), 0.0f, 1.0f);
            const float d = length_squared(a + ab * t - p);
            if (d < best_d) {
                best_d = d;
                best = {fv[i], fv[(i + 1) % fv.size()]};
            }
        }
    }
    return best;
}

Result<std::vector<Index>> RetopoMesh::insert_loop(Index a, Index b, float t) {
    auto built = HalfEdgeMesh::build(mesh_);
    if (!built) return built.error();
    const HalfEdgeMesh& he = built.value();
    Index start = kInvalidIndex;
    for (Index h = 0; h < he.half_edges.size(); ++h) {
        if (he.half_edges[h].origin == a && he.target(h) == b) start = h;
    }
    if (start == kInvalidIndex) return Error{ErrorCode::InvalidArgument, "insert loop: (a, b) is not an edge"};
    t = std::clamp(t, 0.01f, 0.99f);

    // Walk the ring of quads, recording each cut edge as (a-side vertex,
    // other vertex) so every cut sits at t from the same side of the ring and
    // the new loop runs parallel to its neighbours. A ring may cross itself:
    // each quad can be passed once per pair of opposite edges (bit 0 = edges
    // 0/2, bit 1 = edges 1/3), and a quad passed both ways becomes four quads.
    using Cut = std::pair<Index, Index>;
    std::unordered_map<Index, int> pairs;  // quad -> pairs crossed
    auto local_index = [&](Index h) {
        const Index f = he.half_edges[h].face;
        int i = 0;
        for (Index g = he.face_half_edge[f]; g != h; g = he.half_edges[g].next) ++i;
        return i;
    };
    auto walk = [&](Index h, bool a_side_is_origin, std::vector<Cut>& cuts) {
        while (true) {
            const Index f = he.half_edges[h].face;
            if (he.face_size(f) != 4) return;  // ends on a non-quad: it gains the cut vertex
            const int bit = 1 << (local_index(h) & 1);
            int& crossed = pairs[f];
            if (crossed & bit) return;  // back where the ring started
            crossed |= bit;
            const Index opp = he.half_edges[he.half_edges[h].next].next;
            // In quad (a, b, c, d) with h = a->b, the opposite edge is c->d and
            // d is on a's side; walking from the reversed edge flips that.
            cuts.push_back(a_side_is_origin ? Cut{he.target(opp), he.half_edges[opp].origin}
                                            : Cut{he.half_edges[opp].origin, he.target(opp)});
            const Index twin = he.half_edges[opp].twin;
            if (twin == kInvalidIndex) return;
            h = twin;
        }
    };
    std::vector<Cut> forward{{a, b}}, backward;
    walk(start, true, forward);
    if (forward.size() > 1 && edge_key(forward.back().first, forward.back().second) == edge_key(a, b)) {
        forward.pop_back();  // closed ring: back at the start edge
    } else if (he.half_edges[start].twin != kInvalidIndex) {
        walk(he.half_edges[start].twin, false, backward);
    }
    std::vector<Cut> cuts(backward.rbegin(), backward.rend());
    cuts.insert(cuts.end(), forward.begin(), forward.end());

    // One new vertex per cut edge.
    std::unordered_map<std::uint64_t, Index> new_vertex;
    std::vector<Index> loop;
    for (const auto& [u, v] : cuts) {
        const std::uint64_t key = edge_key(u, v);
        if (new_vertex.count(key)) continue;
        const Index nv = mesh_.add_vertex(snap(lerp(mesh_.positions[u], mesh_.positions[v], t)));
        new_vertex.emplace(key, nv);
        loop.push_back(nv);
    }
    auto cut_of = [&](Index u, Index v) {
        const auto it = new_vertex.find(edge_key(u, v));
        return it == new_vertex.end() ? kInvalidIndex : it->second;
    };
    Mesh out;
    for (Index f = 0; f < he.face_count(); ++f) {
        std::vector<Index> c;
        const Index h0 = he.face_half_edge[f];
        Index h = h0;
        do {
            c.push_back(he.half_edges[h].origin);
            h = he.half_edges[h].next;
        } while (h != h0);
        const auto it = pairs.find(f);
        const int crossed = it == pairs.end() ? 0 : it->second;
        if (crossed == 3) {
            // Crossed both ways: four quads around a centre vertex.
            std::array<Index, 4> m{};
            Vec3 centre;
            for (int i = 0; i < 4; ++i) {
                m[i] = cut_of(c[i], c[(i + 1) % 4]);
                centre += mesh_.positions[m[i]] * 0.25f;
            }
            const Index centre_vertex = mesh_.add_vertex(snap(centre));
            for (int i = 0; i < 4; ++i) out.add_face({c[i], m[i], centre_vertex, m[(i + 3) % 4]});
        } else if (crossed != 0) {
            // Crossed once: split between the cuts on edges k and k + 2.
            const int k = crossed == 1 ? 0 : 1;
            const Index qa = c[k], qb = c[k + 1], qc = c[(k + 2) % 4], qd = c[(k + 3) % 4];
            const Index p = cut_of(qa, qb), q = cut_of(qc, qd);
            out.add_face({qa, p, q, qd});
            out.add_face({p, qb, qc, q});
        } else {
            // Untouched, or a non-quad where the ring ended: keep it, adding
            // any cut vertex on its edges so the mesh stays watertight.
            std::vector<Index> poly;
            for (std::size_t i = 0; i < c.size(); ++i) {
                poly.push_back(c[i]);
                const Index m = cut_of(c[i], c[(i + 1) % c.size()]);
                if (m != kInvalidIndex) poly.push_back(m);
            }
            out.add_face(poly);
        }
    }
    out.positions = mesh_.positions;
    mesh_ = std::move(out);
    return loop;
}

void RetopoMesh::move_vertex(Index v, Vec3 p) {
    if (v < mesh_.vertex_count()) mesh_.positions[v] = snap(p);
}

void RetopoMesh::relax(int iterations, bool include_boundary) {
    std::unordered_map<std::uint64_t, int> uses;
    for (std::size_t f = 0; f < mesh_.face_count(); ++f) {
        auto fv = mesh_.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) ++uses[edge_key(fv[i], fv[(i + 1) % fv.size()])];
    }
    std::vector<char> boundary(mesh_.vertex_count(), 0);
    for (const auto& [key, count] : uses) {
        if (count == 1) boundary[key >> 32] = boundary[key & 0xFFFFFFFFu] = 1;
    }
    const VertexAdjacency adj = VertexAdjacency::build(mesh_);
    std::vector<Vec3> next(mesh_.vertex_count());
    for (int it = 0; it < iterations; ++it) {
        for (Index v = 0; v < mesh_.vertex_count(); ++v) {
            next[v] = mesh_.positions[v];
            auto nb = adj.of(v);
            if (nb.empty() || (boundary[v] && !include_boundary)) continue;
            Vec3 avg;
            for (Index u : nb) avg += mesh_.positions[u];
            Vec3 d = avg / float(nb.size()) - mesh_.positions[v];
            const Vec3 n = normal_at(mesh_.positions[v]);
            d -= n * dot(d, n);
            next[v] = snap(mesh_.positions[v] + d * 0.5f);
        }
        mesh_.positions.swap(next);
    }
}

void RetopoMesh::delete_face(Index f) {
    if (f >= mesh_.face_count()) return;
    Mesh out;
    out.positions = mesh_.positions;
    for (std::size_t g = 0; g < mesh_.face_count(); ++g) {
        if (g != f) out.add_face(mesh_.face(g));
    }
    mesh_ = std::move(out);
    compact();
}

void RetopoMesh::compact() {
    std::vector<Index> remap(mesh_.vertex_count(), kInvalidIndex);
    Mesh out;
    std::vector<Index> c;
    for (std::size_t f = 0; f < mesh_.face_count(); ++f) {
        c.clear();
        for (Index v : mesh_.face(f)) {
            if (remap[v] == kInvalidIndex) remap[v] = out.add_vertex(mesh_.positions[v]);
            c.push_back(remap[v]);
        }
        out.add_face(c);
    }
    for (Index& v : last_contour_) v = v < remap.size() ? remap[v] : kInvalidIndex;
    if (std::find(last_contour_.begin(), last_contour_.end(), kInvalidIndex) != last_contour_.end()) last_contour_.clear();
    mesh_ = std::move(out);
}

RetopoStats RetopoMesh::stats() const {
    RetopoStats s;
    s.vertices = mesh_.vertex_count();
    std::unordered_map<std::uint64_t, int> uses;
    for (std::size_t f = 0; f < mesh_.face_count(); ++f) {
        const std::size_t n = mesh_.face_size(f);
        if (n == 3) ++s.triangles;
        else if (n == 4) ++s.quads;
        else ++s.ngons;
        auto fv = mesh_.face(f);
        for (std::size_t i = 0; i < n; ++i) ++uses[edge_key(fv[i], fv[(i + 1) % n])];
    }
    std::vector<char> boundary(mesh_.vertex_count(), 0);
    for (const auto& [key, count] : uses) {
        if (count == 1) {
            ++s.boundary_edges;
            boundary[key >> 32] = boundary[key & 0xFFFFFFFFu] = 1;
        }
    }
    const VertexAdjacency adj = VertexAdjacency::build(mesh_);
    for (Index v = 0; v < mesh_.vertex_count(); ++v) {
        if (!boundary[v] && !adj.of(v).empty() && adj.of(v).size() != 4) ++s.poles;
    }
    return s;
}

}  // namespace cruth
