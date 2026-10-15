// Cruth core — bounding volume hierarchy over a mesh's triangles.
//
// Used for picking, closest-point queries (voxel remesh, projection, baking)
// and overlap queries (self-intersection check). Polygons are fan-triangulated.
// After vertices move without a topology change, refit() updates the boxes in
// O(n) without rebuilding the tree.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "cruth/mesh.hpp"
#include "cruth/raycast.hpp"

namespace cruth {

struct ClosestPoint {
    Vec3 point;
    float distance = 0;
    Index triangle = kInvalidIndex;
    // Barycentric weights of `point` on the triangle (corner 0, 1, 2).
    float u = 0, v = 0, w = 0;
};

class TriangleBVH {
public:
    TriangleBVH() = default;
    explicit TriangleBVH(const Mesh& mesh) { build(mesh); }

    void build(const Mesh& mesh);
    void refit(const std::vector<Vec3>& positions);  // same topology, new positions
    bool empty() const { return triangles_.empty(); }

    std::size_t triangle_count() const { return triangles_.size(); }
    const std::array<Index, 3>& triangle(Index t) const { return triangles_[t]; }
    Index face_of(Index t) const { return triangle_face_[t]; }
    const std::vector<Vec3>& positions() const { return positions_; }

    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float max_distance = 3.4e38f) const;
    std::optional<ClosestPoint> closest_point(Vec3 p, float max_distance = 3.4e38f) const;

    // Calls fn(triangle) for every triangle whose box overlaps `box`.
    template <typename Fn>
    void query(const Bounds& box, Fn&& fn) const {
        if (nodes_.empty()) return;
        std::uint32_t stack[64];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Node& n = nodes_[stack[--top]];
            if (!overlaps(n.box, box)) continue;
            if (n.count > 0) {
                for (std::uint32_t i = 0; i < n.count; ++i) fn(order_[n.first + i]);
            } else {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
        }
    }

private:
    struct Node {
        Bounds box;
        std::uint32_t first = 0;  // leaf: first index into order_; inner: left child (right = first + 1)
        std::uint32_t count = 0;  // > 0 for leaves
    };

    static bool overlaps(const Bounds& a, const Bounds& b) {
        return a.lo.x <= b.hi.x && a.hi.x >= b.lo.x && a.lo.y <= b.hi.y && a.hi.y >= b.lo.y && a.lo.z <= b.hi.z &&
               a.hi.z >= b.lo.z;
    }

    std::vector<Vec3> positions_;
    std::vector<std::array<Index, 3>> triangles_;
    std::vector<Index> triangle_face_;
    std::vector<Index> order_;
    std::vector<Node> nodes_;
};

// Closest point on triangle abc to p, with barycentric weights.
ClosestPoint closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c);

// Number of triangle pairs that intersect without sharing a vertex.
std::size_t count_self_intersections(const TriangleBVH& bvh, std::size_t stop_after = 0);

}  // namespace cruth
