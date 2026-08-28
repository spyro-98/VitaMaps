# Changelog

All notable user-facing changes to VitaMaps are documented here. Vita package
versions use the two-part format shown in LiveArea and the VPK metadata.

## 01.23 - Public beta

### Highlights

- Promotes VitaMaps to a documented public beta.
- Presents the Offline Atlas explicitly as a beta cache-level visualizer.
- Adds the NASA Johnson Space Center image credit for the Apollo 17 “Blue
  Marble” application icon to the native Settings screen and README.
- Includes a public beta guide with installation, privacy, diagnostics, known
  limitations, and a bug-report checklist.

### Offline Atlas beta

The visualizer began with the curiosity to see what was actually stored inside
VitaMaps' persistent tile cache. It turns cached XYZ files into navigable 3D
layers grouped by style and zoom, so coverage and zoom depth can be inspected
instead of inferred from directory names.

This experiment also helps evaluate a possible future workflow for downloading
a selected area at multiple zoom levels. Version 01.23 does not implement that
workflow: the atlas is cache-only, never bulk-downloads or fills missing tiles,
and shows only data accumulated through normal map browsing.

### Release boundary

Release and Debug builds and VPK packaging are validated locally with VitaSDK.
The exact 01.23 package still requires a complete physical-console pass. Local
build success is not presented as proof of network, input, GPU, or persistence
behavior on every Vita configuration.

## 01.22

- Added per-layer geographic scaling and continuous camera-derived requests to
  make deep cached zoom layers readable and reachable.
- Added a high-contrast online/offline HUD badge.
- Moved provider attribution away from bottom-right controls when the HUD is
  visible and into a compact status strip when it is hidden.

For the detailed milestone history from 01.03 onward, see the
[project status section](README.md#project-status) in the README.
