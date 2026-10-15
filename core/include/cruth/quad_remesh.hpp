// Cruth core — automatic quad remeshing (PRD R-01, R-03, R-04).
//
// Field-aligned quad-dominant remeshing with QuadriFlow (Huang et al. 2018):
// an orientation field and a position field are solved on a multiresolution
// hierarchy, then integer flows remove most singularities before quads are
// extracted. Exposed behind this interface (the PRD's IRemesher) so other
// engines can be added; guide strokes (R-02) will feed its constraints.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

struct QuadRemeshOptions {
    std::size_t target_faces = 5000;
    bool preserve_sharp = false;     // keep creases sharper than ~60 degrees
    bool preserve_boundary = true;   // align to open borders
    bool adaptive = false;           // denser quads where curvature is high
    bool minimum_cost_flow = false;  // slower, fewer singularities
    int seed = 0;
    int relax_iterations = 3;        // tangential relax + project passes on the result
    // Inputs above this are decimated first so memory and time stay bounded.
    std::size_t max_input_triangles = 300000;
};

struct QuadRemeshStats {
    std::size_t input_triangles = 0;
    bool input_decimated = false;
    std::size_t faces = 0;
    std::size_t quads = 0;
};

Result<Mesh> quad_remesh(const Mesh& mesh, const QuadRemeshOptions& options = {}, QuadRemeshStats* stats = nullptr);

}  // namespace cruth
