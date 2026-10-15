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
#include <sstream>
#include <string>
#include <vector>

#include "cruth/cruth.h"
#include "cruth/document.hpp"
#include "cruth/mesh_io.hpp"
#include "cruth/primitives.hpp"
#include "cruth/print_check.hpp"
#include "cruth/subdivide.hpp"

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
        "  undo | redo | subdivide [N] | mask-clear | mask-invert\n"
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
            "\"degenerate_faces\":%zu,\"unreferenced_vertices\":%zu,\"volume\":%.9g,\"surface_area\":%.9g,"
            "\"size\":[%.9g,%.9g,%.9g],\"watertight\":%s,\"printable\":%s,"
            "\"self_intersections_checked\":false,\"wall_thickness_checked\":false}\n",
            r.vertices, r.faces, r.triangles, r.shells, r.boundary_edges, r.holes, r.non_manifold_edges,
            r.non_manifold_vertices, r.inconsistent_edges, r.degenerate_faces, r.unreferenced_vertices, r.volume,
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
    std::printf("not yet checked: self-intersections, wall thickness, overhangs\n");
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
            doc.begin_stroke(brush);
        } else if (cmd == "p") {
            StrokeSample s;
            in >> s.position.x >> s.position.y >> s.position.z;
            if (in.fail()) return fail(where() + "expected 'p X Y Z [pressure]'");
            if (!(in >> s.pressure)) s.pressure = 1.0f;
            if (!doc.in_stroke()) return fail(where() + "sample outside a stroke");
            doc.add_stroke_sample(s);
        } else if (cmd == "end") {
            doc.end_stroke();
            ++strokes;
        } else if (cmd == "undo") {
            doc.undo();
        } else if (cmd == "redo") {
            doc.redo();
        } else if (cmd == "subdivide") {
            int levels = 1;
            in >> levels;
            auto s = doc.subdivide(levels);
            if (!s) return fail(where() + s.error().message);
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
            "welded %zu vertices, removed %zu degenerate and %zu duplicate faces, flipped %zu faces,\n"
            "filled %zu holes, inverted %zu shells, removed %zu unused vertices\n",
            log.welded_vertices, log.removed_degenerate_faces, log.removed_duplicate_faces, log.flipped_faces,
            log.filled_holes, log.inverted_shells, log.removed_vertices);
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
