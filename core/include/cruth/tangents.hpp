// Cruth core: MikkTSpace tangents for a UV-mapped mesh (PRD K-01).
//
// Game engines (Unity, Unreal, Blender, Godot) reconstruct tangent-space normals with MikkTSpace
// tangents, so the normal bake must be written in the same basis.
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"
#include "cruth/uv.hpp"

namespace cruth {

// One entry per triangle corner. Faces are split into triangles (0, i, i + 1), face by face, the
// same triangulation the baker uses; entry 3 * t + k is corner k of triangle t. The bitangent is
// sign * cross(normal, tangent), with the vertex normal from Mesh::vertex_normals().
struct CornerTangents {
    std::vector<Vec3> tangent;  // unit length, perpendicular to the vertex normal
    std::vector<float> sign;    // +1, or -1 where the UV mapping is mirrored
};

Result<CornerTangents> compute_tangents(const Mesh& mesh, const UVSet& uv);

}  // namespace cruth
