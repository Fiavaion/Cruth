// SPDX-License-Identifier: MIT
#include "cruth/print_check.hpp"

#include <algorithm>
#include <deque>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "cruth/mesh_io.hpp"

namespace cruth {

namespace {

std::uint64_t undirected_key(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | b;
}

struct UnionFind {
    explicit UnionFind(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), Index(0)); }
    Index find(Index x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(Index a, Index b) { parent[find(a)] = find(b); }
    std::vector<Index> parent;
};

struct EdgeUse {
    Index faces = 0;
    Index forward = 0;  // uses as (min -> max)
    Index f0 = kInvalidIndex, f1 = kInvalidIndex;
};

std::unordered_map<std::uint64_t, EdgeUse> edge_uses(const Mesh& mesh) {
    std::unordered_map<std::uint64_t, EdgeUse> edges;
    edges.reserve(mesh.corner_count());
    for (Index f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            Index a = fv[i], b = fv[(i + 1) % fv.size()];
            if (a == b) continue;
            EdgeUse& e = edges[undirected_key(a, b)];
            if (e.faces == 0) e.f0 = f;
            else if (e.faces == 1) e.f1 = f;
            ++e.faces;
            if (a < b) ++e.forward;
        }
    }
    return edges;
}

bool is_degenerate(const Mesh& mesh, Index f, float area_eps) {
    auto fv = mesh.face(f);
    for (std::size_t i = 0; i < fv.size(); ++i) {
        for (std::size_t j = i + 1; j < fv.size(); ++j) {
            if (fv[i] == fv[j]) return true;
        }
    }
    return mesh.face_area(f) <= area_eps;
}

float area_epsilon(const Mesh& mesh) {
    Vec3 s = mesh.bounds().size();
    float extent = std::max({s.x, s.y, s.z});
    return extent * extent * 1e-12f;
}

void flip_face(std::vector<Index>& corners) { std::reverse(corners.begin() + 1, corners.end()); }

Mesh rebuild(const Mesh& src, const std::vector<std::vector<Index>>& faces) {
    Mesh out;
    out.positions = src.positions;
    for (const auto& f : faces) out.add_face(f);
    return out;
}

std::vector<std::vector<Index>> face_lists(const Mesh& mesh) {
    std::vector<std::vector<Index>> faces(mesh.face_count());
    for (Index f = 0; f < mesh.face_count(); ++f) faces[f].assign(mesh.face(f).begin(), mesh.face(f).end());
    return faces;
}

double signed_volume(const Mesh& mesh, const std::vector<Index>& corners) {
    double v = 0;
    for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
        Vec3 a = mesh.positions[corners[0]], b = mesh.positions[corners[i]], c = mesh.positions[corners[i + 1]];
        v += double(dot(a, cross(b, c))) / 6.0;
    }
    return v;
}

}  // namespace

PrintReport check_printability(const Mesh& mesh) {
    PrintReport r;
    r.vertices = mesh.vertex_count();
    r.faces = mesh.face_count();
    r.triangles = mesh.triangle_count();
    r.bounds = mesh.bounds();

    const float eps = area_epsilon(mesh);
    for (Index f = 0; f < mesh.face_count(); ++f) {
        if (is_degenerate(mesh, f, eps)) ++r.degenerate_faces;
        r.surface_area += mesh.face_area(f);
        auto fv = mesh.face(f);
        r.volume += signed_volume(mesh, {fv.begin(), fv.end()});
    }

    // Edges.
    UnionFind boundary_loops(mesh.vertex_count());
    std::vector<bool> on_boundary(mesh.vertex_count(), false);
    for (const auto& [key, e] : edge_uses(mesh)) {
        auto a = Index(key >> 32), b = Index(key & 0xFFFFFFFFu);
        if (e.faces == 1) {
            ++r.boundary_edges;
            boundary_loops.unite(a, b);
            on_boundary[a] = on_boundary[b] = true;
        } else if (e.faces > 2) {
            ++r.non_manifold_edges;
        } else if (e.forward != 1) {
            ++r.inconsistent_edges;
        }
    }
    for (Index v = 0; v < mesh.vertex_count(); ++v) {
        if (on_boundary[v] && boundary_loops.find(v) == v) ++r.holes;
    }

    // Shells and unreferenced vertices.
    UnionFind shells(mesh.vertex_count());
    std::vector<bool> referenced(mesh.vertex_count(), false);
    for (Index f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            referenced[fv[i]] = true;
            shells.unite(fv[i], fv[(i + 1) % fv.size()]);
        }
    }
    for (Index v = 0; v < mesh.vertex_count(); ++v) {
        if (!referenced[v]) ++r.unreferenced_vertices;
        else if (shells.find(v) == v) ++r.shells;
    }

    // Non-manifold vertices: the faces around a vertex must form one fan.
    // Corners at the vertex are joined when they share an edge (same neighbour).
    std::vector<Index> corner_offsets(mesh.vertex_count() + 1, 0);
    for (Index v : mesh.face_indices()) ++corner_offsets[v + 1];
    for (std::size_t i = 1; i < corner_offsets.size(); ++i) corner_offsets[i] += corner_offsets[i - 1];
    struct Corner {
        Index prev, next;
    };
    std::vector<Corner> corners(mesh.corner_count());
    {
        std::vector<Index> fill(corner_offsets.begin(), corner_offsets.end() - 1);
        for (Index f = 0; f < mesh.face_count(); ++f) {
            auto fv = mesh.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) {
                corners[fill[fv[i]]++] = {fv[(i + fv.size() - 1) % fv.size()], fv[(i + 1) % fv.size()]};
            }
        }
    }
    std::unordered_map<Index, Index> first_with_neighbor;
    for (Index v = 0; v < mesh.vertex_count(); ++v) {
        const Index begin = corner_offsets[v], end = corner_offsets[v + 1];
        if (end - begin < 2) continue;
        UnionFind fan(end - begin);
        first_with_neighbor.clear();
        for (Index c = begin; c < end; ++c) {
            for (Index nb : {corners[c].prev, corners[c].next}) {
                auto [it, inserted] = first_with_neighbor.emplace(nb, c - begin);
                if (!inserted) fan.unite(c - begin, it->second);
            }
        }
        Index components = 0;
        for (Index c = 0; c < end - begin; ++c) components += fan.find(c) == c;
        if (components > 1) ++r.non_manifold_vertices;
    }
    return r;
}

RepairLog repair_for_print(Mesh& mesh, const RepairOptions& opt) {
    RepairLog log;
    if (opt.weld) log.welded_vertices = weld_vertices(mesh, opt.weld_tolerance);

    auto faces = face_lists(mesh);

    if (opt.remove_degenerate) {
        const float eps = area_epsilon(mesh);
        std::vector<std::vector<Index>> kept;
        kept.reserve(faces.size());
        for (Index f = 0; f < faces.size(); ++f) {
            auto& c = faces[f];
            // Collapse consecutive repeats (e.g. a quad welded into a triangle).
            c.erase(std::unique(c.begin(), c.end()), c.end());
            while (c.size() > 1 && c.front() == c.back()) c.pop_back();
            bool ok = c.size() >= 3;
            if (ok) {
                std::vector<Index> sorted = c;
                std::sort(sorted.begin(), sorted.end());
                ok = std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
            }
            if (ok) {
                Mesh probe;
                for (Index v : c) probe.add_vertex(mesh.positions[v]);
                std::vector<Index> local(c.size());
                std::iota(local.begin(), local.end(), Index(0));
                probe.add_face(local);
                ok = probe.face_area(0) > eps;
            }
            if (ok) kept.push_back(std::move(c));
            else ++log.removed_degenerate_faces;
        }
        faces = std::move(kept);
    }

    if (opt.remove_duplicate_faces) {
        std::unordered_set<std::string> seen;
        std::vector<std::vector<Index>> kept;
        kept.reserve(faces.size());
        for (auto& c : faces) {
            std::vector<Index> sorted = c;
            std::sort(sorted.begin(), sorted.end());
            std::string key(reinterpret_cast<const char*>(sorted.data()), sorted.size() * sizeof(Index));
            if (seen.insert(std::move(key)).second) kept.push_back(std::move(c));
            else ++log.removed_duplicate_faces;
        }
        faces = std::move(kept);
    }

    mesh = rebuild(mesh, faces);

    if (opt.unify_orientation) {
        // Breadth-first over manifold edges; flip a neighbour that traverses the
        // shared edge in the same direction as the current face.
        auto edges = edge_uses(mesh);
        std::vector<char> visited(faces.size(), 0);
        auto uses_directed = [&](const std::vector<Index>& c, Index a, Index b) {
            for (std::size_t i = 0; i < c.size(); ++i) {
                if (c[i] == a && c[(i + 1) % c.size()] == b) return true;
            }
            return false;
        };
        for (Index seed = 0; seed < faces.size(); ++seed) {
            if (visited[seed]) continue;
            visited[seed] = 1;
            std::deque<Index> queue{seed};
            while (!queue.empty()) {
                Index f = queue.front();
                queue.pop_front();
                const auto& c = faces[f];
                for (std::size_t i = 0; i < c.size(); ++i) {
                    Index a = c[i], b = c[(i + 1) % c.size()];
                    const EdgeUse& e = edges[undirected_key(a, b)];
                    if (e.faces != 2) continue;
                    Index g = e.f0 == f ? e.f1 : e.f0;
                    if (visited[g]) continue;
                    visited[g] = 1;
                    if (uses_directed(faces[g], a, b)) {
                        flip_face(faces[g]);
                        ++log.flipped_faces;
                    }
                    queue.push_back(g);
                }
            }
        }
        mesh = rebuild(mesh, faces);
    }

    if (opt.fill_holes) {
        // A hole is walked against the winding of the faces around it, which is
        // exactly the winding a filling face needs.
        std::unordered_map<Index, Index> next_on_hole;
        std::unordered_set<Index> ambiguous;
        auto edges = edge_uses(mesh);
        for (const auto& c : faces) {
            for (std::size_t i = 0; i < c.size(); ++i) {
                Index a = c[i], b = c[(i + 1) % c.size()];
                if (edges[undirected_key(a, b)].faces != 1) continue;
                if (!next_on_hole.emplace(b, a).second) ambiguous.insert(b);
            }
        }
        std::unordered_set<Index> used;
        for (const auto& [start, unused] : next_on_hole) {
            (void)unused;
            if (used.count(start) || ambiguous.count(start)) continue;
            std::vector<Index> loop;
            Index v = start;
            bool closed = false;
            while (true) {
                if (ambiguous.count(v) || used.count(v)) break;
                loop.push_back(v);
                used.insert(v);
                auto it = next_on_hole.find(v);
                if (it == next_on_hole.end()) break;
                v = it->second;
                if (v == start) {
                    closed = true;
                    break;
                }
            }
            if (!closed || loop.size() < 3) continue;
            if (loop.size() == 3) {
                faces.push_back(loop);
            } else {
                Vec3 c;
                for (Index u : loop) c += mesh.positions[u];
                Index center = mesh.add_vertex(c / static_cast<float>(loop.size()));
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    faces.push_back({loop[i], loop[(i + 1) % loop.size()], center});
                }
            }
            ++log.filled_holes;
        }
        mesh = rebuild(mesh, faces);
    }

    if (opt.orient_outward) {
        UnionFind shells(mesh.vertex_count());
        for (const auto& c : faces) {
            for (std::size_t i = 1; i < c.size(); ++i) shells.unite(c[0], c[i]);
        }
        std::unordered_map<Index, double> volume;
        for (const auto& c : faces) volume[shells.find(c[0])] += signed_volume(mesh, c);
        std::unordered_set<Index> inverted;
        for (const auto& [root, vol] : volume) {
            if (vol < 0) inverted.insert(root);
        }
        if (!inverted.empty()) {
            for (auto& c : faces) {
                if (inverted.count(shells.find(c[0]))) flip_face(c);
            }
            log.inverted_shells = inverted.size();
            mesh = rebuild(mesh, faces);
        }
    }

    if (opt.remove_unreferenced) {
        std::vector<Index> remap(mesh.vertex_count(), kInvalidIndex);
        Mesh out;
        for (auto& c : faces) {
            for (auto& v : c) {
                if (remap[v] == kInvalidIndex) remap[v] = out.add_vertex(mesh.positions[v]);
                v = remap[v];
            }
            out.add_face(c);
        }
        log.removed_vertices = mesh.vertex_count() - out.vertex_count();
        mesh = std::move(out);
    }
    return log;
}

}  // namespace cruth
