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
3. Tighten the Mac smoke-test handoff where useful.
   Scope:
   - reduce ambiguity in tester instructions
   - keep the runbook aligned with the launcher and logging surfaces
   Progress:
   - a runnable `scripts/mac_runtime_smoke_test.sh` entry point now builds,
     captures logs, prompts for manual launcher/vkcube outcomes, and packages
     the first-hardware report bundle
   - GitHub now has a dedicated Mac smoke-test issue template for reporting
   - green macOS CI runs now publish downloadable ICD and launcher artifacts

## Exit Criteria

Before the first dedicated Mac test pass, the repo should have:

- checked-in unit tests for parser, emitter, and format table
- CI execution of those tests
- a documented runtime smoke-test checklist
- enough logging to identify the first failing runtime boundary quickly
