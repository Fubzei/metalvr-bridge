# Contributing to MetalVR Bridge

## Working Agreement

This project prioritizes structure, clear ownership, and information consistency.
If a change affects how the repo is built, organized, tested, or handed off, update
the matching source-of-truth document in the same pull request.

## Source-of-Truth Files

Use and maintain these files as the canonical references:

- `README.md`
  - Stable project overview and quick-start information
- `docs/REPO_MAP.md`
  - Checked-in layout and active-build surface
- `docs/MILESTONES.md`
  - Milestone and runtime-validation status
- `SECURITY.md`
  - Security policy and reporting
- `launcher/README.md`
  - Launcher packaging and handoff flow

## Development Workflow

1. Start from the latest `main`.
2. Create a focused branch.
3. Keep changes scoped to one problem or milestone step.
4. Open a pull request.
5. Wait for the required macOS checks to pass.
6. Merge through the protected `main` flow.

## Commit Style

Prefer short imperative subjects with a clear area prefix.

Examples:

- `Fix: resolve ICD linker symbols`
- `Docs: align README with repo structure`
- `Security: add policy, Dependabot, and harden CI permissions`
- `Milestone 10: start Wine integration scaffolding`

## Code and File Conventions

- Use `.cpp` and `.h` for pure C++.
- Use `.mm` when the file talks to Metal, AppKit, CoreFoundation, or other Apple APIs.
- Keep Swift confined to `launcher/`.
- Keep Metal shader sources in `shaders/`.
- Follow the current logging macros in `src/common/logging.h`:
  - `MVRVB_LOG_TRACE`
  - `MVRVB_LOG_DEBUG`
  - `MVRVB_LOG_INFO`
  - `MVRVB_LOG_WARN`
  - `MVRVB_LOG_ERROR`
  - `MVRVB_LOG_FATAL`

## Documentation Update Rules

Update docs when the matching area changes:

- Update `README.md` when the high-level project story, quick start, or repo reality changes.
- Update `docs/REPO_MAP.md` when directories, active targets, or ownership boundaries change.
- Update `docs/MILESTONES.md` when milestone status or runtime-validation status changes.
- Update `SECURITY.md` when reporting or security posture changes.
- Update `launcher/README.md` when the launcher packaging flow changes.

## Pull Request Checklist

Before submitting a PR:

- [ ] The change is scoped and clearly named.
- [ ] Any new Vulkan entry point is wired into the ICD dispatch table if required.
- [ ] Any duplicate stub or symbol situation has been checked.
- [ ] Any structure or status change is reflected in the source-of-truth docs.
- [ ] The PR summary explains what changed, what was verified, and what still remains.

## Notes on Repo Reality

- `src/vr_runtime/` is checked in but not part of the active root build today.
- Transfer work currently lives inside `src/vulkan_layer/commands/vk_commands.mm`.
- There is no checked-in `tests/` directory today, so avoid documenting tests as if
  they already exist unless they are added in the same PR.
