// SPDX-License-Identifier: MIT
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "cruth/bake.hpp"
#include "cruth/document.hpp"
#include "cruth/image_io.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/uv.hpp"
#include "test.hpp"

using namespace cruth;

namespace {

// Low-poly sphere with a raised band, as the high mesh for bakes.
Mesh bumpy_sphere() {
    Mesh m = make_quad_sphere(5, 1.0f);
    for (auto& p : m.positions) {
        if (std::abs(p.y) < 0.2f) p = p * 1.03f;
    }
    return m;
}

}  // namespace

TEST(unwrap_cube_gives_six_flat_charts) {
    Mesh cube = make_cube(2.0f);
    UnwrapStats stats;
    UnwrapOptions o;
    o.max_chart_angle_degrees = 30;
    auto r = auto_unwrap(cube, o, &stats);
    REQUIRE(r.is_ok());
    const UVSet& uv = r.value();
    CHECK_EQ(stats.charts, 6u);
    CHECK(uv.matches(cube));
    CHECK_EQ(count_flipped_uv_faces(cube, uv), 0u);
    for (const auto& t : uv.uvs) {
        CHECK(t.x >= 0 && t.x <= 1);
        CHECK(t.y >= 0 && t.y <= 1);
    }
    // Flat squares map without distortion.
    for (float d : uv_area_distortion(cube, uv)) CHECK_NEAR(d, 1.0, 1e-3);
}

TEST(unwrap_sphere_low_distortion_no_flips) {
    Mesh sphere = make_quad_sphere(4, 1.0f);
    UnwrapStats stats;
    auto r = auto_unwrap(sphere, {}, &stats);
    REQUIRE(r.is_ok());
    CHECK(stats.charts >= 2);
    CHECK_EQ(count_flipped_uv_faces(sphere, r.value()), 0u);
    CHECK(stats.max_stretch < 2.0f);
    CHECK(stats.utilisation > 0.2f);
    // Charts do not overlap: total uv area equals the sum of chart areas and
    // stays below the unit square.
    double area = 0;
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        for (std::size_t i = 1; i + 1 < sphere.face_size(f); ++i) {
            area += 0.5 * std::abs(cross(r.value().corner(sphere, f, i) - r.value().corner(sphere, f, 0),
                                         r.value().corner(sphere, f, i + 1) - r.value().corner(sphere, f, 0)));
        }
    }
    CHECK(area < 1.0);
}

TEST(unwrap_respects_seam_groups_and_udim_tiles) {
    Mesh sphere = make_quad_sphere(3, 1.0f);
    UnwrapOptions o;
    o.tiles = 2;
    o.face_groups.resize(sphere.face_count());
    for (std::size_t f = 0; f < sphere.face_count(); ++f) o.face_groups[f] = sphere.face_centroid(f).x > 0 ? 1 : 0;
    auto r = auto_unwrap(sphere, o);
    REQUIRE(r.is_ok());
    const UVSet& uv = r.value();
    CHECK_EQ(uv.tiles, 2);
    bool used[2] = {false, false};
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        // A chart never mixes groups.
        for (std::size_t g = 0; g < sphere.face_count(); ++g) {
            if (uv.face_chart[g] == uv.face_chart[f]) CHECK_EQ(o.face_groups[g], o.face_groups[f]);
        }
        const int tile = int(std::floor(uv.corner(sphere, f, 0).x));
        REQUIRE(tile >= 0 && tile < 2);
        used[tile] = true;
    }
    CHECK(used[0] && used[1]);
    UnwrapOptions bad;
    bad.face_groups = {1, 2, 3};  // wrong size
    CHECK(!auto_unwrap(sphere, bad).is_ok());
}

TEST(obj_roundtrip_keeps_uvs) {
    Mesh cube = make_cube();
    UnwrapOptions o;
    o.max_chart_angle_degrees = 30;
    auto r = auto_unwrap(cube, o);
    REQUIRE(r.is_ok());
    UVSet back;
    auto m = read_obj(write_obj(cube, &r.value()), &back);
    REQUIRE(m.is_ok());
    REQUIRE(back.matches(m.value()));
    CHECK_EQ(back.uvs.size(), r.value().uvs.size());
    for (std::size_t f = 0; f < cube.face_count(); ++f) {
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK_NEAR(back.corner(m.value(), f, i).x, r.value().corner(cube, f, i).x, 1e-6);
        }
    }
    UVSet none;
    REQUIRE(read_obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", &none).is_ok());
    CHECK(none.uvs.empty());
}

TEST(half_float_conversion) {
    CHECK_EQ(float_to_half(0.0f), 0u);
    CHECK_EQ(float_to_half(1.0f), 0x3C00u);
    CHECK_EQ(float_to_half(-2.0f), 0xC000u);
    CHECK_EQ(float_to_half(0.5f), 0x3800u);
    CHECK_EQ(float_to_half(65504.0f), 0x7BFFu);
    CHECK_EQ(float_to_half(1e6f), 0x7C00u);       // overflow to infinity
    CHECK_EQ(float_to_half(5.960464e-8f), 1u);    // smallest subnormal
    CHECK_EQ(float_to_half(1.0009765625f), 0x3C01u);
}

TEST(png_and_exr_structure) {
    Image img(37, 21, 3);
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = float((x * 7 + y * 3 + c * 11) % 50) / 49.0f;
    for (int bits : {8, 16}) {
        const std::string png = encode_png(img, bits);
        CHECK(png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0);
        CHECK(png.find("IHDR") == 12);
        CHECK(png.find("IEND") != std::string::npos);
    }
    for (bool half : {true, false}) {
        const std::string exr = encode_exr(img, half);
        std::uint32_t magic = 0;
        std::memcpy(&magic, exr.data(), 4);
        CHECK_EQ(magic, 20000630u);
        CHECK(exr.find("channels") != std::string::npos);
        CHECK(exr.find("dataWindow") != std::string::npos);
    }
    // zlib framing and a compressible payload actually shrinks.
    std::vector<std::uint8_t> zeros(10000, 0);
    const std::string z = zlib_compress(zeros);
    CHECK_EQ(std::uint8_t(z[0]), 0x78u);
    CHECK_EQ((std::uint8_t(z[0]) * 256 + std::uint8_t(z[1])) % 31, 0);
    CHECK(z.size() < 200);
}

TEST(bake_sphere_onto_itself_is_flat) {
    Mesh low = make_quad_sphere(3, 1.0f);
    UnwrapOptions uo;
    uo.resolution = 128;
    auto uv = auto_unwrap(low, uo);
    REQUIRE(uv.is_ok());
    const BakeMap maps[] = {BakeMap::TangentNormal, BakeMap::Height};
    BakeStats stats;
    auto r = bake(low, uv.value(), low, maps, {.resolution = 128, .padding = 2}, &stats);
    REQUIRE(r.is_ok());
    CHECK_EQ(r.value().size(), 2u);
    CHECK(stats.texels > 2000);
    CHECK(stats.misses * 100 < stats.texels);  // < 1% misses (seam texels)
    const Image& n = r.value()[0].image;
    const Image& h = r.value()[1].image;
    double worst_normal = 0, worst_height = 0;
    for (int y = 0; y < n.height; ++y) {
        for (int x = 0; x < n.width; ++x) {
            worst_normal = std::max(worst_normal, double(std::abs(n.at(x, y, 2) - 1.0f)));
            worst_height = std::max(worst_height, double(std::abs(h.at(x, y, 0))));
        }
    }
    // Baking a mesh onto itself: smooth vs flat normals differ only slightly.
    CHECK(worst_normal < 0.05);
    CHECK(worst_height < 1e-3);
}

TEST(bake_detail_shows_up_in_height_and_normal) {
    Mesh low = make_quad_sphere(3, 1.0f);
    Mesh high = bumpy_sphere();
    UnwrapOptions uo;
    uo.resolution = 128;
    auto uv = auto_unwrap(low, uo);
    REQUIRE(uv.is_ok());
    const BakeMap maps[] = {BakeMap::Height, BakeMap::TangentNormal, BakeMap::AmbientOcclusion,
                            BakeMap::VectorDisplacement};
    auto r = bake(low, uv.value(), high, maps, {.resolution = 128, .ao_samples = 8});
    REQUIRE(r.is_ok());
    float max_h = -1, min_ao = 2;
    double tilt = 0, sideways = 0;
    for (const auto& m : r.value()) {
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                if (m.map == BakeMap::Height) max_h = std::max(max_h, m.image.at(x, y, 0));
                if (m.map == BakeMap::AmbientOcclusion) min_ao = std::min(min_ao, m.image.at(x, y, 0));
                if (m.map == BakeMap::TangentNormal) {
                    tilt = std::max(tilt, double(1.0f - m.image.at(x, y, 2)));
                    sideways = std::max(sideways, double(std::abs(m.image.at(x, y, 1) - 0.5f)));
                }
            }
        }
    }
    // The band is raised 3%; flat low-poly faces sit up to ~1% inside the sphere.
    CHECK(max_h > 0.03f && max_h < 0.045f);
    CHECK(tilt > 0.015);      // band edges tilt the normal (~19 degrees)
    CHECK(sideways > 0.1);    // ...visibly in the tangent-plane channels
    CHECK(min_ao > 0.5);             // a convex shape is mostly unoccluded
    CHECK(std::string(bake_map_name(BakeMap::VectorDisplacement)) == "vdm");
}

TEST(bake_rejects_bad_input) {
    Mesh low = make_cube();
    const BakeMap maps[] = {BakeMap::Height};
    CHECK(!bake(low, UVSet{}, low, maps).is_ok());  // no UVs
    auto uv = auto_unwrap(low);
    REQUIRE(uv.is_ok());
    CHECK(!bake(low, uv.value(), Mesh{}, maps).is_ok());
    CHECK(!bake(low, uv.value(), low, std::span<const BakeMap>{}).is_ok());
}

TEST(game_asset_export_writes_obj_and_maps) {
    Document doc(make_quad_sphere(3, 1.0f));
    REQUIRE(doc.subdivide(2).is_ok());
    BrushSettings b;
    b.radius = 0.3f;
    b.strength = 1.0f;
    doc.begin_stroke(b);
    doc.add_stroke_sample({{0, 1, 0}, 1});
    doc.end_stroke();
    GameAssetOptions o;
    o.name = "asset_test";
    o.target_triangles = 500;
    o.texture_size = 64;
    o.ao_samples = 4;
    const std::string dir = ".";
    auto r = doc.export_game_asset(dir, o);
    REQUIRE(r.is_ok());
    CHECK(r.value().triangles <= 500);
    CHECK_EQ(r.value().files.size(), 4u);
    UVSet uv;
    auto low = load_mesh("./asset_test.obj", &uv);
    REQUIRE(low.is_ok());
    CHECK(uv.matches(low.value()));
    CHECK_EQ(doc.level_count(), 3);  // the document is unchanged
    for (const auto& f : r.value().files) std::remove(f.c_str());
}

namespace {

UnwrapOptions per_face_groups(const Mesh& m) {
    UnwrapOptions o;
    o.face_groups.resize(m.face_count());
    for (std::size_t f = 0; f < m.face_count(); ++f) o.face_groups[f] = int(f);
    return o;
}

std::vector<int> cell_groups(const Mesh& m, float cell) {
    std::vector<int> g(m.face_count());
    for (std::size_t f = 0; f < m.face_count(); ++f) {
        const Vec3 c = m.face_centroid(f);
        const int x = int(std::floor(c.x / cell)), y = int(std::floor(c.y / cell)), z = int(std::floor(c.z / cell));
        g[f] = (x + 20) * 1600 + (y + 20) * 40 + (z + 20);
    }
    return g;
}

}  // namespace

namespace {

// Packing efficiency (chart UV area / tile area) of the shelf packer this
// replaced, measured on these exact inputs, and what the MaxRects packer must
// now reach. Tile 0 of the two-tile case is what `old_utilisation` records.
struct PackCase {
    const char* name;
    Mesh mesh;
    UnwrapOptions options;
    float old_utilisation;
    float min_utilisation;
};

std::vector<PackCase> pack_cases() {
    std::vector<PackCase> cases;
    cases.push_back({"sphere4", make_quad_sphere(4, 1.0f), {}, 0.3955f, 0.52f});
    cases.push_back({"sphere5", make_quad_sphere(5, 1.0f), {}, 0.3853f, 0.58f});
    {
        Mesh m = make_quad_sphere(5, 1.0f);
        UnwrapOptions o;
        o.face_groups = cell_groups(m, 0.35f);  // ~150 charts of mixed size and shape
        cases.push_back({"many_charts", std::move(m), o, 0.6446f, 0.67f});
    }
    {
        Mesh m = make_quad_sphere(5, 1.0f);
        UnwrapOptions o;
        o.face_groups = cell_groups(m, 0.2f);
        o.tiles = 2;
        cases.push_back({"many_charts_two_tiles", std::move(m), o, 0.6239f, 0.64f});
    }
    cases.push_back({"chart_per_face", make_quad_sphere(4, 1.0f), per_face_groups(make_quad_sphere(4, 1.0f)), 0.5846f,
                     0.62f});  // 1,536 charts: the skyline packer
    return cases;
}

}  // namespace

TEST(unwrap_packing_beats_shelf_baseline) {
    for (auto& c : pack_cases()) {
        UnwrapStats stats;
        auto r = auto_unwrap(c.mesh, c.options, &stats);
        REQUIRE(r.is_ok());
        const auto tiles = uv_tile_utilisation(c.mesh, r.value());
        REQUIRE(tiles.size() == std::size_t(c.options.tiles));
        REQUIRE(stats.tile_utilisation.size() == tiles.size());
        float mean = 0;
        for (std::size_t t = 0; t < tiles.size(); ++t) {
            CHECK_NEAR(tiles[t], stats.tile_utilisation[t], 1e-6);
            CHECK(tiles[t] >= c.min_utilisation);
            CHECK(tiles[t] >= c.old_utilisation);
            mean += tiles[t] / float(tiles.size());
        }
        CHECK_NEAR(mean, stats.utilisation, 1e-4);
        CHECK_EQ(count_flipped_uv_faces(c.mesh, r.value()), 0u);
        if (cruth_test::failures() != 0) std::fprintf(stderr, "  case %s: utilisation %.4f\n", c.name, mean);
    }
}

TEST(unwrap_charts_do_not_overlap_and_keep_padding) {
    for (auto& c : pack_cases()) {
        auto r = auto_unwrap(c.mesh, c.options);
        REQUIRE(r.is_ok());
        const UVSet& uv = r.value();
        struct Box { float x0, y0, x1, y1; int tile; };
        std::vector<Box> box(uv.chart_count, {1e30f, 1e30f, -1e30f, -1e30f, 0});
        for (std::size_t f = 0; f < c.mesh.face_count(); ++f) {
            Box& b = box[uv.face_chart[f]];
            b.tile = int(std::floor(uv.corner(c.mesh, f, 0).x));
            for (std::size_t i = 0; i < c.mesh.face_size(f); ++i) {
                const Vec2 p = uv.corner(c.mesh, f, i);
                b.x0 = std::min(b.x0, p.x - float(b.tile));
                b.x1 = std::max(b.x1, p.x - float(b.tile));
                b.y0 = std::min(b.y0, p.y);
                b.y1 = std::max(b.y1, p.y);
            }
        }
        // Texel padding: padding_texels on each side of a chart, so charts sit
        // 2 * padding_texels apart and padding_texels from the tile edge.
        const float pad = float(c.options.padding_texels) / float(c.options.resolution) * 0.99f;
        for (std::size_t a = 0; a < box.size(); ++a) {
            CHECK(box[a].x0 >= pad && box[a].y0 >= pad);
            CHECK(box[a].x1 <= 1 - pad && box[a].y1 <= 1 - pad);
            for (std::size_t b = a + 1; b < box.size(); ++b) {
                if (box[a].tile != box[b].tile) continue;
                const float gap_x = std::max(box[a].x0 - box[b].x1, box[b].x0 - box[a].x1);
                const float gap_y = std::max(box[a].y0 - box[b].y1, box[b].y0 - box[a].y1);
                // Separated on at least one axis by the full padding. Boxes
                // are of the (rotated) charts, so disjoint boxes mean no overlap.
                CHECK(std::max(gap_x, gap_y) >= 2 * pad);
            }
        }
    }
}

TEST(unwrap_is_deterministic) {
    auto cases = pack_cases();
    for (auto& c : cases) {
        auto a = auto_unwrap(c.mesh, c.options);
        auto b = auto_unwrap(c.mesh, c.options);
        REQUIRE(a.is_ok() && b.is_ok());
        REQUIRE(a.value().uvs.size() == b.value().uvs.size());
        CHECK(std::memcmp(a.value().uvs.data(), b.value().uvs.data(), a.value().uvs.size() * sizeof(Vec2)) == 0);
        CHECK(a.value().corner_uv == b.value().corner_uv);
        CHECK(a.value().face_chart == b.value().face_chart);
    }
}

// Zero-area charts (all corners on one line) have a zero short side; the
// packer must still finish and keep every UV finite and inside the tile.
TEST(unwrap_degenerate_faces_terminate) {
    Mesh quad;
    for (int i = 0; i < 4; ++i) quad.add_vertex({float(i), 0, 0});
    quad.add_face({0, 1, 2, 3});
    Mesh tri;
    for (int i = 0; i < 3; ++i) tri.add_vertex({float(i), 0, 0});
    tri.add_face({0, 1, 2});
    for (const Mesh* m : {&quad, &tri}) {
        UnwrapStats stats;
        auto r = auto_unwrap(*m, {}, &stats);
        REQUIRE(r.is_ok());
        CHECK_EQ(stats.charts, 1u);
        CHECK(r.value().matches(*m));
        for (const auto& t : r.value().uvs) {
            CHECK(std::isfinite(t.x) && std::isfinite(t.y));
            CHECK(t.x >= 0 && t.x <= 1 && t.y >= 0 && t.y <= 1);
        }
    }
}

// Packing cost must not explode with chart count (it once took 40 s here).
TEST(unwrap_many_charts_is_fast) {
    const Mesh m = make_quad_sphere(4, 1.0f);
    for (int tiles : {1, 4}) {
        UnwrapOptions o = per_face_groups(m);
        o.tiles = tiles;
        UnwrapStats stats;
        const auto start = std::chrono::steady_clock::now();
        auto r = auto_unwrap(m, o, &stats);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        REQUIRE(r.is_ok());
        CHECK_EQ(stats.charts, m.face_count());
        CHECK(seconds < 2.0);
        CHECK(stats.utilisation >= (tiles == 1 ? 0.62f : 0.72f));  // shelf packer: 0.585, 0.702
    }
}

TEST(uv_queries_ignore_a_mismatched_uv_set) {
    const Mesh cube = make_cube(2.0f);
    UnwrapOptions o;
    o.tiles = 3;
    auto r = auto_unwrap(cube, o);
    REQUIRE(r.is_ok());
    const Mesh sphere = make_quad_sphere(2, 1.0f);  // more corners than the cube's UVs cover
    REQUIRE(!r.value().matches(sphere));
    CHECK(uv_tile_utilisation(sphere, r.value()) == std::vector<float>(3, 0.0f));
    CHECK(uv_tile_utilisation(sphere, UVSet{}) == std::vector<float>(1, 0.0f));
    CHECK_EQ(count_flipped_uv_faces(sphere, r.value()), 0u);
    CHECK(uv_area_distortion(sphere, r.value()) == std::vector<float>(sphere.face_count(), 0.0f));
}
