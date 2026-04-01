#!/usr/bin/env bash

set -u
set -o pipefail

log() {
  printf '[mvrvb-smoke] %s\n' "$*"
}

prompt_choice() {
  local prompt="$1"
  local choices="$2"
  local response=""

  while true; do
    read -r -p "$prompt " response
    response="$(printf '%s' "$response" | tr '[:upper:]' '[:lower:]')"
    for choice in $choices; do
      if [[ "$response" == "$choice" ]]; then
        printf '%s' "$response"
        return 0
      fi
    done
    printf 'Expected one of: %s\n' "$choices" >&2
  done
}

run_and_capture() {
  local name="$1"
  shift

  local output_file="$LOG_DIR/${name}.txt"
  local exit_file="$LOG_DIR/${name}.exit.txt"

  log "Running $name"
  set +e
  "$@" >"$output_file" 2>&1
  local exit_code=$?
  set -e

  printf '%s\n' "$exit_code" >"$exit_file"
  return "$exit_code"
}

copy_optional_file() {
  local src="$1"
  local dst="$2"
  if [[ -f "$src" ]]; then
    cp "$src" "$dst"
  fi
}

write_summary() {
  cat >"$SUMMARY_FILE" <<EOF
# MetalVR Bridge Mac Smoke Test Summary

- Commit: $COMMIT_SHA
- Run timestamp: $RUN_STAMP
- Repo root: $REPO_ROOT
- Log directory: $LOG_DIR
- ICD log file: $MVRVB_LOG_FILE
- Launcher triangle: $LAUNCHER_TRIANGLE_RESULT
- vulkaninfo: $VULKANINFO_RESULT
- vkcube: $VKCUBE_RESULT

## Bundle Contents

- commit.txt
- env.txt
- git-status.txt
- sw_vers.txt
- system_profile.txt
- summary.md
- build-configure.txt / build-configure.exit.txt
- build.txt / build.exit.txt
- ctest.txt / ctest.exit.txt
- launcher-setup.txt / launcher-setup.exit.txt
- launcher_triangle.result.txt
- vulkaninfo.txt / vulkaninfo.exit.txt
- vkcube.txt / vkcube.exit.txt
- launcher_diagnostic.* (optional)
- vkcube_failure.png (optional)

## First Failure Boundary

$FIRST_FAILURE_BOUNDARY

## Notes

$NOTES
EOF
}

finish_run() {
  write_summary

  if command -v zip >/dev/null 2>&1; then
    local bundle_parent
    local bundle_name
    bundle_parent="$(dirname "$LOG_DIR")"
    bundle_name="$(basename "$LOG_DIR")"
    (
      cd "$bundle_parent"
      zip -qry "$bundle_name.zip" "$bundle_name"
    )
    log "Wrote bundle archive: $bundle_parent/$bundle_name.zip"
  else
    log "zip not found; leaving bundle as directory only"
  fi

  log "Summary written to $SUMMARY_FILE"
  log "Attach the summary and bundle when filing the GitHub smoke-test report."
}

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
RUN_STAMP="${RUN_STAMP:-$(date +%Y%m%d-%H%M%S)}"
LOG_ROOT="${LOG_ROOT:-$HOME/metalvr-bridge-logs}"
LOG_DIR="${LOG_DIR:-$LOG_ROOT/$RUN_STAMP}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
SUMMARY_FILE="$LOG_DIR/summary.md"
LAUNCHER_APP="${LAUNCHER_APP:-$REPO_ROOT/launcher/MetalVR Bridge.app}"

export MVRVB_LOG_LEVEL="${MVRVB_LOG_LEVEL:-debug}"
export MVRVB_LOG_FILE="${MVRVB_LOG_FILE:-$LOG_DIR/icd.log}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$REPO_ROOT/vulkan_icd.json}"
export VK_LOADER_DEBUG="${VK_LOADER_DEBUG:-}"

LAUNCHER_TRIANGLE_RESULT="not-run"
VULKANINFO_RESULT="not-run"
VKCUBE_RESULT="not-run"
FIRST_FAILURE_BOUNDARY="No failure recorded."
NOTES="Add any tester notes here before filing the GitHub issue."

mkdir -p "$LOG_DIR"
COMMIT_SHA="$(git -C "$REPO_ROOT" rev-parse HEAD)"

printf '%s\n' "$COMMIT_SHA" >"$LOG_DIR/commit.txt"
git -C "$REPO_ROOT" status --short >"$LOG_DIR/git-status.txt"
sw_vers >"$LOG_DIR/sw_vers.txt"
system_profiler SPHardwareDataType SPSoftwareDataType SPDisplaysDataType >"$LOG_DIR/system_profile.txt" 2>&1
cat >"$LOG_DIR/env.txt" <<EOF
REPO_ROOT=$REPO_ROOT
BUILD_DIR=$BUILD_DIR
LOG_DIR=$LOG_DIR
MVRVB_LOG_LEVEL=$MVRVB_LOG_LEVEL
MVRVB_LOG_FILE=$MVRVB_LOG_FILE
VK_ICD_FILENAMES=$VK_ICD_FILENAMES
VK_LOADER_DEBUG=$VK_LOADER_DEBUG
EOF

log "Smoke-test bundle: $LOG_DIR"

if [[ "${MVRVB_SKIP_BUILD:-0}" != "1" ]]; then
  if ! run_and_capture build-configure cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DMVRVB_BUILD_TESTS=ON; then
    FIRST_FAILURE_BOUNDARY="CMake configure failed."
    NOTES="Inspect build-configure.txt for the configure failure."
    finish_run
    exit 1
  fi

  if ! run_and_capture build cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"; then
    FIRST_FAILURE_BOUNDARY="Native macOS build failed."
    NOTES="Inspect build.txt for compiler or linker failures."
    finish_run
    exit 1
  fi

  if ! run_and_capture ctest ctest --test-dir "$BUILD_DIR" --output-on-failure; then
    FIRST_FAILURE_BOUNDARY="Host-side unit tests failed on the Mac."
    NOTES="Inspect ctest.txt for the failing test target."
    finish_run
    exit 1
  fi

  if ! run_and_capture launcher-setup bash "$REPO_ROOT/launcher/setup.sh"; then
    FIRST_FAILURE_BOUNDARY="Launcher app packaging failed."
    NOTES="Inspect launcher-setup.txt for the launcher packaging failure."
    finish_run
    exit 1
  fi
else
  log "Skipping build/setup because MVRVB_SKIP_BUILD=1"
fi

if [[ "${MVRVB_SKIP_LAUNCHER_TRIANGLE:-0}" != "1" ]]; then
  if [[ -d "$LAUNCHER_APP" ]]; then
    open "$LAUNCHER_APP"
  else
    log "Launcher app missing at $LAUNCHER_APP"
  fi

  printf '%s\n' \
    "In the launcher:" \
    "1. Click Run Triangle Test" \
    "2. Export diagnostics if the test fails" \
    "3. Return here after the test completes" \
    >"$LOG_DIR/launcher_triangle.instructions.txt"

  LAUNCHER_TRIANGLE_RESULT="$(prompt_choice 'Launcher triangle result [pass/fail/skip]:' 'pass fail skip')"
  printf '%s\n' "$LAUNCHER_TRIANGLE_RESULT" >"$LOG_DIR/launcher_triangle.result.txt"

  if [[ "$LAUNCHER_TRIANGLE_RESULT" == "fail" ]]; then
    read -r -p "Optional path to exported launcher diagnostics: " launcher_diag_path
    if [[ -n "$launcher_diag_path" ]]; then
      copy_optional_file "$launcher_diag_path" "$LOG_DIR/launcher_diagnostic_$(basename "$launcher_diag_path")"
    fi
    FIRST_FAILURE_BOUNDARY="Launcher triangle test failed."
    NOTES="Attach any launcher diagnostic export and note the failing Metal boundary."
    finish_run
    exit 1
  fi
else
  LAUNCHER_TRIANGLE_RESULT="skip"
  printf '%s\n' "$LAUNCHER_TRIANGLE_RESULT" >"$LOG_DIR/launcher_triangle.result.txt"
fi

if ! run_and_capture vulkaninfo vulkaninfo; then
  VULKANINFO_RESULT="fail"
  FIRST_FAILURE_BOUNDARY="vulkaninfo failed before runtime smoke-test completion."
  NOTES="Attach icd.log, vulkaninfo.txt, and vulkaninfo.exit.txt."
  finish_run
  exit 1
fi
VULKANINFO_RESULT="pass"

if ! run_and_capture vkcube vkcube; then
  VKCUBE_RESULT="fail"
  FIRST_FAILURE_BOUNDARY="vkcube exited with a failing status code."
  NOTES="Attach icd.log, vkcube.txt, vkcube.exit.txt, and a screenshot if possible."
  if command -v screencapture >/dev/null 2>&1; then
    screencapture -x "$LOG_DIR/vkcube_failure.png" || true
  fi
  finish_run
  exit 1
fi

VKCUBE_RESULT="$(prompt_choice 'Did vkcube render and animate correctly? [pass/fail]:' 'pass fail')"
if [[ "$VKCUBE_RESULT" == "fail" ]]; then
  FIRST_FAILURE_BOUNDARY="vkcube launched but did not render correctly."
  NOTES="Attach icd.log, vkcube.txt, vkcube.exit.txt, and the failure screenshot."
  if command -v screencapture >/dev/null 2>&1; then
    screencapture -x "$LOG_DIR/vkcube_failure.png" || true
  fi
  finish_run
  exit 1
fi

FIRST_FAILURE_BOUNDARY="No failure recorded. Launcher triangle, vulkaninfo, and vkcube all passed."
NOTES="Attach the summary and bundle to the GitHub smoke-test issue so the pass is recorded."
finish_run
