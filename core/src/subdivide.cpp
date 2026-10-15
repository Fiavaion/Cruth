// SPDX-License-Identifier: MIT
#include "cruth/subdivide.hpp"

#include "cruth/half_edge.hpp"

namespace cruth {

Result<Mesh> subdivide_catmull_clark(const Mesh& mesh) {
    auto built = HalfEdgeMesh::build(mesh);
    if (!built) return built.error();
    const HalfEdgeMesh& he = built.value();

    const std::size_t V = he.vertex_count(), F = he.face_count(), E = he.edge_count();
    const auto face_base = static_cast<Index>(V);
    const auto edge_base = static_cast<Index>(V + F);

    Mesh out;
    out.reserve(V + F + E, he.half_edges.size(), he.half_edges.size() * 4);
    out.positions.resize(V + F + E);

    // Face points: centroids.
    for (Index f = 0; f < F; ++f) {
        Vec3 c;
        float n = 0;
        Index start = he.face_half_edge[f], h = start;
        do {
            c += he.positions[he.half_edges[h].origin];
            n += 1;
            h = he.half_edges[h].next;
        } while (h != start);
        out.positions[face_base + f] = c / n;
    }

    // Edge points, plus the per-vertex sums the vertex rule needs.
    std::vector<Vec3> face_sum(V), mid_sum(V), boundary_sum(V);
    std::vector<int> face_n(V, 0), edge_n(V, 0), boundary_n(V, 0);

    for (Index h = 0; h < he.half_edges.size(); ++h) {
        const HalfEdge& e = he.half_edges[h];
        face_sum[e.origin] += out.positions[face_base + e.face];
        ++face_n[e.origin];

        if (!he.is_boundary(h) && e.twin < h) continue;  // visit each edge once
        Index a = e.origin, b = he.target(h);
        Vec3 pa = he.positions[a], pb = he.positions[b];
        Vec3 mid = (pa + pb) * 0.5f;
        Vec3 point = mid;
        if (!he.is_boundary(h)) {
            Vec3 f1 = out.positions[face_base + e.face];
            Vec3 f2 = out.positions[face_base + he.half_edges[e.twin].face];
            point = (pa + pb + f1 + f2) * 0.25f;
        } else {
            boundary_sum[a] += pb;
            boundary_sum[b] += pa;
            ++boundary_n[a];
            ++boundary_n[b];
        }
        out.positions[edge_base + e.edge] = point;
        mid_sum[a] += mid;
        mid_sum[b] += mid;
        ++edge_n[a];
        ++edge_n[b];
    }

    // Vertex points.
    for (Index v = 0; v < V; ++v) {
        Vec3 p = he.positions[v];
        if (boundary_n[v] == 0 && edge_n[v] >= 3) {
            auto n = static_cast<float>(edge_n[v]);
            Vec3 Q = face_sum[v] / static_cast<float>(face_n[v]);
            Vec3 R = mid_sum[v] / n;
            out.positions[v] = (Q + R * 2.0f + p * (n - 3.0f)) / n;
        } else if (boundary_n[v] == 2) {
            out.positions[v] = (boundary_sum[v] + p * 6.0f) / 8.0f;
        } else {
            out.positions[v] = p;  // isolated, corner-of-bowtie or degenerate: keep fixed
        }
    }

    // One quad per face corner: corner -> next edge point -> face point -> previous edge point.
    for (Index f = 0; f < F; ++f) {
        Index start = he.face_half_edge[f], h = start;
        do {
            const HalfEdge& e = he.half_edges[h];
            out.add_face({e.origin, edge_base + e.edge, face_base + f,
                          edge_base + he.half_edges[e.prev].edge});
            h = e.next;
        } while (h != start);
    }
    return out;
}

Result<Mesh> subdivide_catmull_clark(const Mesh& mesh, int levels) {
    if (levels < 0) return Error{ErrorCode::InvalidArgument, "levels must be >= 0"};
    Mesh current = mesh;
    for (int i = 0; i < levels; ++i) {
        auto next = subdivide_catmull_clark(current);
        if (!next) return next.error();
        current = std::move(next).value();
    }
    return current;
}

}  // namespace cruth
