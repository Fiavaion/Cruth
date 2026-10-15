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
- [x] Automatic UVs: charts, LSCM (conjugate gradient), packing with padding, UDIM, distortion and flip metrics; OBJ UV I/O (U-01, U-04, U-06 data)
- [x] Baking, CPU reference: tangent/object normal, height, VDM and AO with cage, dilation and multithreading; PNG 8/16 and EXR half/float (ZIP) writers checked by an independent decoder (K-01, K-02)
- [x] Game-asset export: decimate → unwrap → bake → OBJ + maps, from the C API and the iPad shell
- [x] Exception-free C API and headless CLI with an end-to-end ctest chain (sculpt → multires → subdivide → remesh → decimate → unwrap → bake → decode)
- [x] iPad shell with Metal viewport, raw Pencil input, gestures, multires stepper, remesh and decimate menu, Files I/O (I-01, I-02, I-03, I-04, partial)

### Measured (CPU reference, 4-core Linux VM, Release)

| Operation | Input | Time |
| --- | --- | --- |
| Voxel remesh, 256 | 98k quads | 1.2 s |
| Voxel remesh, 512 | 98k quads → 1.06M quads | 3.9 s |
| Decimate | 74k → 5k triangles | 0.3 s |
| Print check with self-intersections | 98k quads | 0.4 s |
| Unwrap | 1.5k-quad sphere, 8 charts | < 0.01 s |
| Bake normal + height + AO (16 spp) + VDM, 512² | 12k → 98k-quad source | 0.8 s |

PRD target: 512³ in ≤ 1 s on M4 and ≤ 3 s on A14, through the Metal compute path below.

### Known issues

- Voxel remesh can leave a non-manifold edge where two sheets pinch along a single edge in a concave crease thinner than a voxel. Seen once, at resolution 40 on two overlapping cubes. Subdividing such a mesh reports `NonManifold`; remeshing at a different resolution clears it.
- Remesh projection can leave 1–3 self-intersecting triangle pairs at sharp creases at very high resolution.
- Multires detail is stored in object space, so detail does not rotate with large lower-level edits. Tangent-space detail is needed.
- Remesh, decimate and subdivide run on the main thread in the iPad shell; only Export Game Asset runs in the background. Background Tasks with Live Activities (R-03) are not wired yet.
- The baker's tangent frame is per triangle, not MikkTSpace, so normal maps can show faint seams in engines that expect MikkTSpace.
- UV packing is shelf-based on chart bounding boxes: about 35% of a tile is covered on organic shapes. Better packing is P1 (U-04).
- The deflate encoder uses fixed Huffman codes only, so files are larger than zlib's level 6 output. They are valid and readable everywhere.

### Next, in dependency order

1. **Quad remesh**: field-aligned quad remesher (Instant Meshes-style orientation and position fields, or QuadriFlow vendored with a Boost-free max-flow), behind `IRemesher`, then guide strokes that bias the cross-field (R-01, R-02, R-04).
2. **Manual retopology** core operations (T-01 to T-03, T-06), then the reference asset plus the acceptance chain in CI with timings per commit.
3. MikkTSpace tangents, curvature and cavity bakes, better chart packing, seam painting (U-02).
4. Tangent-space multires detail and sparse on-disk levels, dyntopo (S-03), Metal compute paths for remesh and bake (K-03).

## Phase 1: MVP (TestFlight)

Shell work on top of the phase-0 core: retopo workspace and stroke tools (T-01 to T-03, T-06), 2D UV editor (U-03), layers (L-01), autosave (L-04), Metal compute paths, Background Tasks with Live Activities (R-03), open project format v0 (`spec/`), and print profiles and export (X-03 to X-05).

## Phases 2 and 3: v1 and post-v1

See the PRD tables; P1 items land in v1 and P2 items after v1.
