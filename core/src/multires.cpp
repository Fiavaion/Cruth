// SPDX-License-Identifier: MIT
#include "cruth/multires.hpp"

#include <string>

#include "cruth/subdivide.hpp"

namespace cruth {

Status MultiresStack::add_level() {
    if (levels_.empty()) return Error{ErrorCode::InvalidArgument, "multires: no base level"};
    if (level_count() >= kMaxLevels) {
        return Error{ErrorCode::InvalidArgument, "multires: at most " + std::to_string(kMaxLevels) + " levels"};
    }
    auto next = subdivide_catmull_clark(levels_.back());
    if (!next) return next.error();
    levels_.push_back(std::move(next).value());
    return Status::ok();
}

void MultiresStack::truncate(int k) {
    if (k >= 0 && k < level_count()) levels_.resize(std::size_t(k) + 1);
}

void MultiresStack::set_positions(int k, const std::vector<Vec3>& positions) {
    if (k < 0 || k >= level_count() || positions.size() != levels_[k].vertex_count()) return;

    std::vector<Vec3> delta(positions.size());
    bool changed = false;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        delta[i] = positions[i] - levels_[k].positions[i];
        changed = changed || !(delta[i] == Vec3{});
    }
    if (!changed) return;
    levels_[k].positions = positions;

    // Up: subdivide the displacement with each coarser level's topology.
    std::vector<Vec3> d = delta;
    for (int j = k + 1; j < level_count(); ++j) {
        Mesh carrier = levels_[j - 1];
        carrier.positions = d;
        auto sub = subdivide_catmull_clark(carrier);
        if (!sub) break;  // cannot happen: the level was built by the same subdivision
        d = std::move(sub.value().positions);
        auto& p = levels_[j].positions;
        for (std::size_t i = 0; i < p.size() && i < d.size(); ++i) p[i] += d[i];
    }

    // Down: coarse vertices are the first vertices of the finer level.
    d = std::move(delta);
    const int lowest = base_locked_ ? 1 : 0;
    for (int j = k - 1; j >= lowest; --j) {
        auto& p = levels_[j].positions;
        d.resize(p.size());
        for (std::size_t i = 0; i < p.size(); ++i) p[i] += d[i];
    }
}

}  // namespace cruth
