// SPDX-License-Identifier: MIT
#include "cruth/bvh.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace cruth {

namespace {

constexpr std::uint32_t kLeafSize = 4;
constexpr std::uint32_t kNoParent = ~std::uint32_t(0);
// A partial refit costs several times more per moved vertex than the full
// sweep does per vertex (leaf lookups, ancestor walks, a sort), so past this
// fraction of the vertices the full refit is cheaper.
constexpr std::size_t kPartialRefitDivisor = 16;

// Slab test; returns the entry distance or +inf on a miss.
float ray_box(const Bounds& b, Vec3 o, Vec3 inv_dir, float t_max) {
    float t0 = 0, t1 = t_max;
    for (int a = 0; a < 3; ++a) {
        float tn = (b.lo[a] - o[a]) * inv_dir[a];
        float tf = (b.hi[a] - o[a]) * inv_dir[a];
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return std::numeric_limits<float>::infinity();
    }
    return t0;
}

float box_distance_squared(const Bounds& b, Vec3 p) {
    float d2 = 0;
    for (int a = 0; a < 3; ++a) {
        float v = p[a] < b.lo[a] ? b.lo[a] - p[a] : (p[a] > b.hi[a] ? p[a] - b.hi[a] : 0.0f);
        d2 += v * v;
    }
    return d2;
}

// Segment pq against triangle abc (two-sided, excluding touching at endpoints).
bool segment_hits_triangle(Vec3 p, Vec3 q, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 dir = q - p;
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 pv = cross(dir, e2);
    const float det = dot(e1, pv);
    if (std::abs(det) < 1e-20f) return false;
    const float inv = 1.0f / det;
    const Vec3 tv = p - a;
    const float u = dot(tv, pv) * inv;
    if (u < 0 || u > 1) return false;
    const Vec3 qv = cross(tv, e1);
    const float v = dot(dir, qv) * inv;
    if (v < 0 || u + v > 1) return false;
    const float t = dot(e2, qv) * inv;
    return t > 1e-5f && t < 1 - 1e-5f;
}

}  // namespace

ClosestPoint closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    // Ericson, Real-Time Collision Detection, 5.1.5.
    auto make = [&](float u, float v, float w) {
        ClosestPoint cp;
        cp.point = a * u + b * v + c * w;
        cp.u = u;
        cp.v = v;
        cp.w = w;
        cp.distance = distance(p, cp.point);
        return cp;
    };
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return make(1, 0, 0);
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return make(0, 1, 0);
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        return make(1 - v, v, 0);
    }
    const Vec3 cp_ = p - c;
    const float d5 = dot(ab, cp_), d6 = dot(ac, cp_);
    if (d6 >= 0 && d5 <= d6) return make(0, 0, 1);
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / (d2 - d6);
        return make(1 - w, 0, w);
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return make(0, 1 - w, w);
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return make(1 - v - w, v, w);
}

void TriangleBVH::build(const Mesh& mesh) {
    positions_ = mesh.positions;
    triangles_.clear();
    triangle_face_.clear();
    triangles_.reserve(mesh.triangle_count());
    triangle_face_.reserve(mesh.triangle_count());
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            triangles_.push_back({fv[0], fv[i], fv[i + 1]});
            triangle_face_.push_back(static_cast<Index>(f));
        }
    }
    const auto n = static_cast<std::uint32_t>(triangles_.size());
    order_.resize(n);
    nodes_.clear();
    parent_.clear();
    vertex_leaf_offsets_.clear();
    vertex_leaves_.clear();
    node_stamp_.clear();
    if (n == 0) return;
    for (std::uint32_t i = 0; i < n; ++i) order_[i] = i;

    std::vector<Vec3> centroid(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto& t = triangles_[i];
        centroid[i] = (positions_[t[0]] + positions_[t[1]] + positions_[t[2]]) / 3.0f;
    }

    nodes_.reserve(2 * (n / kLeafSize + 1));
    nodes_.push_back({});
    struct Task {
        std::uint32_t node, first, count;
    };
    std::vector<Task> tasks{{0, 0, n}};
    while (!tasks.empty()) {
        Task task = tasks.back();
        tasks.pop_back();
        Bounds box, cbox;
        for (std::uint32_t i = task.first; i < task.first + task.count; ++i) {
            for (Index v : triangles_[order_[i]]) box.extend(positions_[v]);
            cbox.extend(centroid[order_[i]]);
        }
        nodes_[task.node].box = box;
        Vec3 extent = cbox.size();
        if (task.count <= kLeafSize || std::max({extent.x, extent.y, extent.z}) <= 0) {
            nodes_[task.node].first = task.first;
            nodes_[task.node].count = task.count;
            continue;
        }
        // Median split on the widest centroid axis.
        int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        std::uint32_t half = task.count / 2;
        auto begin = order_.begin() + task.first;
        std::nth_element(begin, begin + half, begin + task.count,
                         [&](Index a, Index b) { return centroid[a][axis] < centroid[b][axis]; });
        auto left = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back({});
        nodes_.push_back({});
        nodes_[task.node].first = left;
        nodes_[task.node].count = 0;
        tasks.push_back({left, task.first, half});
        tasks.push_back({left + 1, task.first + half, task.count - half});
    }
}

void TriangleBVH::refit_node(std::size_t i) {
    Node& n = nodes_[i];
    Bounds b;
    if (n.count > 0) {
        for (std::uint32_t k = 0; k < n.count; ++k) {
            for (Index v : triangles_[order_[n.first + k]]) b.extend(positions_[v]);
        }
    } else {
        b = nodes_[n.first].box;
        b.extend(nodes_[n.first + 1].box.lo);
        b.extend(nodes_[n.first + 1].box.hi);
    }
    n.box = b;
}

void TriangleBVH::refit(const std::vector<Vec3>& positions) {
    positions_ = positions;
    // Children always have larger indices than their parent, so a reverse
    // sweep updates children before parents.
    for (std::size_t i = nodes_.size(); i-- > 0;) refit_node(i);
}

void TriangleBVH::prepare_partial_refit() {
    const auto node_count = static_cast<std::uint32_t>(nodes_.size());
    parent_.assign(node_count, kNoParent);
    for (std::uint32_t i = 0; i < node_count; ++i) {
        if (nodes_[i].count == 0) parent_[nodes_[i].first] = parent_[nodes_[i].first + 1] = i;
    }
    // Leaves are visited in index order, so a vertex's repeated entries for
    // one leaf are consecutive and `last` drops them.
    const std::size_t vertex_count = positions_.size();
    std::vector<std::uint32_t> last(vertex_count, kNoParent);
    auto for_each_leaf_vertex = [&](auto&& fn) {
        for (std::uint32_t i = 0; i < node_count; ++i) {
            const Node& n = nodes_[i];
            for (std::uint32_t k = 0; k < n.count; ++k) {
                for (Index v : triangles_[order_[n.first + k]]) {
                    if (last[v] == i) continue;
                    last[v] = i;
                    fn(v, i);
                }
            }
        }
    };
    vertex_leaf_offsets_.assign(vertex_count + 1, 0);
    for_each_leaf_vertex([&](Index v, std::uint32_t) { ++vertex_leaf_offsets_[v + 1]; });
    for (std::size_t v = 0; v < vertex_count; ++v) vertex_leaf_offsets_[v + 1] += vertex_leaf_offsets_[v];
    vertex_leaves_.resize(vertex_leaf_offsets_.back());
    std::vector<std::uint32_t> fill(vertex_leaf_offsets_.begin(), vertex_leaf_offsets_.end() - 1);
    std::fill(last.begin(), last.end(), kNoParent);
    for_each_leaf_vertex([&](Index v, std::uint32_t leaf) { vertex_leaves_[fill[v]++] = leaf; });
    node_stamp_.assign(node_count, 0);
    stamp_ = 0;
}

void TriangleBVH::refit(const std::vector<Vec3>& positions, std::span<const Index> moved) {
    if (nodes_.empty() || positions.size() != positions_.size() ||
        moved.size() > positions_.size() / kPartialRefitDivisor) {
        refit(positions);
        return;
    }
    if (vertex_leaf_offsets_.empty()) prepare_partial_refit();
    if (++stamp_ == 0) {
        std::fill(node_stamp_.begin(), node_stamp_.end(), 0);
        stamp_ = 1;
    }
    // Dirty leaves: every leaf with a triangle on a moved vertex. Leaves
    // without one see the same positions as at their last refit, so their
    // boxes are already what a full refit would compute.
    dirty_.clear();
    for (Index v : moved) {
        if (v >= positions_.size()) {
            refit(positions);
            return;
        }
        positions_[v] = positions[v];
        for (std::uint32_t i = vertex_leaf_offsets_[v]; i < vertex_leaf_offsets_[v + 1]; ++i) {
            const std::uint32_t leaf = vertex_leaves_[i];
            if (node_stamp_[leaf] == stamp_) continue;
            node_stamp_[leaf] = stamp_;
            dirty_.push_back(leaf);
        }
    }
    // Their ancestors. A stamped inner node already has its whole chain to the
    // root stamped, so each walk stops at the first one.
    const std::size_t leaves = dirty_.size();
    for (std::size_t i = 0; i < leaves; ++i) {
        for (std::uint32_t p = parent_[dirty_[i]]; p != kNoParent && node_stamp_[p] != stamp_; p = parent_[p]) {
            node_stamp_[p] = stamp_;
            dirty_.push_back(p);
        }
    }
    // Children before parents, as in the full sweep. Min and max are exact, so
    // each recomputed box is bit-identical to the full refit's.
    std::sort(dirty_.begin(), dirty_.end(), std::greater<>());
    for (std::uint32_t i : dirty_) refit_node(i);
}

std::optional<RayHit> TriangleBVH::raycast(Vec3 origin, Vec3 direction, float max_distance) const {
    const Vec3 dir = normalize(direction);
    if (nodes_.empty() || length_squared(dir) == 0) return std::nullopt;
    const Vec3 inv{1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z};
    float best = max_distance;
    std::optional<RayHit> hit;

    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = nodes_[stack[--top]];
        if (ray_box(n.box, origin, inv, best) == std::numeric_limits<float>::infinity()) continue;
        if (n.count == 0) {
            // Visit the nearer child first.
            float tl = ray_box(nodes_[n.first].box, origin, inv, best);
            float tr = ray_box(nodes_[n.first + 1].box, origin, inv, best);
            if (tl < tr) {
                stack[top++] = n.first + 1;
                stack[top++] = n.first;
            } else {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
            continue;
        }
        for (std::uint32_t k = 0; k < n.count; ++k) {
            const Index t = order_[n.first + k];
            const auto& tri = triangles_[t];
            const Vec3 a = positions_[tri[0]], b = positions_[tri[1]], c = positions_[tri[2]];
            const Vec3 e1 = b - a, e2 = c - a;
            const Vec3 pv = cross(dir, e2);
            const float det = dot(e1, pv);
            if (std::abs(det) < 1e-12f) continue;
            const float inv_det = 1.0f / det;
            const Vec3 tv = origin - a;
            const float u = dot(tv, pv) * inv_det;
            if (u < 0 || u > 1) continue;
            const Vec3 qv = cross(tv, e1);
            const float v = dot(dir, qv) * inv_det;
            if (v < 0 || u + v > 1) continue;
            const float dist = dot(e2, qv) * inv_det;
            if (dist <= 0 || dist >= best) continue;
            best = dist;
            Vec3 normal = normalize(cross(e1, e2));
            if (dot(normal, dir) > 0) normal = -normal;
            hit = RayHit{origin + dir * dist, normal, dist, triangle_face_[t], t, u, v};
        }
    }
    return hit;
}

std::optional<ClosestPoint> TriangleBVH::closest_point(Vec3 p, float max_distance) const {
    if (nodes_.empty()) return std::nullopt;
    float best2 = max_distance >= 3.4e38f ? std::numeric_limits<float>::max() : max_distance * max_distance;
    std::optional<ClosestPoint> best;

    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = nodes_[stack[--top]];
        if (box_distance_squared(n.box, p) > best2) continue;
        if (n.count == 0) {
            float dl = box_distance_squared(nodes_[n.first].box, p);
            float dr = box_distance_squared(nodes_[n.first + 1].box, p);
            if (dl < dr) {
                stack[top++] = n.first + 1;
                stack[top++] = n.first;
            } else {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
            continue;
        }
        for (std::uint32_t k = 0; k < n.count; ++k) {
            const Index t = order_[n.first + k];
            const auto& tri = triangles_[t];
            ClosestPoint cp = closest_point_on_triangle(p, positions_[tri[0]], positions_[tri[1]], positions_[tri[2]]);
            if (cp.distance * cp.distance < best2) {
                best2 = cp.distance * cp.distance;
                cp.triangle = t;
                best = cp;
            }
        }
    }
    return best;
}

std::size_t count_self_intersections(const TriangleBVH& bvh, std::size_t stop_after) {
    std::size_t count = 0;
    const auto& pos = bvh.positions();
    for (Index t = 0; t < bvh.triangle_count(); ++t) {
        const auto& a = bvh.triangle(t);
        Bounds box;
        for (Index v : a) box.extend(pos[v]);
        bvh.query(box, [&](Index s) {
            if (s <= t) return;  // each unordered pair once
            const auto& b = bvh.triangle(s);
            for (Index x : a) {
                for (Index y : b) {
                    if (x == y) return;  // neighbours touch by construction
                }
            }
            const Vec3 A[3] = {pos[a[0]], pos[a[1]], pos[a[2]]};
            const Vec3 B[3] = {pos[b[0]], pos[b[1]], pos[b[2]]};
            bool hit = false;
            for (int i = 0; i < 3 && !hit; ++i) {
                hit = segment_hits_triangle(A[i], A[(i + 1) % 3], B[0], B[1], B[2]) ||
                      segment_hits_triangle(B[i], B[(i + 1) % 3], A[0], A[1], A[2]);
            }
            if (hit) ++count;
        });
        if (stop_after && count >= stop_after) break;
    }
    return count;
}

}  // namespace cruth
