// SPDX-License-Identifier: MIT
#include "cruth/half_edge.hpp"

#include <string>
#include <unordered_map>

namespace cruth {

namespace {
std::uint64_t edge_key(Index a, Index b) { return (std::uint64_t(a) << 32) | b; }
}  // namespace

Result<HalfEdgeMesh> HalfEdgeMesh::build(const Mesh& mesh) {
    if (!mesh.is_valid()) return Error{ErrorCode::InvalidArgument, "mesh has out-of-range indices or faces with < 3 corners"};

    HalfEdgeMesh he;
    he.positions = mesh.positions;
    he.half_edges.resize(mesh.corner_count());
    he.face_half_edge.resize(mesh.face_count());
    he.vertex_half_edge.assign(mesh.vertex_count(), kInvalidIndex);

    std::unordered_map<std::uint64_t, Index> directed;
    directed.reserve(mesh.corner_count() * 2);

    Index h = 0;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        const Index first = h;
        const auto n = static_cast<Index>(fv.size());
        he.face_half_edge[f] = first;
        for (Index i = 0; i < n; ++i, ++h) {
            Index a = fv[i], b = fv[(i + 1) % n];
            if (a == b) return Error{ErrorCode::NonManifold, "face " + std::to_string(f) + " repeats a vertex"};
            auto& e = he.half_edges[h];
            e.origin = a;
            e.face = static_cast<Index>(f);
            e.next = first + (i + 1) % n;
            e.prev = first + (i + n - 1) % n;
            if (!directed.emplace(edge_key(a, b), h).second) {
                return Error{ErrorCode::NonManifold,
                             "edge " + std::to_string(a) + "-" + std::to_string(b) +
                                 " is shared by more than two faces or faces are inconsistently oriented"};
            }
        }
    }

    for (Index i = 0; i < he.half_edges.size(); ++i) {
        auto& e = he.half_edges[i];
        auto it = directed.find(edge_key(he.target(i), e.origin));
        if (it != directed.end()) e.twin = it->second;
        if (e.twin == kInvalidIndex || i < e.twin) {
            e.edge = static_cast<Index>(he.edge_count_++);
        } else {
            e.edge = he.half_edges[e.twin].edge;
        }
        // Prefer a boundary half-edge as the vertex's representative.
        Index& rep = he.vertex_half_edge[e.origin];
        if (rep == kInvalidIndex || e.twin == kInvalidIndex) rep = i;
    }
    return he;
}

std::size_t HalfEdgeMesh::face_size(Index f) const {
    std::size_t n = 0;
    Index start = face_half_edge[f], h = start;
    do {
        ++n;
        h = half_edges[h].next;
    } while (h != start);
    return n;
}

Mesh HalfEdgeMesh::to_mesh() const {
    Mesh m;
    m.reserve(positions.size(), face_count(), half_edges.size());
    m.positions = positions;
    std::vector<Index> corners;
    for (Index f = 0; f < face_count(); ++f) {
        corners.clear();
        Index start = face_half_edge[f], h = start;
        do {
            corners.push_back(half_edges[h].origin);
            h = half_edges[h].next;
        } while (h != start);
        m.add_face(corners);
    }
    return m;
}

}  // namespace cruth
