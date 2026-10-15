# Research: Cruth — iPad Digital Sculpting Competitor Landscape & Opportunities

Oct 15, 2026 · @Mark Jones

## Executive summary

No iPad app offers a manual UV editor, UDIM output or 32-bit EXR displacement baking, and only ZBrush (subscription, €90/yr) offers manual retopology. That is the gap: a free, open-source sculpting app that treats the Mudbox-style texture pipeline (retopo → UV → bake) as a first-class touch workflow rather than a desktop afterthought.

The iPad market has consolidated to two serious sculptors. [Nomad Sculpt](https://apps.apple.com/ie/app/nomad-sculpt/id1519508653) (€22.99 one-time, solo developer) wins on touch-native UI, real-time PBR rendering and price, but has no manual retopo or UV editing and its developer has said a ZRemesher-class tool is not planned. [ZBrush for iPad](https://www.maxon.net/zbrush-plan-options) (Maxon) has the full industry toolset incl. ZRemesher, Retopo Brush and UV Master, but its free tier cannot export at all, rendering is weak, exports crash, and the new UV Editor is desktop-only. [Forger](https://www.cgchannel.com/2025/07/maxon-discontinues-forger/) was killed in September 2025, leaving a user base resentful of subscription lock-in. Blender's iPad port is on hold.

Recommended positioning: **"the free iPad sculpting app that finishes the model"** — sculpt, remesh, retopo, unwrap and bake to production-ready, open formats (glTF/USD/EXR) on one device, with no account and no subscription. Novel features (section 5) centre on Pencil-native retopology, AI-assisted seams and flow, and a non-destructive pipeline graph that no competitor has.

Stack recommendation (section 7): a portable C++ geometry core with a Swift/UIKit/Metal shell. Strategy recommendation (section 8): build the core once, iPad-first, and let the Windows Mudbox-replacement be the second shell on the same core.

## Competitor profiles

Two apps matter; the rest define the edges of the market. Versions and prices are as of 15 Oct 2026.

### Nomad Sculpt 2.12 (Hexanomad / Stéphane Ginier)

€22.99 one-time on iPad; [Quad Remesher](https://nomadsculpt.com/manual/topology) IAP €17.99; separate \~$35 desktop licence with classroom volume licensing; 4.8★ from \~14K ratings. One developer, very fast cadence (2.9–2.12 shipped in one week in Oct 2026).

**Strengths**

- Touch-first, fully customisable UI (floating toolbars, mirrored layouts, every key/stylus button/gamepad/SpaceMouse bindable); Pencil hover, barrel roll, two/three-finger undo/redo.
- Real-time PBR viewport with IBL, SSAO, DoF, bloom, fog — reviewers call this its decisive edge over ZBrush.
- Combined sculpt + paint layers with 25 blend modes that survive voxel remesh and multires; exported as glTF morph targets.
- Mesh toolset: voxel remesh, dynamic topology (now UV-preserving), multires, decimation, Instant Meshes quad remesh, new Autoremesher, VDM brushes, lattice, stroke-to-curve editing, tubes with IK.
- Baking: normal, PBR channels, colour, and (2.11) displacement from the subdivided base; 512–4096 px.
- Open formats: glTF/GLB, OBJ, STL, PLY, USD (presets for Blender/Procreate/ZBrush), experimental FBX; [two-way Live Link to Blender](https://80.lv/articles/nomad-sculpt-adds-direct-app-linking-to-blender-zbrush) and GoZ.

**Weaknesses and complaints** ([forum](https://forum.nomadsculpt.com/t/features-to-bring-it-closer-to-industry-standard/23346))

- No manual retopology (QuadDraw-style tool is the top request); developer says it is not planned.
- Auto UV only (UVAtlas/BFF, face groups as seams); no UV editor, no UDIM, no 32-bit EXR displacement.
- No polymodelling, no morph brush, thin hard-surface and deformer set; subdivision switching buried in menus.
- Pencil Pro squeeze unconfirmed; OBJ winding quirks; quad remesher costs extra; single-developer bus factor.

### ZBrush for iPad 2027.0 (Maxon)

Free tier + $9.99/mo or $89.99/yr; included with desktop ZBrush ($399/yr); 4.1★ from \~249 ratings; iPadOS 17+, A12+. Released 14 Oct 2026 alongside desktop 2027 ([release notes](https://www.zbrushcentral.com/t/zbrush-2027-has-arrived/486939)).

**Strengths**

- Industry toolset: 200+ brushes, DynaMesh, Sculptris Pro, multires + dynamic subdiv, ZRemesher, Decimation Master, Live Boolean, ZSpheres, ZModeler, Retopo Brush (2026.1), UV Master, displacement/texture map creation, Spotlight projection painting and 3D Layers (both new in 2027.0).
- Pipeline fidelity: ZTL/ZPR round-trip with desktop, GoZ to ZBrush/C4D, USD/OBJ/STL/3MF export.
- iPad-only extras: photogrammetry capture, Global Quick Search, modifier wheel, importable desktop hotkeys, Pencil hover tooltips, squeeze/double-tap assignable.

**Weaknesses and complaints** ([App Store reviews](https://apps.apple.com/us/app/zbrush-made-for-3d-creators/id6612028716?see-all=reviews&platform=ipad), [Creative Bloq review](https://tech.yahoo.com/computing/articles/zbrush-ipad-review-powerful-feature-160000169.html))

- Free tier cannot export 3D at all, has 28 brushes, DynaMesh ≤1024, no layers/UVs/maps — widely called pointless.
- No real-time PBR rendering; BPR basic only, no passes, no SSS.
- Crashes on Booleans, ZRemesher, turntable export and save/import (Maxon's own 2026.2 notes acknowledge export crashes); GoZ unreliable.
- Still missing from desktop: UV Editor, Selection Sets, plug-ins/scripting, HD Geometry, MicroPoly, FBX/PLY/ABC export.
- Cluttered desktop-derived menus; steep learning curve for Nomad users; no external-storage assets.

### Forger (Maxon) — discontinued

Created 2011, acquired by Maxon 2021, placed in [Limited Maintenance Mode](https://www.maxon.net/en/forger-eos-faq) 10 Sep 2025: removed from the App Store, no renewals, lapsed users locked out of save/export. Native format is unreadable by ZBrush or C4D; migration is OBJ/GoZ with layers lost; no migration discount (≈6× price rise). It had voxel remesh, polygon tools, non-destructive layers and PBR painting — features ZBrush for iPad only matched this month. Its orphaned users are the most receptive audience for a free, open-format alternative.

### Adjacent apps

| App | Version / price | What it is | Relevance |
| --- | --- | --- | --- |
| [Shapr3D](https://www.shapr3d.com/pricing) | 26.180 · free (2 projects) / $299/yr · **free EDU licence** | Parasolid NURBS CAD; Pencil-first sketching; STEP/X\_T/STL/OBJ | Proves free-education licensing and Pencil-first UX work on iPad; no sculpt/UV |
| [Procreate](https://help.procreate.com/articles/Z0mATv-3d-painting) | 5.4.14 · $12.99 | Paints colour/roughness/metallic on UV-mapped USDZ/OBJ; Lighting Studio; AR view | Needs UVs it cannot make — a UV-capable sculptor is its natural upstream; Nomad already ships a Procreate USD preset |
| [Valence 3D](https://valence3d.app/) | 1.4.7 · $19.99 | Poly/SubD modeller: extrude, bevel, knife, booleans, path tracer | Explicitly never adding UVs; a hard-surface companion, not a sculptor |
| [Sculptura 2](https://apps.apple.com/app/id6463495541) | 2.7 · $14.99 · M1+ | Voxel sculpting, PBR voxel paint, path tracing; auto-UV only on USDZ export | Small base (17 ratings); shows voxel-native sculpting is viable on M-series |
| uMake | 3.54 · subscription | NURBS concept sketching, AI image-to-3D | Not a sculptor |
| Putty 3D | last update 2019 | Abandoned | — |
| Re:Form | alpha 0.7, WebGPU in Safari | Browser sculptor, free ≤12.5M tris | Early, but a no-install cross-device model |
| Blender | — | iPad port [on hold](https://www.macrumors.com/2026/02/27/blender-ipad-pro-app-development-halted/) since Feb 2026 | No free incumbent is coming |

## Feature matrix

The last column is the proposed app's target so the gaps read at a glance. Sources: [Nomad manual](https://nomadsculpt.com/manual/topology), [Maxon plan matrix](https://www.maxon.net/zbrush-plan-options), [ZBrush iPad what's new](https://help.maxon.net/zbp/en-us/Content/html/0_ZBrush-ipad-whats-new.html).

| Capability | Nomad 2.12 | ZBrush iPad 2027 (sub) | ZBrush iPad free | Sculptura 2.7 | Procreate 3D | **Proposed app** |
| --- | --- | --- | --- | --- | --- | --- |
| Price | €22.99 once | $89.99/yr | Free, no export | $14.99 once | $12.99 once | **Free, open source** |
| Voxel remesh | Yes | DynaMesh | ≤1024 | Native voxel | — | Yes |
| Dynamic topology | Yes (tri) | Sculptris Pro | Yes | — | — | Yes |
| Multires / subdiv | Yes | Yes + dynamic | Basic | — | — | Yes (OpenSubdiv) |
| Auto quad remesh | Instant Meshes; paid QR | ZRemesher | Basic | — | — | Instant Meshes + QuadriFlow + AI flow |
| Manual retopo | **No** | Retopo Brush, ZModeler | No | — | — | **Pencil stroke retopo** |
| Auto UV | Yes | UV Master | No | Export only | No | Yes (BFF / xatlas) |
| Manual UV editor | **No** | **No (desktop only)** | No | No | No | **Yes** |
| UDIM / texel density | No | No on iPad | No | No | No | **Yes** |
| Bake normal / disp | 8/16-bit PNG | Yes | No | Colour only | — | **16/32-bit EXR, VDM** |
| Sculpt + paint layers | Yes | New 2027.0 | No | Yes | Per-map | Yes |
| PBR painting | Vertex + textures | Polypaint basic | Polypaint | Voxel PBR | Texture maps | Vertex + texture |
| Real-time render | PBR + post FX | BPR basic | Same | Path tracer | Lighting Studio | PBR + MetalFX |
| Export | glTF, OBJ, STL, PLY, USD, FBX | OBJ, USD, STL, GoZ, 3MF | None | USDZ, OBJ, STL | OBJ, images | glTF, USD, OBJ, STL, EXR |
| Desktop bridge | Blender Live Link, GoZ | GoZ, ZTL | None | Files | Files | Open file format + Blender link |
| Pencil hover / squeeze | Hover yes; squeeze unconfirmed | Both | Both | ? | Both | Both + barrel roll |
| Developer | 1 person | Maxon | Maxon | 1 person | Savage | Open source (Cruth) |

## Gaps nobody fills

The unfilled needs cluster around finishing a model for a pipeline, which both leaders treat as desktop work.

1. **Manual retopology on iPad** — only ZBrush has it, behind a subscription; Nomad's forum has asked since [2020](https://forum.nomadsculpt.com/t/feature-request-remesh-retopo/59) and the developer has declined.
2. **Manual UV editing** — none. Nomad is auto-only; ZBrush's new UV Editor (UDIM, texel density) is desktop-only in 2027.
3. **Production baking** — no 32-bit EXR, no vector displacement output, no UDIM tiles on any iPad app. Nomad users list EXR displacement and UDIM as the two things keeping it from "industry standard".
4. **A free app that exports** — ZBrush's free tier has no 3D export; Forger's lapsed users cannot save. Students who cannot pay are locked out of the only full-toolset option.
5. **Open formats and data sovereignty** — Forger's proprietary format stranded its users; ZTL is closed. No iPad sculptor stores projects in an open, documented, git-friendly format.
6. **Real-time rendering in a full-toolset app** — ZBrush has the tools but not the viewport; Nomad has the viewport but not the tools.
7. **Reliability** — ZBrush iPad crashes on Booleans, ZRemesher and export per Maxon's own notes; a smaller, stable toolset would be preferred by many reviewers.
8. **Classroom deployment** — no iPad sculptor offers MDM-friendly config, shared asset libraries or a lab licence model beyond Nomad desktop's volume keys.
9. **Pencil Pro depth** — squeeze, barrel roll and hover are partially used (ZBrush assigns squeeze; Nomad uses roll for drag brushes) but no app builds a tool around them.
10. **Non-destructive workflow** — layers exist, but no app records the remesh → retopo → UV → bake chain as something re-runnable after further sculpting.

## Novel feature opportunities

None of these ship in Nomad, ZBrush for iPad, Forger, Sculptura or Valence as of October 2026. Ranked by feasibility × distinctiveness.

| # | Feature | What it does | Why nobody has it | Feasibility |
| --- | --- | --- | --- | --- |
| 1 | **Flow-stroke retopology** | Draw a few Pencil strokes along the forms you care about (eye loops, mouth, limbs); a cross-field is fitted to them and Instant Meshes/QuadriFlow fills the rest. Stroke again to correct. Pencil Pro squeeze = create vs tweak, barrel roll = loop count. | ZBrush requires full manual Topology brush or guide curves plus ZRemesher; Nomad has no retopo. RetopoFlow's stroke tools are desktop-only and GPL. | High — BSD/MIT extractors exist; the stroke-to-field step is the only new code |
| 2 | **AI seam suggestion** | Part-segment the mesh (Segment-Any-Mesh style, Core ML on device) and propose artist-like seams at part boundaries; user accepts, nudges or paints over; BFF/xatlas unwrap. | Auto-UV in Nomad/ZBrush uses geometric heuristics; research ([PartUV](https://arxiv.org/abs/2511.16659), [ArtUV](https://arxiv.org/abs/2509.20710)) is 2025–26 and unshipped | Medium — needs a distilled segmentation model; fallback to curvature-based seams |
| 3 | **Pencil UV editor** | A proper 2D UV editor on iPad: pinch islands, relax, straighten, pin, UDIM tiles, texel-density heat map; live-unwrap updates while sculpting. | Only desktop ZBrush 2027 has one; the iPad gap is absolute | High — xatlas pack + libigl ARAP/LSCM + own SLIM |
| 4 | **Re-runnable finishing graph** | Remesh → retopo → UV → bake stored as a non-destructive node chain; keep sculpting the high-res and press "re-run" to regenerate the game-ready low-poly and maps. | Layers exist, but no app persists the pipeline | Medium — architecture decision, not an algorithm |
| 5 | **32-bit EXR and VDM baking on device** | Normal, height, vector displacement, AO, curvature, cavity to 16/32-bit EXR via Metal compute; UDIM tile sets. | Nomad is 8/16-bit PNG; ZBrush iPad lacks EXR displacement | High — tinyexr lists iOS; Metal ray-cast baker |
| 6 | **Open, git-friendly project format** | Documented, versionable project folder (USD-based scene + layers as sparse deltas) with a published spec so Blender/Maya can read it. | Forger's closed format stranded users; ZTL is proprietary | High — policy + USD |
| 7 | **Squeeze-and-hover tool wheel** | Pencil Pro squeeze opens a radial modifier wheel at the hover pose; barrel roll rotates through options; release to pick. No screen taps for modifiers. | ZBrush has a wheel but it is tap-driven; Nomad's squeeze support is unconfirmed | High — UIPencilInteraction + UIPencilHoverPose |
| 8 | **Sculpt-to-surface-flow painting** | Paint flow curves on the surface to direct quad flow, hair/fur direction, texture direction and displacement anisotropy from one stroke set (your existing post-MVP idea, now a differentiator). | Nothing on iPad or desktop ties all four to one curve set | Medium |
| 9 | **Classroom mode** | MDM config, read-only shared asset library on NAS, anonymous export, lab-wide brush/material presets; no accounts. | No competitor targets institutions on iPad | High — config files and MDM profiles |
| 10 | **Live Link to Blender and to the desktop twin** | Two-way sync like Nomad's, but open-protocol and also to your own Windows app. | Nomad's link is Nomad-only | Medium |
| 11 | **Metal 4 ML in the viewport** | On-device denoised path-traced preview (MetalFX temporal denoiser, iPadOS 26) for look-dev without leaving the app. | Sculptura/Valence have path tracers but no denoiser; Nomad is rasterised | Medium — M-series only |
| 12 | **Accessibility-first sculpting** | Hover-based tool preview, one-handed layouts, voice-named tool switching, large-target mode. | Untouched by all competitors | High |

Features 1, 3 and 5 deliver the user's stated retopo/remesh/UV requirement in a form no competitor matches; 2 and 4 are the headline novelties; 6 and 9 are policy choices that cost little and protect users from Forger-style lock-in.

## Technical research: algorithms and libraries

Every remesh, UV and baking need can be met with permissive (MIT/BSD/MPL2/Apache) libraries; the only things to avoid are CoMISo, libQEx, CGAL's algorithm packages and the original SLIM repo, all GPL. Exoside's Quad Remesher has no public SDK.

### Auto quad remeshing

| Library | License | App Store | Verdict |
| --- | --- | --- | --- |
| [Instant Meshes](https://github.com/wjakob/instant-meshes) | BSD-3 | Yes | Fastest; weakest global flow; unmaintained but small. Use for interactive preview and quick uniform remesh. Nomad already ships it. |
| [QuadriFlow](https://github.com/hjwdzh/QuadriFlow) | MIT (`BUILD_FREE_LICENSE=ON` swaps LGPL Cholesky for MPL2 LU) | Yes | Blender's and Forger's remesher; \~30 s on 183k faces at 20k target; RAM-hungry; needs manifold input. Run as cancellable Background Task. |
| [Autoremesher](https://github.com/huxingyi/autoremesher) | MIT app, links CoMISo + libQEx (GPL3) | No | Better flow; GPL-tainted. Reference only. |
| [QuadWild](https://github.com/nicopietroni/quadwild) | GPL3 + Gurobi | No | Academic state of the art; quality benchmark only. |
| Exoside Quad Remesher | Commercial plug-in, no SDK | — | Would need a private deal. |

The novel path: fit a cross-field from user strokes (or a CrossGen-style learned field) and hand it to Instant Meshes' field-aligned extraction — the optimisation step becomes guidance, the extractor stays BSD.

### Manual retopology UX to borrow

- [Maya Quad Draw](https://help.autodesk.com/cloudhelp/2016/ENU/Maya/files/GUID-ED28E6BC-6141-491C-8C1B-2AE6FC813284.htm): one cursor + 3 modifiers; dot-then-fill. Maps to tap-tap-tap-lift.
- [RetopoFlow 4](https://docs.retopoflow.com/v4/quick_start.html): Contours (stroke across a limb → loop), PolyStrips (stroke → quad strip), Strokes (bridge parallel strokes), Relax/Tweak surface-snapped brushes. The most Pencil-native idioms anywhere; GPL, so re-implement.
- ZBrush Topology brush: crossing curves make quads with no modifiers.
- [TopoGun 3](https://www.topogun.com/): persistent guide strokes on the reference mesh, pressure-driven relax brush, one-pass multi-map bake.

### UV unwrapping

| Library | License | App Store | Verdict |
| --- | --- | --- | --- |
| [geometry-central](https://github.com/nmwsharp/geometry-central) | MIT | Yes | Half-edge mesh + Boundary First Flattening on Eigen with no SuiteSparse. **Primary choice.** |
| [Boundary First Flattening](https://github.com/GeometryCollective/boundary-first-flattening) | MIT (needs SuiteSparse) | With solver swap | Conformal, editable boundaries, cones; millions of tris interactively. Use via geometry-central. |
| [xatlas](https://github.com/jpcy/xatlas) | MIT | Yes | Zero-dependency charting + packing; usable as packer only; multi-atlas gives UDIM tiles. |
| [libigl core](https://libigl.github.io/) | MPL2 | Yes | LSCM, ARAP, harmonic; SLIM header — check banner. Avoid `igl/copyleft/`. |
| [OptCuts](https://github.com/liminchen/OptCuts) | MIT | Yes | Joint seam + distortion optimisation; offline; candidate for "auto-seam" button. |
| UVAtlas | MIT | Archived Apr 2026 | Skip. |
| CGAL parameterization | GPL | No | Skip. |

UDIM and texel density have no permissive library; the maths is trivial and goes in-house.

### Baking and EXR

- [tinyexr](https://github.com/syoyo/tinyexr) — BSD-3, single header, half/float, explicitly lists iOS AArch64. Use this.
- [OpenEXR 3.5](https://github.com/AcademySoftwareFoundation/openexr) — BSD-3, heavier; no iOS recipe.
- Baker: rasterise UV space in Metal compute, ray-cast against a BVH of the high-res mesh; output normal, height, VDM (per-texel XYZ offset in tangent frame), AO, curvature, cavity.

### Mesh core

| Component | License | Verdict |
| --- | --- | --- |
| [OpenSubdiv](https://github.com/PixarAnimationStudios/OpenSubdiv) | Apache-2-derived | Metal backend, explicit iOS build recipe. Use for multires/Catmull-Clark. |
| [OpenVDB](https://github.com/AcademySoftwareFoundation/openvdb) | Apache 2 | Needs TBB; no iOS docs; heavy. Prefer a bespoke sparse SDF + marching cubes in Metal compute for voxel remesh (what mobile sculptors realistically do). |
| Dynamic topology | algorithm | Blender's three local ops (collapse / split / merge) on a PBVH; implement on own half-edge. |
| [OpenMesh ≥4](https://www.graphics.rwth-aachen.de/software/openmesh/license/) | BSD-3 | Alternative half-edge kernel. |
| Eigen | MPL2 | Underpins everything. |
| CGAL | GPL algorithms | Avoid without GeometryFactory licence. |

### AI research worth tracking (2025–26)

- [SQuadGen](https://github.com/microsoft/SQuadGen) (MIT, TOG 2026) — diffusion quad layouts; 1B params, server-side only.
- [CrossGen](https://arxiv.org/abs/2506.07020) — feed-forward cross-field in under a second; the tractable on-device idea.
- [TopGen](https://arxiv.org/html/2603.10606v1) (Mar 2026) — joint feature lines + cross-field, 220k-sample dataset.
- [PartUV](https://github.com/EricWang12/PartUV), [ArtUV](https://arxiv.org/abs/2509.20710), [Segment Any Mesh](https://github.com/gtangg12/samesh) — part-aware seams; licences partly unstated; the seam-suggestion feature can start heuristic and swap in a model later.

## Tech stack options and recommendation

Recommendation: **a C++17/20 geometry core, a thin renderer interface with Metal as the first backend, and a Swift/UIKit shell** using Swift↔C++ interop. Every geometry library above is C++; the shell gets native Pencil, windowing and App Store behaviour; the core is reusable on macOS and, with a Vulkan backend, on Windows.

| Option | Pencil / platform fidelity | Portability to Windows | Licensing | Verdict |
| --- | --- | --- | --- | --- |
| Pure Swift + Metal | Best | None | Clean | Fastest to first demo; strands the desktop plan |
| **C++ core + Metal, Swift shell** | Best | Core yes, renderer via Vulkan later | Clean (all deps permissive) | **Recommended** |
| Rust core + [wgpu](https://github.com/gfx-rs/wgpu), Swift shell | Good (Metal first-class on iOS) | Yes (wgpu is cross-platform) | MIT/Apache | Viable, but the geometry libraries are C++; FFI tax on every call |
| C++ + [Dawn](https://dawn.googlesource.com/dawn/+/refs/heads/main/README.md) WebGPU | Unproven | Yes | BSD | README lists iOS as "eventually"; not ready |
| Qt / QML | Poor for pen | Yes | LGPL incompatible with App Store; commercial only | Not recommended |
| Web (WebGPU in Safari, like Re:Form) | No Pencil Pro, no Background Tasks | Yes | Clean | Fine for a viewer, not a sculptor |

**Platform facts that shape the design** ([Apple docs](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api.md))

- Target iPadOS 26, A14/M1+. Metal 4 adds argument tables, mesh shaders, a unified compute encoder and an ML encoder that runs Core ML inside the command stream — the route for on-device seam/flow models later. Adopt classic Metal for v1, MTL4 for ML passes.
- MetalFX temporal upscaling (iOS 16+) keeps the 2752×2064 panel at 120 Hz at \~0.65× render scale; frame interpolation adds pen latency and should stay off.
- Pencil: consume raw `UITouch` (force, altitude, azimuth, `rollAngle`, coalesced + predicted touches, 240 Hz). `UIPencilInteraction` gives double-tap, squeeze (17.5+) and `UIPencilHoverPose`; hover via `UIHoverGestureRecognizer`. Honour `preferredSqueezeAction`. Skip PencilKit — it is an ink canvas, not a low-latency input path.
- iPadOS 26 windowing and per-app menu bar make a desktop-style multi-pane retopo/UV layout legitimate; Background Tasks API with Live Activities is the correct home for 30–60 s remeshes and bakes.
- Memory: the increased-memory-limit entitlement gives \~14 GB practical on a 16 GB M4; keep multires levels on disk, use purgeable buffers, poll `os_proc_available_memory`.
- App size cap 4 GB; 500 MB `__TEXT`. Ship any ML models as downloadable assets.
- Swift↔C++ interop (Swift 5.9+) covers std containers, smart pointers, virtual methods; cannot catch C++ exceptions — keep the core exception-free at the boundary.

**Development approach**: VS Code + Claude Code for the C++ core with a CMake build and a headless test harness on macOS/Linux; Xcode only for the Swift shell and signing. This matches the existing dev methodology and keeps the core testable without an iPad in the loop.

## Strategy: iPad port of the desktop app, or a separate iPad-first app?

Recommendation: **one shared core, iPad-first shell, desktop shell second** — not a port in either direction and not two codebases.

The existing plan is a from-scratch Windows Mudbox replacement with ZRemesher-style retopo and auto-UV. Everything that is hard in that plan (mesh kernel, multires, remesh, UV, bake) is platform-neutral C++. Everything that differs between iPad and Windows (input, windowing, GPU API, file access) is a thin shell. The three options:

| Option | Pros | Cons |
| --- | --- | --- |
| Desktop first, port to iPad later | Mouse/keyboard UX is familiar; Windows lab fleet is the install base | Ports of desktop sculptors (ZBrush iPad) inherit cluttered menus and desktop assumptions; retrofitting Pencil, hover, squeeze and 120 Hz touch is where ZBrush iPad still falls short two years in |
| Separate iPad-first app | Purest touch UX; smallest scope | Duplicates the hard geometry work; two apps to maintain; splits the open-source community |
| **Shared core, iPad-first shell** | One geometry kernel tested headless; iPad constraints (memory, latency, touch) force a lean, fast core that then runs trivially on desktop; iPad has the clearer market gap (no free full-toolset app) while Windows already has Blender | Renderer needs a second backend (Vulkan) for Windows; Swift shell is Apple-only, so the Windows shell is a second UI (Dear ImGui or Qt-free native) |

Why iPad first rather than Windows first:

- The market gap is larger on iPad: Windows students already have Blender for free; iPad students have nothing free that exports.
- The iPad + Pencil audience is large and underserved; desktop users have many free options, so the desktop shell can follow once the core is proven.
- Designing for Pencil latency, 14 GB memory and background-task limits produces a core that is fast everywhere; the reverse is not true.
- Nomad proves a one-developer iPad sculptor can reach 14K ratings; the open-source angle plus retopo/UV is the differentiation.

What to keep from the desktop plan unchanged: Mudbox's Alt-navigation (mapped to Pencil/finger on iPad, Alt on desktop), vector displacement / normal map output, brush curves, multires with lockable level 0, ZRemesher-class remesh and UV Master-class auto UV as MVP. What changes: the MVP ships on iPad; the Windows shell follows once the core is stable, reusing the same project format and tests.

## Sources

Pages opened 15 Oct 2026. Competitor facts: [Nomad App Store (IE)](https://apps.apple.com/ie/app/nomad-sculpt/id1519508653), [Nomad manual: topology](https://nomadsculpt.com/manual/topology), [Nomad manual: files](https://nomadsculpt.com/manual/files), [Nomad manual: interface](https://nomadsculpt.com/manual/interface), [Nomad 2.9 changelog](https://forum.nomadsculpt.com/t/nomad-change-log/139/83), [Nomad industry-standard thread](https://forum.nomadsculpt.com/t/features-to-bring-it-closer-to-industry-standard/23346), [Nomad Pencil Pro thread](https://forum.nomadsculpt.com/t/apple-pencil-pro-update/16269), [80.lv on Live Link](https://80.lv/articles/nomad-sculpt-adds-direct-app-linking-to-blender-zbrush), [Maxon plan options](https://www.maxon.net/zbrush-plan-options), [ZBrush iPad free vs subscription](https://help.maxon.net/zbp/en-us/Content/html/2_installation-and-activation_Free-vs-subscription.html), [ZBrush iPad what's new](https://help.maxon.net/zbp/en-us/Content/html/0_ZBrush-ipad-whats-new.html), [ZBrush 2027 announcement](https://www.zbrushcentral.com/t/zbrush-2027-has-arrived/486939), [CG Channel: ZBrush 2026.2](https://www.cgchannel.com/2026/04/maxon-releases-zbrush-2026-2-and-zbrush-for-ipad-2026-2/), [ZBrush iPad reviews](https://apps.apple.com/us/app/zbrush-made-for-3d-creators/id6612028716?see-all=reviews&platform=ipad), [Creative Bloq review](https://tech.yahoo.com/computing/articles/zbrush-ipad-review-powerful-feature-160000169.html), [CG Channel: Forger discontinued](https://www.cgchannel.com/2025/07/maxon-discontinues-forger/), [Forger EoS FAQ](https://www.maxon.net/en/forger-eos-faq), [Consumer Rights Wiki on Forger](https://consumerrights.wiki/w/Maxon_Forger_discontinuation_and_subscription_file_lockout), [Shapr3D pricing](https://www.shapr3d.com/pricing), [Procreate 3D painting](https://help.procreate.com/articles/Z0mATv-3d-painting), [Valence 3D](https://valence3d.app/), [Sculptura 2](https://apps.apple.com/app/id6463495541), [Re:Form comparison](https://reformsculpt.com/blog/nomad-sculpt-vs-zbrush-ipad), [MacRumors: Blender iPad halted](https://www.macrumors.com/2026/02/27/blender-ipad-pro-app-development-halted/).

Libraries and platform: [Instant Meshes](https://github.com/wjakob/instant-meshes), [QuadriFlow](https://github.com/hjwdzh/QuadriFlow), [Autoremesher acknowledgements](https://github.com/huxingyi/autoremesher/blob/master/ACKNOWLEDGEMENTS.html), [QuadWild](https://github.com/nicopietroni/quadwild), [Exoside Quad Remesher](https://exoside.com/quadremesher/), [libigl tutorial](https://libigl.github.io/tutorial/), [geometry-central parameterization](https://geometry-central.net/surface/algorithms/parameterization/), [BFF](https://github.com/GeometryCollective/boundary-first-flattening), [xatlas](https://github.com/jpcy/xatlas), [UVAtlas](https://github.com/microsoft/UVAtlas), [OptCuts](https://github.com/liminchen/OptCuts), [CGAL licence](https://www.cgal.org/license.html), [tinyexr](https://github.com/syoyo/tinyexr), [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr), [OpenVDB dependencies](https://www.openvdb.org/documentation/doxygen/dependencies.html), [OpenSubdiv](https://github.com/PixarAnimationStudios/OpenSubdiv), [OpenMesh licence](https://www.graphics.rwth-aachen.de/software/openmesh/license/), [Blender dyntopo](https://code.blender.org/2011/10/dynamic-topology-sculpting/), [RetopoFlow docs](https://docs.retopoflow.com/v4/quick_start.html), [Maya Quad Draw](https://help.autodesk.com/cloudhelp/2016/ENU/Maya/files/GUID-ED28E6BC-6141-491C-8C1B-2AE6FC813284.htm), [TopoGun](https://www.topogun.com/), [Metal 4 core API](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api.md), [MetalFX](https://developer.apple.com/documentation/metalfx.md), [UIPencilInteraction](https://developer.apple.com/documentation/uikit/uipencilinteraction.md), [UIPencilHoverPose](https://developer.apple.com/documentation/uikit/uipencilhoverpose.md), [Predicted touches](https://developer.apple.com/documentation/uikit/minimizing-latency-with-predicted-touches.md), [iPadOS 26](https://www.apple.com/newsroom/2025/06/ipados-26-introduces-powerful-new-features-that-push-ipad-even-further/), [Swift C++ interop status](https://swift.org/documentation/cxx-interop/status/), [Increased memory limit](https://developer.apple.com/tutorials/data/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit.md), [App size limits](https://developer.apple.com/help/app-store-connect/reference/maximum-build-file-sizes/), [wgpu](https://github.com/gfx-rs/wgpu), [Dawn README](https://dawn.googlesource.com/dawn/+/refs/heads/main/README.md), [Qt LGPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations), [SQuadGen](https://github.com/microsoft/SQuadGen), [CrossGen](https://arxiv.org/abs/2506.07020), [TopGen](https://arxiv.org/html/2603.10606v1), [PartUV](https://github.com/EricWang12/PartUV), [ArtUV](https://arxiv.org/abs/2509.20710), [Segment Any Mesh](https://github.com/gtangg12/samesh).
