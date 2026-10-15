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
    uint64_t inconsistent_edges, degenerate_faces, unreferenced_vertices;
    double volume, surface_area;
    CruthVec3 bounds_min, bounds_max;
    bool watertight;
    bool printable;
} CruthPrintReport;

typedef struct CruthRepairLog {
    uint64_t welded_vertices, removed_degenerate_faces, removed_duplicate_faces;
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

/* Commands ----------------------------------------------------------------- */

CruthStatus cruth_document_subdivide(CruthDocument* doc, int32_t levels);
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
