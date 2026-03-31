# Phase 4: CrossOver-Competitor Roadmap

This document defines what MetalVR Bridge would need to become in order to
compete with CrossOver as a Windows-games-on-Mac product, not just as a Vulkan
translation backend.

## Goal

The target is not merely "some Vulkan demos render on macOS." The target is a
product that can:

- launch and manage Windows game environments reliably
- choose sane rendering/runtime defaults per title
- support both breadth and performance across modern games
- provide diagnostics and compatibility reporting that improve over time
- eventually approach low-latency, high-framerate play for competitive titles

## Current Gap

The repository today is strongest in one layer:

- `DXVK or native Vulkan -> Vulkan -> MetalVR Bridge -> Metal`

CrossOver is broader. To compete with it, MetalVR Bridge needs a product layer
around the renderer:

- runtime and prefix setup
- backend selection
- per-game compatibility policy
- installer and launcher orchestration
- performance tuning
- compatibility operations and reporting

## Phase 4 Tracks

### 4.0 Runtime Product Foundation

Status: `STARTED`

Scope:

- checked-in compatibility profiles
- profile parser and validation in CI
- auto-selection of the best profile by executable, launcher, or store identity
- launcher/runtime plumbing for profile selection
- install/runtime configuration model
- issue templates and reporting flow for game outcomes

Exit criteria:

- the repo has a validated compatibility-profile schema
- the launcher or future runtime layer can resolve a profile by game identity
- profile-driven env vars, DLL overrides, and launch arguments can be emitted
- runtime policy can express at least:
  - backend preference and fallbacks
  - sync mode
  - high-resolution mode
  - Windows-version intent
  - MetalFX upscaling intent

### 4.1 Backend Breadth

Status: `PLANNED`

Scope:

- complete `Wine + DXVK + MetalVR Bridge` flow
- support a real `VKD3D-Proton` path for D3D12-era games
- design backend fallback behavior per profile
- keep room for direct or alternate D3D-to-Metal paths when they are the better fit

### 4.2 Compatibility Database and Installer Orchestration

Status: `PLANNED`

Scope:

- game identity matching
- launcher/store matching
- bottle/prefix presets
- install notes and required components
- compatibility states: planning, experimental, validated

Progress checked in so far:

- `src/common/compatibility_catalog.*`
  - host-safe compatibility matrix builder derived from checked-in profile files
- `tools/mvrvb_profile_catalog`
  - export path for JSON, report, or Markdown compatibility outputs
- `scripts/export_profile_catalog.ps1`
  - host-safe helper for generating compatibility-catalog bundles on disk

### 4.3 Performance and Latency Engineering

Status: `PLANNED`

Scope:

- shader/pipeline cache strategy
- frame pacing and present behavior
- CPU overhead reduction in hot replay paths
- low-latency tuning for competitive titles
- startup hitch and compile-stutter reduction
- CrossOver-style exposed knobs such as sync mode and high-resolution policy

### 4.4 Competitive-Game Hardening

Status: `PLANNED`

Scope:

- raw-input and fullscreen behavior
- low-latency runtime defaults
- network-sensitive and matchmaking-safe launch settings
- anti-cheat risk tracking
- fast regression loops for competitive shooters

### 4.5 Supportability and Operations

Status: `PLANNED`

Scope:

- reproducible bug reports
- game compatibility reports
- profile changes tied to evidence
- diagnostics bundles for launcher/runtime/game failures
- eventually, artifact and release packaging for testers

## First Concrete Deliverables

The first checked-in Phase 4 deliverables are:

- `src/common/compatibility_profile.*`
  - CI-validated parser plus auto-selection helpers for runtime compatibility profiles
  - now includes install/setup policy fields for future bottle or prefix orchestration
- `src/common/prefix_preset.*`
  - CI-validated parser and loader for named bottle-style prefix presets that
    carry shared package, launcher-bootstrap, environment, DLL-override, and
    launch-argument defaults
- `src/common/compatibility_catalog.*`
  - host-safe compatibility database builder for JSON/report export from checked-in
    profiles plus merged prefix-preset setup intent
- `src/common/profile_lint.*`
  - host-safe governance layer for missing preset references, duplicate IDs,
    and ambiguous auto-match policy
- `src/common/runtime_launch_plan.*`
  - host-safe launch-plan builder that resolves backend/install/env/args/runtime policy
    from profiles plus named prefix presets
  - now also emits a Markdown setup checklist for tester-facing install/bootstrap guidance
- `src/common/runtime_launch_command.*`
  - host-safe launch-command materializer that turns a resolved plan into runnable
    command, environment, and wrapper-script output
- `src/common/runtime_setup_command.*`
  - host-safe setup/bootstrap materializer that turns install policy into
    automated prefix actions, manual follow-up notes, and generated setup scripts
- `tools/mvrvb_runtime_plan_preview`
  - host-safe preview entry point for inspecting merged launch decisions without Mac hardware,
    with JSON output, checklist output, launch/setup script generation, and
    persisted file export that future launcher/runtime code can reuse
- `scripts/export_runtime_plan.ps1`
  - host-safe helper for generating JSON, report, checklist, launch-script, and
    setup-script bundles on disk from the shared contract
- `scripts/run_profile_lint.ps1`
  - direct helper for validating profile and prefix-preset policy without a full host test run
- `scripts/export_runtime_bundle.ps1`
  - one-command tester handoff bundle containing the launch plan, setup scripts,
  compatibility catalog, lint report, and manifest
- `docs/AI_HANDOFF.md`
  - canonical checked-in resume brief for any AI coding helper
- `docs/PROJECT_STATUS.json`
  - machine-readable status snapshot for tooling and AI handoff
- `scripts/update_ai_handoff_doc.ps1` and `scripts/export_ai_handoff_bundle.ps1`
  - direct update and export path for a carry-forward AI handoff package
- persisted launch-plan JSON now round-trips through the shared runtime-plan layer,
  so future launcher/runtime code can consume exported bundles directly
- the persisted launch-plan JSON contract is now schema-versioned for future
  compatibility checks
- `profiles/`
  - checked-in defaults, templates, planning profiles, and prefix presets with
    runtime/setup-policy knobs
- `tests/unit/compatibility_catalog_tests.cpp`
  - regression coverage for compatibility-catalog summaries and entry content
- `tests/unit/compatibility_profile_tests.cpp`
  - regression coverage for parser behavior, checked-in files, and profile selection
- `tests/unit/runtime_launch_plan_tests.cpp`
  - regression coverage for merged launch-plan decisions
- `.github/ISSUE_TEMPLATE/game-compatibility-report.yml`
  - standardized reporting for game-level outcomes

These do not make the project CrossOver-competitive yet. They create the
structure needed to get there without waiting for the full runtime layer first.

## Strategic Rule

Do not treat "renderer works" as the finish line.

To compete with CrossOver, MetalVR Bridge must become:

1. a strong renderer backend
2. a game-aware runtime product
3. a compatibility and performance operations loop
