# VitaMaps feature guide

This document describes the current VitaMaps 01.23 public-beta functionality.
For implementation contracts, memory budgets, and state transitions, read
[ARCHITECTURE.md](ARCHITECTURE.md). For every input mapping, read
[CONTROLS.md](CONTROLS.md).

## Native map engine

VitaMaps renders 256×256 raster XYZ tiles directly with vita2d. The main thread
owns input, camera updates, GPU uploads, texture lifetime, and drawing. A single
native Vita kernel worker handles disk access, HTTPS, PNG decoding, geocoding,
Overpass, elevation, GPX I/O, cache scans, and cache clearing.

The camera keeps latitude, longitude, bearing, and visual zoom as continuous
values. Web Mercator conversion retains floating-point world coordinates, so
panning remains sub-tile and zoom animation does not jump between integer
levels. Longitude wraps around the world and latitude is clamped to the valid
Web Mercator range.

Map movement supports:

- left-stick and D-pad pan;
- right-stick rotation;
- one-finger drag with release inertia;
- simultaneous two-finger pan, pinch zoom, and twist rotation;
- L/R zoom and smooth north reset;
- delta-time-based movement independent of frame rate.

## Layered tile pipeline

Every frame computes the tile set required by the current camera. The wanted
set is replaced dynamically, so tiles visible now always outrank stale work
from an earlier position or zoom. Distance from the screen center defines the
base priority; a small look-ahead bias favors the direction of travel.

Each tile has an explicit lifecycle:

```text
Missing -> Queued -> Disk lookup -> Download -> Decode -> GPU upload -> Ready
                 \-> cache hit ----------------^                 |
                                                               LRU eviction
```

The worker cooperatively cancels an obsolete transfer when the user moves away
or when a visible tile must replace active prefetch work. Failed requests use
bounded exponential retry. Server errors back off longer, and HTTP 429 begins
with a 30–60 second delay instead of entering a tight loop.

At fractional zoom, the renderer continues displaying a cached parent or
adjacent child-level tile until the sharper requested tile is ready. Missing
areas use neutral placeholders and cyan/white animated loaders. Tile quads
overlap their shared edge slightly to avoid white seams during rotation and
scaling.

## Offline Atlas (beta)

The Offline Atlas is a cache-level visualizer and a second presentation of the
same layered tile engine. It does not create synthetic map previews. A worker
scan indexes the real persistent cache by:

- provider/style;
- integer zoom layer;
- canonical XYZ tile key;
- geographic footprint and coverage bounds;
- stored byte size and tile count.

### Layer overview

Overview mode projects the cached zoom levels as a 3D Mercator stack. Every
layer is geographically aligned with the others, so a location occupies the
same world position as the zoom depth changes. The selected layer is
highlighted and reports style, zoom, XYZ focus, coordinates, spacing, view
magnification, cache-only state, tile count, and GPU-ready/requested counts.

The complete stack can be:

- orbited through 360 degrees on both axes;
- moved as a block;
- zoomed toward or away from the camera;
- spread apart or compressed by changing layer spacing;
- filtered by cached map style;
- reset to a readable default orientation.

### Layer navigation

Entering a layer changes from presentation-space movement to geographic
navigation. One XYZ tile maps to 256 screen pixels before relative view zoom is
applied. Analog pan moves the cache-only request window continuously through
world space; D-pad navigation selects real neighbouring keys and snaps over
sparse gaps to the nearest stored tile in the requested direction.

The selected layer requests only a bounded 5×5 neighbourhood. Its immediately
adjacent cached zoom layers receive bounded 3×3 neighbourhoods, giving depth
context without flooding RAM or the upload queue. Up to three decoded atlas
textures are uploaded per frame.

Pressing X on a selected cached tile hands its coordinates, style, and zoom to
the normal map. If a file disappears between indexing and display, its request
ends as a gap. The cache-only flag prevents every atlas miss from reaching
HTTPS.

### Purpose and future direction

The atlas began as an experiment to make an invisible cache understandable.
It now shows at a glance which geographic areas and zoom depths are already
available locally. It also provides a concrete interface for evaluating a
future deliberate area/zoom download workflow.

That downloader does not exist in 01.23. The atlas never expands coverage,
fills holes, or claims that a city or route is completely available offline.

## Cache hierarchy

| Level | Data | Budget | Policy |
|---|---|---:|---|
| L1 | vita2d RGBA textures | 12 MiB | LRU; current-frame entries protected |
| L2 | Compressed PNG payloads | 6 MiB | Worker-local LRU |
| L3 | Persistent PNG files | 200 MiB per style | Access-aware LRU and atomic writes |
| L4 | HTTPS provider | One active request | Timeout, cancellation, and bounded retry |

Persistent tiles do not expire by age. When one style reaches 200 MiB, a new
admitted tile replaces the least recently used tile in that style only. Cache
status and tile count are visible in Settings, and clearing runs asynchronously
with double confirmation.

## Map styles and hiking mode

The abstract `MapProvider` boundary keeps the renderer independent from any
hostname. The bundled raster styles are:

- OSM Standard;
- CyclOSM;
- OSM France;
- Humanitarian;
- OpenTopoMap.

Each stable style ID owns a separate cache namespace. Hiking mode selects
OpenTopoMap automatically; selecting another map style disables hiking mode.
OpenTopoMap supplies topographic and trail presentation without a personal API
key.

## HUD and native interface

The HUD shows the center coordinate, map state, provider attribution, scale,
and context controls. With automatic behavior enabled, it fades after 2.5
seconds. A short tap toggles it, while drag, pinch, and twist gestures do not.
The scale bar can remain visible independently without its dark HUD panel.

Provider attribution always remains visible. Its position changes with the HUD
to avoid covering the lower-right controls. A high-contrast ONLINE/OFFLINE badge
distinguishes active network mapping from cached display.

UI motion uses frame-rate-independent easing for screen transitions, focus
travel, HUD appearance, button feedback, selection glow, messages, atlas camera
movement, and reset actions. Reduced-motion mode removes ambient animation and
snaps transitions while preserving all information.

The primary navigation keeps Map, Offline Atlas, Lists and routes, and Settings
at the same hierarchy level. Settings is divided into Map, Interface, and
Storage and logs categories.

## Pin lists and measurements

VitaMaps stores up to 16 ordered lists with up to 64 points each. Every list
has a selectable color, icon, visibility state, and open/closed geometry. Icons
include pin, star, flag, camp, water, and summit.

Pin placement uses the center crosshair. Placing the crosshair over the first
point of a list with at least three points closes the polygon instead of adding
a duplicate endpoint. Reopening or adding a point removes the closing edge.

The map and list editor show:

- distance for each segment;
- total geodesic distance;
- closing perimeter for a closed list;
- spherical polygon area for a closed list with at least three points;
- optional elevation for each point;
- elevation profile, range, ascent, and descent when available.

These are direct geographic measurements, not road distances or routing
instructions.

## GPX import and export

The worker can import bounded GPX 1.0/1.1 waypoints, routes, and tracks from:

```text
ux0:data/VitaMaps/gpx/inbox
```

Oversized tracks are reduced deterministically to the 64-point list limit while
preserving their endpoints. Imports create styled pin lists and are recorded in
a bounded persistent history. The active list can be exported as a GPX 1.1
route under `ux0:data/VitaMaps/gpx/exports`.

## Search, POIs, and elevation

The native Vita IME accepts coordinates, populated-place names, localities, and
addresses. Coordinates and 7,342 bundled Natural Earth city names resolve
locally. Other explicit submissions use a rate-limited Nominatim request with
one result and a persistent 128-entry result cache. There is no autocomplete.

Nearby POIs are requested only through an explicit command. The bounded
Overpass query returns up to 64 named amenities, tourism objects, peaks,
springs, caves, and water features within the selected radius. Results appear
as native icons without drawing a second city-label layer over the raster map.

An explicit elevation request sends at most the 64 coordinates from the active
pin list to Open-Meteo/Copernicus. Parsed metric values are stored with their
source and used to build the hiking profile.

## Localization and fonts

The UI supports Italian, English, Japanese, Korean, Simplified Chinese,
Traditional Chinese, Russian, French, Spanish, German, and Portuguese. The
default follows the console language, and the active language changes without
restarting.

Bundled Inter fonts cover Latin, Greek, and Cyrillic. Japanese, Chinese, and
Korean use the Vita system PGF fonts with point-filtered atlas magnification to
avoid the blur introduced by linear enlargement while retaining full console
glyph coverage.

Raster place and road labels are part of each provider image. They cannot be
resized or removed independently without replacing the raster pipeline with a
vector-tile renderer.

## Diagnostics and persistence

Diagnostics always use a bounded 256 KiB RAM buffer. Persistent logging writes
atomic snapshots to:

```text
ux0:data/VitaMaps/session_log.txt
```

Release builds default to disk logging off. Debug builds default it on only
when no valid user preference exists. Disk I/O never runs in the render loop.

VitaMaps displays an early boot frame and degrades network failures to cached
maps and placeholders. Structural startup failures show their stage and error
code on an independent fallback screen instead of silently returning to
LiveArea.

Settings, logs, pin lists, GPX files, geocoding results, and tile caches live
under `ux0:data/VitaMaps/`. Persistent records use bounded formats, CRC checks
where appropriate, temporary-file replacement, and rollback copies.

## Network and privacy boundary

VitaMaps delegates HTTPS to `vita-https`, which provides pinned libcurl with
Mbed TLS, TLS 1.2 minimum, Mozilla CA roots, certificate and hostname
verification, redirects, timeouts, low-speed aborts, and cooperative
cancellation. Certificate verification cannot be disabled through the app.

Normal mapping contacts the selected raster provider. Explicit address search,
POI lookup, and elevation lookup send the required query or coordinates to
Nominatim, Overpass, or Open-Meteo respectively. No account or personal API key
is required. Users should not submit private text to public geocoding services.

VitaMaps requests only the active viewport and a one-tile interactive
look-ahead ring. It does not bulk-fetch provider tiles.
