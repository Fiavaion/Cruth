// Cruth core — half-edge connectivity (PRD S-01).
//
// Built from a Mesh when an algorithm needs manifold topology (subdivision,
// later remeshing, retopology and UV seams). Building fails cleanly on
// non-manifold or inconsistently oriented input instead of producing garbage.
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

struct HalfEdge {
    Index origin = kInvalidIndex;  // vertex the half-edge leaves
    Index next = kInvalidIndex;
    Index prev = kInvalidIndex;
    Index twin = kInvalidIndex;  // kInvalidIndex on a boundary
    Index face = kInvalidIndex;
    Index edge = kInvalidIndex;  // undirected edge id shared with twin
};

class HalfEdgeMesh {
public:
    static Result<HalfEdgeMesh> build(const Mesh& mesh);

    std::vector<Vec3> positions;
    std::vector<HalfEdge> half_edges;
    std::vector<Index> face_half_edge;    // one half-edge per face
    std::vector<Index> vertex_half_edge;  // one outgoing half-edge, kInvalidIndex if isolated

    std::size_t vertex_count() const { return positions.size(); }
    std::size_t face_count() const { return face_half_edge.size(); }
    std::size_t edge_count() const { return edge_count_; }

    Index target(Index h) const { return half_edges[half_edges[h].next].origin; }
    bool is_boundary(Index h) const { return half_edges[h].twin == kInvalidIndex; }
    std::size_t face_size(Index f) const;

    Mesh to_mesh() const;

private:
    std::size_t edge_count_ = 0;
};

}  // namespace cruth
