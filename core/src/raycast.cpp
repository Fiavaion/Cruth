// SPDX-License-Identifier: MIT
#include "cruth/raycast.hpp"

#include <limits>

namespace cruth {

std::optional<RayHit> raycast(const Mesh& mesh, Vec3 origin, Vec3 direction) {
    Vec3 dir = normalize(direction);
    if (length_squared(dir) == 0) return std::nullopt;

    float best = std::numeric_limits<float>::max();
    std::optional<RayHit> hit;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            // Möller–Trumbore, two-sided.
            const Vec3 a = mesh.positions[fv[0]], b = mesh.positions[fv[i]], c = mesh.positions[fv[i + 1]];
            const Vec3 e1 = b - a, e2 = c - a;
            const Vec3 pv = cross(dir, e2);
            const float det = dot(e1, pv);
            if (std::abs(det) < 1e-12f) continue;
            const float inv = 1.0f / det;
            const Vec3 tv = origin - a;
            const float u = dot(tv, pv) * inv;
            if (u < 0 || u > 1) continue;
            const Vec3 qv = cross(tv, e1);
            const float v = dot(dir, qv) * inv;
            if (v < 0 || u + v > 1) continue;
            const float t = dot(e2, qv) * inv;
            if (t <= 0 || t >= best) continue;
            best = t;
            Vec3 n = normalize(cross(e1, e2));
            if (dot(n, dir) > 0) n = -n;
            hit = RayHit{origin + dir * t, n, t, static_cast<Index>(f)};
        }
    }
    return hit;
}

}  // namespace cruth
