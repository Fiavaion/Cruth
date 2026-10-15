// SPDX-License-Identifier: MIT
#include <cmath>

#include "cruth/cruth.h"
#include "cruth/document.hpp"
#include "cruth/primitives.hpp"
#include "cruth/retopo.hpp"
#include "test.hpp"

using namespace cruth;

namespace {

// A smooth capsule-like reference: an ellipsoid stretched along Y.
Mesh capsule() {
    Mesh m = make_quad_sphere(5, 1.0f);
    for (auto& p : m.positions) p = {p.x * 0.5f, p.y * 2.0f, p.z * 0.5f};
    return m;
}

double max_gap(const RetopoMesh& r, const Mesh& reference) {
    TriangleBVH bvh(reference);
    double worst = 0;
    for (const auto& p : r.mesh().positions) worst = std::max(worst, double(bvh.closest_point(p)->distance));
    return worst;
}

}  // namespace

TEST(slice_and_resample) {
    const Mesh sphere = make_quad_sphere(4, 1.0f);
    auto loops = slice_mesh(sphere, {0, 1, 0}, 0.0f);
    CHECK_EQ(loops.size(), 1u);
    REQUIRE(!loops.empty());
    CHECK(loops[0].closed);
    for (const auto& p : loops[0].points) CHECK_NEAR(p.y, 0.0, 1e-5);
    auto ring = resample_polyline(loops[0].points, 12, true);
    CHECK_EQ(ring.size(), 12u);
    const double step = distance(ring[0], ring[1]);
    for (std::size_t i = 0; i < ring.size(); ++i) CHECK_NEAR(distance(ring[i], ring[(i + 1) % ring.size()]), step, 0.02);

    const std::vector<Vec3> line{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    auto pts = resample_polyline(line, 5, false);
    CHECK_EQ(pts.size(), 5u);
    CHECK_NEAR(pts[2].x, 1.0, 1e-6);  // midpoint by arc length is the corner
    CHECK_NEAR(pts[4].y, 1.0, 1e-6);
}

TEST(contours_build_a_tube_of_quads_on_the_surface) {
    const Mesh ref = capsule();
    RetopoMesh r(ref);
    // Strokes across the capsule, seen from +Z.
    for (int i = 0; i < 7; ++i) {
        const float y = -1.5f + 0.5f * float(i);
        const std::vector<Vec3> stroke{{-0.6f, y, 0.4f}, {0.0f, y, 0.55f}, {0.6f, y, 0.4f}};
        auto ring = r.add_contour(stroke, 8, {0, 0, -1});
        REQUIRE(ring.is_ok());
        CHECK_EQ(ring.value().size(), 8u);
    }
    const RetopoStats s = r.stats();
    CHECK_EQ(s.quads, 6u * 8u);
    CHECK_EQ(s.triangles + s.ngons, 0u);
    CHECK_EQ(s.boundary_edges, 16u);  // the two open ends
    CHECK_EQ(s.poles, 0u);
    CHECK(max_gap(r, ref) < 1e-4);
    // Every quad faces outward, along the reference normal.
    for (std::size_t f = 0; f < r.mesh().face_count(); ++f) {
        CHECK(dot(r.mesh().face_normal(f), r.normal_at(r.mesh().face_centroid(f))) > 0.5f);
    }
}

TEST(strip_quad_pen_and_relax) {
    const Mesh sphere = make_quad_sphere(5, 1.0f);
    RetopoMesh r(sphere);
    const std::vector<Vec3> stroke{{-0.5f, 0.86f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.86f, 0.0f}};
    REQUIRE(r.add_strip(stroke, 0.2f, 6).is_ok());
    CHECK_EQ(r.mesh().face_count(), 6u);
    CHECK_EQ(r.mesh().vertex_count(), 14u);
    CHECK(max_gap(r, sphere) < 1e-4);

    // A quad sharing two corners with the strip's end welds onto them.
    // Strip vertices alternate left/right; 12/13 are the last pair, 10 precedes 12.
    const Vec3 a = r.mesh().positions[12], b = r.mesh().positions[13];
    const Vec3 along = normalize(a - r.mesh().positions[10]);
    REQUIRE(r.add_quad({a, b, b + along * 0.15f, a + along * 0.15f}).is_ok());
    CHECK_EQ(r.mesh().vertex_count(), 16u);
    CHECK_EQ(r.mesh().face_count(), 7u);
    CHECK(!r.add_quad({a, a, b, b}).is_ok());

    r.move_vertex(0, {0.0f, 3.0f, 0.0f});  // snaps back onto the sphere
    CHECK_NEAR(length(r.mesh().positions[0]), 1.0, 1e-3);
    r.relax(5, true);
    CHECK(max_gap(r, sphere) < 1e-4);
    r.delete_face(0);
    CHECK_EQ(r.mesh().face_count(), 6u);
}

TEST(adopt_retopology_keeps_detail) {
    Document doc(capsule());
    BrushSettings b;
    b.type = BrushType::Standard;
    b.radius = 0.2f;
    b.strength = 1.0f;
    b.symmetry = {};
    doc.begin_stroke(b);
    doc.add_stroke_sample({{0.0f, 0.0f, 0.5f}, 1});
    doc.end_stroke();
    const Mesh sculpt = doc.mesh();
    auto front_of = [](const Mesh& m) {
        double z = -1e9;
        for (const auto& p : m.positions) z = std::max(z, double(p.z));
        return z;
    };

    RetopoMesh r(sculpt);
    for (int i = 0; i < 9; ++i) {
        const float y = -1.6f + 0.4f * float(i);
        const std::vector<Vec3> stroke{{-0.6f, y, 0.4f}, {0.6f, y, 0.4f}};
        REQUIRE(r.add_contour(stroke, 12, {0, 0, -1}).is_ok());
    }
    REQUIRE(doc.adopt_retopology(r.mesh(), 2).is_ok());
    CHECK_EQ(doc.level_count(), 3);
    CHECK_EQ(doc.level(), 2);
    // The top level follows the sculpt far more closely than the 12-sided base.
    auto mean_gap = [&](const Mesh& m) {
        TriangleBVH bvh(m);
        double sum = 0;
        int n = 0;
        for (const auto& p : sculpt.positions) {
            if (std::abs(p.y) > 1.4f) continue;  // the tube is open past the outer contours
            sum += bvh.closest_point(p)->distance;
            ++n;
        }
        return sum / double(n);
    };
    CHECK_NEAR(front_of(doc.mesh()), front_of(sculpt), 0.003);
    const double top_gap = mean_gap(doc.mesh());
    REQUIRE(doc.set_level(0).is_ok());
    const double base_gap = mean_gap(doc.mesh());
    CHECK(top_gap < base_gap * 0.25);
    CHECK(top_gap < 0.002);
    CHECK(!doc.adopt_retopology(Mesh{}, 1).is_ok());
}

TEST(c_api_retopology_and_guided_remesh) {
    CruthDocument* doc = nullptr;
    REQUIRE(cruth_document_create_sphere(4, 1.0f, &doc) == CRUTH_OK);
    CruthRetopo* retopo = nullptr;
    REQUIRE(cruth_retopo_begin(doc, &retopo) == CRUTH_OK);
    for (int i = 0; i < 3; ++i) {
        const float y = -0.3f + 0.3f * float(i);
        const CruthVec3 stroke[2] = {{-1.2f, y, 0.5f}, {1.2f, y, 0.5f}};
        CHECK(cruth_retopo_add_contour(retopo, stroke, 2, 10, {0, 0, -1}) == CRUTH_OK);
    }
    const CruthRetopoStats s = cruth_retopo_stats(retopo);
    CHECK_EQ(s.quads, 20u);
    CHECK_EQ(s.vertices, 30u);
    const size_t edges = cruth_retopo_copy_edges(retopo, nullptr, 0);
    CHECK_EQ(edges, 10u * 3u + 10u * 2u);
    std::vector<float> seg(edges * 6);
    CHECK_EQ(cruth_retopo_copy_edges(retopo, seg.data(), seg.size()), edges);
    cruth_retopo_relax(retopo, 2);
    const CruthVec3 bad[1] = {{0, 0, 0}};
    CHECK(cruth_retopo_add_contour(retopo, bad, 1, 10, {0, 0, -1}) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK(cruth_retopo_apply(retopo, doc, 1) == CRUTH_OK);
    CHECK_EQ(cruth_document_level_count(doc), 2);
    cruth_retopo_destroy(retopo);
    CHECK(cruth_undo(doc));

    const CruthVec3 guide[3] = {{-1, 0, 0}, {0, 0, 1}, {1, 0, 0}};
    const uint32_t sizes[1] = {3};
    CruthQuadRemeshOptions o{};
    o.target_faces = 300;
    o.detail_levels = 1;
    o.guide_points = guide;
    o.guide_stroke_sizes = sizes;
    o.guide_stroke_count = 1;
    CHECK(cruth_document_quad_remesh_ex(doc, &o) == CRUTH_OK);
    CHECK_EQ(cruth_document_level_count(doc), 2);
    o.guide_points = nullptr;
    CHECK(cruth_document_quad_remesh_ex(doc, &o) == CRUTH_ERROR_INVALID_ARGUMENT);
    cruth_document_destroy(doc);
}
