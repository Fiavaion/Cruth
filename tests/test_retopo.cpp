// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>

#include "cruth/cruth.h"
#include "cruth/document.hpp"
#include "cruth/primitives.hpp"
#include "cruth/print_check.hpp"
#include "cruth/quad_remesh.hpp"
#include "cruth/retopo.hpp"
#include "cruth/uv.hpp"
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

TEST(insert_loop_across_a_quad_ring) {
    // A 4x4 grid on the unit sphere's top cap, as quads.
    const Mesh sphere = make_quad_sphere(4, 1.0f);
    Mesh cap;
    std::vector<Index> remap(sphere.vertex_count(), kInvalidIndex);
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        if (sphere.face_centroid(f).y < 0.85f) continue;
        std::vector<Index> c;
        for (Index v : sphere.face(f)) {
            if (remap[v] == kInvalidIndex) remap[v] = cap.add_vertex(sphere.positions[v]);
            c.push_back(remap[v]);
        }
        cap.add_face(c);
    }
    RetopoMesh r(sphere, cap);
    const std::size_t faces_before = r.mesh().face_count();
    const auto [a, b] = r.nearest_edge({0.0f, 1.0f, 0.0f});
    REQUIRE(a != kInvalidIndex);
    auto loop = r.insert_loop(a, b, 0.5f);
    REQUIRE(loop.is_ok());
    const std::size_t ring = loop.value().size() - 1;  // an open ring: n faces, n + 1 cut edges
    CHECK(ring >= 2);
    CHECK_EQ(r.mesh().face_count(), faces_before + ring);
    CHECK_EQ(r.stats().triangles + r.stats().ngons, 0u);
    // New vertices sit on the surface, halfway along their edges (not zigzagging).
    // (the reference is faceted, so snapped points sit up to ~0.1% inside the sphere)
    for (Index v : loop.value()) CHECK_NEAR(length(r.mesh().positions[v]), 1.0, 2e-3);
    // A closed ring (around a capsule) gains exactly one face per ring face.
    RetopoMesh tube(capsule());
    for (int i = 0; i < 3; ++i) {
        const float y = -0.5f + 0.5f * float(i);
        const std::vector<Vec3> stroke{{-0.6f, y, 0.4f}, {0.6f, y, 0.4f}};
        REQUIRE(tube.add_contour(stroke, 8, {0, 0, -1}).is_ok());
    }
    const auto [c, d] = tube.nearest_edge({0.0f, -0.25f, 0.5f});
    auto ring_loop = tube.insert_loop(c, d, 0.5f);
    REQUIRE(ring_loop.is_ok());
    CHECK_EQ(ring_loop.value().size(), 8u);
    CHECK_EQ(tube.stats().quads, 24u);
    CHECK(!tube.insert_loop(0, 9999).is_ok());
}

TEST(insert_loop_keeps_a_closed_quad_mesh_closed) {
    // Rings on an irregular quad mesh wind around poles and can cross
    // themselves; every loop must still leave a watertight all-quad mesh.
    const Mesh sphere = make_quad_sphere(4, 1.0f);
    QuadRemeshOptions options;
    options.target_faces = 300;
    auto low = quad_remesh(sphere, options);
    REQUIRE(low.is_ok());
    REQUIRE(RetopoMesh(sphere, low.value()).stats().boundary_edges == 0);
    std::size_t tried = 0;
    for (std::size_t f = 0; f < low.value().face_count(); f += 11) {
        RetopoMesh r(sphere, low.value());
        const auto fv = low.value().face(f);
        auto loop = r.insert_loop(fv[0], fv[1], 0.3f);
        REQUIRE(loop.is_ok());
        const RetopoStats s = r.stats();
        CHECK_EQ(s.boundary_edges, 0u);
        CHECK_EQ(s.triangles + s.ngons, 0u);
        CHECK(check_printability(r.mesh()).watertight());
        ++tried;
    }
    CHECK(tried >= 20);
}

TEST(seams_split_unwrap_charts) {
    const Mesh sphere = make_quad_sphere(3, 1.0f);
    // Paint a seam along the x = 0 meridian: every edge whose endpoints straddle it.
    std::vector<std::pair<Index, Index>> seams;
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        auto fv = sphere.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            const Index p = fv[i], q = fv[(i + 1) % fv.size()];
            if (std::abs(sphere.positions[p].x) < 1e-5f && std::abs(sphere.positions[q].x) < 1e-5f) seams.push_back({p, q});
        }
    }
    REQUIRE(!seams.empty());
    const auto groups = face_groups_from_seams(sphere, seams);
    CHECK_EQ(*std::max_element(groups.begin(), groups.end()), 1);  // two halves
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        // Each half of the sphere is one group.
        CHECK_EQ(groups[f] == groups[0], (sphere.face_centroid(f).x > 0) == (sphere.face_centroid(0).x > 0));
    }
    UnwrapOptions o;
    o.face_groups = groups;
    o.tiles = 2;
    auto uv = auto_unwrap(sphere, o);
    REQUIRE(uv.is_ok());
    for (std::size_t f = 0; f < sphere.face_count(); ++f) {
        for (std::size_t g = 0; g < sphere.face_count(); ++g) {
            if (uv.value().face_chart[f] == uv.value().face_chart[g]) CHECK_EQ(groups[f], groups[g]);
        }
    }
}

namespace {

// A point on the unit sphere's top cap.
Vec3 cap_point(float x, float z) { return {x, std::sqrt(1.0f - x * x - z * z), z}; }

}  // namespace

TEST(quad_pen_taps_build_chained_quads) {
    const Mesh sphere = make_quad_sphere(5, 1.0f);
    RetopoMesh r(sphere);
    r.weld_radius = 0.03f;
    const Vec3 a = cap_point(-0.1f, -0.1f), b = cap_point(0.1f, -0.1f);
    const Vec3 c = cap_point(-0.1f, 0.1f), d = cap_point(0.1f, 0.1f);
    // Tapped in a crossed (Z) order: the quad still goes round its corners.
    for (const Vec3& p : {a, b, c}) {
        auto t = r.quad_pen_tap(p);
        REQUIRE(t.is_ok());
        CHECK(!t.value().face_added);
    }
    CHECK_EQ(r.quad_pen_pending().size(), 3u);
    auto t = r.quad_pen_tap(d);
    REQUIRE(t.is_ok());
    CHECK(t.value().face_added);
    CHECK_EQ(t.value().pending, 2u);  // the chain continues from the c-d edge
    REQUIRE(r.mesh().face_count() == 1u);
    CHECK_NEAR(r.mesh().face_area(0), 0.04, 0.004);  // not a bowtie
    const auto pending = r.quad_pen_pending();
    REQUIRE(pending.size() == 2u);
    CHECK(distance(pending[0], c) + distance(pending[1], d) < 1e-3f ||
          distance(pending[0], d) + distance(pending[1], c) < 1e-3f);

    // Two more taps extend the strip across the shared edge.
    REQUIRE(r.quad_pen_tap(cap_point(0.1f, 0.3f)).is_ok());
    t = r.quad_pen_tap(cap_point(-0.1f, 0.3f));
    REQUIRE(t.is_ok());
    CHECK(t.value().face_added);
    CHECK_EQ(r.mesh().face_count(), 2u);
    CHECK_EQ(r.mesh().vertex_count(), 6u);
    CHECK_EQ(r.stats().boundary_edges, 6u);
    CHECK(max_gap(r, sphere) < 1e-4);
    for (std::size_t f = 0; f < r.mesh().face_count(); ++f) {
        CHECK(dot(r.mesh().face_normal(f), r.normal_at(r.mesh().face_centroid(f))) > 0.5f);
    }

    // A tap onto a pending corner is rejected and the chain is kept.
    CHECK(!r.quad_pen_tap(cap_point(0.1f, 0.3f)).is_ok());
    CHECK_EQ(r.quad_pen_pending().size(), 2u);
    r.quad_pen_lift();
    CHECK(r.quad_pen_pending().empty());

    // Re-tapping an existing quad, or a quad on an edge with two faces, is rejected at the 4th tap.
    for (const Vec3& p : {a, b, d}) REQUIRE(r.quad_pen_tap(p).is_ok());
    CHECK(!r.quad_pen_tap(c).is_ok());
    CHECK_EQ(r.quad_pen_pending().size(), 3u);
    r.quad_pen_lift();
    for (const Vec3& p : {c, d, cap_point(0.1f, -0.3f)}) REQUIRE(r.quad_pen_tap(p).is_ok());
    CHECK(!r.quad_pen_tap(cap_point(-0.1f, -0.3f)).is_ok());
    CHECK_EQ(r.mesh().face_count(), 2u);
    r.quad_pen_lift();

    // A quad whose last edge closes onto the cage shares it, and the chain
    // ends because that edge has no free side left.
    const Vec3 g = cap_point(0.3f, -0.1f), h = cap_point(0.3f, 0.1f);
    for (const Vec3& p : {g, h, d}) REQUIRE(r.quad_pen_tap(p).is_ok());
    t = r.quad_pen_tap(b);
    REQUIRE(t.is_ok());
    CHECK(t.value().face_added);
    CHECK_EQ(t.value().pending, 0u);
    CHECK_EQ(r.mesh().face_count(), 3u);
    CHECK_EQ(r.mesh().vertex_count(), 8u);
    CHECK_EQ(r.stats().boundary_edges, 8u);
    CHECK_EQ(r.stats().quads, 3u);
}

TEST(c_api_quad_pen) {
    CruthDocument* doc = nullptr;
    REQUIRE(cruth_document_create_sphere(4, 1.0f, &doc) == CRUTH_OK);
    CruthRetopo* retopo = nullptr;
    REQUIRE(cruth_retopo_begin(doc, &retopo) == CRUTH_OK);
    const float s = 0.15f;
    const CruthVec3 taps[4] = {{-s, 1, -s}, {s, 1, -s}, {s, 1, s}, {-s, 1, s}};
    CruthQuadPenResult res{};
    for (int i = 0; i < 3; ++i) {
        CHECK(cruth_retopo_quad_pen_tap(retopo, taps[i], &res) == CRUTH_OK);
        CHECK_EQ(res.pending, uint32_t(i + 1));
    }
    CruthVec3 pending[4];
    CHECK_EQ(cruth_retopo_quad_pen_pending(retopo, pending, 4), 3u);
    CHECK_EQ(cruth_retopo_quad_pen_pending(retopo, nullptr, 0), 3u);
    CHECK(cruth_retopo_quad_pen_tap(retopo, taps[0], &res) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK_EQ(res.pending, 3u);
    CHECK(cruth_retopo_quad_pen_tap(retopo, taps[3], &res) == CRUTH_OK);
    CHECK(res.face_added);
    CHECK_EQ(res.pending, 2u);
    CHECK_EQ(cruth_retopo_stats(retopo).quads, 1u);
    cruth_retopo_quad_pen_lift(retopo);
    CHECK_EQ(cruth_retopo_quad_pen_pending(retopo, nullptr, 0), 0u);
    CHECK(cruth_retopo_quad_pen_tap(nullptr, taps[0], nullptr) == CRUTH_ERROR_INVALID_ARGUMENT);
    cruth_retopo_destroy(retopo);
    cruth_document_destroy(doc);
}

namespace {

// Five 8-vertex contour rings round the capsule at y = -1, -0.5, 0, 0.5, 1.
std::vector<std::vector<Index>> capsule_tube(RetopoMesh& r) {
    std::vector<std::vector<Index>> rings;
    for (int i = 0; i < 5; ++i) {
        const float y = -1.0f + 0.5f * float(i);
        const std::vector<Vec3> stroke{{-0.6f, y, 0.4f}, {0.6f, y, 0.4f}};
        auto ring = r.add_contour(stroke, 8, {0, 0, -1});
        if (ring) rings.push_back(ring.value());
    }
    return rings;
}

}  // namespace

TEST(slide_loop_between_neighbouring_rings) {
    const Mesh ref = capsule();
    RetopoMesh r(ref);
    const auto rings = capsule_tube(r);
    REQUIRE(rings.size() == 5u);
    const std::vector<Vec3> before = r.mesh().positions;
    const auto& mid = rings[2];

    auto loop = r.edge_loop(mid[0], mid[1]);
    REQUIRE(loop.is_ok());
    CHECK(loop.value().closed);
    CHECK_EQ(loop.value().vertices.size(), 8u);
    CHECK(std::is_permutation(mid.begin(), mid.end(), loop.value().vertices.begin()));

    // The neighbouring rings are 0.5 away along y: t = +-0.5 moves the loop a quarter.
    auto axis = r.begin_slide(mid[0], mid[1]);
    REQUIRE(axis.is_ok());
    CHECK(r.sliding());
    CHECK_NEAR(std::abs(axis.value().y), 0.5, 0.02);
    const float up = axis.value().y > 0 ? 1.0f : -1.0f;
    r.update_slide(0.5f);
    for (Index v : mid) CHECK_NEAR(r.mesh().positions[v].y, 0.25f * up, 0.01);
    r.update_slide(-0.5f);
    for (Index v : mid) CHECK_NEAR(r.mesh().positions[v].y, -0.25f * up, 0.01);
    r.update_slide(0.0f);
    for (Index v : mid) CHECK(distance(r.mesh().positions[v], before[v]) < 1e-4f);
    r.update_slide(0.5f);
    r.end_slide();
    CHECK(!r.sliding());
    CHECK(max_gap(r, ref) < 1e-4);
    for (Index v : rings[1]) CHECK(r.mesh().positions[v] == before[v]);  // only the loop moves

    // One undo step for the whole slide.
    REQUIRE(r.undo());
    CHECK(r.mesh().positions == before);
    REQUIRE(r.redo());
    for (Index v : mid) CHECK_NEAR(r.mesh().positions[v].y, 0.25f * up, 0.01);
    REQUIRE(r.undo());

    // An open loop along the tube runs from border to border; its ends slide along the borders.
    auto rail = r.edge_loop(rings[1][0], rings[2][0]);
    REQUIRE(rail.is_ok());
    CHECK(!rail.value().closed);
    CHECK_EQ(rail.value().vertices.size(), 5u);
    CHECK_EQ(rail.value().vertices.front(), rings[0][0]);
    CHECK_EQ(rail.value().vertices.back(), rings[4][0]);
    REQUIRE(r.slide_loop(rings[1][0], rings[2][0], 0.3f).is_ok());
    for (Index v : rail.value().vertices) CHECK(distance(r.mesh().positions[v], before[v]) > 0.05f);
    // (contours sit where the stroke snapped, so the end rings are near y = -0.96)
    const float end_y = before[rings[0][0]].y, next_y = before[rings[1][0]].y;
    CHECK_NEAR(r.mesh().positions[rings[0][0]].y, end_y, 0.05);  // slides along its border ring
    CHECK(max_gap(r, ref) < 1e-4);
    CHECK_EQ(r.stats().boundary_edges, 16u);
    REQUIRE(r.undo());

    // A border loop has faces on one side only: the axis points toward them
    // either way, and sliding the other way leaves the loop put.
    auto end_loop = r.edge_loop(rings[0][0], rings[0][1]);
    REQUIRE(end_loop.is_ok());
    CHECK(end_loop.value().closed);
    CHECK_EQ(end_loop.value().vertices.size(), 8u);
    auto end_axis = r.begin_slide(rings[0][0], rings[0][1]);
    REQUIRE(end_axis.is_ok());
    CHECK_NEAR(end_axis.value().y * (end_axis.value().y > 0 ? 1.0f : -1.0f), next_y - end_y, 0.02);
    const float inward = end_axis.value().y > 0 ? 0.5f : -0.5f;
    r.update_slide(-inward);
    for (Index v : rings[0]) CHECK(distance(r.mesh().positions[v], before[v]) < 1e-4f);
    r.update_slide(inward);
    for (Index v : rings[0]) CHECK_NEAR(r.mesh().positions[v].y, 0.5f * (end_y + next_y), 0.02);
    r.end_slide();

    CHECK(!r.begin_slide(mid[0], mid[0]).is_ok());
    CHECK(!r.begin_slide(mid[0], mid[4]).is_ok());  // not an edge
}

TEST(edge_loops_stop_at_poles) {
    // Level 1 cube sphere: 2x2 quads per cube face; the cube corners are valence-3 poles.
    const Mesh sphere = make_quad_sphere(4, 1.0f);
    RetopoMesh r(sphere, make_quad_sphere(1, 1.0f));
    const VertexAdjacency adj = VertexAdjacency::build(r.mesh());
    Index corner = kInvalidIndex, centre = kInvalidIndex;
    for (Index v = 0; v < r.mesh().vertex_count(); ++v) {
        if (adj.of(v).size() == 3 && corner == kInvalidIndex) corner = v;
    }
    REQUIRE(corner != kInvalidIndex);
    const Index edge_mid = adj.of(corner)[0];
    auto open = r.edge_loop(corner, edge_mid);
    REQUIRE(open.is_ok());
    CHECK(!open.value().closed);
    REQUIRE(open.value().vertices.size() == 3u);  // corner, edge midpoint, next corner
    CHECK_EQ(adj.of(open.value().vertices[2]).size(), 3u);
    // From an edge midpoint toward a face centre, the loop goes round the cube.
    for (Index v : adj.of(edge_mid)) {
        if (v != corner && adj.of(v).size() == 4) centre = v;
    }
    REQUIRE(centre != kInvalidIndex);
    auto round = r.edge_loop(edge_mid, centre);
    REQUIRE(round.is_ok());
    CHECK(round.value().closed);
    CHECK_EQ(round.value().vertices.size(), 8u);
    REQUIRE(r.slide_loop(edge_mid, centre, 0.4f).is_ok());
    CHECK(max_gap(r, sphere) < 1e-4);
    CHECK_EQ(r.stats().quads, 24u);
}

TEST(retopology_undo_history) {
    RetopoMesh r(capsule());
    CHECK(!r.can_undo());
    capsule_tube(r);
    const RetopoStats tube = r.stats();
    CHECK_EQ(tube.quads, 32u);
    const auto [a, b] = r.nearest_edge({0.0f, -0.25f, 0.5f});
    REQUIRE(r.insert_loop(a, b).is_ok());
    CHECK_EQ(r.stats().quads, 40u);
    REQUIRE(r.undo());
    CHECK_EQ(r.stats().quads, 32u);
    CHECK(r.can_redo());
    REQUIRE(r.redo());
    CHECK_EQ(r.stats().quads, 40u);
    REQUIRE(r.undo());
    r.relax(1);  // a new edit drops the redo branch
    CHECK(!r.can_redo());
    for (int i = 0; i < 6; ++i) REQUIRE(r.undo());
    CHECK_EQ(r.mesh().vertex_count(), 0u);
    CHECK(!r.undo());
    // A failed edit leaves no step behind.
    CHECK(!r.add_quad({Vec3{0, 0, 0.5f}, Vec3{0, 0, 0.5f}, Vec3{0, 0.1f, 0.5f}, Vec3{0, 0.1f, 0.5f}}).is_ok());
    CHECK(!r.can_undo());
    CHECK_EQ(r.mesh().vertex_count(), 0u);
    // The history is capped.
    r.undo_limit = 2;
    for (int i = 0; i < 4; ++i) r.relax(1);
    CHECK(r.undo());
    CHECK(r.undo());
    CHECK(!r.undo());
}

TEST(c_api_slide_loop_and_undo) {
    CruthDocument* doc = nullptr;
    REQUIRE(cruth_document_create_sphere(4, 1.0f, &doc) == CRUTH_OK);
    CruthRetopo* retopo = nullptr;
    REQUIRE(cruth_retopo_begin(doc, &retopo) == CRUTH_OK);
    uint32_t a = 0, b = 0;
    CHECK(!cruth_retopo_nearest_edge(retopo, {0, 0, 1}, &a, &b));
    for (int i = 0; i < 3; ++i) {
        const float y = -0.3f + 0.3f * float(i);
        const CruthVec3 stroke[2] = {{-1.2f, y, 0.5f}, {1.2f, y, 0.5f}};
        REQUIRE(cruth_retopo_add_contour(retopo, stroke, 2, 10, {0, 0, -1}) == CRUTH_OK);
    }
    CHECK(cruth_retopo_can_undo(retopo));
    // The middle ring: its edge nearest a point on it.
    REQUIRE(cruth_retopo_nearest_edge(retopo, {0.0f, 0.0f, 1.0f}, &a, &b));
    std::vector<float> before(cruth_retopo_copy_edges(retopo, nullptr, 0) * 6);
    cruth_retopo_copy_edges(retopo, before.data(), before.size());
    CruthVec3 axis{};
    REQUIRE(cruth_retopo_slide_loop_begin(retopo, a, b, &axis) == CRUTH_OK);
    CHECK(std::abs(axis.y) > 0.2f);
    cruth_retopo_slide_loop_update(retopo, 0.5f);
    cruth_retopo_slide_loop_end(retopo);
    std::vector<float> after(before.size());
    cruth_retopo_copy_edges(retopo, after.data(), after.size());
    CHECK(after != before);
    CHECK(cruth_retopo_undo(retopo));
    cruth_retopo_copy_edges(retopo, after.data(), after.size());
    CHECK(after == before);
    CHECK(cruth_retopo_can_redo(retopo));
    CHECK(cruth_retopo_redo(retopo));
    CHECK(cruth_retopo_slide_loop(retopo, a, b, -0.25f) == CRUTH_OK);
    CHECK(cruth_retopo_slide_loop(retopo, a, a, 0.5f) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK(cruth_retopo_slide_loop_begin(nullptr, a, b, nullptr) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK(!cruth_retopo_undo(nullptr));
    cruth_retopo_destroy(retopo);
    cruth_document_destroy(doc);
}
