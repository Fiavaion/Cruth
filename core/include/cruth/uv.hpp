// Cruth core — UV layouts and automatic unwrapping (PRD U-01, U-04, U-06).
//
// A UVSet stores texture coordinates per face corner, so seams are simply
// corners of one mesh vertex that use different UV indices.
//
// auto_unwrap():
// 1. charts: region growing inside each seam group (face groups or painted
//    seams), limited by a normal cone so every chart is disk-like;
// 2. parameterisation: least-squares conformal maps (LSCM), solved as the
//    symmetric conformal energy E_D - A with preconditioned conjugate gradient;
// 3. packing: each chart scaled to true area, rotated to its tightest box,
//    shelf-packed with texel padding into one or more UDIM tiles.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <vector>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

struct UVSet {
    std::vector<Vec2> uvs;
    std::vector<Index> corner_uv;   // parallel to Mesh::face_indices()
    std::vector<Index> face_chart;  // chart id per face
    std::size_t chart_count = 0;
    int tiles = 1;                  // UDIM tiles along U: tile t spans u in [t, t + 1)

    bool matches(const Mesh& mesh) const { return corner_uv.size() == mesh.corner_count(); }
    Vec2 corner(const Mesh& mesh, std::size_t face, std::size_t i) const {
        return uvs[corner_uv[mesh.face_offsets()[face] + i]];
    }
};

struct UnwrapOptions {
    // Optional seam groups, one per face: charts never cross group borders.
    std::vector<int> face_groups;
    float max_chart_angle_degrees = 60.0f;  // normal cone half-angle per chart
    int resolution = 2048;                  // texture size per tile, for padding
    int padding_texels = 4;
    int tiles = 1;                          // UDIM tiles to spread charts over
};

struct UnwrapStats {
    std::size_t charts = 0;
    float texels_per_unit = 0;   // texel density at `resolution`
    float utilisation = 0;       // chart area / tile area, averaged over tiles
    float max_stretch = 0;       // worst per-face area ratio (uv / 3D, normalised)
};

Result<UVSet> auto_unwrap(const Mesh& mesh, const UnwrapOptions& options = {}, UnwrapStats* stats = nullptr);

// Per-face area distortion: (uv area / 3D area) / (total uv area / total 3D
// area). 1 is ideal; used for heat maps (PRD U-06).
std::vector<float> uv_area_distortion(const Mesh& mesh, const UVSet& uv);

// Number of faces whose UV winding is flipped relative to the majority.
std::size_t count_flipped_uv_faces(const Mesh& mesh, const UVSet& uv);

}  // namespace cruth
