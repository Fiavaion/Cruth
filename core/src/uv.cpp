// SPDX-License-Identifier: MIT
#include "cruth/uv.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>
#include <unordered_map>

#include "cruth/parallel.hpp"

namespace cruth {

namespace {

std::uint64_t edge_key(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | b;
}

// ---------------------------------------------------------------- sparse solver

struct SparseMatrix {
    std::size_t n = 0;
    std::vector<std::size_t> row_start;
    std::vector<std::uint32_t> col;
    std::vector<double> val;

    void multiply(const std::vector<double>& x, std::vector<double>& y) const {
        y.assign(n, 0.0);
        for (std::size_t r = 0; r < n; ++r) {
            double s = 0;
            for (std::size_t k = row_start[r]; k < row_start[r + 1]; ++k) s += val[k] * x[col[k]];
            y[r] = s;
        }
    }
};

struct Triplet {
    std::uint32_t r, c;
    double v;
};

SparseMatrix assemble(std::size_t n, std::vector<Triplet>& t) {
    std::sort(t.begin(), t.end(), [](const Triplet& a, const Triplet& b) { return a.r != b.r ? a.r < b.r : a.c < b.c; });
    SparseMatrix m;
    m.n = n;
    m.row_start.assign(n + 1, 0);
    for (std::size_t i = 0; i < t.size();) {
        std::size_t j = i;
        double v = 0;
        while (j < t.size() && t[j].r == t[i].r && t[j].c == t[i].c) v += t[j++].v;
        m.col.push_back(t[i].c);
        m.val.push_back(v);
        ++m.row_start[t[i].r + 1];
        i = j;
    }
    for (std::size_t r = 1; r <= n; ++r) m.row_start[r] += m.row_start[r - 1];
    return m;
}

// Jacobi-preconditioned conjugate gradient for a symmetric positive definite system.
void solve_cg(const SparseMatrix& A, const std::vector<double>& b, std::vector<double>& x, int max_iterations,
              double tolerance) {
    const std::size_t n = A.n;
    std::vector<double> inv_diag(n, 1.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t k = A.row_start[r]; k < A.row_start[r + 1]; ++k) {
            if (A.col[k] == r && std::abs(A.val[k]) > 1e-300) inv_diag[r] = 1.0 / A.val[k];
        }
    }
    std::vector<double> r(n), z(n), p(n), Ap(n);
    A.multiply(x, Ap);
    double bnorm = 0;
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = b[i] - Ap[i];
        bnorm += b[i] * b[i];
    }
    bnorm = std::sqrt(std::max(bnorm, 1e-300));
    for (std::size_t i = 0; i < n; ++i) p[i] = z[i] = r[i] * inv_diag[i];
    double rz = 0;
    for (std::size_t i = 0; i < n; ++i) rz += r[i] * z[i];
    for (int it = 0; it < max_iterations; ++it) {
        A.multiply(p, Ap);
        double pAp = 0;
        for (std::size_t i = 0; i < n; ++i) pAp += p[i] * Ap[i];
        if (std::abs(pAp) < 1e-300) break;
        const double alpha = rz / pAp;
        double rnorm = 0;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
            rnorm += r[i] * r[i];
        }
        if (std::sqrt(rnorm) <= tolerance * bnorm) break;
        double rz_next = 0;
        for (std::size_t i = 0; i < n; ++i) {
            z[i] = r[i] * inv_diag[i];
            rz_next += r[i] * z[i];
        }
        const double beta = rz_next / rz;
        rz = rz_next;
        for (std::size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
}

// ---------------------------------------------------------------- charts

struct Chart {
    std::vector<Index> faces;
    std::vector<Index> vertices;  // mesh vertex ids, local order
    std::vector<Vec2> uv;         // per local vertex
    Vec3 normal;
    int tile = 0;
    Vec2 size;                    // bounding box after rotation (world units)
    Vec2 offset;                  // placement in the tile (world units)
};

std::vector<Chart> grow_charts(const Mesh& mesh, const UnwrapOptions& o) {
    const std::size_t F = mesh.face_count();
    std::vector<Vec3> normal(F);
    for (Index f = 0; f < F; ++f) normal[f] = mesh.face_normal(f);

    // Face adjacency across manifold edges.
    std::unordered_map<std::uint64_t, std::pair<Index, Index>> edge_faces;
    edge_faces.reserve(mesh.corner_count());
    for (Index f = 0; f < F; ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            auto [it, inserted] = edge_faces.emplace(edge_key(fv[i], fv[(i + 1) % fv.size()]), std::pair{f, kInvalidIndex});
            if (!inserted) it->second.second = it->second.second == kInvalidIndex ? f : kInvalidIndex - 1;
        }
    }
    auto neighbour = [&](Index f, std::size_t corner) -> Index {
        auto fv = mesh.face(f);
        const auto& [a, b] = edge_faces.at(edge_key(fv[corner], fv[(corner + 1) % fv.size()]));
        if (b >= kInvalidIndex - 1) return kInvalidIndex;  // boundary or non-manifold: a seam
        return a == f ? b : a;
    };
    auto group = [&](Index f) { return o.face_groups.size() == F ? o.face_groups[f] : 0; };

    const float cos_limit = std::cos(o.max_chart_angle_degrees * std::numbers::pi_v<float> / 180.0f);
    std::vector<Index> chart_of(F, kInvalidIndex);
    std::vector<Chart> charts;
    using Item = std::pair<float, Index>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> frontier;
    for (Index seed = 0; seed < F; ++seed) {
        if (chart_of[seed] != kInvalidIndex) continue;
        const auto c = static_cast<Index>(charts.size());
        charts.emplace_back();
        Chart& chart = charts.back();
        const Vec3 seed_n = normal[seed];
        Vec3 sum;
        frontier.push({0.0f, seed});
        while (!frontier.empty()) {
            const Index f = frontier.top().second;
            frontier.pop();
            if (chart_of[f] != kInvalidIndex) continue;
            if (f != seed && (group(f) != group(seed) || dot(normal[f], seed_n) < cos_limit)) continue;
            chart_of[f] = c;
            chart.faces.push_back(f);
            sum += normal[f] * mesh.face_area(f);
            const Vec3 avg = normalize(sum);
            for (std::size_t i = 0; i < mesh.face_size(f); ++i) {
                const Index g = neighbour(f, i);
                if (g != kInvalidIndex && chart_of[g] == kInvalidIndex) frontier.push({1.0f - dot(normal[g], avg), g});
            }
        }
        chart.normal = normalize(sum);
    }
    return charts;
}

// Least-squares conformal map of one chart (Lévy et al. 2002) in its
// symmetric form Q = [[L, -S/2], [S/2, L]]: L is the cotangent Laplacian and
// S the boundary orientation matrix, so x^T Q x / 2 = Dirichlet - area.
void parameterise(const Mesh& mesh, Chart& chart) {
    std::unordered_map<Index, Index> local;
    for (Index f : chart.faces) {
        for (Index v : mesh.face(f)) {
            if (local.emplace(v, static_cast<Index>(chart.vertices.size())).second) chart.vertices.push_back(v);
        }
    }
    const std::size_t n = chart.vertices.size();
    chart.uv.assign(n, Vec2{});

    // Planar projection: the initial guess and the fallback for tiny charts.
    const Vec3 nrm = length_squared(chart.normal) > 0 ? chart.normal : Vec3{0, 0, 1};
    const Vec3 t1 = normalize(cross(nrm, std::abs(nrm.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
    const Vec3 t2 = cross(nrm, t1);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p = mesh.positions[chart.vertices[i]];
        chart.uv[i] = {dot(p, t1), dot(p, t2)};
    }
    if (n < 4) return;

    std::vector<Triplet> trip;
    std::unordered_map<std::uint64_t, int> directed;  // +1 per a->b use
    auto add_l = [&](Index a, Index b, double w) {
        trip.push_back({a, b, -w});
        trip.push_back({b, a, -w});
        trip.push_back({a, a, w});
        trip.push_back({b, b, w});
        trip.push_back({Index(a + n), Index(b + n), -w});
        trip.push_back({Index(b + n), Index(a + n), -w});
        trip.push_back({Index(a + n), Index(a + n), w});
        trip.push_back({Index(b + n), Index(b + n), w});
    };
    for (Index f : chart.faces) {
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            const Index tri[3] = {local[fv[0]], local[fv[i]], local[fv[i + 1]]};
            const Vec3 p[3] = {mesh.positions[fv[0]], mesh.positions[fv[i]], mesh.positions[fv[i + 1]]};
            const float area2 = length(cross(p[1] - p[0], p[2] - p[0]));
            if (area2 < 1e-20f) continue;
            for (int k = 0; k < 3; ++k) {
                const Vec3 e1 = p[(k + 1) % 3] - p[k], e2 = p[(k + 2) % 3] - p[k];
                const double cot = std::clamp(double(dot(e1, e2)) / double(area2), -1e4, 1e4);
                // Dirichlet energy of a linear map is 1/4 sum cot * |du|^2, i.e.
                // x^T L x / 2 with edge weight cot / 2.
                add_l(tri[(k + 1) % 3], tri[(k + 2) % 3], 0.5 * cot);
            }
        }
        for (std::size_t i = 0; i < fv.size(); ++i) {
            const Index a = local[fv[i]], b = local[fv[(i + 1) % fv.size()]];
            ++directed[(std::uint64_t(a) << 32) | b];
        }
    }
    // Boundary edges are used once and never in reverse.
    std::vector<std::pair<Index, Index>> boundary;
    for (const auto& [key, count] : directed) {
        const auto a = Index(key >> 32), b = Index(key & 0xFFFFFFFFu);
        if (count == 1 && !directed.count((std::uint64_t(b) << 32) | a)) boundary.push_back({a, b});
    }
    if (boundary.empty()) return;  // closed chart: keep the planar projection
    for (const auto& [a, b] : boundary) {
        trip.push_back({a, Index(b + n), -0.5});
        trip.push_back({Index(b + n), a, -0.5});
        trip.push_back({b, Index(a + n), 0.5});
        trip.push_back({Index(a + n), b, 0.5});
    }

    // Pin the two boundary vertices farthest apart.
    auto farthest_from = [&](Index from) {
        Index best = from;
        float d = -1;
        for (const auto& e : boundary) {
            float dd = distance(mesh.positions[chart.vertices[e.first]], mesh.positions[chart.vertices[from]]);
            if (dd > d) {
                d = dd;
                best = e.first;
            }
        }
        return best;
    };
    const Index pin_b = farthest_from(boundary.front().first);
    const Index pin_a = farthest_from(pin_b);
    const double span = distance(mesh.positions[chart.vertices[pin_a]], mesh.positions[chart.vertices[pin_b]]);
    if (pin_a == pin_b || span <= 0) return;

    // Reduce to the free variables: Q_ff x_f = -Q_fp x_p.
    const std::size_t N = 2 * n;
    std::vector<double> fixed(N, 0.0);
    std::vector<char> is_fixed(N, 0);
    is_fixed[pin_a] = is_fixed[pin_a + n] = is_fixed[pin_b] = is_fixed[pin_b + n] = 1;
    fixed[pin_b] = span;
    std::vector<std::uint32_t> free_index(N, 0);
    std::size_t nf = 0;
    for (std::size_t i = 0; i < N; ++i) free_index[i] = is_fixed[i] ? 0u : std::uint32_t(nf++);
    std::vector<Triplet> reduced;
    reduced.reserve(trip.size());
    std::vector<double> rhs(nf, 0.0);
    for (const Triplet& t : trip) {
        if (is_fixed[t.r]) continue;
        if (is_fixed[t.c]) rhs[free_index[t.r]] -= t.v * fixed[t.c];
        else reduced.push_back({free_index[t.r], free_index[t.c], t.v});
    }
    const SparseMatrix Q = assemble(nf, reduced);

    // Start from the planar projection, aligned so the pins sit near their targets.
    const Vec2 pa = chart.uv[pin_a], pb = chart.uv[pin_b];
    const Vec2 d = pb - pa;
    const float len = std::sqrt(dot(d, d));
    const float c = len > 0 ? d.x / len : 1, s = len > 0 ? d.y / len : 0;
    const float scale = len > 0 ? float(span) / len : 1;
    std::vector<double> x(nf, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 q = chart.uv[i] - pa;
        const double u = (c * q.x + s * q.y) * scale, v = (-s * q.x + c * q.y) * scale;
        if (!is_fixed[i]) x[free_index[i]] = u;
        if (!is_fixed[i + n]) x[free_index[i + n]] = v;
    }
    solve_cg(Q, rhs, x, int(std::min<std::size_t>(20000, 10 * nf + 100)), 1e-7);

    for (std::size_t i = 0; i < n; ++i) {
        const double u = is_fixed[i] ? fixed[i] : x[free_index[i]];
        const double v = is_fixed[i + n] ? fixed[i + n] : x[free_index[i + n]];
        chart.uv[i] = {float(u), float(v)};
    }

    // Keep the map orientation-preserving.
    double signed_area = 0;
    for (Index f : chart.faces) {
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            signed_area += cross(chart.uv[local[fv[i]]] - chart.uv[local[fv[0]]],
                                 chart.uv[local[fv[i + 1]]] - chart.uv[local[fv[0]]]);
        }
    }
    if (signed_area < 0) {
        for (auto& p : chart.uv) p.x = -p.x;
    }
}

std::vector<Vec2> convex_hull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    if (pts.size() < 3) return pts;
    std::vector<Vec2> hull(2 * pts.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 1] - hull[k - 2], pts[i] - hull[k - 2]) <= 0) --k;
        hull[k++] = pts[i];
    }
    for (std::size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(hull[k - 1] - hull[k - 2], pts[i] - hull[k - 2]) <= 0) --k;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);
    return hull;
}

// Scales a chart to true area, rotates it to its tightest box (taller side
// vertical) and moves it to the origin.
void normalise_chart(const Mesh& mesh, Chart& chart) {
    std::unordered_map<Index, Index> local;
    for (Index i = 0; i < chart.vertices.size(); ++i) local.emplace(chart.vertices[i], i);
    double area2d = 0, area3d = 0;
    for (Index f : chart.faces) {
        area3d += mesh.face_area(f);
        auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            area2d += 0.5 * std::abs(cross(chart.uv[local[fv[i]]] - chart.uv[local[fv[0]]],
                                           chart.uv[local[fv[i + 1]]] - chart.uv[local[fv[0]]]));
        }
    }
    const float s = area2d > 0 ? float(std::sqrt(area3d / area2d)) : 1.0f;
    for (auto& p : chart.uv) p = p * s;

    const std::vector<Vec2> hull = convex_hull(chart.uv);
    float best_area = 3.4e38f, best_angle = 0;
    for (int step = 0; step < 45; ++step) {
        const float a = float(step) * std::numbers::pi_v<float> / 90.0f;  // 0..88 degrees
        const float c = std::cos(a), sn = std::sin(a);
        float x0 = 3.4e38f, x1 = -3.4e38f, y0 = 3.4e38f, y1 = -3.4e38f;
        for (Vec2 p : hull) {
            const float x = c * p.x - sn * p.y, y = sn * p.x + c * p.y;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        if ((x1 - x0) * (y1 - y0) < best_area) {
            best_area = (x1 - x0) * (y1 - y0);
            best_angle = a;
        }
    }
    const float c = std::cos(best_angle), sn = std::sin(best_angle);
    Vec2 lo{3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f};
    for (auto& p : chart.uv) {
        p = {c * p.x - sn * p.y, sn * p.x + c * p.y};
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    if (hi.x - lo.x > hi.y - lo.y) {  // shelves pack best with tall charts first
        for (auto& p : chart.uv) p = {p.y, -p.x};
        lo = {3.4e38f, 3.4e38f};
        hi = {-3.4e38f, -3.4e38f};
        for (const auto& p : chart.uv) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
    }
    for (auto& p : chart.uv) p = p - lo;
    chart.size = hi - lo;
}

// Shelf packing of the charts in one tile. Returns the square side used.
float pack_tile(std::vector<Chart*>& charts, float pad) {
    std::sort(charts.begin(), charts.end(), [](const Chart* a, const Chart* b) { return a->size.y > b->size.y; });
    double area = 0;
    float widest = 0;
    for (const Chart* c : charts) {
        area += double(c->size.x + pad) * double(c->size.y + pad);
        widest = std::max(widest, c->size.x + pad);
    }
    const float width = std::max(widest, float(std::sqrt(area) * 1.08));
    float x = 0, y = 0, shelf = 0, used_w = 0;
    for (Chart* c : charts) {
        if (x > 0 && x + c->size.x + pad > width) {
            y += shelf;
            x = 0;
            shelf = 0;
        }
        c->offset = {x + pad * 0.5f, y + pad * 0.5f};
        x += c->size.x + pad;
        used_w = std::max(used_w, x);
        shelf = std::max(shelf, c->size.y + pad);
    }
    return std::max(used_w, y + shelf);
}

}  // namespace

Result<UVSet> auto_unwrap(const Mesh& mesh, const UnwrapOptions& options, UnwrapStats* stats) {
    if (mesh.face_count() == 0) return Error{ErrorCode::InvalidArgument, "unwrap: empty mesh"};
    if (!options.face_groups.empty() && options.face_groups.size() != mesh.face_count()) {
        return Error{ErrorCode::InvalidArgument, "unwrap: face_groups must have one entry per face"};
    }
    const int tiles = std::clamp(options.tiles, 1, 100);
    const int resolution = std::max(16, options.resolution);

    std::vector<Chart> charts = grow_charts(mesh, options);
    parallel_for(
        charts.size(),
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t c = begin; c < end; ++c) {
                parameterise(mesh, charts[c]);
                normalise_chart(mesh, charts[c]);
            }
        },
        1);

    // Spread charts over UDIM tiles by area, largest first.
    std::vector<Chart*> order;
    for (auto& c : charts) order.push_back(&c);
    std::sort(order.begin(), order.end(),
              [](const Chart* a, const Chart* b) { return a->size.x * a->size.y > b->size.x * b->size.y; });
    std::vector<double> tile_area(tiles, 0.0);
    std::vector<std::vector<Chart*>> per_tile(tiles);
    for (Chart* c : order) {
        const auto t = std::size_t(std::min_element(tile_area.begin(), tile_area.end()) - tile_area.begin());
        c->tile = int(t);
        tile_area[t] += double(c->size.x) * c->size.y;
        per_tile[t].push_back(c);
    }

    // Padding is in texels, so it depends on the final scale: iterate.
    float side = 0;
    for (int pass = 0; pass < 3; ++pass) {
        const float pad = pass == 0 ? 0.0f : float(options.padding_texels) * side / float(resolution) * 2.0f;
        side = 0;
        for (auto& tile : per_tile) side = std::max(side, pack_tile(tile, pad));
    }
    const float scale = side > 0 ? 1.0f / side : 1.0f;  // uv units per world unit, shared by all tiles

    UVSet out;
    out.tiles = tiles;
    out.chart_count = charts.size();
    out.face_chart.assign(mesh.face_count(), 0);
    out.corner_uv.assign(mesh.corner_count(), 0);
    for (std::size_t ci = 0; ci < charts.size(); ++ci) {
        Chart& c = charts[ci];
        std::unordered_map<Index, Index> uv_of;
        for (std::size_t i = 0; i < c.vertices.size(); ++i) {
            const Vec2 p = (c.uv[i] + c.offset) * scale;
            uv_of.emplace(c.vertices[i], static_cast<Index>(out.uvs.size()));
            out.uvs.push_back({p.x + float(c.tile), p.y});
        }
        for (Index f : c.faces) {
            out.face_chart[f] = static_cast<Index>(ci);
            const Index base = mesh.face_offsets()[f];
            auto fv = mesh.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) out.corner_uv[base + i] = uv_of[fv[i]];
        }
    }

    if (stats) {
        stats->charts = charts.size();
        stats->texels_per_unit = scale * float(resolution);
        double used = 0;
        for (const auto& c : charts) {
            for (Index f : c.faces) used += mesh.face_area(f);
        }
        stats->utilisation = float(used * scale * scale / tiles);
        const auto distortion = uv_area_distortion(mesh, out);
        float worst = 1;
        for (float d : distortion) {
            if (d > 0) worst = std::max(worst, std::max(d, 1.0f / d));
        }
        stats->max_stretch = worst;
    }
    return out;
}

std::vector<float> uv_area_distortion(const Mesh& mesh, const UVSet& uv) {
    std::vector<float> ratio(mesh.face_count(), 0.0f);
    if (!uv.matches(mesh)) return ratio;
    double total2 = 0, total3 = 0;
    std::vector<double> a2(mesh.face_count()), a3(mesh.face_count());
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        double s = 0;
        for (std::size_t i = 1; i + 1 < mesh.face_size(f); ++i) {
            s += cross(uv.corner(mesh, f, i) - uv.corner(mesh, f, 0), uv.corner(mesh, f, i + 1) - uv.corner(mesh, f, 0));
        }
        a2[f] = std::abs(s) * 0.5;
        a3[f] = mesh.face_area(f);
        total2 += a2[f];
        total3 += a3[f];
    }
    if (total2 <= 0 || total3 <= 0) return ratio;
    const double norm = total3 / total2;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        ratio[f] = a3[f] > 0 ? float(a2[f] / a3[f] * norm) : 0.0f;
    }
    return ratio;
}

std::size_t count_flipped_uv_faces(const Mesh& mesh, const UVSet& uv) {
    if (!uv.matches(mesh)) return 0;
    std::size_t positive = 0, negative = 0;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        double s = 0;
        for (std::size_t i = 1; i + 1 < mesh.face_size(f); ++i) {
            s += cross(uv.corner(mesh, f, i) - uv.corner(mesh, f, 0), uv.corner(mesh, f, i + 1) - uv.corner(mesh, f, 0));
        }
        if (s > 0) ++positive;
        else if (s < 0) ++negative;
    }
    return std::min(positive, negative);
}

}  // namespace cruth
