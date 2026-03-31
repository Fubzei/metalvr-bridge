# Compatibility Profiles

This directory is the first checked-in step toward a CrossOver-class runtime
layer for MetalVR Bridge.

Profiles in this tree are not blanket compatibility claims. They are structured
runtime intents that can express:

- the preferred renderer/backend strategy for a title
- fallback backends
- sync-mode and high-resolution policy
- MetalFX upscaling intent where that becomes relevant
- latency and competitive-play sensitivity
- launcher and executable matching
- environment variables
- DLL override preferences
- launch arguments

## Status Vocabulary

- `planning`
  - design-time profile, not runtime-validated yet
- `experimental`
  - attempted on hardware, still unstable or incomplete
- `validated`
  - verified on Mac hardware for the stated flow

## File Format

Profiles use a simple `.mvrvb-profile` INI-style format so they can be parsed
without extra dependencies and validated in CI.

Top-level keys:

- `schema_version`
- `profile_id`
- `display_name`
- `status`
- `allow_auto_match`
- `category`
- `default_renderer`
- `fallback_renderers`
- `latency_sensitive`
- `competitive`
- `anti_cheat_risk`
- `notes`

Sections:

- `[match]`
  - `executables`
  - `launchers`
  - `stores`
- `[runtime]`
  - `windows_version`
  - `sync_mode`
  - `high_resolution_mode`
  - `metalfx_upscaling`
- `[env]`
  - arbitrary environment variables
- `[dll_overrides]`
  - Wine/CrossOver override values by DLL name
- `[launch]`
  - `args`

## Current Goal

Right now this directory exists to let the project begin expressing per-title
policy before the full runtime product layer is in place.

The checked-in `tools/mvrvb_runtime_plan_preview` tool can resolve these files
into a concrete backend/env/launch-argument plan without Mac hardware, and can
also emit the same decision as JSON for future launcher/runtime consumption.

## Auto-Match Rules

- `allow_auto_match = true`
  - profile may be selected automatically by executable, launcher, or store
- `allow_auto_match = false`
  - profile is a template or manual preset and should not win automatic selection
