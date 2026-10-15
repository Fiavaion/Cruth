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
    bool turned = false;          // packed with a quarter turn, not yet applied to uv/size
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
    std::unordered_map<std::uint64_t, bool> seam;
    for (const auto& [p, q] : o.seams) seam[edge_key(p, q)] = true;
    auto neighbour = [&](Index f, std::size_t corner) -> Index {
        auto fv = mesh.face(f);
        const std::uint64_t key = edge_key(fv[corner], fv[(corner + 1) % fv.size()]);
        if (seam.count(key)) return kInvalidIndex;
        const auto& [a, b] = edge_faces.at(key);
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

    // Rotating calipers: the tightest box has a side flush with a hull edge.
    const std::vector<Vec2> hull = convex_hull(chart.uv);
    float best_area = 3.4e38f, best_angle = 0;
    for (std::size_t i = 0; i < hull.size() && hull.size() >= 3; ++i) {
        const Vec2 e = hull[(i + 1) % hull.size()] - hull[i];
        const float len = std::sqrt(e.x * e.x + e.y * e.y);
        if (len <= 0) continue;
        const float c = e.x / len, sn = -e.y / len;  // rotates the edge onto +x
        float x0 = 3.4e38f, x1 = -3.4e38f, y0 = 3.4e38f, y1 = -3.4e38f;
        for (Vec2 p : hull) {
            const float x = c * p.x - sn * p.y, y = sn * p.x + c * p.y;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        if ((x1 - x0) * (y1 - y0) < best_area * 0.99999f) {
            best_area = (x1 - x0) * (y1 - y0);
            best_angle = std::atan2(sn, c);
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
    // Non-finite input positions: collapse the chart to a point so packing
    // stays bounded and the UVs stay finite.
    if (!std::isfinite(chart.size.x) || !std::isfinite(chart.size.y)) {
        for (auto& p : chart.uv) p = {};
        chart.size = {};
    }
}

// MaxRects packing (best short side fit). A tile is a square of side `side`;
// every chart takes a (size + pad) cell and may turn 90 degrees. Deterministic:
// ties resolve by list order, and no hash container is iterated.
struct FreeRect {
    float x, y, w, h;
};

bool contains(const FreeRect& a, const FreeRect& b) {
    return b.x >= a.x && b.y >= a.y && b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h;
}

// Charts are already sorted; returns false when one does not fit.
bool pack_tile_maxrects(const std::vector<Chart*>& charts, float side, float pad) {
    const float eps = side * 1e-6f;
    std::vector<FreeRect> free_rects{{0, 0, side, side}};
    std::vector<FreeRect> next;
    std::vector<char> split;  // parallel to next: 1 for pieces cut by this placement
    for (Chart* c : charts) {
        const float w = c->size.x + pad, h = c->size.y + pad;
        float best_short = 3.4e38f, best_long = 3.4e38f;
        std::size_t best = free_rects.size();
        bool best_turn = false;
        for (std::size_t i = 0; i < free_rects.size(); ++i) {
            const FreeRect& r = free_rects[i];
            for (int turn = 0; turn < 2; ++turn) {
                const float cw = turn ? h : w, ch = turn ? w : h;
                if (cw > r.w + eps || ch > r.h + eps) continue;
                const float lo = std::min(r.w - cw, r.h - ch), hi = std::max(r.w - cw, r.h - ch);
                if (lo < best_short || (lo == best_short && hi < best_long)) {
                    best_short = lo;
                    best_long = hi;
                    best = i;
                    best_turn = turn != 0;
                }
            }
        }
        if (best == free_rects.size()) return false;
        c->turned = best_turn;  // applied by apply_turns once the layout is final
        const FreeRect placed{free_rects[best].x, free_rects[best].y, best_turn ? h : w, best_turn ? w : h};
        c->offset = {placed.x + pad * 0.5f, placed.y + pad * 0.5f};

        next.clear();
        split.clear();
        for (const FreeRect& r : free_rects) {
            if (placed.x >= r.x + r.w || placed.x + placed.w <= r.x || placed.y >= r.y + r.h ||
                placed.y + placed.h <= r.y) {
                next.push_back(r);
                split.push_back(0);
                continue;
            }
            auto add = [&](const FreeRect& n) {
                next.push_back(n);
                split.push_back(1);
            };
            if (placed.x > r.x) add({r.x, r.y, placed.x - r.x, r.h});
            if (placed.x + placed.w < r.x + r.w) add({placed.x + placed.w, r.y, r.x + r.w - placed.x - placed.w, r.h});
            if (placed.y > r.y) add({r.x, r.y, r.w, placed.y - r.y});
            if (placed.y + placed.h < r.y + r.h) add({r.x, placed.y + placed.h, r.w, r.y + r.h - placed.y - placed.h});
        }
        // Untouched rects were maximal before and lie outside the parents of
        // the new pieces, so only new pieces can be redundant: O(new * F)
        // rather than all pairs. Equal rects keep the untouched one, else the first.
        free_rects.clear();
        for (std::size_t i = 0; i < next.size(); ++i) {
            bool redundant = false;
            for (std::size_t j = 0; split[i] && j < next.size() && !redundant; ++j) {
                redundant = i != j && contains(next[j], next[i]) &&
                            (!split[j] || j < i || !contains(next[i], next[j]));
            }
            if (!redundant) free_rects.push_back(next[i]);
        }
    }
    return true;
}

// Skyline bottom-left packing for many charts: each placement costs O(S)
// over the skyline segments instead of MaxRects' growing free list, for 1-2
// points of efficiency. Same contract as pack_tile_maxrects.
struct SkySegment {
    float x, y, w;
};

bool pack_tile_skyline(const std::vector<Chart*>& charts, float side, float pad) {
    const float eps = side * 1e-6f;
    std::vector<SkySegment> sky{{0, 0, side}};
    std::vector<SkySegment> next;
    for (Chart* c : charts) {
        const float w = c->size.x + pad, h = c->size.y + pad;
        float best_top = 3.4e38f, best_y = 0;
        std::size_t best = sky.size();
        bool best_turn = false;
        for (std::size_t i = 0; i < sky.size(); ++i) {
            for (int turn = 0; turn < 2; ++turn) {
                const float cw = turn ? h : w, ch = turn ? w : h;
                const float x = sky[i].x;
                if (x + cw > side + eps) continue;
                float y = sky[i].y;
                for (std::size_t j = i + 1; j < sky.size() && sky[j].x < x + cw - eps; ++j) y = std::max(y, sky[j].y);
                if (y + ch > side + eps) continue;
                if (y + ch < best_top) {  // lowest top; ties keep the leftmost, unturned
                    best_top = y + ch;
                    best_y = y;
                    best = i;
                    best_turn = turn != 0;
                }
            }
        }
        if (best == sky.size()) return false;
        c->turned = best_turn;
        const float x = sky[best].x, cw = best_turn ? h : w, ch = best_turn ? w : h;
        c->offset = {x + pad * 0.5f, best_y + pad * 0.5f};

        // Rebuild: the parts left of the cell, the cell's top, the parts right of it.
        next.clear();
        for (const SkySegment& s : sky) {
            if (s.x < x) next.push_back({s.x, s.y, std::min(s.w, x - s.x)});
        }
        next.push_back({x, best_y + ch, cw});
        for (const SkySegment& s : sky) {
            const float start = std::max(s.x, x + cw);
            if (s.x + s.w > start) next.push_back({start, s.y, s.x + s.w - start});
        }
        sky.clear();
        for (const SkySegment& s : next) {
            if (s.w <= 0) continue;
            if (!sky.empty() && sky.back().y == s.y) sky.back().w = s.x + s.w - sky.back().x;
            else sky.push_back(s);
        }
        if (sky.empty()) sky.push_back({0, best_top, side});  // side 0: only empty cells fit
    }
    return true;
}

// Applies the quarter turns the packer chose. Counter-clockwise keeps the winding.
void apply_turns(std::vector<Chart>& charts) {
    for (Chart& c : charts) {
        if (!c.turned) continue;
        for (auto& p : c.uv) p = {c.size.y - p.y, p.x};
        c.size = {c.size.y, c.size.x};
        c.turned = false;
    }
}

// Above this many charts in a tile MaxRects' free list makes the square
// search slow (0.6-0.85 s per unwrap at 1,500-1,700 charts against 20-70 ms
// for the skyline, for 1-2 points of efficiency), so the skyline takes over.
constexpr std::size_t kMaxRectsChartLimit = 500;

using TilePacker = bool (*)(const std::vector<Chart*>&, float side, float pad);

bool pack_all(const std::vector<std::vector<Chart*>>& tiles, float side, float pad, TilePacker pack) {
    for (const auto& t : tiles) {
        if (!pack(t, side, pad)) return false;
    }
    return true;
}

// Smallest square (to ~0.3%) that holds every tile's charts at padding `pad`
// for the current chart order.
float fit_square(const std::vector<std::vector<Chart*>>& tiles, float pad, TilePacker pack) {
    double most_area = 0;
    float longest = 0;
    for (const auto& t : tiles) {
        double a = 0;
        for (const Chart* c : t) {
            a += double(c->size.x + pad) * double(c->size.y + pad);
            // A cell's long side must fit whichever way it turns.
            longest = std::max(longest, std::max(c->size.x, c->size.y) + pad);
        }
        most_area = std::max(most_area, a);
    }
    float lo = std::max(longest, float(std::sqrt(most_area)));  // cannot be smaller
    if (lo <= 0 && pack_all(tiles, 0.0f, pad, pack)) return 0.0f;      // only empty cells
    float hi = std::max(lo, 1e-20f);
    // Bounded: once hi >= 2 * n * longest, some empty full-height strip at
    // least `longest` wide survives every placement (n cells cover at most
    // n * longest of the x axis, in at most n + 1 gaps), so every cell fits.
    // 24 steps of 4% then doubling reach that for any chart count.
    for (int step = 0; step < 200 && !pack_all(tiles, hi, pad, pack); ++step) {
        lo = hi;
        hi *= step < 24 ? 1.04f : 2.0f;
    }
    for (int i = 0; i < 12 && hi - lo > hi * 0.003f; ++i) {
        const float mid = 0.5f * (lo + hi);
        (pack_all(tiles, mid, pad, pack) ? hi : lo) = mid;
    }
    return hi;
}

void sort_tiles(std::vector<std::vector<Chart*>>& tiles, float (*key)(const Chart&)) {
    for (auto& t : tiles) {
        std::sort(t.begin(), t.end(), [key](const Chart* a, const Chart* b) {
            const float ka = key(*a), kb = key(*b);
            return ka != kb ? ka > kb : a < b;  // pointers into one vector: stable tie-break
        });
    }
}

using OrderKey = float (*)(const Chart&);
constexpr OrderKey kOrderKeys[] = {
    [](const Chart& c) { return std::max(c.size.x, c.size.y); },
    [](const Chart& c) { return c.size.x * c.size.y; },
    [](const Chart& c) { return c.size.x + c.size.y; },
};

// Packs with insertion order `key`, or when `key` is negative tries every
// order, keeps the tightest (ties keep the earlier) and stores it in `key`.
// Leaves the charts placed at the returned size.
float pack_tiles(std::vector<std::vector<Chart*>>& tiles, float pad, TilePacker pack, int& key) {
    float side = 3.4e38f;
    if (key < 0) {
        key = 0;
        for (int k = 0; k < int(std::size(kOrderKeys)); ++k) {
            sort_tiles(tiles, kOrderKeys[k]);
            const float fitted = fit_square(tiles, pad, pack);
            if (fitted < side * 0.9999f) {
                side = fitted;
                key = k;
            }
        }
        sort_tiles(tiles, kOrderKeys[key]);
    } else {
        sort_tiles(tiles, kOrderKeys[key]);
        side = fit_square(tiles, pad, pack);
    }
    pack_all(tiles, side, pad, pack);  // the last probe may have failed or used another order
    return side;
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
              [](const Chart* a, const Chart* b) {
                  const float aa = a->size.x * a->size.y, ab = b->size.x * b->size.y;
                  return aa != ab ? aa > ab : a < b;  // pointers into one vector: stable tie-break
              });
    std::vector<double> tile_area(tiles, 0.0);
    std::vector<std::vector<Chart*>> per_tile(tiles);
    for (Chart* c : order) {
        const auto t = std::size_t(std::min_element(tile_area.begin(), tile_area.end()) - tile_area.begin());
        c->tile = int(t);
        tile_area[t] += double(c->size.x) * c->size.y;
        per_tile[t].push_back(c);
    }

    // Padding is in texels, so it depends on the final scale: iterate until
    // the gap (2 * padding_texels, as before) holds at the size we ended up with.
    // The zero-padding pass only estimates the scale, so it packs in one
    // order. Padded passes try every order: padding changes the cells'
    // proportions, and fixing the order early cost up to 1% efficiency.
    std::size_t most_charts = 0;
    for (const auto& t : per_tile) most_charts = std::max(most_charts, t.size());
    const TilePacker packer = most_charts > kMaxRectsChartLimit ? pack_tile_skyline : pack_tile_maxrects;
    int key = 0;
    float side = pack_tiles(per_tile, 0.0f, packer, key);
    for (int pass = 0; pass < 6; ++pass) {
        const float pad_fraction = float(options.padding_texels) * 2.0f / float(resolution);
        const float pad = pad_fraction * side * (pass < 5 ? 1.02f : 1.1f);
        key = -1;
        const float packed = pack_tiles(per_tile, pad, packer, key);
        const bool holds = pad >= pad_fraction * packed;
        side = packed;
        if (holds) break;
    }
    apply_turns(charts);
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
        stats->tile_utilisation = uv_tile_utilisation(mesh, out);
        const auto distortion = uv_area_distortion(mesh, out);
        float worst = 1;
        for (float d : distortion) {
            if (d > 0) worst = std::max(worst, std::max(d, 1.0f / d));
        }
        stats->max_stretch = worst;
    }
    return out;
}

std::vector<int> face_groups_from_seams(const Mesh& mesh, std::span<const std::pair<Index, Index>> seams) {
    std::unordered_map<std::uint64_t, std::vector<Index>> edge_faces;
    for (Index f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) edge_faces[edge_key(fv[i], fv[(i + 1) % fv.size()])].push_back(f);
    }
    std::unordered_map<std::uint64_t, bool> is_seam;
    for (const auto& [a, b] : seams) is_seam[edge_key(a, b)] = true;
    std::vector<int> group(mesh.face_count(), -1);
    int next = 0;
    std::vector<Index> stack;
    for (Index seed = 0; seed < mesh.face_count(); ++seed) {
        if (group[seed] >= 0) continue;
        group[seed] = next;
        stack.push_back(seed);
        while (!stack.empty()) {
            const Index f = stack.back();
            stack.pop_back();
            auto fv = mesh.face(f);
            for (std::size_t i = 0; i < fv.size(); ++i) {
                const std::uint64_t key = edge_key(fv[i], fv[(i + 1) % fv.size()]);
                if (is_seam.count(key)) continue;
                for (Index g : edge_faces[key]) {
                    if (group[g] < 0) {
                        group[g] = next;
                        stack.push_back(g);
                    }
                }
            }
        }
        ++next;
    }
    return group;
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

std::vector<float> uv_tile_utilisation(const Mesh& mesh, const UVSet& uv) {
    std::vector<double> area(std::size_t(std::max(uv.tiles, 1)), 0.0);
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        double a = 0;
        for (std::size_t i = 1; i + 1 < mesh.face_size(f); ++i) {
            a += 0.5 * std::abs(cross(uv.corner(mesh, f, i) - uv.corner(mesh, f, 0),
                                      uv.corner(mesh, f, i + 1) - uv.corner(mesh, f, 0)));
        }
        const auto tile = std::size_t(std::clamp(int(std::floor(uv.corner(mesh, f, 0).x)), 0, int(area.size()) - 1));
        area[tile] += a;
    }
    return {area.begin(), area.end()};
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
