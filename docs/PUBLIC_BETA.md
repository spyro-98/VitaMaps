# VitaMaps 1.0.0 release guide

For a detailed description of every subsystem, see the
[feature guide](FEATURES.md). For the full input reference, see
[controls](CONTROLS.md).

VitaMaps is a native PlayStation Vita map viewer built with VitaSDK and vita2d.
It renders raster map tiles directly on the GPU and does not use a WebView,
HTML, JavaScript, or a browser as its map engine.

VitaMaps 1.0.0 is the first public release. Some systems remain experimental,
and the exact package still benefits from broad physical-hardware coverage.

## What to test

- Map movement, fractional zoom, rotation, touch gestures, and north reset.
- Tile loading, progressive fallback, retries, and online/offline transitions.
- Map-style switching and the independent 200 MiB persistent cache per style.
- Search, pin lists, closed-area calculations, GPX import/export, POIs, and
  hiking elevation profiles.
- HUD timing, map scale, UI localization, and Settings persistence.
- Offline Atlas orbit, layer selection, view zoom, geographic pan, tile focus,
  and handoff back to the map.

## Offline Atlas: a beta cache-level visualizer

The Offline Atlas began with a simple question: what is actually inside the
tile cache?

Normal map use leaves behind persistent raster tiles, but a directory tree does
not communicate their geography, gaps, or available zoom depth. The atlas makes
that data visible by grouping real cached XYZ tiles by map style and zoom level,
then displaying them as navigable 3D layers on the Vita.

The experiment is also useful for evaluating whether a future offline feature
could let a user select an area and download only the necessary zoom levels.
That download system is not included in 1.0.0. The current atlas:

- reads only tiles already collected during ordinary browsing;
- never starts provider downloads while exploring the atlas;
- leaves an honest gap when a cached file is missing;
- does not guarantee complete coverage of a city, region, or route.

“Offline” therefore describes the source used by the atlas and the ability to
reuse accumulated tiles, not a bulk regional download feature.

## Installation

1. Download the `VitaMaps.vpk` attached to the GitHub release.
2. Transfer it to the PS Vita and install it with a homebrew package manager.
3. Start VitaMaps from LiveArea.

The application stores user data under `ux0:data/VitaMaps/`. Removing the VPK
does not necessarily remove this directory or its cached tiles.

Developers can build from source by following the standalone instructions in
the [README](../README.md#standalone-clone-and-build). Clone recursively because
`vita-https` is a pinned Git submodule.

## Networking, cache, and privacy

VitaMaps uses HTTPS through `vita-https`. Interactive map use contacts the
selected raster provider. Explicit place/address search can contact Nominatim,
nearby-POI lookup can contact Overpass, and elevation lookup can contact
Open-Meteo. Queries or coordinates required for those operations are sent to
the corresponding service.

There is no account system and no personal API key is required. Do not enter
private or confidential text into public geocoding searches.

Each map style owns a separate persistent cache with a 200 MiB limit. Cached
tiles have no age-based expiry; after the limit is reached, newer admitted
tiles replace the least recently used tiles in that same style. Cache status
and clearing are available under **Settings > Storage and logs**.

## Diagnostics and bug reports

Debug builds enable persistent logging by default. Release builds can enable it
under **Settings > Storage and logs > Persistent logs**. The log is written to:

```text
ux0:data/VitaMaps/session_log.txt
```

Please include the following in a GitHub issue:

- Vita model and firmware/homebrew environment;
- VitaMaps version and whether the build is Release or Debug;
- exact actions needed to reproduce the problem;
- selected map style and whether the HUD reported ONLINE or OFFLINE;
- whether the problem survives an application restart;
- the session log, when available;
- a photo or GPU crash dump for rendering failures.

Do not publish secrets or unrelated personal information in logs or reports.

## Known limitations

- The Offline Atlas is experimental and visualizes accumulated cache only.
- There is no area-based tile downloader or guaranteed complete offline region.
- Raster labels are drawn into provider tiles and cannot be resized separately.
- Turn-by-turn routing is not implemented.
- Public community services are best-effort and can rate-limit or fail.
- Local VPK validation does not replace testing on physical Vita hardware.

## Credits and license

VitaMaps is GPL-3.0-only software. Map and service attribution remains visible
in the application and is documented in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The application icon is derived from NASA's Apollo 17
[“Blue Marble” photograph AS17-148-22727](https://science.nasa.gov/resource/the-blue-marble/).
Image credit: NASA Johnson Space Center. The icon contains no NASA identifier
or logo and does not imply NASA endorsement. See NASA's
[image and media usage guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/).
