# Repository Map

This file describes the actual checked-in repository layout and the current active
build surface.

## Top-Level Layout

```text
.github/
docs/
launcher/
shaders/
src/
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

These directories are part of the current build and should be treated as the active
implementation surface.

## Checked-In but Not in the Active Root Build

- `src/vr_runtime`

That subtree exists in the repo, but it is not currently added by the root
`CMakeLists.txt`.

## Directories Not Currently Checked In

The root build has optional hooks for these directories if they are created later:

- `tests/`
- `tools/`

Do not describe those directories as present unless they are added in the same change.

## Module Map

### `src/common`

- logging
- threading
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
- sync and swapchain

### `launcher`

- SwiftUI app entry point
- main UI
- runtime checks, triangle test, and log export
- packaging script

### `shaders`

- Metal shader sources used by the project

## Ownership Model

GitHub ownership is defined in `CODEOWNERS`.
If module boundaries change, update both this file and `CODEOWNERS`.

## Update Rules

Update this file when:

- a top-level directory is added or removed
- the root build starts or stops including a module
- a module changes purpose enough that the current description is no longer accurate
