# Roadmap

The PRD defines four phases gated by measurable exit criteria rather than dates. This file tracks progress against the requirement IDs in [PRD.md](PRD.md).

## Phase 0: headless core (exit gate met locally, with the deviations under Known issues)

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
- [x] Quad remesh: QuadriFlow vendored without Boost or external processes; large flow problems solved by one capacity-scaling min-cost max-flow with a bypass arc (5.7x faster than lemon's preflow + network simplex); relax, untangle and project post-pass; Document, C API, CLI and iPad menu (R-01, R-03 input cap)
- [x] Guide strokes steer the quad remesher's orientation field: mean edge misalignment along a tilted loop drops from 19.7° to 2.8° (R-02)
- [x] Remesh keeping detail: subdivide the new base and project the finest level onto the old sculpt, so detail survives as multires (R-04, T-06)
- [x] Manual retopology core: view-plane contour loops with auto-bridging, strips, bridge, quad pen, surface-snapped relax, delete, stats, adopt back into the Document; C API, CLI scripts and an iPad Retopo mode (T-01 to T-03, T-05 stats, T-06)
- [x] OBJ, STL and PLY I/O (P-06 and P-07, geometry only)
- [x] Print check and repair, including self-intersections and non-manifold vertex splitting (X-01, X-02)
- [x] Automatic UVs: charts, LSCM (conjugate gradient), packing with padding, UDIM, distortion and flip metrics; OBJ UV I/O (U-01, U-04, U-06 data)
- [x] Baking, CPU reference: tangent/object normal, height, VDM and AO with cage, dilation and multithreading; PNG 8/16 and EXR half/float (ZIP) writers checked by an independent decoder (K-01, K-02)
- [x] Game-asset export: decimate → unwrap → bake → OBJ + maps, from the C API and the iPad shell
- [x] Phase-0 exit gate: `cruth-acceptance` runs the PRD acceptance chain on a deterministic reference head (sculpt → guided quad remesh → retopology touch-up → painted back seam + 2-tile UDIM unwrap → tangent normal + float displacement bake → OBJ/PNG/EXR), times every stage, and verifies "correct displacement" by rebuilding the sculpt from the low mesh plus the baked height map. Smoke scale is a ctest and a CI job; full scale runs from `workflow_dispatch`. Reports go to `report.json` and CI artifacts
- [x] Retopology insert loop across quad rings (self-crossing rings split quads in four; non-quad ends absorb the cut, so meshes stay watertight); painted seams that unwrap charts never cross (T-03, U-02 core)
- [x] Exception-free C API and headless CLI with an end-to-end ctest chain (sculpt → multires → subdivide → remesh → decimate → unwrap → bake → decode)
- [x] iPad shell with Metal viewport, raw Pencil input, gestures, multires stepper, remesh and decimate menu, Files I/O (I-01, I-02, I-03, I-04, partial)
- [x] First Mac build: the shell builds and runs on Xcode 26.6 (iPad Pro 13-inch M5 simulator, iOS 26.5) and the core passes ctest on macOS/arm64; device signing tested through `Signing.local.xcconfig`

#### Shell and core slices added since the first Mac build

- [x] Finger sculpting: a finger on the model sculpts, a finger on the background still orbits; a mouse or trackpad sculpts the same way (I-01)
- [x] "Draw with Finger" toggle in the Guides and Retopo panels
- [x] Guide and cage overlays are occluded by the model, with a faint dashed pass for the hidden parts
- [x] XCUITest smoke suite (26 tests) behind `./scripts/ipad.sh test`
- [x] Autosave and restore of the current sculpt (L-04, partial): binary PLY of the current level in Application Support/Autosave, written off the main thread from a core snapshot (`cruth_document_snapshot`, `cruth_mesh_save`, `cruth_mesh_destroy` in `cruth.h`), debounced (3 s quiet, 60 s cap, 30 s minimum), saved inside a background task on background, atomic replace. Restored at launch with a message; a restore marker guards against crash loops and broken files are set aside, keeping the newest 3. "New Sculpt" is the reset command. Not kept: levels above and below the current one, undo history, mask, guides, retopo cage
- [x] On-device diagnostics: os.Logger (subsystem `org.cruth.app`), a rolling 1 MB log file with absolute paths redacted, and "Share Diagnostics…" in the Cruth menu
- [x] iPad shell is single-window (`UIApplicationSupportsMultipleScenes` false)
- [x] Speed: partial BVH refit after brush samples (exact; full refit above 1/16 of the vertices moved); overlay refresh cached on its inputs and coalesced to once per frame; `ipad.sh --release`; the Release config sets `GCC_OPTIMIZATION_LEVEL` 2
- [x] First device install: iPad Pro 12.9" M1, iPadOS 27.0.1, Xcode 27.1, Release build. `ipad.sh device` builds for the connected iPad with `-allowProvisioningDeviceRegistration` and skips simulators
- [x] GCC 16 (Homebrew) run on this Mac: clean, 3/3 ctest. GitHub Actions CI is not running at the moment (account billing problem), so local GCC, ASan and licence-guard runs stand in
- [x] C++ core optimised in iPad Debug builds: the quad remesh UI test drops from 73 s to 14 s
- [x] MikkTSpace tangents for tangent-space normal bakes, with the exact inverse on encode; VDM uses an orthonormal unit-frame TBN; both decode rules are documented in `bake.hpp` (K-01, K-02)
- [x] Game-asset OBJ export is fan-triangulated with `vn`, so it matches the bake
- [x] UV packing: exact rotating-calipers chart boxes, MaxRects with 90° turns, tightest square tile, skyline packer above 500 charts per tile, texel padding reported in `UnwrapStats` (U-04)
- [x] Retopology core: quad-pen taps chained into strips, slide loop, closed loop around a feature (`insert_loop_around`), insert loop in the C API, 32-step cage undo history, quad-pen rejection of folded, crossed and repeated quads (T-02, T-03)
- [x] iPad Retopo tools: Quad Pen, Slide Loop, Insert Loop and Loop Around, finger-driven, with cage undo in Retopo mode (T-02, T-03)
- [x] Viewport display modes in a Display menu, in every mode and remembered between launches: Lit (the clay look), Flat (per-triangle normals from screen-space derivatives), X-Ray (fresnel-weighted, blended, no depth writes) and Normals (view-space normal as colour). A Wireframe overlay draws over any of them and shows polygon edges (quads after quad remesh, no fan diagonals) from `cruth_document_edge_count` / `cruth_document_copy_edges`, cached per topology revision; the surface takes a depth bias under it so the lines do not z-fight

### Measured (CPU reference, 4-core Linux VM, Release)

| Operation | Input | Time |
| --- | --- | --- |
| Voxel remesh, 256 | 98k quads | 1.2 s |
| Voxel remesh, 512 | 98k quads → 1.06M quads | 3.9 s |
| Decimate | 74k → 5k triangles | 0.3 s |
| Print check with self-intersections | 98k quads | 0.4 s |
| Quad remesh to 5k | 197k-triangle head | 4.9 s |
| Quad remesh to 20k | 197k-triangle head | 24 s (was 134 s before the flow change) |
| Unwrap | 1.5k-quad sphere, 8 charts | < 0.01 s |
| Unwrap, many charts | 1,536 charts | 22 ms |
| UV tile utilisation, acceptance head | before → after the new packer | 0.227 → 0.548 |
| UV tile utilisation, sphere level 5 | before → after the new packer | 0.385 → 0.626 |
| Bake normal + height + AO (16 spp) + VDM, 512² | 12k → 98k-quad source | 0.8 s |
| BVH refit + ray after a brush sample, level-8 quad sphere (393k vertices), radius 0.12 | full refit → partial refit | 3.2 ms → 0.05 ms |
| Unique polygon edges for the wireframe, level-8 quad sphere (786k edges), Apple M5 Pro | first extraction, then cached | 5.8 ms |
| Acceptance chain, smoke | 197k-triangle head → 4.7k quads, 2 × 1K tiles | 7 s |
| Acceptance chain, full | 3.1M-triangle head → 19.5k quads, 2 × 4K tiles | 77 s (quad remesh 48 s, bake 16 s) |

The full acceptance run rebuilds the sculpt from the low mesh plus the baked displacement to an RMS error of 7.5e-6 (head about 2 units across), against 1.8e-4 for the bare low mesh: 24 times closer.

PRD target: 512³ in ≤ 1 s on M4 and ≤ 3 s on A14, through the Metal compute path below.

### Known issues

- Voxel remesh can leave a non-manifold edge where two sheets pinch along a single edge in a concave crease thinner than a voxel. Seen once, at resolution 40 on two overlapping cubes. Subdividing such a mesh reports `NonManifold`; remeshing at a different resolution clears it.
- Remesh projection can leave 1–3 self-intersecting triangle pairs at sharp creases at very high resolution.
- Multires detail is stored in object space, so detail does not rotate with large lower-level edits. Tangent-space detail is needed.
- Background Tasks with Live Activities (R-03) are not wired yet.
- Overlay occlusion is a CPU raycast per refresh, so its cost scales with the cage size.
- Quad remesh leaves a few dozen folded quad pairs on sharp crease strokes narrower than one quad (47 at 20k on the test head; none on smooth forms). The aggressive-SAT flip repair is compiled out because it needs an external solver.
- UV packing is box-based, not polygon-aware: charts are packed by their rotated bounding boxes, so concave charts waste space (U-04).
- VDM decode uses a unit TBN, while the normal map uses the exact MikkTSpace inverse.
- The plain CLI `unwrap` output OBJ is still quads with no `vn`.
- Quad-pen dots ignore occlusion.
- A two-finger undo tap during finger drawing can add a tiny guide or contour first.
- Autosave keeps the current level only, so a kill while you are working at a lower level loses the finer detail.
- xcodebuild under Xcode 27.1 with iOS 26.5 simulators logs harmless `invalidDigitCount` and debugger version warnings.
- Face rings on quad-remesh output are helical: a ring started anywhere tends to wind over the whole mesh, so Insert Loop on raw remesh output roughly triples the face count. Guide strokes align the field but do not close the loops around a feature; Loop Around now closes a loop by hand, but the remesher does not. The acceptance chain therefore uses a snapped relax for its hand-retopology step, and the PRD's "retopo the eye loops by hand" waits on loop-closing in the remesher, or on a bounded loop cut between poles (Loop Around covers the closed-loop case by hand).
- The acceptance reference sculpt is about 3M triangles, not the PRD's 5M: one more multires level would be 12M.
- The deflate encoder uses fixed Huffman codes only, so files are larger than zlib's level 6 output. They are valid and readable everywhere.

### Next, in dependency order

1. Hands-on iPad testing of the finger-driven tools (in progress: an iPad is connected).
2. Seam painting in the shell (U-02); curvature and cavity bakes.
3. Tangent-space multires detail and sparse on-disk levels, dyntopo (S-03), Metal compute paths for remesh and bake (K-03).
4. Retopology leftovers: merge, symmetry, n-gon/pole warnings in the overlay (T-03, T-05), Pencil Pro barrel roll for loop count (I-02).

## Phase 1: MVP (TestFlight)

Shell work on top of the phase-0 core: retopo workspace and stroke tools (T-01 to T-03, T-06), 2D UV editor (U-03), layers (L-01), autosave of the finest level and the full project format (L-04; current-level autosave is done), Metal compute paths, Background Tasks with Live Activities (R-03), open project format v0 (`spec/`), and print profiles and export (X-03 to X-05).

## Phases 2 and 3: v1 and post-v1

See the PRD tables; P1 items land in v1 and P2 items after v1.
