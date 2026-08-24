#include "ui/motion.h"

#include <vita2d.h>

#include <algorithm>
#include <cmath>

namespace vitamaps {

void UiMotionValue::snap(float value) {
    value_ = value;
    target_ = value;
}

void UiMotionValue::tick(double dt, bool reduce_motion) {
    if (reduce_motion) {
        value_ = target_;
        return;
    }
    const float seconds = static_cast<float>(std::clamp(dt, 0.0, 0.05));
    const float blend = 1.0F - std::exp(-response_ * seconds);
    value_ += (target_ - value_) * blend;
    if (std::abs(target_ - value_) < 0.0005F) value_ = target_;
}

bool UiMotionValue::settled(float epsilon) const {
    return std::abs(target_ - value_) <= epsilon;
}

float ui_ease_out_cubic(float progress) {
    const float clamped = std::clamp(progress, 0.0F, 1.0F);
    const float remaining = 1.0F - clamped;
    return 1.0F - remaining * remaining * remaining;
}

std::uint32_t ui_fade_color(std::uint32_t color, float opacity) {
    const float clamped = std::clamp(opacity, 0.0F, 1.0F);
    const std::uint32_t alpha = (color >> 24U) & 0xFFU;
    const auto faded = static_cast<std::uint32_t>(
        static_cast<float>(alpha) * clamped + 0.5F);
    return (color & 0x00FFFFFFU) | (faded << 24U);
}

void ui_draw_focus_glow(float x, float y, float width, float height,
                        std::uint32_t accent, double animation_seconds,
                        float press_emphasis, float opacity) {
    if (width <= 0.0F || height <= 0.0F || opacity <= 0.001F) return;
    const float phase = static_cast<float>(
        std::fmod(std::max(0.0, animation_seconds), 2.6) / 2.6);
    const float pulse = 0.76F + 0.12F *
        (0.5F + 0.5F * std::sin(phase * 6.283185307F));
    const float emphasis = std::clamp(press_emphasis, 0.0F, 1.0F);
    const float expansion = emphasis * 2.0F;
    static constexpr float spreads[] = {12.0F, 7.0F, 3.0F};
    static constexpr int alphas[] = {18, 34, 68};
    for (int layer = 0; layer < 3; ++layer) {
        const float spread = spreads[layer] + expansion;
        const int alpha = static_cast<int>(
            alphas[layer] * pulse * opacity * (1.0F + emphasis * 0.35F));
        const std::uint32_t color =
            (accent & 0x00FFFFFFU) |
            (static_cast<std::uint32_t>(std::clamp(alpha, 0, 255)) << 24U);
        vita2d_draw_rectangle(x - spread, y - spread,
                              width + spread * 2.0F,
                              height + spread * 2.0F, color);
    }
}

} // namespace vitamaps
