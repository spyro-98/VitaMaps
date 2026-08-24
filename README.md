# VitaMaps

VitaMaps is a native, GPU-rendered map viewer for PlayStation Vita homebrew.
It uses VitaSDK and vita2d directly: there is no WebView, embedded browser,
HTML, or JavaScript map engine.

- Current package version: 01.13
- Title ID: `VMAP00001`
- License: [GPL-3.0-only](LICENSE)

The current milestone is an installable raster-map viewer with continuous
Web Mercator navigation, asynchronous tile loading, progressive adjacent-level
fallback, and bounded multi-level caches. It is intentionally structured as
the foundation of a larger mapping application rather than as a one-file
image demo.

## Current features

- Native 960x544 vita2d rendering.
- EPSG:3857/Web Mercator with sub-tile pan and fractional visual zoom.
- Abstract `MapProvider` API with four selectable OpenStreetMap-derived raster
  styles: OSM Standard, CyclOSM, OSM France, and Humanitarian.
- One native Vita kernel worker for priority-ordered disk, HTTPS, and PNG
  decoding; no dependency on `std::thread` or libstdc++ gthread activation.
- A dynamically replaced priority heap on every frame: missing visible tiles
  always precede look-ahead work, and an active prefetch download yields when
  the viewport needs a tile.
- Progressive zoom: cached parent or child-level tiles remain visible while
  the requested level arrives.
- L1 bounded GPU texture LRU (12 MiB).
- L2 bounded compressed-tile RAM LRU (6 MiB).
- L3 atomic disk caches under `ux0:data/VitaMaps/cache/<style-id>` (an
  independent 96 MiB budget for every style, with a minimum seven-day
  eviction protection period and no time expiry on normal reads).
- L4 authenticated HTTPS through
  [spyro-98/vita-https](https://github.com/spyro-98/vita-https).
- Left analog/D-pad pan, right-analog rotation, front-touch drag with inertia,
  combined two-finger pan/pinch/twist, L/R zoom, and smooth north reset.
- Exact-size bundled Inter UI fonts for Latin, Greek, and Cyrillic plus native
  Vita system PGF fallback for Japanese, Chinese, and Korean.
- Seam-free fractional tile quads and bidirectional adjacent-level fallback.
- VitaTube-derived, frame-rate-independent UI motion: soft HUD fades/slides,
  animated focus travel and glow, button feedback, composed crosshair,
  transient-message entrances/exits, and full-screen mode transitions.
- A native Settings screen for map style, UI language, HUD behavior,
  center crosshair, metric/imperial units, smooth/reduced motion, and
  persistent diagnostic logging.
- Live persistent-map-cache status in Settings, including actual occupied
  space and tile count, plus double-confirmed asynchronous clearing. Clearing
  also drops the compressed RAM and GPU tile layers without blocking the
  render thread.
- A full-screen map HUD that auto-hides after 2.5 seconds, responds only to a
  short tap (never drag/pinch), and includes a latitude/longitude readout and
  real-world scale bar.
- A bounded 256 KiB in-RAM diagnostic log plus optional atomic persistence to
  `ux0:data/VitaMaps/session_log.txt`.
- Center-crosshair pin placement with ordered multi-pin lists, atomic
  persistence, rename/delete/reorder editing, independently selectable map
  visibility, explicit open/closed geometry, per-segment and total geodesic
  distance, and spherical polygon area after a route is closed.
- Italian, English, Japanese, Korean, Simplified Chinese, Traditional Chinese,
  Russian, French, Spanish, German, and Portuguese UI localization, with a
  default mode that follows the console. Search result language and the Vita
  IME follow the active UI language. Only the selected language is shown in
  the text-entry screen; no multi-language font-test copy is rendered there.
- Coordinate, city, locality, and address search through the native Vita IME.
  Its keyboard language follows the selected UI language. Coordinates and
  7,342 bundled city names resolve locally; other explicit searches use a
  rate-limited asynchronous geocoder, then enter center-crosshair pin mode.
- Animated cyan/white loaders over neutral missing-tile placeholders; cached
  parents remain preferred whenever available.
- Graphically drawn Cross, Circle, Square, and Triangle command caps plus
  width-bounded contextual legends, independent from font glyph coverage.
- A visible boot frame and a fallback fatal screen that reports the startup
  stage and error code instead of silently returning to LiveArea.

## Standalone clone and build

`vita-https` is pinned as a Git submodule, so the repository does not depend
on VitaTube or on another local checkout.

```sh
git clone --recursive https://github.com/spyro-98/VitaMaps.git
cd VitaMaps
export VITASDK=/absolute/path/to/vitasdk
external/vita-https/tools/build-curl-mbedtls.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

If the repository was cloned without `--recursive`:

```sh
git submodule update --init --recursive
```

The installable package is generated at `build/VitaMaps.vpk`. All VitaMaps
translation units and the in-tree `vita-https` target use `-O3` in Release.
The transport bootstrap builds the pinned libcurl/Mbed TLS dependency and
rejects the legacy VitaSDK libcurl/OpenSSL stack.

Requirements:

- a current VitaSDK toolchain with Mbed TLS 3.x;
- VitaSDK packages for vita2d, libpng, zlib, JPEG, FreeType, bzip2, and pthread;
- network access during the one-time `vita-https` dependency bootstrap.

After building, transfer `build/VitaMaps.vpk` to the console and install it
with VitaShell. VitaMaps stores settings, logs, pins, geocoding results, and
map caches under `ux0:data/VitaMaps/`; uninstalling the LiveArea application
does not necessarily remove that data directory.

For a diagnostic build, still optimized with `-O3` but including debug symbols
and persistent logging enabled by default on a new installation:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel 4
```

The package is `build-debug/VitaMaps.vpk`. A previously saved user setting is
always respected; the Debug default only applies when no valid settings file
exists.

## Controls

| Input | Action |
|---|---|
| Left analog | Continuous pan |
| Right analog (horizontal) | Rotate the map continuously |
| D-pad | Pan |
| Front touch drag | Pan with release inertia |
| Two-finger gesture | Pan, continuous pinch zoom, and twist rotation together |
| L / R | Zoom out / in |
| Short front-screen tap | Show/hide the auto HUD |
| Select on map, or the north control in the visible HUD | Smoothly restore north-up orientation |
| X on map | Enter pin mode; X again stores the center point in the active list |
| Circle in pin mode | Finish pin placement |
| Square on map | Open the pin-list editor |
| Triangle on map | Search coordinates, city, locality, or address |
| Start | Open/close Settings |
| Up/Down in Settings | Select an option |
| X or Left/Right in Settings | Change the selected option |
| X on Map cache in Settings | Press twice to clear tile caches safely |
| Circle in Settings | Return to the map |
| Start + Select | Exit the application |

In the list overview, X opens and activates a list, R toggles whether that list
is drawn on the map, Triangle creates, Square renames, and Select
double-confirms deletion. Inside a list, Select closes/reopens a path (at least
three points are required), L/R reorder the selected point, and every row plus
every visible map segment shows its own geodesic distance. A closed path draws
its final edge, includes it in the perimeter, and enables the area result.

All motion uses elapsed time rather than a fixed number of frames and clamps
late-frame deltas, so disk or network stalls do not make panels jump. The
default is the complete smooth motion system; Settings -> Interface animations
-> Reduced removes ambient pulses and snaps transitions for users who prefer
reduced motion.

## Diagnostics

Diagnostics are always collected in a bounded RAM buffer. The Settings option
controls whether snapshots of that buffer are also written atomically to
`ux0:data/VitaMaps/session_log.txt`; it does not add disk I/O to the render
loop. Debug builds start with this option enabled when no preference has been
saved yet. Release builds default to disabled.

At startup VitaMaps presents a frame before loading the PGF font, initializing
HTTPS, scanning the cache, or starting tile work. The PGF system module is
loaded explicitly before vita2d requests the default font. A structural startup
failure is shown with its stage and hexadecimal result code; network failures
leave the map open with cached tiles and animated placeholders and are recorded
in the log.

## Networking and map data

VitaMaps does not implement TLS. It uses
[vita-https](https://github.com/spyro-98/vita-https), which owns the Vita
network lifecycle and provides pinned libcurl with Mbed TLS, TLS 1.2 minimum,
Mozilla CA roots, certificate/hostname verification, redirects, timeouts,
low-speed aborts, and cooperative cancellation. The application does not
expose any way to disable certificate verification.

The active provider requests only the interactive viewport plus a one-tile
look-ahead ring, uses a stable identifying User-Agent, never bulk-downloads
areas, and keeps provider attribution visible even while the optional HUD is
hidden. Normally visited tiles remain readable with no time expiry until their
style's 96 MiB capacity requires eviction; the seven-day rule is a minimum
eviction-protection period, not an expiration date. See the official
[OpenStreetMap tile usage policy](https://operations.osmfoundation.org/policies/tiles/).

The extra community styles use documented endpoints from
[CyclOSM](https://github.com/cyclosm/cyclosm-cartocss-style) and
[OpenStreetMap France](https://www.openstreetmap.fr/fonds-de-carte/). Their
style IDs are stable and each owns a separate 96 MiB disk-cache namespace.

`tile.openstreetmap.org` is a best-effort community service, not an SLA. The
provider abstraction exists so releases can switch to a self-hosted or
commercially supported endpoint without coupling the renderer to OSM.

### Search, routes, and persistent interactive cache

The bundled community tile endpoints are interactive raster services, not
routing engines. VitaMaps never invents road directions from straight-line pin
geometry. Coordinate search, bundled populated-place search, pin distance, and
polygon area work without network access.

There is no area-download or bulk-prefetch workflow. The disk cache contains
only tile responses requested while the user normally navigates the map. It is
persistent, separated by style, and opportunistically makes already visited
areas usable without a connection, but it is not presented as a guaranteed
complete offline map.

An exact city name first checks the bundled Natural Earth table. Other
user-confirmed place/address queries use the public Nominatim endpoint with one
result, no autocomplete, a distinct User-Agent, a single worker, a minimum
one-second remote interval, and an atomic 128-entry persistent result cache.
The endpoint can be replaced without rebuilding by writing one HTTPS URL to
`ux0:data/VitaMaps/geocoder_url.txt`. These constraints follow the official
[Nominatim usage policy](https://operations.osmfoundation.org/policies/nominatim/).
Search terms are sent to that service, so users should not enter confidential
or personal information. Turn-by-turn routing remains unimplemented.

## Architecture

```text
MapScreen / input
        |
    MapCamera ---- Web Mercator
        |
   MapRenderer ---- GPU Texture LRU (L1)
        |
   TileManager ---- priority + state + cancellation
        |
   one worker ---- explicit geocoder + result cache
        |       \-- compressed RAM LRU (L2)
        |       \-- disk tile cache (L3)
        |       \-- vita-https (L4)
        |       \-- libpng decode
        |
 thread-safe RGBA upload queue
        |
 render-thread-only vita2d texture upload
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for design decisions,
budgets, state transitions, and extension points.

## Raster labels

Place names are already rendered into each 256x256 raster tile by its map
provider, so they cannot be removed or resized independently. VitaMaps displays
only those provider labels and never draws a second city-name layer over the
map. The bundled Natural Earth gazetteer is retained solely as an invisible
local search index. A future MVT vector renderer remains the route to complete,
fully styled road and locality labels.

## Project status

Version 01.03 was confirmed to reach the interactive map on PS Vita hardware.
Subsequent milestones fixed tile color channels, fractional seams, text sizing,
startup failures, and Vita kernel-thread compatibility, then added the current
cache, search, pin-list, geometry, rotation, HUD, animation, and localization
systems. Version 01.13 adds French, Spanish, German, and Portuguese throughout
the UI, Vita IME, console-language detection, and geocoder language negotiation.

Release and Debug 01.13 builds and VPK packaging are validated locally with
VitaSDK. The exact 01.13 package still requires a complete on-device visual,
network, cache, input, and persistence pass; local packaging is not presented
as hardware proof.

## License and attribution

Copyright (C) 2026 spyro-98.

VitaMaps source code is licensed under
[GNU GPL version 3 only](LICENSE). Third-party components and assets retain
their own terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), the
bundled [Inter OFL text](assets/fonts/Inter-OFL.txt), and the pinned
[`vita-https`](https://github.com/spyro-98/vita-https) submodule notices.

OpenStreetMap data attribution remains visible inside the map renderer. The
project is not affiliated with or endorsed by Sony Interactive Entertainment,
OpenStreetMap Foundation, OpenStreetMap France, CyclOSM, Humanitarian
OpenStreetMap Team, Natural Earth, or NASA.
