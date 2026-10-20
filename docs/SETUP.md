# Setting up Cruth on your laptop (VS Code + Claude Code)

This gets you from a fresh clone to the C++ core building and testing, and to the iPad app running in a simulator and on your iPad, all driven from VS Code with Claude Code.

## What you need

| | For the C++ core | For the iPad app |
| --- | --- | --- |
| Computer | macOS, Linux or Windows | **a Mac** (Apple silicon recommended) |
| Compiler | Xcode command-line tools, Clang 15+, GCC 12+ or MSVC 2022 | **Xcode 26** with an iPadOS 26 simulator runtime |
| Tools | CMake 3.21+, Ninja | XcodeGen (`brew install xcodegen`) |
| Editor | VS Code with the Claude Code extension | same |

The iPad app can only be built on a Mac, because Apple's iOS SDK and code signing ship only with Xcode. VS Code drives Xcode's command-line tools for you. On Linux you can still work on and test the whole core, which is where nearly all the code lives. Windows (MSVC) is set up in CMake but not yet tested; expect a few portability fixes.

## 1. Get the code

```sh
git clone -b claude/lucid-franklin-mrpthk https://github.com/fiavaion/cruth.git Cruth
cd Cruth
```

(Once this branch is merged, clone `main` instead.)

## 2. One-time setup (Mac)

```sh
./scripts/setup-mac.sh
```

This script:
- checks Xcode;
- installs `cmake`, `ninja`, `xcodegen` and `xcbeautify` with Homebrew;
- installs the recommended VS Code extensions (if the `code` command is on your PATH);
- builds the core in Release and runs every test;
- generates `shell-ipad/Cruth.xcodeproj`.

It finishes in a few minutes. If Xcode was just installed, open it once first to accept the licence, then run `sudo xcode-select -s /Applications/Xcode.app`.

On **Linux**: install `cmake ninja-build g++` (or clang) with your package manager. On **Windows**: install Visual Studio 2022 (C++ workload, which includes CMake and Ninja) and open VS Code from the "Developer PowerShell for VS". Then:

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
```

## 3. Open in VS Code

```sh
code .
```

- Accept the **recommended extensions** prompt: Claude Code, CMake Tools, clangd, CodeLLDB and Swift.
- CMake Tools configures automatically. Pick the **Release** preset in the status bar (Debug is for stepping through code).
- clangd picks up `compile_commands.json` after the first configure. If it asked to disable Microsoft's C++ IntelliSense, say yes.

Everything else is under **Terminal → Run Task…**:

| Task | What it does |
| --- | --- |
| Core: build (Release) | default build (Cmd+Shift+B) |
| Core: test (Release) | all tests, including the 7-second acceptance chain |
| Core: test with sanitizers | ASan + UBSan build and tests (slow; run before big commits) |
| Acceptance chain (smoke / full) | the PRD acceptance test with timings; writes `build/acceptance-*/report.json` |
| iPad: build (simulator) | builds the app for the simulator |
| iPad: run in simulator | builds, boots an iPad simulator, installs and launches, streams logs |
| iPad: run on connected iPad | see section 5 |
| iPad: open in Xcode | for signing, Instruments, the view debugger and Metal frame capture |

To debug the core, use the Run and Debug panel: **Core: unit tests**, **Core: acceptance chain** or **Core: cruth-cli**. Each builds the Debug preset first.

## 4. iPad simulator

```sh
./scripts/ipad.sh run
```

This picks the first available iPad simulator. To choose another, set `CRUTH_SIMULATOR="iPad Pro 13-inch (M5)"` (list them with `xcrun simctl list devices available`). With finger sculpting on, a finger on the model sculpts and a finger on the background orbits; a mouse or trackpad acts the same way in the simulator. Turn on **Finger Sculpting** in the brush panel. The **Draw with Finger** toggle in the Guides and Retopo panels does the same for guide strokes and retopology tools. The Pencil-only features (pressure, hover, squeeze) need a real iPad.

To run the 25-test XCUITest smoke suite in the simulator:

```sh
./scripts/ipad.sh test
```

## 5. Your own iPad

1. In Xcode → Settings → Accounts, sign in with your Apple ID. A free account works, but the app expires after 7 days.
2. Create `shell-ipad/Signing.local.xcconfig`. It is git-ignored, so it stays on your machine:
   ```
   DEVELOPMENT_TEAM = ABCDE12345
   CRUTH_BUNDLE_ID = com.yourname.cruth
   ```
   Your team ID is in Xcode → Settings → Accounts → (team) or at developer.apple.com → Membership. With a free account, the bundle ID must be unique to you.
3. Connect the iPad by cable, unlock it, tap **Trust**, and turn on **Settings → Privacy & Security → Developer Mode** (it restarts).
4. Run `./scripts/ipad.sh device` (or the **iPad: run on connected iPad** task); add `--release` for a Release build. It builds for the connected iPad with `-allowProvisioningDeviceRegistration` (so Xcode registers it with your team) and skips simulators. On the first run, you may need to trust your developer certificate on the iPad, under Settings → General → VPN & Device Management.

Tested: iPad Pro 12.9" M1, iPadOS 27.0.1, Xcode 27.1, Release build.

## 6. Working with Claude Code

`CLAUDE.md` at the repo root is loaded automatically. It explains the architecture, build commands, rules (core owns all algorithms, no exceptions across the C API, no GPL, DCO sign-off) and current state. `.claude/settings.json` pre-approves the routine build and test commands, so Claude doesn't have to ask each time.

Good first prompts:

- "Build the iPad app for the simulator and run `./scripts/ipad.sh test`; fix anything that fails." The shell has been built and run on a Mac, so this is a regression check.
- "Run the app in the simulator and check that the sculpt viewport and Remesh menu work."
- "Pick the next item from docs/ROADMAP.md and implement it with tests."

## Troubleshooting

- **`cmake --preset` says Ninja not found:** `brew install ninja` (macOS) or `sudo apt install ninja-build` (Linux).
- **`xcodebuild` says it requires Xcode** but only the command-line tools are selected: `sudo xcode-select -s /Applications/Xcode.app`.
- **"Metal Toolchain not installed"** when compiling `Shaders.metal`: Xcode 26 ships the Metal compiler as a separate component. Run `xcodebuild -downloadComponent MetalToolchain`, or use Xcode → Settings → Components.
- **No iPad simulator:** Xcode → Settings → Components, install the iOS 26 simulator runtime.
- **"No signing certificate" or "requires a development team"** on a device build: see section 5. Simulator builds need no signing.
- **"No Accounts" or "profile doesn't include the device":** sign in under Xcode → Settings → Accounts. Xcode updates can drop the sign-in.
- **After an Xcode update:** run `sudo xcodebuild -license accept`, and `xcodebuild -downloadComponent MetalToolchain` again.
- **Developer Mode is missing from the iPad's Settings:** it only appears once Xcode has paired with the iPad.
- **`invalidDigitCount` or debugger version warnings** from xcodebuild with iOS 26.5 simulators under Xcode 27.1: harmless.
- **The generated project is out of date** after you add or move Swift files: run `./scripts/ipad.sh generate`. Never edit `Cruth.xcodeproj` by hand; change `shell-ipad/project.yml` instead.
- **Tests are very slow:** run them in Release. Debug and sanitizer builds of the quad remesher are 20–100× slower.
