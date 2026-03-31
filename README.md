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
  `tests/unit/compatibility_profile_tests.cpp`.
- A checked-in compatibility catalog layer now lives in
  `src/common/compatibility_catalog.*`, turning those checked-in profiles into a
  machine-readable and human-readable game/status database for future launcher,
  wiki, and runtime surfaces.
- A checked-in runtime launch-plan builder now lives in
  `src/common/runtime_launch_plan.*`, turning those profiles into backend,
  fallback, env, DLL-override, and launch-argument decisions that a launcher
  can consume.
- A checked-in runtime launch-command materializer now lives in
  `src/common/runtime_launch_command.*`, turning those plans into concrete
  Wine-style command, environment, and wrapper-script output for future runtime
  or launcher glue.
- A checked-in developer preview tool now lives in `tools/` so those runtime
  plans can be resolved and inspected locally before the launcher wiring is
  complete, including machine-readable JSON export plus bash and PowerShell
  wrapper-script generation for future runtime or launcher integration.
- A checked-in compatibility catalog tool now lives in `tools/` so the repo can
  export a consistent compatibility matrix from the same checked-in profiles the
  runtime layer consumes, including JSON and Markdown views for docs/wiki reuse.
- A checked-in export helper now lives in `scripts/export_runtime_plan.ps1` so
  persisted JSON, human-readable reports, and wrapper scripts can be generated
  from the same shared contract without Mac hardware.
- A companion export helper now lives in `scripts/export_profile_catalog.ps1` so
  JSON, report, and Markdown versions of the compatibility catalog can be generated
  without Mac hardware.
- Those persisted JSON plans can now also be loaded back through the shared
  launch-plan layer, so future runtime or launcher glue can consume exported
  plans without re-solving profiles.
- The persisted launch-plan JSON contract is now schema-versioned so future
  consumers can validate compatibility instead of guessing.
- First-pass runtime observability logs now cover instance, device, pipeline,
  swapchain, submit, present, command-replay boundaries, draw/dispatch
  state-flush summaries, transfer/secondary-command breadcrumbs, and
  synchronization/unsupported-op breadcrumbs.
- The launcher includes an in-app triangle test and diagnostic log export.
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
- `docs/GAME_COMPATIBILITY_CATALOG.md`
- `docs/MAC_RUNTIME_SMOKE_TEST.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `profiles/README.md`
- `launcher/README.md`

## License

MIT License. See `LICENSE`.
