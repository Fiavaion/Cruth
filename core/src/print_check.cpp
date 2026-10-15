// SPDX-License-Identifier: MIT
#include "cruth/print_check.hpp"

#include <algorithm>
#include <cmath>
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

PrintReport check_printability(const Mesh& mesh, const TriangleBVH* bvh) {
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

    if (bvh) {
        r.self_intersections = count_self_intersections(*bvh);
    } else {
        TriangleBVH local(mesh);
        r.self_intersections = count_self_intersections(local);
    }
    return r;
}

std::size_t split_non_manifold_vertices(Mesh& mesh) {
    const auto edges = edge_uses(mesh);
    auto manifold = [&](Index a, Index b) {
        auto it = edges.find(undirected_key(a, b));
        return it != edges.end() && it->second.faces == 2;
    };

    // Vertex -> incident faces.
    std::vector<Index> offsets(mesh.vertex_count() + 1, 0);
    for (Index v : mesh.face_indices()) ++offsets[v + 1];
    for (std::size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
    std::vector<Index> incident(mesh.corner_count());
    {
        std::vector<Index> fill(offsets.begin(), offsets.end() - 1);
        for (Index f = 0; f < mesh.face_count(); ++f) {
            for (Index v : mesh.face(f)) incident[fill[v]++] = f;
        }
    }

    // Around a non-manifold edge, sort its faces by angle and pair neighbours
    // that traverse it in opposite directions: each pair is one sheet.
    std::unordered_map<std::uint64_t, std::vector<Index>> crowded;
    for (Index f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            Index a = fv[i], b = fv[(i + 1) % fv.size()];
            auto it = edges.find(undirected_key(a, b));
            if (it != edges.end() && it->second.faces > 2) crowded[undirected_key(a, b)].push_back(f);
        }
    }
    std::unordered_map<std::uint64_t, std::vector<std::pair<Index, Index>>> sheet_pairs;
    for (auto& [key, list] : crowded) {
        const auto a = Index(key >> 32), b = Index(key & 0xFFFFFFFFu);
        if (list.size() % 2) continue;
        const Vec3 pa = mesh.positions[a], axis = normalize(mesh.positions[b] - pa);
        Vec3 ref = normalize(cross(axis, std::abs(axis.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
        const Vec3 ref2 = cross(axis, ref);
        auto forward = [&](Index f) {  // traverses a -> b
            auto fv = mesh.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) {
                if (fv[i] == a && fv[(i + 1) % fv.size()] == b) return true;
            }
            return false;
        };
        std::vector<std::pair<float, Index>> around;
        for (Index f : list) {
            Vec3 d = mesh.face_centroid(f) - pa;
            d -= axis * dot(d, axis);
            around.push_back({std::atan2(dot(d, ref2), dot(d, ref)), f});
        }
        std::sort(around.begin(), around.end());
        const std::size_t n = around.size();
        bool paired = false;
        for (std::size_t offset = 0; offset < 2 && !paired; ++offset) {
            bool ok = true;
            for (std::size_t i = 0; i < n && ok; i += 2) {
                ok = forward(around[(i + offset) % n].second) != forward(around[(i + offset + 1) % n].second);
            }
            if (!ok) continue;
            auto& pairs = sheet_pairs[key];
            for (std::size_t i = 0; i < n; i += 2) {
                pairs.push_back({around[(i + offset) % n].second, around[(i + offset + 1) % n].second});
            }
            paired = true;
        }
        if (!paired) {
            // Nearly coplanar faces can defeat the angular order; any pairing of
            // forward with backward faces is still manifold.
            std::vector<Index> fwd, bwd;
            for (const auto& [angle, f] : around) (forward(f) ? fwd : bwd).push_back(f);
            if (fwd.size() == bwd.size()) {
                auto& pairs = sheet_pairs[key];
                for (std::size_t i = 0; i < fwd.size(); ++i) pairs.push_back({fwd[i], bwd[i]});
            }
        }
    }

    auto faces = face_lists(mesh);
    std::size_t added = 0;
    std::unordered_map<Index, Index> first_face_with;
    std::unordered_map<Index, Index> slot_of_face;
    for (Index v = 0; v < offsets.size() - 1; ++v) {
        const Index begin = offsets[v], end = offsets[v + 1];
        if (end - begin < 2) continue;
        // Faces around v are in one fan when they share a manifold edge at v.
        UnionFind fan(end - begin);
        first_face_with.clear();
        slot_of_face.clear();
        for (Index c = begin; c < end; ++c) slot_of_face[incident[c]] = c - begin;
        for (Index c = begin; c < end; ++c) {
            const auto fv = mesh.face(incident[c]);
            for (std::size_t i = 0; i < fv.size(); ++i) {
                if (fv[i] != v) continue;
                for (Index nb : {fv[(i + fv.size() - 1) % fv.size()], fv[(i + 1) % fv.size()]}) {
                    if (manifold(v, nb)) {
                        auto [it, inserted] = first_face_with.emplace(nb, c - begin);
                        if (!inserted) fan.unite(c - begin, it->second);
                    } else if (auto sp = sheet_pairs.find(undirected_key(v, nb)); sp != sheet_pairs.end()) {
                        for (auto [f, g] : sp->second) fan.unite(slot_of_face[f], slot_of_face[g]);
                    }
                }
            }
        }
        std::unordered_map<Index, Index> copy_of_root;
        for (Index c = begin; c < end; ++c) {
            const Index root = fan.find(c - begin);
            if (root == fan.find(0)) continue;  // the first fan keeps the original
            auto [it, inserted] = copy_of_root.emplace(root, kInvalidIndex);
            if (inserted) {
                it->second = mesh.add_vertex(mesh.positions[v]);
                ++added;
            }
            for (Index& u : faces[incident[c]]) {
                if (u == v) u = it->second;
            }
        }
    }
    if (added) mesh = rebuild(mesh, faces);
    return added;
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

    if (opt.split_non_manifold) {
        log.split_vertices = split_non_manifold_vertices(mesh);
        faces = face_lists(mesh);
    }

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
