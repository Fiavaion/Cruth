// Cruth core — Catmull-Clark subdivision.
//
// Phase-0 reference implementation used for primitives and as the test oracle
// for the OpenSubdiv-backed multires stack (PRD S-01) that replaces it.
// SPDX-License-Identifier: MIT
#pragma once

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

// One level of Catmull-Clark with smooth boundary rules. Output is all quads.
// Fails with NonManifold if the input cannot be represented as a half-edge mesh.
Result<Mesh> subdivide_catmull_clark(const Mesh& mesh);
Result<Mesh> subdivide_catmull_clark(const Mesh& mesh, int levels);

}  // namespace cruth
