# Changelog

All notable user-facing changes to VitaMaps are documented here. Public releases
use semantic versioning. PS Vita LiveArea/VPK metadata uses a two-part
`APP_VER`, so release `1.0.0` is embedded as `01.00`.

## 1.0.0 - Initial public release

### Highlights

- Publishes the first documented VitaMaps release.
- Presents the Offline Atlas explicitly as a beta cache-level visualizer.
- Adds the NASA Johnson Space Center image credit for the Apollo 17 “Blue
  Marble” application icon to the native Settings screen and README.
- Includes a release guide with installation, privacy, diagnostics, known
  limitations, and a bug-report checklist.

### Offline Atlas beta

The visualizer began with the curiosity to see what was actually stored inside
VitaMaps' persistent tile cache. It turns cached XYZ files into navigable 3D
layers grouped by style and zoom, so coverage and zoom depth can be inspected
instead of inferred from directory names.

This experiment also helps evaluate a possible future workflow for downloading
a selected area at multiple zoom levels. Version 1.0.0 does not implement that
workflow: the atlas is cache-only, never bulk-downloads or fills missing tiles,
and shows only data accumulated through normal map browsing.

### Release boundary

Release and Debug builds and VPK packaging are validated locally with VitaSDK.
The exact 1.0.0 package still requires broad physical-console coverage. Local
build success is not presented as proof of network, input, GPU, or persistence
behavior on every Vita configuration.

## Pre-release milestone 01.22

- Added per-layer geographic scaling and continuous camera-derived requests to
  make deep cached zoom layers readable and reachable.
- Added a high-contrast online/offline HUD badge.
- Moved provider attribution away from bottom-right controls when the HUD is
  visible and into a compact status strip when it is hidden.

Earlier `01.xx` values were internal PS Vita package milestones before the
public semantic-versioning baseline.
