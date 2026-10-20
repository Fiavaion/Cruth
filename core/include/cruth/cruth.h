/*
 * Cruth core — C API.
 *
 * The only interface the shells use. Plain C so it imports into Swift through a
 * bridging header with no C++ interop, and no C++ exception ever crosses it:
 * every function returns a status or a plain value and catches internally.
 *
 * Coordinates are object space, Y up, right-handed. Index buffers are
 * triangles (polygons are fan-triangulated).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CRUTH_H
#define CRUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CruthDocument CruthDocument;

typedef enum CruthStatus {
    CRUTH_OK = 0,
    CRUTH_ERROR_INVALID_ARGUMENT = 1,
    CRUTH_ERROR_IO = 2,
    CRUTH_ERROR_PARSE = 3,
    CRUTH_ERROR_UNSUPPORTED_FORMAT = 4,
    CRUTH_ERROR_NON_MANIFOLD = 5,
    CRUTH_ERROR_OUT_OF_MEMORY = 6,
    CRUTH_ERROR_INTERNAL = 7,
} CruthStatus;

typedef struct CruthVec3 {
    float x, y, z;
} CruthVec3;

typedef enum CruthBrushType {
    CRUTH_BRUSH_STANDARD = 0,
    CRUTH_BRUSH_CLAY = 1,
    CRUTH_BRUSH_INFLATE = 2,
    CRUTH_BRUSH_SMOOTH = 3,
    CRUTH_BRUSH_FLATTEN = 4,
    CRUTH_BRUSH_PINCH = 5,
    CRUTH_BRUSH_CREASE = 6,
    CRUTH_BRUSH_GRAB = 7,
    CRUTH_BRUSH_MASK = 8,
} CruthBrushType;

typedef enum CruthFalloff {
    CRUTH_FALLOFF_SMOOTH = 0,
    CRUTH_FALLOFF_SPHERE = 1,
    CRUTH_FALLOFF_LINEAR = 2,
    CRUTH_FALLOFF_SHARP = 3,
    CRUTH_FALLOFF_CONSTANT = 4,
} CruthFalloff;

enum {
    CRUTH_SYMMETRY_X = 1 << 0,
    CRUTH_SYMMETRY_Y = 1 << 1,
    CRUTH_SYMMETRY_Z = 1 << 2,
};

typedef struct CruthBrush {
    CruthBrushType type;
    CruthFalloff falloff;
    float radius;   /* object-space units */
    float strength; /* 0..1 */
    float spacing;  /* fraction of radius between dabs, e.g. 0.15 */
    bool invert;
    bool pressure_affects_radius;
    bool pressure_affects_strength;
    bool respect_mask;
    uint32_t symmetry;      /* CRUTH_SYMMETRY_* bits */
    int32_t radial_count;   /* 1 = off */
    int32_t radial_axis;    /* 0 = X, 1 = Y, 2 = Z */
} CruthBrush;

typedef struct CruthStrokeSample {
    CruthVec3 position; /* point on (or near) the surface */
    float pressure;     /* 0..1; use 1 for mouse/finger */
} CruthStrokeSample;

typedef struct CruthRayHit {
    CruthVec3 position;
    CruthVec3 normal;
    float distance;
} CruthRayHit;

typedef struct CruthPrintReport {
    uint64_t vertices, faces, triangles, shells;
    uint64_t boundary_edges, holes, non_manifold_edges, non_manifold_vertices;
    uint64_t inconsistent_edges, degenerate_faces, unreferenced_vertices, self_intersections;
    double volume, surface_area;
    CruthVec3 bounds_min, bounds_max;
    bool watertight;
    bool printable;
} CruthPrintReport;

typedef struct CruthRepairLog {
    uint64_t welded_vertices, removed_degenerate_faces, removed_duplicate_faces, split_vertices;
    uint64_t flipped_faces, filled_holes, inverted_shells, removed_vertices;
} CruthRepairLog;

/* Library ---------------------------------------------------------------- */

const char* cruth_version(void);
const char* cruth_status_string(CruthStatus status);
/* Message of the last error on this thread, or "" — valid until the next call. */
const char* cruth_last_error(void);
CruthBrush cruth_brush_default(CruthBrushType type);

/* Documents -------------------------------------------------------------- */

CruthStatus cruth_document_create_sphere(int32_t levels, float radius, CruthDocument** out);
CruthStatus cruth_document_create_plane(int32_t divisions, float size, CruthDocument** out);
CruthStatus cruth_document_load(const char* path, CruthDocument** out);
CruthStatus cruth_document_save(const CruthDocument* doc, const char* path);
void cruth_document_destroy(CruthDocument* doc);

/* Snapshots -------------------------------------------------------------- */

typedef struct CruthMesh CruthMesh;

/* Copies the mesh cruth_document_save would write (the current level, with any
 * stroke so far). The copy is independent of the document, so it can be saved
 * on another thread while the document keeps changing. */
CruthStatus cruth_document_snapshot(const CruthDocument* doc, CruthMesh** out);
/* Writes a snapshot; the format follows the extension, as for cruth_document_save. */
CruthStatus cruth_mesh_save(const CruthMesh* mesh, const char* path);
void cruth_mesh_destroy(CruthMesh* mesh);

/* Render data -------------------------------------------------------------- */

size_t cruth_document_vertex_count(const CruthDocument* doc);
size_t cruth_document_triangle_count(const CruthDocument* doc);
uint64_t cruth_document_geometry_revision(const CruthDocument* doc);
uint64_t cruth_document_topology_revision(const CruthDocument* doc);
/* Writes vertex_count * 7 floats: position xyz, normal xyz, mask. */
CruthStatus cruth_document_copy_vertices(const CruthDocument* doc, float* out, size_t float_capacity);
/* Writes triangle_count * 3 indices. */
CruthStatus cruth_document_copy_triangles(const CruthDocument* doc, uint32_t* out, size_t index_capacity);
void cruth_document_bounds(const CruthDocument* doc, CruthVec3* min, CruthVec3* max);

/* Picking ------------------------------------------------------------------ */

bool cruth_document_raycast(const CruthDocument* doc, CruthVec3 origin, CruthVec3 direction, CruthRayHit* hit);

/* Sculpting ---------------------------------------------------------------- */

CruthStatus cruth_stroke_begin(CruthDocument* doc, const CruthBrush* brush);
CruthStatus cruth_stroke_add_sample(CruthDocument* doc, const CruthStrokeSample* sample);
CruthStatus cruth_stroke_end(CruthDocument* doc);
void cruth_mask_clear(CruthDocument* doc);
void cruth_mask_invert(CruthDocument* doc);

/* Multires ----------------------------------------------------------------- */

/* Adds `levels` (1..4) subdivision levels on top of the stack and moves there. */
CruthStatus cruth_document_subdivide(CruthDocument* doc, int32_t levels);
int32_t cruth_document_level_count(const CruthDocument* doc);
int32_t cruth_document_level(const CruthDocument* doc);
CruthStatus cruth_document_set_level(CruthDocument* doc, int32_t level);
void cruth_document_set_base_locked(CruthDocument* doc, bool locked);

/* Topology commands (each resets multires) --------------------------------- */

/* resolution: voxels along the longest side, 8..512. */
CruthStatus cruth_document_voxel_remesh(CruthDocument* doc, int32_t resolution);
CruthStatus cruth_document_decimate(CruthDocument* doc, uint64_t target_triangles);
/* Field-aligned quad remesh of the most detailed level (QuadriFlow). target_faces >= 8. */
CruthStatus cruth_document_quad_remesh(CruthDocument* doc, uint64_t target_faces, bool preserve_sharp);

typedef struct CruthQuadRemeshOptions {
    uint64_t target_faces;
    bool preserve_sharp;
    int32_t detail_levels;              /* > 0: subdivide and reproject the sculpt as multires detail */
    const CruthVec3* guide_points;      /* guide strokes, concatenated */
    const uint32_t* guide_stroke_sizes; /* points per stroke */
    uint32_t guide_stroke_count;
} CruthQuadRemeshOptions;

CruthStatus cruth_document_quad_remesh_ex(CruthDocument* doc, const CruthQuadRemeshOptions* options);

/* Retopology (PRD T-01..T-06) ---------------------------------------------- */

typedef struct CruthRetopo CruthRetopo;

typedef struct CruthRetopoStats {
    uint64_t vertices, quads, triangles, ngons, poles, boundary_edges;
} CruthRetopoStats;

/* Starts a retopology over the document's most detailed level. */
CruthStatus cruth_retopo_begin(CruthDocument* doc, CruthRetopo** out);
void cruth_retopo_destroy(CruthRetopo* retopo);
CruthStatus cruth_retopo_add_strip(CruthRetopo* retopo, const CruthVec3* stroke, uint32_t count, float width,
                                   int32_t segments);
/* view_direction: the camera's forward vector when the stroke was drawn. */
CruthStatus cruth_retopo_add_contour(CruthRetopo* retopo, const CruthVec3* stroke, uint32_t count, int32_t loop_vertices,
                                     CruthVec3 view_direction);
CruthStatus cruth_retopo_add_quad(CruthRetopo* retopo, const CruthVec3 corners[4]);

/* Quad pen, tap by tap (T-02). Each tap is one point on the surface; a tap near a
 * cage vertex reuses it, so quads share edges. The 4th pending tap makes a quad
 * (taps in any order round it), then the chain continues from that quad's last
 * edge: every 2 more taps add the next quad of a strip. Lift ends the chain.
 * A rejected tap (repeats a pending corner, or would make a non-manifold or
 * duplicate quad) returns CRUTH_ERROR_INVALID_ARGUMENT and keeps the pending taps. */
typedef struct CruthQuadPenResult {
    uint32_t pending; /* taps waiting for the next quad, 0..3 */
    bool face_added;  /* this tap closed a quad */
} CruthQuadPenResult;

CruthStatus cruth_retopo_quad_pen_tap(CruthRetopo* retopo, CruthVec3 point, CruthQuadPenResult* result);
void cruth_retopo_quad_pen_lift(CruthRetopo* retopo);
/* Pending tap positions for an overlay. Returns the count; writes at most capacity. */
size_t cruth_retopo_quad_pen_pending(const CruthRetopo* retopo, CruthVec3* out, size_t capacity);
void cruth_retopo_relax(CruthRetopo* retopo, int32_t iterations);

/* Edge of the cage nearest to point, as two vertex indices. Returns false when
 * the cage has no edges. */
bool cruth_retopo_nearest_edge(const CruthRetopo* retopo, CruthVec3 point, uint32_t* a, uint32_t* b);

/* Slide loop (T-03): slides the edge loop through edge (a, b) between its
 * neighbouring rings, re-projected onto the surface. The loop runs straight on
 * through valence-4 vertices and stops at poles and borders.
 * begin: picks the loop; writes to axis (nullable) how far t = 1 moves the
 *        middle of edge (a, b) (on a border with no face on that side, the
 *        reverse of the t = -1 move), so a 3D drag d maps to
 *        t = dot(d, axis) / dot(axis, axis).
 * update: t in (-1, 1), absolute from where the loop was at begin (clamped to
 *        +-0.99; NaN or infinity is ignored). t > 0 moves toward the side the axis points to.
 * end:   finishes; begin..end is one cage undo step, taken on the first update
 *        that moves the loop (a slide that never moves records nothing).
 *        Any other edit also ends it. */
CruthStatus cruth_retopo_slide_loop_begin(CruthRetopo* retopo, uint32_t a, uint32_t b, CruthVec3* axis);
void cruth_retopo_slide_loop_update(CruthRetopo* retopo, float t);
void cruth_retopo_slide_loop_end(CruthRetopo* retopo);
/* begin + update + end in one call. */
CruthStatus cruth_retopo_slide_loop(CruthRetopo* retopo, uint32_t a, uint32_t b, float t);

/* Insert loop (T-03) across the quad ring through edge (a, b), each ring edge cut
 * at fraction t (0.01..0.99) from a's side. The ring runs on until a border, a
 * non-quad face (which absorbs the cut) or back to its start. loop_vertices
 * (nullable) receives the new loop's vertex count. */
CruthStatus cruth_retopo_insert_loop(CruthRetopo* retopo, uint32_t a, uint32_t b, float t, uint32_t* loop_vertices);
/* Closed loop around a feature (T-03): stroke = points on the surface drawn
 * round the feature over the cage (ends are joined). A new edge loop runs
 * through the middle of the ring of quads under the stroke; corners become
 * three quads round a centre vertex, so the cage stays all-quad and nothing
 * outside the ring changes. Fails (CRUTH_ERROR_INVALID_ARGUMENT, nothing
 * changed) when the ring crosses a non-quad, strays off the cage or is shorter
 * than 3 faces. loop_vertices (nullable) receives the loop's vertex count. */
CruthStatus cruth_retopo_insert_loop_around(CruthRetopo* retopo, const CruthVec3* stroke, uint32_t count,
                                            uint32_t* loop_vertices);

/* Undo history of cage edits (strips, contours, quads, loops, slides, relax),
 * separate from the document's: cruth_retopo_apply is the document's undo step. */
bool cruth_retopo_undo(CruthRetopo* retopo);
bool cruth_retopo_redo(CruthRetopo* retopo);
bool cruth_retopo_can_undo(const CruthRetopo* retopo);
bool cruth_retopo_can_redo(const CruthRetopo* retopo);
CruthRetopoStats cruth_retopo_stats(const CruthRetopo* retopo);
/* Edges as line segments for an overlay: 6 floats per edge. Returns the edge count;
 * writes at most float_capacity / 6 edges. */
size_t cruth_retopo_copy_edges(const CruthRetopo* retopo, float* out, size_t float_capacity);
/* Replaces the document's object with the retopology (undoable) and carries the
 * sculpt over as detail_levels multires levels. */
CruthStatus cruth_retopo_apply(const CruthRetopo* retopo, CruthDocument* doc, int32_t detail_levels);

/* Game asset (decimate + unwrap + bake) ------------------------------------ */

typedef struct CruthAssetReport {
    uint64_t triangles, charts, texels, misses, file_count;
} CruthAssetReport;

/* Writes <name>.obj, <name>_normal.png, <name>_ao.png and <name>_height.exr
 * into an existing directory. texture_size: 64..8192. */
CruthStatus cruth_document_export_game_asset(CruthDocument* doc, const char* directory, const char* name,
                                             uint64_t target_triangles, int32_t texture_size,
                                             CruthAssetReport* report);

/* Print -------------------------------------------------------------------- */

CruthStatus cruth_document_check_print(const CruthDocument* doc, CruthPrintReport* report);
CruthStatus cruth_document_repair_print(CruthDocument* doc, CruthRepairLog* log);

bool cruth_undo(CruthDocument* doc);
bool cruth_redo(CruthDocument* doc);
bool cruth_can_undo(const CruthDocument* doc);
bool cruth_can_redo(const CruthDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* CRUTH_H */
