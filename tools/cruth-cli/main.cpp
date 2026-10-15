// cruth-cli — headless driver for the Cruth core (PRD phase 0).
//
// Exercises the same Document commands the iPad shell uses, so every feature
// can be scripted, tested in CI and profiled without an iPad attached.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cruth/bake.hpp"
#include "cruth/cruth.h"
#include "cruth/document.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/print_check.hpp"
#include "cruth/quad_remesh.hpp"
#include "cruth/retopo.hpp"
#include "cruth/subdivide.hpp"
#include "cruth/uv.hpp"
#include "cruth/voxel_remesh.hpp"

using namespace cruth;

namespace {

int usage() {
    std::fputs(
        "usage: cruth-cli <command> [options]\n"
        "\n"
        "commands:\n"
        "  new <sphere|cube|plane> <out> [--levels N] [--size S]  create a base mesh\n"
        "  info <in>                                             counts and bounds\n"
        "  check <in> [--json]                                   3D-print check (exit 2 if not printable)\n"
        "  repair <in> <out>                                     weld, fix winding, fill holes\n"
        "  subdivide <in> <out> [--levels N]                     Catmull-Clark\n"
        "  remesh <in> <out> [--resolution N]                    voxel remesh (8..512, default 128)\n"
        "  decimate <in> <out> --target N                        reduce to N triangles\n"
        "  quadremesh <in> <out> [--faces N] [--sharp] [--adaptive]\n"
        "                                                        field-aligned all-quad remesh\n"
        "  unwrap <in> <out.obj> [--tiles N] [--resolution R] [--angle DEG]\n"
        "                                                        automatic UVs (OBJ with vt)\n"
        "  bake <low.obj> <high> <prefix> [--maps LIST] [--resolution N] [--distance D]\n"
        "       [--ao-samples N] [--format png8|png16|exr|exr32]\n"
        "                                                        maps: normal,object_normal,height,vdm,ao\n"
        "                                                        writes <prefix>_<map>.<1001+tile>.<ext>\n"
        "  convert <in> <out>                                    OBJ / STL / PLY by extension\n"
        "  sculpt <in> <script> <out>                            replay a stroke script\n"
        "  version\n"
        "\n"
        "stroke script lines:\n"
        "  brush <type> [radius R] [strength S] [spacing F] [falloff smooth|sphere|linear|sharp|constant]\n"
        "               [symmetry xyz] [radial N] [axis x|y|z] [invert] [pressure-size]\n"
        "  stroke            begin a stroke with the current brush\n"
        "  p X Y Z [P]       add a sample (P = pressure, default 1)\n"
        "  end               end the stroke\n"
        "  undo | redo | mask-clear | mask-invert\n"
        "  subdivide [N]     add N multires levels and move to the top\n"
        "  level K           sculpt at multires level K\n"
        "  lock-base on|off  keep level 0 fixed when finer levels change\n"
        "  remesh [N]        voxel remesh at resolution N\n"
        "  decimate N        reduce to N triangles\n"
        "  quadremesh N [L]  field-aligned quad remesh to about N faces, keeping the\n"
        "                    sculpt as L multires levels; uses any guide strokes\n"
        "  guide             start a guide stroke (p lines, then end) for quadremesh\n"
        "  guides-clear\n"
        "  retopo-begin      start a retopology over the current sculpt\n"
        "  contour N VX VY VZ  stroke (p lines, then end) -> loop of N vertices, view dir V\n"
        "  strip WIDTH SEGMENTS  stroke (p lines, then end) -> quad strip\n"
        "  tap X Y Z         quad pen: every 4th tap makes a quad, then each 2 more extend the strip\n"
        "  tap-lift          end the quad-pen chain\n"
        "  insert-loop X Y Z [T]  insert a loop across the quad ring through the edge nearest X Y Z\n"
        "  loop-around       stroke (p lines, then end) round a feature -> closed loop through the quads under it\n"
        "  slide-loop X Y Z T  slide the edge loop through the edge nearest X Y Z by T in (-1, 1)\n"
        "  retopo-undo | retopo-redo   step through the retopology's own history\n"
        "  retopo-relax N | retopo-apply L   relax, or replace the sculpt keeping L levels\n"
        "brush types: standard clay inflate smooth flatten pinch crease grab mask\n",
        stderr);
    return 64;
}

int fail(const std::string& message) {
    std::fprintf(stderr, "cruth-cli: %s\n", message.c_str());
    return 1;
}

std::string option(std::vector<std::string>& args, const std::string& name, const std::string& fallback) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            std::string v = args[i + 1];
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
            return v;
        }
    }
    return fallback;
}

bool flag(std::vector<std::string>& args, const std::string& name) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name) {
            args.erase(args.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

Result<Mesh> load(const std::string& path) { return load_mesh(path); }

int save(const Mesh& mesh, const std::string& path) {
    auto s = save_mesh(mesh, path);
    if (!s) return fail(s.error().message);
    std::printf("wrote %s (%zu vertices, %zu faces)\n", path.c_str(), mesh.vertex_count(), mesh.face_count());
    return 0;
}

void print_report(const PrintReport& r, bool json) {
    const Vec3 size = r.bounds.size();
    if (json) {
        std::printf(
            "{\"vertices\":%zu,\"faces\":%zu,\"triangles\":%zu,\"shells\":%zu,\"boundary_edges\":%zu,"
            "\"holes\":%zu,\"non_manifold_edges\":%zu,\"non_manifold_vertices\":%zu,\"inconsistent_edges\":%zu,"
            "\"degenerate_faces\":%zu,\"unreferenced_vertices\":%zu,\"self_intersections\":%zu,"
            "\"volume\":%.9g,\"surface_area\":%.9g,"
            "\"size\":[%.9g,%.9g,%.9g],\"watertight\":%s,\"printable\":%s,\"wall_thickness_checked\":false}\n",
            r.vertices, r.faces, r.triangles, r.shells, r.boundary_edges, r.holes, r.non_manifold_edges,
            r.non_manifold_vertices, r.inconsistent_edges, r.degenerate_faces, r.unreferenced_vertices,
            r.self_intersections, r.volume,
            r.surface_area, double(size.x), double(size.y), double(size.z), r.watertight() ? "true" : "false",
            r.printable() ? "true" : "false");
        return;
    }
    auto line = [](const char* label, std::size_t n) { std::printf("  %-24s %zu\n", label, n); };
    std::printf("mesh\n");
    line("vertices", r.vertices);
    line("faces", r.faces);
    line("triangles", r.triangles);
    line("shells", r.shells);
    std::printf("  %-24s %.6g x %.6g x %.6g\n", "size", double(size.x), double(size.y), double(size.z));
    std::printf("  %-24s %.6g\n", "volume", r.volume);
    std::printf("  %-24s %.6g\n", "surface area", r.surface_area);
    std::printf("problems\n");
    line("holes", r.holes);
    line("boundary edges", r.boundary_edges);
    line("non-manifold edges", r.non_manifold_edges);
    line("non-manifold vertices", r.non_manifold_vertices);
    line("inconsistent winding", r.inconsistent_edges);
    line("degenerate faces", r.degenerate_faces);
    line("unreferenced vertices", r.unreferenced_vertices);
    line("self-intersections", r.self_intersections);
    std::printf("not yet checked: wall thickness, overhangs\n");
    std::printf("result: %s\n", r.printable() ? "PRINTABLE" : (r.watertight() ? "NEEDS REPAIR" : "NOT WATERTIGHT"));
}

bool parse_brush(std::istringstream& in, BrushSettings& b, std::string& error) {
    static const std::map<std::string, BrushType> types = {
        {"standard", BrushType::Standard}, {"clay", BrushType::Clay},       {"inflate", BrushType::Inflate},
        {"smooth", BrushType::Smooth},     {"flatten", BrushType::Flatten}, {"pinch", BrushType::Pinch},
        {"crease", BrushType::Crease},     {"grab", BrushType::Grab},       {"mask", BrushType::Mask},
    };
    std::string type;
    in >> type;
    auto it = types.find(type);
    if (it == types.end()) {
        error = "unknown brush '" + type + "'";
        return false;
    }
    b = BrushSettings{};
    b.type = it->second;
    std::string key;
    while (in >> key) {
        if (key == "radius") in >> b.radius;
        else if (key == "strength") in >> b.strength;
        else if (key == "spacing") in >> b.spacing;
        else if (key == "invert") b.invert = true;
        else if (key == "pressure-size") b.pressure_affects_radius = true;
        else if (key == "radial") in >> b.symmetry.radial_count;
        else if (key == "axis") {
            std::string a;
            in >> a;
            b.symmetry.radial_axis = a == "x" ? 0 : (a == "z" ? 2 : 1);
        } else if (key == "symmetry") {
            std::string axes;
            in >> axes;
            b.symmetry.x = axes.find('x') != std::string::npos;
            b.symmetry.y = axes.find('y') != std::string::npos;
            b.symmetry.z = axes.find('z') != std::string::npos;
        } else if (key == "falloff") {
            std::string f;
            in >> f;
            if (f == "sphere") b.falloff = Curve::sphere_falloff();
            else if (f == "linear") b.falloff = Curve::linear_falloff();
            else if (f == "sharp") b.falloff = Curve::sharp_falloff();
            else if (f == "constant") b.falloff = Curve::constant(1);
            else b.falloff = Curve::smooth_falloff();
        } else {
            error = "unknown brush option '" + key + "'";
            return false;
        }
        if (in.fail()) {
            error = "bad value for '" + key + "'";
            return false;
        }
    }
    return true;
}

int run_sculpt(const std::string& in_path, const std::string& script_path, const std::string& out_path) {
    auto loaded = Document::load(in_path);
    if (!loaded) return fail(loaded.error().message);
    Document& doc = *loaded.value();

    std::ifstream script(script_path);
    if (!script) return fail("cannot open " + script_path);

    BrushSettings brush;
    std::string raw;
    int line_no = 0, strokes = 0;
    // A stroke's samples feed sculpting, a guide or a retopology tool.
    enum class Pending { Sculpt, Guide, Contour, Strip, LoopAround } pending = Pending::Sculpt;
    std::vector<Vec3> points;
    std::vector<std::vector<Vec3>> guides;
    std::unique_ptr<RetopoMesh> retopo;
    int contour_count = 0;
    Vec3 contour_view;
    float strip_width = 0;
    int strip_segments = 0;
    while (std::getline(script, raw)) {
        ++line_no;
        auto hash = raw.find('#');
        if (hash != std::string::npos) raw.resize(hash);
        std::istringstream in(raw);
        std::string cmd;
        if (!(in >> cmd)) continue;
        std::string error;
        auto where = [&] { return script_path + ":" + std::to_string(line_no) + ": "; };
        if (cmd == "brush") {
            if (!parse_brush(in, brush, error)) return fail(where() + error);
        } else if (cmd == "stroke") {
            pending = Pending::Sculpt;
            doc.begin_stroke(brush);
        } else if (cmd == "guide") {
            pending = Pending::Guide;
            points.clear();
        } else if (cmd == "guides-clear") {
            guides.clear();
        } else if (cmd == "retopo-begin") {
            retopo = std::make_unique<RetopoMesh>(doc.sculpt_reference());
        } else if (cmd == "contour" || cmd == "strip") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            if (cmd == "contour") {
                in >> contour_count >> contour_view.x >> contour_view.y >> contour_view.z;
                pending = Pending::Contour;
            } else {
                in >> strip_width >> strip_segments;
                pending = Pending::Strip;
            }
            if (in.fail()) return fail(where() + "bad " + cmd + " arguments");
            points.clear();
        } else if (cmd == "tap" || cmd == "tap-lift") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            if (cmd == "tap-lift") {
                retopo->quad_pen_lift();
                continue;
            }
            Vec3 p;
            if (!(in >> p.x >> p.y >> p.z)) return fail(where() + "expected 'tap X Y Z'");
            auto r = retopo->quad_pen_tap(p);
            if (!r) return fail(where() + r.error().message);
        } else if (cmd == "loop-around") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            pending = Pending::LoopAround;
            points.clear();
        } else if (cmd == "insert-loop") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            Vec3 p;
            float t = 0.5f;
            if (!(in >> p.x >> p.y >> p.z)) return fail(where() + "expected 'insert-loop X Y Z [T]'");
            in >> t;
            const auto [a, b] = retopo->nearest_edge(p);
            auto r = retopo->insert_loop(a, b, t);
            if (!r) return fail(where() + r.error().message);
        } else if (cmd == "slide-loop") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            Vec3 p;
            float t = 0;
            if (!(in >> p.x >> p.y >> p.z >> t)) return fail(where() + "expected 'slide-loop X Y Z T'");
            const auto [a, b] = retopo->nearest_edge(p);
            auto s = retopo->slide_loop(a, b, t);
            if (!s) return fail(where() + s.error().message);
        } else if (cmd == "retopo-undo" || cmd == "retopo-redo") {
            if (!retopo) return fail(where() + "run retopo-begin first");
            const bool done = cmd == "retopo-undo" ? retopo->undo() : retopo->redo();
            if (!done) return fail(where() + "nothing to " + cmd.substr(7));
        } else if (cmd == "retopo-relax") {
            int n = 1;
            in >> n;
            if (!retopo) return fail(where() + "run retopo-begin first");
            retopo->relax(n);
        } else if (cmd == "retopo-apply") {
            int levels = 0;
            in >> levels;
            if (!retopo) return fail(where() + "run retopo-begin first");
            const RetopoStats rs = retopo->stats();
            std::printf("retopology: %zu quads, %zu triangles, %zu n-gons, %zu poles\n", rs.quads, rs.triangles, rs.ngons, rs.poles);
            auto s = doc.adopt_retopology(retopo->mesh(), levels);
            if (!s) return fail(where() + s.error().message);
            retopo.reset();
        } else if (cmd == "p") {
            StrokeSample s;
            in >> s.position.x >> s.position.y >> s.position.z;
            if (in.fail()) return fail(where() + "expected 'p X Y Z [pressure]'");
            if (!(in >> s.pressure)) s.pressure = 1.0f;
            if (pending != Pending::Sculpt) {
                points.push_back(s.position);
                continue;
            }
            if (!doc.in_stroke()) return fail(where() + "sample outside a stroke");
            doc.add_stroke_sample(s);
        } else if (cmd == "end") {
            if (pending == Pending::Guide) {
                guides.push_back(points);
            } else if (pending == Pending::Contour) {
                auto r = retopo->add_contour(points, contour_count, contour_view);
                if (!r) return fail(where() + r.error().message);
            } else if (pending == Pending::LoopAround) {
                auto r = retopo->insert_loop_around(points);
                if (!r) return fail(where() + r.error().message);
                std::printf("loop around: %zu vertices\n", r.value().size());
            } else if (pending == Pending::Strip) {
                auto r = retopo->add_strip(points, strip_width, strip_segments);
                if (!r) return fail(where() + r.error().message);
            } else {
                doc.end_stroke();
                ++strokes;
            }
            pending = Pending::Sculpt;
        } else if (cmd == "undo") {
            doc.undo();
        } else if (cmd == "redo") {
            doc.redo();
        } else if (cmd == "subdivide") {
            int levels = 1;
            in >> levels;
            auto s = doc.subdivide(levels);
            if (!s) return fail(where() + s.error().message);
        } else if (cmd == "level") {
            int level = 0;
            in >> level;
            auto s = doc.set_level(level);
            if (!s) return fail(where() + s.error().message);
        } else if (cmd == "lock-base") {
            std::string on;
            in >> on;
            doc.set_base_locked(on == "on");
        } else if (cmd == "remesh") {
            VoxelRemeshOptions o;
            in >> o.resolution;
            auto r = doc.voxel_remesh(o);
            if (!r) return fail(where() + r.error().message);
        } else if (cmd == "quadremesh") {
            QuadRemeshOptions o;
            if (!(in >> o.target_faces) || o.target_faces < 8) return fail(where() + "expected 'quadremesh N' with N >= 8");
            int levels = 0;
            in >> levels;
            o.guide_strokes = guides;
            auto r = doc.quad_remesh(o, levels);
            if (!r) return fail(where() + r.error().message);
        } else if (cmd == "decimate") {
            std::size_t target = 0;
            if (!(in >> target) || target < 4) return fail(where() + "expected 'decimate N' with N >= 4");
            doc.decimate(target);
        } else if (cmd == "mask-clear") {
            doc.clear_mask();
        } else if (cmd == "mask-invert") {
            doc.invert_mask();
        } else {
            return fail(where() + "unknown command '" + cmd + "'");
        }
    }
    doc.end_stroke();
    std::printf("replayed %d strokes\n", strokes);
    return save(doc.mesh(), out_path);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if (cmd == "version") {
        std::printf("cruth-cli %s\n", cruth_version());
        return 0;
    }
    if (cmd == "new") {
        int levels = std::atoi(option(args, "--levels", "5").c_str());
        float size = std::strtof(option(args, "--size", "1").c_str(), nullptr);
        if (args.size() != 2 || size <= 0) return usage();
        Mesh m;
        if (args[0] == "sphere") m = make_quad_sphere(levels, size);
        else if (args[0] == "cube") m = make_cube(size);
        else if (args[0] == "plane") m = make_plane(levels > 0 ? 1 << std::min(levels, 10) : 1, size);
        else return usage();
        return save(m, args[1]);
    }
    if (cmd == "info" || cmd == "check") {
        bool json = flag(args, "--json");
        if (args.size() != 1) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        PrintReport r = check_printability(m.value());
        if (cmd == "info") {
            Vec3 lo = r.bounds.lo, hi = r.bounds.hi;
            std::printf("%zu vertices, %zu faces, %zu triangles\nbounds [%g %g %g] - [%g %g %g]\n", r.vertices,
                        r.faces, r.triangles, double(lo.x), double(lo.y), double(lo.z), double(hi.x),
                        double(hi.y), double(hi.z));
            return 0;
        }
        print_report(r, json);
        return r.printable() ? 0 : 2;
    }
    if (cmd == "repair") {
        if (args.size() != 2) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        Mesh mesh = std::move(m).value();
        RepairLog log = repair_for_print(mesh);
        std::printf(
            "welded %zu vertices, removed %zu degenerate and %zu duplicate faces, split %zu non-manifold\n"
            "vertices, flipped %zu faces, filled %zu holes, inverted %zu shells, removed %zu unused vertices\n",
            log.welded_vertices, log.removed_degenerate_faces, log.removed_duplicate_faces, log.split_vertices,
            log.flipped_faces, log.filled_holes, log.inverted_shells, log.removed_vertices);
        PrintReport r = check_printability(mesh);
        std::printf("result: %s\n", r.printable() ? "PRINTABLE" : "STILL NEEDS WORK (run check for details)");
        return save(mesh, args[1]);
    }
    if (cmd == "subdivide") {
        int levels = std::atoi(option(args, "--levels", "1").c_str());
        if (args.size() != 2) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        auto sub = subdivide_catmull_clark(m.value(), levels);
        if (!sub) return fail(sub.error().message);
        return save(sub.value(), args[1]);
    }
    if (cmd == "remesh") {
        VoxelRemeshOptions o;
        o.resolution = std::atoi(option(args, "--resolution", "128").c_str());
        if (args.size() != 2) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        VoxelRemeshStats stats;
        auto out = voxel_remesh(m.value(), o, &stats);
        if (!out) return fail(out.error().message);
        std::printf("grid %d x %d x %d, voxel %.6g%s\n", stats.grid[0], stats.grid[1], stats.grid[2],
                    double(stats.voxel_size), stats.input_decimated ? " (input decimated first)" : "");
        return save(out.value(), args[1]);
    }
    if (cmd == "quadremesh") {
        QuadRemeshOptions o;
        o.target_faces = static_cast<std::size_t>(std::atol(option(args, "--faces", "5000").c_str()));
        o.preserve_sharp = flag(args, "--sharp");
        o.adaptive = flag(args, "--adaptive");
        if (args.size() != 2 || o.target_faces < 8) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        QuadRemeshStats stats;
        auto out = quad_remesh(m.value(), o, &stats);
        if (!out) return fail(out.error().message);
        std::printf("%zu faces (%zu quads) from %zu triangles%s\n", stats.faces, stats.quads, stats.input_triangles,
                    stats.input_decimated ? " (input decimated first)" : "");
        return save(out.value(), args[1]);
    }
    if (cmd == "decimate") {
        long target = std::atol(option(args, "--target", "0").c_str());
        if (args.size() != 2 || target < 4) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        DecimateOptions o;
        o.target_triangles = static_cast<std::size_t>(target);
        DecimateStats stats;
        Mesh out = decimate(m.value(), o, &stats);
        std::printf("%zu -> %zu triangles\n", stats.triangles_before, stats.triangles_after);
        return save(out, args[1]);
    }
    if (cmd == "unwrap") {
        UnwrapOptions o;
        o.tiles = std::atoi(option(args, "--tiles", "1").c_str());
        o.resolution = std::atoi(option(args, "--resolution", "2048").c_str());
        o.max_chart_angle_degrees = std::strtof(option(args, "--angle", "60").c_str(), nullptr);
        if (args.size() != 2 || format_from_path(args[1]) != MeshFormat::Obj) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        UnwrapStats stats;
        auto uv = auto_unwrap(m.value(), o, &stats);
        if (!uv) return fail(uv.error().message);
        std::printf("%zu charts, %.1f texels per unit at %d px, %.0f%% of each tile used, worst stretch %.2f, %zu flipped\n",
                    stats.charts, double(stats.texels_per_unit), o.resolution, double(stats.utilisation) * 100.0,
                    double(stats.max_stretch), count_flipped_uv_faces(m.value(), uv.value()));
        auto s = save_mesh(m.value(), args[1], &uv.value());
        if (!s) return fail(s.error().message);
        std::printf("wrote %s\n", args[1].c_str());
        return 0;
    }
    if (cmd == "bake") {
        BakeOptions o;
        o.resolution = std::atoi(option(args, "--resolution", "1024").c_str());
        o.max_distance = std::strtof(option(args, "--distance", "0").c_str(), nullptr);
        o.ao_samples = std::atoi(option(args, "--ao-samples", "32").c_str());
        const std::string format = option(args, "--format", "");
        const std::string list = option(args, "--maps", "normal,height");
        if (args.size() != 3) return usage();
        std::vector<BakeMap> maps;
        for (std::size_t start = 0; start <= list.size();) {
            const std::size_t comma = std::min(list.find(',', start), list.size());
            const std::string name = list.substr(start, comma - start);
            bool found = false;
            for (BakeMap m : {BakeMap::TangentNormal, BakeMap::ObjectNormal, BakeMap::Height,
                              BakeMap::VectorDisplacement, BakeMap::AmbientOcclusion}) {
                if (name == bake_map_name(m)) {
                    maps.push_back(m);
                    found = true;
                }
            }
            if (!found) return fail("unknown map '" + name + "'");
            start = comma + 1;
        }
        UVSet uv;
        auto low = load_mesh(args[0], &uv);
        if (!low) return fail(low.error().message);
        if (!uv.matches(low.value())) return fail(args[0] + " has no UVs; run 'cruth-cli unwrap' first");
        auto high = load(args[1]);
        if (!high) return fail(high.error().message);
        BakeStats stats;
        auto baked = bake(low.value(), uv, high.value(), maps, o, &stats);
        if (!baked) return fail(baked.error().message);
        std::printf("%zu texels, %zu missed (cage distance %.4g)\n", stats.texels, stats.misses, double(stats.max_distance));
        for (const auto& m : baked.value()) {
            // Displacement needs float range; colour-like maps default to 16-bit PNG.
            const bool displacement = m.map == BakeMap::Height || m.map == BakeMap::VectorDisplacement;
            std::string f = format.empty() ? (displacement ? "exr32" : "png16") : format;
            ImageFormat fmt = f == "png8" ? ImageFormat::Png8
                              : f == "png16" ? ImageFormat::Png16
                              : f == "exr" ? ImageFormat::ExrHalf
                              : ImageFormat::ExrFloat;
            const std::string path = args[2] + "_" + bake_map_name(m.map) + "." + std::to_string(1001 + m.tile) +
                                     (f.rfind("png", 0) == 0 ? ".png" : ".exr");
            auto s = write_image(m.image, path, fmt);
            if (!s) return fail(s.error().message);
            std::printf("wrote %s\n", path.c_str());
        }
        return 0;
    }
    if (cmd == "convert") {
        if (args.size() != 2) return usage();
        auto m = load(args[0]);
        if (!m) return fail(m.error().message);
        return save(m.value(), args[1]);
    }
    if (cmd == "sculpt") {
        if (args.size() != 3) return usage();
        return run_sculpt(args[0], args[1], args[2]);
    }
    return usage();
}
