// Cruth core — brush engine (PRD B-01, B-02, B-04, N-02, N-03).
//
// A Sculptor deforms a Mesh in place. The shell feeds it surface-hit samples
// (position, pressure) in stroke order; the sculptor spaces dabs along the
// stroke, applies them with symmetry, keeps vertex normals current for the
// touched region and records an exact delta for undo.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "cruth/curve.hpp"
#include "cruth/mesh.hpp"

namespace cruth {

enum class BrushType {
    Standard,  // push along the brush normal
    Clay,      // fill up to a plane above the surface
    Inflate,   // push along each vertex normal
    Smooth,    // Laplacian relax
    Flatten,   // pull onto the average plane
    Pinch,     // pull toward the stroke centre along the surface
    Crease,    // cut in and pinch
    Grab,      // move captured vertices with the stroke
    Mask,      // paint the protection mask
};

struct Symmetry {
    bool x = false, y = false, z = false;  // mirror planes through the origin
    int radial_count = 1;                  // >1 repeats each dab around radial_axis
    int radial_axis = 1;                   // 0 = X, 1 = Y, 2 = Z
};

struct BrushSettings {
    BrushType type = BrushType::Standard;
    float radius = 0.1f;    // world units
    float strength = 0.5f;  // 0..1
    bool invert = false;
    float spacing = 0.15f;  // dab spacing as a fraction of the radius
    Curve falloff = Curve::smooth_falloff();
    bool pressure_affects_radius = false;
    bool pressure_affects_strength = true;
    Curve pressure_curve = Curve::identity();
    Symmetry symmetry;
    bool respect_mask = true;
};

struct StrokeSample {
    Vec3 position;     // point on the surface
    float pressure = 1.0f;
};

// Exact before/after record of one stroke, for undo/redo.
struct SculptDelta {
    std::vector<Index> vertices;
    std::vector<Vec3> before, after;
    std::vector<Index> mask_vertices;
    std::vector<float> mask_before, mask_after;

    bool empty() const { return vertices.empty() && mask_vertices.empty(); }
};

class Sculptor {
public:
    explicit Sculptor(Mesh& mesh);
    ~Sculptor();
    Sculptor(const Sculptor&) = delete;
    Sculptor& operator=(const Sculptor&) = delete;

    // Call after anything other than the sculptor changes the mesh's topology
    // or many positions at once (load, subdivide, repair, undo of those).
    void rebuild();

    void begin_stroke(const BrushSettings& settings);
    void add_sample(const StrokeSample& sample);
    SculptDelta end_stroke();
    bool in_stroke() const;

    // Apply a delta in either direction, keeping normals and the spatial index current.
    void apply(const SculptDelta& delta, bool forward);

    const std::vector<Vec3>& normals() const;
    std::vector<float>& mask();
    const std::vector<float>& mask() const;
    void clear_mask();
    void invert_mask();

    // Bumped whenever positions, normals or the mask change.
    std::uint64_t revision() const;

    // Journal of moved vertices, so a consumer such as the document's BVH can
    // update only what changed. Keep journal_end() after syncing; moved_since()
    // with that mark then lists every vertex moved since (with repeats), or
    // returns nullopt when the journal no longer reaches back that far (after
    // rebuild() or an overflow) and the consumer must resync in full.
    std::uint64_t journal_end() const;
    std::optional<std::span<const Index>> moved_since(std::uint64_t mark) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cruth
