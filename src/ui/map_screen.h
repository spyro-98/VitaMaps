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
#include <vector>

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
        Navigation,
        Settings,
        PinLists,
        PinList,
        Gpx,
        OfflineAtlas,
    };

    enum class NavigationItem {
        Map = 0,
        OfflineAtlas,
        PinLists,
        Settings,
        Count,
    };

    enum class TransitionPhase {
        None,
        Closing,
        Opening,
    };

    enum class SettingRow {
        MapStyle = 0,
        Hiking,
        Language,
        Cache,
        HudBehavior,
        MapScale,
        Crosshair,
        Units,
        Animations,
        DiskLogging,
        Count,
    };

    enum class SettingCategory {
        Map = 0,
        Interface,
        Storage,
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
    void update_navigation(unsigned int pressed);
    void update_settings(double dt, unsigned int pressed, bool force_quit);
    void update_pin_lists(unsigned int pressed);
    void update_pin_list(unsigned int pressed);
    void update_gpx(unsigned int pressed);
    void update_offline_atlas(double dt, const SceCtrlData &controls,
                              unsigned int pressed);
    const OfflineAtlasLayer *selected_offline_atlas_layer() const;
    void sync_offline_atlas_selection(bool prefer_camera_position);
    void move_offline_atlas_layer(int direction);
    void move_offline_atlas_tile(int direction_x, int direction_y);
    void build_offline_atlas_requests();
    bool open_offline_atlas_target();
    int setting_category_row_count() const;
    SettingRow setting_category_row(int index) const;
    int selected_setting_index() const;
    void change_setting_category(int direction);
    void begin_search();
    void poll_search_result();
    void poll_cache_result();
    void poll_gpx_result();
    void poll_poi_result();
    void poll_elevation_result();
    void request_pois();
    void request_elevation_for_list(std::size_t list_index);
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
    void draw_navigation();
    void draw_chrome();
    void draw_attribution();
    void draw_crosshair();
    void draw_scale();
    void draw_settings();
    void draw_pin_overlays();
    void draw_pin_lists();
    void draw_pin_list();
    void draw_gpx();
    void draw_offline_atlas(std::uint64_t frame);
    void draw_pois();
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
    UiMotionValue scale_motion_{1.0F, 15.0F};
    UiMotionValue crosshair_motion_{0.0F, 18.0F};
    UiMotionValue message_motion_{0.0F, 18.0F};
    UiMotionValue settings_message_motion_{0.0F, 18.0F};
    UiMotionValue screen_motion_{1.0F, 24.0F};
    UiMotionValue settings_focus_y_{72.0F, 18.0F};
    UiMotionValue navigation_focus_y_{112.0F, 17.0F};
    UiMotionValue list_focus_y_{82.0F, 18.0F};
    UiMotionValue pin_focus_y_{137.0F, 18.0F};
    UiMotionValue gpx_focus_y_{108.0F, 18.0F};
    UiMotionValue button_feedback_{0.0F, 8.0F};
    std::uint32_t last_action_button_{0};
    double stats_timer_{10.0};
    double connection_timer_{10.0};
    bool connected_{false};
    TileManagerStats cached_stats_{};
    int settings_result_{0};
    double settings_message_timer_{0.0};
    NavigationItem selected_navigation_{NavigationItem::OfflineAtlas};
    SettingCategory selected_setting_category_{SettingCategory::Map};
    SettingRow selected_setting_{SettingRow::MapStyle};
    ScreenMode pin_lists_return_mode_{ScreenMode::Map};
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
    std::vector<GpxInboxEntry> gpx_inbox_;
    std::vector<GpxImportRecord> gpx_history_;
    std::size_t selected_gpx_{0};
    std::size_t gpx_history_offset_{0};
    std::vector<PointOfInterest> pois_;
    OfflineAtlasSnapshot offline_atlas_{};
    bool offline_atlas_valid_{false};
    int offline_atlas_style_{0};
    int atlas_selected_zoom_{-1};
    std::size_t atlas_selected_tile_{0};
    bool atlas_layer_browse_{false};
    float atlas_yaw_{-0.38F};
    float atlas_yaw_target_{-0.38F};
    float atlas_pitch_{0.82F};
    float atlas_pitch_target_{0.82F};
    UiMotionValue atlas_focus_zoom_{0.0F, 11.0F};
    UiMotionValue atlas_spacing_{24.0F, 12.0F};
    UiMotionValue atlas_view_scale_{1.0F, 11.0F};
    UiMotionValue atlas_pan_x_{0.0F, 11.0F};
    UiMotionValue atlas_pan_y_{0.0F, 11.0F};
    UiMotionValue atlas_world_center_x_{0.5F, 10.0F};
    UiMotionValue atlas_world_center_y_{0.5F, 10.0F};
    std::vector<TileRequest> atlas_tile_requests_;
};

} // namespace vitamaps
