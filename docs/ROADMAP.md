# Roadmap

The PRD defines four phases gated by measurable exit criteria rather than dates. This file tracks progress against the requirement IDs in [PRD.md](PRD.md).

## Phase 0: headless core (in progress)

**Exit gate:** the core runs the sculpt → remesh → retopo → UV → bake chain on the reference asset from `cruth-cli`, in CI, on macOS and Linux, with no UI.

### Done

- [x] Mono-repo layout (`core/`, `shell-ipad/`, `shell-desktop/`, `spec/`, `tests/`), MIT, DCO, CI on Linux and macOS with sanitizers, plus a GPL guard
- [x] Mesh plus half-edge kernel with clean non-manifold rejection (S-01, partial)
- [x] Catmull-Clark subdivision and primitives (reference implementation for S-01)
- [x] Brush engine: 9 brushes, falloff and pressure curves, spacing, mirror and radial symmetry, mask (B-01, B-02, B-04, N-02, N-03)
- [x] Exact undo and redo for strokes and topology steps (L-04, linear)
- [x] OBJ, STL and PLY I/O (P-06 and P-07, geometry only)
- [x] Print check and repair, topological part (X-01, X-02)
- [x] Exception-free C API and headless CLI with an end-to-end ctest chain
- [x] iPad shell with Metal viewport, raw Pencil input, gestures and Files I/O (I-01, I-02, I-03, I-04, partial)

### Next, in dependency order

1. **BVH over triangles with refit.** Replaces brute-force raycasts and brush queries, and is needed for 1M+ triangles (S-07), baking (K-03) and self-intersection checks (X-01).
2. **Multires stack** on OpenSubdiv: sparse per-level deltas, lockable level 0, reverse-subdivide (S-01).
3. **Voxel remesh**: sparse SDF plus marching cubes, CPU reference first, then Metal compute (S-02).
4. **Quad remesh**: Instant Meshes for preview and QuadriFlow for the final result, behind `IRemesher`, then guide strokes that bias the cross-field (R-01, R-02, R-04).
5. **UV**: geometry-central BFF with seams from face groups, xatlas packing, UDIM (U-01, U-04).
6. **Baking**: CPU reference ray-cast baker to tinyexr with normal, height and VDM (K-01, K-02).
7. **Reference asset plus acceptance chain in CI**, with timings recorded per commit.
8. Volume-preserving smoothing (the current Smooth brush is a plain Laplacian and shrinks convex forms on repeated passes), dyntopo (S-03) and decimation (S-04).

## Phase 1: MVP (TestFlight)

Shell work on top of the phase-0 core: retopo workspace and stroke tools (T-01 to T-03, T-06), 2D UV editor (U-03), layers (L-01), autosave (L-04), Metal compute paths, Background Tasks with Live Activities (R-03), open project format v0 (`spec/`), and print profiles and export (X-03 to X-05).

## Phases 2 and 3: v1 and post-v1

See the PRD tables; P1 items land in v1 and P2 items after v1.
