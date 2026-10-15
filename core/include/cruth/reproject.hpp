// Cruth core — projecting one surface onto another (PRD R-04, T-06, L-02).
//
// After a remesh, the new mesh is subdivided and its finest level projected
// onto the old high-resolution surface, so sculpted detail survives the
// topology change as multires detail on clean topology.
// SPDX-License-Identifier: MIT
#pragma once

#include "cruth/bvh.hpp"
#include "cruth/mesh.hpp"

namespace cruth {

struct ProjectionStats {
    std::size_t projected = 0;   // moved along their normal
    std::size_t fallback = 0;    // moved to the closest point instead
    float max_offset = 0;        // largest distance moved
};

// Moves every vertex onto `target` along its own vertex normal (whichever
// direction hits first within max_distance), falling back to the closest
// point when neither ray hits. Multithreaded.
ProjectionStats project_onto(Mesh& mesh, const TriangleBVH& target, float max_distance);

}  // namespace cruth
