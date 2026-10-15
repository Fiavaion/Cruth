// SPDX-License-Identifier: MIT
#include "cruth/mesh.hpp"

#include <algorithm>

namespace cruth {

void Mesh::add_face(std::span<const Index> vertices) {
    face_indices_.insert(face_indices_.end(), vertices.begin(), vertices.end());
    face_offsets_.push_back(static_cast<Index>(face_indices_.size()));
}

void Mesh::clear_faces() {
    face_offsets_.assign(1, 0);
    face_indices_.clear();
}

void Mesh::clear() {
    positions.clear();
    clear_faces();
}

void Mesh::reserve(std::size_t vertices, std::size_t faces, std::size_t corners) {
    positions.reserve(vertices);
    face_offsets_.reserve(faces + 1);
    face_indices_.reserve(corners);
}

std::size_t Mesh::triangle_count() const {
    std::size_t n = 0;
    for (std::size_t f = 0; f < face_count(); ++f) n += face_size(f) >= 3 ? face_size(f) - 2 : 0;
    return n;
}

std::vector<Index> Mesh::triangulated_indices() const {
    std::vector<Index> out;
    out.reserve(triangle_count() * 3);
    for (std::size_t f = 0; f < face_count(); ++f) {
        auto fv = face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            out.push_back(fv[0]);
            out.push_back(fv[i]);
            out.push_back(fv[i + 1]);
        }
    }
    return out;
}

// Newell's method: robust for non-planar polygons; the length is twice the area.
static Vec3 newell(const Mesh& m, std::size_t f) {
    Vec3 n;
    auto fv = m.face(f);
    for (std::size_t i = 0; i < fv.size(); ++i) {
        Vec3 a = m.positions[fv[i]];
        Vec3 b = m.positions[fv[(i + 1) % fv.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

Vec3 Mesh::face_normal(std::size_t f) const { return normalize(newell(*this, f)); }
float Mesh::face_area(std::size_t f) const { return 0.5f * length(newell(*this, f)); }

Vec3 Mesh::face_centroid(std::size_t f) const {
    Vec3 c;
    auto fv = face(f);
    for (Index v : fv) c += positions[v];
    return fv.empty() ? c : c / static_cast<float>(fv.size());
}

std::vector<Vec3> Mesh::vertex_normals() const {
    std::vector<Vec3> normals(positions.size());
    for (std::size_t f = 0; f < face_count(); ++f) {
        Vec3 n = newell(*this, f);  // area-weighted
        for (Index v : face(f)) normals[v] += n;
    }
    for (auto& n : normals) n = normalize(n);
    return normals;
}

Bounds Mesh::bounds() const {
    Bounds b;
    for (const auto& p : positions) b.extend(p);
    return b;
}

bool Mesh::is_valid() const {
    for (std::size_t f = 0; f < face_count(); ++f) {
        if (face_size(f) < 3) return false;
    }
    return std::all_of(face_indices_.begin(), face_indices_.end(),
                       [&](Index v) { return v < positions.size(); });
}

VertexAdjacency VertexAdjacency::build(const Mesh& mesh) {
    // Counting sort: each corner contributes its two polygon neighbours to its
    // vertex's row; rows are then de-duplicated in place.
    const std::size_t n = mesh.vertex_count();
    std::vector<Index> start(n + 1, 0);
    for (Index v : mesh.face_indices()) start[v + 1] += 2;
    for (std::size_t i = 1; i <= n; ++i) start[i] += start[i - 1];
    std::vector<Index> raw(start[n]);
    std::vector<Index> fill(start.begin(), start.end() - 1);
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            const Index v = fv[i];
            raw[fill[v]++] = fv[(i + fv.size() - 1) % fv.size()];
            raw[fill[v]++] = fv[(i + 1) % fv.size()];
        }
    }

    VertexAdjacency adj;
    adj.offsets.assign(n + 1, 0);
    adj.neighbors.reserve(raw.size() / 2 + n);
    for (std::size_t v = 0; v < n; ++v) {
        auto first = raw.begin() + start[v], last = raw.begin() + start[v + 1];
        std::sort(first, last);
        last = std::unique(first, last);
        for (auto it = first; it != last; ++it) {
            if (*it != v) adj.neighbors.push_back(*it);
        }
        adj.offsets[v + 1] = static_cast<Index>(adj.neighbors.size());
    }
    return adj;
}

}  // namespace cruth
