// SPDX-License-Identifier: MIT
#include "cruth/bake.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "cruth/bvh.hpp"
#include "cruth/parallel.hpp"
#include "cruth/tangents.hpp"

namespace cruth {

const char* bake_map_name(BakeMap map) {
    switch (map) {
        case BakeMap::TangentNormal: return "normal";
        case BakeMap::ObjectNormal: return "object_normal";
        case BakeMap::Height: return "height";
        case BakeMap::VectorDisplacement: return "vdm";
        case BakeMap::AmbientOcclusion: return "ao";
    }
    return "map";
}

namespace {

int channels_of(BakeMap map) {
    return map == BakeMap::Height || map == BakeMap::AmbientOcclusion ? 1 : 3;
}

struct LowTriangle {
    Index v[3];  // mesh vertices
    Vec2 uv[3];  // tile-local uv
    int tile;
};

// What a texel sees: the low triangle covering its centre and barycentrics.
struct TexelRef {
    Index triangle = kInvalidIndex;
    float b1 = 0, b2 = 0;
};

// Small deterministic hash for per-texel AO sample rotation.
float hash01(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x) / 4294967296.0f;
}

}  // namespace

Result<std::vector<BakedMap>> bake(const Mesh& low, const UVSet& uv, const Mesh& high, std::span<const BakeMap> maps,
                                   const BakeOptions& options, BakeStats* stats) {
    if (!uv.matches(low)) return Error{ErrorCode::InvalidArgument, "bake: the low mesh has no matching UV set"};
    if (high.face_count() == 0) return Error{ErrorCode::InvalidArgument, "bake: the high mesh is empty"};
    if (maps.empty()) return Error{ErrorCode::InvalidArgument, "bake: no maps requested"};
    const int res = std::clamp(options.resolution, 16, 8192);

    const TriangleBVH bvh(high);
    const std::vector<Vec3> high_normals = high.vertex_normals();
    const std::vector<Vec3> low_normals = low.vertex_normals();
    const float diagonal = length(high.bounds().size());
    const float reach = options.max_distance > 0 ? options.max_distance : 0.03f * diagonal;
    const float ao_reach = options.ao_distance > 0 ? options.ao_distance : 0.15f * diagonal;

    // Low triangles with tile-local UVs.
    std::vector<LowTriangle> tris;
    int tiles = std::max(1, uv.tiles);
    for (std::size_t f = 0; f < low.face_count(); ++f) {
        auto fv = low.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            LowTriangle t;
            const std::size_t c[3] = {0, i, i + 1};
            float min_u = 3.4e38f;
            for (int k = 0; k < 3; ++k) {
                t.v[k] = fv[c[k]];
                t.uv[k] = uv.corner(low, f, c[k]);
                min_u = std::min(min_u, t.uv[k].x);
            }
            t.tile = std::max(0, int(std::floor(min_u + 1e-6f)));
            tiles = std::max(tiles, t.tile + 1);
            for (auto& p : t.uv) p.x -= float(t.tile);
            tris.push_back(t);
        }
    }

    // Same triangulation as `tris`, so entry 3 * t + k belongs to tris[t].v[k].
    auto tangents = compute_tangents(low, uv);
    if (!tangents) return tangents.error();
    const CornerTangents& corner_tangents = tangents.value();

    std::vector<BakedMap> out;
    std::size_t covered_total = 0, miss_total = 0;
    for (int tile = 0; tile < tiles; ++tile) {
        // Rasterise the tile's triangles at texel centres (v up, rows top-down).
        std::vector<TexelRef> texels(std::size_t(res) * res);
        bool any = false;
        for (Index ti = 0; ti < tris.size(); ++ti) {
            const LowTriangle& t = tris[ti];
            if (t.tile != tile) continue;
            any = true;
            Vec2 p[3];
            for (int k = 0; k < 3; ++k) p[k] = {t.uv[k].x * res - 0.5f, (1.0f - t.uv[k].y) * res - 0.5f};
            const float area = cross(p[1] - p[0], p[2] - p[0]);
            if (std::abs(area) < 1e-12f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
            const int x1 = std::min(res - 1, int(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
            const int y0 = std::max(0, int(std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
            const int y1 = std::min(res - 1, int(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const Vec2 q{float(x), float(y)};
                    const float b1 = cross(q - p[0], p[2] - p[0]) / area;
                    const float b2 = cross(p[1] - p[0], q - p[0]) / area;
                    const float b0 = 1 - b1 - b2;
                    const float eps = -1e-4f;
                    if (b0 < eps || b1 < eps || b2 < eps) continue;
                    texels[std::size_t(y) * res + x] = {ti, b1, b2};
                }
            }
        }
        if (!any) continue;

        // Background takes each map's neutral value: flat normal, open AO, zero offset.
        std::vector<Image> images;
        for (BakeMap m : maps) {
            Image img(res, res, channels_of(m));
            if (m == BakeMap::TangentNormal || m == BakeMap::ObjectNormal) {
                const float flat[3] = {0.5f, 0.5f, m == BakeMap::TangentNormal ? 1.0f : 0.5f};
                for (std::size_t i = 0; i < img.pixels.size(); ++i) img.pixels[i] = flat[i % 3];
            } else if (m == BakeMap::AmbientOcclusion) {
                std::fill(img.pixels.begin(), img.pixels.end(), 1.0f);
            }
            images.push_back(std::move(img));
        }
        std::vector<char> filled(texels.size(), 0);
        std::vector<std::size_t> misses(std::size_t(res), 0), covered(std::size_t(res), 0);

        parallel_for(
            std::size_t(res),
            [&](std::size_t row_begin, std::size_t row_end) {
                for (std::size_t y = row_begin; y < row_end; ++y) {
                    for (int x = 0; x < res; ++x) {
                        const std::size_t id = y * std::size_t(res) + std::size_t(x);
                        const TexelRef& ref = texels[id];
                        if (ref.triangle == kInvalidIndex) continue;
                        ++covered[y];
                        const LowTriangle& t = tris[ref.triangle];
                        const float b0 = 1 - ref.b1 - ref.b2;
                        const Vec3 P[3] = {low.positions[t.v[0]], low.positions[t.v[1]], low.positions[t.v[2]]};
                        const Vec3 p = P[0] * b0 + P[1] * ref.b1 + P[2] * ref.b2;
                        Vec3 n = normalize(low_normals[t.v[0]] * b0 + low_normals[t.v[1]] * ref.b1 +
                                           low_normals[t.v[2]] * ref.b2);
                        if (length_squared(n) == 0) n = normalize(cross(P[1] - P[0], P[2] - P[0]));

                        // MikkTSpace frame, the exact inverse of the engine-side decode: the unnormalised
                        // interpolated tangent vT and normal vN, vB = sign * cross(vN, vT). The sign comes
                        // from the nearest corner. A direction d maps to tangent space as
                        // (d . row0, d . row1, d . row2) / det with row0 = vB x vN, row1 = vN x vT, row2 = vT x vB.
                        const std::size_t c0 = std::size_t(ref.triangle) * 3;
                        const Vec3 vN = length_squared(low_normals[t.v[0]] * b0 + low_normals[t.v[1]] * ref.b1 +
                                                       low_normals[t.v[2]] * ref.b2) == 0
                                            ? n
                                            : low_normals[t.v[0]] * b0 + low_normals[t.v[1]] * ref.b1 +
                                                  low_normals[t.v[2]] * ref.b2;
                        Vec3 vT = corner_tangents.tangent[c0] * b0 + corner_tangents.tangent[c0 + 1] * ref.b1 +
                                  corner_tangents.tangent[c0 + 2] * ref.b2;
                        if (length_squared(cross(vN, vT)) < 1e-12f) {
                            // Degenerate corner tangents: any vector perpendicular to the normal.
                            vT = cross(n, std::abs(n.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
                        }
                        const std::size_t nearest = b0 >= ref.b1 && b0 >= ref.b2 ? 0 : (ref.b1 >= ref.b2 ? 1 : 2);
                        const Vec3 vB = cross(vN, vT) * corner_tangents.sign[c0 + nearest];
                        const Vec3 row0 = cross(vB, vN), row1 = cross(vN, vT), row2 = cross(vT, vB);
                        const float det = dot(vT, row0);
                        const float inv_det = std::abs(det) > 1e-20f ? 1.0f / det : 0.0f;
                        auto to_tangent_space = [&](Vec3 d) { return Vec3{dot(d, row0), dot(d, row1), dot(d, row2)} * inv_det; };

                        // First hit from the cage inward.
                        auto hit = bvh.raycast(p + n * reach, -n, 2.0f * reach);
                        if (!hit) {
                            ++misses[y];
                            continue;
                        }
                        filled[id] = 1;
                        const auto& ht = bvh.triangle(hit->triangle);
                        const float hb0 = 1 - hit->u - hit->v;
                        Vec3 N = normalize(high_normals[ht[0]] * hb0 + high_normals[ht[1]] * hit->u +
                                           high_normals[ht[2]] * hit->v);
                        if (length_squared(N) == 0) N = hit->normal;
                        const Vec3 D = hit->position - p;

                        for (std::size_t mi = 0; mi < maps.size(); ++mi) {
                            Image& img = images[mi];
                            float* px = &img.at(x, int(y), 0);
                            switch (maps[mi]) {
                                case BakeMap::TangentNormal: {
                                    const Vec3 tn = normalize(to_tangent_space(N));
                                    px[0] = tn.x * 0.5f + 0.5f;
                                    px[1] = tn.y * 0.5f + 0.5f;
                                    px[2] = tn.z * 0.5f + 0.5f;
                                    break;
                                }
                                case BakeMap::ObjectNormal:
                                    px[0] = N.x * 0.5f + 0.5f;
                                    px[1] = N.y * 0.5f + 0.5f;
                                    px[2] = N.z * 0.5f + 0.5f;
                                    break;
                                case BakeMap::Height: px[0] = dot(D, n); break;
                                case BakeMap::VectorDisplacement: {
                                    // Orthonormal unit frame, not the exact normal-map inverse.
                                    const Vec3 ut = normalize(vT - n * dot(n, vT));
                                    const Vec3 ub = cross(n, ut) * corner_tangents.sign[c0 + nearest];
                                    px[0] = dot(D, ut);
                                    px[1] = dot(D, ub);
                                    px[2] = dot(D, n);
                                    break;
                                }
                                case BakeMap::AmbientOcclusion: {
                                    // Cosine-weighted hemisphere, stratified and rotated per texel.
                                    const Vec3 tx = normalize(cross(N, std::abs(N.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
                                    const Vec3 ty = cross(N, tx);
                                    const int s = std::max(1, options.ao_samples);
                                    const float rot = hash01(std::uint32_t(id) * 2654435761u) * 2.0f * std::numbers::pi_v<float>;
                                    int open = 0;
                                    for (int k = 0; k < s; ++k) {
                                        const float r1 = (float(k) + 0.5f) / float(s);
                                        const float phi = rot + float(k) * 2.399963f;  // golden angle
                                        const float r = std::sqrt(r1);
                                        const Vec3 dir = tx * (r * std::cos(phi)) + ty * (r * std::sin(phi)) +
                                                         N * std::sqrt(std::max(0.0f, 1 - r1));
                                        if (!bvh.raycast(hit->position + N * (1e-4f * diagonal), dir, ao_reach)) ++open;
                                    }
                                    px[0] = float(open) / float(s);
                                    break;
                                }
                            }
                        }
                    }
                }
            },
            4);

        // Dilate into the padding so filtering never reads background.
        for (int pass = 0; pass < options.padding; ++pass) {
            std::vector<std::size_t> grow;
            for (int y = 0; y < res; ++y) {
                for (int x = 0; x < res; ++x) {
                    const std::size_t id = std::size_t(y) * res + x;
                    if (filled[id]) continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < res && ny < res &&
                                filled[std::size_t(ny) * res + nx] == 1) {
                                grow.push_back(id);
                                dx = dy = 2;  // leave both loops
                            }
                        }
                }
            }
            if (grow.empty()) break;
            for (std::size_t id : grow) {
                const int x = int(id % std::size_t(res)), y = int(id / std::size_t(res));
                for (auto& img : images) {
                    for (int c = 0; c < img.channels; ++c) {
                        float sum = 0;
                        int count = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= res || ny >= res) continue;
                                if (filled[std::size_t(ny) * res + nx] != 1) continue;
                                sum += img.at(nx, ny, c);
                                ++count;
                            }
                        img.at(x, y, c) = count ? sum / float(count) : 0.0f;
                    }
                }
            }
            for (std::size_t id : grow) filled[id] = 2;  // usable as a source next pass
            for (auto& f : filled) {
                if (f == 2) f = 1;
            }
        }

        for (std::size_t mi = 0; mi < maps.size(); ++mi) out.push_back({maps[mi], tile, std::move(images[mi])});
        for (int y = 0; y < res; ++y) {
            covered_total += covered[std::size_t(y)];
            miss_total += misses[std::size_t(y)];
        }
    }

    if (stats) {
        stats->texels = covered_total;
        stats->misses = miss_total;
        stats->max_distance = reach;
    }
    return out;
}

}  // namespace cruth
