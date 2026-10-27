// SPDX-License-Identifier: MIT
#include <numbers>

#include "cruth/half_edge.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/print_check.hpp"
#include "cruth/raycast.hpp"
#include "cruth/subdivide.hpp"
#include "test.hpp"

using namespace cruth;

TEST(cube_is_closed_and_outward) {
    Mesh cube = make_cube(2.0f);
    CHECK_EQ(cube.vertex_count(), 8u);
    CHECK_EQ(cube.face_count(), 6u);
    CHECK_EQ(cube.triangle_count(), 12u);
    PrintReport r = check_printability(cube);
    CHECK(r.watertight());
    CHECK(r.printable());
    CHECK_NEAR(r.volume, 8.0, 1e-5);
    CHECK_NEAR(r.surface_area, 24.0, 1e-5);
    CHECK_EQ(r.shells, 1u);
    // Every face normal points away from the centre.
    for (std::size_t f = 0; f < cube.face_count(); ++f) CHECK(dot(cube.face_normal(f), cube.face_centroid(f)) > 0);
}

TEST(unique_edges_are_polygon_edges_counted_once) {
    CHECK(Mesh().unique_edges().empty());

    Mesh quad;
    for (int i = 0; i < 4; ++i) quad.add_vertex({float(i & 1), float(i >> 1), 0});
    quad.add_face({0, 1, 3, 2});
    const auto q = quad.unique_edges();
    REQUIRE(q.size() == 8u);  // 4 edges, no fan diagonal
    for (std::size_t e = 0; e < q.size(); e += 2) {
        CHECK(q[e] < q[e + 1]);
        CHECK(!(q[e] == 0 && q[e + 1] == 3) && !(q[e] == 1 && q[e + 1] == 2));  // the diagonals
    }

    // Two triangles sharing an edge (given in opposite directions): 5 edges.
    Mesh pair;
    for (int i = 0; i < 4; ++i) pair.add_vertex({float(i), float(i % 2), 0});
    pair.add_face({0, 1, 2});
    pair.add_face({2, 1, 3});
    CHECK_EQ(pair.unique_edges().size(), 10u);

    CHECK_EQ(make_cube().unique_edges().size(), 24u);
    // A level-n quad sphere has 6·4ⁿ quads, so 12·4ⁿ edges (each quad has 4, each edge 2 quads).
    for (int n = 0; n <= 3; ++n) {
        const Mesh sphere = make_quad_sphere(n);
        const auto edges = sphere.unique_edges();
        CHECK_EQ(edges.size(), 2u * 12u * (std::size_t(1) << (2 * n)));
        for (std::size_t e = 0; e < edges.size(); e += 2) {
            CHECK(edges[e] < edges[e + 1]);
            CHECK(edges[e + 1] < sphere.vertex_count());
        }
    }
}

TEST(half_edge_build_and_roundtrip) {
    Mesh cube = make_cube();
    auto he = HalfEdgeMesh::build(cube);
    REQUIRE(he.is_ok());
    CHECK_EQ(he.value().edge_count(), 12u);
    CHECK_EQ(he.value().half_edges.size(), 24u);
    for (Index h = 0; h < he.value().half_edges.size(); ++h) {
        const auto& e = he.value().half_edges[h];
        REQUIRE(e.twin != kInvalidIndex);
        CHECK_EQ(he.value().half_edges[e.twin].twin, h);
        CHECK_EQ(he.value().half_edges[e.next].prev, h);
    }
    Mesh back = he.value().to_mesh();
    CHECK_EQ(back.face_count(), cube.face_count());
    CHECK(back.face_indices() == cube.face_indices());
}

TEST(half_edge_rejects_non_manifold_edge) {
    Mesh m;
    for (int i = 0; i < 5; ++i) m.add_vertex({float(i), float(i * i), 0});
    m.add_face({0, 1, 2});
    m.add_face({1, 0, 3});
    m.add_face({0, 1, 4});  // third face on edge 0-1, same direction as the first
    auto he = HalfEdgeMesh::build(m);
    CHECK(!he.is_ok());
    if (!he.is_ok()) CHECK(he.error().code == ErrorCode::NonManifold);
}

TEST(catmull_clark_counts_and_closure) {
    Mesh cube = make_cube();
    auto sub = subdivide_catmull_clark(cube);
    REQUIRE(sub.is_ok());
    // V' = V + E + F, F' = sum of face sizes.
    CHECK_EQ(sub.value().vertex_count(), 8u + 12u + 6u);
    CHECK_EQ(sub.value().face_count(), 24u);
    PrintReport r = check_printability(sub.value());
    CHECK(r.printable());
    // Corner (valence 3): (Q + 2R) / 3 = (-1/6 - 2/3) / 3 = -5/18 per axis.
    CHECK_NEAR(sub.value().positions[0].x, -5.0 / 18.0, 1e-6);
    CHECK_NEAR(sub.value().positions[0].y, -5.0 / 18.0, 1e-6);
    CHECK_NEAR(sub.value().bounds().center().x, 0.0, 1e-6);
}

TEST(catmull_clark_boundary_plane_stays_flat) {
    Mesh plane = make_plane(4, 2.0f);
    auto sub = subdivide_catmull_clark(plane, 2);
    REQUIRE(sub.is_ok());
    CHECK_EQ(sub.value().face_count(), 16u * 16u);
    for (const auto& p : sub.value().positions) CHECK_NEAR(p.y, 0.0, 1e-6);
    // Smooth boundary rule keeps the straight border on the original extent.
    CHECK_NEAR(sub.value().bounds().size().x, 2.0, 1e-5);
}

TEST(quad_sphere_is_round_and_printable) {
    Mesh s = make_quad_sphere(4, 2.0f);
    CHECK_EQ(s.face_count(), 6u * 256u);
    for (const auto& p : s.positions) CHECK_NEAR(length(p), 2.0, 1e-4);
    PrintReport r = check_printability(s);
    CHECK(r.printable());
    CHECK_NEAR(r.volume, 4.0 / 3.0 * std::numbers::pi * 8.0, 0.4);
}

TEST(obj_roundtrip_and_negative_indices) {
    Mesh cube = make_cube();
    auto back = read_obj(write_obj(cube));
    REQUIRE(back.is_ok());
    CHECK(back.value().positions == cube.positions);
    CHECK(back.value().face_indices() == cube.face_indices());

    auto rel = read_obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf -3/1 -2/1 -1/1\n");
    REQUIRE(rel.is_ok());
    CHECK_EQ(rel.value().face_count(), 1u);
    CHECK_EQ(rel.value().face(0)[2], 2u);

    CHECK(!read_obj("v 0 0 0\nf 1 2 3\n").is_ok());
    CHECK(!read_obj("v 0 zero 0\n").is_ok());
}

TEST(stl_binary_roundtrip_welds_vertices) {
    Mesh cube = make_cube();
    std::string bytes = write_stl_binary(cube);
    CHECK_EQ(bytes.size(), 84u + 12u * 50u);
    auto back = read_stl(bytes);
    REQUIRE(back.is_ok());
    CHECK_EQ(back.value().vertex_count(), 8u);
    CHECK_EQ(back.value().face_count(), 12u);
    CHECK(check_printability(back.value()).printable());
}

TEST(stl_ascii_import) {
    const char* text =
        "solid t\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n"
        "  endloop\n endfacet\nendsolid t\n";
    auto m = read_stl(text);
    REQUIRE(m.is_ok());
    CHECK_EQ(m.value().face_count(), 1u);
    CHECK_EQ(m.value().vertex_count(), 3u);
}

TEST(ply_binary_and_ascii) {
    Mesh cube = make_cube();
    auto back = read_ply(write_ply_binary(cube));
    REQUIRE(back.is_ok());
    CHECK(back.value().positions == cube.positions);
    CHECK(back.value().face_indices() == cube.face_indices());

    const char* ascii =
        "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
        "property uchar red\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n"
        "0 0 0 255\n1 0 0 255\n0 1 0 255\n3 0 1 2\n";
    auto m = read_ply(ascii);
    REQUIRE(m.is_ok());
    CHECK_EQ(m.value().vertex_count(), 3u);
    CHECK_EQ(m.value().face_count(), 1u);
    CHECK_NEAR(m.value().positions[1].x, 1.0, 0);
}

TEST(print_check_finds_and_repair_fills_hole) {
    Mesh sphere = make_quad_sphere(2, 1.0f);
    Mesh holed;
    holed.positions = sphere.positions;
    for (std::size_t f = 1; f < sphere.face_count(); ++f) holed.add_face(sphere.face(f));
    PrintReport before = check_printability(holed);
    CHECK(!before.watertight());
    CHECK_EQ(before.holes, 1u);
    CHECK_EQ(before.boundary_edges, 4u);

    RepairLog log = repair_for_print(holed);
    CHECK_EQ(log.filled_holes, 1u);
    PrintReport after = check_printability(holed);
    CHECK(after.printable());
}

TEST(repair_fixes_winding_and_inside_out) {
    Mesh cube = make_cube();
    Mesh broken;
    broken.positions = cube.positions;
    for (std::size_t f = 0; f < cube.face_count(); ++f) {
        std::vector<Index> c(cube.face(f).begin(), cube.face(f).end());
        if (f == 2) std::reverse(c.begin(), c.end());  // one face flipped
        broken.add_face(c);
    }
    CHECK(check_printability(broken).inconsistent_edges > 0);
    repair_for_print(broken);
    PrintReport r = check_printability(broken);
    CHECK_EQ(r.inconsistent_edges, 0u);
    CHECK(r.volume > 0);
    CHECK(r.printable());

    Mesh inside_out;
    inside_out.positions = cube.positions;
    for (std::size_t f = 0; f < cube.face_count(); ++f) {
        std::vector<Index> c(cube.face(f).begin(), cube.face(f).end());
        std::reverse(c.begin(), c.end());
        inside_out.add_face(c);
    }
    CHECK(check_printability(inside_out).volume < 0);
    RepairLog log = repair_for_print(inside_out);
    CHECK_EQ(log.inverted_shells, 1u);
    CHECK(check_printability(inside_out).printable());
}

TEST(print_check_detects_bowtie_vertex_and_shells) {
    // Two tetrahedra sharing only vertex 0.
    Mesh m;
    m.add_vertex({0, 0, 0});
    for (float s : {1.0f, -1.0f}) {
        Index a = m.add_vertex({s, 0, 0}), b = m.add_vertex({0, s, 0}), c = m.add_vertex({0, 0, s});
        m.add_face({0, b, a});
        m.add_face({0, c, b});
        m.add_face({0, a, c});
        m.add_face({a, b, c});
    }
    PrintReport r = check_printability(m);
    CHECK_EQ(r.non_manifold_vertices, 1u);
    CHECK_EQ(r.shells, 1u);  // connected through the shared vertex
    CHECK(!r.printable());
}

TEST(raycast_hits_nearest_face) {
    Mesh cube = make_cube(2.0f);
    auto hit = raycast(cube, {0, 0, 5}, {0, 0, -1});
    REQUIRE(hit.has_value());
    CHECK_NEAR(hit->position.z, 1.0, 1e-5);
    CHECK_NEAR(hit->distance, 4.0, 1e-5);
    CHECK_NEAR(hit->normal.z, 1.0, 1e-5);
    CHECK(!raycast(cube, {0, 0, 5}, {0, 0, 1}).has_value());
}

TEST(weld_with_tolerance) {
    Mesh m;
    m.add_vertex({0, 0, 0});
    m.add_vertex({1, 0, 0});
    m.add_vertex({0, 1, 0});
    m.add_vertex({1e-5f, 0, 0});
    m.add_face({0, 1, 2});
    m.add_face({3, 2, 1});
    CHECK_EQ(weld_vertices(m, 0.0f), 0u);
    CHECK_EQ(weld_vertices(m, 1e-3f), 1u);
    CHECK_EQ(m.vertex_count(), 3u);
    CHECK_EQ(m.face(1)[0], 0u);
}
