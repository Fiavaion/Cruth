// SPDX-License-Identifier: MIT
#include "cruth/reproject.hpp"

#include <algorithm>
#include <atomic>

#include "cruth/parallel.hpp"

namespace cruth {

ProjectionStats project_onto(Mesh& mesh, const TriangleBVH& target, float max_distance) {
    const std::vector<Vec3> normals = mesh.vertex_normals();
    std::vector<Vec3> out(mesh.positions.size());
    std::vector<float> moved(mesh.positions.size(), 0.0f);
    std::atomic<std::size_t> projected{0}, fallback{0};
    parallel_for(mesh.positions.size(), [&](std::size_t begin, std::size_t end) {
        for (std::size_t v = begin; v < end; ++v) {
            const Vec3 p = mesh.positions[v];
            const Vec3 n = normals[v];
            Vec3 q = p;
            bool hit = false;
            if (length_squared(n) > 0) {
                auto up = target.raycast(p, n, max_distance);
                auto down = target.raycast(p, -n, max_distance);
                if (up && (!down || up->distance <= down->distance)) {
                    q = up->position;
                    hit = true;
                } else if (down) {
                    q = down->position;
                    hit = true;
                }
            }
            if (!hit) {
                if (auto cp = target.closest_point(p)) q = cp->point;
                ++fallback;
            } else {
                ++projected;
            }
            out[v] = q;
            moved[v] = distance(p, q);
        }
    });
    mesh.positions.swap(out);
    ProjectionStats stats;
    stats.projected = projected;
    stats.fallback = fallback;
    stats.max_offset = moved.empty() ? 0.0f : *std::max_element(moved.begin(), moved.end());
    return stats;
}

}  // namespace cruth
