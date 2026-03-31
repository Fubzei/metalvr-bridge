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
| 9 | Transfers | DONE | `src/vulkan_layer/commands/` | Integrated into command replay with host-tested helper coverage |
| 10 | Wine + DXVK integration | NOT STARTED | no dedicated module yet | Some surface hooks exist, full flow not implemented |
| 11 | Utilities | PARTIAL | `src/common/` | Logging, threading, and memory-pool header exist |
| 12 | Game testing | PARTIAL | `tests/` | Host-side unit harness covers parser, emitter, format table, ICD contracts, and transfer helpers; Mac runtime validation is still next |

## Phase 4: Productization and CrossOver-Competitor Path

| # | Milestone | Repo Status | Canonical Paths | Notes |
|---|-----------|-------------|-----------------|-------|
| 13 | Compatibility profile system | STARTED | `src/common/compatibility_profile.*`, `src/common/prefix_preset.*`, `profiles/`, `tests/unit/compatibility_profile_tests.cpp`, `tests/unit/prefix_preset_tests.cpp` | Parser, runtime knobs, install/setup policy, prefix-preset validation, and auto-selection helper are checked in |
| 14 | Runtime launcher/product integration | STARTED | `src/common/runtime_launch_plan.*`, `src/common/runtime_launch_command.*`, `tools/`, `launcher/`, future runtime glue | Launch-plan builder, launch-command materializer, preview tool, and wrapper-script export are checked in, now including install/setup metadata; the launcher now consumes the bundled project-status snapshot and deeper runtime-bundle wiring is next |
| 15 | Backend breadth and fallback strategy | PLANNED | future runtime/backend glue | DXVK, VKD3D-Proton, and fallback selection story |
| 16 | Compatibility database and installer flow | STARTED | `profiles/`, `src/common/compatibility_catalog.*`, `src/common/prefix_preset.*`, `tools/`, launcher, future runtime tooling | Compatibility catalog/reporting plus merged prefix-preset setup intent are checked in; fuller installer flow is still pending |
| 17 | Performance and latency tuning | PLANNED | runtime + renderer hot paths | Frametime stability and low-latency work |
| 18 | Competitive-game hardening | PLANNED | profiles + runtime + renderer | Separate bar for shooters and anti-cheat-sensitive titles |

## Runtime Validation

| Target | Status | Notes |
|--------|--------|-------|
| macOS ICD CI build | PASSING | Compile and link checks are green |
| macOS launcher CI build | PASSING | Swift launcher compiles |
| Host-side unit tests | PASSING | Parser, emitter, format-table, ICD contract, and transfer-helper coverage including region geometry and transfer-format classification run in CI |
| Local host-side checks | IN PLACE | `host-tests/CMakeLists.txt` and `scripts/run_host_checks.ps1` run the Apple-free unit surface on Windows/LLVM |
| Compatibility profile validation | IN PLACE | Checked-in profile parser, runtime knobs, install/setup policy, sample profiles, checked-in prefix presets, and auto-selection helper are covered in CI |
| Compatibility catalog | IN PLACE | `src/common/compatibility_catalog.*`, `tools/mvrvb_profile_catalog`, and `scripts/export_profile_catalog.ps1` turn checked-in profiles plus merged prefix-preset setup intent into JSON/report/Markdown compatibility outputs without Mac hardware |
| Compatibility policy lint | IN PLACE | `src/common/profile_lint.*`, `tools/mvrvb_profile_lint`, and `scripts/run_profile_lint.ps1` catch missing preset references, duplicate IDs, and ambiguous auto-match policy without Mac hardware |
| Runtime tester bundle export | IN PLACE | `scripts/export_runtime_bundle.ps1` packages launch-plan, setup-script, compatibility-catalog, and lint outputs into a single handoff directory without Mac hardware |
| AI handoff documentation | IN PLACE | `docs/AI_HANDOFF.md`, `scripts/update_ai_handoff_doc.ps1`, and `scripts/export_ai_handoff_bundle.ps1` provide a canonical AI resume path and handoff bundle without Mac hardware |
| Machine-readable project status | IN PLACE | `docs/PROJECT_STATUS.json` and `scripts/update_project_status_json.ps1` provide a machine-readable state snapshot for tooling and AI handoff without Mac hardware |
| Launcher project-status snapshot | IN PLACE | `launcher/ProjectStatus.swift`, `launcher/BridgeViewModel.swift`, `launcher/ContentView.swift`, and `launcher/setup.sh` now bundle and surface `docs/PROJECT_STATUS.json` in the macOS launcher UI and exported diagnostics |
| Runtime launch-plan builder | IN PLACE | `src/common/runtime_launch_plan.*` resolves profiles plus checked-in prefix presets into backend/install/env/args/runtime policy, can persist and reload schema-versioned JSON plans, and can emit Markdown setup checklists in host tests |
| Runtime launch-command materializer | IN PLACE | `src/common/runtime_launch_command.*` turns a resolved plan into a Wine-style command, environment block, and bash/PowerShell wrapper scripts without Mac hardware |
| Runtime setup-command materializer | IN PLACE | `src/common/runtime_setup_command.*` turns install policy into bootstrap actions, manual follow-up notes, and bash/PowerShell setup scripts without Mac hardware |
| Runtime plan preview tool | IN PLACE | `tools/mvrvb_runtime_plan_preview` resolves a profile query into a concrete launch summary, JSON launch-plan payload, Markdown setup checklist, launch wrapper script, or setup/bootstrap script, can persist those outputs to disk, and can reload exported JSON plans without Mac hardware |
| Runtime observability | IN PLACE | Instance, device, pipeline, swapchain, submit, present, replay-boundary, state-flush summary, transfer/secondary replay, and synchronization/unsupported-op logs are checked in |
| Mac runtime runbook | IN PLACE | `docs/MAC_RUNTIME_SMOKE_TEST.md` and `scripts/mac_runtime_smoke_test.sh` define the first hardware validation pass |
| CI build artifacts | IN PLACE | Green macOS runs publish a portable ICD tarball and launcher installer artifact |
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
