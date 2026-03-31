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
- launcher/runtime plumbing for profile selection
- install/runtime configuration model
- issue templates and reporting flow for game outcomes

Exit criteria:

- the repo has a validated compatibility-profile schema
- the launcher or future runtime layer can resolve a profile by game identity
- profile-driven env vars, DLL overrides, and launch arguments can be emitted

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

### 4.3 Performance and Latency Engineering

Status: `PLANNED`

Scope:

- shader/pipeline cache strategy
- frame pacing and present behavior
- CPU overhead reduction in hot replay paths
- low-latency tuning for competitive titles
- startup hitch and compile-stutter reduction

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
  - CI-validated parser for runtime compatibility profiles
- `profiles/`
  - checked-in defaults, templates, and planning profiles
- `tests/unit/compatibility_profile_tests.cpp`
  - regression coverage for the parser and checked-in profile files
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
