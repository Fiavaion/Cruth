// SPDX-License-Identifier: MIT
#include <cmath>
#include <numbers>

#include "cruth/bvh.hpp"
#include "cruth/cruth.h"
#include "cruth/decimate.hpp"
#include "cruth/document.hpp"
#include "cruth/multires.hpp"
#include "cruth/primitives.hpp"
#include "cruth/quad_remesh.hpp"
#include "cruth/subdivide.hpp"
#include "cruth/print_check.hpp"
#include "cruth/raycast.hpp"
#include "cruth/voxel_remesh.hpp"
#include "test.hpp"

using namespace cruth;

namespace {

double sphere_error(const Mesh& m, float radius) {
    double worst = 0;
    for (const auto& p : m.positions) worst = std::max(worst, std::abs(double(length(p)) - radius));
    return worst;
}

double max_y(const Mesh& m) {
    double y = -1e30;
    for (const auto& p : m.positions) y = std::max(y, double(p.y));
    return y;
}

// Two overlapping cubes as one mesh with two shells.
Mesh overlapping_cubes() {
    Mesh a = make_cube(1.0f);
    Mesh m = a;
    const auto offset = static_cast<Index>(m.vertex_count());
    for (const auto& p : a.positions) m.add_vertex(p + Vec3{0.5f, 0.3f, 0.2f});
    for (std::size_t f = 0; f < a.face_count(); ++f) {
        std::vector<Index> c;
        for (Index v : a.face(f)) c.push_back(v + offset);
        m.add_face(c);
    }
    return m;
}

}  // namespace

TEST(bvh_raycast_matches_brute_force) {
    Mesh m = make_quad_sphere(4, 1.0f);
    TriangleBVH bvh(m);
    CHECK_EQ(bvh.triangle_count(), m.triangle_count());
    for (int i = 0; i < 200; ++i) {
        float a = float(i) * 0.37f, b = float(i) * 0.11f;
        Vec3 origin{3 * std::cos(a) * std::cos(b), 3 * std::sin(b), 3 * std::sin(a) * std::cos(b)};
        Vec3 dir = Vec3{0.1f * std::sin(float(i)), 0.05f, 0.0f} - origin;
        auto fast = bvh.raycast(origin, dir);
        auto slow = raycast(m, origin, dir);
        REQUIRE(fast.has_value() == slow.has_value());
        if (fast) CHECK_NEAR(fast->distance, slow->distance, 1e-4);
    }
}

TEST(bvh_refit_follows_moved_vertices) {
    Mesh m = make_quad_sphere(3, 1.0f);
    TriangleBVH bvh(m);
    for (auto& p : m.positions) p = p * 2.0f;
    bvh.refit(m.positions);
    auto hit = bvh.raycast({0, 0, 5}, {0, 0, -1});
    REQUIRE(hit.has_value());
    CHECK_NEAR(hit->position.z, 2.0, 1e-3);
}

TEST(bvh_closest_point) {
    Mesh cube = make_cube(2.0f);
    TriangleBVH bvh(cube);
    auto cp = bvh.closest_point({3, 0.25f, -0.5f});
    REQUIRE(cp.has_value());
    CHECK_NEAR(cp->distance, 2.0, 1e-5);
    CHECK_NEAR(cp->point.x, 1.0, 1e-5);
    CHECK(!bvh.closest_point({3, 0, 0}, 1.5f).has_value());  // beyond max distance

    // Corner region: barycentric weights collapse onto one vertex.
    ClosestPoint corner = closest_point_on_triangle({-1, -1, 0}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    CHECK_NEAR(corner.u, 1.0, 0);
    CHECK_NEAR(corner.distance, std::sqrt(2.0), 1e-6);
}

TEST(self_intersections_detected) {
    CHECK_EQ(check_printability(make_quad_sphere(3, 1.0f)).self_intersections, 0u);
    PrintReport r = check_printability(overlapping_cubes());
    CHECK(r.self_intersections > 0);
    CHECK(r.watertight());
    CHECK(!r.printable());
}

TEST(decimate_reaches_target_and_stays_closed) {
    Mesh sphere = make_quad_sphere(5, 1.0f);  // 12,288 triangles
    DecimateOptions o;
    o.target_triangles = 1000;
    DecimateStats stats;
    Mesh low = decimate(sphere, o, &stats);
    CHECK(low.face_count() <= 1000);
    CHECK(low.face_count() > 900);
    CHECK_EQ(stats.triangles_before, 12288u);
    PrintReport r = check_printability(low);
    CHECK(r.watertight());
    CHECK_EQ(r.non_manifold_vertices, 0u);
    CHECK_EQ(r.inconsistent_edges, 0u);
    CHECK(sphere_error(low, 1.0f) < 0.02);
    CHECK_NEAR(r.volume, 4.0 / 3.0 * std::numbers::pi, 0.15);
}

TEST(decimate_keeps_plane_boundary) {
    Mesh plane = make_plane(16, 2.0f);
    DecimateOptions o;
    o.target_triangles = 60;
    Mesh low = decimate(plane, o);
    CHECK(low.face_count() <= 60);
    Bounds b = low.bounds();
    CHECK_NEAR(b.size().x, 2.0, 1e-4);
    CHECK_NEAR(b.size().z, 2.0, 1e-4);
    for (const auto& p : low.positions) CHECK_NEAR(p.y, 0.0, 1e-5);
}

TEST(voxel_remesh_sphere_is_closed_quads_on_surface) {
    Mesh sphere = make_quad_sphere(3, 1.0f);
    VoxelRemeshOptions o;
    o.resolution = 48;
    VoxelRemeshStats stats;
    auto result = voxel_remesh(sphere, o, &stats);
    REQUIRE(result.is_ok());
    const Mesh& m = result.value();
    CHECK(stats.voxel_size > 0);
    for (std::size_t f = 0; f < m.face_count(); ++f) CHECK_EQ(m.face_size(f), 4u);
    PrintReport r = check_printability(m);
    CHECK(r.watertight());
    CHECK(r.volume > 0);  // outward winding
    CHECK_EQ(r.shells, 1u);
    CHECK(sphere_error(m, 1.0f) < 0.02);
}

TEST(voxel_remesh_merges_overlapping_shells) {
    auto result = voxel_remesh(overlapping_cubes(), {.resolution = 64});
    REQUIRE(result.is_ok());
    PrintReport r = check_printability(result.value());
    CHECK_EQ(r.shells, 1u);
    CHECK(r.watertight());
    CHECK(r.volume > 1.0);  // union is larger than one cube
    CHECK(r.volume < 2.0);  // and smaller than two
}

TEST(voxel_remesh_closes_small_hole) {
    Mesh sphere = make_quad_sphere(3, 1.0f);
    Mesh holed;
    holed.positions = sphere.positions;
    for (std::size_t f = 1; f < sphere.face_count(); ++f) holed.add_face(sphere.face(f));
    REQUIRE(!check_printability(holed).watertight());
    auto result = voxel_remesh(holed, {.resolution = 32});
    REQUIRE(result.is_ok());
    PrintReport r = check_printability(result.value());
    CHECK(r.watertight());
    CHECK_EQ(r.shells, 1u);
}

TEST(multires_edit_propagates_up_and_down) {
    MultiresStack stack(make_cube(2.0f));
    REQUIRE(stack.add_level().is_ok());
    REQUIRE(stack.add_level().is_ok());
    CHECK_EQ(stack.level_count(), 3);
    CHECK_EQ(stack.level(2).face_count(), 96u);

    // Edit level 1: move one coarse vertex outward.
    std::vector<Vec3> p1 = stack.level(1).positions;
    const Vec3 delta{0.2f, 0, 0};
    p1[0] += delta;
    stack.set_positions(1, p1);
    CHECK(stack.level(1).positions[0] == p1[0]);
    CHECK_NEAR(stack.level(0).positions[0].x, -1.0 + 0.2, 1e-6);  // restricted down
    CHECK(stack.level(2).positions[0].x > stack.level(2).positions[0].y);  // propagated up

    // Detail sculpted on level 2 survives an edit on level 0.
    std::vector<Vec3> p2 = stack.level(2).positions;
    p2[30] += Vec3{0, 0, 0.1f};
    stack.set_positions(2, p2);
    const Vec3 detail = stack.level(2).positions[30];
    std::vector<Vec3> p0 = stack.level(0).positions;
    for (auto& p : p0) p += Vec3{0, 1, 0};
    stack.set_positions(0, p0);
    CHECK_NEAR(stack.level(2).positions[30].y, detail.y + 1.0, 1e-5);
    CHECK_NEAR(stack.level(2).positions[30].z, detail.z, 1e-5);
}

TEST(multires_locked_base_stays_put) {
    MultiresStack stack(make_cube(2.0f));
    stack.set_base_locked(true);
    REQUIRE(stack.add_level().is_ok());
    std::vector<Vec3> p1 = stack.level(1).positions;
    for (auto& p : p1) p = p * 1.5f;
    stack.set_positions(1, p1);
    CHECK(stack.level(0).positions == make_cube(2.0f).positions);
}

TEST(document_levels_strokes_and_undo) {
    Document doc(make_quad_sphere(2, 1.0f));
    REQUIRE(doc.subdivide(2).is_ok());
    CHECK_EQ(doc.level_count(), 3);
    CHECK_EQ(doc.level(), 2);
    const std::size_t top_vertices = doc.mesh().vertex_count();

    // Sculpt at the top, step down, sculpt, step back up: both edits present.
    BrushSettings b;
    b.radius = 0.3f;
    b.strength = 1.0f;
    b.symmetry = {};
    const double high_before = max_y(doc.mesh());
    doc.begin_stroke(b);
    doc.add_stroke_sample({{0, 1, 0}, 1});
    doc.end_stroke();
    const double high_after = max_y(doc.mesh());
    CHECK(high_after > high_before + 0.005);
    auto lowest = [&] {
        double low = 0;
        for (const auto& p : doc.mesh().positions) low = std::min(low, double(p.y));
        return low;
    };
    const double low_before = lowest();

    REQUIRE(doc.set_level(0).is_ok());
    CHECK(doc.mesh().vertex_count() < top_vertices);
    doc.begin_stroke(b);
    doc.add_stroke_sample({{0, -1, 0}, 1});
    doc.end_stroke();
    REQUIRE(doc.set_level(2).is_ok());
    CHECK_EQ(doc.mesh().vertex_count(), top_vertices);
    CHECK(lowest() < low_before - 0.005);  // the level-0 edit reached the top level
    CHECK_NEAR(max_y(doc.mesh()), high_after, 1e-5);  // and the top-level edit survived

    // Undo walks back through the level changes and strokes.
    CHECK(doc.undo());  // level 0 -> 2
    CHECK_EQ(doc.level(), 0);
    CHECK(doc.undo());  // level-0 stroke
    CHECK(doc.undo());  // level 2 -> 0
    CHECK_EQ(doc.level(), 2);
    CHECK(doc.undo());  // top stroke
    CHECK(doc.undo());  // subdivide
    CHECK_EQ(doc.level_count(), 1);
    CHECK_EQ(doc.mesh().vertex_count(), make_quad_sphere(2, 1.0f).vertex_count());
}

TEST(document_remesh_and_decimate_commands) {
    Document doc(make_quad_sphere(4, 1.0f));
    REQUIRE(doc.subdivide(1).is_ok());
    auto stats = doc.voxel_remesh({.resolution = 40});
    REQUIRE(stats.is_ok());
    CHECK_EQ(doc.level_count(), 1);  // remesh resets multires
    CHECK(doc.check_printability().watertight());
    doc.decimate(500);
    CHECK(doc.mesh().face_count() <= 500);
    CHECK(doc.undo());
    CHECK(doc.undo());
    CHECK_EQ(doc.level_count(), 2);  // back to the subdivided sphere
    CHECK(doc.raycast({0, 0, 5}, {0, 0, -1}).has_value());
}

TEST(c_api_multires_and_remesh) {
    CruthDocument* doc = nullptr;
    REQUIRE(cruth_document_create_sphere(3, 1.0f, &doc) == CRUTH_OK);
    CHECK(cruth_document_subdivide(doc, 1) == CRUTH_OK);
    CHECK_EQ(cruth_document_level_count(doc), 2);
    CHECK_EQ(cruth_document_level(doc), 1);
    CHECK(cruth_document_set_level(doc, 0) == CRUTH_OK);
    CHECK(cruth_document_set_level(doc, 5) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK(cruth_document_voxel_remesh(doc, 4) == CRUTH_ERROR_INVALID_ARGUMENT);
    CHECK(cruth_document_voxel_remesh(doc, 32) == CRUTH_OK);
    CHECK_EQ(cruth_document_level_count(doc), 1);
    CHECK(cruth_document_decimate(doc, 300) == CRUTH_OK);
    CHECK(cruth_document_triangle_count(doc) <= 300);
    CruthPrintReport r{};
    CHECK(cruth_document_check_print(doc, &r) == CRUTH_OK);
    CHECK(r.watertight);
    CHECK_EQ(r.self_intersections, 0u);
    cruth_document_destroy(doc);
}

TEST(quad_remesh_sphere_all_quads_near_target) {
    Mesh sphere = make_quad_sphere(4, 1.0f);
    QuadRemeshOptions o;
    o.target_faces = 600;
    QuadRemeshStats stats;
    auto r = quad_remesh(sphere, o, &stats);
    REQUIRE(r.is_ok());
    const Mesh& m = r.value();
    CHECK(stats.faces > 400 && stats.faces < 800);
    CHECK(stats.quads * 100 >= stats.faces * 98);  // essentially all quads
    PrintReport p = check_printability(m);
    CHECK(p.watertight());
    CHECK_EQ(p.shells, 1u);
    CHECK(p.volume > 0);
    CHECK(sphere_error(m, 1.0f) < 0.01);
    // The result feeds multires: it must build a half-edge mesh and subdivide.
    CHECK(subdivide_catmull_clark(m).is_ok());
}

TEST(quad_remesh_document_command_and_errors) {
    Document doc(make_quad_sphere(3, 1.0f));
    QuadRemeshOptions o;
    o.target_faces = 300;
    auto r = doc.quad_remesh(o);
    REQUIRE(r.is_ok());
    CHECK(doc.mesh().face_count() > 150);
    CHECK(doc.undo());
    CHECK_EQ(doc.mesh().face_count(), 384u);
    o.target_faces = 2;
    CHECK(!quad_remesh(make_cube(), o).is_ok());
    CHECK(!quad_remesh(Mesh{}, QuadRemeshOptions{}).is_ok());
}

namespace {

// Mean and max distance from `from`'s vertices to `to`'s surface.
std::pair<double, double> surface_gap(const Mesh& from, const Mesh& to) {
    TriangleBVH bvh(to);
    double sum = 0, worst = 0;
    for (const auto& p : from.positions) {
        auto cp = bvh.closest_point(p);
        const double d = cp ? cp->distance : 1e9;
        sum += d;
        worst = std::max(worst, d);
    }
    return {sum / double(from.vertex_count()), worst};
}

}  // namespace

TEST(quad_remesh_guide_strokes_steer_edge_flow) {
    Mesh sphere = make_quad_sphere(4, 1.0f);
    // A great circle tilted 35 degrees: without guidance the field follows the
    // cube-sphere's own axes, not this loop.
    std::vector<Vec3> stroke;
    const float tilt = 0.6f;
    for (int i = 0; i <= 96; ++i) {
        const float t = 2.0f * std::numbers::pi_v<float> * float(i) / 96.0f;
        stroke.push_back({std::cos(t), -std::sin(t) * std::sin(tilt), std::sin(t) * std::cos(tilt)});
    }
    auto misalignment = [&](const Mesh& m) {
        double sum = 0;
        int n = 0;
        for (std::size_t f = 0; f < m.face_count(); ++f) {
            auto fv = m.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) {
                const Vec3 a = m.positions[fv[i]], b = m.positions[fv[(i + 1) % fv.size()]], mid = (a + b) * 0.5f;
                float best = 1e9f;
                Vec3 tangent;
                for (std::size_t k = 0; k + 1 < stroke.size(); ++k) {
                    const Vec3 ab = stroke[k + 1] - stroke[k];
                    const float t = std::clamp(dot(mid - stroke[k], ab) / dot(ab, ab), 0.0f, 1.0f);
                    const float d = distance(stroke[k] + ab * t, mid);
                    if (d < best) {
                        best = d;
                        tangent = normalize(ab);
                    }
                }
                if (best > 0.06f) continue;
                double angle = std::acos(std::min(1.0, double(std::abs(dot(normalize(b - a), tangent))))) * 180.0 / std::numbers::pi;
                if (angle > 45) angle = 90 - angle;
                sum += angle;
                ++n;
            }
        }
        return n ? sum / n : 90.0;
    };
    QuadRemeshOptions o;
    o.target_faces = 800;
    auto plain = quad_remesh(sphere, o);
    o.guide_strokes = {stroke};
    auto guided = quad_remesh(sphere, o);
    REQUIRE(plain.is_ok() && guided.is_ok());
    const double before = misalignment(plain.value()), after = misalignment(guided.value());
    CHECK(after < 6.0);
    CHECK(after < before * 0.5);
}

TEST(quad_remesh_keeps_sculpted_detail_as_multires) {
    Document doc(make_quad_sphere(3, 1.0f));
    REQUIRE(doc.subdivide(2).is_ok());
    BrushSettings crease;
    crease.type = BrushType::Crease;
    crease.radius = 0.12f;
    crease.strength = 1.0f;
    crease.symmetry = {};
    doc.begin_stroke(crease);
    for (int i = 0; i <= 20; ++i) doc.add_stroke_sample({normalize(Vec3{-0.4f + 0.04f * float(i), 1, 0.2f}), 1});
    doc.end_stroke();
    const Mesh sculpt = doc.mesh();

    QuadRemeshOptions o;
    o.target_faces = 500;
    REQUIRE(doc.quad_remesh(o, 0).is_ok());
    const auto coarse = surface_gap(sculpt, doc.mesh());
    REQUIRE(doc.undo());

    REQUIRE(doc.quad_remesh(o, 3).is_ok());
    CHECK_EQ(doc.level_count(), 4);
    CHECK_EQ(doc.level(), 3);
    const auto detailed = surface_gap(sculpt, doc.mesh());
    // The finest level follows the sculpt far more closely than the bare remesh.
    CHECK(detailed.second < coarse.second * 0.5);
    CHECK(detailed.first < 0.004);
    // The base is still the clean remesh, and the whole result is undoable.
    CHECK(doc.set_level(0).is_ok());
    CHECK(doc.mesh().face_count() < 600);
    REQUIRE(doc.undo());
    REQUIRE(doc.undo());
    CHECK_EQ(doc.level_count(), 3);
}
