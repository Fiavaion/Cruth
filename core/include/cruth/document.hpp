// Cruth core — the editing session for one object: multires stack, sculptor,
// picking structure and undo history.
//
// Every user-visible operation goes through Document as a command so that the
// iPad shell, the desktop shell and the headless CLI drive identical code.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cruth/bvh.hpp"
#include "cruth/decimate.hpp"
#include "cruth/mesh.hpp"
#include "cruth/multires.hpp"
#include "cruth/print_check.hpp"
#include "cruth/quad_remesh.hpp"
#include "cruth/raycast.hpp"
#include "cruth/result.hpp"
#include "cruth/sculpt.hpp"
#include "cruth/voxel_remesh.hpp"

namespace cruth {

// "Finish" the sculpt into a game-ready asset (PRD acceptance chain, minus
// retopology): decimate the most detailed level, unwrap it, and bake the
// sculpt onto it.
struct GameAssetOptions {
    std::string name = "cruth";
    std::size_t target_triangles = 20000;
    int texture_size = 2048;
    bool ambient_occlusion = true;
    int ao_samples = 32;
};

struct GameAssetReport {
    std::size_t triangles = 0;
    std::size_t charts = 0;
    std::size_t texels = 0;
    std::size_t misses = 0;
    std::vector<std::string> files;
};

class Document {
public:
    Document();
    explicit Document(Mesh mesh);

    static Result<std::unique_ptr<Document>> load(const std::string& path);
    Status save(const std::string& path) const;

    // The mesh being edited: the current multires level.
    const Mesh& mesh() const { return mesh_; }
    const std::vector<Vec3>& normals() const { return sculptor_.normals(); }
    const std::vector<float>& mask() const { return sculptor_.mask(); }

    // The most detailed version of the sculpt (top multires level), with any
    // pending strokes folded in: the reference for retopology and baking.
    const Mesh& sculpt_reference();

    // Replace the whole object (drops multires levels) as one undoable step.
    void set_mesh(Mesh mesh, std::string label);

    // Strokes (PRD B-01..B-04).
    void begin_stroke(const BrushSettings& brush);
    void add_stroke_sample(const StrokeSample& sample);
    void end_stroke();
    bool in_stroke() const { return sculptor_.in_stroke(); }

    // Multires (PRD S-01).
    int level() const { return level_; }
    int level_count() const { return multires_.level_count(); }
    Status subdivide(int levels = 1);  // adds levels on top and moves there
    Status set_level(int level);
    bool base_locked() const { return multires_.base_locked(); }
    void set_base_locked(bool locked) { multires_.set_base_locked(locked); }

    // Topology commands; each replaces the object and resets multires.
    Result<VoxelRemeshStats> voxel_remesh(const VoxelRemeshOptions& options);
    // With detail_levels > 0 the result is subdivided that many times and the
    // finest level projected onto the old sculpt, so detail carries over as
    // multires levels on the new topology (PRD R-04, T-06).
    Result<QuadRemeshStats> quad_remesh(const QuadRemeshOptions& options, int detail_levels = 0);
    // Replaces the object with a new low-poly mesh (e.g. from RetopoMesh) and,
    // as above, carries the sculpt over as `detail_levels` multires levels.
    Status adopt_retopology(Mesh low, int detail_levels, std::string label = "Retopology");
    DecimateStats decimate(std::size_t target_triangles);
    RepairLog repair_for_print(const RepairOptions& options = {});
    PrintReport check_printability() const;

    // Writes <name>.obj (with UVs), <name>_normal.png, <name>_ao.png and
    // <name>_height.exr into `directory`. Does not change the document.
    Result<GameAssetReport> export_game_asset(const std::string& directory, const GameAssetOptions& options);

    void clear_mask();
    void invert_mask();

    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction) const;

    // Linear undo; branch history (PRD L-04) builds on these entries.
    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const { return cursor_ < history_.size(); }
    bool undo();
    bool redo();
    std::string undo_label() const;
    std::string redo_label() const;

    // Renderers re-upload vertex data when geometry_revision changes and
    // index data when topology_revision changes.
    std::uint64_t geometry_revision() const { return sculptor_.revision() + topology_revision_; }
    std::uint64_t topology_revision() const { return topology_revision_; }

private:
    struct State {
        MultiresStack stack;
        int level = 0;
    };
    using Snapshot = std::shared_ptr<const State>;
    struct StateSwap {
        Snapshot before, after;
    };
    struct Entry {
        std::string label;
        std::variant<SculptDelta, StateSwap> change;
    };

    void push(Entry entry);
    void commit_level();
    Snapshot snapshot();
    void restore(const Snapshot& state);
    void apply_state(State state, std::string label);
    Status replace_keeping_detail(Mesh low, int detail_levels, std::string label);
    const TriangleBVH& bvh() const;

    MultiresStack multires_;
    int level_ = 0;
    Mesh mesh_;
    Sculptor sculptor_;
    std::vector<Entry> history_;
    std::size_t cursor_ = 0;
    std::uint64_t topology_revision_ = 0;
    std::string stroke_label_;
    Snapshot cached_snapshot_;  // valid while the stack is unchanged

    mutable TriangleBVH bvh_;
    mutable std::uint64_t bvh_topology_ = ~std::uint64_t(0);
    mutable std::uint64_t bvh_geometry_ = ~std::uint64_t(0);
    mutable std::uint64_t bvh_journal_ = 0;  // sculptor journal mark the BVH is synced to
};

const char* brush_name(BrushType type);

}  // namespace cruth
