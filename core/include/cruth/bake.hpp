// Cruth core — texture baking from a high-resolution mesh onto a UV-mapped
// low-resolution mesh (PRD K-01, K-02).
//
// For every texel covered by a low-poly triangle, a ray is cast from the cage
// (the low surface pushed out along its smooth normal by max_distance) back
// along the normal, and the first hit on the high mesh supplies the value.
// Uncovered texels around charts are filled by dilation so mip-mapping and
// filtering do not bleed background into the surface.
//
// CPU reference baker; the Metal compute baker (K-03) must match its output.
// SPDX-License-Identifier: MIT
#pragma once

#include <span>
#include <vector>

#include "cruth/image_io.hpp"
#include "cruth/mesh.hpp"
#include "cruth/result.hpp"
#include "cruth/uv.hpp"

namespace cruth {

// Tangent frames are MikkTSpace (cruth/tangents.hpp). Decode rules:
//  - TangentNormal: the engine's pixel shader, with the unnormalised interpolated tangent vT and
//    vertex normal vN, vB = sign * cross(vN, vT): normalize(tn.x * vT + tn.y * vB + tn.z * vN).
//  - VectorDisplacement: an orthonormal unit frame, n = normalize(vN), T = normalize(vT - n (n . vT)),
//    B = sign * cross(n, T): offset = d.x * T + d.y * B + d.z * n.
enum class BakeMap {
    TangentNormal,      // RGB, tangent space, OpenGL (+Y) convention, 0.5-biased
    ObjectNormal,        // RGB, object space, 0.5-biased
    Height,              // 1 channel, signed distance along the low normal (object units)
    VectorDisplacement,  // RGB, tangent-space offset low -> high (object units)
    AmbientOcclusion,    // 1 channel, 1 = fully open
};

const char* bake_map_name(BakeMap map);

struct BakeOptions {
    int resolution = 1024;     // texels per tile side
    float max_distance = 0;    // cage offset and search range; 0 = 3% of the high mesh's diagonal
    int padding = 8;           // dilation passes around charts
    int ao_samples = 32;
    float ao_distance = 0;     // 0 = 15% of the high mesh's diagonal
};

struct BakedMap {
    BakeMap map;
    int tile = 0;   // UDIM tile index; file names use 1001 + tile
    Image image;
};

struct BakeStats {
    std::size_t texels = 0;  // covered texels across tiles
    std::size_t misses = 0;  // covered texels whose ray found no high surface
    float max_distance = 0;
};

Result<std::vector<BakedMap>> bake(const Mesh& low, const UVSet& uv, const Mesh& high, std::span<const BakeMap> maps,
                                   const BakeOptions& options = {}, BakeStats* stats = nullptr);

}  // namespace cruth
