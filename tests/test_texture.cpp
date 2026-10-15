// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cruth/bake.hpp"
#include "cruth/document.hpp"
#include "cruth/image_io.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/tangents.hpp"
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

// Unit quad in the XY plane facing +Z, mapped so that u = x (or 1 - x when mirrored) and v = y.
Mesh unit_quad(UVSet* uv, bool mirror_u, float lift_per_x = 0.0f) {
    Mesh m;
    const Vec2 corners[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const Vec2& c : corners) m.add_vertex({c.x, c.y, c.x * lift_per_x});
    m.add_face({0, 1, 2, 3});
    if (uv) {
        *uv = UVSet{};
        for (const Vec2& c : corners) uv->uvs.push_back({mirror_u ? 1 - c.x : c.x, c.y});
        uv->corner_uv = {0, 1, 2, 3};
        uv->face_chart = {0};
        uv->chart_count = 1;
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

TEST(mikktspace_tangents_follow_u_axis_and_flag_mirroring) {
    UVSet uv;
    const Mesh plane = unit_quad(&uv, false);
    auto t = compute_tangents(plane, uv);
    REQUIRE(t.is_ok());
    CHECK_EQ(t.value().tangent.size(), 6u);  // two triangles
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK_NEAR(t.value().tangent[i].x, 1.0f, 1e-5f);
        CHECK_NEAR(t.value().tangent[i].y, 0.0f, 1e-5f);
        CHECK_NEAR(t.value().tangent[i].z, 0.0f, 1e-5f);
        CHECK_NEAR(t.value().sign[i], 1.0f, 1e-6f);
    }

    UVSet mirrored_uv;
    const Mesh mirrored = unit_quad(&mirrored_uv, true);
    auto m = compute_tangents(mirrored, mirrored_uv);
    REQUIRE(m.is_ok());
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK_NEAR(m.value().tangent[i].x, -1.0f, 1e-5f);  // still follows +u, which now runs along -x
        CHECK_NEAR(m.value().sign[i], -1.0f, 1e-6f);
    }
    CHECK(!compute_tangents(plane, UVSet{}).is_ok());
}

TEST(bake_tangent_normal_uses_the_uv_frame) {
    // The high surface rises along +x, so its normal leans toward -x. In tangent space that is
    // -T.x, and mirroring u flips which way T points.
    const BakeMap maps[] = {BakeMap::TangentNormal};
    const BakeOptions options{.resolution = 32, .max_distance = 0.5f, .padding = 0};
    for (bool mirror : {false, true}) {
        UVSet uv;
        const Mesh low = unit_quad(&uv, mirror);
        const Mesh high = unit_quad(nullptr, false, 0.25f);
        auto r = bake(low, uv, high, maps, options);
        REQUIRE(r.is_ok());
        const Image& n = r.value()[0].image;
        const float lean = 0.25f / std::sqrt(1.0f + 0.25f * 0.25f);
        const float expected_r = 0.5f + 0.5f * (mirror ? lean : -lean);
        CHECK_NEAR(n.at(16, 16, 0), expected_r, 0.01f);
        CHECK_NEAR(n.at(16, 16, 1), 0.5f, 0.01f);
    }
}
