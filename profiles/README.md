# Compatibility Profiles

This directory is the first checked-in step toward a CrossOver-class runtime
layer for MetalVR Bridge.

Profiles in this tree are not blanket compatibility claims. They are structured
runtime intents that can express:

- the preferred renderer/backend strategy for a title
- fallback backends
- install/setup intent for the target prefix
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
- `[install]`
  - `prefix_preset`
  - `packages`
  - `winetricks`
  - `requires_launcher`
  - `notes`
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
also emit the same decision as JSON, a Markdown setup checklist, plus bash or
PowerShell launch/setup scripts for future launcher/runtime consumption.
The checked-in `tools/mvrvb_profile_catalog` tool can also turn the whole
profiles tree into a compatibility matrix so the repo can export one consistent
JSON/report/Markdown view of current planning, experimental, and validated states.
The companion `scripts/export_runtime_plan.ps1` helper can persist that plan as
JSON, a human-readable report, a Markdown setup checklist, launch scripts, and,
when a prefix path is supplied, setup/bootstrap scripts on disk.
The companion `scripts/export_profile_catalog.ps1` helper can persist the
catalog as JSON, a human-readable report, and a Markdown matrix on disk.
The companion `scripts/update_profile_catalog_doc.ps1` helper regenerates the
checked-in `docs/GAME_COMPATIBILITY_CATALOG.md` file from that same catalog.
Those JSON plans can now also be loaded back by the shared runtime-launch-plan
layer, so exported bundles can become an input to future launcher/runtime flows.
That persisted JSON contract is schema-versioned so future consumers can reject
incompatible plan formats cleanly.

## Prefix Presets

The `prefix-presets/` subtree holds `.mvrvb-prefix-preset` files. These are
named bottle-style setup templates that can carry:

- install packages
- Winetricks verbs
- launcher-bootstrap requirements
- environment defaults
- DLL override defaults
- launch-argument defaults

Profiles reference them with `[install] prefix_preset = ...`. The shared
runtime launch-plan and compatibility-catalog layers merge those presets so the
repo can express reusable setup families like `general-game`,
`competitive-shooter`, and `battlenet-shooter` without repeating the same
launcher/package defaults in every profile.

## Auto-Match Rules

- `allow_auto_match = true`
  - profile may be selected automatically by executable, launcher, or store
- `allow_auto_match = false`
  - profile is a template or manual preset and should not win automatic selection
