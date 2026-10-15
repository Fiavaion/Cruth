// SPDX-License-Identifier: MIT
#include "cruth/voxel_remesh.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

#include "cruth/bvh.hpp"
#include "cruth/decimate.hpp"
#include "cruth/parallel.hpp"
#include "cruth/print_check.hpp"

namespace cruth {

namespace {

// Bit set over a 3D grid stored row by row: row r = j + ny * k holds the x
// bits of that row in `words_per_row` 64-bit words, so a whole row can be
// processed with word operations.
class BitRows {
public:
    BitRows() = default;
    BitRows(std::size_t rows, std::size_t words_per_row) : w_(words_per_row), bits_(rows * words_per_row, 0) {}

    std::uint64_t* row(std::size_t r) { return bits_.data() + r * w_; }
    const std::uint64_t* row(std::size_t r) const { return bits_.data() + r * w_; }
    bool get(int i, std::size_t r) const { return (bits_[r * w_ + (std::size_t(i) >> 6)] >> (i & 63)) & 1u; }
    void set(int i, std::size_t r) { bits_[r * w_ + (std::size_t(i) >> 6)] |= std::uint64_t(1) << (i & 63); }
    bool test_and_set(int i, std::size_t r) {
        std::uint64_t& word = bits_[r * w_ + (std::size_t(i) >> 6)];
        const std::uint64_t bit = std::uint64_t(1) << (i & 63);
        const bool was = word & bit;
        word |= bit;
        return was;
    }

private:
    std::size_t w_ = 0;
    std::vector<std::uint64_t> bits_;
};

// Open-addressing hash map from grid index to float with linear probing;
// several times faster than std::unordered_map for the narrow band.
class FlatMap {
public:
    explicit FlatMap(std::size_t expected = 1024) {
        std::size_t cap = 64;
        while (cap < expected * 2) cap <<= 1;
        keys_.assign(cap, kEmpty);
        values_.resize(cap);
    }
    std::size_t size() const { return size_; }

    void insert_min(std::size_t key, float value) {
        if ((size_ + 1) * 2 > keys_.size()) grow();
        std::size_t s = slot(key);
        if (keys_[s] == kEmpty) {
            keys_[s] = key;
            values_[s] = value;
            ++size_;
        } else {
            values_[s] = std::min(values_[s], value);
        }
    }
    const float* find(std::size_t key) const {
        const std::size_t s = slot(key);
        return keys_[s] == kEmpty ? nullptr : &values_[s];
    }
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t s = 0; s < keys_.size(); ++s) {
            if (keys_[s] != kEmpty) fn(keys_[s], values_[s]);
        }
    }

private:
    static constexpr std::size_t kEmpty = ~std::size_t(0);
    std::size_t slot(std::size_t key) const {
        const std::size_t mask = keys_.size() - 1;
        std::size_t s = (key * 0x9E3779B97F4A7C15ull) >> 17 & mask;
        while (keys_[s] != kEmpty && keys_[s] != key) s = (s + 1) & mask;
        return s;
    }
    void grow() {
        std::vector<std::size_t> keys;
        std::vector<float> values;
        keys.swap(keys_);
        values.swap(values_);
        keys_.assign(keys.size() * 2, kEmpty);
        values_.resize(keys.size() * 2);
        size_ = 0;
        for (std::size_t s = 0; s < keys.size(); ++s) {
            if (keys[s] != kEmpty) insert_min(keys[s], values[s]);
        }
    }

    std::vector<std::size_t> keys_;
    std::vector<float> values_;
    std::size_t size_ = 0;
};

// Spreads the set bits of `fill` through runs of set bits in `open` along one
// row, in both directions (Kogge–Stone occluded fill, carried across words).
// Returns true if anything was added.
bool fill_row(std::uint64_t* fill, const std::uint64_t* open, std::size_t words) {
    bool any = false;
    for (bool changed = true; changed;) {
        changed = false;
        std::uint64_t carry = 0;
        for (std::size_t w = 0; w < words; ++w) {  // toward higher x
            std::uint64_t g = fill[w] | (carry & open[w] & 1u), p = open[w];
            g |= p & (g << 1); p &= p << 1;
            g |= p & (g << 2); p &= p << 2;
            g |= p & (g << 4); p &= p << 4;
            g |= p & (g << 8); p &= p << 8;
            g |= p & (g << 16); p &= p << 16;
            g |= p & (g << 32);
            carry = g >> 63;
            if (g != fill[w]) {
                fill[w] = g;
                changed = any = true;
            }
        }
        carry = 0;
        for (std::size_t w = words; w-- > 0;) {  // toward lower x
            std::uint64_t g = fill[w] | ((carry << 63) & open[w]), p = open[w];
            g |= p & (g >> 1); p &= p >> 1;
            g |= p & (g >> 2); p &= p >> 2;
            g |= p & (g >> 4); p &= p >> 4;
            g |= p & (g >> 8); p &= p >> 8;
            g |= p & (g >> 16); p &= p >> 16;
            g |= p & (g >> 32);
            carry = g & 1u;
            if (g != fill[w]) {
                fill[w] = g;
                changed = any = true;
            }
        }
    }
    return any;
}

std::uint64_t edge_key(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | b;
}

// Angle-weighted vertex normals and summed edge normals (Bærentzen & Aanæs),
// which give a correct inside/outside sign for any closest-point feature of a
// closed, non-self-intersecting surface.
struct Pseudonormals {
    std::vector<Vec3> face, vertex;
    std::unordered_map<std::uint64_t, Vec3> edge;

    explicit Pseudonormals(const TriangleBVH& bvh) {
        const auto& p = bvh.positions();
        face.resize(bvh.triangle_count());
        vertex.assign(p.size(), Vec3{});
        edge.reserve(bvh.triangle_count() * 3);
        for (Index t = 0; t < bvh.triangle_count(); ++t) {
            const auto& tri = bvh.triangle(t);
            Vec3 n = normalize(cross(p[tri[1]] - p[tri[0]], p[tri[2]] - p[tri[0]]));
            face[t] = n;
            for (int i = 0; i < 3; ++i) {
                Vec3 e1 = normalize(p[tri[(i + 1) % 3]] - p[tri[i]]);
                Vec3 e2 = normalize(p[tri[(i + 2) % 3]] - p[tri[i]]);
                float angle = std::acos(std::clamp(dot(e1, e2), -1.0f, 1.0f));
                vertex[tri[i]] += n * angle;
                edge[edge_key(tri[i], tri[(i + 1) % 3])] += n;
            }
        }
    }

    Vec3 at(const TriangleBVH& bvh, const ClosestPoint& cp) const {
        const auto& tri = bvh.triangle(cp.triangle);
        const float w[3] = {cp.u, cp.v, cp.w};
        const int zeros = (w[0] == 0) + (w[1] == 0) + (w[2] == 0);
        if (zeros == 2) {
            for (int i = 0; i < 3; ++i) {
                if (w[i] != 0) return vertex[tri[i]];
            }
        }
        if (zeros == 1) {
            for (int i = 0; i < 3; ++i) {
                if (w[i] == 0) {
                    auto it = edge.find(edge_key(tri[(i + 1) % 3], tri[(i + 2) % 3]));
                    if (it != edge.end()) return it->second;
                }
            }
        }
        return face[cp.triangle];
    }
};

// Splits a mesh into its connected components (shared vertices).
std::vector<Mesh> split_shells(const Mesh& mesh) {
    std::vector<Index> parent(mesh.vertex_count());
    std::iota(parent.begin(), parent.end(), Index(0));
    auto find = [&](Index x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i < fv.size(); ++i) parent[find(fv[i])] = find(fv[0]);
    }
    std::unordered_map<Index, std::size_t> shell_of_root;
    std::vector<Mesh> shells;
    std::vector<std::unordered_map<Index, Index>> remap;
    std::vector<Index> corners;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        auto [it, inserted] = shell_of_root.emplace(find(fv[0]), shells.size());
        if (inserted) {
            shells.emplace_back();
            remap.emplace_back();
        }
        Mesh& s = shells[it->second];
        auto& r = remap[it->second];
        corners.clear();
        for (Index v : fv) {
            auto [rit, added] = r.emplace(v, static_cast<Index>(s.vertex_count()));
            if (added) s.add_vertex(mesh.positions[v]);
            corners.push_back(rit->second);
        }
        s.add_face(corners);
    }
    return shells;
}

struct Grid {
    Vec3 origin;
    float h = 0;
    int n[3] = {0, 0, 0};

    std::size_t total() const { return std::size_t(n[0]) * n[1] * n[2]; }
    std::size_t index(int i, int j, int k) const {
        return std::size_t(i) + std::size_t(n[0]) * (std::size_t(j) + std::size_t(n[1]) * k);
    }
    Vec3 point(int i, int j, int k) const { return origin + Vec3{float(i), float(j), float(k)} * h; }
    void coords(std::size_t id, int& i, int& j, int& k) const {
        i = int(id % std::size_t(n[0]));
        id /= std::size_t(n[0]);
        j = int(id % std::size_t(n[1]));
        k = int(id / std::size_t(n[1]));
    }
};

}  // namespace

Result<Mesh> voxel_remesh(const Mesh& input, const VoxelRemeshOptions& options, VoxelRemeshStats* stats) {
    if (input.face_count() == 0) return Error{ErrorCode::InvalidArgument, "voxel remesh: empty mesh"};
    const int resolution = std::clamp(options.resolution, 8, 512);

    const Mesh* source = &input;
    Mesh reduced;
    bool decimated = false;
    if (input.triangle_count() > options.max_input_triangles) {
        DecimateOptions d;
        d.target_triangles = options.max_input_triangles;
        reduced = decimate(input, d);
        source = &reduced;
        decimated = true;
    }

    const Bounds bounds = source->bounds();
    const Vec3 size = bounds.size();
    const float longest = std::max({size.x, size.y, size.z});
    if (!(longest > 0)) return Error{ErrorCode::InvalidArgument, "voxel remesh: mesh has no extent"};

    Grid g;
    g.h = longest / float(resolution);
    const int pad = 3;
    g.origin = bounds.lo - Vec3{g.h, g.h, g.h} * float(pad);
    for (int a = 0; a < 3; ++a) g.n[a] = int(std::ceil(size[a] / g.h)) + 2 * pad + 1;
    const float h = g.h;
    const float band_width = 1.5f * h;

    // Union of per-shell signed distance fields (CSG union = min):
    //  - `band`: min signed distance over shells whose surface is within band_width;
    //  - `deep`: points inside some shell and farther than band_width from its surface.
    // Signing each shell on its own keeps overlapping parts correct: one global
    // pseudonormal test is fooled by surfaces buried inside another part.
    const std::size_t words = (std::size_t(g.n[0]) + 63) / 64;
    FlatMap band(std::size_t(64) * 1024);
    BitRows deep(std::size_t(g.n[1]) * g.n[2], words);

    for (const Mesh& shell : split_shells(*source)) {
        const TriangleBVH bvh(shell);
        const Pseudonormals normals(bvh);

        // Sub-box around this shell, padded so its border is outside the shell.
        // Rows inside it keep the full grid width, so x needs no remapping.
        int lo[3], hi[3];
        const Bounds sb = shell.bounds();
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::max(0, int(std::floor((sb.lo[a] - g.origin[a]) / h)) - pad);
            hi[a] = std::min(g.n[a] - 1, int(std::ceil((sb.hi[a] - g.origin[a]) / h)) + pad);
        }
        const int nj = hi[1] - lo[1] + 1, nk = hi[2] - lo[2] + 1;
        const std::size_t rows = std::size_t(nj) * nk;
        auto row_of = [&](int j, int k) { return std::size_t(j - lo[1]) + std::size_t(nj) * (k - lo[2]); };

        // 1. Candidates: grid points in each triangle's band-expanded box that
        //    lie within band_width of its plane.
        BitRows candidate(rows, words);
        std::vector<std::size_t> candidates;  // global grid indices
        const auto& P = bvh.positions();
        for (Index t = 0; t < bvh.triangle_count(); ++t) {
            const auto& tri = bvh.triangle(t);
            const Vec3 a = P[tri[0]], b = P[tri[1]], c = P[tri[2]];
            const Vec3 n = normalize(cross(b - a, c - a));
            int tlo[3], thi[3];
            for (int ax = 0; ax < 3; ++ax) {
                const float mn = std::min({a[ax], b[ax], c[ax]}) - band_width;
                const float mx = std::max({a[ax], b[ax], c[ax]}) + band_width;
                tlo[ax] = std::max(lo[ax], int(std::ceil((mn - g.origin[ax]) / h)));
                thi[ax] = std::min(hi[ax], int(std::floor((mx - g.origin[ax]) / h)));
            }
            const bool degenerate = length_squared(n) == 0;
            for (int k = tlo[2]; k <= thi[2]; ++k)
                for (int j = tlo[1]; j <= thi[1]; ++j)
                    for (int i = tlo[0]; i <= thi[0]; ++i) {
                        if (!degenerate && std::abs(dot(g.point(i, j, k) - a, n)) >= band_width) continue;
                        if (!candidate.test_and_set(i, row_of(j, k))) candidates.push_back(g.index(i, j, k));
                    }
        }

        // 2. Narrow band with pseudonormal side (closest-point queries in parallel).
        std::vector<float> cand_dist(candidates.size(), -1.0f);
        std::vector<char> cand_inside(candidates.size(), 0);
        parallel_for(candidates.size(), [&](std::size_t begin, std::size_t end) {
            for (std::size_t c = begin; c < end; ++c) {
                int i, j, k;
                g.coords(candidates[c], i, j, k);
                const Vec3 p = g.point(i, j, k);
                auto cp = bvh.closest_point(p, band_width);
                if (!cp || cp->distance >= band_width) continue;
                cand_dist[c] = std::max(cp->distance, 1e-7f);
                cand_inside[c] = dot(p - cp->point, normals.at(bvh, *cp)) < 0;
            }
        });

        // 3. Flood the shell's exterior. Only inside-signed band points block it:
        //    along any grid axis crossing the surface the first inside point is
        //    within one voxel, so the wall has no gaps. Holes narrower than the
        //    wall stay sealed; whatever the flood cannot reach is inside.
        //    Runs a row at a time with word operations (64 voxels per op).
        BitRows open(rows, words), in_band(rows, words), exterior(rows, words);
        std::vector<std::uint64_t> xmask(words, 0);
        for (int i = lo[0]; i <= hi[0]; ++i) xmask[std::size_t(i) >> 6] |= std::uint64_t(1) << (i & 63);
        for (std::size_t r = 0; r < rows; ++r) std::copy(xmask.begin(), xmask.end(), open.row(r));
        for (std::size_t c = 0; c < candidates.size(); ++c) {
            if (cand_dist[c] < 0) continue;
            int i, j, k;
            g.coords(candidates[c], i, j, k);
            in_band.set(i, row_of(j, k));
            if (cand_inside[c]) open.row(row_of(j, k))[std::size_t(i) >> 6] &= ~(std::uint64_t(1) << (i & 63));
        }
        std::vector<std::size_t> queue;
        std::vector<char> queued(rows, 0);
        for (int k = lo[2]; k <= hi[2]; ++k) {
            for (int j = lo[1]; j <= hi[1]; ++j) {
                const std::size_t r = row_of(j, k);
                std::uint64_t* e = exterior.row(r);
                const std::uint64_t* o = open.row(r);
                if (j == lo[1] || j == hi[1] || k == lo[2] || k == hi[2]) {
                    std::copy(o, o + words, e);  // a face of the padded box: all outside
                } else {
                    for (int i : {lo[0], hi[0]}) {
                        if (open.get(i, r)) exterior.set(i, r);
                    }
                }
                queue.push_back(r);
                queued[r] = 1;
            }
        }
        while (!queue.empty()) {
            const std::size_t r = queue.back();
            queue.pop_back();
            queued[r] = 0;
            std::uint64_t* e = exterior.row(r);
            fill_row(e, open.row(r), words);
            const int j = lo[1] + int(r % std::size_t(nj)), k = lo[2] + int(r / std::size_t(nj));
            const int nbs[4][2] = {{j - 1, k}, {j + 1, k}, {j, k - 1}, {j, k + 1}};
            for (const auto& nb : nbs) {
                if (nb[0] < lo[1] || nb[0] > hi[1] || nb[1] < lo[2] || nb[1] > hi[2]) continue;
                const std::size_t nr = row_of(nb[0], nb[1]);
                std::uint64_t* ne = exterior.row(nr);
                const std::uint64_t* no = open.row(nr);
                bool grew = false;
                for (std::size_t w = 0; w < words; ++w) {
                    const std::uint64_t add = e[w] & no[w] & ~ne[w];
                    if (add) {
                        ne[w] |= add;
                        grew = true;
                    }
                }
                if (grew && !queued[nr]) {
                    queued[nr] = 1;
                    queue.push_back(nr);
                }
            }
        }

        // 4. Merge into the union field.
        for (std::size_t c = 0; c < candidates.size(); ++c) {
            if (cand_dist[c] < 0) continue;
            int i, j, k;
            g.coords(candidates[c], i, j, k);
            band.insert_min(candidates[c], exterior.get(i, row_of(j, k)) ? cand_dist[c] : -cand_dist[c]);
        }
        for (int k = lo[2]; k <= hi[2]; ++k) {
            for (int j = lo[1]; j <= hi[1]; ++j) {
                const std::size_t r = row_of(j, k);
                const std::uint64_t *e = exterior.row(r), *b = in_band.row(r);
                std::uint64_t* d = deep.row(std::size_t(j) + std::size_t(g.n[1]) * k);
                for (std::size_t w = 0; w < words; ++w) d[w] |= xmask[w] & ~e[w] & ~b[w];
            }
        }
    }

    auto value = [&](int i, int j, int k) -> float {
        const float bulk = deep.get(i, std::size_t(j) + std::size_t(g.n[1]) * k) ? -band_width : band_width;
        const float* d = band.find(g.index(i, j, k));
        return d ? std::min(*d, bulk) : bulk;
    };

    // 5. Surface nets: a vertex per cell that straddles the surface, a quad per
    //    sign-changing grid edge.
    Mesh out;
    std::unordered_map<std::size_t, Index> cell_vertex;
    cell_vertex.reserve(band.size() / 2);
    const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    const int cell_edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                   {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    auto vertex_for_cell = [&](int i, int j, int k) -> Index {
        const std::size_t id = g.index(i, j, k);
        auto it = cell_vertex.find(id);
        if (it != cell_vertex.end()) return it->second;
        float v[8];
        Vec3 c[8];
        for (int n = 0; n < 8; ++n) {
            v[n] = value(i + corner[n][0], j + corner[n][1], k + corner[n][2]);
            c[n] = g.point(i + corner[n][0], j + corner[n][1], k + corner[n][2]);
        }
        Vec3 sum;
        int count = 0;
        for (const auto& e : cell_edges) {
            const float a = v[e[0]], b = v[e[1]];
            if ((a < 0) == (b < 0)) continue;
            sum += lerp(c[e[0]], c[e[1]], a / (a - b));
            ++count;
        }
        const Index vi = out.add_vertex(count ? sum / float(count) : g.point(i, j, k) + Vec3{h, h, h} * 0.5f);
        cell_vertex.emplace(id, vi);
        return vi;
    };

    auto emit_edge = [&](int i, int j, int k, int axis) {
        int e[3] = {0, 0, 0};
        e[axis] = 1;
        if (i + e[0] >= g.n[0] || j + e[1] >= g.n[1] || k + e[2] >= g.n[2]) return;
        const float a = value(i, j, k), b = value(i + e[0], j + e[1], k + e[2]);
        if ((a < 0) == (b < 0)) return;
        const int u = (axis + 1) % 3, w = (axis + 2) % 3;
        const int base[3] = {i, j, k};
        if (base[u] == 0 || base[w] == 0 || base[u] >= g.n[u] - 1 || base[w] >= g.n[w] - 1) return;
        // The four cells around the edge, counter-clockwise about +axis.
        Index q[4];
        const int offs[4][2] = {{-1, -1}, {0, -1}, {0, 0}, {-1, 0}};
        for (int n = 0; n < 4; ++n) {
            int cidx[3] = {base[0], base[1], base[2]};
            cidx[u] += offs[n][0];
            cidx[w] += offs[n][1];
            q[n] = vertex_for_cell(cidx[0], cidx[1], cidx[2]);
        }
        if (a < 0) out.add_face({q[0], q[1], q[2], q[3]});  // inside -> outside along +axis
        else out.add_face({q[3], q[2], q[1], q[0]});
    };

    // Every sign change has a band point on at least one end (two bulk values
    // never differ in sign across one grid edge), so walking the band suffices.
    // Sorting the band makes the output vertex order deterministic.
    std::vector<std::size_t> band_ids;
    band_ids.reserve(band.size());
    band.for_each([&](std::size_t id, float) { band_ids.push_back(id); });
    std::sort(band_ids.begin(), band_ids.end());
    for (std::size_t id : band_ids) {
        int i, j, k;
        g.coords(id, i, j, k);
        for (int axis = 0; axis < 3; ++axis) {
            emit_edge(i, j, k, axis);
            int p[3] = {i, j, k};
            p[axis] -= 1;
            if (p[axis] >= 0 && !band.find(g.index(p[0], p[1], p[2]))) emit_edge(p[0], p[1], p[2], axis);
        }
    }

    // Where two sheets of the surface pass through one cell (thin gaps and
    // concave creases below voxel size), surface nets shares a vertex between
    // them; give each sheet its own copy so the result is manifold. The relax
    // pass below separates the copies.
    {
        std::vector<std::uint64_t> keys;
        keys.reserve(out.corner_count());
        for (std::size_t f = 0; f < out.face_count(); ++f) {
            auto fv = out.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) keys.push_back(edge_key(fv[i], fv[(i + 1) % fv.size()]));
        }
        std::sort(keys.begin(), keys.end());
        bool crowded = false;
        for (std::size_t i = 2; i < keys.size() && !crowded; ++i) crowded = keys[i] == keys[i - 2];
        if (crowded) split_non_manifold_vertices(out);
    }

    // 6. Relax, then project onto the source so forms stay crisp. Projection
    //    targets buried inside the union (surfaces hidden by an overlapping
    //    part) are rejected using the trilinear union field.
    auto field = [&](Vec3 p) {
        const Vec3 q = (p - g.origin) / h;
        const int i = std::clamp(int(std::floor(q.x)), 0, g.n[0] - 2);
        const int j = std::clamp(int(std::floor(q.y)), 0, g.n[1] - 2);
        const int k = std::clamp(int(std::floor(q.z)), 0, g.n[2] - 2);
        const float fx = std::clamp(q.x - float(i), 0.0f, 1.0f), fy = std::clamp(q.y - float(j), 0.0f, 1.0f),
                    fz = std::clamp(q.z - float(k), 0.0f, 1.0f);
        float r = 0;
        for (int n = 0; n < 8; ++n) {
            const float wx = corner[n][0] ? fx : 1 - fx, wy = corner[n][1] ? fy : 1 - fy, wz = corner[n][2] ? fz : 1 - fz;
            r += wx * wy * wz * value(i + corner[n][0], j + corner[n][1], k + corner[n][2]);
        }
        return r;
    };
    if (options.relax_iterations > 0 && out.vertex_count() > 0) {
        const TriangleBVH whole(*source);
        const VertexAdjacency adj = VertexAdjacency::build(out);
        std::vector<Vec3> next(out.vertex_count());
        for (int it = 0; it < options.relax_iterations; ++it) {
            parallel_for(out.vertex_count(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t v = begin; v < end; ++v) {
                    auto nb = adj.of(Index(v));
                    Vec3 avg;
                    for (Index n : nb) avg += out.positions[n];
                    next[v] = nb.empty() ? out.positions[v] : lerp(out.positions[v], avg / float(nb.size()), 0.5f);
                    if (options.project_to_source) {
                        auto cp = whole.closest_point(next[v], 2.0f * h);
                        if (cp && field(cp->point) > -0.35f * h) next[v] = cp->point;
                    }
                }
            });
            out.positions.swap(next);
        }
    }

    if (stats) {
        for (int a = 0; a < 3; ++a) stats->grid[a] = g.n[a];
        stats->voxel_size = h;
        stats->band_points = band.size();
        stats->input_decimated = decimated;
    }
    if (out.face_count() == 0) return Error{ErrorCode::InvalidArgument, "voxel remesh: no surface found at this resolution"};
    return out;
}

}  // namespace cruth
