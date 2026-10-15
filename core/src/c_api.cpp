// SPDX-License-Identifier: MIT
#include "cruth/cruth.h"

#include <algorithm>
#include <exception>
#include <new>
#include <string>

#include "cruth/document.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"

struct CruthDocument {
    cruth::Document doc;
    explicit CruthDocument(cruth::Mesh mesh) : doc(std::move(mesh)) {}
};

namespace {

thread_local std::string g_last_error;

CruthStatus fail(CruthStatus status, std::string message) {
    g_last_error = std::move(message);
    return status;
}

CruthStatus from_error(const cruth::Error& e) {
    switch (e.code) {
        case cruth::ErrorCode::InvalidArgument: return fail(CRUTH_ERROR_INVALID_ARGUMENT, e.message);
        case cruth::ErrorCode::Io: return fail(CRUTH_ERROR_IO, e.message);
        case cruth::ErrorCode::Parse: return fail(CRUTH_ERROR_PARSE, e.message);
        case cruth::ErrorCode::UnsupportedFormat: return fail(CRUTH_ERROR_UNSUPPORTED_FORMAT, e.message);
        case cruth::ErrorCode::NonManifold: return fail(CRUTH_ERROR_NON_MANIFOLD, e.message);
        case cruth::ErrorCode::OutOfMemory: return fail(CRUTH_ERROR_OUT_OF_MEMORY, e.message);
        case cruth::ErrorCode::Internal: return fail(CRUTH_ERROR_INTERNAL, e.message);
    }
    return fail(CRUTH_ERROR_INTERNAL, e.message);
}

// Runs fn and converts any escaping C++ exception into a status.
template <typename Fn>
CruthStatus guarded(Fn&& fn) {
    try {
        g_last_error.clear();
        return fn();
    } catch (const std::bad_alloc&) {
        return fail(CRUTH_ERROR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& e) {
        return fail(CRUTH_ERROR_INTERNAL, e.what());
    } catch (...) {
        return fail(CRUTH_ERROR_INTERNAL, "unknown error");
    }
}

template <typename T, typename Fn>
T guarded_value(T fallback, Fn&& fn) noexcept {
    try {
        return fn();
    } catch (...) {
        return fallback;
    }
}

cruth::Vec3 to_core(CruthVec3 v) { return {v.x, v.y, v.z}; }
CruthVec3 to_c(cruth::Vec3 v) { return {v.x, v.y, v.z}; }

cruth::Curve falloff_curve(CruthFalloff f) {
    switch (f) {
        case CRUTH_FALLOFF_SPHERE: return cruth::Curve::sphere_falloff();
        case CRUTH_FALLOFF_LINEAR: return cruth::Curve::linear_falloff();
        case CRUTH_FALLOFF_SHARP: return cruth::Curve::sharp_falloff();
        case CRUTH_FALLOFF_CONSTANT: return cruth::Curve::constant(1.0f);
        case CRUTH_FALLOFF_SMOOTH: break;
    }
    return cruth::Curve::smooth_falloff();
}

bool valid_brush_type(CruthBrushType t) { return t >= CRUTH_BRUSH_STANDARD && t <= CRUTH_BRUSH_MASK; }

CruthStatus make_document(cruth::Mesh mesh, CruthDocument** out) {
    *out = new CruthDocument(std::move(mesh));
    return CRUTH_OK;
}

}  // namespace

extern "C" {

const char* cruth_version(void) { return "0.1.0"; }

const char* cruth_status_string(CruthStatus status) {
    switch (status) {
        case CRUTH_OK: return "ok";
        case CRUTH_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case CRUTH_ERROR_IO: return "i/o error";
        case CRUTH_ERROR_PARSE: return "parse error";
        case CRUTH_ERROR_UNSUPPORTED_FORMAT: return "unsupported format";
        case CRUTH_ERROR_NON_MANIFOLD: return "non-manifold mesh";
        case CRUTH_ERROR_OUT_OF_MEMORY: return "out of memory";
        case CRUTH_ERROR_INTERNAL: return "internal error";
    }
    return "unknown status";
}

const char* cruth_last_error(void) { return g_last_error.c_str(); }

CruthBrush cruth_brush_default(CruthBrushType type) {
    CruthBrush b{};
    b.type = valid_brush_type(type) ? type : CRUTH_BRUSH_STANDARD;
    b.falloff = CRUTH_FALLOFF_SMOOTH;
    b.radius = 0.15f;
    b.strength = 0.5f;
    b.spacing = 0.15f;
    b.invert = false;
    b.pressure_affects_radius = false;
    b.pressure_affects_strength = true;
    b.respect_mask = true;
    b.symmetry = CRUTH_SYMMETRY_X;
    b.radial_count = 1;
    b.radial_axis = 1;
    if (b.type == CRUTH_BRUSH_SMOOTH) b.strength = 0.7f;
    if (b.type == CRUTH_BRUSH_GRAB) b.radius = 0.3f;
    return b;
}

CruthStatus cruth_document_create_sphere(int32_t levels, float radius, CruthDocument** out) {
    if (!out || radius <= 0) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "out must be non-null and radius > 0");
    *out = nullptr;
    return guarded([&] { return make_document(cruth::make_quad_sphere(levels, radius), out); });
}

CruthStatus cruth_document_create_plane(int32_t divisions, float size, CruthDocument** out) {
    if (!out || size <= 0) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "out must be non-null and size > 0");
    *out = nullptr;
    return guarded([&] { return make_document(cruth::make_plane(divisions, size), out); });
}

CruthStatus cruth_document_load(const char* path, CruthDocument** out) {
    if (!out || !path) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    *out = nullptr;
    return guarded([&] {
        auto mesh = cruth::load_mesh(path);
        if (!mesh) return from_error(mesh.error());
        return make_document(std::move(mesh).value(), out);
    });
}

CruthStatus cruth_document_save(const CruthDocument* doc, const char* path) {
    if (!doc || !path) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        auto status = doc->doc.save(path);
        return status ? CRUTH_OK : from_error(status.error());
    });
}

void cruth_document_destroy(CruthDocument* doc) { delete doc; }

size_t cruth_document_vertex_count(const CruthDocument* doc) { return doc ? doc->doc.mesh().vertex_count() : 0; }

size_t cruth_document_triangle_count(const CruthDocument* doc) {
    return doc ? doc->doc.mesh().triangle_count() : 0;
}

uint64_t cruth_document_geometry_revision(const CruthDocument* doc) { return doc ? doc->doc.geometry_revision() : 0; }
uint64_t cruth_document_topology_revision(const CruthDocument* doc) { return doc ? doc->doc.topology_revision() : 0; }

CruthStatus cruth_document_copy_vertices(const CruthDocument* doc, float* out, size_t float_capacity) {
    if (!doc || !out) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    const auto& mesh = doc->doc.mesh();
    const auto& normals = doc->doc.normals();
    const auto& mask = doc->doc.mask();
    if (float_capacity < mesh.vertex_count() * 7) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "buffer too small");
    for (std::size_t i = 0; i < mesh.vertex_count(); ++i) {
        float* o = out + i * 7;
        o[0] = mesh.positions[i].x;
        o[1] = mesh.positions[i].y;
        o[2] = mesh.positions[i].z;
        o[3] = normals[i].x;
        o[4] = normals[i].y;
        o[5] = normals[i].z;
        o[6] = mask[i];
    }
    return CRUTH_OK;
}

CruthStatus cruth_document_copy_triangles(const CruthDocument* doc, uint32_t* out, size_t index_capacity) {
    if (!doc || !out) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        const auto& mesh = doc->doc.mesh();
        if (index_capacity < mesh.triangle_count() * 3) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "buffer too small");
        std::size_t k = 0;
        for (std::size_t f = 0; f < mesh.face_count(); ++f) {
            auto fv = mesh.face(f);
            for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
                out[k++] = fv[0];
                out[k++] = fv[i];
                out[k++] = fv[i + 1];
            }
        }
        return CRUTH_OK;
    });
}

void cruth_document_bounds(const CruthDocument* doc, CruthVec3* min, CruthVec3* max) {
    cruth::Bounds b;
    if (doc) b = doc->doc.mesh().bounds();
    if (b.empty()) b.lo = b.hi = {};
    if (min) *min = to_c(b.lo);
    if (max) *max = to_c(b.hi);
}

bool cruth_document_raycast(const CruthDocument* doc, CruthVec3 origin, CruthVec3 direction, CruthRayHit* hit) {
    if (!doc) return false;
    return guarded_value(false, [&] {
        auto h = doc->doc.raycast(to_core(origin), to_core(direction));
        if (!h) return false;
        if (hit) *hit = {to_c(h->position), to_c(h->normal), h->distance};
        return true;
    });
}

CruthStatus cruth_stroke_begin(CruthDocument* doc, const CruthBrush* brush) {
    if (!doc || !brush) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    if (!valid_brush_type(brush->type)) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "unknown brush type");
    if (!(brush->radius > 0)) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "radius must be > 0");
    return guarded([&] {
        cruth::BrushSettings s;
        s.type = static_cast<cruth::BrushType>(brush->type);
        s.radius = brush->radius;
        s.strength = std::clamp(brush->strength, 0.0f, 1.0f);
        s.spacing = brush->spacing > 0 ? brush->spacing : 0.15f;
        s.invert = brush->invert;
        s.falloff = falloff_curve(brush->falloff);
        s.pressure_affects_radius = brush->pressure_affects_radius;
        s.pressure_affects_strength = brush->pressure_affects_strength;
        s.respect_mask = brush->respect_mask;
        s.symmetry.x = brush->symmetry & CRUTH_SYMMETRY_X;
        s.symmetry.y = brush->symmetry & CRUTH_SYMMETRY_Y;
        s.symmetry.z = brush->symmetry & CRUTH_SYMMETRY_Z;
        s.symmetry.radial_count = std::clamp(brush->radial_count, 1, 64);
        s.symmetry.radial_axis = std::clamp(brush->radial_axis, 0, 2);
        doc->doc.begin_stroke(s);
        return CRUTH_OK;
    });
}

CruthStatus cruth_stroke_add_sample(CruthDocument* doc, const CruthStrokeSample* sample) {
    if (!doc || !sample) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        doc->doc.add_stroke_sample({to_core(sample->position), std::clamp(sample->pressure, 0.0f, 1.0f)});
        return CRUTH_OK;
    });
}

CruthStatus cruth_stroke_end(CruthDocument* doc) {
    if (!doc) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        doc->doc.end_stroke();
        return CRUTH_OK;
    });
}

void cruth_mask_clear(CruthDocument* doc) {
    if (doc) doc->doc.clear_mask();
}

void cruth_mask_invert(CruthDocument* doc) {
    if (doc) doc->doc.invert_mask();
}

CruthStatus cruth_document_subdivide(CruthDocument* doc, int32_t levels) {
    if (!doc || levels < 1 || levels > 4) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "levels must be 1..4");
    return guarded([&] {
        auto status = doc->doc.subdivide(levels);
        return status ? CRUTH_OK : from_error(status.error());
    });
}

int32_t cruth_document_level_count(const CruthDocument* doc) { return doc ? doc->doc.level_count() : 0; }
int32_t cruth_document_level(const CruthDocument* doc) { return doc ? doc->doc.level() : 0; }

CruthStatus cruth_document_set_level(CruthDocument* doc, int32_t level) {
    if (!doc) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        auto status = doc->doc.set_level(level);
        return status ? CRUTH_OK : from_error(status.error());
    });
}

void cruth_document_set_base_locked(CruthDocument* doc, bool locked) {
    if (doc) doc->doc.set_base_locked(locked);
}

CruthStatus cruth_document_voxel_remesh(CruthDocument* doc, int32_t resolution) {
    if (!doc || resolution < 8 || resolution > 512) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "resolution must be 8..512");
    return guarded([&] {
        cruth::VoxelRemeshOptions options;
        options.resolution = resolution;
        auto result = doc->doc.voxel_remesh(options);
        return result ? CRUTH_OK : from_error(result.error());
    });
}

CruthStatus cruth_document_quad_remesh(CruthDocument* doc, uint64_t target_faces, bool preserve_sharp) {
    if (!doc || target_faces < 8) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "target must be >= 8 faces");
    return guarded([&] {
        cruth::QuadRemeshOptions options;
        options.target_faces = static_cast<std::size_t>(target_faces);
        options.preserve_sharp = preserve_sharp;
        auto result = doc->doc.quad_remesh(options);
        return result ? CRUTH_OK : from_error(result.error());
    });
}

CruthStatus cruth_document_decimate(CruthDocument* doc, uint64_t target_triangles) {
    if (!doc || target_triangles < 4) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "target must be >= 4 triangles");
    return guarded([&] {
        doc->doc.decimate(static_cast<std::size_t>(target_triangles));
        return CRUTH_OK;
    });
}

CruthStatus cruth_document_check_print(const CruthDocument* doc, CruthPrintReport* report) {
    if (!doc || !report) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        auto r = doc->doc.check_printability();
        *report = {};
        report->vertices = r.vertices;
        report->faces = r.faces;
        report->triangles = r.triangles;
        report->shells = r.shells;
        report->boundary_edges = r.boundary_edges;
        report->holes = r.holes;
        report->non_manifold_edges = r.non_manifold_edges;
        report->non_manifold_vertices = r.non_manifold_vertices;
        report->inconsistent_edges = r.inconsistent_edges;
        report->degenerate_faces = r.degenerate_faces;
        report->unreferenced_vertices = r.unreferenced_vertices;
        report->self_intersections = r.self_intersections;
        report->volume = r.volume;
        report->surface_area = r.surface_area;
        cruth::Bounds b = r.bounds;
        if (b.empty()) b.lo = b.hi = {};
        report->bounds_min = to_c(b.lo);
        report->bounds_max = to_c(b.hi);
        report->watertight = r.watertight();
        report->printable = r.printable();
        return CRUTH_OK;
    });
}

CruthStatus cruth_document_repair_print(CruthDocument* doc, CruthRepairLog* log) {
    if (!doc) return fail(CRUTH_ERROR_INVALID_ARGUMENT, "null argument");
    return guarded([&] {
        auto r = doc->doc.repair_for_print();
        if (log) {
            *log = {};
            log->welded_vertices = r.welded_vertices;
            log->removed_degenerate_faces = r.removed_degenerate_faces;
            log->removed_duplicate_faces = r.removed_duplicate_faces;
            log->split_vertices = r.split_vertices;
            log->flipped_faces = r.flipped_faces;
            log->filled_holes = r.filled_holes;
            log->inverted_shells = r.inverted_shells;
            log->removed_vertices = r.removed_vertices;
        }
        return CRUTH_OK;
    });
}

CruthStatus cruth_document_export_game_asset(CruthDocument* doc, const char* directory, const char* name,
                                             uint64_t target_triangles, int32_t texture_size,
                                             CruthAssetReport* report) {
    if (!doc || !directory || !name || texture_size < 64 || texture_size > 8192) {
        return fail(CRUTH_ERROR_INVALID_ARGUMENT, "directory and name are required; texture_size must be 64..8192");
    }
    return guarded([&] {
        cruth::GameAssetOptions o;
        o.name = name;
        o.target_triangles = static_cast<std::size_t>(target_triangles);
        o.texture_size = texture_size;
        auto r = doc->doc.export_game_asset(directory, o);
        if (!r) return from_error(r.error());
        if (report) {
            *report = {};
            report->triangles = r.value().triangles;
            report->charts = r.value().charts;
            report->texels = r.value().texels;
            report->misses = r.value().misses;
            report->file_count = r.value().files.size();
        }
        return CRUTH_OK;
    });
}

bool cruth_undo(CruthDocument* doc) { return doc && guarded_value(false, [&] { return doc->doc.undo(); }); }
bool cruth_redo(CruthDocument* doc) { return doc && guarded_value(false, [&] { return doc->doc.redo(); }); }
bool cruth_can_undo(const CruthDocument* doc) { return doc && doc->doc.can_undo(); }
bool cruth_can_redo(const CruthDocument* doc) { return doc && doc->doc.can_redo(); }

}  // extern "C"
