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
#include <vector>

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

    // Guide strokes (PRD R-02): polylines on the surface along which edge
    // loops should run. Nearby orientation-field directions are pulled toward
    // each stroke's tangent; 4-fold symmetry means loops run along or across it.
    std::vector<std::vector<Vec3>> guide_strokes;
    float guide_radius = 0;      // influence radius; 0 = 1.5 target edge lengths
    float guide_strength = 1.0f; // 0..1 at the stroke, falling off to 0 at the radius
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
