// Cruth core — manual retopology on a reference surface (PRD T-01..T-03, T-05).
//
// RetopoMesh builds a new low-poly mesh over a high-resolution reference.
// Every vertex it creates or moves is snapped onto the reference, and new
// geometry welds onto existing vertices within weld_radius, so strokes join
// up into one mesh. The operations mirror the PRD's Pencil tools:
//
// - add_strip    stroke along a form -> quad strip            (T-02 Strip)
// - add_contour  stroke across a form -> closed edge loop     (T-02 Contour),
//                bridged to the previous contour loop
// - bridge       two vertex chains or loops -> quads          (T-02 Bridge)
// - add_quad     four points -> one quad                      (T-02 Quad-pen)
// - quad_pen_tap single taps -> quads, chained into strips      (T-02 Quad-pen)
// - insert_loop / slide begin..end / move_vertex / relax / delete_face (T-03)
//
// Every edit is one undo step on the cage's own history (undo / redo).
//
// The shell turns Pencil input into these calls; the result goes back to the
// Document with Document::adopt_retopology (T-06).
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "cruth/bvh.hpp"
#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

struct RetopoStats {
    std::size_t vertices = 0, quads = 0, triangles = 0, ngons = 0;
    std::size_t poles = 0;           // interior vertices with valence != 4
    std::size_t boundary_edges = 0;
};

class RetopoMesh {
public:
    explicit RetopoMesh(const Mesh& reference);
    RetopoMesh(const Mesh& reference, Mesh start);  // continue an existing retopology

    const Mesh& mesh() const { return mesh_; }
    float weld_radius = 0;  // 0 = 1% of the reference's diagonal

    // Nearest point on the reference surface and its normal there.
    Vec3 snap(Vec3 p) const;
    Vec3 normal_at(Vec3 p) const;

    // Quad strip of `segments` quads along the stroke, `width` wide.
    Status add_strip(std::span<const Vec3> stroke, float width, int segments);

    // Closed loop of `count` vertices around the form the stroke crosses: the
    // reference is cut by the plane through the stroke that contains the view
    // direction (the line of sight the stroke was drawn along), and the cut
    // loop nearest the stroke is resampled. Without a view direction, the
    // surface normal at the stroke's middle stands in for it. When the
    // previous contour has the same count, the two are bridged.
    // Returns the new loop's vertices.
    Result<std::vector<Index>> add_contour(std::span<const Vec3> stroke, int count, Vec3 view_direction = {},
                                           bool bridge_previous = true);

    // Quads between two equally long chains. Closed loops (closed = true) are
    // rotated and, if needed, reversed to line up before bridging.
    Status bridge(std::span<const Index> a, std::span<const Index> b, bool closed);

    // One quad through four surface points; corners weld to nearby vertices.
    Status add_quad(const std::array<Vec3, 4>& corners);

    // Quad-pen tap input (T-02 Quad-pen): each tap is one surface point. A tap
    // within weld_radius of a cage vertex reuses that vertex, so new quads share
    // edges with the cage. The fourth pending tap closes a quad; the taps may go
    // round it in any order (the shortest-perimeter order is used). The chain
    // then continues from the quad's last edge, so each further pair of taps
    // adds the next quad of a strip, until quad_pen_lift() ends the chain. A tap
    // that repeats a pending corner, or would close a quad onto an edge that
    // already has two faces or duplicate a face, is rejected and the pending
    // taps are kept. Edits that renumber vertices end the chain.
    struct QuadPenTap {
        std::size_t pending = 0;  // taps waiting for the next quad, 0..3
        bool face_added = false;
    };
    Result<QuadPenTap> quad_pen_tap(Vec3 p);
    void quad_pen_lift();
    // Positions of the pending taps, for an overlay.
    std::vector<Vec3> quad_pen_pending() const;

    // Edge loop through edge (a, b), listed from a then b: it runs straight on
    // across interior valence-4 vertices (and along a border across valence-3
    // border vertices) and stops at poles, borders and where it meets itself.
    struct EdgeLoop {
        std::vector<Index> vertices;
        bool closed = false;
    };
    Result<EdgeLoop> edge_loop(Index a, Index b) const;

    // Slide loop (T-03): slides the edge loop through (a, b) between its
    // neighbouring rings, re-projected onto the reference. update_slide(t) is
    // absolute from where the loop was at begin_slide; t in (-1, 1) is clamped
    // to [-0.99, 0.99]. t > 0 moves each vertex toward its neighbour across the
    // face that has a -> b in its winding (on an outward-wound cage, the left of
    // a -> b seen from outside), t < 0 toward the other side; a vertex with no
    // neighbour on that side stays put. begin_slide returns the slide axis v:
    // how far t = 1 moves the middle of edge (a, b), or, on a border where that
    // side has no face, the reverse of the t = -1 move. A drag d then maps to
    // t = dot(d, v) / dot(v, v) either way.
    // begin..end is one undo step; any other edit ends the slide.
    Result<Vec3> begin_slide(Index a, Index b);
    void update_slide(float t);
    void end_slide() { slide_.reset(); }
    bool sliding() const { return slide_.has_value(); }
    Status slide_loop(Index a, Index b, float t);  // begin, update, end

    // Undo history of cage edits, at most undo_limit steps. Undo and redo end
    // the quad-pen chain and any slide.
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    std::size_t undo_limit = 32;

    // Edge (vertex pair) of the current mesh nearest to p.
    std::pair<Index, Index> nearest_edge(Vec3 p) const;

    // Inserts an edge loop across the quad ring through edge (a, b), cutting
    // each ring edge at fraction t from the side of `a` (T-03 Insert loop).
    // The ring stops at boundaries and non-quad faces (which gain the cut
    // vertex, so the mesh stays watertight). Where the ring crosses itself, the
    // quad is split into four. Returns the new loop's vertices.
    Result<std::vector<Index>> insert_loop(Index a, Index b, float t = 0.5f);

    void move_vertex(Index v, Vec3 p);  // one undo step per call
    // Surface-snapped tangential smoothing; boundary vertices stay put unless asked.
    void relax(int iterations, bool include_boundary = false);
    void delete_face(Index f);

    RetopoStats stats() const;

private:
    struct PenTap {
        Vec3 position;
        Index vertex = kInvalidIndex;  // existing cage vertex, or none yet
    };

    struct Snapshot {
        Mesh mesh;
        std::vector<Index> last_contour;
    };
    struct Slide {
        std::vector<Index> vertices;
        std::vector<Vec3> origin, positive, negative;  // targets at t = 1 and t = -1
    };

    // Records an undo step before an edit, ending the quad-pen chain and any slide.
    void checkpoint() { commit({mesh_, last_contour_}); }
    void commit(Snapshot before);
    Status bridge_chains(std::span<const Index> a, std::span<const Index> b, bool closed);
    Index nearest_vertex(Vec3 p, float radius) const;
    Index add_vertex_welded(Vec3 p);
    void add_face_outward(std::vector<Index> corners);
    void compact();

    TriangleBVH reference_;
    Mesh mesh_;
    std::vector<Index> last_contour_;
    std::vector<PenTap> pen_;
    std::size_t pen_fixed_ = 1;  // leading pending taps whose order is fixed (2 when chaining)
    std::optional<Slide> slide_;
    std::vector<Snapshot> undo_, redo_;
};

struct Polyline {
    std::vector<Vec3> points;
    bool closed = false;
};

// Cuts a mesh with the plane dot(n, x) = d and returns the cross-section:
// closed loops, plus open chains where the cut leaves through an open border.
std::vector<Polyline> slice_mesh(const Mesh& mesh, Vec3 plane_normal, float plane_d);

// Resamples a polyline (closed or open) into `count` points evenly spaced by arc length.
std::vector<Vec3> resample_polyline(std::span<const Vec3> points, int count, bool closed);

}  // namespace cruth
