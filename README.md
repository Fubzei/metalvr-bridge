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
  format table, ICD contract checks, and extracted transfer-range helpers.
- First-pass runtime observability logs now cover instance, device, pipeline,
  swapchain, submit, present, command-replay boundaries, draw/dispatch
  state-flush summaries, transfer/secondary-command breadcrumbs, and
  synchronization/unsupported-op breadcrumbs.
- There is currently no checked-in `tools/` directory.
- The launcher includes an in-app triangle test and diagnostic log export.
- `scripts/mac_runtime_smoke_test.sh` now automates the first hardware smoke-test
  bundle so Mac-side validation is easier to execute and report.
- Green macOS CI runs now publish downloadable ICD and launcher artifacts.
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
- `docs/MAC_RUNTIME_SMOKE_TEST.md`
  - Canonical first-Mac smoke-test runbook and log-collection checklist
- `CONTRIBUTING.md`
  - Workflow, conventions, and doc-update expectations
- `SECURITY.md`
  - Security reporting and scope

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
scripts/               Mac smoke-test automation and future support scripts
shaders/               Metal shader sources
docs/                  Repository source-of-truth docs
tests/                 Host-side unit tests for Apple-free modules
```

## Related Docs

- `docs/REPO_MAP.md`
- `docs/MILESTONES.md`
- `docs/EXECUTION_PLAN.md`
- `docs/MAC_RUNTIME_SMOKE_TEST.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `launcher/README.md`

## License

MIT License. See `LICENSE`.
