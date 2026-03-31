# Execution Plan

This file tracks the current two-week execution order while dedicated Mac
hardware is still pending.

## Current Constraint

The repository now builds cleanly in macOS CI, but runtime validation still
requires a real Mac for:

- `vulkaninfo`
- `vkcube`
- launcher triangle verification
- Xcode Metal frame capture
- Wine and DXVK game testing

## Completed Foundation

These items are now checked in and verified in CI:

1. Host-side unit coverage for Apple-free modules.
   Scope:
   - SPIR-V parser
   - SPIR-V to MSL emitter
   - format table
2. CI execution of those tests before runtime validation.
3. ICD contract checks.
   Scope:
   - exported symbol coverage
   - manifest sanity checks
   - dispatch-table consistency
4. First-pass runtime observability.
   Scope:
   - instance and physical-device discovery
   - logical-device creation
   - graphics and compute pipeline creation
   - swapchain create, acquire, and present
   - queue submit, idle, command-replay boundaries, draw/dispatch state-flush summaries,
     transfer/secondary-command breadcrumbs, and synchronization/unsupported-op breadcrumbs
5. Mac runtime execution kit.
   Scope:
   - exact launcher, `vulkaninfo`, and `vkcube` order
   - log environment variables and output locations
   - failure triage checklist and report bundle

## Next Focus

1. Continue expanding runtime observability into failure triage before the first Mac smoke test.
   Scope:
   - extend concise failure summaries into any remaining low-visibility replay paths
   - keep the first broken runtime boundary obvious in the exported log
2. Separate more Apple-free logic into host-testable units where practical.
   Progress:
   - transfer range, mip, slice, and buffer-span helpers now live in a pure C++
     module with direct unit coverage
   - transfer-region uniform, viewport, and scissor geometry now shares that
     same host-tested helper surface
   - transfer-format compatibility and pipeline-kind selection now shares that
     same host-tested helper surface
   - `host-tests/CMakeLists.txt` plus `scripts/run_host_checks.ps1` now provide
     a dedicated local Windows/LLVM entrypoint for the Apple-free unit surface
3. Tighten the Mac smoke-test handoff where useful.
   Scope:
   - reduce ambiguity in tester instructions
   - keep the runbook aligned with the launcher and logging surfaces
   Progress:
   - a runnable `scripts/mac_runtime_smoke_test.sh` entry point now builds,
     captures logs, prompts for manual launcher/vkcube outcomes, and packages
     the first-hardware report bundle
   - GitHub now has a dedicated Mac smoke-test issue template for reporting
   - green macOS CI runs now publish downloadable ICD and launcher installer artifacts
4. Start the CrossOver-competitor product layer with host-safe pieces.
   Scope:
   - checked-in compatibility profiles
   - CI validation of profile files
   - automatic profile selection by executable, launcher, and store
   - future launcher/runtime integration points for backend, env, and launch policy
   Progress:
   - `src/common/compatibility_profile.*` now parses the profile format, including
     install/setup policy for future bottle or prefix orchestration
   - `src/common/compatibility_catalog.*` now turns those checked-in profiles into
     a compatibility matrix the repo can export as JSON, a human-readable report,
     or a Markdown matrix
   - `profiles/` now holds defaults, templates, and planning profiles
   - `tests/unit/compatibility_profile_tests.cpp` validates the parser and the
     checked-in profile files
   - `tests/unit/compatibility_catalog_tests.cpp` validates the checked-in
     compatibility catalog summary and entry data
   - the profile layer now models backend fallback, sync mode, high-resolution mode,
     MetalFX-upscaling intent, install/setup policy, and auto-selection rules
   - `tools/mvrvb_profile_catalog` now exports the compatibility matrix from the
     same checked-in profiles the launcher/runtime layer will use
   - `src/common/runtime_launch_plan.*` now turns those profiles into a concrete
     launch plan with backend, fallbacks, install/setup policy, env vars,
     DLL overrides, launch args, and runtime-policy settings
   - `src/common/runtime_launch_command.*` now materializes that plan into a
     runnable Wine-style command with merged environment metadata and wrapper-script
     output for future runtime or launcher glue
   - `tools/mvrvb_runtime_plan_preview` now resolves and prints that launch plan
     from the command line so product/runtime policy can be exercised without
     waiting on launcher wiring or Mac hardware
   - the same preview tool now has a machine-readable JSON mode so future
     launcher/runtime code can consume the shared launch-plan contract directly
   - the preview tool can now also emit bash or PowerShell wrapper scripts from
     either a fresh profile query or a previously exported JSON plan
   - the same launch-plan layer can now emit a Markdown setup checklist so the
     first Mac tester gets install/bootstrap guidance alongside launch details
   - `scripts/export_runtime_plan.ps1` now produces persisted JSON, a human-readable
     report, a Markdown setup checklist, and launch-script bundles from that same
     shared contract for future runtime glue
   - `scripts/export_profile_catalog.ps1` now exports the compatibility matrix as
     both JSON and a human-readable report bundle without Mac hardware
   - `scripts/update_profile_catalog_doc.ps1` now regenerates the checked-in
     `docs/GAME_COMPATIBILITY_CATALOG.md` file from the same profile catalog data
   - persisted JSON launch-plan bundles can now be loaded back into
     `src/common/runtime_launch_plan.*`, so future runtime or launcher glue can
     consume exported plans without re-solving compatibility profiles
   - the persisted JSON contract is now schema-versioned for safer future
     launcher/runtime integration
   - the export helper now also emits bash and PowerShell wrapper scripts so a
     future runtime wrapper does not need to re-implement launch-command assembly
   - GitHub now has a dedicated game-compatibility report template

## Exit Criteria

Before the first dedicated Mac test pass, the repo should have:

- checked-in unit tests for parser, emitter, and format table
- CI execution of those tests
- a documented runtime smoke-test checklist
- enough logging to identify the first failing runtime boundary quickly
