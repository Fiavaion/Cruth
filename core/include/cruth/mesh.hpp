// Cruth core — indexed polygon mesh.
//
// Mesh is the interchange representation: positions plus a flat list of
// polygons (any size >= 3). It tolerates non-manifold input so that import and
// the print checker can see broken files as they are. Topology-dependent
// algorithms build a HalfEdgeMesh (half_edge.hpp) from it.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "cruth/math.hpp"

namespace cruth {

using Index = std::uint32_t;
inline constexpr Index kInvalidIndex = 0xFFFFFFFFu;

class Mesh {
public:
    std::vector<Vec3> positions;

    std::size_t vertex_count() const { return positions.size(); }
    std::size_t face_count() const { return face_offsets_.size() - 1; }
    std::size_t corner_count() const { return face_indices_.size(); }

    std::span<const Index> face(std::size_t f) const {
        return {face_indices_.data() + face_offsets_[f], face_offsets_[f + 1] - face_offsets_[f]};
    }
    std::size_t face_size(std::size_t f) const { return face_offsets_[f + 1] - face_offsets_[f]; }

    Index add_vertex(Vec3 p) {
        positions.push_back(p);
        return static_cast<Index>(positions.size() - 1);
    }
    void add_face(std::span<const Index> vertices);
    void add_face(std::initializer_list<Index> vertices) {
        add_face(std::span<const Index>(vertices.begin(), vertices.size()));
    }
    void clear_faces();
    void clear();
    void reserve(std::size_t vertices, std::size_t faces, std::size_t corners);

    // Number of triangles after fan triangulation of every polygon.
    std::size_t triangle_count() const;
    // Fan-triangulated index buffer (3 indices per triangle), as a renderer wants it.
    std::vector<Index> triangulated_indices() const;

    Vec3 face_normal(std::size_t f) const;  // unit, Newell's method
    float face_area(std::size_t f) const;
    Vec3 face_centroid(std::size_t f) const;
    // Area-weighted vertex normals.
    std::vector<Vec3> vertex_normals() const;
    Bounds bounds() const;

    // Every face references an existing vertex and has >= 3 corners.
    bool is_valid() const;

    const std::vector<Index>& face_offsets() const { return face_offsets_; }
    const std::vector<Index>& face_indices() const { return face_indices_; }

private:
    std::vector<Index> face_offsets_{0};
    std::vector<Index> face_indices_;
};

// Vertex -> neighbouring vertices across polygon edges (compressed rows).
struct VertexAdjacency {
    std::vector<Index> offsets;
    std::vector<Index> neighbors;

    std::span<const Index> of(Index v) const {
        return {neighbors.data() + offsets[v], offsets[v + 1] - offsets[v]};
    }
    static VertexAdjacency build(const Mesh& mesh);
};

}  // namespace cruth
