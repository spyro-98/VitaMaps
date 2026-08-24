#include "ui/map_screen.h"

#include "core/log.h"
#include "map/place_lookup.h"
#include "settings/preferences.h"
#include "ui/font.h"
#include "ui/localization.h"
#include "ui/text_input.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace vitamaps {
namespace {
constexpr unsigned int kTopBar = RGBA8(15, 22, 30, 245);
constexpr unsigned int kBottomBar = RGBA8(15, 22, 30, 235);
constexpr unsigned int kPrimaryText = RGBA8(242, 246, 250, 255);
constexpr unsigned int kSecondaryText = RGBA8(174, 188, 202, 255);
constexpr unsigned int kAccent = RGBA8(88, 190, 255, 255);
constexpr unsigned int kSettingsBackground = RGBA8(18, 27, 36, 255);
constexpr unsigned int kPanel = RGBA8(30, 43, 55, 255);
constexpr unsigned int kPanelSelected = RGBA8(39, 57, 72, 255);
constexpr unsigned int kSuccess = RGBA8(94, 219, 148, 255);
constexpr unsigned int kError = RGBA8(255, 116, 116, 255);
constexpr unsigned int kShadow = RGBA8(4, 8, 12, 210);
constexpr unsigned int kHudPanel = RGBA8(15, 22, 30, 210);
constexpr unsigned int kCrosshair = RGBA8(245, 249, 252, 235);
constexpr unsigned int kPin = RGBA8(255, 91, 105, 255);
constexpr unsigned int kPinMuted = RGBA8(255, 170, 178, 220);
constexpr unsigned int kFontSmall = 16;
constexpr unsigned int kFontBody = 20;
constexpr unsigned int kFontDisplay = 28;
constexpr double kHudVisibleSeconds = 2.5;
constexpr float kTapMovementPixels = 12.0F;
constexpr double kTapMaximumSeconds = 0.35;
constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kPi = 3.14159265358979323846;
constexpr float kSettingsRowY[] = {72.0F, 125.0F, 178.0F, 231.0F,
                                   284.0F, 337.0F, 390.0F, 443.0F};
constexpr float kSettingsRowHeight = 44.0F;

float point_distance(float left_x, float left_y, float right_x, float right_y) {
    const float dx = right_x - left_x;
    const float dy = right_y - left_y;
    return std::sqrt(dx * dx + dy * dy);
}

double nice_distance(double maximum) {
    if (maximum <= 0.0) return 0.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(maximum)));
    const double normalized = maximum / magnitude;
    const double factor = normalized >= 5.0 ? 5.0
                        : normalized >= 2.0 ? 2.0 : 1.0;
    return factor * magnitude;
}

void format_distance(double meters, bool imperial, char *output,
                     std::size_t capacity) {
    if (imperial) {
        const double feet = meters * 3.280839895;
        if (feet >= 5280.0)
            std::snprintf(output, capacity, "%.2f mi", feet / 5280.0);
        else if (feet < 1.0)
            std::snprintf(output, capacity, "%.1f in", feet * 12.0);
        else
            std::snprintf(output, capacity, "%.0f ft", feet);
    } else if (meters >= 1000.0) {
        std::snprintf(output, capacity, "%.2f km", meters / 1000.0);
    } else if (meters < 1.0) {
        std::snprintf(output, capacity, "%.0f cm", meters * 100.0);
    } else {
        std::snprintf(output, capacity, "%.0f m", meters);
    }
}

void format_area(double square_meters, bool imperial, char *output,
                 std::size_t capacity) {
    if (imperial) {
        const double square_feet = square_meters * 10.763910417;
        if (square_feet >= 27878400.0)
            std::snprintf(output, capacity, "%.2f mi²",
                          square_feet / 27878400.0);
        else if (square_feet >= 43560.0)
            std::snprintf(output, capacity, "%.2f ac",
                          square_feet / 43560.0);
        else
            std::snprintf(output, capacity, "%.0f ft²", square_feet);
    } else if (square_meters >= 1000000.0) {
        std::snprintf(output, capacity, "%.2f km²",
                      square_meters / 1000000.0);
    } else if (square_meters >= 10000.0) {
        std::snprintf(output, capacity, "%.2f ha", square_meters / 10000.0);
    } else {
        std::snprintf(output, capacity, "%.0f m²", square_meters);
    }
}

void format_cache_size(std::uint64_t bytes, char *output,
                       std::size_t capacity) {
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 10.0)
        std::snprintf(output, capacity, "%.0f MiB", mib);
    else if (mib >= 0.1)
        std::snprintf(output, capacity, "%.1f MiB", mib);
    else
        std::snprintf(output, capacity, "%.0f KiB",
                      static_cast<double>(bytes) / 1024.0);
}

std::size_t visible_first(std::size_t selected, std::size_t visible_rows) {
    return selected < visible_rows ? 0 : selected - visible_rows + 1;
}

int draw_key_hint(vita2d_font *font, int x, const char *key,
                  const char *label, unsigned int button,
                  unsigned int last_button, float feedback, float opacity,
                  float y_offset) {
    if (!font || !key || !label) return x;
    const bool active = button != 0U && (button & last_button) != 0U &&
                        feedback > 0.01F;
    const bool cross = std::strcmp(key, "X") == 0;
    const bool circle = std::strcmp(key, "O") == 0;
    const bool square = std::strcmp(key, "SQUARE") == 0;
    const bool triangle = std::strcmp(key, "TRIANGLE") == 0;
    const bool graphical = cross || circle || square || triangle;
    const int key_width = graphical ? 24 : std::max(24,
        ui_font_text_width(font, kFontSmall, key) + 10);
    const float lift = active ? -1.5F * feedback : 0.0F;
    const unsigned int cap = ui_fade_color(
        active ? kAccent : kPanelSelected, opacity);
    vita2d_draw_rectangle(static_cast<float>(x), 518.0F + y_offset + lift,
                          static_cast<float>(key_width), 20.0F, cap);
    const unsigned int symbol_color = ui_fade_color(
        active ? kSettingsBackground : kPrimaryText, opacity);
    const float center_x = static_cast<float>(x + key_width / 2);
    const float center_y = 528.0F + y_offset + lift;
    if (cross) {
        for (int offset = -1; offset <= 1; ++offset) {
            vita2d_draw_line(center_x - 5.0F, center_y - 5.0F + offset,
                             center_x + 5.0F, center_y + 5.0F + offset,
                             symbol_color);
            vita2d_draw_line(center_x + 5.0F, center_y - 5.0F + offset,
                             center_x - 5.0F, center_y + 5.0F + offset,
                             symbol_color);
        }
    } else if (circle) {
        vita2d_draw_fill_circle(center_x, center_y, 6.5F, symbol_color);
        vita2d_draw_fill_circle(center_x, center_y, 4.0F, cap);
    } else if (square) {
        vita2d_draw_rectangle(center_x - 6.0F, center_y - 6.0F,
                              12.0F, 12.0F, symbol_color);
        vita2d_draw_rectangle(center_x - 3.5F, center_y - 3.5F,
                              7.0F, 7.0F, cap);
    } else if (triangle) {
        vita2d_draw_line(center_x, center_y - 7.0F,
                         center_x - 7.0F, center_y + 6.0F, symbol_color);
        vita2d_draw_line(center_x - 7.0F, center_y + 6.0F,
                         center_x + 7.0F, center_y + 6.0F, symbol_color);
        vita2d_draw_line(center_x + 7.0F, center_y + 6.0F,
                         center_x, center_y - 7.0F, symbol_color);
    } else {
        const int text_width = ui_font_text_width(font, kFontSmall, key);
        ui_font_draw_text(font, x + (key_width - text_width) / 2,
                          static_cast<int>(535.0F + y_offset + lift),
                          symbol_color, kFontSmall, key);
    }
    ui_font_draw_text(font, x + key_width + 5,
                      static_cast<int>(535.0F + y_offset + lift),
                      ui_fade_color(active ? kPrimaryText
                                           : kSecondaryText, opacity),
                      kFontSmall, label);
    return x + key_width + 5 +
        ui_font_text_width(font, kFontSmall, label) + 14;
}
} // namespace

MapScreen::MapScreen(MapProvider &provider, TileManager &manager,
                     MapRenderer &renderer, bool https_initialized,
                     bool pgf_available)
    : provider_(provider), manager_(manager), renderer_(renderer),
      https_initialized_(https_initialized), pgf_available_(pgf_available) {
    camera_.min_zoom = provider_.min_zoom();
    camera_.max_zoom = provider_.max_zoom();
    camera_.target_zoom = camera_.zoom;
}

MapScreen::~MapScreen() { shutdown(); }

bool MapScreen::initialize() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    if (sceCtrlPeekBufferPositive(0, &previous_controls_, 1) < 0) {
        previous_controls_.lx = 127;
        previous_controls_.ly = 127;
        previous_controls_.rx = 127;
        previous_controls_.ry = 127;
    }
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &touch_panel_) >= 0) {
        SceTouchSamplingState state;
        if (sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT, &state) >= 0) {
            if (state != SCE_TOUCH_SAMPLING_STATE_START &&
                sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                                          SCE_TOUCH_SAMPLING_STATE_START) >= 0) {
                touch_sampling_started_here_ = true;
            }
            touch_initialized_ = true;
        }
    }
    const int fallback_result = ui_font_fallback_init();
    font_small_ = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
    font_body_ = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
    font_display_ = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
    log_printf("map screen: controller=ready touch=%d pgf_module=%d "
               "fallback=0x%08X mask=0x%X fonts=%p/%p/%p",
               touch_initialized_ ? 1 : 0, pgf_available_ ? 1 : 0,
               static_cast<unsigned>(fallback_result),
               static_cast<unsigned>(ui_font_fallback_language_mask()),
               font_small_, font_body_, font_display_);
    if (!font_small_ || !font_body_ || !font_display_)
        log_printf("map screen: one or more exact-size Inter faces failed");
    hud_visible_ = true;
    hud_timer_ = kHudVisibleSeconds;
    pending_mode_ = mode_;
    hud_motion_.snap(1.0F);
    screen_motion_.snap(1.0F);
    crosshair_motion_.snap(preferences_crosshair_enabled() ? 1.0F : 0.0F);
    message_motion_.snap(0.0F);
    settings_message_motion_.snap(0.0F);
    const int pins_result = pins_.load(ui_text(UiText::DefaultFavorites));
    if (pins_result < 0)
        log_printf("pin repository fallback used: 0x%08X",
                   static_cast<unsigned>(pins_result));
    return true;
}

void MapScreen::shutdown() {
    if (font_small_) vita2d_free_font(font_small_);
    if (font_body_) vita2d_free_font(font_body_);
    if (font_display_) vita2d_free_font(font_display_);
    font_small_ = nullptr;
    font_body_ = nullptr;
    font_display_ = nullptr;
    ui_font_fallback_term();
    if (touch_sampling_started_here_) {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                                 SCE_TOUCH_SAMPLING_STATE_STOP);
    }
    touch_sampling_started_here_ = false;
    touch_initialized_ = false;
}

double MapScreen::analog_axis(unsigned char value) {
    double axis = (static_cast<double>(value) - 127.5) / 127.5;
    constexpr double dead_zone = 0.18;
    if (std::abs(axis) <= dead_zone) return 0.0;
    const double magnitude = (std::abs(axis) - dead_zone) / (1.0 - dead_zone);
    return std::copysign(magnitude * magnitude, axis);
}

MapScreen::TouchPoint MapScreen::map_touch(const SceTouchReport &report) const {
    const int span_x = touch_panel_.maxDispX - touch_panel_.minDispX;
    const int span_y = touch_panel_.maxDispY - touch_panel_.minDispY;
    TouchPoint point;
    if (span_x > 0) {
        point.x = static_cast<float>(report.x - touch_panel_.minDispX) * 959.0F /
                  span_x;
    }
    if (span_y > 0) {
        point.y = static_cast<float>(report.y - touch_panel_.minDispY) * 543.0F /
                  span_y;
    }
    point.x = std::clamp(point.x, 0.0F, 959.0F);
    point.y = std::clamp(point.y, 0.0F, 543.0F);
    return point;
}

void MapScreen::update_touch(double dt, bool &manual_input) {
    if (!touch_initialized_) return;
    SceTouchData data{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) < 0) return;
    TouchPoint points[2];
    int count = 0;
    for (unsigned int index = 0;
         index < data.reportNum && index < SCE_TOUCH_MAX_REPORT && count < 2;
         ++index) {
        if (data.report[index].info & SCE_TOUCH_REPORT_INFO_HIDE_UPPER_LAYER)
            continue;
        points[count++] = map_touch(data.report[index]);
    }

    if (count >= 2 && previous_touch_count_ >= 2) {
        const float direct = point_distance(
            previous_touch_a_.x, previous_touch_a_.y,
            points[0].x, points[0].y) + point_distance(
            previous_touch_b_.x, previous_touch_b_.y,
            points[1].x, points[1].y);
        const float swapped = point_distance(
            previous_touch_a_.x, previous_touch_a_.y,
            points[1].x, points[1].y) + point_distance(
            previous_touch_b_.x, previous_touch_b_.y,
            points[0].x, points[0].y);
        if (swapped < direct) std::swap(points[0], points[1]);
    }

    if (count == 1) {
        if (previous_touch_count_ == 0) {
            tap_start_ = points[0];
            tap_candidate_ = true;
            touch_duration_ = 0.0;
            camera_.velocityX = 0.0;
            camera_.velocityY = 0.0;
        }
        touch_duration_ += dt;
        if (touch_duration_ > kTapMaximumSeconds) tap_candidate_ = false;
        if (previous_touch_count_ == 1) {
            float dx = points[0].x - previous_touch_a_.x;
            float dy = points[0].y - previous_touch_a_.y;
            const float travel = point_distance(
                tap_start_.x, tap_start_.y, points[0].x, points[0].y);
            if (tap_candidate_ && travel > kTapMovementPixels) {
                tap_candidate_ = false;
                // Apply the movement held back while classifying the gesture.
                dx = points[0].x - tap_start_.x;
                dy = points[0].y - tap_start_.y;
            }
            if (!tap_candidate_) {
                camera_.pan_by_screen_pixels(-dx, -dy);
                if (dt > 0.0001) {
                    const float velocity_dx =
                        points[0].x - previous_touch_a_.x;
                    const float velocity_dy =
                        points[0].y - previous_touch_a_.y;
                    camera_.velocityX = -velocity_dx / dt;
                    camera_.velocityY = -velocity_dy / dt;
                }
            }
        } else if (previous_touch_count_ > 1) {
            tap_candidate_ = false;
            camera_.velocityX = 0.0;
            camera_.velocityY = 0.0;
        } else {
            camera_.velocityX = 0.0;
            camera_.velocityY = 0.0;
        }
        previous_touch_a_ = points[0];
        previous_pinch_distance_ = 0.0F;
        previous_pinch_angle_ = 0.0F;
        manual_input = true;
    } else if (count >= 2) {
        tap_candidate_ = false;
        touch_duration_ += dt;
        const TouchPoint midpoint{(points[0].x + points[1].x) * 0.5F,
                                  (points[0].y + points[1].y) * 0.5F};
        const float distance = point_distance(points[0].x, points[0].y,
                                              points[1].x, points[1].y);
        const float angle = std::atan2(points[1].y - points[0].y,
                                       points[1].x - points[0].x);
        if (previous_touch_count_ >= 2 && previous_pinch_distance_ > 4.0F &&
            distance > 4.0F) {
            camera_.pan_by_screen_pixels(
                -(midpoint.x - previous_touch_midpoint_.x),
                -(midpoint.y - previous_touch_midpoint_.y));
            camera_.zoom_immediate(std::log2(distance /
                                              previous_pinch_distance_));
            float angle_delta = angle - previous_pinch_angle_;
            while (angle_delta > static_cast<float>(kPi))
                angle_delta -= static_cast<float>(2.0 * kPi);
            while (angle_delta < static_cast<float>(-kPi))
                angle_delta += static_cast<float>(2.0 * kPi);
            // Ignore impossible per-frame jumps caused by touch-ID churn.
            if (std::abs(angle_delta) < 0.65F)
                camera_.rotate_immediate(angle_delta * 180.0 / kPi);
        }
        previous_touch_a_ = points[0];
        previous_touch_b_ = points[1];
        previous_touch_midpoint_ = midpoint;
        previous_pinch_distance_ = distance;
        previous_pinch_angle_ = angle;
        camera_.velocityX = 0.0;
        camera_.velocityY = 0.0;
        manual_input = true;
    } else if (previous_touch_count_ == 1) {
        if (tap_candidate_ && touch_duration_ <= kTapMaximumSeconds) {
            const bool north_button = hud_visible_ &&
                previous_touch_a_.x >= 780.0F &&
                previous_touch_a_.y >= 450.0F &&
                previous_touch_a_.y <= 510.0F;
            if (north_button) {
                camera_.set_bearing_target(0.0);
                hud_timer_ = kHudVisibleSeconds;
            } else {
                toggle_hud();
            }
        }
        tap_candidate_ = false;
        touch_duration_ = 0.0;
    }
    previous_touch_count_ = count;
}

void MapScreen::reset_touch_state() {
    previous_touch_count_ = 0;
    previous_pinch_distance_ = 0.0F;
    previous_pinch_angle_ = 0.0F;
    tap_candidate_ = false;
    touch_duration_ = 0.0;
}

void MapScreen::toggle_hud() {
    if (!preferences_hud_auto_hide()) {
        hud_visible_ = true;
        hud_timer_ = kHudVisibleSeconds;
        return;
    }
    hud_visible_ = !hud_visible_;
    hud_timer_ = hud_visible_ ? kHudVisibleSeconds : 0.0;
    log_printf("hud: visible=%d", hud_visible_ ? 1 : 0);
}

void MapScreen::request_mode(ScreenMode requested) {
    if (requested == mode_ || transition_phase_ != TransitionPhase::None)
        return;
    reset_touch_state();
    if (mode_ == ScreenMode::Settings && requested != ScreenMode::Settings) {
        settings_message_timer_ = 0.0;
        settings_message_motion_.set_target(0.0F);
    }
    if (preferences_reduce_motion()) {
        mode_ = requested;
        pending_mode_ = requested;
        screen_motion_.snap(1.0F);
        if (mode_ == ScreenMode::Settings) {
            settings_focus_y_.snap(
                kSettingsRowY[static_cast<int>(selected_setting_)]);
        } else if (mode_ == ScreenMode::PinLists) {
            const std::size_t first = visible_first(selected_list_, 6);
            list_focus_y_.snap(82.0F +
                static_cast<float>(selected_list_ - first) * 67.0F);
        } else if (mode_ == ScreenMode::PinList) {
            const std::size_t first = visible_first(selected_pin_, 6);
            pin_focus_y_.snap(137.0F +
                static_cast<float>(selected_pin_ - first) * 57.0F);
        }
        return;
    }
    pending_mode_ = requested;
    transition_phase_ = TransitionPhase::Closing;
    screen_motion_.set_target(0.0F);
}

bool MapScreen::transition_blocks_input() const {
    return transition_phase_ != TransitionPhase::None;
}

void MapScreen::update_animations(double dt, unsigned int pressed) {
    const bool reduced = preferences_reduce_motion();
    animation_seconds_ = reduced ? 0.0 : animation_seconds_ + dt;

    hud_motion_.set_target(hud_visible_ ? 1.0F : 0.0F);
    hud_motion_.tick(dt, reduced);
    const bool crosshair_visible = preferences_crosshair_enabled() || pinning_;
    crosshair_motion_.set_target(crosshair_visible ? 1.0F : 0.0F);
    crosshair_motion_.tick(dt, reduced);

    message_motion_.set_target(
        message_timer_ > 0.0 && !message_.empty() ? 1.0F : 0.0F);
    message_motion_.tick(dt, reduced);
    settings_message_motion_.set_target(settings_message_timer_ > 0.0
                                            ? 1.0F : 0.0F);
    settings_message_motion_.tick(dt, reduced);

    const unsigned int action_mask =
        SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE |
        SCE_CTRL_TRIANGLE | SCE_CTRL_SELECT | SCE_CTRL_START |
        SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
    if (pressed & action_mask) {
        static constexpr unsigned int order[] = {
            SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE,
            SCE_CTRL_TRIANGLE, SCE_CTRL_SELECT, SCE_CTRL_START,
            SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
        for (unsigned int button : order) {
            if (pressed & button) {
                last_action_button_ = button;
                break;
            }
        }
        button_feedback_.snap(1.0F);
        button_feedback_.set_target(0.0F);
    }
    button_feedback_.tick(dt, reduced);

    if (mode_ == ScreenMode::Settings) {
        const int row = static_cast<int>(selected_setting_);
        settings_focus_y_.set_target(kSettingsRowY[row]);
        settings_focus_y_.tick(dt, reduced);
    } else if (mode_ == ScreenMode::PinLists) {
        constexpr std::size_t rows = 6;
        const std::size_t first = visible_first(selected_list_, rows);
        const float row = static_cast<float>(selected_list_ - first);
        list_focus_y_.set_target(82.0F + row * 67.0F);
        list_focus_y_.tick(dt, reduced);
    } else if (mode_ == ScreenMode::PinList) {
        constexpr std::size_t rows = 6;
        const std::size_t first = visible_first(selected_pin_, rows);
        const float row = static_cast<float>(selected_pin_ - first);
        pin_focus_y_.set_target(137.0F + row * 57.0F);
        pin_focus_y_.tick(dt, reduced);
    }

    screen_motion_.set_target(
        transition_phase_ == TransitionPhase::Closing ? 0.0F : 1.0F);
    screen_motion_.tick(dt, reduced);
    if (transition_phase_ == TransitionPhase::Closing &&
        screen_motion_.value() <= 0.015F) {
        mode_ = pending_mode_;
        transition_phase_ = TransitionPhase::Opening;
        screen_motion_.snap(0.0F);
        screen_motion_.set_target(1.0F);
        if (mode_ == ScreenMode::Settings) {
            settings_focus_y_.snap(
                kSettingsRowY[static_cast<int>(selected_setting_)]);
        } else if (mode_ == ScreenMode::PinLists) {
            const std::size_t first = visible_first(selected_list_, 6);
            list_focus_y_.snap(82.0F +
                static_cast<float>(selected_list_ - first) * 67.0F);
        } else if (mode_ == ScreenMode::PinList) {
            const std::size_t first = visible_first(selected_pin_, 6);
            pin_focus_y_.snap(137.0F +
                static_cast<float>(selected_pin_ - first) * 57.0F);
        }
    } else if (transition_phase_ == TransitionPhase::Opening &&
               screen_motion_.value() >= 0.985F) {
        screen_motion_.snap(1.0F);
        transition_phase_ = TransitionPhase::None;
    }
}

void MapScreen::draw_transition_veil() {
    const float darkness = 1.0F - screen_motion_.value();
    if (darkness <= 0.001F) return;
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          RGBA8(6, 12, 18,
                              static_cast<int>(235.0F * darkness)));
}

void MapScreen::show_message(const std::string &message, bool error,
                             double seconds) {
    message_ = message;
    message_error_ = error;
    message_timer_ = seconds;
    log_printf("ui message: %s", message.c_str());
}

bool MapScreen::edit_text(const char *title, const char *initial, char *output,
                          std::size_t capacity) {
    reset_touch_state();
    const int result = ui_text_input(font_small_, font_body_, font_display_,
                                     title, initial, output, capacity);
    if (sceCtrlPeekBufferPositive(0, &previous_controls_, 1) < 0)
        std::memset(&previous_controls_, 0, sizeof(previous_controls_));
    if (result < 0) {
        char error[64];
        std::snprintf(error, sizeof(error), ui_text(UiText::KeyboardError),
                      static_cast<unsigned>(result));
        show_message(error, true);
    }
    return result > 0;
}

void MapScreen::begin_pinning() {
    if (!pins_.active().visible) {
        pins_.set_list_visible(pins_.active_index(), true);
        persist_pins();
    }
    pinning_ = true;
    hud_visible_ = true;
    hud_timer_ = kHudVisibleSeconds;
    camera_.velocityX = 0.0;
    camera_.velocityY = 0.0;
    show_message(ui_text(UiText::PinModeInstruction), false, 2.5);
}

void MapScreen::capture_pin() {
    char default_name[96];
    std::snprintf(default_name, sizeof(default_name),
                  ui_text(UiText::DefaultPoint),
                  static_cast<unsigned>(pins_.active().pins.size() + 1U));
    if (!pins_.add_pin({camera_.latitude, camera_.longitude}, default_name)) {
        show_message(ui_text(UiText::ListFull), true);
        return;
    }
    if (persist_pins()) {
        char message[128];
        std::snprintf(message, sizeof(message), ui_text(UiText::PointSaved),
                      static_cast<unsigned>(pins_.active().pins.size()),
                      pins_.active().name.c_str());
        show_message(message);
    }
}

bool MapScreen::persist_pins(const char *success_message) {
    const int result = pins_.save();
    if (result < 0) {
        char error[80];
        std::snprintf(error, sizeof(error), ui_text(UiText::RamOnly),
                      static_cast<unsigned>(result));
        show_message(error, true, 4.0);
        return false;
    }
    if (success_message) show_message(success_message);
    return true;
}

void MapScreen::begin_search() {
    if (search_pending_ || manager_.geocode_pending()) {
        show_message(ui_text(UiText::SearchBusy), false, 2.0);
        return;
    }
    char query[192];
    if (!edit_text(ui_text(UiText::SearchPrompt),
                   "", query, sizeof(query)))
        return;
    mercator::GeoPoint point;
    if (parse_coordinates(query, point)) {
        camera_.set_center(point.latitude, point.longitude);
        begin_pinning();
        show_message(ui_text(UiText::CoordinatesFound));
        return;
    }
    std::string local_name;
    if (find_local_place(query, point, local_name)) {
        camera_.set_center(point.latitude, point.longitude);
        if (camera_.target_zoom < 11.0) camera_.set_zoom_target(11.0);
        begin_pinning();
        char message[320];
        std::snprintf(message, sizeof(message), ui_text(UiText::LocalFound),
                      local_name.c_str());
        show_message(message, false, 4.0);
        return;
    }
    if (!manager_.request_geocode(query, ui_language_http_tag())) {
        show_message(ui_text(UiText::SearchStartFailed), true, 4.0);
        return;
    }
    search_pending_ = true;
    show_message(ui_text(UiText::SearchRunning), false, 30.0);
}

void MapScreen::poll_search_result() {
    if (!search_pending_) return;
    GeocodeResult result;
    if (!manager_.take_geocode_result(result)) return;
    search_pending_ = false;
    if (result.status != GeocodeStatus::Found) {
        if (result.status == GeocodeStatus::NotFound)
            show_message(ui_text(UiText::SearchNotFound), true, 4.0);
        else if (result.error == VITA_HTTPS_ERROR_NOT_INITIALIZED)
            show_message(ui_text(UiText::SearchOffline), true,
                         4.0);
        else {
            char error[96];
            std::snprintf(error, sizeof(error), ui_text(UiText::SearchFailed),
                          static_cast<unsigned>(result.error),
                          result.http_status);
            show_message(error, true, 5.0);
        }
        return;
    }
    camera_.set_center(result.point.latitude, result.point.longitude);
    if (camera_.target_zoom < 13.0) camera_.set_zoom_target(13.0);
    begin_pinning();
    char message[384];
    std::snprintf(message, sizeof(message),
                  ui_text(result.from_cache ? UiText::SearchCached
                                            : UiText::SearchFound),
                  result.display_name.c_str());
    show_message(message, false, 5.0);
}

void MapScreen::poll_cache_result() {
    TileCacheOperationResult result;
    if (!manager_.take_cache_result(result)) return;
    cache_status_ = result;
    cache_status_valid_ = true;
    cache_clear_pending_ = false;
    cache_refresh_timer_ = 5.0;
    if (!result.cleared) return;
    if (result.error == 0) renderer_.clear_cache();
    settings_result_ = result.error;
    settings_message_timer_ = 3.0;
    if (result.error == 0) {
        settings_feedback_ = ui_text(UiText::CacheCleared);
    } else {
        char error[96];
        std::snprintf(error, sizeof(error), ui_text(UiText::CacheClearFailed),
                      static_cast<unsigned>(result.error));
        settings_feedback_ = error;
    }
}

void MapScreen::update_settings(double dt, unsigned int pressed,
                                bool force_quit) {
    reset_touch_state();
    settings_message_timer_ = std::max(0.0, settings_message_timer_ - dt);
    cache_clear_confirm_timer_ = std::max(0.0,
                                         cache_clear_confirm_timer_ - dt);
    cache_refresh_timer_ = std::max(0.0, cache_refresh_timer_ - dt);
    if (cache_clear_confirm_timer_ <= 0.0) cache_clear_confirm_ = false;
    if (cache_refresh_timer_ <= 0.0 &&
        !manager_.cache_operation_pending()) {
        manager_.request_cache_status();
        cache_refresh_timer_ = 5.0;
    }
    if (!force_quit && (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START))) {
        cache_clear_confirm_ = false;
        request_mode(ScreenMode::Map);
        log_printf("settings closed");
    } else if (pressed & SCE_CTRL_UP) {
        cache_clear_confirm_ = false;
        int row = static_cast<int>(selected_setting_) - 1;
        if (row < 0) row = static_cast<int>(SettingRow::Count) - 1;
        selected_setting_ = static_cast<SettingRow>(row);
    } else if (pressed & SCE_CTRL_DOWN) {
        cache_clear_confirm_ = false;
        int row = static_cast<int>(selected_setting_) + 1;
        if (row >= static_cast<int>(SettingRow::Count)) row = 0;
        selected_setting_ = static_cast<SettingRow>(row);
    } else if (pressed & SCE_CTRL_LEFT) {
        if (selected_setting_ == SettingRow::Cache) {
            manager_.request_cache_status();
            cache_refresh_timer_ = 5.0;
        } else {
            change_setting(-1);
        }
    } else if (pressed & SCE_CTRL_RIGHT) {
        if (selected_setting_ == SettingRow::Cache) {
            manager_.request_cache_status();
            cache_refresh_timer_ = 5.0;
        } else {
            change_setting(1);
        }
    } else if (pressed & SCE_CTRL_CROSS) {
        if (selected_setting_ != SettingRow::Cache) {
            change_setting(1);
        } else if (manager_.cache_operation_pending()) {
            if (cache_clear_pending_) {
                settings_feedback_ = ui_text(UiText::CacheClearing);
                settings_message_timer_ = 2.0;
            } else {
                cache_clear_confirm_ = true;
                cache_clear_confirm_timer_ = 3.0;
            }
        } else if (!cache_clear_confirm_) {
            cache_clear_confirm_ = true;
            cache_clear_confirm_timer_ = 3.0;
        } else if (manager_.request_cache_clear()) {
            cache_clear_confirm_ = false;
            cache_clear_pending_ = true;
            cache_status_valid_ = false;
            settings_feedback_ = ui_text(UiText::CacheClearing);
            settings_message_timer_ = 30.0;
        }
    }
}

void MapScreen::update_pin_lists(unsigned int pressed) {
    reset_touch_state();
    const auto count = pins_.lists().size();
    if (count == 0) return;
    if (pressed & SCE_CTRL_CIRCLE) {
        delete_confirm_armed_ = false;
        request_mode(ScreenMode::Map);
    } else if (pressed & SCE_CTRL_UP) {
        delete_confirm_armed_ = false;
        selected_list_ = selected_list_ == 0 ? count - 1 : selected_list_ - 1;
    } else if (pressed & SCE_CTRL_DOWN) {
        delete_confirm_armed_ = false;
        selected_list_ = (selected_list_ + 1) % count;
    } else if (pressed & SCE_CTRL_CROSS) {
        delete_confirm_armed_ = false;
        pins_.set_active(selected_list_);
        persist_pins();
        selected_pin_ = 0;
        request_mode(ScreenMode::PinList);
    } else if (pressed & SCE_CTRL_TRIANGLE) {
        char name[97];
        if (edit_text(ui_text(UiText::NewList), ui_text(UiText::NewList),
                      name, sizeof(name))) {
            if (pins_.add_list(name)) {
                persist_pins(ui_text(UiText::ListCreated));
                selected_list_ = pins_.active_index();
            } else {
                show_message(ui_text(UiText::ListCreateFailed), true);
            }
        }
    } else if (pressed & SCE_CTRL_SQUARE) {
        char name[97];
        if (edit_text(ui_text(UiText::RenameList),
                      pins_.lists()[selected_list_].name.c_str(),
                      name, sizeof(name)) &&
            pins_.rename_list(selected_list_, name)) {
            persist_pins(ui_text(UiText::ListRenamed));
        }
    } else if (pressed & SCE_CTRL_RTRIGGER) {
        const bool visible = !pins_.lists()[selected_list_].visible;
        if (pins_.set_list_visible(selected_list_, visible))
            persist_pins(ui_text(visible ? UiText::Visible
                                         : UiText::Hidden));
    } else if (pressed & SCE_CTRL_SELECT) {
        if (pins_.lists().size() <= 1) {
            delete_confirm_armed_ = false;
            show_message(ui_text(UiText::OnlyListCannotDelete), true);
        } else if (!delete_confirm_armed_) {
            delete_confirm_armed_ = true;
            delete_confirm_timer_ = 3.0;
            show_message(ui_text(UiText::ConfirmDeleteList), true);
        } else if (pins_.remove_list(selected_list_)) {
            delete_confirm_armed_ = false;
            persist_pins(ui_text(UiText::ListDeleted));
            selected_list_ = std::min(selected_list_, pins_.lists().size() - 1);
        }
    }
}

void MapScreen::update_pin_list(unsigned int pressed) {
    reset_touch_state();
    if (selected_list_ >= pins_.lists().size()) {
        request_mode(ScreenMode::PinLists);
        return;
    }
    auto &list = pins_.lists()[selected_list_];
    if (pressed & SCE_CTRL_CIRCLE) {
        delete_confirm_armed_ = false;
        request_mode(ScreenMode::PinLists);
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_UP)) {
        delete_confirm_armed_ = false;
        selected_pin_ = selected_pin_ == 0
            ? list.pins.size() - 1 : selected_pin_ - 1;
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_DOWN)) {
        delete_confirm_armed_ = false;
        selected_pin_ = (selected_pin_ + 1) % list.pins.size();
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_CROSS)) {
        const auto position = list.pins[selected_pin_].position;
        pins_.set_active(selected_list_);
        camera_.set_center(position.latitude, position.longitude);
        request_mode(ScreenMode::Map);
        begin_pinning();
        show_message(ui_text(UiText::PointCentered));
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_TRIANGLE)) {
        char name[97];
        if (edit_text(ui_text(UiText::RenamePoint),
                      list.pins[selected_pin_].name.c_str(),
                      name, sizeof(name)) &&
            pins_.rename_pin(selected_list_, selected_pin_, name)) {
            persist_pins(ui_text(UiText::PointRenamed));
        }
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_SQUARE)) {
        if (!delete_confirm_armed_) {
            delete_confirm_armed_ = true;
            delete_confirm_timer_ = 3.0;
            show_message(ui_text(UiText::ConfirmDeletePoint), true);
        } else {
            delete_confirm_armed_ = false;
            pins_.remove_pin(selected_list_, selected_pin_);
            persist_pins(ui_text(UiText::PointDeleted));
            if (!list.pins.empty())
                selected_pin_ = std::min(selected_pin_, list.pins.size() - 1);
            else
                selected_pin_ = 0;
        }
    } else if (pressed & SCE_CTRL_SELECT) {
        if (list.pins.size() < 3) {
            show_message(ui_text(UiText::NeedThreePoints), true);
        } else if (pins_.set_list_closed(selected_list_, !list.closed)) {
            persist_pins(ui_text(list.closed ? UiText::Closed
                                             : UiText::OpenPath));
        }
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_LTRIGGER)) {
        if (pins_.move_pin(selected_list_, selected_pin_, -1)) {
            --selected_pin_;
            persist_pins();
        }
    } else if (!list.pins.empty() && (pressed & SCE_CTRL_RTRIGGER)) {
        if (pins_.move_pin(selected_list_, selected_pin_, 1)) {
            ++selected_pin_;
            persist_pins();
        }
    }
}

void MapScreen::update(double dt, bool &quit) {
    SceCtrlData controls{};
    if (sceCtrlPeekBufferPositive(0, &controls, 1) < 0)
        controls = previous_controls_;
    const unsigned int pressed = controls.buttons & ~previous_controls_.buttons;
    bool manual_input = false;
    message_timer_ = std::max(0.0, message_timer_ - dt);
    delete_confirm_timer_ = std::max(0.0, delete_confirm_timer_ - dt);
    if (delete_confirm_timer_ <= 0.0) delete_confirm_armed_ = false;
    update_animations(dt, pressed);
    poll_search_result();
    poll_cache_result();

    const bool force_quit = (controls.buttons & SCE_CTRL_START) &&
                            (controls.buttons & SCE_CTRL_SELECT);
    if (force_quit) {
        quit = true;
        previous_controls_ = controls;
        return;
    }

    if (transition_blocks_input()) {
        previous_controls_ = controls;
        return;
    }

    if (mode_ == ScreenMode::Settings) {
        update_settings(dt, pressed, force_quit);
        previous_controls_ = controls;
        return;
    }
    if (mode_ == ScreenMode::PinLists) {
        update_pin_lists(pressed);
        previous_controls_ = controls;
        return;
    }
    if (mode_ == ScreenMode::PinList) {
        update_pin_list(pressed);
        previous_controls_ = controls;
        return;
    }
    if (!force_quit && (pressed & SCE_CTRL_START)) {
        request_mode(ScreenMode::Settings);
        cache_refresh_timer_ = 5.0;
        manager_.request_cache_status();
        camera_.velocityX = 0.0;
        camera_.velocityY = 0.0;
        reset_touch_state();
        log_printf("settings opened");
        previous_controls_ = controls;
        return;
    }
    if (pressed & SCE_CTRL_SQUARE) {
        request_mode(ScreenMode::PinLists);
        selected_list_ = pins_.active_index();
        delete_confirm_armed_ = false;
        pinning_ = false;
        reset_touch_state();
        previous_controls_ = controls;
        return;
    }
    if (pressed & SCE_CTRL_TRIANGLE) {
        begin_search();
        return;
    }
    if (pressed & SCE_CTRL_CROSS) {
        if (pinning_) capture_pin();
        else begin_pinning();
    }
    if ((pressed & SCE_CTRL_CIRCLE) && pinning_) {
        pinning_ = false;
        show_message(ui_text(UiText::PinModeEnded));
    }
    if (pressed & SCE_CTRL_LTRIGGER)
        camera_.set_zoom_target(camera_.target_zoom - 1.0);
    if (pressed & SCE_CTRL_RTRIGGER)
        camera_.set_zoom_target(camera_.target_zoom + 1.0);
    if (pressed & SCE_CTRL_SELECT) {
        camera_.set_bearing_target(0.0);
        manual_input = true;
    }

    double axis_x = analog_axis(controls.lx);
    double axis_y = analog_axis(controls.ly);
    if (controls.buttons & SCE_CTRL_LEFT) axis_x -= 0.65;
    if (controls.buttons & SCE_CTRL_RIGHT) axis_x += 0.65;
    if (controls.buttons & SCE_CTRL_UP) axis_y -= 0.65;
    if (controls.buttons & SCE_CTRL_DOWN) axis_y += 0.65;
    if (axis_x != 0.0 || axis_y != 0.0) {
        constexpr double speed = 520.0;
        camera_.pan_by_screen_pixels(axis_x * speed * dt,
                                     axis_y * speed * dt);
        camera_.velocityX = 0.0;
        camera_.velocityY = 0.0;
        manual_input = true;
    }

    const double rotation_axis = analog_axis(controls.rx);
    if (rotation_axis != 0.0) {
        constexpr double rotation_speed = 120.0;
        camera_.rotate_immediate(rotation_axis * rotation_speed * dt);
        manual_input = true;
    }

    update_touch(dt, manual_input);
    camera_.update(dt, !manual_input && previous_touch_count_ == 0);
    if (preferences_hud_auto_hide()) {
        if (hud_visible_ && hud_timer_ > 0.0) {
            hud_timer_ = std::max(0.0, hud_timer_ - dt);
            if (hud_timer_ <= 0.0) hud_visible_ = false;
        }
    } else {
        hud_visible_ = true;
    }
    stats_timer_ += dt;
    connection_timer_ += dt;
    if (stats_timer_ >= 0.5) {
        cached_stats_ = manager_.stats();
        stats_timer_ = 0.0;
    }
    if (connection_timer_ >= 2.0) {
        connected_ = https_initialized_ && vita_https_is_connected() != 0;
        connection_timer_ = 0.0;
    }
    previous_controls_ = controls;
}

void MapScreen::change_setting(int direction) {
    settings_result_ = 0;
    settings_feedback_.clear();
    switch (selected_setting_) {
    case SettingRow::MapStyle: {
        const int count = provider_.style_count();
        const int previous = provider_.style_index();
        int next = previous + (direction < 0 ? -1 : 1);
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        if (!provider_.set_style(next)) {
            settings_result_ = -1;
            break;
        }
        settings_result_ = preferences_set_map_style(next);
        if (settings_result_ < 0) provider_.set_style(previous);
        camera_.min_zoom = provider_.min_zoom();
        camera_.max_zoom = provider_.max_zoom();
        camera_.zoom = std::clamp(camera_.zoom, camera_.min_zoom,
                                  camera_.max_zoom);
        camera_.target_zoom = std::clamp(camera_.target_zoom,
                                         camera_.min_zoom,
                                         camera_.max_zoom);
        log_printf("settings: map style index=%d id=%u name=%s",
                   provider_.style_index(), static_cast<unsigned>(provider_.id()),
                   provider_.name());
        break;
    }
    case SettingRow::Language: {
        const int previous = preferences_ui_language();
        int next = previous + (direction < 0 ? -1 : 1);
        if (next < 0) next = kUiLanguageSettingCount - 1;
        if (next >= kUiLanguageSettingCount) next = 0;
        settings_result_ = preferences_set_ui_language(next);
        if (settings_result_ == 0) ui_localization_init(next);
        log_printf("settings: ui language=%d", next);
        break;
    }
    case SettingRow::Cache:
        return;
    case SettingRow::HudBehavior: {
        const bool next = !preferences_hud_auto_hide();
        settings_result_ = preferences_set_hud_auto_hide(next);
        hud_visible_ = true;
        hud_timer_ = kHudVisibleSeconds;
        log_printf("settings: hud auto hide=%d", next ? 1 : 0);
        break;
    }
    case SettingRow::Crosshair: {
        const bool next = !preferences_crosshair_enabled();
        settings_result_ = preferences_set_crosshair_enabled(next);
        log_printf("settings: crosshair=%d", next ? 1 : 0);
        break;
    }
    case SettingRow::Units: {
        const bool next = !preferences_imperial_units();
        settings_result_ = preferences_set_imperial_units(next);
        log_printf("settings: imperial units=%d", next ? 1 : 0);
        break;
    }
    case SettingRow::Animations: {
        const bool next = !preferences_reduce_motion();
        settings_result_ = preferences_set_reduce_motion(next);
        log_printf("settings: reduce motion=%d", next ? 1 : 0);
        break;
    }
    case SettingRow::DiskLogging:
        toggle_disk_logging();
        return;
    case SettingRow::Count:
        return;
    }
    settings_message_timer_ = 2.0;
    if (settings_result_ < 0)
        log_printf("settings: persist failed 0x%08X",
                   static_cast<unsigned>(settings_result_));
    log_save();
}

void MapScreen::prepare(std::uint64_t frame) {
    if (mode_ == ScreenMode::Map)
        renderer_.prepare(camera_, viewport_, frame);
}

void MapScreen::toggle_disk_logging() {
    settings_feedback_.clear();
    const bool next = !preferences_disk_logs_enabled();
    settings_result_ = preferences_set_disk_logs_enabled(next);
    settings_message_timer_ = 3.0;
    if (settings_result_ < 0) {
        log_printf("settings: persist disk logging=%d failed 0x%08X",
                   next ? 1 : 0, static_cast<unsigned>(settings_result_));
        log_save();
        return;
    }
    if (next) {
        log_set_disk_enabled(true);
        log_printf("settings: disk logging=1");
        settings_result_ = log_save();
    } else {
        // Persist the final transition while the previous policy is still on,
        // then stop all subsequent ux0 log snapshots.
        log_printf("settings: disk logging=0");
        settings_result_ = log_save();
        log_set_disk_enabled(false);
    }
}

void MapScreen::draw_chrome() {
    const float opacity = hud_motion_.value();
    if (opacity <= 0.002F) return;
    const float eased = ui_ease_out_cubic(opacity);
    const float top_offset = -12.0F * (1.0F - eased);
    const float bottom_offset = 12.0F * (1.0F - eased);
    vita2d_draw_rectangle(0.0F, top_offset, 960.0F, 42.0F,
                          ui_fade_color(kTopBar, opacity));
    vita2d_draw_rectangle(0.0F, 512.0F + bottom_offset, 960.0F, 32.0F,
                          ui_fade_color(kBottomBar, opacity));
    vita2d_draw_rectangle(0.0F, 41.0F + top_offset, 960.0F, 1.0F,
                          ui_fade_color(kAccent, opacity));
    ui_font_draw_text(font_body_, 16, static_cast<int>(29.0F + top_offset),
                      ui_fade_color(kPrimaryText, opacity), kFontBody,
                      "VitaMaps");
    ui_font_draw_textf(font_small_, 130,
                       static_cast<int>(28.0F + top_offset),
                       ui_fade_color(kSecondaryText, opacity), kFontSmall,
                       ui_text(UiText::CenterStatus),
                       camera_.latitude, camera_.longitude, camera_.zoom,
                       renderer_.tile_zoom(), camera_.bearing);
    ui_font_draw_textf(font_small_, 742,
                       static_cast<int>(28.0F + top_offset),
                       ui_fade_color(connected_ ? kAccent : kSecondaryText,
                                     opacity), kFontSmall,
                       "%s Q:%u GPU:%u",
                       ui_text(connected_ ? UiText::Online : UiText::Offline),
                       static_cast<unsigned int>(cached_stats_.queued),
                       static_cast<unsigned int>(renderer_.texture_count()));
    int hint_x = 12;
    const float feedback = button_feedback_.value();
    hint_x = draw_key_hint(font_small_, hint_x, "X",
                           ui_text(pinning_ ? UiText::Save : UiText::Pin),
                           SCE_CTRL_CROSS,
                           last_action_button_, feedback, opacity,
                           bottom_offset);
    if (pinning_)
        hint_x = draw_key_hint(font_small_, hint_x, "O",
                               ui_text(UiText::Finish),
                               SCE_CTRL_CIRCLE, last_action_button_, feedback,
                               opacity, bottom_offset);
    hint_x = draw_key_hint(font_small_, hint_x, "SQUARE", ui_text(UiText::Lists),
                           SCE_CTRL_SQUARE,
                           last_action_button_, feedback, opacity,
                           bottom_offset);
    hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE", ui_text(UiText::Search),
                           SCE_CTRL_TRIANGLE, last_action_button_, feedback,
                           opacity, bottom_offset);
    hint_x = draw_key_hint(font_small_, hint_x, "START", ui_text(UiText::Options),
                           SCE_CTRL_START, last_action_button_, feedback,
                           opacity, bottom_offset);
    draw_key_hint(font_small_, hint_x, "L/R", ui_text(UiText::Zoom),
                  SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER,
                  last_action_button_, feedback, opacity, bottom_offset);
    draw_key_hint(font_small_, 620, "R-STICK", ui_text(UiText::Rotate),
                  0U, last_action_button_, feedback, opacity,
                  bottom_offset - 52.0F);
    draw_key_hint(font_small_, 804, "SELECT", ui_text(UiText::North),
                  SCE_CTRL_SELECT, last_action_button_, feedback, opacity,
                  bottom_offset - 52.0F);
    draw_scale();
}

void MapScreen::draw_attribution() {
    const char *text = provider_.attribution();
    const int width = ui_font_text_width(font_small_, kFontSmall, text);
    const float x = static_cast<float>(std::max(8, 950 - width));
    const float hidden = 1.0F - hud_motion_.value();
    if (hidden > 0.002F)
        vita2d_draw_rectangle(x - 6.0F, 518.0F,
                              static_cast<float>(width + 12), 26.0F,
                              ui_fade_color(kHudPanel, hidden));
    ui_font_draw_text(font_small_, static_cast<int>(x), 538,
                      kPrimaryText, kFontSmall, text);
}

void MapScreen::draw_crosshair() {
    const float visibility = crosshair_motion_.value();
    if (visibility <= 0.002F) return;
    const float eased = ui_ease_out_cubic(visibility);
    constexpr float x = 480.0F;
    constexpr float y = 272.0F;
    const float shadow_half = 19.0F * eased;
    const float line_half = 16.0F * eased;
    vita2d_draw_rectangle(x - shadow_half, y - 1.5F,
                          shadow_half * 2.0F, 3.0F,
                          ui_fade_color(kShadow, visibility));
    vita2d_draw_rectangle(x - 1.5F, y - shadow_half, 3.0F,
                          shadow_half * 2.0F,
                          ui_fade_color(kShadow, visibility));
    vita2d_draw_rectangle(x - line_half, y - 0.5F,
                          line_half * 2.0F, 1.0F,
                          ui_fade_color(kCrosshair, visibility));
    vita2d_draw_rectangle(x - 0.5F, y - line_half, 1.0F,
                          line_half * 2.0F,
                          ui_fade_color(kCrosshair, visibility));
    const float center = 2.0F + button_feedback_.value();
    vita2d_draw_rectangle(x - center, y - center, center * 2.0F,
                          center * 2.0F,
                          ui_fade_color(kAccent, visibility));
}

void MapScreen::draw_scale() {
    const float opacity = hud_motion_.value();
    if (opacity <= 0.002F) return;
    const float offset = 12.0F *
        (1.0F - ui_ease_out_cubic(opacity));
    const double latitude_radians = camera_.latitude * kPi / 180.0;
    const double meters_per_pixel =
        std::cos(latitude_radians) * (2.0 * kPi * kEarthRadiusMeters) /
        std::exp2(camera_.zoom + 8.0);
    if (!(meters_per_pixel > 0.0)) return;

    constexpr double target_pixels = 128.0;
    const bool imperial = preferences_imperial_units();
    double displayed = 0.0;
    double meters = 0.0;
    const char *unit = nullptr;
    if (imperial) {
        const double maximum_meters = meters_per_pixel * target_pixels;
        if (maximum_meters >= 1609.344) {
            displayed = nice_distance(maximum_meters / 1609.344);
            meters = displayed * 1609.344;
            unit = "mi";
        } else {
            displayed = nice_distance(maximum_meters * 3.280839895);
            meters = displayed / 3.280839895;
            unit = "ft";
        }
    } else {
        const double maximum_meters = meters_per_pixel * target_pixels;
        if (maximum_meters >= 1000.0) {
            displayed = nice_distance(maximum_meters / 1000.0);
            meters = displayed * 1000.0;
            unit = "km";
        } else {
            displayed = nice_distance(maximum_meters);
            meters = displayed;
            unit = "m";
        }
    }
    const float pixels = static_cast<float>(meters / meters_per_pixel);
    char label[32];
    if (displayed >= 10.0 || std::abs(displayed - std::round(displayed)) < 0.01)
        std::snprintf(label, sizeof(label), "%.0f %s", displayed, unit);
    else
        std::snprintf(label, sizeof(label), "%.1f %s", displayed, unit);

    vita2d_draw_rectangle(12.0F, 460.0F + offset, 166.0F, 46.0F,
                          ui_fade_color(kHudPanel, opacity));
    ui_font_draw_text(font_small_, 22, static_cast<int>(481.0F + offset),
                      ui_fade_color(kPrimaryText, opacity), kFontSmall, label);
    vita2d_draw_rectangle(22.0F, 494.0F + offset, pixels, 2.0F,
                          ui_fade_color(kPrimaryText, opacity));
    vita2d_draw_rectangle(22.0F, 489.0F + offset, 2.0F, 9.0F,
                          ui_fade_color(kPrimaryText, opacity));
    vita2d_draw_rectangle(20.0F + pixels, 489.0F + offset, 2.0F, 9.0F,
                          ui_fade_color(kPrimaryText, opacity));
}

void MapScreen::draw_pin_overlays() {
    const auto &lists = pins_.lists();
    const bool imperial = preferences_imperial_units();
    for (std::size_t list_index = 0; list_index < lists.size(); ++list_index) {
        const auto &list = lists[list_index];
        if (!list.visible) continue;
        const bool active = list_index == pins_.active_index();
        const unsigned int route_color = active ? kPin : kPinMuted;
        const auto draw_segment = [&](const MapPin &from, const MapPin &to) {
            const auto left = camera_.geo_to_screen(
                from.position.latitude, from.position.longitude,
                960.0, 544.0);
            const auto right = camera_.geo_to_screen(
                to.position.latitude, to.position.longitude,
                960.0, 544.0);
            if ((left.x < -80.0 && right.x < -80.0) ||
                (left.x > 1040.0 && right.x > 1040.0) ||
                (left.y < -80.0 && right.y < -80.0) ||
                (left.y > 624.0 && right.y > 624.0))
                return;
            const float left_x = static_cast<float>(left.x);
            const float left_y = static_cast<float>(left.y);
            const float right_x = static_cast<float>(right.x);
            const float right_y = static_cast<float>(right.y);
            vita2d_draw_line(left_x, left_y - 1.0F,
                             right_x, right_y - 1.0F, kShadow);
            vita2d_draw_line(left_x, left_y,
                             right_x, right_y, route_color);
            vita2d_draw_line(left_x, left_y + 1.0F,
                             right_x, right_y + 1.0F, route_color);

            const float midpoint_x = (left_x + right_x) * 0.5F;
            const float midpoint_y = (left_y + right_y) * 0.5F;
            if (midpoint_x < 18.0F || midpoint_x > 942.0F ||
                midpoint_y < 54.0F || midpoint_y > 494.0F)
                return;
            char distance[32];
            format_distance(pin_distance_meters(from, to), imperial,
                            distance, sizeof(distance));
            const int text_width = ui_font_text_width(font_small_, kFontSmall,
                                                       distance);
            const float panel_x = midpoint_x - text_width * 0.5F - 6.0F;
            const float panel_y = midpoint_y - 12.0F;
            vita2d_draw_rectangle(panel_x, panel_y,
                                  static_cast<float>(text_width + 12), 22.0F,
                                  kHudPanel);
            ui_font_draw_text(font_small_,
                              static_cast<int>(midpoint_x) - text_width / 2,
                              static_cast<int>(midpoint_y) + 6,
                              kPrimaryText, kFontSmall, distance);
        };
        for (std::size_t index = 1; index < list.pins.size(); ++index)
            draw_segment(list.pins[index - 1], list.pins[index]);
        if (list.closed && list.pins.size() >= 3)
            draw_segment(list.pins.back(), list.pins.front());

        for (std::size_t index = 0; index < list.pins.size(); ++index) {
            const auto screen = camera_.geo_to_screen(
                list.pins[index].position.latitude,
                list.pins[index].position.longitude, 960.0, 544.0);
            if (screen.x < -24.0 || screen.x > 984.0 ||
                screen.y < -24.0 || screen.y > 568.0)
                continue;
            const float x = static_cast<float>(screen.x);
            const float y = static_cast<float>(screen.y);
            vita2d_draw_fill_circle(x, y, 12.0F, kShadow);
            vita2d_draw_fill_circle(x, y - 1.0F, active ? 10.0F : 8.0F,
                                    active ? kPin : kPinMuted);
            if (active && index < 99) {
                char number[4];
                std::snprintf(number, sizeof(number), "%u",
                              static_cast<unsigned>(index + 1));
                const int width = ui_font_text_width(font_small_, kFontSmall,
                                                     number);
                ui_font_draw_text(font_small_, static_cast<int>(x) - width / 2,
                                  static_cast<int>(y) + 6, kPrimaryText,
                                  kFontSmall, number);
            }
        }
    }
    if (pinning_) {
        const PinList &list = pins_.active();
        char distance[32];
        format_distance(pin_path_distance_meters(list),
                        imperial, distance,
                        sizeof(distance));
        const float hud_position = ui_ease_out_cubic(hud_motion_.value());
        const float panel_y = 12.0F + 40.0F * hud_position;
        vita2d_draw_rectangle(12.0F, panel_y,
                              390.0F, 42.0F, kHudPanel);
        ui_font_draw_textf(font_small_, 24,
                           static_cast<int>(panel_y + 28.0F),
                           kPrimaryText, kFontSmall,
                           ui_text(UiText::PinSummary), list.name.c_str(),
                           static_cast<unsigned>(list.pins.size()), distance);
    }
}

void MapScreen::draw_pin_lists() {
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::ListsAndRoutes));
    const auto &lists = pins_.lists();
    constexpr std::size_t visible_rows = 6;
    const std::size_t first = visible_first(selected_list_, visible_rows);
    ui_draw_focus_glow(48.0F, list_focus_y_.value(), 864.0F, 56.0F,
                       kAccent, animation_seconds_, button_feedback_.value());
    for (std::size_t row = 0; row < visible_rows && first + row < lists.size();
         ++row) {
        const std::size_t index = first + row;
        const int y = 82 + static_cast<int>(row) * 67;
        vita2d_draw_rectangle(48.0F, static_cast<float>(y), 864.0F, 56.0F,
                              kPanel);
        char name[160];
        ui_font_fit_text(font_body_, kFontBody, lists[index].name.c_str(),
                         name, sizeof(name), 410);
        ui_font_draw_text(font_body_, 70, y + 35, kPrimaryText, kFontBody,
                          name);
        const unsigned int map_state_color = lists[index].visible
            ? kAccent : kSecondaryText;
        vita2d_draw_rectangle(500.0F, static_cast<float>(y + 17),
                              96.0F, 24.0F,
                              lists[index].visible ? kPanelSelected : kTopBar);
        char map_state[64];
        ui_font_fit_text(font_small_, kFontSmall,
                         ui_text(lists[index].visible ? UiText::Visible
                                                      : UiText::Hidden),
                         map_state, sizeof(map_state), 84);
        const int map_state_width = ui_font_text_width(
            font_small_, kFontSmall, map_state);
        ui_font_draw_text(font_small_, 548 - map_state_width / 2, y + 35,
                          map_state_color, kFontSmall, map_state);
        char distance[32];
        format_distance(pin_path_distance_meters(lists[index]),
                        preferences_imperial_units(), distance,
                        sizeof(distance));
        ui_font_draw_textf(font_small_, 612, y + 33,
                           index == pins_.active_index() ? kAccent
                                                         : kSecondaryText,
                           kFontSmall, ui_text(UiText::PinCount),
                           static_cast<unsigned>(lists[index].pins.size()),
                           distance,
                           index == pins_.active_index()
                               ? ui_text(UiText::ActiveSuffix) : "");
    }
    vita2d_draw_rectangle(48.0F, list_focus_y_.value(),
                          5.0F + button_feedback_.value() * 3.0F,
                          56.0F, kAccent);
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "X", ui_text(UiText::Open),
                           SCE_CTRL_CROSS,
                           last_action_button_, button_feedback_.value(), 1.0F,
                           -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "R", ui_text(UiText::ShowHide),
                           SCE_CTRL_RTRIGGER, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE", ui_text(UiText::New),
                           SCE_CTRL_TRIANGLE, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "SQUARE", ui_text(UiText::Rename),
                           SCE_CTRL_SQUARE, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SELECT", ui_text(UiText::Delete),
                           SCE_CTRL_SELECT, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "O", ui_text(UiText::Map),
                  SCE_CTRL_CIRCLE, last_action_button_,
                  button_feedback_.value(), 1.0F, 0.0F);
    draw_message();
}

void MapScreen::draw_pin_list() {
    if (selected_list_ >= pins_.lists().size()) return;
    const PinList &list = pins_.lists()[selected_list_];
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    char title[192];
    ui_font_fit_text(font_display_, kFontDisplay, list.name.c_str(), title,
                     sizeof(title), 650);
    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      title);
    char distance[32];
    char area[32]{};
    format_distance(pin_path_distance_meters(list),
                    preferences_imperial_units(), distance, sizeof(distance));
    if (list.closed)
        format_area(pin_polygon_area_square_meters(list),
                    preferences_imperial_units(), area, sizeof(area));
    ui_font_draw_textf(font_small_, 36, 96, kSecondaryText, kFontSmall,
                       ui_text(list.closed && list.pins.size() >= 3
                                   ? UiText::StopsDistanceArea
                                   : UiText::StopsDistance),
                       static_cast<unsigned>(list.pins.size()), distance,
                       area);
    ui_font_draw_text(font_small_, 36, 123, kSecondaryText, kFontSmall,
                      ui_text(UiText::RoutingUnavailable));

    constexpr std::size_t visible_rows = 6;
    const std::size_t first = visible_first(selected_pin_, visible_rows);
    if (!list.pins.empty())
        ui_draw_focus_glow(48.0F, pin_focus_y_.value(), 864.0F, 48.0F,
                           kPin, animation_seconds_, button_feedback_.value());
    for (std::size_t row = 0; row < visible_rows && first + row < list.pins.size();
         ++row) {
        const std::size_t index = first + row;
        const int y = 137 + static_cast<int>(row) * 57;
        vita2d_draw_rectangle(48.0F, static_cast<float>(y), 864.0F, 48.0F,
                              kPanel);
        char name[144];
        ui_font_fit_text(font_body_, kFontBody, list.pins[index].name.c_str(),
                         name, sizeof(name), 350);
        ui_font_draw_textf(font_body_, 70, y + 31, kPrimaryText, kFontBody,
                           "%u. %s", static_cast<unsigned>(index + 1), name);
        char segment[64];
        if (index == 0 && !list.closed) {
            std::snprintf(segment, sizeof(segment), "%s",
                          ui_text(UiText::StartPoint));
        } else {
            const std::size_t previous = index == 0
                ? list.pins.size() - 1 : index - 1;
            char segment_distance[32];
            format_distance(pin_distance_meters(list.pins[previous],
                                                 list.pins[index]),
                            preferences_imperial_units(), segment_distance,
                            sizeof(segment_distance));
            std::snprintf(segment, sizeof(segment), "%u-%u %s",
                          static_cast<unsigned>(previous + 1),
                          static_cast<unsigned>(index + 1), segment_distance);
        }
        char details[160];
        char fitted_details[160];
        std::snprintf(details, sizeof(details), "%s  |  %.5f, %.5f", segment,
                      list.pins[index].position.latitude,
                      list.pins[index].position.longitude);
        ui_font_fit_text(font_small_, kFontSmall, details, fitted_details,
                         sizeof(fitted_details), 410);
        ui_font_draw_text(font_small_, 482, y + 29, kSecondaryText,
                          kFontSmall, fitted_details);
    }
    if (!list.pins.empty())
        vita2d_draw_rectangle(48.0F, pin_focus_y_.value(),
                              5.0F + button_feedback_.value() * 3.0F,
                              48.0F, kPin);
    if (list.pins.empty())
        ui_font_draw_text_centered(font_body_, 480, 286, 760, kSecondaryText,
                                   kFontBody,
                                   ui_text(UiText::EmptyList));
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "X", ui_text(UiText::Center),
                           SCE_CTRL_CROSS,
                           last_action_button_, button_feedback_.value(), 1.0F,
                           -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SELECT",
                           ui_text(list.closed ? UiText::OpenPath
                                               : UiText::ClosePath),
                           SCE_CTRL_SELECT, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "L/R", ui_text(UiText::Reorder),
                           SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER,
                           last_action_button_, button_feedback_.value(), 1.0F,
                           -28.0F);
    hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE", ui_text(UiText::Rename),
                           SCE_CTRL_TRIANGLE, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SQUARE", ui_text(UiText::Delete),
                           SCE_CTRL_SQUARE, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "O", ui_text(UiText::Lists), SCE_CTRL_CIRCLE,
                  last_action_button_, button_feedback_.value(), 1.0F, 0.0F);
    draw_message();
}

void MapScreen::draw_message() {
    const float opacity = message_motion_.value();
    if (opacity <= 0.002F || message_.empty()) return;
    char fitted[320];
    const int width = ui_font_fit_text(font_small_, kFontSmall,
                                       message_.c_str(), fitted,
                                       sizeof(fitted), 780);
    const float panel_x = static_cast<float>((960 - width) / 2 - 16);
    const float y = 430.0F + 12.0F *
        (1.0F - ui_ease_out_cubic(opacity));
    vita2d_draw_rectangle(panel_x, y,
                          static_cast<float>(width + 32), 42.0F,
                          ui_fade_color(kHudPanel, opacity));
    ui_font_draw_text(font_small_, static_cast<int>(panel_x) + 16,
                      static_cast<int>(y + 28.0F),
                      ui_fade_color(message_error_ ? kError : kPrimaryText,
                                    opacity),
                      kFontSmall, fitted);
}

void MapScreen::draw_settings() {
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    for (int row = 0; row < static_cast<int>(SettingRow::Count); ++row) {
        vita2d_draw_rectangle(54.0F, kSettingsRowY[row],
                              852.0F, kSettingsRowHeight, kPanel);
    }
    ui_draw_focus_glow(54.0F, settings_focus_y_.value(), 852.0F,
                       kSettingsRowHeight,
                       kAccent, animation_seconds_, button_feedback_.value());
    vita2d_draw_rectangle(54.0F, settings_focus_y_.value(),
                          852.0F, kSettingsRowHeight, kPanelSelected);
    vita2d_draw_rectangle(54.0F, settings_focus_y_.value(),
                          5.0F + button_feedback_.value() * 3.0F,
                          kSettingsRowHeight, kAccent);
    vita2d_draw_rectangle(0.0F, 508.0F, 960.0F, 36.0F, kBottomBar);

    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::SettingsTitle));
    const char *labels[] = {
        ui_text(UiText::MapStyle), ui_text(UiText::Language),
        ui_text(UiText::CacheMaps), ui_text(UiText::HudBehavior),
        ui_text(UiText::Crosshair), ui_text(UiText::Units),
        ui_text(UiText::Animations),
        ui_text(UiText::PersistentLogs)
    };
    char cache_value[128];
    if (cache_clear_pending_) {
        std::snprintf(cache_value, sizeof(cache_value), "%s",
                      ui_text(UiText::CacheClearing));
    } else if (cache_clear_confirm_) {
        std::snprintf(cache_value, sizeof(cache_value), "%s",
                      ui_text(UiText::CacheConfirm));
    } else if (cache_status_valid_) {
        char size[32];
        format_cache_size(cache_status_.status.bytes, size, sizeof(size));
        std::snprintf(cache_value, sizeof(cache_value),
                      ui_text(UiText::CacheState), size,
                      static_cast<unsigned>(cache_status_.status.entries));
    } else {
        std::snprintf(cache_value, sizeof(cache_value), "%s",
                      ui_text(UiText::CacheReading));
    }
    const char *values[] = {
        provider_.name(),
        ui_language_name(preferences_ui_language()),
        cache_value,
        ui_text(preferences_hud_auto_hide() ? UiText::HudAuto
                                            : UiText::AlwaysVisible),
        ui_text(preferences_crosshair_enabled() ? UiText::Visible
                                                : UiText::Hidden),
        ui_text(preferences_imperial_units() ? UiText::Imperial
                                             : UiText::Metric),
        ui_text(preferences_reduce_motion() ? UiText::Reduced
                                            : UiText::Smooth),
        ui_text(preferences_disk_logs_enabled() ? UiText::Enabled
                                                : UiText::Disabled)
    };
    for (int row = 0; row < static_cast<int>(SettingRow::Count); ++row) {
        char fitted_label[192];
        char fitted_value[192];
        ui_font_fit_text(font_body_, kFontBody, labels[row], fitted_label,
                         sizeof(fitted_label), 500);
        ui_font_fit_text(font_small_, kFontSmall, values[row], fitted_value,
                         sizeof(fitted_value), 280);
        ui_font_draw_text(font_body_, 78,
                          static_cast<int>(kSettingsRowY[row]) + 31,
                          kPrimaryText,
                          kFontBody, fitted_label);
        const int width = ui_font_text_width(font_small_, kFontSmall,
                                             fitted_value);
        ui_font_draw_text(font_small_, 876 - width,
                          static_cast<int>(kSettingsRowY[row]) + 30,
                          row == static_cast<int>(selected_setting_)
                              ? kAccent : kSecondaryText,
                          kFontSmall, fitted_value);
    }

    if (settings_message_motion_.value() > 0.002F) {
        const bool success = settings_result_ >= 0;
        char feedback[192];
        char fitted_feedback[192];
        if (!settings_feedback_.empty()) {
            std::snprintf(feedback, sizeof(feedback), "%s",
                          settings_feedback_.c_str());
        } else if (success) {
            std::snprintf(feedback, sizeof(feedback), "%s",
                          ui_text(UiText::Saved));
        } else {
            std::snprintf(feedback, sizeof(feedback), ui_text(UiText::Error),
                          static_cast<unsigned>(settings_result_));
        }
        ui_font_fit_text(font_small_, kFontSmall, feedback, fitted_feedback,
                         sizeof(fitted_feedback), 300);
        const int feedback_width = ui_font_text_width(
            font_small_, kFontSmall, fitted_feedback);
        ui_font_draw_text(font_small_, 924 - feedback_width,
                          45 + static_cast<int>(
                              3.0F * (1.0F -
                                  settings_message_motion_.value())),
                          ui_fade_color(success ? kSuccess : kError,
                                        settings_message_motion_.value()),
                          kFontSmall, fitted_feedback);
    }
    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "D-PAD",
                           ui_text(UiText::SelectChange),
                           0U, last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "X",
                           ui_text(selected_setting_ == SettingRow::Cache
                                       ? UiText::Clear : UiText::Change),
                           SCE_CTRL_CROSS,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "O", ui_text(UiText::Back),
                           SCE_CTRL_CIRCLE,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "START+SELECT", ui_text(UiText::Quit), 0U,
                  last_action_button_, button_feedback_.value(), 1.0F, 0.0F);
}

void MapScreen::draw(std::uint64_t frame) {
    if (mode_ == ScreenMode::Settings) {
        draw_settings();
    } else if (mode_ == ScreenMode::PinLists) {
        draw_pin_lists();
    } else if (mode_ == ScreenMode::PinList) {
        draw_pin_list();
    } else {
        renderer_.draw(camera_, viewport_, frame);
        draw_pin_overlays();
        draw_crosshair();
        if (hud_motion_.value() > 0.002F)
            draw_chrome();
        draw_attribution();
        draw_message();
    }
    draw_transition_veil();
}

} // namespace vitamaps
