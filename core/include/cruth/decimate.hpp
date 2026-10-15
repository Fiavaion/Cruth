// Cruth core — decimation to a target triangle count (PRD S-04).
//
// Garland–Heckbert quadric error edge collapse with a link-condition check so
// manifold input stays manifold, a normal-flip guard, and boundary-preserving
// penalty planes. Output is all triangles.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

#include "cruth/mesh.hpp"

namespace cruth {

struct DecimateOptions {
    std::size_t target_triangles = 10000;
    bool preserve_boundary = true;
    // Smallest allowed cosine between a face normal before and after a collapse.
    float min_normal_cosine = 0.2f;
};

struct DecimateStats {
    std::size_t collapses = 0;
    std::size_t triangles_before = 0;
    std::size_t triangles_after = 0;
};

Mesh decimate(const Mesh& mesh, const DecimateOptions& options, DecimateStats* stats = nullptr);

}  // namespace cruth
