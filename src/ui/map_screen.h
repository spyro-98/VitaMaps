#pragma once

#include "map/map_camera.h"
#include "map/pin_collection.h"
#include "map/tile_manager.h"
#include "providers/map_provider.h"
#include "render/map_renderer.h"
#include "ui/motion.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>

#include <cstdint>
#include <string>

namespace vitamaps {

class MapScreen {
public:
    MapScreen(MapProvider &provider, TileManager &manager,
              MapRenderer &renderer, bool https_initialized,
              bool pgf_available);
    ~MapScreen();

    bool initialize();
    void update(double dt, bool &quit);
    void prepare(std::uint64_t frame);
    void draw(std::uint64_t frame);
    void shutdown();

private:
    enum class ScreenMode {
        Map,
        Settings,
        PinLists,
        PinList,
    };

    enum class TransitionPhase {
        None,
        Closing,
        Opening,
    };

    enum class SettingRow {
        MapStyle = 0,
        Language,
        Cache,
        HudBehavior,
        Crosshair,
        Units,
        Animations,
        DiskLogging,
        Count,
    };

    struct TouchPoint {
        float x{0.0F};
        float y{0.0F};
    };

    static double analog_axis(unsigned char value);
    TouchPoint map_touch(const SceTouchReport &report) const;
    void update_touch(double dt, bool &manual_input);
    void reset_touch_state();
    void update_settings(double dt, unsigned int pressed, bool force_quit);
    void update_pin_lists(unsigned int pressed);
    void update_pin_list(unsigned int pressed);
    void begin_search();
    void poll_search_result();
    void poll_cache_result();
    void begin_pinning();
    void capture_pin();
    bool persist_pins(const char *success_message = nullptr);
    void show_message(const std::string &message, bool error = false,
                      double seconds = 3.0);
    bool edit_text(const char *title, const char *initial,
                   char *output, std::size_t capacity);
    void request_mode(ScreenMode mode);
    void update_animations(double dt, unsigned int pressed);
    bool transition_blocks_input() const;
    void draw_transition_veil();
    void draw_chrome();
    void draw_attribution();
    void draw_crosshair();
    void draw_scale();
    void draw_settings();
    void draw_pin_overlays();
    void draw_pin_lists();
    void draw_pin_list();
    void draw_message();
    void toggle_hud();
    void change_setting(int direction);
    void toggle_disk_logging();

    MapProvider &provider_;
    TileManager &manager_;
    MapRenderer &renderer_;
    bool https_initialized_{false};
    bool pgf_available_{false};
    ScreenMode mode_{ScreenMode::Map};
    ScreenMode pending_mode_{ScreenMode::Map};
    TransitionPhase transition_phase_{TransitionPhase::None};
    MapCamera camera_{};
    MapViewport viewport_{0.0F, 0.0F, 960.0F, 544.0F};
    vita2d_font *font_small_{nullptr};
    vita2d_font *font_body_{nullptr};
    vita2d_font *font_display_{nullptr};
    SceCtrlData previous_controls_{};
    SceTouchPanelInfo touch_panel_{};
    bool touch_sampling_started_here_{false};
    bool touch_initialized_{false};
    int previous_touch_count_{0};
    TouchPoint previous_touch_a_{};
    TouchPoint previous_touch_b_{};
    TouchPoint previous_touch_midpoint_{};
    float previous_pinch_distance_{0.0F};
    float previous_pinch_angle_{0.0F};
    TouchPoint tap_start_{};
    bool tap_candidate_{false};
    double touch_duration_{0.0};
    bool hud_visible_{true};
    double hud_timer_{2.5};
    double animation_seconds_{0.0};
    UiMotionValue hud_motion_{1.0F, 15.0F};
    UiMotionValue crosshair_motion_{0.0F, 18.0F};
    UiMotionValue message_motion_{0.0F, 18.0F};
    UiMotionValue settings_message_motion_{0.0F, 18.0F};
    UiMotionValue screen_motion_{1.0F, 24.0F};
    UiMotionValue settings_focus_y_{74.0F, 18.0F};
    UiMotionValue list_focus_y_{82.0F, 18.0F};
    UiMotionValue pin_focus_y_{137.0F, 18.0F};
    UiMotionValue button_feedback_{0.0F, 8.0F};
    std::uint32_t last_action_button_{0};
    double stats_timer_{10.0};
    double connection_timer_{10.0};
    bool connected_{false};
    TileManagerStats cached_stats_{};
    int settings_result_{0};
    double settings_message_timer_{0.0};
    SettingRow selected_setting_{SettingRow::MapStyle};
    PinRepository pins_{};
    bool pinning_{false};
    std::size_t selected_list_{0};
    std::size_t selected_pin_{0};
    std::string message_;
    bool message_error_{false};
    double message_timer_{0.0};
    bool delete_confirm_armed_{false};
    double delete_confirm_timer_{0.0};
    bool search_pending_{false};
    TileCacheOperationResult cache_status_{};
    bool cache_status_valid_{false};
    bool cache_clear_pending_{false};
    bool cache_clear_confirm_{false};
    double cache_clear_confirm_timer_{0.0};
    double cache_refresh_timer_{0.0};
    std::string settings_feedback_;
};

} // namespace vitamaps
