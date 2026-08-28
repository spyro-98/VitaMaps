# VitaMaps 01.23 public beta announcement copy

Replace `[release link]` with the public GitHub release URL after the release is
published. The repository URL can remain unchanged.

## Reddit

### Suggested title

VitaMaps 01.23 public beta — a native map viewer and 3D cache atlas for PS Vita

### Post

Hi everyone,

I am releasing the public beta of VitaMaps, a fully native map viewer for the
PS Vita. It is built with VitaSDK and vita2d and renders raster tiles directly
on the GPU—there is no WebView, browser, HTML, or JavaScript map engine.

The current build supports fluid pan, fractional zoom, map rotation, touch
gestures, multiple map styles, search, styled pin lists, distance and area
measurements, GPX import/export, nearby POIs, hiking mode with elevation
profiles, UI localization, and a persistent 200 MiB cache for each map style.

The most unusual part is the **Offline Atlas**, currently a beta feature. It
started from my curiosity to see what was actually stored inside the tile
cache. Instead of treating the cache as an invisible directory, VitaMaps groups
real cached XYZ tiles by style and zoom and displays them as navigable 3D
layers. You can orbit the stack, adjust layer spacing, zoom into a level, move
between cached tiles, and reopen one on the normal map.

This is also an experiment for evaluating a possible future offline-download
workflow: selecting an area and deciding which zoom levels are really useful
before downloading anything. That workflow does **not** exist in this release.
The atlas never bulk-downloads tiles and never fills missing areas; it only
shows tiles accumulated during ordinary map browsing. If a tile is not in the
cache, the gap remains visible.

VitaMaps uses `vita-https` for authenticated HTTPS and keeps provider
attribution visible in the renderer. The application icon is derived from the
Apollo 17 “Blue Marble” photograph (AS17-148-22727), with image credit to NASA
Johnson Space Center.

This is still a beta, so hardware feedback matters—especially around GPU
stability, cache persistence, touch/analog controls, localization, and the 3D
atlas on large or sparse caches. Persistent logging can be enabled in Settings;
debug builds enable it by default.

Repository: https://github.com/spyro-98/VitaMaps

Release: [release link]

If you test it, please include your Vita model, firmware environment, selected
map style, exact reproduction steps, and `ux0:data/VitaMaps/session_log.txt`
when it is available. Photos and GPU crash dumps are also very useful.

## X thread

### 1/7

VitaMaps 01.23 is entering public beta: a fully native, GPU-rendered map viewer
for PS Vita. No WebView, browser, HTML, or JavaScript map engine—just VitaSDK,
vita2d, Web Mercator, and asynchronous HTTPS tile loading. #PSVita

### 2/7

It includes fluid pan/zoom/rotation, touch gestures, several map styles,
search, styled pin lists, distance and area tools, GPX import/export, POIs,
hiking elevation profiles, localization, and a persistent cache per style.

### 3/7

Its strangest feature is the beta Offline Atlas. It began with one question:
what is actually inside the tile cache? VitaMaps turns real cached XYZ tiles
into a 3D stack grouped by map style and zoom level.

### 4/7

You can orbit the cache stack, change layer spacing, zoom into a level, browse
its stored tiles, and jump back to the matching place on the map. It makes
offline coverage and missing zoom levels visible instead of hiding them in
folders.

### 5/7

The atlas is an experiment, not a bulk downloader. It only uses tiles collected
during normal browsing and leaves missing areas empty. The goal is to evaluate
a future, deliberate area/zoom download workflow without pretending it exists
today.

### 6/7

This beta needs real-hardware feedback, especially for GPU stability, touch and
analog input, cache persistence, localization, and large 3D atlas stacks.
Logs can be enabled in Settings and are on by default in Debug builds.

### 7/7

Source and build instructions:
https://github.com/spyro-98/VitaMaps

Release: [release link]

Icon imagery: Apollo 17 “Blue Marble,” NASA Johnson Space Center.
#VitaHomebrew #OpenStreetMap
