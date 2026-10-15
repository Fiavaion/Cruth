// Cruth core — multiresolution stack (PRD S-01).
//
// Level k+1 is the Catmull-Clark subdivision of level k's topology, and each
// level stores its own positions. Editing a level propagates:
// - up, by subdividing the edit's displacement (Catmull-Clark is linear, so
//   finer detail rides along unchanged);
// - down, by restricting the displacement to the coarse level's vertices
//   (they keep their indices through subdivision), unless level 0 is locked.
//
// Detail is carried in object space for now; tangent-space detail, sparse
// on-disk levels and OpenSubdiv evaluation replace this behind the same API.
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "cruth/mesh.hpp"
#include "cruth/result.hpp"

namespace cruth {

class MultiresStack {
public:
    static constexpr int kMaxLevels = 8;

    MultiresStack() = default;
    explicit MultiresStack(Mesh base) { levels_.push_back(std::move(base)); }

    int level_count() const { return static_cast<int>(levels_.size()); }
    int top() const { return level_count() - 1; }
    const Mesh& level(int k) const { return levels_[k]; }

    bool base_locked() const { return base_locked_; }
    void set_base_locked(bool locked) { base_locked_ = locked; }

    // Appends a subdivided level on top. Fails on non-manifold topology or at kMaxLevels.
    Status add_level();
    // Drops every level above k.
    void truncate(int k);
    // Replaces level k's positions and propagates the change to the other levels.
    void set_positions(int k, const std::vector<Vec3>& positions);

private:
    std::vector<Mesh> levels_;
    bool base_locked_ = false;
};

}  // namespace cruth
