// Cruth core — voxel remesh (PRD S-02).
//
// Rebuilds a mesh as a closed, evenly spaced quad-dominant surface:
// 1. narrow-band unsigned distance from a BVH, signed with angle-weighted
//    pseudonormals;
// 2. exterior flood fill, so holes smaller than a voxel close and buried
//    surfaces from self-intersections are dissolved;
// 3. surface-nets extraction (one quad per sign-changing grid edge);
// 4. a few relax-and-project passes back onto the source for crisp forms.
//
// CPU reference implementation; the Metal compute path must match its output.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

struct VoxelRemeshOptions {
    int resolution = 128;            // voxels along the longest bounding-box side, 8..512
    int relax_iterations = 2;        // relax + project passes after extraction
    bool project_to_source = true;
    std::size_t max_input_triangles = 1500000;  // decimated first above this (PRD R-03 input cap)
};

struct VoxelRemeshStats {
    int grid[3] = {0, 0, 0};
    float voxel_size = 0;
    std::size_t band_points = 0;
    bool input_decimated = false;
};

Result<Mesh> voxel_remesh(const Mesh& mesh, const VoxelRemeshOptions& options = {},
                          VoxelRemeshStats* stats = nullptr);

}  // namespace cruth
