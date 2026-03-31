<!--
  Generated from checked-in .mvrvb-profile files via scripts/update_profile_catalog_doc.ps1.
  Do not hand-edit this file without regenerating it from the profiles tree.
-->
# Compatibility Catalog

Generated from checked-in `.mvrvb-profile` files.

## Summary

- Total profiles: 3
- Auto-match profiles: 2
- Template/manual profiles: 1
- Competitive profiles: 2
- Latency-sensitive profiles: 2

## Matrix

| Profile | Status | Category | Auto Match | Renderer | Stores/Launchers | Anti-Cheat | Notes |
|---|---|---|---|---|---|---|---|
| Global Defaults (`global-defaults`) | planning | baseline | yes | auto | (none) | unknown | Baseline runtime defaults while MetalVR Bridge grows from a Vulkan backend into a broader game runtime product. |
| Competitive Shooter (DXVK Template) (`competitive-shooter-dxvk`) | planning | competitive-shooter | no | dxvk | battlenet, steam | high | Planning template for latency-sensitive DXVK-backed titles. This is not a validated compatibility claim. |
| Overwatch 2 (`overwatch-2`) | planning | competitive-shooter | yes | dxvk | battlenet, Battle.net | blocking | Planning profile only. This file captures intended runtime policy and should not be read as a current compatibility claim. |

## Runtime Highlights

### Global Defaults

- Profile ID: `global-defaults`
- Sync mode: `default`
- High resolution mode: `false`
- MetalFX upscaling: `false`
- Launch args: `(none)`
- Environment entries: `1`
- DLL override entries: `0`

### Competitive Shooter (DXVK Template)

- Profile ID: `competitive-shooter-dxvk`
- Sync mode: `msync`
- High resolution mode: `true`
- MetalFX upscaling: `false`
- Launch args: `--fullscreen`
- Environment entries: `2`
- DLL override entries: `2`

### Overwatch 2

- Profile ID: `overwatch-2`
- Sync mode: `msync`
- High resolution mode: `true`
- MetalFX upscaling: `false`
- Launch args: `--fullscreen`
- Environment entries: `2`
- DLL override entries: `2`

