// SPDX-License-Identifier: MIT
#include "cruth/document.hpp"

#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/subdivide.hpp"

namespace cruth {

namespace {
constexpr std::size_t kMaxHistory = 256;
}

const char* brush_name(BrushType type) {
    switch (type) {
        case BrushType::Standard: return "Standard";
        case BrushType::Clay: return "Clay";
        case BrushType::Inflate: return "Inflate";
        case BrushType::Smooth: return "Smooth";
        case BrushType::Flatten: return "Flatten";
        case BrushType::Pinch: return "Pinch";
        case BrushType::Crease: return "Crease";
        case BrushType::Grab: return "Grab";
        case BrushType::Mask: return "Mask";
    }
    return "Brush";
}

Document::Document() : Document(make_quad_sphere(5, 1.0f)) {}

Document::Document(Mesh mesh) : mesh_(std::move(mesh)), sculptor_(mesh_) {}

Result<std::unique_ptr<Document>> Document::load(const std::string& path) {
    auto mesh = load_mesh(path);
    if (!mesh) return mesh.error();
    return std::make_unique<Document>(std::move(mesh).value());
}

Status Document::save(const std::string& path) const { return save_mesh(mesh_, path); }

void Document::push(Entry entry) {
    history_.resize(cursor_);  // drop the redo branch
    history_.push_back(std::move(entry));
    if (history_.size() > kMaxHistory) history_.erase(history_.begin());
    cursor_ = history_.size();
}

void Document::replace_mesh(Mesh mesh) {
    mesh_ = std::move(mesh);
    sculptor_.rebuild();
    sculptor_.clear_mask();  // vertex ids changed, so the old mask is meaningless
    ++topology_revision_;
}

void Document::set_mesh(Mesh mesh, std::string label) {
    if (in_stroke()) end_stroke();
    Mesh before = mesh_;
    replace_mesh(std::move(mesh));
    push({std::move(label), MeshSwap{std::move(before), mesh_}});
}

void Document::begin_stroke(const BrushSettings& brush) {
    if (in_stroke()) end_stroke();
    stroke_label_ = brush_name(brush.type);
    sculptor_.begin_stroke(brush);
}

void Document::add_stroke_sample(const StrokeSample& sample) { sculptor_.add_sample(sample); }

void Document::end_stroke() {
    if (!in_stroke()) return;
    SculptDelta delta = sculptor_.end_stroke();
    if (!delta.empty()) push({stroke_label_, std::move(delta)});
}

Status Document::subdivide(int levels) {
    auto result = subdivide_catmull_clark(mesh_, levels);
    if (!result) return result.error();
    set_mesh(std::move(result).value(), "Subdivide");
    return Status::ok();
}

RepairLog Document::repair_for_print(const RepairOptions& options) {
    Mesh repaired = mesh_;
    RepairLog log = cruth::repair_for_print(repaired, options);
    set_mesh(std::move(repaired), "Repair for print");
    return log;
}

PrintReport Document::check_printability() const { return cruth::check_printability(mesh_); }

void Document::clear_mask() { sculptor_.clear_mask(); }
void Document::invert_mask() { sculptor_.invert_mask(); }

std::optional<RayHit> Document::raycast(Vec3 origin, Vec3 direction) const {
    return cruth::raycast(mesh_, origin, direction);
}

bool Document::undo() {
    if (in_stroke()) end_stroke();
    if (!can_undo()) return false;
    Entry& e = history_[--cursor_];
    if (auto* d = std::get_if<SculptDelta>(&e.change)) {
        sculptor_.apply(*d, false);
    } else {
        replace_mesh(std::get<MeshSwap>(e.change).before);
    }
    return true;
}

bool Document::redo() {
    if (in_stroke()) end_stroke();
    if (!can_redo()) return false;
    Entry& e = history_[cursor_++];
    if (auto* d = std::get_if<SculptDelta>(&e.change)) {
        sculptor_.apply(*d, true);
    } else {
        replace_mesh(std::get<MeshSwap>(e.change).after);
    }
    return true;
}

std::string Document::undo_label() const { return can_undo() ? history_[cursor_ - 1].label : std::string(); }
std::string Document::redo_label() const { return can_redo() ? history_[cursor_].label : std::string(); }

}  // namespace cruth
