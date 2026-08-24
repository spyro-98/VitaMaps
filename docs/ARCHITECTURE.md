# VitaMaps architecture

## Scope of milestones 1-2

The first milestone renders live 256x256 PNG XYZ tiles on a PS Vita while all
network, filesystem, and image-decoding work stays outside the render thread.
Milestone 1 includes a native Settings screen for provider, UI language, HUD,
units, crosshair, live cache status/clearing, and diagnostic logging policy.
Milestone 2 adds persistent ordered
pin collections, local geometry, local city lookup, and explicit asynchronous
place/address search. Bulk map downloads remain outside the application.

## Startup and failure containment

The startup sequence mirrors the proven Vita lifecycle used by the sibling
VitaTube project where the two applications share runtime requirements:

1. initialize the bounded RAM diagnostic buffer and read preferences;
2. initialize vita2d and validate both its GXM context and framebuffer;
3. present a no-font boot frame immediately;
4. load `SCE_SYSMODULE_PGF`, four script-aware system PGFs, and three
   independent exact-size Inter instances;
5. initialize `vita-https` and retain ownership through worker shutdown;
6. start a native `sceKernelCreateThread` tile worker with an explicit 1 MiB
   stack and VitaSDK's documented `0x10000100` priority token, then construct
   the renderer, input, and map screen. If that token is rejected, retry once
   at a valid priority derived from the current main thread.

VitaSDK's static libstdc++ can expose the C++ threading API while its weak
gthread activation probe still reports a single-threaded runtime. VitaMaps
therefore does not use `std::thread`, `std::mutex`, or
`std::condition_variable`: the worker follows VitaTube's explicit kernel-thread
lifecycle, and bounded critical sections use an atomic spin mutex. The idle
scheduler polls every 8 ms, releases its lock before sleeping, and has only one
consumer.

Network initialization is degradable: the renderer stays alive with cached
tiles and placeholders. Structural failures are logged with a named stage,
vita2d is shut down, and the independent VitaSDK debug-screen backend displays
the code instead of returning silently to LiveArea. Worker exceptions are
caught at their thread boundary and recorded rather than terminating the
process.

The diagnostic buffer retains the newest 256 KiB in RAM. A settings bit enables
atomic snapshots at `ux0:data/VitaMaps/session_log.txt`; Debug builds default
that bit on only when no valid preferences record exists. Settings and log
writes use temporary-file replacement so power loss cannot expose a partially
written current record.

Persistent cache scans and recursive clearing run only on the existing tile
worker. A clear request cancels the active transfer, empties queued prefetch,
then removes the disk and compressed-RAM layers. The completion result returns
to the main thread, which destroys GPU textures through vita2d. The Settings
screen therefore remains responsive and never races a cache read or write.

## Coordinate spaces and camera

`mercator.cpp` maps latitude/longitude to normalized Web Mercator world space.
Longitude wraps, latitude clamps to ±85.0511287798066°, and `double` precision
is retained through camera and visible-rectangle calculations.

The camera zoom and bearing are continuous. At zoom 12.35 the renderer retains level-12
tiles at a greater-than-1.0 GPU scale; selecting level 13 early would shrink
its rasterized labels on the physical Vita display. The renderer advances at
the next integer and uses cached parents until the sharper level is ready.
Screen movement is converted through `256 * 2^zoom`, so touch panning remains
sub-tile and independent of the selected integer tile level. Screen/world
conversion applies the inverse bearing while map rendering, pin projection,
and world/screen conversion apply the forward bearing. This keeps the center
coordinate invariant while movement remains relative to the rotated display.

The renderer maintains these boundaries:

```text
latitude/longitude <-> normalized world <-> tile-level pixels
                                             |
                                             +-> viewport pixels
```

## Frame pipeline

Each frame performs bounded main-thread work:

1. sample controller and front touch input;
2. update camera position, fractional zoom, and inertia with `dt`;
3. if Settings or a collection editor is open, draw it without scheduling or
   uploading tiles;
4. otherwise calculate the bearing-expanded visible tile bounds and one-tile
   look-ahead ring;
5. replace the manager's wanted set and cancel an obsolete active transfer;
6. upload at most two already-decoded RGBA tiles;
7. rotate ready tiles or cached parent/child-level fallbacks about their tile
   centers with a small shared-edge overlap, then rotate neutral borderless
   placeholders and place cyan/white loaders at their transformed centers;
8. draw each user-selected list, its open or closed route, per-segment distance
   labels, pin markers, the optional timed HUD/north control, center crosshair,
   scale, and mandatory provider attribution.

Pin collection format version 2 stores `visible` and `closed` flags per list.
The loader accepts version 1 and migrates old lists as visible, open routes.
Only an explicitly closed list with at least three points produces an area;
the closing edge is drawn and included in the perimeter. Adding another point
reopens the route, avoiding an ambiguous polygon with a moving closing edge.

vita2d texture creation, mutation, use, and destruction are render-thread-only.
Decoded workers return ownership through a mutex-protected upload queue.

## Native UI motion

`ui/motion` provides allocation-free exponential motion values, opacity
composition, easing, and the shared cartographic focus glow. Every response is
expressed per second and receives a delta clamped to 50 ms, following the
frame-pacing-safe behavior proven in VitaTube. The system covers HUD opacity
and edge movement, crosshair composition, transient messages, button press
feedback, settings/list focus travel, IME entrance,
and fade-through transitions between full-screen modes.

Transitions keep the outgoing mode alive until its closing phase is fully
covered, switch state only at the opaque midpoint, then reveal the new mode.
Input is ignored during this short interval so a held button cannot activate a
control on the destination screen. The persistent reduced-motion preference
snaps positional/visibility changes and disables ambient pulses without
changing input behavior or information hierarchy.

## Tile identity and state

`TileKey` contains provider ID, zoom, canonical wrapped X, and Y. Provider ID
also versions the disk namespace; a future style/version field can become part
of the provider ID without changing cache code.

```text
Missing -> Queued -> DiskLookup
                    |        |
                    | hit    | miss
                    v        v
                 Decoding <- Downloading -> Downloaded
                    |
                    v
            RGBA upload queue -> Ready

Any active stage -> Missing when obsolete
Any failing stage -> Failed -> timed retry
Ready -> Missing on GPU eviction
```

Only one record can be queued per key. The queue is replaced from the current
viewport every frame. Old entries are checked against both record state and
the latest wanted set before work begins.

## Scheduling and cancellation

Lower numerical priority wins. Visible tiles are ranked by squared distance
from the screen center. The immediately adjacent ring receives a large
penalty, while a bounded dot-product bias favors the current movement
direction. This is modest interactive look-ahead, not area preloading.

The first milestone intentionally uses one worker. It avoids simultaneous
full PNG decode buffers, respects the community tile endpoint, and is enough
to overlap I/O with rendering. Splitting disk, network, and decode pools is
only justified after on-device profiling shows a bottleneck and memory budgets
are re-measured.

When the wanted set changes, queued tasks become cheap no-ops. If the active
key is no longer wanted, or it is prefetch work while a visible tile is queued,
the worker's `volatile int` cancellation flag is set and passed directly to
`vita_https_perform` for cooperative cancellation.

## Cache hierarchy

| Level | Contents | Budget | Policy |
|---|---|---:|---|
| L1 | vita2d RGBA textures | 12 MiB | LRU, current-frame entries protected |
| L2 | Compressed PNG payloads | 6 MiB | Worker-local LRU |
| L3 | PNG files | 96 MiB per style | Atomic writes, oldest eligible file first |
| L4 | HTTPS provider | One active request | Timeout, low-speed abort, cancellation |

Each style ID owns a separate disk directory and an independent 96 MiB budget;
one style can never evict another. Tiles cannot be evicted before the minimum
seven-day eviction-protection period. Once eligible, oldest modification times are removed
until that style reaches its target. If a namespace is full and every entry is
still protected, the new response is rendered and retained in RAM but is not
admitted to disk, so the cache does not grow past its per-style budget.
There is no time expiry on read: an existing tile remains usable until budget
eviction removes it. Scans run on the worker at startup, when admission needs
space, and after each batch of 16 writes.

The current `vita-https` response type does not expose `ETag`,
`Last-Modified`, `Cache-Control`, or `Expires`, so milestone 1 uses the policy's
seven-day fallback. Adding response-header access to the transport would allow
conditional GETs without weakening TLS ownership or duplicating curl.

## Image decoder choice

Milestone 1 uses VitaSDK libpng's simplified `png_image` API:

- the initial OSM endpoint serves PNG tiles;
- decoding has a small, format-specific surface;
- dimensions are rejected unless exactly 256x256;
- encoded responses are capped at 4 MiB;
- output is a predictable RGBA buffer suitable for the upload queue.

`stb_image` would add unused format code and a second PNG implementation.
FFmpeg is substantially too large for static map images. libjpeg-turbo remains
the preferred future path for providers that explicitly serve JPEG; it should
be added behind a decoder interface when such a provider is introduced.

## Provider boundary

`MapProvider` supplies stable style ID, display name, attribution, zoom bounds,
tile size, URL generation, and style selection. URL generation receives the
ID stored in `TileKey`, so changing style cannot redirect an older in-flight
request into the wrong cache namespace. Renderer, scheduler, and caches never
contain an OSM hostname.

The provider boundary intentionally has no bulk-download capability. The app
requests only the interactive viewport and look-ahead ring; disk persistence is
a consequence of normal navigation, never permission to prefetch an area.

## Pin collections and local geometry

Pin collections are bounded to 16 lists and 64 ordered points per list. They
are serialized under `ux0:data/VitaMaps/pin_collections.bin` with explicit
length limits, CRC32, temporary-file replacement, and one rollback copy.
Adding, renaming, deleting, activating, and reordering is main-thread state;
disk writes happen only on explicit editor actions, never in the render loop.

Each pin stores a stable ID, latitude/longitude, UTF-8 name, and a reserved
address field. Segment/total distances use the haversine formula. Lists with
at least three points expose a closed spherical polygon area. These are local
geometric measurements, not road distances; route guidance is not displayed
until an actual routing response exists.

The direct `sceIme` editor is loaded lazily and uses the existing vita2d
framebuffer. AppUtil/CommonDialog is not initialized. It accepts the same
English, Italian, Russian, Japanese, Korean, Simplified Chinese, and
Traditional Chinese language set as the font pipeline. The UI language is
persisted as a compact preference, defaults to the console language, and
changes immediately without restarting. The text-entry page and Vita IME expose
only the selected language; the application does not draw a simultaneous
multi-language test block. Local coordinate input
is parsed without any network access.

## Place and address search

Search is always an explicit IME submission, never autocomplete. Coordinates
are parsed first, then exact names are checked against the preprojected Natural
Earth table. Remaining queries enter the existing kernel worker at higher
priority than tile work; an active tile transfer is cooperatively cancelled,
so the render thread remains free. Successful network results are atomically
cached in a 128-entry hashed store under `ux0:data/VitaMaps/search_cache`.

The default HTTPS backend is public Nominatim and is constrained to a single
result, at least one second between remote calls, a distinct User-Agent,
visible OSM attribution, no batching, and no autocomplete. An HTTPS endpoint in
`ux0:data/VitaMaps/geocoder_url.txt` overrides the default without a software
update. The JSON parser accepts only the bounded fields required for a point
and display name and validates coordinates before returning to the UI.

## Overlay extension point

The rendering order is reserved as:

```text
background -> raster tiles -> routes/polylines
           -> POIs -> markers -> current location -> status UI
```

Overlay models should use normalized world coordinates and be projected by the
same camera transform. They must not be baked into tile textures.

The generated table of 7,342 Natural Earth populated places is an invisible
local search index only. It is never submitted to the render pipeline. Place,
road, and locality names visible on the map therefore come exclusively from the
selected provider's raster tiles.

## Hardware validation gate

Before publishing a binary release, test the exact VPK on hardware for:

- visible boot frame, successful PGF text, and Settings navigation;
- Debug-default log creation plus disabling/re-enabling persistent writes;
- forced offline startup and a readable fatal stage/code for structural errors;
- startup with Wi-Fi disconnected and connected;
- certificate-verified tile downloads and cancellation during fast pan;
- RGBA channel order and tile seams at fractional zoom;
- one- and two-finger input stability, twist rotation, right-stick bearing,
  rotated-tile corner coverage, and north reset from SELECT/touch HUD control;
- direct IME open/confirm/cancel in every enabled language without startup
  regression or CommonDialog initialization;
- create, rename, reorder, delete, hide/show, close/reopen, restart, and reload
  pin collections; verify every segment label, closing perimeter, and area;
- pin projection and route-line wrapping near the international date line;
- city lookup without network, address lookup success/not-found/error, query
  cache reuse, and geocoder rate limiting;
- complete command legends and readable drawn button symbols on every screen;
- cyan/white missing-tile loaders with no yellow warning palette;
- stable frame pacing while cache files are written and evicted;
- HUD timeout/tap fade, selector travel, button feedback, reduced-motion mode,
  and Settings/list/map transitions without inherited input;
- long-session RAM/CDRAM usage and LRU behavior;
- suspend/resume and clean exit while a transfer is active;
- server-policy behavior: identifying User-Agent, visible attribution, no bulk
  fetch, one-second geocoder limit, and persistent cache reuse.
