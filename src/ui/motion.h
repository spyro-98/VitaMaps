#pragma once

#include <cstdint>

namespace vitamaps {

// Small, allocation-free motion primitive shared by every native screen.
// Exponential smoothing is stable across frame-rate changes and never
// overshoots, which keeps focus and HUD hit targets visually trustworthy.
class UiMotionValue {
public:
    UiMotionValue(float initial = 0.0F, float response_per_second = 18.0F)
        : value_(initial), target_(initial), response_(response_per_second) {}

    void set_target(float target) { target_ = target; }
    void snap(float value);
    void tick(double dt, bool reduce_motion);
    float value() const { return value_; }
    float target() const { return target_; }
    bool settled(float epsilon = 0.002F) const;

private:
    float value_{0.0F};
    float target_{0.0F};
    float response_{18.0F};
};

float ui_ease_out_cubic(float progress);
std::uint32_t ui_fade_color(std::uint32_t color, float opacity);

// VitaMaps' focus language is a restrained cartographic locator glow. It is
// drawn behind a row/card so the content stays sharp while the focus visibly
// travels between targets, matching VitaTube's physical selector model.
void ui_draw_focus_glow(float x, float y, float width, float height,
                        std::uint32_t accent, double animation_seconds,
                        float press_emphasis = 0.0F,
                        float opacity = 1.0F);

} // namespace vitamaps
