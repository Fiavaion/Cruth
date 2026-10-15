#!/usr/bin/env bash
# Build and run the iPad app from the terminal (and from VS Code tasks).
#
#   ./scripts/ipad.sh generate   regenerate shell-ipad/Cruth.xcodeproj from project.yml
#   ./scripts/ipad.sh build      build for the iPad simulator
#   ./scripts/ipad.sh run        build, boot an iPad simulator, install and launch (logs stream here)
#   ./scripts/ipad.sh test       run the UI smoke tests (CruthUITests) in the simulator; extra arguments
#                                go to xcodebuild, e.g. -only-testing:CruthUITests/CruthUITests/testQuadRemeshAndUndo
#   ./scripts/ipad.sh device     build, install and launch on a connected iPad (needs signing, see docs/SETUP.md)
#   ./scripts/ipad.sh open       open the project in Xcode
#   ./scripts/ipad.sh clean      delete the app build products
#
# Environment: CRUTH_SIMULATOR picks a simulator by name (default: the first
# available iPad); CRUTH_DEVICE picks a device by name or identifier.
# The test result bundle lands in build/ipad/UITests.xcresult.
# SPDX-License-Identifier: MIT
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
shell="$root/shell-ipad"
derived="$root/build/ipad"
bundle_id_default="org.cruth.app"

fail() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
[[ "$(uname -s)" == "Darwin" ]] || fail "the iPad app builds on macOS with Xcode only"

pretty() { if command -v xcbeautify >/dev/null; then xcbeautify; else cat; fi; }

generate() {
  command -v xcodegen >/dev/null || fail "xcodegen is missing: brew install xcodegen (or run scripts/setup-mac.sh)"
  (cd "$shell" && xcodegen generate --quiet)
}

simulator_id() {
  local list line
  list="$(xcrun simctl list devices available)"
  if [[ -n "${CRUTH_SIMULATOR:-}" ]]; then
    line="$(grep -F "    ${CRUTH_SIMULATOR} (" <<<"$list" | head -1 || true)"
  else
    line="$(grep -E '^ +iPad' <<<"$list" | head -1 || true)"
  fi
  [[ -n "$line" ]] || fail "no iPad simulator found. Create one in Xcode > Window > Devices and Simulators, or install an iOS runtime in Xcode > Settings > Components."
  sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/' <<<"$line"
}

bundle_id() {
  # The bundle identifier can be overridden in shell-ipad/Signing.local.xcconfig.
  local id=""
  [[ -f "$shell/Signing.local.xcconfig" ]] && id="$(sed -nE 's/^ *CRUTH_BUNDLE_ID *= *([^ ]+).*/\1/p' "$shell/Signing.local.xcconfig" | tail -1)"
  echo "${id:-$bundle_id_default}"
}

xcb() {
  set -o pipefail
  xcodebuild -project "$shell/Cruth.xcodeproj" -scheme Cruth -derivedDataPath "$derived" "$@" | pretty
}

build_sim() {
  generate >&2
  local id; id="$(simulator_id)"
  xcb -configuration Debug -destination "id=$id" CODE_SIGNING_ALLOWED=NO build >&2
  echo "$id"
}

case "${1:-help}" in
  generate) generate; echo "generated $shell/Cruth.xcodeproj" ;;
  build) build_sim >/dev/null ;;
  run)
    id="$(build_sim)"
    app="$derived/Build/Products/Debug-iphonesimulator/Cruth.app"
    # Boot and wait until the simulator is ready: installing into a simulator
    # that is still booting silently loses the app on a cold start.
    xcrun simctl bootstatus "$id" -b >/dev/null
    open -a Simulator --args -CurrentDeviceUDID "$id"
    xcrun simctl install "$id" "$app"
    echo "launching $(bundle_id) (Ctrl-C stops log streaming; the app keeps running)"
    xcrun simctl launch --console-pty "$id" "$(bundle_id)"
    ;;
  test)
    shift
    generate
    id="$(simulator_id)"
    xcrun simctl bootstatus "$id" -b >/dev/null
    rm -rf "$derived/UITests.xcresult"
    xcb -configuration Debug -destination "id=$id" -resultBundlePath "$derived/UITests.xcresult" \
      CODE_SIGNING_ALLOWED=NO test ${1+"$@"}
    ;;
  device)
    generate
    xcb -configuration Debug -destination 'generic/platform=iOS' -allowProvisioningUpdates build
    app="$derived/Build/Products/Debug-iphoneos/Cruth.app"
    device="${CRUTH_DEVICE:-}"
    if [[ -z "$device" ]]; then
      device="$(xcrun devicectl list devices 2>/dev/null | grep -i ipad \
        | grep -oE '[0-9A-F]{8}-([0-9A-F]{4}-){3}[0-9A-F]{12}' | head -1 || true)"
    fi
    [[ -n "$device" ]] || fail "no iPad found. Connect it, unlock it, trust this Mac, and enable Developer Mode; or set CRUTH_DEVICE (see: xcrun devicectl list devices)"
    xcrun devicectl device install app --device "$device" "$app"
    xcrun devicectl device process launch --device "$device" "$(bundle_id)"
    ;;
  open) generate; open "$shell/Cruth.xcodeproj" ;;
  clean) rm -rf "$derived"; echo "removed $derived" ;;
  *) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
