// Cruth core — mesh import/export (PRD P-06, P-07, X-04).
//
// Phase 0 covers the geometry-only formats: OBJ, STL (ASCII and binary) and
// PLY (ASCII and binary little-endian). glTF/USD/3MF follow with the scene layer.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"
#include "cruth/uv.hpp"

namespace cruth {

enum class MeshFormat { Unknown, Obj, Stl, Ply };

MeshFormat format_from_path(std::string_view path);

// OBJ texture coordinates (vt) are read into `uvs` when every face corner has one.
Result<Mesh> read_obj(std::string_view text, UVSet* uvs = nullptr);
Result<Mesh> read_stl(std::string_view bytes);  // detects ASCII vs binary
Result<Mesh> read_ply(std::string_view bytes);

std::string write_obj(const Mesh& mesh, const UVSet* uvs = nullptr);
std::string write_stl_binary(const Mesh& mesh);  // triangulates polygons
std::string write_ply_binary(const Mesh& mesh);  // polygons must have <= 255 corners

// File helpers dispatch on the extension.
// UVs are read from and written to OBJ only.
Result<Mesh> load_mesh(const std::string& path, UVSet* uvs = nullptr);
Status save_mesh(const Mesh& mesh, const std::string& path, const UVSet* uvs = nullptr);

// Merge vertices whose positions are within `tolerance` (0 = bit-exact) and
// remap faces; STL import uses this because STL stores unshared triangles.
std::size_t weld_vertices(Mesh& mesh, float tolerance = 0.0f);

}  // namespace cruth
