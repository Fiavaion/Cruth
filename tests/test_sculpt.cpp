// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

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

// Deterministic on every platform, unlike the <random> distributions.
struct Rng {
    std::uint64_t state;
    float unit() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return float(state >> 40) / float(1 << 24);
    }
    Vec3 in_cube() { return {unit() * 2 - 1, unit() * 2 - 1, unit() * 2 - 1}; }
    Vec3 on_sphere() {
        for (;;) {
            Vec3 p = in_cube();
            if (length_squared(p) > 0.01f && length_squared(p) <= 1) return normalize(p);
        }
    }
};

bool same_box(const Bounds& a, const Bounds& b) {
    return std::memcmp(&a.lo, &b.lo, sizeof(Vec3)) == 0 && std::memcmp(&a.hi, &b.hi, sizeof(Vec3)) == 0;
}

bool same_hit(const std::optional<RayHit>& a, const std::optional<RayHit>& b) {
    if (a.has_value() != b.has_value()) return false;
    return !a || (a->distance == b->distance && a->triangle == b->triangle && a->face == b->face);
}

// Rays from outside the unit sphere aimed near its centre, so most of them hit.
template <typename Cast>
int ray_mismatches(const TriangleBVH& reference, Cast&& cast, Rng& rng, int rays = 200) {
    int bad = 0;
    for (int i = 0; i < rays; ++i) {
        const Vec3 origin = rng.on_sphere() * 3.0f;
        const Vec3 dir = rng.in_cube() * 0.8f - origin;
        if (!same_hit(cast(origin, dir), reference.raycast(origin, dir))) ++bad;
    }
    return bad;
}

// Half the rays graze the area around `aim`, where stale boxes would cut off hits.
int ray_mismatches(const Document& doc, Rng& rng, Vec3 aim) {
    const TriangleBVH fresh(doc.mesh());
    int bad = ray_mismatches(fresh, [&](Vec3 o, Vec3 d) { return doc.raycast(o, d); }, rng);
    for (int i = 0; i < 200; ++i) {
        const Vec3 origin = normalize(aim + rng.in_cube() * 0.6f) * 3.0f;
        const Vec3 dir = aim * 1.1f + rng.in_cube() * 0.3f - origin;
        if (!same_hit(doc.raycast(origin, dir), fresh.raycast(origin, dir))) ++bad;
    }
    return bad;
}

// Returns where the stroke started.
Vec3 random_stroke(Document& doc, Rng& rng, BrushType type, bool mirror) {
    BrushSettings b;
    b.type = type;
    b.radius = 0.1f + 0.1f * rng.unit();
    b.strength = 1.0f;
    b.symmetry.x = mirror;
    const Vec3 from = rng.on_sphere();
    stroke(doc, b, from, normalize(from + rng.in_cube() * 0.3f), 8);
    return from;
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

TEST(smooth_brush_reduces_roughness_without_shrinking) {
    Document doc(make_quad_sphere(4, 1.0f));
    BrushSettings bump;
    bump.type = BrushType::Crease;
    bump.radius = 0.15f;
    bump.strength = 1.0f;
    stroke(doc, bump, {-0.3f, 1, 0}, {0.3f, 1, 0});
    auto spread = [&] { return max_radius(doc.mesh()) - min_radius(doc.mesh()); };
    const double rough = spread();
    BrushSettings smooth;
    smooth.type = BrushType::Smooth;
    smooth.radius = 0.4f;
    smooth.strength = 1.0f;
    double previous_floor = min_radius(doc.mesh());
    for (int i = 0; i < 4; ++i) {
        stroke(doc, smooth, {-0.3f, 1, 0}, {0.3f, 1, 0});
        // Taubin smoothing keeps lifting the crease instead of sinking the area.
        CHECK(min_radius(doc.mesh()) > previous_floor);
        previous_floor = min_radius(doc.mesh());
    }
    CHECK(spread() < rough * 0.8);

    // Smoothing an already smooth sphere barely changes its size.
    Document clean(make_quad_sphere(4, 1.0f));
    for (int i = 0; i < 4; ++i) stroke(clean, smooth, {-0.3f, 1, 0}, {0.3f, 1, 0});
    CHECK(min_radius(clean.mesh()) > 0.995);
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

// The partial refit after each brush sample must give exactly the boxes of a
// full refit, across brush types, mirror and radial symmetry.
TEST(partial_bvh_refit_matches_full_refit) {
    Mesh m = make_quad_sphere(4, 1.0f);
    Sculptor sculptor(m);
    TriangleBVH partial(m), full(m);
    std::uint64_t mark = sculptor.journal_end();
    Rng rng{0x9E3779B97F4A7C15ull};
    const BrushType types[] = {BrushType::Standard, BrushType::Inflate, BrushType::Clay,
                               BrushType::Smooth,   BrushType::Pinch,   BrushType::Grab};
    int samples = 0, small = 0, box_mismatches = 0;
    for (int s = 0; s < 12; ++s) {
        BrushSettings b;
        b.type = types[s % 6];
        b.radius = 0.12f + 0.1f * rng.unit();
        b.strength = 1.0f;
        b.invert = s % 4 == 1;
        b.symmetry.x = s % 2 == 0;
        b.symmetry.radial_count = s % 3 == 0 ? 3 : 1;
        sculptor.begin_stroke(b);
        const Vec3 from = rng.on_sphere(), to = normalize(from + rng.in_cube() * 0.5f);
        for (int i = 0; i <= 10; ++i) {
            sculptor.add_sample({normalize(lerp(from, to, float(i) / 10.0f)), 0.5f + 0.5f * rng.unit()});
            auto moved = sculptor.moved_since(mark);
            REQUIRE(moved.has_value());
            ++samples;
            if (moved->size() * 32 < m.vertex_count()) ++small;  // well inside the partial path
            partial.refit(m.positions, *moved);
            mark = sculptor.journal_end();
            full.refit(m.positions);
            REQUIRE(partial.node_count() == full.node_count());
            for (std::size_t n = 0; n < full.node_count(); ++n) {
                if (!same_box(partial.node_bounds(n), full.node_bounds(n))) ++box_mismatches;
            }
        }
        sculptor.end_stroke();
    }
    CHECK_EQ(box_mismatches, 0);
    CHECK(small > samples / 2);
    CHECK(partial.positions() == m.positions);

    // Queries agree with a tree built from scratch on the sculpted mesh.
    const TriangleBVH fresh(m);
    CHECK_EQ(ray_mismatches(fresh, [&](Vec3 o, Vec3 d) { return partial.raycast(o, d); }, rng, 500), 0);
    int closest_mismatches = 0;
    for (int i = 0; i < 200; ++i) {
        const Vec3 p = rng.in_cube() * 1.5f;
        auto a = partial.closest_point(p), c = fresh.closest_point(p);
        if (!a || !c || a->distance != c->distance) ++closest_mismatches;
    }
    CHECK_EQ(closest_mismatches, 0);
}

// The journal must not hand out a partial list once it has lost track.
TEST(sculpt_journal_invalidates_on_rebuild) {
    Mesh m = make_quad_sphere(3, 1.0f);
    Sculptor sculptor(m);
    const std::uint64_t mark = sculptor.journal_end();
    BrushSettings b;
    b.radius = 0.3f;
    sculptor.begin_stroke(b);
    sculptor.add_sample({{0, 1, 0}, 1.0f});
    sculptor.end_stroke();
    auto moved = sculptor.moved_since(mark);
    REQUIRE(moved.has_value());
    CHECK(!moved->empty());
    sculptor.rebuild();
    CHECK(!sculptor.moved_since(mark).has_value());
    CHECK(sculptor.moved_since(sculptor.journal_end()).has_value());
}

// Document picking after strokes (mirrored too), undo/redo and level changes
// matches a BVH built from scratch on the current mesh.
TEST(document_raycasts_track_strokes_undo_and_levels) {
    Document doc(make_quad_sphere(4, 1.0f));
    REQUIRE(doc.subdivide(1).is_ok());
    Rng rng{42};
    Vec3 aim{0, 1, 0};
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    for (int s = 0; s < 4; ++s) {
        aim = random_stroke(doc, rng, s % 2 ? BrushType::Clay : BrushType::Standard, s % 2 == 0);
        CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    }
    // Picking between samples, as the shell does.
    BrushSettings b;
    b.radius = 0.12f;
    b.strength = 1.0f;
    b.symmetry.x = true;
    doc.begin_stroke(b);
    for (int i = 0; i <= 8; ++i) {
        aim = normalize(Vec3{0.5f, 0.05f * float(i), 0.8f});
        doc.add_stroke_sample({aim, 1.0f});
        CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
        CHECK_EQ(ray_mismatches(doc, rng, Vec3{-aim.x, aim.y, aim.z}), 0);
    }
    doc.end_stroke();
    CHECK(doc.undo());
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    CHECK(doc.undo());
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    CHECK(doc.redo());
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);

    REQUIRE(doc.set_level(0).is_ok());
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    aim = random_stroke(doc, rng, BrushType::Inflate, true);
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    REQUIRE(doc.set_level(1).is_ok());  // the coarse stroke propagates up
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    aim = random_stroke(doc, rng, BrushType::Smooth, false);
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    CHECK(doc.undo());  // the stroke
    CHECK(doc.undo());  // the level change
    CHECK_EQ(doc.level(), 0);
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
    CHECK(doc.redo());
    CHECK(doc.redo());
    CHECK_EQ(ray_mismatches(doc, rng, aim), 0);
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
