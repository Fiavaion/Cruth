# PRD: Cruth — iPad Digital Sculpting App

Oct 15, 2026 · @Mark Jones

## Overview

**Vision:** a free, open-source iPad sculpting app that takes a model from first blob to game- or render-ready asset — sculpt, remesh, retopologise, unwrap and bake — on one device, in open formats, with no account and no subscription. Name: *Cruth*.

**Problem:** Sculptors on iPad have two choices. Nomad Sculpt sculpts well but cannot retopologise or edit UVs, so every model goes to a desktop to be finished. ZBrush for iPad has those tools but its free tier cannot export, the full tier costs €90/yr, rendering is weak and exports crash. Forger's shutdown in 2025 showed that a closed format plus a subscription can strand a user's whole portfolio. Full findings: [Research: iPad Digital Sculpting App](https://claude.ai/code/artifact/df536454-8ce8-43eb-9a56-f8f1a754e890).

**Goals**

1. Match Nomad on sculpting feel, viewport quality and touch UI within v1.
2. Ship manual retopology, a manual UV editor and 32-bit EXR/VDM baking on iPad — which no competitor has — by v1.
3. Store every project in an open, documented, versionable format that Blender and Maya can read.
4. Run on an A14/M1 iPad at 120 Hz Pencil latency; scale to 30M+ triangles on M4/M5.
5. Share one C++ core with the planned Windows Mudbox replacement, so the desktop app is a second shell, not a second codebase.
6. Work with no accounts and no telemetry, and be deployable in schools and studios through MDM.

**Non-goals (v1)**

- Polygon box-modelling (ZModeler / Valence territory).
- Rigging, animation, cloth or physics simulation.
- Cloud sync, accounts, marketplaces or any server-side feature required for core use.
- Photogrammetry capture (possible post-v1 via Object Capture).
- Android, visionOS or web builds (the core permits them; the shells are out of scope).

## Target users

Primary users are iPad sculptors who need to hand over a finished asset — students, indie game artists, 3D-print makers and former Forger users — plus the open-source community that will keep the app alive.

| Persona | Device | Needs | Today's pain |
| --- | --- | --- | --- |
| **Learner** (art/design student, self-taught beginner) | iPad Air M2–M3, Pencil Pro | Learn sculpting fast; export to Blender/Unreal/Unity | Nomad is affordable but every retopo/UV step means a desktop |
| **Indie game / film artist** | iPad Pro M4–M5, Pencil Pro, keyboard | Production low-poly with clean UVs, normal/displacement maps, UDIM for characters | ZBrush iPad subscription or desktop-only pipeline; EXR/UDIM impossible on iPad |
| **3D-print maker** | Any supported iPad | Sculpt, check the mesh is printable, fix it, export STL/3MF | No iPad sculptor checks or repairs meshes for printing |
| **Educator / studio lead** | Mixed fleet | One tool across iPad and desktop; deploy without accounts; portable files | No iPad sculptor has an institutional model or open format |
| **Former Forger user / hobbyist** | Any supported iPad | A maintained, free app that respects their files | Locked out of saving after Forger's EoS |
| **Open-source contributor** | macOS/Linux dev box | Headless-testable C++ core, clear architecture, permissive licence | — |

Minimum supported device is A14/M1 with iPadOS 26; the target experience is defined on an M4 iPad Pro 13" with Pencil Pro.

## Competitive positioning

Position: **"the free iPad sculpting app that finishes the model."** Nomad owns "sculpt beautifully for €23"; ZBrush owns "the industry tool, rented"; this app owns "from sculpt to export-ready asset, free and open."

| Dimension | Nomad Sculpt | ZBrush for iPad | This app |
| --- | --- | --- | --- |
| Price | €22.99 once | Free (no export) / €90/yr | Free, open source (MIT/Apache) |
| Sculpt feel and viewport | Benchmark | Weak render | Match Nomad |
| Retopo | None | Manual + ZRemesher (paid) | Pencil flow-stroke retopo + auto |
| UV | Auto only | Auto only on iPad | Auto + full editor + UDIM |
| Baking | PNG | Maps, no EXR | 16/32-bit EXR, VDM, AO/curvature |
| Project format | .nom (closed) | ZTL (closed) | Open, documented, USD-based |
| Desktop twin | Nomad desktop (paid separately) | ZBrush desktop (€399/yr) | Same core, free Windows/macOS shell |

Adopt from competitors (table-stakes, not differentiators): Nomad's floating/mirrorable toolbars, gesture undo, layer blend modes and Blender Live Link; ZBrush's modifier wheel, importable hotkeys and Quick Search. Avoid: ZBrush's desktop-derived menu depth; Nomad's buried subdivision controls.

The full strengths/weaknesses analysis, feature matrix and library licensing are in the [research doc](https://claude.ai/code/artifact/df536454-8ce8-43eb-9a56-f8f1a754e890).

## Core sculpting requirements

Priority: P0 = MVP, P1 = v1, P2 = post-v1.

**Mesh system**

| ID | Requirement | Priority |
| --- | --- | --- |
| S-01 | Half-edge mesh kernel with sparse multires (Catmull-Clark via OpenSubdiv), up to 8 levels, lockable level 0, reverse-subdivide, per-level undo | P0 |
| S-02 | Voxel remesh (sparse SDF + marching cubes in Metal compute) with resolution 32–2048, closes holes, resolves self-intersections, preserves layers by reprojection | P0 |
| S-03 | Dynamic topology (triangle collapse/split under brush; detail by screen, radius or constant; UV-preserving option) | P1 |
| S-04 | Decimation to target count with mask/UV preservation | P0 |
| S-05 | Boolean union/subtract/intersect on closed meshes with live preview | P1 |
| S-06 | Scene of multiple objects with instancing, parenting, pivots, snapping | P0 |
| S-07 | 30M triangles interactive on M4 (≥60 Hz), 10M on A14/M1; scene cap set by available memory | P0 |

**Brushes**

| ID | Requirement | Priority |
| --- | --- | --- |
| B-01 | Core set: clay, standard, move, grab, flatten, smooth, pinch, inflate, crease, trim, snake hook, polish, elastic, layer, mask, paint — all with pressure, tilt and roll mapping | P0 |
| B-02 | Brush curves (falloff, pressure→size/intensity) editable on-canvas; Mudbox-style stamp + stencil projection | P0 |
| B-03 | Alphas and VDM brushes (create from surface, import EXR) | P1 |
| B-04 | Stroke modes: dots, drag-rect, lazy mouse/stabiliser, curve, spacing, stroke-to-editable-curve after lift | P0 |
| B-05 | Tubes/primitives with IK and lattice deform; stroke editing with hold-after-draw | P1 |
| B-06 | Custom brush save/share as open JSON + alpha files | P0 |

**Navigation and symmetry**

| ID | Requirement | Priority |
| --- | --- | --- |
| N-01 | Mudbox-style navigation: one finger orbits, two fingers pan/zoom, Alt+mouse mapping on desktop/trackpad; focus-on-tap; 120 Hz | P0 |
| N-02 | Symmetry on X/Y/Z, radial symmetry, local pivot symmetry, mirror-and-weld | P0 |
| N-03 | Masking: paint, lasso, cavity, grow/shrink/blur, mask-to-face-group, transpose-with-mask | P0 |
| N-04 | Face groups from mask/UV/normal; group visibility and isolate | P0 |

**Layers**

| ID | Requirement | Priority |
| --- | --- | --- |
| L-01 | Sculpt + paint layers in one stack; per-channel (offset, colour, roughness, metalness, opacity) blend modes and intensity | P0 |
| L-02 | Layers survive multires change, voxel remesh and decimation by reprojection | P1 |
| L-03 | Layer export as glTF morph targets / USD blendShapes | P1 |
| L-04 | Unlimited undo with branch history per object; crash-safe autosave every 30 s | P0 |

## Retopology, remeshing and UV requirements

These three are the reason the app exists; they ship in the MVP, not after it.

**Auto remeshing**

| ID | Requirement | Priority |
| --- | --- | --- |
| R-01 | Quad remesh with target count or edge length, adaptive density, symmetry, hard-edge detection, boundary alignment — Instant Meshes (interactive preview) + QuadriFlow (final), both BSD/MIT | P0 |
| R-02 | Guide strokes: user-drawn flow curves and face-group borders bias the cross-field before extraction | P0 |
| R-03 | Runs as a cancellable Background Task with Live Activity progress; hard input cap with automatic decimation above it | P0 |
| R-04 | Result arrives as a new object with the high-res kept; reproject sculpt detail and layers onto it | P0 |
| R-05 | Learned cross-field (CrossGen-style Core ML model) as an optional download to improve flow | P2 |

**Manual retopology** (Pencil-native; interaction detail in the input section)

| ID | Requirement | Priority |
| --- | --- | --- |
| T-01 | Retopo workspace: high-res reference in x-ray with surface snapping; low-poly overlay editable in place | P0 |
| T-02 | Stroke tools: Contour (stroke across a form → edge loop, roll to set count), Strip (stroke → quad strip), Bridge (two strokes → quads between), Quad-pen (tap points, lift to fill) | P0 |
| T-03 | Edit tools: tweak, relax (surface-snapped), slide loop, insert loop, delete, merge, flip, bridge edges | P0 |
| T-04 | Persistent guide strokes drawn on the reference (TopoGun-style) reusable by auto remesh (R-02) | P1 |
| T-05 | Symmetry-aware; live poly count, n-gon/tri/pole warnings; one-tap "auto-fill selected region" using Instant Meshes locally | P1 |
| T-06 | Project and subdivide: new low-poly becomes level 0 of a multires stack with high-res detail reprojected | P0 |

**UV**

| ID | Requirement | Priority |
| --- | --- | --- |
| U-01 | Auto unwrap: BFF (via geometry-central, MIT) with face-group or painted seams; xatlas charting + packing as fallback | P0 |
| U-02 | Seam painting on the 3D model with Pencil; seam suggestion from curvature/part heuristics, later from an on-device segmentation model | P0 / P2 |
| U-03 | 2D UV editor: select islands/edges/verts, move/rotate/scale, pin, relax (ARAP), straighten, align, stitch, split, mirror | P0 |
| U-04 | Packing: rotation, margin, UDIM tile assignment, group-by-material; texel-density readout and equalise | P1 |
| U-05 | Live unwrap: edits on the 3D seam or pins re-solve the layout in real time | P1 |
| U-06 | Distortion and texel-density heat maps in 2D and 3D | P1 |
| U-07 | Multiple UV sets per object; UVs preserved through dyntopo and decimation | P1 |

**Baking**

| ID | Requirement | Priority |
| --- | --- | --- |
| K-01 | Bake from multires (self) or from any high-res object to low-poly: tangent/object normal, height, vector displacement, AO, curvature, cavity, position, colour and PBR channels | P0 |
| K-02 | Output PNG 8/16-bit and EXR 16/32-bit float (tinyexr), 512–8192 px, UDIM tile sets, cage/ray-distance controls, anti-aliasing | P0 |
| K-03 | Metal compute baker with BVH; runs in background with cancel | P0 |
| K-04 | Presets per target: Blender, Unreal, Unity, Substance, Procreate (Nomad has the last; match it) | P1 |

Acceptance test for the whole chain: sculpt a 5M-triangle head on an M4, remesh to 20k quads with two guide strokes, retopo the eye loops by hand, auto-unwrap with painted seams, pack to 2 UDIM tiles, bake 4K normal + 32-bit displacement to EXR, open in Blender with correct displacement — under 10 minutes wall-clock with no desktop involved.

## Novel differentiating features

Six features no competitor ships as of October 2026, each tied to a requirement above so it is built into the architecture rather than bolted on.

1. **Flow-stroke retopology (R-02, T-02).** Draw strokes where edge loops must run; the app fits a cross-field to them and fills the rest with quads. Correcting a region is another stroke, not a manual rebuild. Pencil Pro squeeze toggles create/tweak, barrel roll sets loop count, hover previews the loop before contact.
2. **Finishing graph (R-04, T-06, U-05, K-01).** Remesh → retopo → UV → bake is stored as a non-destructive chain on the object. Keep sculpting the high-res and tap *Re-run* to regenerate the low-poly, UVs and maps; manual retopo edits are stored as deltas and re-applied where topology still matches.
3. **Pencil UV editor with UDIM and texel density (U-03 to U-06).** The first manual UV editor on iPad, designed for a stylus: pinch to scale islands, two-finger rotate, Pencil to pin and drag, hover to highlight the matching 3D region.
4. **AI seam and flow assist (U-02, R-05).** Starts as curvature/part heuristics; v2 swaps in a Core ML segmentation model (Segment-Any-Mesh style) for artist-like seam proposals and a CrossGen-style learned cross-field, run through Metal 4's ML encoder. Always optional, always on-device, models downloaded separately to stay under the 4 GB cap.
5. **Open project format.** A documented folder: USD scene, multires levels as sparse delta files, layers as per-level offsets, brushes and materials as JSON, maps as EXR/PNG. Git-friendly, readable by Blender via the USD importer, and published as a spec so Forger-style lock-in can never happen.
6. **Squeeze-and-hover tool wheel.** Squeeze opens a radial wheel at the Pencil's hover pose; roll scrolls options; release selects. Modifier-free retopo and sculpting for people who never want to tap the screen with a finger.

Also planned, lower novelty: classroom mode (MDM config, shared NAS library, preset packs, no accounts); open-protocol Live Link to Blender and to the desktop twin; MetalFX-denoised path-traced look-dev (M-series); accessibility layouts (one-handed, large-target, voice tool switching).

## Painting, materials, rendering and export

| ID | Requirement | Priority |
| --- | --- | --- |
| P-01 | Vertex painting of colour, roughness, metalness, emissive, opacity with pressure; fill, gradient, shape and stencil tools | P0 |
| P-02 | Texture painting on UV-mapped meshes once UVs exist (U-01), 4K–8K, per-channel layers; projection (Spotlight-style) painting from reference images | P1 |
| P-03 | PBR materials with matcap fallback; material library in open JSON; per-object and per-face-group assignment | P0 |
| P-04 | Real-time viewport: PBR + IBL (Poly Haven HDRIs bundled), SSAO, DoF, bloom, fog, transparent, wireframe/cavity/flat overlays; MetalFX temporal upscaling to hold 120 Hz on 13" panel | P0 |
| P-05 | Path-traced look-dev preview with MetalFX denoiser on M-series; turntable image/video export | P2 |
| P-06 | Import: OBJ, STL, PLY, glTF/GLB, USD/USDZ, FBX (via open parser), images PNG/JPG/EXR/PSD alphas, Nomad .nom via converter if feasible | P0 (P1 for FBX/.nom) |
| P-07 | Export: glTF/GLB, USD/USDZ, OBJ, STL, PLY with presets for Blender, Unreal, Unity, Procreate, Substance; layers as morph targets; maps as EXR/PNG; 3MF for printing | P0 |
| P-08 | Live Link: two-way sync with Blender (free MIT add-on) and the desktop twin over LAN; file-based GoZ-compatible bridge for ZBrush | P1 |
| P-09 | Files app, external drives, NAS/SMB project folders; no proprietary container | P0 |

**3D-print check and repair**

| ID | Requirement | Priority |
| --- | --- | --- |
| X-01 | Printability report: watertight/manifold, non-manifold edges and verts, self-intersections, flipped normals, degenerate faces, disconnected shells, minimum wall thickness and feature size against a chosen printer profile, overhang and unsupported-area estimate, physical dimensions and volume in the chosen units | P0 |
| X-02 | One-tap repair: close holes, remove internal shells, unify normals, resolve self-intersections (voxel-based rebuild as last resort), thicken walls below threshold, split or merge shells; each fix previewed and undoable | P0 |
| X-03 | Printer profiles bundled for common FDM and resin classes (nozzle/min-wall, build volume, resin min feature) and editable by the user; hollowing with drain holes for resin | P1 |
| X-04 | Export STL (binary, units embedded in filename), 3MF with units, colour and multi-object, OBJ; orientation and scale set at export; checked file re-validated before writing | P0 |
| X-05 | Scale/units: metric and imperial, set real-world size by dimension, print-bed preview against profile | P0 |

## Input, UI and Apple Pencil interaction

Principle: **Pencil makes geometry, fingers move the camera, modifiers live on the Pencil.** Every tool must be usable with Pencil alone, and every tool must also work with finger-only and with keyboard + trackpad on iPad and desktop.

| ID | Requirement | Priority |
| --- | --- | --- |
| I-01 | Raw `UITouch` input path with force, altitude, azimuth, `rollAngle`, coalesced and predicted touches; ≤ 1 frame added latency at 120 Hz; no PencilKit | P0 |
| I-02 | Pencil Pro: squeeze = momentary modifier / tool wheel, double-tap = user-assignable, barrel roll mapped per tool (brush rotation, loop count, strip width); honour the user's system-level preferred actions | P0 |
| I-03 | Hover (M2+): brush-size/shape ghost, snap-target highlight in retopo, UV island highlight; `prefersHoverToolPreview` respected | P0 |
| I-04 | Gestures: 1-finger orbit, 2-finger pan/zoom/rotate, 2-finger tap undo, 3-finger tap redo, 3-finger drag for radius/intensity (all remappable); trackpad equivalents | P0 |
| I-05 | Keyboard shortcuts with on-screen cheat sheet on Cmd-hold; import of Nomad and ZBrush hotkey schemes | P1 |
| I-06 | UI: floating, dockable, mirrorable toolbars; left/right-hand layouts; compact mode for 11"; iPadOS 26 menu bar and multi-window (retopo/UV editor as a second window, Stage Manager and external display) | P0 |
| I-07 | Radial tool wheel at hover pose on squeeze; recent-tools strip; search-anywhere (Cmd-K / tap-and-type) | P0 |
| I-08 | Reference images: pinned image planes, see-through overlay, picture-in-picture reference window | P1 |
| I-09 | Accessibility: VoiceOver on all controls, Dynamic Type, reduce-motion, one-handed and large-target layouts, voice tool switching via Siri shortcuts | P1 |
| I-10 | Onboarding: three five-minute in-app lessons (sculpt, remesh+retopo, UV+bake) built from the acceptance-test asset | P1 |

## Technical architecture and stack

The app is a C++ core with a Swift shell, chosen so the desktop Mudbox replacement is a second shell on the same core. Rationale and alternatives (Rust/wgpu, Dawn, Qt, web) are in the research doc.

&#91;embedded content: architecture · 4 layers\]

Shells hold only input, windowing and file access; the core owns every algorithm and is built and tested headless, so Claude Code in VS Code can drive it without an iPad attached.

**Decisions**

| Area | Decision |
| --- | --- |
| Core language | C++20, CMake, exception-free at the Swift boundary; Swift↔C++ interop (Swift 5.9+) for the shell |
| GPU | Metal 3-level API for v1 behind a renderer interface; Metal 4 argument tables and ML encoder adopted incrementally; MetalFX temporal upscaling |
| Input | Raw UITouch + UIPencilInteraction + UIHoverGestureRecognizer; no PencilKit |
| Subdivision | OpenSubdiv (Metal backend, iOS build recipe) |
| Voxel remesh | Bespoke sparse SDF + marching cubes in Metal compute (OpenVDB kept as a desktop option) |
| Quad remesh | Instant Meshes (preview) + QuadriFlow with `BUILD_FREE_LICENSE` (final); own cross-field-from-strokes step |
| UV | geometry-central BFF, libigl ARAP/LSCM, xatlas packing; own SLIM, texel density, UDIM |
| Baking | Metal compute ray-cast baker; tinyexr output |
| Scene / format | OpenUSD scene, sparse multires deltas, JSON brushes/materials; spec published in-repo |
| Long jobs | iPadOS Background Tasks + Live Activities; every job cancellable and memory-capped |
| Licence | Core and shells MIT; contributors sign off with the DCO (no CLA); no GPL dependency anywhere |
| Repo | Public GitHub mono-repo: `core/`, `shell-ipad/`, `shell-desktop/`, `spec/`, `tests/`; CI builds core on macOS/Linux and runs the acceptance asset through the full chain |

## Performance and device requirements

| Metric | A14 / M1 (floor) | M4 / M5 13" (target) |
| --- | --- | --- |
| Minimum OS | iPadOS 26 | iPadOS 26 |
| Interactive triangle budget at ≥ 60 Hz | 10M | 30M (≥ 90 Hz at 15M) |
| Pencil stroke latency (input to pixel) | ≤ 16 ms | ≤ 9 ms at 120 Hz with predicted touches |
| Voxel remesh 512³ | ≤ 3 s | ≤ 1 s |
| Quad remesh 2M → 20k (QuadriFlow, background) | ≤ 90 s | ≤ 30 s |
| BFF unwrap 500k tris | ≤ 5 s | ≤ 2 s |
| Bake 4K normal + 32-bit height from 5M-tri source | ≤ 60 s | ≤ 20 s |
| Peak memory | ≤ 3 GB (jetsam-safe on 8 GB) | ≤ 12 GB with increased-memory entitlement |
| App download size | ≤ 150 MB; ML models and HDRIs as optional downloads | same |
| Autosave | every 30 s, incremental, ≤ 200 ms stall | same |
| Cold launch to blank canvas | ≤ 2 s | ≤ 1 s |

All long jobs run through the Background Tasks API with a Live Activity and a cancel button; the UI never blocks. Memory pressure triggers automatic eviction of unused multires levels to disk before the OS warning fires. Hover and squeeze degrade gracefully on Pencil 2 and USB-C Pencil (no pressure).

## Roadmap

Four phases, each gated by a measurable exit criterion rather than a date; the MVP already contains the three features no competitor has.

&#91;embedded content: roadmap · 4 phases, 3 gates\]

Phase 0 proves the core on a headless CLI before any UI exists; the MVP gate is the acceptance chain in the retopo/UV section; v1 ships once external TestFlight users have run the full chain on their own models. Durations are an open question until phase 0 is scoped with Claude Code.

## Success metrics, risks and decisions

**Success metrics (12 months after v1)**

- 1,000 TestFlight users during MVP; 50% of them complete sculpt → retopo → UV → bake on their own model without a desktop step.
- Acceptance chain (sculpt → bake) under 10 minutes on M4 and under 25 minutes on M1, measured in CI on the reference asset.
- App Store rating ≥ 4.5 with ≥ 500 ratings; crash-free sessions ≥ 99.5%.
- At least 10 external contributors with merged PRs; project format read by Blender's USD importer with no custom add-on.
- 3D-print check passes on 100% of exports that report "printable" when sliced in PrusaSlicer, Bambu Studio and Cura.
- Zero accounts, zero telemetry, zero subscription — verified in every release.

**Risks**

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Quad remesh quality below ZRemesher (Instant Meshes/QuadriFlow are weaker on edge flow) | Students still go to desktop for retopo | Guide strokes + manual retopo cover the gap; learned cross-field in post-v1; keep an Exoside licence conversation open |
| QuadriFlow memory/time on dense meshes | Crashes on 8 GB iPads | Hard input cap with auto-decimation; background task with cancel; memory polling |
| Single-maintainer bus factor (the same risk Nomad carries) | Project stalls | Public repo, MIT licence, documented architecture, a second maintainer recruited before v1 |
| Swift↔C++ interop edge cases (exceptions, templates) | Shell bugs | Exception-free C-style boundary API; interop limited to a thin façade |
| OpenVDB on iOS proves heavy | Voxel remesh slips | Bespoke Metal SDF remesher is the plan; OpenVDB desktop-only |
| Apple review of an open-source app with downloadable ML models | Launch delay | Models as on-demand resources, no code download, no dynamic execution |
| Scope: MVP is large | Nothing ships | Phase 0 gate on the headless core; MVP feature set frozen to the retopo/UV section tables |
| GPL contamination via a transitive dependency | Cannot ship | Licence scan in CI; dependency allow-list in `spec/` |

**Decisions taken**

| Question | Decision | Why |
| --- | --- | --- |
| Name | **Cruth** | Chosen 15 Oct 2026 |
| Licence and contributor terms | **MIT core and shells; DCO sign-off, no CLA** | MIT is the simplest permissive licence and matches the dependencies; a DCO (`Signed-off-by`) keeps contributions low-friction with a clear provenance trail, which a CLA adds paperwork to without adding protection for an MIT project |
| Desktop shell UI | **Dear ImGui (MIT) on the shared renderer, with the tool/command model in the core** | ImGui gives docking, multi-viewport and Metal/Vulkan/DX12 backends out of the box, is the standard for GPU tooling, and keeps the shell under \~10% of the code; because every tool is a core command, a native Win32/AppKit or SwiftUI shell can replace it later without touching the core |
| Exoside Quad Remesher | **Stay fully open; expose a remesher plug-in interface** | The MIT/BSD remeshers plus flow strokes cover the MVP; a documented plug-in API (`IRemesher`) lets anyone add a commercial engine on desktop without tainting the core; revisit if Exoside offers OEM terms |
| Institutional pilot | **None required** | The product is general-purpose; pilots are a distribution tactic, not a requirement |
| Nomad .nom import | **No converter; USD is the native interchange, glTF and OBJ for everything else** | .nom is undocumented and single-vendor; USD is read by Blender, Maya, Houdini, Unreal, Unity, Procreate, ZBrush and Nomad itself, and a USD preset per target app (as Nomad does) is cheaper and more reliable than reverse engineering |
| 3D-print readiness | **In scope for v1** | See the print check and repair requirements under Painting, materials, rendering and export |
| Phase durations | **Set after phase 0 is scoped in Claude Code** | — |
