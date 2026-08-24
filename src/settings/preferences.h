#pragma once

namespace vitamaps {

int preferences_init();
bool preferences_disk_logs_enabled();
int preferences_set_disk_logs_enabled(bool enabled);
int preferences_map_style();
int preferences_set_map_style(int index);
int preferences_ui_language();
int preferences_set_ui_language(int index);
bool preferences_hud_auto_hide();
int preferences_set_hud_auto_hide(bool enabled);
bool preferences_crosshair_enabled();
int preferences_set_crosshair_enabled(bool enabled);
bool preferences_imperial_units();
int preferences_set_imperial_units(bool enabled);
bool preferences_reduce_motion();
int preferences_set_reduce_motion(bool enabled);
bool preferences_debug_default();

} // namespace vitamaps
