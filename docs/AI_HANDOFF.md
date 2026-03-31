# AI Handoff

This is the fastest repo-native handoff for any AI coding helper resuming work on
MetalVR Bridge.

## Current State

- The project is past Milestone 9 and not yet into real Milestone 10 execution.
- The repo builds cleanly in macOS CI.
- The launcher builds in macOS CI.
- The Apple-free host test surface, profile lint, and product-layer tooling all run
  locally without Mac hardware.
- The next decisive gate is still real Mac runtime validation:
  - launcher triangle
  - `vulkaninfo`
  - `vkcube`

## Proven Today

- SPIR-V parser, MSL emitter, format-table logic, ICD contract checks, and extracted
  transfer helpers are covered in host-side tests.
- Compatibility profiles, prefix presets, compatibility catalog export, launch-plan
  generation, launch/setup script generation, and policy lint are all checked in and
  host-verified.
- The launcher now consumes the bundled `docs/PROJECT_STATUS.json` snapshot so the
  Mac app reflects the same current phase and next gate tracked in the repo docs.
- The launcher now also consumes the bundled `docs/GAME_COMPATIBILITY_CATALOG.json`
  snapshot so the app can summarize checked-in profile coverage without rerunning tools.
- The launcher now imports or consumes bundled `launch-plan.json` previews exported
  from the shared runtime-plan tooling, so product policy can be inspected in-app.
- The launcher now also imports `bundle-manifest.json` exports from the shared
  runtime-bundle tooling, follows them to the referenced launch-plan payload,
  previews checklist/setup/launch/lint/catalog surfaces from the same bundle,
  can reveal, open, export, or copy execution-prep snippets from the imported
  bundle assets in the app, and now derives guided runtime actions from the same
  shared plan contract.
- A tester/runtime handoff bundle can be exported locally without Mac hardware.

## Not Yet Proven

- Real Vulkan loader behavior on macOS
- Real swapchain/present behavior on macOS
- `vulkaninfo`
- `vkcube`
- real Wine + DXVK game launch on Mac hardware

## Read Order

Read these files in order before changing code:

1. `docs/MILESTONES.md`
2. `docs/PROJECT_STATUS.json`
3. `docs/EXECUTION_PLAN.md`
4. `docs/REPO_MAP.md`
5. `docs/PHASE4_CROSSOVER_COMPETITOR_ROADMAP.md`
6. `docs/GAME_COMPATIBILITY_CATALOG.json`
7. `docs/GAME_COMPATIBILITY_CATALOG.md`
8. `docs/MAC_RUNTIME_SMOKE_TEST.md`
9. `profiles/README.md`
10. `README.md`
11. `CONTRIBUTING.md`

## Most Useful Commands

Host-safe verification:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_host_checks.ps1
powershell -ExecutionPolicy Bypass -File scripts\run_profile_lint.ps1
```

Compatibility/runtime exports:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\export_profile_catalog.ps1
powershell -ExecutionPolicy Bypass -File scripts\export_runtime_plan.ps1 -Executable "C:/Games/Overwatch/Overwatch.exe" -Launcher "Battle.net" -Store "battlenet" -PrefixPath "C:/Prefixes/Overwatch"
powershell -ExecutionPolicy Bypass -File scripts\export_runtime_bundle.ps1 -Executable "C:/Games/Overwatch/Overwatch.exe" -Launcher "Battle.net" -Store "battlenet" -PrefixPath "C:/Prefixes/Overwatch"
```

Mac-side runtime validation when hardware is available:

```bash
bash scripts/mac_runtime_smoke_test.sh
```

## Best Next Non-Mac Priorities

1. Promote the shared runtime-plan contract into richer launcher-side execution-prep
   and handoff flows now that import, preview, guided actions, and copy helpers exist.
2. Keep the compatibility profile and prefix-preset governance strong as more games or
   templates are added.
3. Keep the repo and generated docs aligned whenever the product/runtime surface changes.

## Mac Arrival Priorities

1. Run `scripts/mac_runtime_smoke_test.sh`.
2. Stop at the first failing boundary.
3. Attach the generated smoke-test bundle.
4. Only move toward Wine/DXVK game execution after launcher triangle, `vulkaninfo`, and
   `vkcube` pass.

## Guardrails

- Do not claim runtime compatibility for a title until it is actually tested on Mac hardware.
- Do not let the wiki drift away from the checked-in repo docs.
- Update this file when the current phase, proven surface, or next-step ordering changes.
