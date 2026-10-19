// SPDX-License-Identifier: MIT
#include "cruth/document.hpp"

#include "cruth/bake.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/reproject.hpp"
#include "cruth/uv.hpp"

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

Document::Document(Mesh mesh) : multires_(mesh), mesh_(std::move(mesh)), sculptor_(mesh_) {}

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

// Folds strokes made at the current level into the multires stack.
void Document::commit_level() {
    if (in_stroke()) end_stroke();
    const auto& stored = multires_.level(level_).positions;
    if (stored != mesh_.positions) {
        multires_.set_positions(level_, mesh_.positions);
        cached_snapshot_.reset();
    }
}

Document::Snapshot Document::snapshot() {
    commit_level();
    if (!cached_snapshot_) cached_snapshot_ = std::make_shared<const State>(State{multires_, level_});
    return cached_snapshot_;
}

void Document::restore(const Snapshot& state) {
    multires_ = state->stack;
    level_ = state->level;
    cached_snapshot_ = state;
    mesh_ = multires_.level(level_);
    sculptor_.rebuild();
    sculptor_.clear_mask();  // vertex ids changed, so the old mask is meaningless
    ++topology_revision_;
}

void Document::apply_state(State state, std::string label) {
    Snapshot before = snapshot();
    Snapshot after = std::make_shared<const State>(std::move(state));
    restore(after);
    push({std::move(label), StateSwap{std::move(before), std::move(after)}});
}

void Document::set_mesh(Mesh mesh, std::string label) {
    State next{MultiresStack(std::move(mesh)), 0};
    next.stack.set_base_locked(multires_.base_locked());
    apply_state(std::move(next), std::move(label));
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
    if (levels < 1) return Error{ErrorCode::InvalidArgument, "levels must be >= 1"};
    commit_level();
    State next{multires_, 0};
    for (int i = 0; i < levels; ++i) {
        auto status = next.stack.add_level();
        if (!status) return status;
    }
    next.level = next.stack.top();
    apply_state(std::move(next), "Subdivide");
    return Status::ok();
}

Status Document::set_level(int level) {
    if (level < 0 || level >= level_count()) return Error{ErrorCode::InvalidArgument, "no such multires level"};
    if (level == level_) return Status::ok();
    commit_level();
    State next{multires_, level};
    apply_state(std::move(next), "Level " + std::to_string(level));
    return Status::ok();
}

Result<VoxelRemeshStats> Document::voxel_remesh(const VoxelRemeshOptions& options) {
    commit_level();
    VoxelRemeshStats stats;
    auto result = cruth::voxel_remesh(mesh_, options, &stats);
    if (!result) return result.error();
    set_mesh(std::move(result).value(), "Voxel remesh");
    return stats;
}

Status Document::replace_keeping_detail(Mesh low, int detail_levels, std::string label) {
    if (detail_levels < 0 || detail_levels >= MultiresStack::kMaxLevels) {
        return Error{ErrorCode::InvalidArgument, "detail levels must be 0.." + std::to_string(MultiresStack::kMaxLevels - 1)};
    }
    commit_level();
    const Mesh& sculpt = multires_.level(multires_.top());
    State next{MultiresStack(std::move(low)), 0};
    next.stack.set_base_locked(multires_.base_locked());
    if (detail_levels > 0) {
        for (int i = 0; i < detail_levels; ++i) {
            auto added = next.stack.add_level();
            if (!added) return added;
        }
        // Project the finest level onto the old sculpt. The base stays as
        // given; the levels in between follow by restriction.
        Mesh top = next.stack.level(next.stack.top());
        const TriangleBVH target(sculpt);
        project_onto(top, target, 0.05f * length(sculpt.bounds().size()));
        const bool locked = next.stack.base_locked();
        next.stack.set_base_locked(true);
        next.stack.set_positions(next.stack.top(), top.positions);
        next.stack.set_base_locked(locked);
        next.level = next.stack.top();
    }
    apply_state(std::move(next), std::move(label));
    return Status::ok();
}

Result<QuadRemeshStats> Document::quad_remesh(const QuadRemeshOptions& options, int detail_levels) {
    commit_level();
    QuadRemeshStats stats;
    // Remesh the most detailed level so fine forms guide the field.
    auto result = cruth::quad_remesh(multires_.level(multires_.top()), options, &stats);
    if (!result) return result.error();
    auto s = replace_keeping_detail(std::move(result).value(), detail_levels, "Quad remesh");
    if (!s) return s.error();
    return stats;
}

const Mesh& Document::sculpt_reference() {
    commit_level();
    return multires_.level(multires_.top());
}

Status Document::adopt_retopology(Mesh low, int detail_levels, std::string label) {
    if (low.face_count() == 0 || !low.is_valid()) return Error{ErrorCode::InvalidArgument, "retopology mesh is empty or invalid"};
    return replace_keeping_detail(std::move(low), detail_levels, std::move(label));
}

DecimateStats Document::decimate(std::size_t target_triangles) {
    commit_level();
    DecimateOptions options;
    options.target_triangles = target_triangles;
    DecimateStats stats;
    Mesh reduced = cruth::decimate(mesh_, options, &stats);
    set_mesh(std::move(reduced), "Decimate");
    return stats;
}

RepairLog Document::repair_for_print(const RepairOptions& options) {
    commit_level();
    Mesh repaired = mesh_;
    RepairLog log = cruth::repair_for_print(repaired, options);
    set_mesh(std::move(repaired), "Repair for print");
    return log;
}

Result<GameAssetReport> Document::export_game_asset(const std::string& directory, const GameAssetOptions& options) {
    commit_level();
    const Mesh& high = multires_.level(multires_.top());
    DecimateOptions d;
    d.target_triangles = std::max<std::size_t>(options.target_triangles, 16);
    const Mesh low = high.triangle_count() > d.target_triangles ? cruth::decimate(high, d) : high;

    UnwrapOptions u;
    u.resolution = options.texture_size;
    UnwrapStats ustats;
    auto uv = auto_unwrap(low, u, &ustats);
    if (!uv) return uv.error();

    std::vector<BakeMap> maps{BakeMap::TangentNormal, BakeMap::Height};
    if (options.ambient_occlusion) maps.push_back(BakeMap::AmbientOcclusion);
    BakeOptions b;
    b.resolution = options.texture_size;
    b.ao_samples = options.ao_samples;
    BakeStats bstats;
    auto baked = bake(low, uv.value(), high, maps, b, &bstats);
    if (!baked) return baked.error();

    GameAssetReport report;
    report.triangles = low.triangle_count();
    report.charts = ustats.charts;
    report.texels = bstats.texels;
    report.misses = bstats.misses;
    const std::string base = directory + "/" + options.name;
    // Triangles and vn, to match what the normal map was baked against.
    auto s = write_file_bytes(base + ".obj", write_baked_asset_obj(low, uv.value()));
    if (!s) return s.error();
    report.files.push_back(base + ".obj");
    for (const auto& m : baked.value()) {
        const bool height = m.map == BakeMap::Height;
        const std::string path = base + "_" + bake_map_name(m.map) + (height ? ".exr" : ".png");
        const ImageFormat fmt =
            height ? ImageFormat::ExrFloat : (m.map == BakeMap::AmbientOcclusion ? ImageFormat::Png8 : ImageFormat::Png16);
        auto w = write_image(m.image, path, fmt);
        if (!w) return w.error();
        report.files.push_back(path);
    }
    return report;
}

PrintReport Document::check_printability() const { return cruth::check_printability(mesh_, &bvh()); }

void Document::clear_mask() { sculptor_.clear_mask(); }
void Document::invert_mask() { sculptor_.invert_mask(); }

const TriangleBVH& Document::bvh() const {
    if (bvh_topology_ != topology_revision_) {
        bvh_.build(mesh_);
        bvh_topology_ = topology_revision_;
    } else if (bvh_geometry_ != geometry_revision()) {
        // Strokes and their undo move a few vertices each: refit only around
        // them. If the journal lost track (rebuild, overflow), refit it all.
        if (auto moved = sculptor_.moved_since(bvh_journal_)) {
            bvh_.refit(mesh_.positions, *moved);
        } else {
            bvh_.refit(mesh_.positions);
        }
    }
    bvh_geometry_ = geometry_revision();
    bvh_journal_ = sculptor_.journal_end();
    return bvh_;
}

std::optional<RayHit> Document::raycast(Vec3 origin, Vec3 direction) const {
    return bvh().raycast(origin, direction);
}

bool Document::undo() {
    if (in_stroke()) end_stroke();
    if (!can_undo()) return false;
    Entry& e = history_[--cursor_];
    if (auto* d = std::get_if<SculptDelta>(&e.change)) {
        sculptor_.apply(*d, false);
    } else {
        restore(std::get<StateSwap>(e.change).before);
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
        restore(std::get<StateSwap>(e.change).after);
    }
    return true;
}

std::string Document::undo_label() const { return can_undo() ? history_[cursor_ - 1].label : std::string(); }
std::string Document::redo_label() const { return can_redo() ? history_[cursor_].label : std::string(); }

}  // namespace cruth
