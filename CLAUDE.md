# Cruth: notes for Claude Code

Cruth is a free, MIT-licensed iPad sculpting app: sculpt → remesh → retopologise → unwrap → bake, on one device. A C++20 geometry core does all the work, headless and tested. A thin SwiftUI and Metal shell on iPad drives it through a plain-C API.

- Requirements: `docs/PRD.md` (requirement IDs like S-02, T-03, U-02 are referenced everywhere)
- What's done, measured, broken and next: `docs/ROADMAP.md`. Read it before starting new work.
- Laptop setup: `docs/SETUP.md`

## Layout

```
core/include/cruth/   public headers; cruth.h is the C API (C11, no C++ types)
core/src/             implementations, one .cpp per header
third_party/          vendored QuadriFlow (patched, see CRUTH_PATCHES.md), lemon subset, Eigen (MPL2-only), MikkTSpace (zlib)
tools/cruth-cli/      headless CLI: every core feature is reachable from here
tools/cruth-acceptance/ the PRD acceptance chain, timed, with pass/fail gates (phase-0 exit gate)
tests/                unit tests (tiny framework in test.hpp), cli_chain.cmake, acceptance.cmake
shell-ipad/           SwiftUI + Metal app; project.yml is the XcodeGen spec (the .xcodeproj is generated, never edit it)
scripts/              setup-mac.sh, ipad.sh (generate / build / run / device / open)
spec/                 dependency allow-list and project-format spec
```

## Build and test

Use the presets (Ninja). Build directories land in `build/<preset>`.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release   # everyday loop (~30 s of tests)
./build/release/tests/cruth_tests                     # unit tests only, prints each test
./build/release/tools/cruth-acceptance/cruth-acceptance --scale smoke --out build/acc   # 7 s; --scale full ~80 s
cmake --preset asan && cmake --build --preset asan && ctest --preset asan              # before committing core changes
./scripts/ipad.sh build        # iPad app for the simulator (macOS + Xcode 26 only)
./scripts/ipad.sh run          # build, wait for the simulator to boot, launch, stream logs
./scripts/ipad.sh test         # 16-test XCUITest smoke suite in the simulator
```

- Run tests in **Release**. Debug and ASan are very slow on quad remesh (Eigen unoptimised): the CLI chain takes about 40 minutes under ASan. The debug and asan test presets skip the acceptance label for that reason.
- Warnings are errors (`CRUTH_WARNINGS_AS_ERRORS=ON` in every preset). Keep GCC and Clang both clean: `-Wall -Wextra -Wpedantic -Wshadow`.
- A new core source file goes into `core/CMakeLists.txt`. The iPad target picks up all of `core/src` automatically.

## Rules (from CONTRIBUTING.md; CI enforces some)

- **The core owns every algorithm.** Shells only handle input, windowing, rendering and files. A new feature means: core function → `Document` command if it edits the model (for undo) → C API in `cruth.h` / `c_api.cpp` → CLI command → test → Swift UI.
- **No exceptions cross the C API.** Fallible core functions return `Status` or `Result<T>` (`core/include/cruth/result.hpp`). `c_api.cpp` catches everything. `cruth.h` must stay valid C11: `tests/c_header_check.c` compiles it as C. Helpers returning C++ types go outside `extern "C"`.
- **Licences:** MIT; new files start with an `SPDX-License-Identifier: MIT` comment. No GPL, LGPL or AGPL code anywhere (App Store), and CI greps for it. A new dependency goes into `spec/dependencies.md` and `THIRD_PARTY_NOTICES.md`. Record changes to vendored code in `third_party/quadriflow/CRUTH_PATCHES.md`.
- **Commits:** sign off with `git commit -s` (DCO).
- **Test it headless:** a unit test in `tests/test_*.cpp` for every core change. Extend `tests/cli_chain.cmake` for user-facing chain steps.

## Code conventions

- C++20, 4-space indent, about 120 columns, `snake_case` functions and variables, `PascalCase` types, `kConstant` constants. Comments explain why, briefly.
- Geometry: `Mesh` is an indexed polygon mesh (`positions`, `face(f)`, `face_offsets()`, `add_face`). `Index` is `uint32_t` and `kInvalidIndex` means none. Use `HalfEdgeMesh::build` for connectivity (it rejects non-manifold input). `TriangleBVH` handles closest point and raycast. `parallel_for` (parallel.hpp) is the threading primitive.
- Test framework: `TEST(name)`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR(a, b, eps)`, `REQUIRE`. There is no `REQUIRE_EQ`. Tests must stay fast in Release: the whole suite runs in about 3 s.
- Gotcha: `make_quad_sphere(n, r)` takes a **subdivision level**, giving 6·4ⁿ faces. Level 4 is 1.5k quads; level 8 is 393k.
- Swift shell: `SculptSession` owns the C handles. Heavy commands go through `runInBackground`, which suspends drawing, picking and input. Overlays are `CAShapeLayer`s in `SculptMetalView`.

## Current state

Phase 0 (headless core) has met its exit gate locally. See ROADMAP for measurements and known issues. The iPad shell builds and runs on a Mac (Xcode 26.6, iPad Pro 13-inch M5 simulator, iOS 26.5), and the core passes ctest on macOS/arm64. Things to know:
- The Metal toolchain must be downloaded once (`xcodebuild -downloadComponent MetalToolchain`) before `Shaders.metal` compiles.
- `./scripts/ipad.sh test` runs a 16-test XCUITest smoke suite. `ipad.sh run` waits for the simulator to boot.
- Device signing has been tested through `shell-ipad/Signing.local.xcconfig`.
- Keep Swift 5 mode with default concurrency checking. Don't enable "Default Actor Isolation = MainActor" or strict concurrency without fixing `Renderer` (MTKViewDelegate) and `SculptSession` background closures.
- GCC has not been run on this Mac.
- To change project settings, edit `shell-ipad/project.yml` (then `./scripts/ipad.sh generate`), never the generated project.

Next work, in order: hands-on iPad testing of the finger-driven tools, seam painting in the shell, tangent-space multires, Metal compute paths, then phase 1 (TestFlight MVP).
