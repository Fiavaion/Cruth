#!/usr/bin/env bash
# One-time setup on a Mac: checks Xcode, installs the command-line tools the
# build needs (via Homebrew), installs the recommended VS Code extensions,
# then builds and tests the core and generates the iPad Xcode project.
#
#   ./scripts/setup-mac.sh            # everything
#   ./scripts/setup-mac.sh --no-build # tools only
# SPDX-License-Identifier: MIT
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || fail "this script is for macOS. On Linux, install cmake, ninja and a C++20 compiler, then run: cmake --preset release && cmake --build --preset release && ctest --preset release"

say "Xcode"
if ! xcodebuild -version >/dev/null 2>&1; then
  fail "Xcode is not installed or not selected. Install Xcode 26 from the App Store, open it once, then run: sudo xcode-select -s /Applications/Xcode.app"
fi
xcodebuild -version | head -1
major="$(xcodebuild -version | head -1 | sed -E 's/Xcode ([0-9]+).*/\1/')"
if (( major < 26 )); then
  echo "warning: the iPad app targets iPadOS 26 and needs Xcode 26 or newer (found $major). The C++ core still builds."
fi
if ! xcrun simctl list runtimes 2>/dev/null | grep -q "iOS"; then
  echo "note: no iOS simulator runtime found. Install one in Xcode > Settings > Components (or: xcodebuild -downloadPlatform iOS)."
fi

say "Homebrew packages: cmake, ninja, xcodegen"
command -v brew >/dev/null || fail "Homebrew is needed: see https://brew.sh"
for pkg in cmake ninja xcodegen; do
  if command -v "$pkg" >/dev/null; then echo "$pkg: $(command -v "$pkg")"; else brew install "$pkg"; fi
done
command -v xcbeautify >/dev/null || brew install xcbeautify || true  # optional: readable xcodebuild logs

say "VS Code extensions"
if command -v code >/dev/null; then
  for ext in ms-vscode.cmake-tools llvm-vs-code-extensions.vscode-clangd vadimcn.vscode-lldb swiftlang.swift-vscode anthropic.claude-code; do
    code --install-extension "$ext" >/dev/null && echo "$ext"
  done
else
  echo "'code' is not on PATH: in VS Code run 'Shell Command: Install code command in PATH', or accept the"
  echo "recommended-extensions prompt when you open the folder."
fi

[[ "${1:-}" == "--no-build" ]] && exit 0

say "Build and test the C++ core (Release)"
cmake --preset release
cmake --build --preset release
ctest --preset release

say "Generate the iPad Xcode project"
(cd shell-ipad && xcodegen generate --quiet)
echo "shell-ipad/Cruth.xcodeproj generated."

say "Done"
cat <<'EOF'
Next:
  ./scripts/ipad.sh run      build and launch the iPad app in a simulator
  ./scripts/ipad.sh open     open the project in Xcode (for devices, signing and Instruments)
See docs/SETUP.md for running on your own iPad.
EOF
