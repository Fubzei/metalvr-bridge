# Milestone Tracker

This file is the canonical repository status document for milestone progress and
runtime validation.

## Phase 2: Windows Games on Mac

| # | Milestone | Repo Status | Canonical Paths | Notes |
|---|-----------|-------------|-----------------|-------|
| 0 | Format table | DONE | `src/vulkan_layer/format_table/` | In active build |
| 1 | SPIR-V to MSL | DONE | `src/shader_translator/spirv/`, `msl_emitter/`, `cache/` | In active build |
| 2 | Resources | DONE | `src/vulkan_layer/resources/` | In active build |
| 3 | Memory | DONE | `src/vulkan_layer/memory/` | In active build |
| 4 | Commands | DONE | `src/vulkan_layer/commands/` | Deferred recording and replay |
| 5 | Pipelines | DONE | `src/vulkan_layer/pipeline/` | Graphics and compute pipelines |
| 6 | Descriptors | DONE | `src/vulkan_layer/descriptors/` | In active build |
| 7 | Sync | DONE | `src/vulkan_layer/sync/` | In active build |
| 8 | Swapchain | DONE | `src/vulkan_layer/swapchain/` | CAMetalLayer path in repo |
| 9 | Transfers | DONE | `src/vulkan_layer/commands/vk_commands.mm` | Integrated into command replay |
| 10 | Wine + DXVK integration | NOT STARTED | no dedicated module yet | Some surface hooks exist, full flow not implemented |
| 11 | Utilities | PARTIAL | `src/common/` | Logging, threading, and memory-pool header exist |
| 12 | Game testing | PARTIAL | `tests/` | Host-side unit harness added; Mac runtime validation is still next |

## Runtime Validation

| Target | Status | Notes |
|--------|--------|-------|
| macOS ICD CI build | PASSING | Compile and link checks are green |
| macOS launcher CI build | PASSING | Swift launcher compiles |
| Host-side unit tests | PASSING | Parser, emitter, format-table, and ICD contract coverage run in CI |
| `vulkaninfo` | NOT TESTED | First real ICD runtime target |
| `vkcube` | NOT TESTED | First real rendering target |
| Launcher triangle test | IMPLEMENTED, NOT VERIFIED ON MAC YET | Implemented in `launcher/BridgeViewModel.swift` |
| Native Vulkan triangle app | NOT IN REPO | Do not document one unless it is added |
| Wine game launch | NOT TESTED | Depends on runtime validation first |

## Deferred or Inactive Tree

These areas exist in the repository but are not part of the active root build:

- `src/vr_runtime/`

Treat them as checked-in but not currently integrated unless the root `CMakeLists.txt`
changes to include them.

## Update Rules

Update this file when:

- a milestone changes state
- runtime-validation status changes
- a deferred area becomes part of the active build
- a new checked-in test or tool directory is added
