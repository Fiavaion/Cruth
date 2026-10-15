// SPDX-License-Identifier: MIT
#include "cruth/primitives.hpp"

#include <algorithm>

#include "cruth/subdivide.hpp"

namespace cruth {

Mesh make_cube(float size) {
    const float h = size * 0.5f;
    Mesh m;
    m.reserve(8, 6, 24);
    for (int i = 0; i < 8; ++i) {
        m.add_vertex({(i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h});
    }
    // Counter-clockwise when seen from outside.
    m.add_face({0, 2, 3, 1});  // -Z
    m.add_face({4, 5, 7, 6});  // +Z
    m.add_face({0, 1, 5, 4});  // -Y
    m.add_face({2, 6, 7, 3});  // +Y
    m.add_face({0, 4, 6, 2});  // -X
    m.add_face({1, 3, 7, 5});  // +X
    return m;
}

Mesh make_quad_sphere(int levels, float radius) {
    levels = std::clamp(levels, 0, 9);
    Mesh m = make_cube(2.0f);
    for (int i = 0; i < levels; ++i) {
        // A closed cube is always manifold, so subdivision cannot fail here.
        m = std::move(subdivide_catmull_clark(m)).value();
        // Re-project every level so the next level subdivides a round surface.
        for (auto& p : m.positions) p = normalize(p);
    }
    for (auto& p : m.positions) p = normalize(p) * radius;
    return m;
}

Mesh make_plane(int divisions, float size) {
    divisions = std::max(divisions, 1);
    const int n = divisions + 1;
    Mesh m;
    m.reserve(std::size_t(n) * n, std::size_t(divisions) * divisions, std::size_t(divisions) * divisions * 4);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            m.add_vertex({(float(x) / divisions - 0.5f) * size, 0.0f, (float(z) / divisions - 0.5f) * size});
        }
    }
    for (int z = 0; z < divisions; ++z) {
        for (int x = 0; x < divisions; ++x) {
            auto i = Index(z * n + x);
            m.add_face({i, i + Index(n), i + Index(n) + 1, i + 1});  // normal +Y
        }
    }
    return m;
}

}  // namespace cruth
