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
#include "cruth/raycast.hpp"
#include "cruth/result.hpp"
#include "cruth/sculpt.hpp"
#include "cruth/voxel_remesh.hpp"

namespace cruth {

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
    DecimateStats decimate(std::size_t target_triangles);
    RepairLog repair_for_print(const RepairOptions& options = {});
    PrintReport check_printability() const;

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
};

const char* brush_name(BrushType type);

}  // namespace cruth
