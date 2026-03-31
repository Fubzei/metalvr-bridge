# Repository Map

This file describes the actual checked-in repository layout and the current active
build surface.

## Top-Level Layout

```text
.github/
docs/
host-tests/
launcher/
profiles/
scripts/
shaders/
src/
tests/
tools/
CMakeLists.txt
README.md
CONTRIBUTING.md
SECURITY.md
vulkan_icd.json
```

## Active Root Build

The root `CMakeLists.txt` currently adds these directories:

- `src/common`
- `src/shader_translator`
- `src/vulkan_layer`

When `MVRVB_BUILD_TESTS` is enabled, the root build also adds:

- `tests`

These directories are part of the current build and should be treated as the active
implementation surface.

## Checked-In but Not in the Active Root Build

- `src/vr_runtime`

That subtree exists in the repo, but it is not currently added by the root
`CMakeLists.txt`.

## Checked-In Test Surface

- `tests/`
  - GoogleTest-based unit coverage for parser, emitter, format-table logic,
    ICD contract checks, and extracted transfer helper logic including region geometry
    and transfer-format classification
- `host-tests/`
  - non-Apple CMake entrypoint for running the Apple-free test surface locally
- `profiles/`
  - checked-in compatibility profiles and prefix presets for future runtime and launcher policy
- `tools/`
  - checked-in host-safe preview tool for resolving runtime launch plans from
    profile inputs and writing launch/setup artifacts to disk

## Module Map

### `src/common`

- logging
- threading
- compatibility-profile parser, loader, and selector
- prefix-preset parser and loader for bottle-style setup defaults
- compatibility-catalog builder for machine-readable and report-style profile indexing
- compatibility-policy lint layer for profile and prefix-preset governance
- runtime launch-plan builder for launcher/runtime consumption
- runtime launch-command materializer for wrapper-script generation
- runtime setup-command materializer for bootstrap/setup script generation
- shared utility headers

### `src/shader_translator`

- SPIR-V parser
- MSL emitter
- shader cache
- geometry-emulation support

### `src/vulkan_layer`

- ICD negotiation and dispatch table
- format table
- device, memory, resources
- commands, pipelines, descriptors
- pure transfer helpers in `commands/transfer_utils.*`
- sync and swapchain
- first-smoke-test observability across instance, device, pipeline, submit,
  present, command-replay boundaries, draw/dispatch state-flush summaries,
  transfer/secondary-command breadcrumbs, and synchronization/unsupported-op
  breadcrumbs

### `launcher`

- SwiftUI app entry point
- main UI
- runtime checks, triangle test, bundled project-status and compatibility-catalog
  snapshot readers, imported or bundled runtime-plan preview reader, and log export
- packaging script

### `docs`

- source-of-truth repo status and execution docs
- checked-in AI handoff doc for fast coding-agent resume
- checked-in machine-readable project status snapshot
- checked-in machine-readable compatibility catalog snapshot
- generated compatibility catalog doc synced from checked-in profiles
- Mac runtime smoke-test runbook
- CrossOver-competitor productization roadmap

### `profiles`

- compatibility profile defaults, templates, and planning files
- prefix-preset files for reusable package, launcher-bootstrap, env, DLL-override,
  and launch-argument defaults
- intended to drive future runtime backend selection, launch args, env vars,
  DLL override policy, install/setup policy, sync mode, high-resolution mode,
  and MetalFX-upscaling intent
- validated in CI through `src/common/compatibility_profile.*` and
  `src/common/prefix_preset.*`

### `scripts`

- Mac smoke-test automation and bundle collection
- Windows/host-side local test entrypoint for Apple-free modules
- profile-catalog export helper for JSON and human-readable compatibility reports
- profile-catalog doc sync helper for the generated Markdown compatibility matrix
- direct profile-lint helper for policy governance without a full test run
- launch-plan export helper for JSON, human-readable reports, launch scripts,
  and setup/bootstrap scripts
- runtime bundle export helper for one-directory tester handoff packages
- AI handoff doc update and bundle export helpers for coding-agent continuity
- machine-readable project-status update helper for tooling continuity
- machine-readable compatibility-catalog update helper for tooling continuity
- intended to reduce ambiguity for the first dedicated hardware run

### `host-tests`

- dedicated CMake entrypoint for non-Apple local checks
- builds `src/common`, pure shader-translation modules, format-table helpers,
  transfer helpers, the runtime-plan preview tool, and the shared GoogleTest unit
  surface without the Metal ICD target

### `tools`

- host-safe developer tooling
- `mvrvb_profile_catalog` builds a compatibility matrix from checked-in profiles
  and can emit a human-readable report, machine-readable JSON, or Markdown matrix
- `mvrvb_profile_lint` validates checked-in profiles and prefix presets for
  missing references, duplicate IDs, and ambiguous auto-match rules
- `mvrvb_runtime_plan_preview` resolves a checked-in compatibility profile
  selection into backend, env, DLL override, and launch-argument output
- supports human-readable summaries, machine-readable JSON output, Markdown setup
  checklists, bash/PowerShell launch-script generation, and bash/PowerShell
  setup-script generation for future launcher/runtime consumption
- verified in host-side checks for stdout, file-output, persisted-plan reload,
  and script-generation modes

### `shaders`

- Metal shader sources used by the project

### `tests`

- host-side unit tests
- parser, emitter, format-table, ICD contract, and transfer-helper regression coverage,
  including region-geometry math and transfer-format classification used by blit/resolve replay
- intended to run in CI before Mac runtime validation

## Ownership Model

GitHub ownership is defined in `CODEOWNERS`.
If module boundaries change, update both this file and `CODEOWNERS`.

## Update Rules

Update this file when:

- a top-level directory is added or removed
- the root build starts or stops including a module
- a module changes purpose enough that the current description is no longer accurate
