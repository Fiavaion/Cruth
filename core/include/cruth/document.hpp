// Cruth core — the editing session for one object: mesh, sculptor and undo.
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

#include "cruth/mesh.hpp"
#include "cruth/print_check.hpp"
#include "cruth/raycast.hpp"
#include "cruth/result.hpp"
#include "cruth/sculpt.hpp"

namespace cruth {

class Document {
public:
    Document();
    explicit Document(Mesh mesh);

    static Result<std::unique_ptr<Document>> load(const std::string& path);
    Status save(const std::string& path) const;

    const Mesh& mesh() const { return mesh_; }
    const std::vector<Vec3>& normals() const { return sculptor_.normals(); }
    const std::vector<float>& mask() const { return sculptor_.mask(); }

    // Replace the whole mesh as one undoable step.
    void set_mesh(Mesh mesh, std::string label);

    // Strokes (PRD B-01..B-04).
    void begin_stroke(const BrushSettings& brush);
    void add_stroke_sample(const StrokeSample& sample);
    void end_stroke();
    bool in_stroke() const { return sculptor_.in_stroke(); }

    // Topology commands.
    Status subdivide(int levels = 1);
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
    struct MeshSwap {
        Mesh before, after;
    };
    struct Entry {
        std::string label;
        std::variant<SculptDelta, MeshSwap> change;
    };

    void push(Entry entry);
    void replace_mesh(Mesh mesh);

    Mesh mesh_;
    Sculptor sculptor_;
    std::vector<Entry> history_;
    std::size_t cursor_ = 0;
    std::uint64_t topology_revision_ = 0;
    std::string stroke_label_;
};

const char* brush_name(BrushType type);

}  // namespace cruth
