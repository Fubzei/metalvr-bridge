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
  - checked-in compatibility profiles for future runtime and launcher policy

## Directories Not Currently Checked In

The root build still has an optional hook for:

- `tools/`

Do not describe that directory as present unless it is added in the same change.

## Module Map

### `src/common`

- logging
- threading
- compatibility-profile parser, loader, and selector
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
- runtime checks, triangle test, and log export
- packaging script

### `docs`

- source-of-truth repo status and execution docs
- Mac runtime smoke-test runbook
- CrossOver-competitor productization roadmap

### `profiles`

- compatibility profile defaults, templates, and planning files
- intended to drive future runtime backend selection, launch args, env vars,
  DLL override policy, sync mode, high-resolution mode, and MetalFX-upscaling intent
- validated in CI through `src/common/compatibility_profile.*`

### `scripts`

- Mac smoke-test automation and bundle collection
- Windows/host-side local test entrypoint for Apple-free modules
- intended to reduce ambiguity for the first dedicated hardware run

### `host-tests`

- dedicated CMake entrypoint for non-Apple local checks
- builds `src/common`, pure shader-translation modules, format-table helpers,
  transfer helpers, and the shared GoogleTest unit surface without the Metal ICD target

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
