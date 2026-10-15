// Cruth core — ray picking for stroke input and focus-on-tap (PRD N-01).
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

#include "cruth/mesh.hpp"

namespace cruth {

struct RayHit {
    Vec3 position;
    Vec3 normal;  // geometric normal of the hit triangle, facing the ray origin
    float distance = 0;
    Index face = kInvalidIndex;
};

// Nearest hit along origin + t * direction, t > 0. Brute force over the fan
// triangulation; a refittable BVH replaces this before meshes reach millions
// of triangles (tracked in docs/ROADMAP.md).
std::optional<RayHit> raycast(const Mesh& mesh, Vec3 origin, Vec3 direction);

}  // namespace cruth
