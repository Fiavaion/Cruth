// Cruth core — 3D-print check and repair (PRD X-01, X-02).
//
// Covers topology, self-intersections, volume and size. Minimum wall
// thickness and overhang analysis need printer profiles (PRD X-03) and are
// reported as "not checked" until they land.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

#include "cruth/bvh.hpp"
#include "cruth/mesh.hpp"

namespace cruth {

struct PrintReport {
    std::size_t vertices = 0;
    std::size_t faces = 0;
    std::size_t triangles = 0;
    std::size_t shells = 0;                 // connected components
    std::size_t boundary_edges = 0;         // edges used by one face
    std::size_t holes = 0;                  // boundary loops
    std::size_t non_manifold_edges = 0;     // edges used by > 2 faces
    std::size_t non_manifold_vertices = 0;  // "bow-tie" vertices
    std::size_t inconsistent_edges = 0;     // neighbouring faces with opposite winding
    std::size_t degenerate_faces = 0;       // repeated corners or ~zero area
    std::size_t unreferenced_vertices = 0;
    std::size_t self_intersections = 0;     // intersecting triangle pairs
    double volume = 0;                      // signed; negative means inside-out
    double surface_area = 0;
    Bounds bounds;

    bool wall_thickness_checked = false;

    bool watertight() const { return boundary_edges == 0 && non_manifold_edges == 0; }
    bool printable() const {
        return watertight() && non_manifold_vertices == 0 && inconsistent_edges == 0 && degenerate_faces == 0 &&
               self_intersections == 0 && volume > 0;
    }
};

// Pass a BVH already built over `mesh` to reuse it; otherwise one is built.
PrintReport check_printability(const Mesh& mesh, const TriangleBVH* bvh = nullptr);

struct RepairOptions {
    bool weld = true;
    float weld_tolerance = 0.0f;  // 0 = merge bit-identical positions only
    bool remove_degenerate = true;
    bool remove_duplicate_faces = true;
    bool split_non_manifold = true;
    bool unify_orientation = true;
    bool fill_holes = true;
    bool orient_outward = true;
    bool remove_unreferenced = true;
};

struct RepairLog {
    std::size_t welded_vertices = 0;
    std::size_t removed_degenerate_faces = 0;
    std::size_t removed_duplicate_faces = 0;
    std::size_t split_vertices = 0;
    std::size_t flipped_faces = 0;
    std::size_t filled_holes = 0;
    std::size_t inverted_shells = 0;
    std::size_t removed_vertices = 0;
};

RepairLog repair_for_print(Mesh& mesh, const RepairOptions& options = {});

// Gives each fan of faces around a non-manifold vertex its own copy of the
// vertex, so faces meet only along manifold edges. Returns copies added.
std::size_t split_non_manifold_vertices(Mesh& mesh);

}  // namespace cruth
