# Cruth

A free, open-source iPad sculpting app that takes a model from first blob to a game- or render-ready asset (sculpt, remesh, retopologise, unwrap and bake) on one device, in open formats, with no account and no subscription.

- Product requirements: [docs/PRD.md](docs/PRD.md)
- Competitor research and stack rationale: [docs/Research.md](docs/Research.md)
- What is built and what comes next: [docs/ROADMAP.md](docs/ROADMAP.md)

## Status: phase 0

The PRD's phase 0 is to prove the C++ core headless before the UI grows. This repository currently contains:

| Area | What works today |
| --- | --- |
| Mesh kernel | Indexed polygon mesh; half-edge connectivity that rejects non-manifold input cleanly; BVH with refit for picking, closest-point and overlap queries |
| Multires | Up to 8 Catmull-Clark levels; sculpt at any level, with edits propagated up and down; lockable level 0 (S-01) |
| Voxel remesh | Closed, even quad surface at 8–512 voxels; merges overlapping parts, closes small holes, dissolves buried surfaces; multithreaded (S-02) |
| Decimation | Quadric edge collapse to a target triangle count, boundary-preserving and manifold-safe (S-04) |
| Quad remesh | Field-aligned all-quad remeshing (QuadriFlow, Boost-free with a single-solve min-cost flow), then relax, untangle and project; feeds multires directly (R-01, R-03 input cap) |
| UVs | Automatic unwrap: normal-cone charts inside seam groups, LSCM via conjugate gradient, rotation-optimised packing with padding, UDIM tiles, distortion readout; OBJ `vt` import and export (U-01, U-04, U-06 data) |
| Baking | Normal (tangent and object space), height, vector displacement and AO from a high mesh onto a UV'd low mesh; cage ray casting, dilation padding, multithreaded; 8/16-bit PNG and half/float EXR (ZIP) writers with no dependencies (K-01, K-02) |
| Game asset | One command: decimate the top multires level, unwrap, bake, and write OBJ plus maps |
| Brushes | Standard, Clay, Inflate, Smooth (volume-preserving Taubin), Flatten, Pinch, Crease, Grab and Mask, with falloff curves, pressure curves, dab spacing, X/Y/Z mirror and radial symmetry (B-01, B-02, B-04, N-02, N-03) |
| Undo | Exact per-stroke deltas plus whole-mesh steps for topology changes (L-04, linear for now) |
| Files | OBJ, STL (ASCII and binary) and PLY (ASCII and binary) import and export (P-06, P-07) |
| 3D-print | Check for holes, non-manifold edges and vertices, winding, degenerate faces, self-intersections, shells, volume and size; repair by welding, splitting non-manifold vertices, re-winding, filling holes and turning shells outward (X-01, X-02) |
| C API | `core/include/cruth/cruth.h`: the exception-free boundary every shell uses |
| CLI | `cruth-cli`: create, sculpt from a stroke script (including multires levels), subdivide, voxel and quad remesh, decimate, unwrap, bake, check, repair and convert, all headless |
| iPad shell | SwiftUI and Metal viewport; raw Apple Pencil input with pressure and coalesced touches; finger orbit, pan and zoom; two- and three-finger tap for undo and redo; Pencil double-tap and squeeze; hover brush cursor; multires level stepper; topology menu (subdivide, voxel remesh, quad remesh, decimate) running in the background; Export Game Asset (bakes in the background, saves a folder to Files); Files import and export; print check sheet |

## Repository layout

```
core/            C++20 geometry core (MIT). No platform code.
third_party/     QuadriFlow, LEMON (subset) and Eigen; see THIRD_PARTY_NOTICES.md.
  include/cruth/   Public headers; cruth.h is the C API for the shells.
  src/
tools/cruth-cli/ Headless command-line driver for the core.
tests/           Core unit tests and the CLI end-to-end chain (ctest).
shell-ipad/      SwiftUI + Metal iPad app (XcodeGen project spec).
shell-desktop/   Placeholder for the Dear ImGui desktop shell (PRD decision).
spec/            Open project-format spec and the dependency allow-list.
docs/            PRD, research and roadmap.
```

## Build the core and run the tests

You need CMake 3.21 or newer and a C++20 compiler (Clang 15+, GCC 12+ or Apple Clang from Xcode 15+), on macOS or Linux.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Options: `-DCRUTH_SANITIZE=ON` for ASan and UBSan, and `-DCRUTH_WARNINGS_AS_ERRORS=ON` as CI uses.

### Try the CLI

```sh
./build/tools/cruth-cli/cruth-cli new sphere base.obj --levels 5
./build/tools/cruth-cli/cruth-cli sculpt base.obj tests/data/head_blockout.stroke head.obj
./build/tools/cruth-cli/cruth-cli subdivide head.obj head_smooth.obj
./build/tools/cruth-cli/cruth-cli check head_smooth.obj
./build/tools/cruth-cli/cruth-cli remesh head_smooth.obj head_remeshed.obj --resolution 192
./build/tools/cruth-cli/cruth-cli decimate head_remeshed.obj head_low.obj --target 20000
./build/tools/cruth-cli/cruth-cli quadremesh head_smooth.obj head_quads.obj --faces 5000
./build/tools/cruth-cli/cruth-cli unwrap head_low.obj head_uv.obj
./build/tools/cruth-cli/cruth-cli bake head_uv.obj head_smooth.obj head --maps normal,height,vdm,ao
./build/tools/cruth-cli/cruth-cli convert head_low.obj head.stl
```

Run `cruth-cli` with no arguments for the stroke-script syntax.

## Build the iPad app

The iPad shell needs a Mac with Xcode 26 (iPadOS 26 SDK). The Xcode project is generated from `shell-ipad/project.yml`, so it never conflicts in git.

```sh
brew install xcodegen
cd shell-ipad
xcodegen generate
open Cruth.xcodeproj
```

Select an iPad simulator or a connected iPad, set your signing team under *Signing & Capabilities*, and run. The core's C++ sources are compiled directly into the app target.

**Controls:** draw with Apple Pencil to sculpt. One finger orbits, two fingers pan and pinch to zoom. Tap with two fingers to undo and three fingers to redo. Double-tap the Pencil to toggle invert; squeeze a Pencil Pro to toggle Smooth. Turn on *Finger Sculpting* in the brush panel to sculpt with a finger, for example in the simulator.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions are MIT-licensed and signed off with the [DCO](https://developercertificate.org/) (`git commit -s`); there is no CLA.

## Licence

[MIT](LICENSE).
