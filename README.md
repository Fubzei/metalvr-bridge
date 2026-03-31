# MetalVR Bridge

MetalVR Bridge is a Vulkan Installable Client Driver (ICD) that translates
Vulkan API calls to Apple Metal so Vulkan-based rendering can run on macOS.
For DirectX titles, the intended stack is DXVK -> Vulkan -> MetalVR Bridge.

## Quick Start

```text
Windows game
  -> Wine or CrossOver
  -> DXVK for DirectX titles
  -> Vulkan
  -> MetalVR Bridge
  -> Apple Metal
```

## Current Repo Reality

- The active build currently includes:
  - `src/common`
  - `src/shader_translator`
  - `src/vulkan_layer`
- `src/vr_runtime` exists in the repository, but it is not part of the active root
  build today.
- A checked-in `tests/` harness now covers the SPIR-V parser, MSL emitter,
  format table, ICD contract checks, and extracted transfer helpers including
  range, slice, region-geometry, and transfer-format classification math.
- A checked-in `host-tests/` entrypoint plus `scripts/run_host_checks.ps1`
  now let those Apple-free checks run locally on Windows with LLVM, CMake, and Ninja.
- A checked-in compatibility profile system now lives under `profiles/`, with
  CI-backed parsing, auto-selection, and validation in
  `src/common/compatibility_profile.*` and
  `tests/unit/compatibility_profile_tests.cpp`, including install/setup policy
  alongside runtime policy.
- A checked-in prefix preset system now lives under `profiles/prefix-presets/`,
  with CI-backed parsing and validation in `src/common/prefix_preset.*` and
  `tests/unit/prefix_preset_tests.cpp`, so game profiles can point at named
  bottle-style setup defaults instead of duplicating launcher/package intent.
- A checked-in compatibility catalog layer now lives in
  `src/common/compatibility_catalog.*`, turning those checked-in profiles into a
  machine-readable and human-readable game/status database for future launcher,
  wiki, and runtime surfaces, now with merged prefix-preset setup intent.
- A checked-in profile lint layer now lives in `src/common/profile_lint.*` and
  `tools/mvrvb_profile_lint`, so the repo can catch missing preset references,
  duplicate IDs, and ambiguous auto-match policy before those mistakes reach
  runtime work.
- A checked-in runtime launch-plan builder now lives in
  `src/common/runtime_launch_plan.*`, turning those profiles into backend,
  fallback, install/setup, env, DLL-override, and launch-argument decisions
  that a launcher can consume, including resolved checked-in prefix presets.
- A checked-in runtime launch-command materializer now lives in
  `src/common/runtime_launch_command.*`, turning those plans into concrete
  Wine-style command, environment, and wrapper-script output for future runtime
  or launcher glue.
- A checked-in runtime setup-command materializer now lives in
  `src/common/runtime_setup_command.*`, turning install/setup policy into
  bootstrap actions, manual follow-up notes, and generated bash/PowerShell
  setup scripts for future prefix orchestration.
- A checked-in developer preview tool now lives in `tools/` so those runtime
  plans can be resolved and inspected locally before the launcher wiring is
  complete, including machine-readable JSON export, Markdown setup-checklist
  export, launch-script generation, and setup-script generation for future
  runtime or launcher integration.
- A checked-in compatibility catalog tool now lives in `tools/` so the repo can
  export a consistent compatibility matrix from the same checked-in profiles the
  runtime layer consumes, including JSON and Markdown views for docs/wiki reuse.
- A checked-in export helper now lives in `scripts/export_runtime_plan.ps1` so
  persisted JSON, human-readable reports, Markdown setup checklists, and wrapper
  scripts can be generated from the same shared contract without Mac hardware.
  When a prefix path is supplied, the same helper now also emits setup/bootstrap
  bash and PowerShell scripts from the install policy in the resolved plan.
- A companion export helper now lives in `scripts/export_profile_catalog.ps1` so
  JSON, report, and Markdown versions of the compatibility catalog can be generated
  without Mac hardware.
- A companion lint helper now lives in `scripts/run_profile_lint.ps1` so the
  checked-in profile and prefix-preset policy can be validated directly without
  waiting for the full host test suite.
- A cross-platform runtime bundle builder now lives in
  `tools/mvrvb_runtime_bundle_builder`, and `scripts/export_runtime_bundle.ps1`
  now wraps that builder so launch plans, setup scripts, compatibility catalog,
  and lint output can be packaged into one handoff directory for tester or
  future launcher/runtime flows. When no prefix path is supplied, the exported
  bundle now defaults to a portable self-contained `prefix/` directory.
- A dedicated AI handoff bundle helper now lives in
  `scripts/export_ai_handoff_bundle.ps1`, packaging the canonical repo docs,
  lint output, and compatibility catalog into one carry-forward bundle for the
  next coding agent.
- Those persisted JSON plans can now also be loaded back through the shared
  launch-plan layer, so future runtime or launcher glue can consume exported
  plans without re-solving profiles.
- The persisted launch-plan JSON contract is now schema-versioned so future
  consumers can validate compatibility instead of guessing.
- First-pass runtime observability logs now cover instance, device, pipeline,
  swapchain, submit, present, command-replay boundaries, draw/dispatch
  state-flush summaries, transfer/secondary-command breadcrumbs, and
  synchronization/unsupported-op breadcrumbs.
- The launcher now includes an in-app triangle test, a bundled project-status
  card sourced from `docs/PROJECT_STATUS.json`, a bundled compatibility summary
  sourced from `docs/GAME_COMPATIBILITY_CATALOG.json`, an imported or bundled
  runtime-plan preview sourced from `export_runtime_plan.ps1` output, imported
  runtime-bundle manifest support sourced from `export_runtime_bundle.ps1`,
  in-app setup/checklist/lint/launch-script/catalog previews for imported
  runtime bundles, guided runtime actions sourced from the shared plan contract,
  reveal/open/export actions for imported runtime bundles, one-click setup and
  launch actions for imported bash runtime scripts, clipboard copy helpers for
  imported launch-command and environment snippets, concise execution-prep
  sheet export from the launcher, in-app execution-prep preview, and
  diagnostic log export.
- `scripts/mac_runtime_smoke_test.sh` now automates the first hardware smoke-test
  bundle so Mac-side validation is easier to execute and report.
- Green macOS CI runs now publish downloadable ICD and launcher installer artifacts.
- The project now has protected `main` branch rules, security reporting, Dependabot,
  secret scanning, and an explicit CodeQL workflow.

## Source of Truth

The repository is the canonical source of truth for project structure and current
status. Use these files first:

- `docs/REPO_MAP.md`
  - Actual checked-in layout and which modules are in the active build
- `docs/MILESTONES.md`
  - Current milestone status and runtime-validation status
- `docs/EXECUTION_PLAN.md`
  - Current two-week execution plan while Mac runtime testing is pending
- `docs/PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md`
  - Productization roadmap for the CrossOver-competitor path
- `docs/AI_HANDOFF.md`
  - Fastest repo-native resume file for AI coding helpers
- `docs/PROJECT_STATUS.json`
  - Machine-readable project status snapshot for tooling and AI handoff
- `docs/GAME_COMPATIBILITY_CATALOG.json`
  - Machine-readable compatibility catalog snapshot derived from checked-in profiles
- `docs/GAME_COMPATIBILITY_CATALOG.md`
  - Generated Markdown compatibility matrix derived from checked-in profiles
- `docs/MAC_RUNTIME_SMOKE_TEST.md`
  - Canonical first-Mac smoke-test runbook and log-collection checklist
- `CONTRIBUTING.md`
  - Workflow, conventions, and doc-update expectations
- `SECURITY.md`
  - Security reporting and scope
- `profiles/README.md`
  - Compatibility profile vocabulary and file-format notes

The GitHub Wiki is supplementary onboarding material. If the wiki and repository
disagree, follow the repository.

## Requirements

- macOS 13 or newer
- Apple Silicon preferred
- Xcode Command Line Tools
- CMake 3.25 or newer
- Homebrew packages:
  - `cmake`
  - `vulkan-headers`
- Optional local test package:
  - `vulkan-tools`

## Build the ICD

```bash
git clone https://github.com/Fubzei/metalvr-bridge.git
cd metalvr-bridge
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

Output:

- `build/libMetalVRBridge.dylib`
- `vulkan_icd.json`

## Build the Launcher

```bash
cd launcher
bash setup.sh
```

Output:

- `MetalVR Bridge.app`
- `MetalVR-Bridge-Installer.zip`

## First Runtime Targets

```bash
export VK_ICD_FILENAMES=/full/path/to/vulkan_icd.json
vulkaninfo
vkcube
```

## Project Layout

```text
src/
  common/              Shared logging and threading utilities
  shader_translator/   SPIR-V parsing, MSL emission, shader cache
  vulkan_layer/        Active Vulkan ICD implementation
  vr_runtime/          Deferred VR-related code, not in active root build
launcher/              SwiftUI macOS launcher app
profiles/              Compatibility profile defaults, templates, and planning files
host-tests/            Host-only CMake entrypoint for Apple-free checks
scripts/               Mac smoke-test automation and future support scripts
shaders/               Metal shader sources
docs/                  Repository source-of-truth docs
tests/                 Host-side unit tests for Apple-free modules
```

## Related Docs

- `docs/REPO_MAP.md`
- `docs/MILESTONES.md`
- `docs/EXECUTION_PLAN.md`
- `docs/PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md`
- `docs/AI_HANDOFF.md`
- `docs/PROJECT_STATUS.json`
- `docs/GAME_COMPATIBILITY_CATALOG.json`
- `docs/GAME_COMPATIBILITY_CATALOG.md`
- `docs/MAC_RUNTIME_SMOKE_TEST.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `profiles/README.md`
- `launcher/README.md`

## License

MIT License. See `LICENSE`.
