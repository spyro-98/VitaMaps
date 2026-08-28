# VitaMaps controls

This is the complete VitaMaps 01.23 input reference. Button legends inside the
application change with the active screen and mode.

## Global

| Input | Action |
|---|---|
| Start + Select | Exit VitaMaps |
| Circle | Return to the previous mode or screen |

## Primary navigation

| Input | Action |
|---|---|
| Up/Down | Select Map, Offline Atlas, Lists and routes, or Settings |
| X | Open the selected section |
| Circle | Return to the map |

## Map

| Input | Action |
|---|---|
| Left analog | Continuous pan |
| D-pad | Pan |
| Right analog horizontal | Rotate the map continuously |
| Front-touch drag | Pan with release inertia |
| Two-finger gesture | Combined pan, pinch zoom, and twist rotation |
| L/R | Zoom out/in |
| Short front-screen tap | Show or hide the timed HUD |
| Select | Smoothly reset the map to north-up |
| HUD north control | Smoothly reset the map to north-up |
| X | Enter pin mode |
| Square | Open Lists and routes |
| Triangle | Search coordinates, place, locality, or address |
| Select + Triangle | Request nearby Overpass POIs |
| Start | Open primary navigation |

Touch drag, pinch, and twist never reveal the HUD. Only a short tap toggles it.

## Pin placement

| Input | Action |
|---|---|
| Left analog/D-pad/touch | Move the map under the center crosshair |
| X | Store the center point in the active list |
| X over the first marker | Close a list with at least three points instead of adding a duplicate |
| Circle | Finish pin placement |

## Offline Atlas overview

| Input | Action |
|---|---|
| Up/Down | Select an actually cached zoom layer |
| Left/Right | Decrease/increase the distance between layers |
| Triangle | Cycle map styles that have cached data |
| Left analog | Pan the complete layer stack |
| Right analog | Orbit the stack through 360 degrees on both axes |
| L/R | Move the atlas camera away/toward the layers |
| Square | Enter navigation inside the selected layer |
| X | Open the selected cached tile on the normal map |
| Select | Reset orbit, spacing, pan, and overview magnification |
| Start | Refresh the cache index on the worker |
| Circle | Return to primary navigation |

## Offline Atlas layer navigation

| Input | Action |
|---|---|
| D-pad | Select a real cached neighbouring tile; sparse gaps snap to the nearest key in that direction |
| Left analog | Pan geographically inside the selected layer and move its cache-only request window |
| Right analog | Orbit the visible layered context |
| L/R | Zoom the selected geographic layer out/in |
| X | Open the focused cached tile on the normal map at matching style and zoom |
| Square | Return to layer overview |
| Select | Recenter and refit the selected layer |
| Start | Refresh the cache index |
| Circle | Leave layer navigation; a second press returns to primary navigation |

## List overview

| Input | Action |
|---|---|
| Up/Down | Select a list |
| X | Open and activate the selected list |
| R | Toggle whether the list is visible on the map |
| Left/Right | Change the selected list color/icon |
| Triangle | Create a list |
| Square | Rename the selected list |
| Select twice | Confirm deletion |
| L | Open GPX management |
| Circle | Return to primary navigation |

## List detail

| Input | Action |
|---|---|
| Up/Down | Select a point |
| L/R | Move the selected point earlier/later in the list |
| Select | Close or reopen a list with at least three points |
| Circle | Return to list overview |

Closing a list draws the last-to-first segment, includes it in the perimeter,
and enables polygon area. Adding another point reopens it.

## GPX management

| Input | Action |
|---|---|
| Up/Down | Select a GPX inbox file |
| X | Import the selected file as a new pin list |
| Square | Export the active list as GPX 1.1 |
| Triangle | Refresh the inbox |
| Left/Right | Page through the persisted import history |
| Circle | Return to list overview |

## Settings

| Input | Action |
|---|---|
| L/R | Change between Map, Interface, and Storage and logs categories |
| Up/Down | Select an option in the current category |
| X or Left/Right | Change the selected option |
| X twice on Map cache | Confirm asynchronous cache clearing |
| Circle | Return to primary navigation |

Settings include map style, hiking mode, UI language, HUD behavior, independent
scale visibility, center crosshair, metric/imperial units, full/reduced motion,
cache status/clearing, and persistent diagnostic logging.
