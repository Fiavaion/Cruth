# Roadmap

The PRD defines four phases gated by measurable exit criteria rather than dates. This file tracks progress against the requirement IDs in [PRD.md](PRD.md).

## Phase 0: headless core (in progress)

**Exit gate:** the core runs the sculpt → remesh → retopo → UV → bake chain on the reference asset from `cruth-cli`, in CI, on macOS and Linux, with no UI.

### Done

- [x] Mono-repo layout (`core/`, `shell-ipad/`, `shell-desktop/`, `spec/`, `tests/`), MIT, DCO, CI on Linux and macOS with sanitizers, plus a GPL guard
- [x] Mesh plus half-edge kernel with clean non-manifold rejection (S-01)
- [x] Triangle BVH with refit: picking, closest point, overlap queries
- [x] Catmull-Clark subdivision and primitives
- [x] Multires stack: up to 8 levels, edits propagate up (subdivided displacement) and down (restriction), lockable level 0, undoable level changes (S-01, object-space detail)
- [x] Brush engine: 9 brushes, falloff and pressure curves, spacing, mirror and radial symmetry, mask, volume-preserving Taubin smoothing (B-01, B-02, B-04, N-02, N-03)
- [x] Exact undo and redo for strokes, level changes and topology steps (L-04, linear)
- [x] Voxel remesh, CPU reference: per-shell signed distance, CSG union, row-wise bit-parallel flood fill, surface nets, manifold repair, relax and project, multithreaded (S-02)
- [x] Decimation by quadric edge collapse (S-04)
- [x] OBJ, STL and PLY I/O (P-06 and P-07, geometry only)
- [x] Print check and repair, including self-intersections and non-manifold vertex splitting (X-01, X-02)
- [x] Exception-free C API and headless CLI with an end-to-end ctest chain (sculpt → multires → subdivide → remesh → decimate → check)
- [x] iPad shell with Metal viewport, raw Pencil input, gestures, multires stepper, remesh and decimate menu, Files I/O (I-01, I-02, I-03, I-04, partial)

### Measured (CPU reference, 4-core Linux VM, Release)

| Operation | Input | Time |
| --- | --- | --- |
| Voxel remesh, 256 | 98k quads | 1.2 s |
| Voxel remesh, 512 | 98k quads → 1.06M quads | 3.9 s |
| Decimate | 74k → 5k triangles | 0.3 s |
| Print check with self-intersections | 98k quads | 0.4 s |

PRD target: 512³ in ≤ 1 s on M4 and ≤ 3 s on A14, through the Metal compute path below.

### Known issues

- Voxel remesh can leave a non-manifold edge where two sheets pinch along a single edge in a concave crease thinner than a voxel. Seen once, at resolution 40 on two overlapping cubes. Subdividing such a mesh reports `NonManifold`; remeshing at a different resolution clears it.
- Remesh projection can leave 1–3 self-intersecting triangle pairs at sharp creases at very high resolution.
- Multires detail is stored in object space, so detail does not rotate with large lower-level edits. Tangent-space detail is needed.
- Remesh, decimate and subdivide run on the main thread in the iPad shell. Background Tasks with Live Activities (R-03) are not wired yet.

### Next, in dependency order

1. **Quad remesh**: Instant Meshes for preview and QuadriFlow for the final result, behind `IRemesher`, then guide strokes that bias the cross-field (R-01, R-02, R-04).
2. **UV**: geometry-central BFF with seams from face groups, xatlas packing, UDIM (U-01, U-04).
3. **Baking**: CPU reference ray-cast baker to tinyexr with normal, height and VDM (K-01, K-02).
4. **Reference asset plus acceptance chain in CI**, with timings recorded per commit.
5. Tangent-space multires detail and sparse on-disk levels, dyntopo (S-03), Metal compute voxel remesh.

## Phase 1: MVP (TestFlight)

Shell work on top of the phase-0 core: retopo workspace and stroke tools (T-01 to T-03, T-06), 2D UV editor (U-03), layers (L-01), autosave (L-04), Metal compute paths, Background Tasks with Live Activities (R-03), open project format v0 (`spec/`), and print profiles and export (X-03 to X-05).

## Phases 2 and 3: v1 and post-v1

See the PRD tables; P1 items land in v1 and P2 items after v1.
