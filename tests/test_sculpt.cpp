// SPDX-License-Identifier: MIT
#include <cmath>

#include "cruth/cruth.h"
#include "cruth/document.hpp"
#include "cruth/primitives.hpp"
#include "test.hpp"

using namespace cruth;

namespace {

double max_radius(const Mesh& m) {
    double r = 0;
    for (const auto& p : m.positions) r = std::max(r, double(length(p)));
    return r;
}

double min_radius(const Mesh& m) {
    double r = 1e30;
    for (const auto& p : m.positions) r = std::min(r, double(length(p)));
    return r;
}

void stroke(Document& doc, const BrushSettings& b, Vec3 from, Vec3 to, int samples = 20) {
    doc.begin_stroke(b);
    for (int i = 0; i <= samples; ++i) {
        Vec3 p = lerp(from, to, float(i) / float(samples));
        doc.add_stroke_sample({normalize(p), 1.0f});  // keep samples on the unit sphere
    }
    doc.end_stroke();
}

}  // namespace

TEST(curve_presets) {
    Curve s = Curve::smooth_falloff();
    CHECK_NEAR(s.evaluate(0), 1.0, 1e-6);
    CHECK_NEAR(s.evaluate(1), 0.0, 1e-6);
    CHECK_NEAR(s.evaluate(0.5f), 0.5, 1e-3);
    CHECK_NEAR(Curve::identity().evaluate(0.25f), 0.25, 1e-6);
    CHECK_NEAR(Curve::constant(0.3f).evaluate(0.9f), 0.3, 1e-6);
    CHECK_NEAR(Curve::linear_falloff().evaluate(2.0f), 0.0, 1e-6);  // clamps
}

TEST(standard_brush_raises_surface_and_undo_restores) {
    Document doc(make_quad_sphere(4, 1.0f));
    const Mesh original = doc.mesh();
    BrushSettings b;
    b.type = BrushType::Standard;
    b.radius = 0.3f;
    b.strength = 1.0f;
    stroke(doc, b, {-0.3f, 1, 0}, {0.3f, 1, 0});
    CHECK(max_radius(doc.mesh()) > 1.02);
    CHECK(doc.can_undo());
    CHECK(doc.undo_label() == "Standard");

    CHECK(doc.undo());
    CHECK(doc.mesh().positions == original.positions);
    CHECK(doc.redo());
    CHECK(max_radius(doc.mesh()) > 1.02);
}

TEST(inverted_brush_carves_in) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings b;
    b.type = BrushType::Standard;
    b.radius = 0.3f;
    b.strength = 1.0f;
    b.invert = true;
    stroke(doc, b, {-0.3f, 1, 0}, {0.3f, 1, 0});
    CHECK(min_radius(doc.mesh()) < 0.98);
    CHECK_NEAR(max_radius(doc.mesh()), 1.0, 1e-4);
}

TEST(x_symmetry_mirrors_the_stroke) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings b;
    b.type = BrushType::Clay;
    b.radius = 0.25f;
    b.strength = 1.0f;
    b.symmetry.x = true;
    stroke(doc, b, {0.6f, 0.6f, 0.3f}, {0.6f, 0.2f, 0.6f});
    // For every vertex there is a mirrored partner moved the same way.
    const Mesh& m = doc.mesh();
    const Mesh base = make_quad_sphere(4, 1.0f);
    double worst = 0;
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        Vec3 p = base.positions[i];
        // The quad sphere is itself symmetric, so find the mirrored base vertex.
        for (std::size_t j = 0; j < m.vertex_count(); ++j) {
            Vec3 q = base.positions[j];
            if (std::fabs(q.x + p.x) < 1e-5f && std::fabs(q.y - p.y) < 1e-5f && std::fabs(q.z - p.z) < 1e-5f) {
                Vec3 a = m.positions[i], c = m.positions[j];
                worst = std::max(worst, double(length(Vec3{-c.x, c.y, c.z} - a)));
                break;
            }
        }
    }
    CHECK(worst < 1e-4);
    CHECK(max_radius(m) > 1.01);
}

TEST(smooth_brush_reduces_roughness) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings bump;
    bump.type = BrushType::Crease;
    bump.radius = 0.15f;
    bump.strength = 1.0f;
    stroke(doc, bump, {-0.3f, 1, 0}, {0.3f, 1, 0});
    auto spread = [&] { return max_radius(doc.mesh()) - min_radius(doc.mesh()); };
    double rough = spread();
    BrushSettings smooth;
    smooth.type = BrushType::Smooth;
    smooth.radius = 0.4f;
    smooth.strength = 1.0f;
    // One pass: plain Laplacian smoothing also shrinks convex areas, so repeated
    // passes trade crease depth for overall shrinkage (volume-preserving
    // smoothing is on the roadmap).
    stroke(doc, smooth, {-0.3f, 1, 0}, {0.3f, 1, 0});
    CHECK(spread() < rough * 0.8);
}

TEST(mask_protects_vertices) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings mask;
    mask.type = BrushType::Mask;
    mask.radius = 0.5f;
    mask.strength = 1.0f;
    mask.falloff = Curve::constant(1);
    stroke(doc, mask, {0, 1, 0}, {0, 1, 0.01f}, 2);
    std::vector<Index> masked;
    for (Index v = 0; v < doc.mask().size(); ++v) {
        if (doc.mask()[v] >= 1.0f) masked.push_back(v);
    }
    REQUIRE(!masked.empty());
    const Mesh before = doc.mesh();

    BrushSettings b;
    b.type = BrushType::Standard;
    b.radius = 0.4f;
    b.strength = 1.0f;
    stroke(doc, b, {-0.2f, 1, 0}, {0.2f, 1, 0});
    for (Index v : masked) CHECK(doc.mesh().positions[v] == before.positions[v]);
    // The mask is undone together with the stroke history.
    doc.undo();
    doc.undo();
    CHECK(doc.mask()[masked.front()] == 0.0f);
}

TEST(grab_moves_region_with_stroke) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings b;
    b.type = BrushType::Grab;
    b.radius = 0.3f;
    b.strength = 0.5f;
    b.symmetry = {};
    doc.begin_stroke(b);
    doc.add_stroke_sample({{0, 1, 0}, 1});
    doc.add_stroke_sample({{0, 1.25f, 0}, 1});
    doc.add_stroke_sample({{0, 1.5f, 0}, 1});
    doc.end_stroke();
    CHECK_NEAR(max_radius(doc.mesh()), 1.5, 0.02);
}

TEST(subdivide_is_undoable) {
    Document doc(make_cube());
    REQUIRE(doc.subdivide(2).is_ok());
    CHECK_EQ(doc.mesh().face_count(), 96u);
    CHECK_EQ(doc.mask().size(), doc.mesh().vertex_count());
    auto topo = doc.topology_revision();
    doc.undo();
    CHECK_EQ(doc.mesh().face_count(), 6u);
    CHECK(doc.topology_revision() != topo);
    doc.redo();
    CHECK_EQ(doc.mesh().face_count(), 96u);
}

TEST(new_action_drops_redo_branch) {
    Document doc(make_quad_sphere(3, 1.0f));
    BrushSettings b;
    b.radius = 0.3f;
    stroke(doc, b, {0, 1, 0}, {0.2f, 1, 0});
    doc.undo();
    CHECK(doc.can_redo());
    stroke(doc, b, {0, -1, 0}, {0.2f, -1, 0});
    CHECK(!doc.can_redo());
}

TEST(c_api_end_to_end) {
    CruthDocument* doc = nullptr;
    REQUIRE(cruth_document_create_sphere(4, 1.0f, &doc) == CRUTH_OK);
    REQUIRE(doc != nullptr);
    const size_t verts = cruth_document_vertex_count(doc);
    const size_t tris = cruth_document_triangle_count(doc);
    CHECK_EQ(tris, 6u * 256u * 2u);

    CruthRayHit hit{};
    REQUIRE(cruth_document_raycast(doc, {0, 0, 5}, {0, 0, -1}, &hit));
    CHECK_NEAR(hit.position.z, 1.0, 1e-3);

    const auto rev = cruth_document_geometry_revision(doc);
    CruthBrush brush = cruth_brush_default(CRUTH_BRUSH_CLAY);
    brush.strength = 1.0f;
    REQUIRE(cruth_stroke_begin(doc, &brush) == CRUTH_OK);
    for (int i = 0; i <= 10; ++i) {
        CruthStrokeSample s{{-0.2f + 0.04f * float(i), 0.0f, 0.98f}, 0.8f};
        CHECK(cruth_stroke_add_sample(doc, &s) == CRUTH_OK);
    }
    CHECK(cruth_stroke_end(doc) == CRUTH_OK);
    CHECK(cruth_document_geometry_revision(doc) != rev);
    CHECK(cruth_can_undo(doc));

    std::vector<float> vbuf(verts * 7);
    std::vector<uint32_t> ibuf(tris * 3);
    CHECK(cruth_document_copy_vertices(doc, vbuf.data(), vbuf.size()) == CRUTH_OK);
    CHECK(cruth_document_copy_triangles(doc, ibuf.data(), ibuf.size()) == CRUTH_OK);
    CHECK(cruth_document_copy_triangles(doc, ibuf.data(), 3) == CRUTH_ERROR_INVALID_ARGUMENT);
    for (uint32_t i : ibuf) CHECK(i < verts);

    CruthPrintReport report{};
    CHECK(cruth_document_check_print(doc, &report) == CRUTH_OK);
    CHECK(report.printable);

    CHECK(cruth_document_subdivide(doc, 1) == CRUTH_OK);
    CHECK(cruth_document_vertex_count(doc) > verts);
    CHECK(cruth_undo(doc));
    CHECK_EQ(cruth_document_vertex_count(doc), verts);

    cruth_document_destroy(doc);

    CruthDocument* missing = nullptr;
    CHECK(cruth_document_load("/nonexistent/file.obj", &missing) == CRUTH_ERROR_IO);
    CHECK(missing == nullptr);
    CHECK(std::string(cruth_last_error()).find("cannot open") != std::string::npos);

    CruthBrush bad = cruth_brush_default(CRUTH_BRUSH_STANDARD);
    bad.radius = 0;
    CHECK(cruth_stroke_begin(nullptr, &bad) == CRUTH_ERROR_INVALID_ARGUMENT);
    cruth_document_destroy(nullptr);
}
