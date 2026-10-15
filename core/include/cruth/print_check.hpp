// Cruth core — 3D-print check and repair (PRD X-01, X-02).
//
// Phase 0 covers the topological checks and repairs. Self-intersection,
// minimum wall thickness and overhang analysis need the BVH and printer
// profiles and are reported as "not checked" until they land.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

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
    double volume = 0;                      // signed; negative means inside-out
    double surface_area = 0;
    Bounds bounds;

    bool self_intersections_checked = false;
    bool wall_thickness_checked = false;

    bool watertight() const { return boundary_edges == 0 && non_manifold_edges == 0; }
    bool printable() const {
        return watertight() && non_manifold_vertices == 0 && inconsistent_edges == 0 && degenerate_faces == 0 &&
               volume > 0;
    }
};

PrintReport check_printability(const Mesh& mesh);

struct RepairOptions {
    bool weld = true;
    float weld_tolerance = 0.0f;  // 0 = merge bit-identical positions only
    bool remove_degenerate = true;
    bool remove_duplicate_faces = true;
    bool unify_orientation = true;
    bool fill_holes = true;
    bool orient_outward = true;
    bool remove_unreferenced = true;
};

struct RepairLog {
    std::size_t welded_vertices = 0;
    std::size_t removed_degenerate_faces = 0;
    std::size_t removed_duplicate_faces = 0;
    std::size_t flipped_faces = 0;
    std::size_t filled_holes = 0;
    std::size_t inverted_shells = 0;
    std::size_t removed_vertices = 0;
};

RepairLog repair_for_print(Mesh& mesh, const RepairOptions& options = {});

}  // namespace cruth
