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

## Next Focus

1. Improve runtime observability before the first Mac smoke test.
   Scope:
   - structured logs for instance, device, pipeline, swapchain, submit, and present
2. Separate more Apple-free logic into host-testable units where practical.
3. Prepare the Mac runtime execution kit.
   Scope:
   - exact `vulkaninfo` and `vkcube` runbook
   - log collection instructions
   - failure triage checklist

## Exit Criteria

Before the first dedicated Mac test pass, the repo should have:

- checked-in unit tests for parser, emitter, and format table
- CI execution of those tests
- a documented runtime smoke-test checklist
- enough logging to identify the first failing runtime boundary quickly
