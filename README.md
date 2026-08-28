# VitaMaps

VitaMaps is a native, GPU-rendered map viewer for PlayStation Vita homebrew.
It uses VitaSDK and vita2d directly: there is no WebView, embedded browser,
HTML, or JavaScript map engine.

- Current package version: 01.23 public beta
- Title ID: `VMAP00001`
- License: [GPL-3.0-only](LICENSE)

VitaMaps is in public beta. The current milestone is an installable raster-map
viewer with continuous Web Mercator navigation, asynchronous tile loading,
progressive adjacent-level fallback, and bounded multi-level caches. It is
intentionally structured as the foundation of a larger mapping application
rather than as a one-file image demo. Expect experimental features and rough
edges; hardware reports are welcome through
[GitHub Issues](https://github.com/spyro-98/VitaMaps/issues).

Release notes are tracked in the [changelog](CHANGELOG.md). Installation
guidance, known limitations, and a useful bug-report template are collected in
the [public beta guide](docs/PUBLIC_BETA.md).

## Screenshots

<p align="center">
  <img src="docs/images/vitamaps-online-map.jpeg"
       alt="VitaMaps online raster map running on PlayStation Vita"
       width="900">
</p>
<p align="center"><em>Native map view on PlayStation Vita hardware.</em></p>

<table>
  <tr>
    <td width="50%">
      <img src="docs/images/vitamaps-offline-atlas-layers.jpeg"
           alt="VitaMaps Offline Atlas showing cached map zoom layers">
    </td>
    <td width="50%">
      <img src="docs/images/vitamaps-offline-atlas-detail.jpeg"
           alt="VitaMaps Offline Atlas showing detailed cached tile layers">
    </td>
  </tr>
  <tr>
    <td align="center"><em>Cache layers grouped by zoom level.</em></td>
    <td align="center"><em>Real cached tiles in the 3D atlas.</em></td>
  </tr>
</table>

## Current features

- Native 960x544 vita2d rendering.
- EPSG:3857/Web Mercator with sub-tile pan and fractional visual zoom.
- Abstract `MapProvider` API with five selectable OpenStreetMap-derived raster
  styles: OSM Standard, CyclOSM, OSM France, Humanitarian, and OpenTopoMap.
- One native Vita kernel worker for priority-ordered disk, HTTPS, and PNG
  decoding; no dependency on `std::thread` or libstdc++ gthread activation.
- A dynamically replaced priority heap on every frame: missing visible tiles
  always precede look-ahead work, and an active prefetch download yields when
  the viewport needs a tile.
- HTTP failures use bounded exponential retry while preserving current
  viewport priority; `429` responses receive a longer 30–60 second backoff
  instead of being decoded as images or retried in a tight loop.
- Progressive zoom: cached parent or child-level tiles remain visible while
  the requested level arrives.
- L1 bounded GPU texture LRU (12 MiB).
- L2 bounded compressed-tile RAM LRU (6 MiB).
- L3 atomic disk caches under `ux0:data/VitaMaps/cache/<style-id>` with an
  independent 200 MiB LRU budget for every style. Tiles do not expire by age;
  at the limit, a newly admitted tile replaces the least recently used tile
  from that same style only.
- L4 authenticated HTTPS through
  [spyro-98/vita-https](https://github.com/spyro-98/vita-https).
- Left analog/D-pad pan, right-analog rotation, front-touch drag with inertia,
  combined two-finger pan/pinch/twist, L/R zoom, and smooth north reset.
- Exact-size bundled Inter UI fonts for Latin, Greek, and Cyrillic plus native
  Vita system PGF fallback for Japanese, Chinese, and Korean. CJK system-font
  atlases use sharp point magnification instead of libvita2d's stock linear
  enlargement, while retaining the console's complete glyph coverage.
- Seam-free fractional tile quads and bidirectional adjacent-level fallback.
- VitaWave-derived, frame-rate-independent UI motion: soft HUD fades/slides,
  animated focus travel and glow, button feedback, composed crosshair,
  transient-message entrances/exits, and full-screen mode transitions.
- A primary instrument-style navigation screen keeps Map, Offline Atlas,
  Lists and routes, and Settings at the same hierarchy level. Settings is
  divided into Map, Interface, and Storage and logs categories instead of one
  long undifferentiated list.
- Categorized Settings cover map style, automatic hiking mode, UI language,
  HUD behavior, independently persistent map scale, center crosshair,
  metric/imperial units, smooth/reduced motion, cache status/clearing, and
  persistent diagnostic logging.
- Live persistent-map-cache status in Settings, including actual occupied
  space and tile count, plus double-confirmed asynchronous clearing. Clearing
  also drops the compressed RAM and GPU tile layers without blocking the
  render thread.
- A full-screen map HUD that auto-hides after 2.5 seconds, responds only to a
  short tap (never drag/pinch), and includes a latitude/longitude readout. The
  real-world scale bar can remain visible independently, without a dark panel.
- A bounded 256 KiB in-RAM diagnostic log plus optional atomic persistence to
  `ux0:data/VitaMaps/session_log.txt`.
- Center-crosshair pin placement with ordered multi-pin lists, atomic
  persistence, rename/delete/reorder editing, independently selectable map
  visibility, explicit open/closed geometry, per-segment and total geodesic
  distance, and spherical polygon area after a route is closed.
- Per-list colors and icons (pin, star, flag, camp, water, and summit), stored
  with the list and reused consistently by markers, routes, focus, and GPX
  imports.
- [GPX 1.0/1.1](https://www.topografix.com/gpx.asp) waypoint, route, and track import from
  `ux0:data/VitaMaps/gpx/inbox`, GPX 1.1 export to `gpx/exports`, and a bounded
  persistent import history. All GPX filesystem work runs on the worker.
- Explicit nearby-POI lookup through the
  [Overpass API](https://wiki.openstreetmap.org/wiki/Overpass_API), rendered as transient native
  map icons without replacing or duplicating the labels embedded in tiles.
- A key-free hiking mode that selects OpenTopoMap automatically, exposes
  paths/topography from that style, and adds an elevation profile, range,
  ascent, and descent to pin lists using
  [Open-Meteo/Copernicus elevation data](https://open-meteo.com/en/docs/elevation-api).
- A beta Offline Atlas/cache-level visualizer built exclusively from already
  accumulated cache files. It groups true tile footprints by style and zoom in
  an orbitable 3D
  stack and streams the actual cached PNG imagery into the selected layer and
  its immediate neighbours. Overview mode offers full-angle X/Y orbit, layer
  spacing, block pan, and view zoom; layer-navigation mode adds continuous
  in-layer pan and D-pad movement between real cached tile keys. The selected
  tile exposes XYZ and geographic coordinates and can reopen the normal map at
  the matching style and zoom. Atlas reads are explicitly cache-only and can
  never turn into provider downloads.
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
on VitaWave or on another local checkout.

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
| X on map | Enter pin mode; X stores the center point, or closes a 3+ point polygon when the crosshair overlaps its first pin |
| Circle in pin mode | Finish pin placement |
| Square on map | Open the pin-list editor |
| Triangle on map | Search coordinates, city, locality, or address |
| Select + Triangle on map | Explicitly query and display nearby Overpass POIs |
| Start on map | Open the primary navigation screen |
| Up/Down + X in primary navigation | Select and open Map, Offline Atlas, Lists and routes, or Settings |
| L/R in Settings | Change between Map, Interface, and Storage and logs categories |
| Up/Down in Settings | Select an option in the active category |
| X or Left/Right in Settings | Change the selected option |
| X on Map cache in Settings | Press twice to clear tile caches safely |
| Triangle in atlas overview | Cycle the cached map style |
| Up/Down in atlas overview | Select an actually cached zoom layer |
| Left/Right in atlas overview | Decrease/increase spacing between zoom layers |
| Square in offline atlas | Enter/leave navigation inside the selected layer |
| D-pad in atlas layer navigation | Move to a real cached neighbouring tile; sparse gaps snap to the nearest tile in that direction |
| Left analog in offline atlas | Pan the complete stack in overview, or pan geographically inside a selected layer |
| Right analog in offline atlas | Orbit the textured stack through 360 degrees on both axes |
| L/R in offline atlas | Zoom the 3D atlas view out/in |
| X in offline atlas | Open the selected cached tile on the matching map style and zoom |
| Select in offline atlas | Smoothly reset orbit, spacing, pan, and mode-appropriate view zoom |
| Start in offline atlas | Refresh the cache index on the worker |
| Circle | Leave the current atlas layer, then return to primary navigation; Settings also returns to primary navigation |
| Start + Select | Exit the application |

In the list overview, X opens and activates a list, R toggles whether that list
is drawn on the map, Left/Right changes its color/icon, L opens GPX management,
Triangle creates, Square renames, and Select double-confirms deletion. The GPX
screen imports the selected inbox file with X, exports the active list with
Square, refreshes with Triangle, and pages through up to 64 persisted import
results with Left/Right. Inside a
list, Select closes/reopens a path (at least
three points are required), L/R reorder the selected point, and every row plus
every visible map segment shows its own geodesic distance. A closed path draws
its final edge, includes it in the perimeter, and enables the area result. The
same closure can be performed directly on the map by placing the center
crosshair over the first marker and pressing X; no duplicate pin is inserted.

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
style's 200 MiB capacity requires LRU eviction. Successful offline reads update
access metadata without rewriting PNG payloads. See the official
[OpenStreetMap tile usage policy](https://operations.osmfoundation.org/policies/tiles/).

The extra community styles use documented endpoints from
[CyclOSM](https://github.com/cyclosm/cyclosm-cartocss-style) and
[OpenStreetMap France](https://www.openstreetmap.fr/fonds-de-carte/), plus the
documented [OpenTopoMap service](https://dev.opentopomap.org/about). Their
style IDs are stable and each owns a separate 200 MiB disk-cache namespace.

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

### Offline Atlas and cache-level visualizer (beta)

The Offline Atlas began as an experiment driven by a simple curiosity: what is
actually stored inside the persistent tile cache? It makes those otherwise
invisible files tangible by arranging real cached XYZ tiles into navigable 3D
layers, separated by map style and zoom level.

The feature is also a way to evaluate the interface and technical constraints
of a possible future offline workflow that could deliberately download a
selected area at several zoom levels. That future workflow is not part of this
release. The atlas does not bulk-download tiles, does not fill missing areas,
and does not claim complete regional coverage; it displays only tiles already
accumulated during ordinary map browsing.

The worker prepares full tile-footprint bounds, a compact coverage sample, and
the exact sorted tile keys for every style/zoom layer in one cache scan. The
selected layer then requests a bounded 5x5 neighbourhood plus a small corresponding
neighbourhood on each adjacent cached zoom level. These requests reuse the
normal RAM/decode/GPU pipeline but carry a cache-only contract, so a file
evicted between scan and display becomes a neutral gap rather than an HTTPS
request. Tile textures, selection outlines, orbit, pan, view zoom, XYZ and
latitude/longitude readouts therefore remain tied to data that really exists
on local storage.

An exact city name first checks the bundled Natural Earth table. Other
user-confirmed place/address queries use the public Nominatim endpoint with one
result, no autocomplete, a distinct User-Agent, a single worker, a minimum
one-second remote interval, and an atomic 128-entry persistent result cache.
The endpoint can be replaced without rebuilding by writing one HTTPS URL to
`ux0:data/VitaMaps/geocoder_url.txt`. These constraints follow the official
[Nominatim usage policy](https://operations.osmfoundation.org/policies/nominatim/).
Search terms are sent to that service, so users should not enter confidential
or personal information. Turn-by-turn routing remains unimplemented.

Nearby POIs are requested only after the `Select + Triangle` command. VitaMaps
uses the public Overpass main endpoint with a bounded 250–5000 m radius, at
most 64 returned elements, one active request, and a minimum ten-second local
interval. Hiking elevation requests contain at most the 64 coordinates in the
selected pin list and use the Open-Meteo elevation endpoint. These services
receive the requested coordinates; neither integration requires a personal
API key. Their availability is not required to open cached maps or GPX files.

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
   one worker ---- geocoder / Overpass / elevation / GPX
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
systems. Version 01.13 added French, Spanish, German, and Portuguese throughout
the UI, Vita IME, console-language detection, and geocoder language negotiation.
Version 01.14 fixes a GPU core crash caused by submitting stack-backed vertex
data to GXM while rendering rotated placeholder tiles.
Version 01.15 adds an independently persistent, panel-free map scale and closes
pin polygons by snapping the center crosshair to their first point.
Version 01.16 removes the extra linear-filter blur from Japanese, Korean, and
Chinese system-font glyphs without bundling large duplicate CJK font files.
Version 01.17 adds styled pin lists, asynchronous GPX import/export and import
history, explicit Overpass POIs, automatic OpenTopoMap hiking mode with pin
elevation profiles, 200 MiB non-expiring per-style LRU caches, and a cache-only
parallax offline atlas.
Version 01.18 turns that atlas into a navigable geographic cache explorer with
full-angle orbit, adjustable layer spacing, block pan, real zoom-layer
selection, tile focus, coordinate readout, orientation axes, and map handoff.
Version 01.19 promotes the atlas out of Settings into the primary navigation,
organizes Settings into Map, Interface, and Storage categories, and turns the
atlas layers into bounded cache-only mosaics of the real stored tile images.
It also adds selectable layer-navigation mode, directional tile traversal,
continuous atlas view zoom, and mode-aware reset/back controls.
Version 01.20 fixes the atlas textured-quad UV range so cached PNG imagery is
actually sampled, reverses both left-stick atlas pan axes to match direct
manipulation, and exposes a live GPU-ready/requested tile count for hardware
diagnostics.
Version 01.21 replaces the atlas' fixed screen-space pan clamp with a dynamic
envelope derived from view magnification, cached zoom depth, and layer spacing.
Pan acceleration now scales smoothly with magnification, keeping distant
layers reachable after zooming without affecting in-layer geographic motion.
Version 01.22 gives every selected atlas layer its own geographic projection
scale: an overview layer fits the viewport at 1x and layer navigation maps one
XYZ tile to 256 pixels before relative zoom is applied. Layer changes now
recenter and refit automatically, and analog browse continuously moves the
cache-only request window with the camera. The map HUD adds an explicit,
high-contrast online/offline badge; provider attribution crossfades between a
top position while the HUD is open and a compact bottom status strip when it
is hidden, avoiding bottom-right control overlap.
Version 01.23 prepares the public beta documentation, identifies the Offline
Atlas explicitly as an experimental cache-level visualizer, and adds the NASA
image credit for the application icon directly to the native Settings screen.

Release and Debug 01.23 builds and VPK packaging are validated locally with
VitaSDK. The exact 01.23 package still requires a complete on-device visual,
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
application icon is derived from NASA's Apollo 17
[“Blue Marble” photograph AS17-148-22727](https://science.nasa.gov/resource/the-blue-marble/).
Image credit: NASA Johnson Space Center. The icon contains no NASA identifier
or logo and does not imply NASA endorsement; see NASA's
[image and media usage guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/).

The project is not affiliated with or endorsed by Sony Interactive
Entertainment, OpenStreetMap Foundation, OpenStreetMap France, CyclOSM, Humanitarian
OpenStreetMap Team, OpenTopoMap, Open-Meteo, Copernicus, Natural Earth, or
NASA.
