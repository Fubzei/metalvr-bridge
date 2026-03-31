# Compatibility Profiles

This directory is the first checked-in step toward a CrossOver-class runtime
layer for MetalVR Bridge.

Profiles in this tree are not blanket compatibility claims. They are structured
runtime intents that can express:

- the preferred renderer/backend strategy for a title
- fallback backends
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
- `[env]`
  - arbitrary environment variables
- `[dll_overrides]`
  - Wine/CrossOver override values by DLL name
- `[launch]`
  - `args`

## Current Goal

Right now this directory exists to let the project begin expressing per-title
policy before the full runtime product layer is in place.
