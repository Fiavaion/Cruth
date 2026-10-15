// SPDX-License-Identifier: MIT
#include "cruth/quad_remesh.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>

#include "cruth/bvh.hpp"
#include "cruth/decimate.hpp"
#include "cruth/parallel.hpp"

// QuadriFlow (third_party/quadriflow). Its headers pull Eigen in globally,
// so they are confined to this file.
#include "optimizer.hpp"
#include "parametrizer.hpp"

namespace cruth {

Result<Mesh> quad_remesh(const Mesh& input, const QuadRemeshOptions& options, QuadRemeshStats* stats) {
    if (input.face_count() == 0) return Error{ErrorCode::InvalidArgument, "quad remesh: empty mesh"};
    if (options.target_faces < 8) return Error{ErrorCode::InvalidArgument, "quad remesh: target must be >= 8 faces"};

    Mesh reduced;
    const Mesh* source = &input;
    if (input.triangle_count() > options.max_input_triangles) {
        DecimateOptions d;
        d.target_triangles = options.max_input_triangles;
        reduced = decimate(input, d);
        source = &reduced;
    }

    try {
        auto field = std::make_unique<qflow::Parametrizer>();
        const auto tris = source->triangulated_indices();
        field->V.resize(3, Eigen::Index(source->vertex_count()));
        for (std::size_t i = 0; i < source->vertex_count(); ++i) {
            for (int a = 0; a < 3; ++a) field->V(a, Eigen::Index(i)) = double(source->positions[i][a]);
        }
        field->F.resize(3, Eigen::Index(tris.size() / 3));
        for (std::size_t t = 0; t < tris.size() / 3; ++t) {
            for (int a = 0; a < 3; ++a) field->F(a, Eigen::Index(t)) = int(tris[3 * t + std::size_t(a)]);
        }
        field->NormalizeMesh();
        field->flag_preserve_sharp = options.preserve_sharp;
        field->flag_preserve_boundary = options.preserve_boundary;
        field->flag_adaptive_scale = options.adaptive;
        field->flag_minimum_cost_flow = options.minimum_cost_flow;
        field->flag_aggresive_sat = 0;
        field->hierarchy.rng_seed = options.seed;

        // The same sequence as QuadriFlow's command-line tool.
        field->Initialize(int(options.target_faces));
        if (options.preserve_boundary) {
            qflow::Hierarchy& h = field->hierarchy;
            h.clearConstraints();
            for (std::uint32_t i = 0; i < 3 * h.mF.cols(); ++i) {
                if (h.mE2E[i] != -1) continue;
                const std::uint32_t i0 = h.mF(i % 3, i / 3), i1 = h.mF((i + 1) % 3, i / 3);
                const Eigen::Vector3d p0 = h.mV[0].col(i0), p1 = h.mV[0].col(i1);
                Eigen::Vector3d edge = p1 - p0;
                if (edge.squaredNorm() <= 0) continue;
                edge.normalize();
                h.mCO[0].col(i0) = p0;
                h.mCO[0].col(i1) = p1;
                h.mCQ[0].col(i0) = h.mCQ[0].col(i1) = edge;
                h.mCQw[0][i0] = h.mCQw[0][i1] = h.mCOw[0][i0] = h.mCOw[0][i1] = 1.0;
            }
            h.propagateConstraints();
        }
        qflow::Optimizer::optimize_orientations(field->hierarchy);
        field->ComputeOrientationSingularities();
        if (field->flag_adaptive_scale) field->EstimateSlope();
        qflow::Optimizer::optimize_scale(field->hierarchy, field->rho, field->flag_adaptive_scale);
        field->flag_adaptive_scale = 1;
        qflow::Optimizer::optimize_positions(field->hierarchy, field->flag_adaptive_scale);
        field->ComputePositionSingularities();
        field->ComputeIndexMap();

        // Collect the result in object space, dropping collapsed corners.
        Mesh out;
        std::vector<Index> remap(field->O_compact.size(), kInvalidIndex);
        std::vector<Index> corners;
        std::size_t quads = 0;
        for (const auto& f : field->F_compact) {
            corners.clear();
            for (int k = 0; k < 4; ++k) {
                const int v = f[k];
                if (v < 0 || std::size_t(v) >= remap.size()) continue;
                if (!corners.empty() && corners.back() == Index(v)) continue;
                corners.push_back(Index(v));
            }
            while (corners.size() > 1 && corners.front() == corners.back()) corners.pop_back();
            if (corners.size() < 3) continue;
            for (Index& v : corners) {
                if (remap[v] == kInvalidIndex) {
                    const Eigen::Vector3d p = field->O_compact[v] * field->normalize_scale + field->normalize_offset;
                    remap[v] = out.add_vertex({float(p.x()), float(p.y()), float(p.z())});
                }
                v = remap[v];
            }
            out.add_face(corners);
            quads += corners.size() == 4;
        }
        if (out.face_count() == 0) return Error{ErrorCode::Internal, "quad remesh produced no faces"};

        // QuadriFlow places vertices from its position field, which can fold
        // quads slightly near singularities. A few tangential relax steps,
        // each followed by projection onto the source, even them out.
        if (options.relax_iterations > 0) {
            const TriangleBVH bvh(*source);
            const VertexAdjacency adj = VertexAdjacency::build(out);
            std::vector<Vec3> next(out.vertex_count());
            for (int it = 0; it < options.relax_iterations; ++it) {
                const std::vector<Vec3> normals = out.vertex_normals();
                parallel_for(out.vertex_count(), [&](std::size_t begin, std::size_t end) {
                    for (std::size_t v = begin; v < end; ++v) {
                        auto nb = adj.of(Index(v));
                        Vec3 p = out.positions[v];
                        if (!nb.empty()) {
                            Vec3 avg;
                            for (Index n : nb) avg += out.positions[n];
                            Vec3 d = avg / float(nb.size()) - p;
                            d -= normals[v] * dot(d, normals[v]);  // tangential only
                            p += d * 0.5f;
                        }
                        if (auto cp = bvh.closest_point(p)) p = cp->point;
                        next[v] = p;
                    }
                });
                out.positions.swap(next);
            }

            // Untangle: faces that disagree with the source surface's normal
            // are folded; fully relax their neighbourhood, then re-project.
            for (int pass = 0; pass < 8; ++pass) {
                std::vector<char> bad(out.vertex_count(), 0);
                std::size_t folded = 0;
                for (std::size_t f = 0; f < out.face_count(); ++f) {
                    const Vec3 n = out.face_normal(f);
                    auto cp = bvh.closest_point(out.face_centroid(f));
                    if (!cp) continue;
                    const auto& t = bvh.triangle(cp->triangle);
                    const Vec3 sn = normalize(cross(bvh.positions()[t[1]] - bvh.positions()[t[0]],
                                                    bvh.positions()[t[2]] - bvh.positions()[t[0]]));
                    if (dot(n, sn) > 0.3f) continue;
                    ++folded;
                    for (Index v : out.face(f)) {
                        bad[v] = 1;
                        for (Index u : adj.of(v)) bad[u] = 1;
                    }
                }
                if (folded == 0) break;
                next = out.positions;
                for (Index v = 0; v < out.vertex_count(); ++v) {
                    auto nb = adj.of(v);
                    if (!bad[v] || nb.empty()) continue;
                    Vec3 avg;
                    for (Index n : nb) avg += out.positions[n];
                    Vec3 p = avg / float(nb.size());
                    if (auto cp = bvh.closest_point(p)) p = cp->point;
                    next[v] = p;
                }
                out.positions.swap(next);
            }
        }
        if (stats) {
            stats->input_triangles = source->triangle_count();
            stats->input_decimated = source != &input;
            stats->faces = out.face_count();
            stats->quads = quads;
        }
        return out;
    } catch (const std::bad_alloc&) {
        return Error{ErrorCode::OutOfMemory, "quad remesh: out of memory"};
    } catch (const std::exception& e) {
        return Error{ErrorCode::Internal, std::string("quad remesh failed: ") + e.what()};
    }
}

}  // namespace cruth
