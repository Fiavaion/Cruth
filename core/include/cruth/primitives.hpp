// Cruth core — starting shapes ("first blob").
// SPDX-License-Identifier: MIT
#pragma once

#include "cruth/mesh.hpp"

namespace cruth {

// Axis-aligned cube centred on the origin, 6 outward-facing quads.
Mesh make_cube(float size = 1.0f);

// Quad sphere: a cube subdivided `levels` times and projected onto the sphere.
// Quads only, evenly distributed, no poles — the standard sculpting base.
// levels is clamped to [0, 9]; level 6 has 24,578 vertices.
Mesh make_quad_sphere(int levels = 5, float radius = 1.0f);

// Flat grid in the XZ plane facing +Y, `divisions` quads per side.
Mesh make_plane(int divisions = 16, float size = 2.0f);

}  // namespace cruth
