# VitaMaps

VitaMaps is a native, GPU-rendered map viewer for PlayStation Vita homebrew.
It uses VitaSDK and vita2d directly: there is no WebView, embedded browser,
HTML, or JavaScript map engine.

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
  and Russian UI localization, with a default mode that follows the console.
  Only the selected language is shown in the text-entry screen; no
  multi-language font-test copy is rendered there.
- Coordinate, city, locality, and address search through the native Vita IME.
  Its keyboard language follows the selected UI language. Coordinates and
  7,342 bundled city names resolve
  locally; other explicit searches use a rate-limited asynchronous geocoder,
  then enter center-crosshair pin mode.
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
default is the complete smooth motion system; Settings -> Animazioni
interfaccia -> Ridotte removes ambient pulses and snaps transitions for users
who prefer reduced motion.

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

The Release and Debug builds and VPK packaging are validated locally with
VitaSDK. On-device diagnostics from 01.01 proved that graphics, PGF and HTTPS
initialized correctly, then isolated the failure to libstdc++ rejecting
`std::thread` at runtime. Version 01.02 replaced that path, but its requested
kernel priority was rejected on hardware with
`SCE_KERNEL_ERROR_ILLEGAL_PRIORITY`. Version 01.03 uses VitaSDK's documented
`0x10000100` priority token and was confirmed to reach the interactive map on
real hardware. That pass exposed swapped tile color channels, scaled PGF text,
and fractional tile seams. Version 01.04 addresses those defects and adds the
multilingual font pipeline, per-style caches, strict viewport-first scheduling,
four styles, and the configurable full-screen HUD. Version 01.05 adds the
crosshair pin workflow, persistent list editor, geometry summaries, direct
multilingual Vita IME coordinate search, revised control map, and policy-aware
offline-area planner. Version 01.06 adds the shared VitaTube-style motion
system and fixes fractional zoom choosing undersized raster labels. Version
01.07 added the independently scalable, decluttered native populated-place
overlay. Version 01.08 removes the bulk/offline planner, makes normal tile-cache
entries persistent until budget eviction, enables local plus asynchronous
place/address search, adds missing-tile loaders, and replaces fragile legend
glyphs with drawn button symbols. Version 01.09 removes the experimental native
city-name overlay completely, leaving only labels baked into provider tiles.
Version 01.10 adds persistent UI-language selection, localizes all interactive
map, list, settings, search, and text-entry copy, and limits the IME to the
selected language instead of presenting multiple language samples. Version
01.11 replaces the static cache-budget footer with live cache size/tile status
and adds confirmed worker-coordinated clearing of disk, compressed RAM, and GPU
tile caches. Version 01.12 adds persistent per-list map visibility, explicit
route closure with corrected perimeter/area semantics, distance labels for
every segment, map bearing across coordinate conversion and tile scheduling,
two-finger/right-stick rotation, and a smooth north-up HUD control. The exact
01.12 VPK still requires an
on-device visual and interaction pass; local packaging alone is not claimed as
hardware proof.

## License

VitaMaps is licensed under GPL-3.0-only. Third-party components retain their
own licenses; `vita-https` is GPL-3.0-only and includes its dependency notices
and pinned source provenance.
