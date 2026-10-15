// cruth-acceptance — the PRD's phase-0 exit gate, headless.
//
// PRD acceptance test: "sculpt a 5M-triangle head, remesh to 20k quads with two
// guide strokes, retopo the eye loops by hand, auto-unwrap with painted seams,
// pack to 2 UDIM tiles, bake 4K normal + 32-bit displacement to EXR, open in
// Blender with correct displacement — under 10 minutes."
//
// This tool runs that chain on a deterministic reference head, times every
// stage and checks the result. "Correct displacement" is verified directly:
// the high surface is rebuilt from the low mesh plus the baked height map and
// compared with the real sculpt.
//
// Differences from the PRD wording, tracked in docs/ROADMAP.md: the full
// scale sculpts ~3M triangles rather than 5M, and the hand retopology step
// is a snapped relax, because loop cuts on a field-aligned remesh follow
// helical rings across the whole mesh.
//
//   cruth-acceptance [--scale smoke|full] [--out DIR] [--report FILE]
//
// Exit status 0 when every quality gate passes. The full scale also checks
// the 10-minute budget; timings depend on the machine, so CI records them.
// SPDX-License-Identifier: MIT
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "cruth/bake.hpp"
#include "cruth/bvh.hpp"
#include "cruth/document.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/print_check.hpp"
#include "cruth/quad_remesh.hpp"
#include "cruth/retopo.hpp"
#include "cruth/uv.hpp"

using namespace cruth;
using Clock = std::chrono::steady_clock;

namespace {

struct Scale {
    const char* name;
    int subdivide_levels;  // on top of a 6k-quad sphere
    std::size_t quads;
    int texture;
};
constexpr Scale kSmoke{"smoke", 2, 5000, 1024};
constexpr Scale kFull{"full", 4, 20000, 4096};

struct Report {
    std::vector<std::pair<std::string, double>> stages;
    std::vector<std::pair<std::string, std::string>> facts;
    std::vector<std::pair<std::string, bool>> gates;
    Clock::time_point start = Clock::now(), last = Clock::now();

    void stage(const std::string& name) {
        const auto now = Clock::now();
        const double s = std::chrono::duration<double>(now - last).count();
        stages.push_back({name, s});
        last = now;
        std::printf("  %-28s %8.2f s\n", name.c_str(), s);
        std::fflush(stdout);
    }
    template <typename T>
    void fact(const std::string& key, T value) {
        std::ostringstream os;
        os << value;
        facts.push_back({key, os.str()});
    }
    void gate(const std::string& name, bool ok) {
        gates.push_back({name, ok});
        std::printf("  [%s] %s\n", ok ? "pass" : "FAIL", name.c_str());
    }
    double total() const { return std::chrono::duration<double>(last - start).count(); }
    bool passed() const {
        for (const auto& g : gates) {
            if (!g.second) return false;
        }
        return true;
    }
    std::string json(const Scale& scale) const {
        std::ostringstream os;
        os << "{\n  \"scale\": \"" << scale.name << "\",\n  \"total_seconds\": " << total() << ",\n  \"stages\": {";
        for (std::size_t i = 0; i < stages.size(); ++i) {
            os << (i ? "," : "") << "\n    \"" << stages[i].first << "\": " << stages[i].second;
        }
        os << "\n  },\n  \"facts\": {";
        for (std::size_t i = 0; i < facts.size(); ++i) {
            os << (i ? "," : "") << "\n    \"" << facts[i].first << "\": \"" << facts[i].second << "\"";
        }
        os << "\n  },\n  \"gates\": {";
        for (std::size_t i = 0; i < gates.size(); ++i) {
            os << (i ? "," : "") << "\n    \"" << gates[i].first << "\": " << (gates[i].second ? "true" : "false");
        }
        os << "\n  },\n  \"passed\": " << (passed() ? "true" : "false") << "\n}\n";
        return os.str();
    }
};

const Vec3 kEyes[2] = {normalize(Vec3{0.25f, 0.2f, 0.95f}), normalize(Vec3{-0.25f, 0.2f, 0.95f})};

void stroke(Document& doc, BrushSettings brush, std::initializer_list<Vec3> pts, int samples = 24) {
    std::vector<Vec3> p(pts);
    doc.begin_stroke(brush);
    for (std::size_t s = 0; s + 1 < p.size(); ++s) {
        for (int i = 0; i <= samples; ++i) {
            doc.add_stroke_sample({normalize(lerp(p[s], p[s + 1], float(i) / float(samples))), 1.0f});
        }
    }
    doc.end_stroke();
}

BrushSettings brush(BrushType type, float radius, float strength, bool invert = false) {
    BrushSettings b;
    b.type = type;
    b.radius = radius;
    b.strength = strength;
    b.invert = invert;
    b.symmetry.x = true;
    return b;
}

// Deterministic reference head: block-out at a low level, refined detail at the top.
Mesh sculpt_head(Document& doc, const Scale& scale) {
    stroke(doc, brush(BrushType::Clay, 0.28f, 0.9f), {{0.05f, 0.42f, 0.9f}, {0.32f, 0.4f, 0.86f}});           // brow
    stroke(doc, brush(BrushType::Standard, 0.2f, 1.0f), {{0.0f, 0.18f, 1.0f}, {0.0f, -0.08f, 1.0f}});          // nose
    stroke(doc, brush(BrushType::Standard, 0.17f, 1.0f, true), {kEyes[0], kEyes[0] + Vec3{0.03f, 0, 0}}, 4);  // sockets
    stroke(doc, brush(BrushType::Clay, 0.25f, 0.7f), {{0.42f, -0.05f, 0.85f}, {0.5f, -0.2f, 0.75f}});          // cheekbones
    stroke(doc, brush(BrushType::Crease, 0.08f, 0.8f), {{0.0f, -0.38f, 0.93f}, {0.2f, -0.35f, 0.9f}});         // mouth
    for (int i = 0; i < scale.subdivide_levels; ++i) (void)doc.subdivide(1);
    // Fine detail on the top level: wrinkles the low mesh cannot carry.
    for (int k = 0; k < 4; ++k) {
        const float y = 0.55f + 0.06f * float(k);
        stroke(doc, brush(BrushType::Crease, 0.035f, 0.6f), {{0.0f, y, 0.83f}, {0.25f, y - 0.01f, 0.79f}});
    }
    return doc.sculpt_reference();
}

// A loop of surface points around `center`, for a guide stroke.
std::vector<Vec3> ring_around(const TriangleBVH& surface, Vec3 center, float radius) {
    const Vec3 n = normalize(center);
    const Vec3 t1 = normalize(cross(n, Vec3{0, 1, 0})), t2 = cross(n, t1);
    std::vector<Vec3> ring;
    for (int i = 0; i <= 48; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * float(i) / 48.0f;
        const Vec3 p = center + (t1 * std::cos(a) + t2 * std::sin(a)) * radius;
        ring.push_back(surface.closest_point(p)->point);
    }
    return ring;
}

// The seam a user would paint down the back of the head: shortest edge path
// from crown to chin that stays behind the face.
std::vector<std::pair<Index, Index>> paint_back_seam(const Mesh& m) {
    auto nearest = [&](Vec3 p) {
        Index best = 0;
        for (Index v = 1; v < m.vertex_count(); ++v) {
            if (length_squared(m.positions[v] - p) < length_squared(m.positions[best] - p)) best = v;
        }
        return best;
    };
    const Index from = nearest({0, 1.1f, -0.2f}), to = nearest({0, -1.1f, -0.2f});
    const VertexAdjacency adj = VertexAdjacency::build(m);
    std::vector<double> dist(m.vertex_count(), 1e300);
    std::vector<Index> prev(m.vertex_count(), kInvalidIndex);
    using Item = std::pair<double, Index>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> q;
    dist[from] = 0;
    q.push({0, from});
    while (!q.empty()) {
        auto [d, v] = q.top();
        q.pop();
        if (d > dist[v]) continue;
        if (v == to) break;
        for (Index u : adj.of(v)) {
            const Vec3 mid = (m.positions[u] + m.positions[v]) * 0.5f;
            const double w = distance(m.positions[u], m.positions[v]) * (1.0 + 20.0 * std::max(0.0f, mid.z + 0.1f) +
                                                                          10.0 * std::abs(mid.x));
            if (d + w < dist[u]) {
                dist[u] = d + w;
                prev[u] = v;
                q.push({d + w, u});
            }
        }
    }
    std::vector<std::pair<Index, Index>> seam;
    for (Index v = to; prev[v] != kInvalidIndex; v = prev[v]) seam.push_back({prev[v], v});
    return seam;
}

}  // namespace

int main(int argc, char** argv) {
    Scale scale = kSmoke;
    std::string out_dir = "acceptance-out", report_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scale" && i + 1 < argc) scale = std::string(argv[++i]) == "full" ? kFull : kSmoke;
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--report" && i + 1 < argc) report_path = argv[++i];
        else {
            std::fprintf(stderr, "usage: cruth-acceptance [--scale smoke|full] [--out DIR] [--report FILE]\n");
            return 64;
        }
    }
    std::filesystem::create_directories(out_dir);
    if (report_path.empty()) report_path = out_dir + "/report.json";
    std::printf("Cruth acceptance chain (%s scale)\n", scale.name);
    Report r;

    // 1. Sculpt.
    Document doc(make_quad_sphere(5, 1.0f));
    const Mesh high = sculpt_head(doc, scale);
    r.stage("sculpt");
    r.fact("sculpt_triangles", high.triangle_count());
    const TriangleBVH high_bvh(high);
    r.stage("reference_bvh");

    // 2. Guided quad remesh: one loop around each eye.
    QuadRemeshOptions qo;
    qo.target_faces = scale.quads;
    qo.guide_strokes = {ring_around(high_bvh, kEyes[0], 0.16f), ring_around(high_bvh, kEyes[1], 0.16f)};
    QuadRemeshStats qs;
    auto low_result = quad_remesh(high, qo, &qs);
    r.stage("quad_remesh");
    if (!low_result) {
        std::fprintf(stderr, "quad remesh failed: %s\n", low_result.error().message.c_str());
        return 1;
    }
    r.fact("remesh_faces", qs.faces);
    r.fact("remesh_quads", qs.quads);

    // 3. Retopology touch-up: snapped relax over the remesh, as a user would
    // before baking. (Insert-loop is not used here: field-aligned remeshes
    // have helical face rings, so a loop cut would run over the whole mesh;
    // see docs/ROADMAP.md.)
    RetopoMesh retopo(high, std::move(low_result).value());
    retopo.relax(1);
    const Mesh low = retopo.mesh();
    const RetopoStats rs = retopo.stats();
    r.stage("retopo_touch_up");
    r.fact("low_quads", rs.quads);
    r.fact("low_poles", rs.poles);

    // 4. Painted seam + unwrap to 2 UDIM tiles.
    const auto seam = paint_back_seam(low);
    UnwrapOptions uo;
    uo.seams = seam;
    uo.tiles = 2;
    uo.resolution = scale.texture;
    UnwrapStats us;
    auto uv = auto_unwrap(low, uo, &us);
    r.stage("unwrap");
    if (!uv) {
        std::fprintf(stderr, "unwrap failed: %s\n", uv.error().message.c_str());
        return 1;
    }
    r.fact("seam_edges", seam.size());
    r.fact("uv_charts", us.charts);
    r.fact("uv_utilisation", us.utilisation);

    // 5. Bake 4K normal + 32-bit displacement.
    const BakeMap maps[] = {BakeMap::TangentNormal, BakeMap::Height};
    BakeOptions bo;
    bo.resolution = scale.texture;
    BakeStats bs;
    auto baked = bake(low, uv.value(), high, maps, bo, &bs);
    r.stage("bake");
    if (!baked) {
        std::fprintf(stderr, "bake failed: %s\n", baked.error().message.c_str());
        return 1;
    }
    r.fact("bake_texels", bs.texels);
    r.fact("bake_misses", bs.misses);

    // 6. Write the asset as a DCC would load it.
    (void)save_mesh(low, out_dir + "/head_low.obj", &uv.value());
    std::vector<const Image*> height_tiles(2, nullptr);
    for (const auto& m : baked.value()) {
        const std::string tile = std::to_string(1001 + m.tile);
        if (m.map == BakeMap::Height) {
            (void)write_image(m.image, out_dir + "/head_height." + tile + ".exr", ImageFormat::ExrFloat);
            if (m.tile < 2) height_tiles[std::size_t(m.tile)] = &m.image;
        } else {
            (void)write_image(m.image, out_dir + "/head_normal." + tile + ".png", ImageFormat::Png16);
        }
    }
    r.stage("write_files");

    // 7. Verify: rebuild the sculpt from low mesh + height map and measure the error.
    const std::vector<Vec3> normals = low.vertex_normals();
    double err_with = 0, err_without = 0, worst = 0;
    int samples = 0;
    std::uint32_t rng = 12345;
    auto rand01 = [&] {
        rng = rng * 1664525u + 1013904223u;
        return float(rng >> 8) / 16777216.0f;
    };
    for (int s = 0; s < 20000; ++s) {
        const std::size_t f = std::size_t(rand01() * float(low.face_count())) % low.face_count();
        auto fv = low.face(f);
        const std::size_t k = 1 + std::size_t(rand01() * float(fv.size() - 2)) % (fv.size() - 2);
        float b1 = rand01(), b2 = rand01();
        if (b1 + b2 > 1) {
            b1 = 1 - b1;
            b2 = 1 - b2;
        }
        const float b0 = 1 - b1 - b2;
        const Index i0 = fv[0], i1 = fv[k], i2 = fv[k + 1];
        const Vec3 p = low.positions[i0] * b0 + low.positions[i1] * b1 + low.positions[i2] * b2;
        const Vec3 n = normalize(normals[i0] * b0 + normals[i1] * b1 + normals[i2] * b2);
        const Vec2 t = uv.value().corner(low, f, 0) * b0 + uv.value().corner(low, f, k) * b1 + uv.value().corner(low, f, k + 1) * b2;
        const int tile = int(std::floor(t.x));
        if (tile < 0 || tile > 1 || !height_tiles[std::size_t(tile)]) continue;
        const Image& h = *height_tiles[std::size_t(tile)];
        const int px = std::clamp(int((t.x - float(tile)) * float(h.width)), 0, h.width - 1);
        const int py = std::clamp(int((1.0f - t.y) * float(h.height)), 0, h.height - 1);
        const Vec3 rebuilt = p + n * h.at(px, py, 0);
        const double with = high_bvh.closest_point(rebuilt)->distance;
        const double without = high_bvh.closest_point(p)->distance;
        err_with += with * with;
        err_without += without * without;
        worst = std::max(worst, with);
        ++samples;
    }
    err_with = std::sqrt(err_with / std::max(samples, 1));
    err_without = std::sqrt(err_without / std::max(samples, 1));
    const double diagonal = length(high.bounds().size());
    r.stage("verify");
    r.fact("displacement_rms_error", err_with);
    r.fact("low_poly_rms_error", err_without);
    r.fact("displacement_max_error", worst);

    // Gates.
    const PrintReport pr = check_printability(low);
    r.gate("sculpt has >= 1M triangles (full) / >= 150k (smoke)",
           high.triangle_count() >= (scale.subdivide_levels >= 4 ? 1000000u : 150000u));
    r.gate("remesh within 25% of the quad target",
           qs.faces >= scale.quads * 3 / 4 && qs.faces <= scale.quads * 5 / 4);
    r.fact("low_faces", low.face_count());
    r.fact("low_non_quads", rs.triangles + rs.ngons);
    r.fact("low_boundary_edges", pr.boundary_edges);
    r.fact("low_non_manifold_edges", pr.non_manifold_edges);
    r.fact("low_non_manifold_vertices", pr.non_manifold_vertices);
    r.fact("low_degenerate_faces", pr.degenerate_faces);
    r.gate("low mesh is all quads, watertight and manifold",
           rs.triangles + rs.ngons == 0 && pr.watertight() && pr.non_manifold_vertices == 0);
    r.gate("UVs: no flipped faces", count_flipped_uv_faces(low, uv.value()) == 0);
    r.gate("UVs: both UDIM tiles used", height_tiles[0] && height_tiles[1]);
    r.gate("bake: under 1% of texels missed", bs.misses * 100 <= bs.texels);
    r.gate("displacement rebuilds the sculpt (RMS < 0.1% of size)", err_with < 0.001 * diagonal);
    r.gate("displacement beats the bare low mesh by 5x", err_with * 5 < err_without);
    if (scale.subdivide_levels >= 4) r.gate("PRD budget: under 10 minutes", r.total() < 600.0);
    std::printf("  total %.1f s\n", r.total());

    std::ofstream(report_path) << r.json(scale);
    std::printf("report: %s\n", report_path.c_str());
    return r.passed() ? 0 : 1;
}
