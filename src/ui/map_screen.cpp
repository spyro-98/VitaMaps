#include "ui/map_screen.h"

#include "app/app_paths.h"
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
constexpr float kPinClosureSnapPixels = 24.0F;
constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kPi = 3.14159265358979323846;
constexpr float kAtlasTau = 6.283185307F;
constexpr float kAtlasDefaultYaw = -0.38F;
constexpr float kAtlasDefaultPitch = 0.82F;
constexpr float kAtlasMinimumSpacing = 7.0F;
constexpr float kAtlasMaximumSpacing = 58.0F;
constexpr float kAtlasMinimumViewScale = 0.20F;
constexpr float kAtlasMaximumViewScale = 32.0F;
constexpr float kAtlasProjectedCoordinateLimit = 16384.0F;
constexpr float kSettingsRowsStartY = 142.0F;
constexpr float kSettingsRowStep = 52.0F;
constexpr float kSettingsRowHeight = 42.0F;
constexpr float kNavigationRowsStartY = 100.0F;
constexpr float kNavigationRowStep = 82.0F;
constexpr float kNavigationRowHeight = 64.0F;

constexpr float settings_row_y(int row) {
    return kSettingsRowsStartY + static_cast<float>(row) * kSettingsRowStep;
}

constexpr float navigation_row_y(int row) {
    return kNavigationRowsStartY + static_cast<float>(row) *
           kNavigationRowStep;
}

float wrap_atlas_angle(float radians) {
    radians = std::fmod(radians + static_cast<float>(kPi), kAtlasTau);
    if (radians < 0.0F) radians += kAtlasTau;
    return radians - static_cast<float>(kPi);
}

struct AtlasGeometry {
    bool valid{false};
    double minimum_x{1.0};
    double maximum_x{0.0};
    double minimum_y{1.0};
    double maximum_y{0.0};
    double center_x{0.5};
    double center_y{0.5};
    float world_scale{1.0F};
    int minimum_zoom{31};
    int maximum_zoom{0};
    std::size_t layers{0};
    std::size_t tiles{0};
    std::uint64_t bytes{0};
};

struct AtlasPanLimits {
    float x{480.0F};
    float y{330.0F};
};

AtlasGeometry atlas_geometry(const OfflineAtlasSnapshot &snapshot,
                             std::uint32_t provider) {
    AtlasGeometry geometry;
    for (const OfflineAtlasLayer &layer : snapshot.layers) {
        if (layer.provider != provider || layer.tiles == 0U) continue;
        geometry.minimum_x = std::min(geometry.minimum_x, layer.minimum_x);
        geometry.maximum_x = std::max(geometry.maximum_x, layer.maximum_x);
        geometry.minimum_y = std::min(geometry.minimum_y, layer.minimum_y);
        geometry.maximum_y = std::max(geometry.maximum_y, layer.maximum_y);
        geometry.minimum_zoom = std::min(geometry.minimum_zoom, layer.zoom);
        geometry.maximum_zoom = std::max(geometry.maximum_zoom, layer.zoom);
        geometry.tiles += layer.tiles;
        geometry.bytes += layer.bytes;
        ++geometry.layers;
    }
    if (geometry.tiles == 0U || geometry.maximum_x <= geometry.minimum_x ||
        geometry.maximum_y <= geometry.minimum_y)
        return geometry;
    geometry.center_x = (geometry.minimum_x + geometry.maximum_x) * 0.5;
    geometry.center_y = (geometry.minimum_y + geometry.maximum_y) * 0.5;
    const double maximum_span = std::max(geometry.maximum_x - geometry.minimum_x,
                                         geometry.maximum_y - geometry.minimum_y);
    geometry.world_scale = static_cast<float>(390.0 / maximum_span);
    geometry.valid = true;
    return geometry;
}

double atlas_layer_world_span(const OfflineAtlasLayer &layer,
                              bool tile_browse) {
    const double tile_span = std::exp2(-static_cast<double>(layer.zoom));
    if (tile_browse) return tile_span;
    return std::max({layer.maximum_x - layer.minimum_x,
                     layer.maximum_y - layer.minimum_y, tile_span});
}

float atlas_projection_world_scale(const OfflineAtlasLayer &layer,
                                   bool tile_browse) {
    // The selected layer defines the projection reference. At 1x an overview
    // layer occupies about 340 px, while browse mode makes one XYZ tile 256 px.
    // This avoids the old global-cache normalization where a z18/z20 tile could
    // remain tiny even after the relative zoom hit its hard maximum.
    const double target_pixels = tile_browse ? 256.0 : 340.0;
    return static_cast<float>(target_pixels /
        atlas_layer_world_span(layer, tile_browse));
}

AtlasPanLimits atlas_pan_limits(const AtlasGeometry &geometry,
                                const OfflineAtlasLayer &layer,
                                float projection_world_scale,
                                float view_scale, float spacing,
                                float focus_zoom) {
    if (!geometry.valid) return {};
    const float displayed_span = static_cast<float>(
        atlas_layer_world_span(layer, false)) * projection_world_scale *
        std::max(1.0F, view_scale);
    const float plane_radius = std::max(320.0F, displayed_span * 0.75F);
    const float layer_distance = std::max(
        std::abs(static_cast<float>(geometry.minimum_zoom) - focus_zoom),
        std::abs(static_cast<float>(geometry.maximum_zoom) - focus_zoom));
    const float depth_radius = layer_distance * spacing * 1.35F;
    return {480.0F + plane_radius,
            330.0F + plane_radius + depth_radius};
}

bool atlas_layer_has_tile(const OfflineAtlasLayer &layer, int x, int y) {
    const OfflineAtlasTile wanted{x, y};
    const auto found = std::lower_bound(
        layer.tile_index.begin(), layer.tile_index.end(), wanted,
        [](const OfflineAtlasTile &left, const OfflineAtlasTile &right) {
            return left.y < right.y ||
                   (left.y == right.y && left.x < right.x);
        });
    return found != layer.tile_index.end() && found->x == x && found->y == y;
}

mercator::WorldPoint atlas_tile_center(const OfflineAtlasLayer &layer,
                                       std::size_t index) {
    if (layer.tile_index.empty())
        return {(layer.minimum_x + layer.maximum_x) * 0.5,
                (layer.minimum_y + layer.maximum_y) * 0.5};
    index = std::min(index, layer.tile_index.size() - 1U);
    const double count = std::exp2(static_cast<double>(layer.zoom));
    return {(static_cast<double>(layer.tile_index[index].x) + 0.5) / count,
            (static_cast<double>(layer.tile_index[index].y) + 0.5) / count};
}

std::size_t nearest_atlas_tile(const OfflineAtlasLayer &layer,
                               const mercator::WorldPoint &anchor) {
    if (layer.tile_index.empty()) return 0U;
    const double count = std::exp2(static_cast<double>(layer.zoom));
    std::size_t nearest = 0U;
    double nearest_distance = 10.0;
    for (std::size_t index = 0; index < layer.tile_index.size(); ++index) {
        const double x =
            (static_cast<double>(layer.tile_index[index].x) + 0.5) / count;
        const double y =
            (static_cast<double>(layer.tile_index[index].y) + 0.5) / count;
        double dx = std::abs(x - anchor.x);
        dx = std::min(dx, 1.0 - dx);
        const double dy = y - anchor.y;
        const double distance = dx * dx + dy * dy;
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = index;
        }
    }
    return nearest;
}

struct AtlasProjectedPoint {
    float x{0.0F};
    float y{0.0F};
    float depth{0.0F};
};

AtlasProjectedPoint project_atlas_point(float projection_world_scale,
                                        double world_x, double world_y,
                                        double view_center_x,
                                        double view_center_y,
                                        float zoom, float focus_zoom,
                                        float yaw, float pitch,
                                        float spacing, float view_scale,
                                        float pan_x, float pan_y) {
    double world_delta_x = world_x - view_center_x;
    world_delta_x -= std::floor(world_delta_x + 0.5);
    const float local_x = static_cast<float>(world_delta_x) *
                          projection_world_scale * view_scale;
    const float local_y = static_cast<float>(world_y - view_center_y) *
                          projection_world_scale * view_scale;
    const float local_z = (zoom - focus_zoom) * spacing;
    const float cosine_yaw = std::cos(yaw);
    const float sine_yaw = std::sin(yaw);
    const float rotated_x = cosine_yaw * local_x - sine_yaw * local_y;
    const float rotated_y = sine_yaw * local_x + cosine_yaw * local_y;
    const float cosine_pitch = std::cos(pitch);
    const float sine_pitch = std::sin(pitch);
    const float screen_plane_y = cosine_pitch * rotated_y -
                                 sine_pitch * local_z;
    const float depth = sine_pitch * rotated_y + cosine_pitch * local_z;
    const float perspective = std::clamp(1.0F + depth * 0.0007F,
                                         0.78F, 1.22F);
    const float screen_x = 590.0F + pan_x + rotated_x * perspective;
    const float screen_y = 278.0F + pan_y + screen_plane_y * perspective;
    return {std::clamp(screen_x, -kAtlasProjectedCoordinateLimit,
                       kAtlasProjectedCoordinateLimit),
            std::clamp(screen_y, -kAtlasProjectedCoordinateLimit,
                       kAtlasProjectedCoordinateLimit),
            depth};
}

void draw_atlas_color_quad(const AtlasProjectedPoint &top_left,
                           const AtlasProjectedPoint &top_right,
                           const AtlasProjectedPoint &bottom_right,
                           const AtlasProjectedPoint &bottom_left,
                           unsigned int color) {
    auto *vertices = static_cast<vita2d_color_vertex *>(
        vita2d_pool_malloc(sizeof(vita2d_color_vertex) * 4U));
    if (!vertices) return;
    const AtlasProjectedPoint points[] = {
        top_left, top_right, bottom_left, bottom_right};
    for (int index = 0; index < 4; ++index) {
        vertices[index].x = points[index].x;
        vertices[index].y = points[index].y;
        vertices[index].z = 0.5F;
        vertices[index].color = color;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, vertices, 4U);
}

void draw_atlas_texture_quad(const vita2d_texture *texture,
                             const AtlasProjectedPoint &top_left,
                             const AtlasProjectedPoint &top_right,
                             const AtlasProjectedPoint &bottom_right,
                             const AtlasProjectedPoint &bottom_left,
                             unsigned int tint) {
    if (!texture) return;
    auto *vertices = static_cast<vita2d_texture_vertex *>(
        vita2d_pool_malloc(sizeof(vita2d_texture_vertex) * 4U));
    if (!vertices) return;
    const AtlasProjectedPoint points[] = {
        top_left, top_right, bottom_left, bottom_right};
    // Unlike the texture-part helpers, vita2d_draw_array_textured() consumes
    // normalized UV coordinates. Pixel-sized UVs sampled almost the complete
    // quad outside the image and made valid cached tiles look blank.
    const float texture_u[] = {0.0F, 1.0F, 0.0F, 1.0F};
    const float texture_v[] = {0.0F, 0.0F, 1.0F, 1.0F};
    for (int index = 0; index < 4; ++index) {
        vertices[index].x = points[index].x;
        vertices[index].y = points[index].y;
        vertices[index].z = 0.5F;
        vertices[index].u = texture_u[index];
        vertices[index].v = texture_v[index];
    }
    vita2d_draw_array_textured(texture, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP,
                               vertices, 4U, tint);
}

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

unsigned int pin_color(PinColor color, bool muted = false) {
    unsigned int value = kPin;
    switch (color) {
    case PinColor::Coral: value = RGBA8(255, 91, 105, 255); break;
    case PinColor::Cyan: value = RGBA8(72, 206, 230, 255); break;
    case PinColor::Green: value = RGBA8(89, 214, 148, 255); break;
    case PinColor::Violet: value = RGBA8(177, 126, 255, 255); break;
    case PinColor::White: value = RGBA8(242, 246, 250, 255); break;
    case PinColor::Blue: value = RGBA8(88, 145, 255, 255); break;
    case PinColor::Count: break;
    }
    return muted ? ui_fade_color(value, 0.58F) : value;
}

void draw_pin_symbol(float x, float y, PinIcon icon, unsigned int color,
                     float scale) {
    const float radius = 6.0F * scale;
    switch (icon) {
    case PinIcon::Pin:
        vita2d_draw_fill_circle(x, y - 1.0F * scale, radius, color);
        vita2d_draw_line(x, y + 4.0F * scale, x, y + 10.0F * scale, color);
        break;
    case PinIcon::Star:
        vita2d_draw_fill_circle(x, y, radius, color);
        vita2d_draw_line(x - radius - 3.0F, y, x + radius + 3.0F, y, color);
        vita2d_draw_line(x, y - radius - 3.0F, x, y + radius + 3.0F, color);
        break;
    case PinIcon::Flag:
        vita2d_draw_line(x - 4.0F * scale, y - 9.0F * scale,
                         x - 4.0F * scale, y + 9.0F * scale, color);
        vita2d_draw_rectangle(x - 3.0F * scale, y - 9.0F * scale,
                              12.0F * scale, 7.0F * scale, color);
        break;
    case PinIcon::Camp:
        vita2d_draw_line(x - 9.0F * scale, y + 7.0F * scale,
                         x, y - 9.0F * scale, color);
        vita2d_draw_line(x, y - 9.0F * scale,
                         x + 9.0F * scale, y + 7.0F * scale, color);
        vita2d_draw_line(x - 9.0F * scale, y + 7.0F * scale,
                         x + 9.0F * scale, y + 7.0F * scale, color);
        break;
    case PinIcon::Water:
        vita2d_draw_fill_circle(x, y + 2.0F * scale, radius, color);
        vita2d_draw_line(x, y - 10.0F * scale,
                         x - 5.0F * scale, y, color);
        vita2d_draw_line(x, y - 10.0F * scale,
                         x + 5.0F * scale, y, color);
        break;
    case PinIcon::Summit:
        vita2d_draw_line(x - 10.0F * scale, y + 7.0F * scale,
                         x - 1.0F * scale, y - 8.0F * scale, color);
        vita2d_draw_line(x - 1.0F * scale, y - 8.0F * scale,
                         x + 10.0F * scale, y + 7.0F * scale, color);
        vita2d_draw_line(x - 10.0F * scale, y + 7.0F * scale,
                         x + 10.0F * scale, y + 7.0F * scale, color);
        break;
    case PinIcon::Count: break;
    }
}

unsigned int poi_color(PoiCategory category) {
    switch (category) {
    case PoiCategory::Food: return RGBA8(255, 126, 150, 255);
    case PoiCategory::Water: return RGBA8(72, 206, 230, 255);
    case PoiCategory::Shelter: return RGBA8(89, 214, 148, 255);
    case PoiCategory::Summit: return RGBA8(242, 246, 250, 255);
    case PoiCategory::Tourism: return RGBA8(177, 126, 255, 255);
    case PoiCategory::Nature: return RGBA8(104, 226, 169, 255);
    case PoiCategory::Amenity: return RGBA8(88, 145, 255, 255);
    }
    return kAccent;
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
    atlas_tile_requests_.reserve(48U);
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
            settings_focus_y_.snap(settings_row_y(selected_setting_index()));
        } else if (mode_ == ScreenMode::Navigation) {
            navigation_focus_y_.snap(navigation_row_y(
                static_cast<int>(selected_navigation_)));
        } else if (mode_ == ScreenMode::PinLists) {
            const std::size_t first = visible_first(selected_list_, 6);
            list_focus_y_.snap(82.0F +
                static_cast<float>(selected_list_ - first) * 67.0F);
        } else if (mode_ == ScreenMode::PinList) {
            const std::size_t first = visible_first(selected_pin_, 6);
            pin_focus_y_.snap(137.0F +
                static_cast<float>(selected_pin_ - first) * 57.0F);
        } else if (mode_ == ScreenMode::Gpx) {
            const std::size_t first = visible_first(selected_gpx_, 4);
            gpx_focus_y_.snap(108.0F +
                static_cast<float>(selected_gpx_ - first) * 48.0F);
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
    const bool scale_visible = hud_visible_ ||
                               preferences_scale_always_visible();
    scale_motion_.set_target(scale_visible ? 1.0F : 0.0F);
    scale_motion_.tick(dt, reduced);
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
        SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_LEFT |
        SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN;
    if (pressed & action_mask) {
        static constexpr unsigned int order[] = {
            SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE,
            SCE_CTRL_TRIANGLE, SCE_CTRL_SELECT, SCE_CTRL_START,
            SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER, SCE_CTRL_LEFT,
            SCE_CTRL_RIGHT, SCE_CTRL_UP, SCE_CTRL_DOWN};
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
        settings_focus_y_.set_target(settings_row_y(selected_setting_index()));
        settings_focus_y_.tick(dt, reduced);
    } else if (mode_ == ScreenMode::Navigation) {
        navigation_focus_y_.set_target(navigation_row_y(
            static_cast<int>(selected_navigation_)));
        navigation_focus_y_.tick(dt, reduced);
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
    } else if (mode_ == ScreenMode::Gpx && !gpx_inbox_.empty()) {
        constexpr std::size_t rows = 4;
        const std::size_t first = visible_first(selected_gpx_, rows);
        const float row = static_cast<float>(selected_gpx_ - first);
        gpx_focus_y_.set_target(108.0F + row * 48.0F);
        gpx_focus_y_.tick(dt, reduced);
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
            settings_focus_y_.snap(settings_row_y(selected_setting_index()));
        } else if (mode_ == ScreenMode::Navigation) {
            navigation_focus_y_.snap(navigation_row_y(
                static_cast<int>(selected_navigation_)));
        } else if (mode_ == ScreenMode::PinLists) {
            const std::size_t first = visible_first(selected_list_, 6);
            list_focus_y_.snap(82.0F +
                static_cast<float>(selected_list_ - first) * 67.0F);
        } else if (mode_ == ScreenMode::PinList) {
            const std::size_t first = visible_first(selected_pin_, 6);
            pin_focus_y_.snap(137.0F +
                static_cast<float>(selected_pin_ - first) * 57.0F);
        } else if (mode_ == ScreenMode::Gpx) {
            const std::size_t first = visible_first(selected_gpx_, 4);
            gpx_focus_y_.snap(108.0F +
                static_cast<float>(selected_gpx_ - first) * 48.0F);
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
    const PinList &list = pins_.active();
    if (list.pins.size() >= 3) {
        // The marker and crosshair are screen-space controls. Comparing their
        // rendered positions keeps the snap target stable across zoom and
        // rotation, and avoids adding a duplicate closing coordinate.
        const auto first = camera_.geo_to_screen(
            list.pins.front().position.latitude,
            list.pins.front().position.longitude,
            viewport_.width, viewport_.height);
        if (point_distance(static_cast<float>(first.x),
                           static_cast<float>(first.y),
                           viewport_.width * 0.5F,
                           viewport_.height * 0.5F) <=
            kPinClosureSnapPixels) {
            if (pins_.set_list_closed(pins_.active_index(), true)) {
                pinning_ = false;
                log_printf("pin path closed from map: list=%u points=%u",
                           static_cast<unsigned>(pins_.active_index()),
                           static_cast<unsigned>(list.pins.size()));
                persist_pins(ui_text(UiText::Closed));
            }
            return;
        }
    }
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
    cache_status_.status = result.status;
    cache_status_.cleared = result.cleared;
    cache_status_.atlas_loaded = false;
    cache_status_.error = result.error;
    cache_status_valid_ = true;
    cache_clear_pending_ = false;
    cache_refresh_timer_ = 5.0;
    if (result.atlas_loaded) {
        offline_atlas_ = std::move(result.atlas);
        offline_atlas_valid_ = result.error == 0;
        if (offline_atlas_valid_)
            sync_offline_atlas_selection(true);
    }
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

void MapScreen::poll_gpx_result() {
    GpxWorkerResult result;
    if (!manager_.take_gpx_result(result)) return;
    gpx_inbox_ = std::move(result.inbox);
    gpx_history_ = std::move(result.history);
    if (result.repository_changed) {
        pins_ = std::move(result.repository);
        selected_list_ = pins_.active_index();
    }
    if (result.operation.error < 0) {
        char error[96];
        std::snprintf(error, sizeof(error), ui_text(UiText::Error),
                      static_cast<unsigned>(result.operation.error));
        show_message(error, true, 4.0);
    } else if (result.type == GpxRequestType::Import) {
        gpx_history_offset_ = 0;
        char message[96];
        std::snprintf(message, sizeof(message), ui_text(UiText::GpxImported),
                      static_cast<unsigned>(result.operation.points));
        show_message(message, false, 4.0);
    } else if (result.type == GpxRequestType::Export) {
        show_message(ui_text(UiText::GpxExported), false, 4.0);
    }
    if (gpx_inbox_.empty()) selected_gpx_ = 0;
    else selected_gpx_ = std::min(selected_gpx_, gpx_inbox_.size() - 1U);
    if (gpx_history_.empty()) gpx_history_offset_ = 0;
    else gpx_history_offset_ = std::min(
        gpx_history_offset_, ((gpx_history_.size() - 1U) / 3U) * 3U);
}

void MapScreen::poll_poi_result() {
    OverpassResult result;
    if (!manager_.take_poi_result(result)) return;
    if (result.error < 0) {
        char error[96];
        std::snprintf(error, sizeof(error), ui_text(UiText::Error),
                      static_cast<unsigned>(result.error));
        show_message(error, true, 4.0);
        return;
    }
    pois_ = std::move(result.points);
    char message[96];
    std::snprintf(message, sizeof(message), ui_text(UiText::PoiFound),
                  static_cast<unsigned>(pois_.size()));
    show_message(message, false, 3.0);
}

void MapScreen::poll_elevation_result() {
    ElevationOperationResult result;
    if (!manager_.take_elevation_result(result)) return;
    if (result.elevation.error < 0) {
        char error[96];
        std::snprintf(error, sizeof(error), ui_text(UiText::Error),
                      static_cast<unsigned>(result.elevation.error));
        show_message(error, true, 4.0);
        return;
    }
    for (std::size_t list_index = 0; list_index < pins_.lists().size();
         ++list_index) {
        if (pins_.lists()[list_index].id != result.list_id) continue;
        const std::size_t count = std::min(
            pins_.lists()[list_index].pins.size(),
            result.elevation.meters.size());
        for (std::size_t index = 0; index < count; ++index)
            pins_.set_pin_elevation(list_index, index,
                                    result.elevation.meters[index]);
        persist_pins();
        break;
    }
}

void MapScreen::request_pois() {
    if (manager_.poi_pending()) {
        show_message(ui_text(UiText::PoiLoading), false, 2.0);
        return;
    }
    const double latitude_radians = camera_.latitude * kPi / 180.0;
    const double meters_per_pixel =
        std::cos(latitude_radians) * (2.0 * kPi * kEarthRadiusMeters) /
        std::exp2(camera_.zoom + 8.0);
    const double radius = std::clamp(meters_per_pixel * 430.0, 250.0, 5000.0);
    if (!manager_.request_pois({camera_.latitude, camera_.longitude}, radius)) {
        show_message(ui_text(UiText::SearchOffline), true, 4.0);
        return;
    }
    show_message(ui_text(UiText::PoiLoading), false, 30.0);
}

void MapScreen::request_elevation_for_list(std::size_t list_index) {
    if (!preferences_hiking_mode() || list_index >= pins_.lists().size() ||
        manager_.elevation_pending())
        return;
    const PinList &list = pins_.lists()[list_index];
    if (list.pins.empty()) return;
    bool missing = false;
    std::vector<mercator::GeoPoint> points;
    points.reserve(list.pins.size());
    for (const MapPin &pin : list.pins) {
        points.push_back(pin.position);
        missing = missing || !pin.has_elevation;
    }
    if (missing) manager_.request_elevation(list.id, points);
}

void MapScreen::update_navigation(unsigned int pressed) {
    reset_touch_state();
    if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
        request_mode(ScreenMode::Map);
        return;
    }

    constexpr int item_count = static_cast<int>(NavigationItem::Count);
    if (pressed & SCE_CTRL_UP) {
        int item = static_cast<int>(selected_navigation_) - 1;
        if (item < 0) item = item_count - 1;
        selected_navigation_ = static_cast<NavigationItem>(item);
    } else if (pressed & SCE_CTRL_DOWN) {
        int item = static_cast<int>(selected_navigation_) + 1;
        if (item >= item_count) item = 0;
        selected_navigation_ = static_cast<NavigationItem>(item);
    } else if (pressed & SCE_CTRL_CROSS) {
        switch (selected_navigation_) {
        case NavigationItem::Map:
            request_mode(ScreenMode::Map);
            break;
        case NavigationItem::OfflineAtlas: {
            const bool loading = manager_.request_offline_atlas();
            if (loading) offline_atlas_valid_ = false;
            if (!loading && !offline_atlas_valid_) {
                show_message(ui_text(UiText::CacheReading), false, 2.0);
                break;
            }
            offline_atlas_style_ = provider_.style_index();
            atlas_layer_browse_ = false;
            atlas_view_scale_.set_target(1.0F);
            atlas_pan_x_.set_target(0.0F);
            atlas_pan_y_.set_target(0.0F);
            request_mode(ScreenMode::OfflineAtlas);
            break;
        }
        case NavigationItem::PinLists:
            pin_lists_return_mode_ = ScreenMode::Navigation;
            selected_list_ = pins_.active_index();
            delete_confirm_armed_ = false;
            pinning_ = false;
            request_mode(ScreenMode::PinLists);
            break;
        case NavigationItem::Settings:
            selected_setting_ = setting_category_row(0);
            cache_refresh_timer_ = 5.0;
            manager_.request_cache_status();
            request_mode(ScreenMode::Settings);
            break;
        case NavigationItem::Count:
            break;
        }
    }
}

int MapScreen::setting_category_row_count() const {
    switch (selected_setting_category_) {
    case SettingCategory::Map: return 2;
    case SettingCategory::Interface: return 6;
    case SettingCategory::Storage: return 2;
    case SettingCategory::Count: return 0;
    }
    return 0;
}

MapScreen::SettingRow MapScreen::setting_category_row(int index) const {
    static constexpr SettingRow map_rows[] = {
        SettingRow::MapStyle, SettingRow::Hiking};
    static constexpr SettingRow interface_rows[] = {
        SettingRow::Language, SettingRow::HudBehavior,
        SettingRow::MapScale, SettingRow::Crosshair,
        SettingRow::Units, SettingRow::Animations};
    static constexpr SettingRow storage_rows[] = {
        SettingRow::Cache, SettingRow::DiskLogging};
    switch (selected_setting_category_) {
    case SettingCategory::Map:
        return map_rows[std::clamp(index, 0, 1)];
    case SettingCategory::Interface:
        return interface_rows[std::clamp(index, 0, 5)];
    case SettingCategory::Storage:
        return storage_rows[std::clamp(index, 0, 1)];
    case SettingCategory::Count:
        return SettingRow::MapStyle;
    }
    return SettingRow::MapStyle;
}

int MapScreen::selected_setting_index() const {
    const int count = setting_category_row_count();
    for (int index = 0; index < count; ++index) {
        if (setting_category_row(index) == selected_setting_) return index;
    }
    return 0;
}

void MapScreen::change_setting_category(int direction) {
    constexpr int category_count = static_cast<int>(SettingCategory::Count);
    int category = static_cast<int>(selected_setting_category_) +
                   (direction < 0 ? -1 : 1);
    if (category < 0) category = category_count - 1;
    if (category >= category_count) category = 0;
    selected_setting_category_ = static_cast<SettingCategory>(category);
    selected_setting_ = setting_category_row(0);
    cache_clear_confirm_ = false;
    if (selected_setting_category_ == SettingCategory::Storage) {
        manager_.request_cache_status();
        cache_refresh_timer_ = 5.0;
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
        request_mode(ScreenMode::Navigation);
        log_printf("settings closed");
    } else if (pressed & SCE_CTRL_LTRIGGER) {
        change_setting_category(-1);
    } else if (pressed & SCE_CTRL_RTRIGGER) {
        change_setting_category(1);
    } else if (pressed & SCE_CTRL_UP) {
        cache_clear_confirm_ = false;
        int row = selected_setting_index() - 1;
        if (row < 0) row = setting_category_row_count() - 1;
        selected_setting_ = setting_category_row(row);
    } else if (pressed & SCE_CTRL_DOWN) {
        cache_clear_confirm_ = false;
        int row = selected_setting_index() + 1;
        if (row >= setting_category_row_count()) row = 0;
        selected_setting_ = setting_category_row(row);
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
        request_mode(pin_lists_return_mode_);
    } else if (pressed & SCE_CTRL_UP) {
        delete_confirm_armed_ = false;
        selected_list_ = selected_list_ == 0 ? count - 1 : selected_list_ - 1;
    } else if (pressed & SCE_CTRL_DOWN) {
        delete_confirm_armed_ = false;
        selected_list_ = (selected_list_ + 1) % count;
    } else if (pressed & SCE_CTRL_LEFT) {
        const int count_colors = static_cast<int>(PinColor::Count);
        const int current = static_cast<int>(pins_.lists()[selected_list_].color);
        pins_.set_list_color(selected_list_, static_cast<PinColor>(
            (current + count_colors - 1) % count_colors));
        persist_pins();
    } else if (pressed & SCE_CTRL_RIGHT) {
        const int count_icons = static_cast<int>(PinIcon::Count);
        const int current = static_cast<int>(pins_.lists()[selected_list_].icon);
        pins_.set_list_icon(selected_list_, static_cast<PinIcon>(
            (current + 1) % count_icons));
        persist_pins();
    } else if (pressed & SCE_CTRL_LTRIGGER) {
        selected_gpx_ = 0;
        manager_.request_gpx_refresh();
        request_mode(ScreenMode::Gpx);
    } else if (pressed & SCE_CTRL_CROSS) {
        delete_confirm_armed_ = false;
        pins_.set_active(selected_list_);
        persist_pins();
        selected_pin_ = 0;
        request_elevation_for_list(selected_list_);
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

void MapScreen::update_gpx(unsigned int pressed) {
    reset_touch_state();
    if (pressed & SCE_CTRL_CIRCLE) {
        request_mode(ScreenMode::PinLists);
    } else if (!gpx_inbox_.empty() && (pressed & SCE_CTRL_UP)) {
        selected_gpx_ = selected_gpx_ == 0
            ? gpx_inbox_.size() - 1U : selected_gpx_ - 1U;
    } else if (!gpx_inbox_.empty() && (pressed & SCE_CTRL_DOWN)) {
        selected_gpx_ = (selected_gpx_ + 1U) % gpx_inbox_.size();
    } else if ((pressed & SCE_CTRL_LEFT) && gpx_history_offset_ >= 3U) {
        gpx_history_offset_ -= 3U;
    } else if ((pressed & SCE_CTRL_RIGHT) &&
               gpx_history_offset_ + 3U < gpx_history_.size()) {
        gpx_history_offset_ += 3U;
    } else if ((pressed & SCE_CTRL_TRIANGLE) && !manager_.gpx_pending()) {
        manager_.request_gpx_refresh();
    } else if ((pressed & SCE_CTRL_CROSS) && !gpx_inbox_.empty() &&
               !manager_.gpx_pending()) {
        manager_.request_gpx_import(selected_gpx_, pins_);
    } else if ((pressed & SCE_CTRL_SQUARE) && !manager_.gpx_pending()) {
        manager_.request_gpx_export(pins_.active());
    }
}

const OfflineAtlasLayer *MapScreen::selected_offline_atlas_layer() const {
    if (!offline_atlas_valid_ || offline_atlas_style_ < 0 ||
        offline_atlas_style_ >= provider_.style_count())
        return nullptr;
    const std::uint32_t provider_id =
        provider_.style_id(offline_atlas_style_);
    const OfflineAtlasLayer *nearest = nullptr;
    int nearest_distance = 1000;
    for (const OfflineAtlasLayer &layer : offline_atlas_.layers) {
        if (layer.provider != provider_id || layer.tiles == 0U) continue;
        if (layer.zoom == atlas_selected_zoom_) return &layer;
        const int distance = std::abs(layer.zoom - atlas_selected_zoom_);
        if (!nearest || distance < nearest_distance) {
            nearest = &layer;
            nearest_distance = distance;
        }
    }
    return nearest;
}

void MapScreen::sync_offline_atlas_selection(bool prefer_camera_position) {
    const bool had_selection = atlas_selected_zoom_ >= 0;
    const std::uint32_t provider_id =
        provider_.style_id(offline_atlas_style_);
    const int wanted_zoom = prefer_camera_position
        ? static_cast<int>(std::lround(camera_.zoom)) : atlas_selected_zoom_;
    const OfflineAtlasLayer *selected = nullptr;
    int selected_distance = 1000;
    for (const OfflineAtlasLayer &layer : offline_atlas_.layers) {
        if (layer.provider != provider_id || layer.tiles == 0U) continue;
        const int distance = std::abs(layer.zoom - wanted_zoom);
        if (!selected || distance < selected_distance) {
            selected = &layer;
            selected_distance = distance;
        }
    }
    if (!selected) {
        atlas_selected_zoom_ = -1;
        atlas_selected_tile_ = 0U;
        atlas_tile_requests_.clear();
        return;
    }
    atlas_selected_zoom_ = selected->zoom;
    if (!had_selection) atlas_focus_zoom_.snap(
        static_cast<float>(atlas_selected_zoom_));
    else atlas_focus_zoom_.set_target(
        static_cast<float>(atlas_selected_zoom_));

    if (selected->tile_index.empty()) {
        atlas_selected_tile_ = 0U;
        return;
    }
    const mercator::WorldPoint anchor = prefer_camera_position
        ? camera_.world()
        : mercator::WorldPoint{atlas_world_center_x_.target(),
                               atlas_world_center_y_.target()};
    atlas_selected_tile_ = nearest_atlas_tile(*selected, anchor);

    const mercator::WorldPoint view_center =
        atlas_tile_center(*selected, atlas_selected_tile_);
    if (!had_selection) {
        atlas_world_center_x_.snap(static_cast<float>(view_center.x));
        atlas_world_center_y_.snap(static_cast<float>(view_center.y));
    } else {
        atlas_world_center_x_.set_target(static_cast<float>(view_center.x));
        atlas_world_center_y_.set_target(static_cast<float>(view_center.y));
    }
}

void MapScreen::move_offline_atlas_layer(int direction) {
    const OfflineAtlasLayer *current = selected_offline_atlas_layer();
    if (!current) {
        sync_offline_atlas_selection(true);
        return;
    }
    const mercator::WorldPoint anchor =
        atlas_tile_center(*current, atlas_selected_tile_);
    const std::uint32_t provider_id =
        provider_.style_id(offline_atlas_style_);
    const OfflineAtlasLayer *next = nullptr;
    const OfflineAtlasLayer *wrapped = nullptr;
    for (const OfflineAtlasLayer &layer : offline_atlas_.layers) {
        if (layer.provider != provider_id || layer.tiles == 0U) continue;
        if (!wrapped || (direction > 0 ? layer.zoom < wrapped->zoom
                                      : layer.zoom > wrapped->zoom))
            wrapped = &layer;
        if ((direction > 0 && layer.zoom > current->zoom &&
             (!next || layer.zoom < next->zoom)) ||
            (direction < 0 && layer.zoom < current->zoom &&
             (!next || layer.zoom > next->zoom)))
            next = &layer;
    }
    if (!next) next = wrapped;
    if (!next) return;
    atlas_selected_zoom_ = next->zoom;
    atlas_focus_zoom_.set_target(static_cast<float>(next->zoom));
    atlas_selected_tile_ = nearest_atlas_tile(*next, anchor);
    const mercator::WorldPoint target =
        atlas_tile_center(*next, atlas_selected_tile_);
    atlas_world_center_x_.set_target(static_cast<float>(target.x));
    atlas_world_center_y_.set_target(static_cast<float>(target.y));
    atlas_pan_x_.set_target(0.0F);
    atlas_pan_y_.set_target(0.0F);
    atlas_view_scale_.set_target(1.0F);
}

void MapScreen::move_offline_atlas_tile(int direction_x, int direction_y) {
    const OfflineAtlasLayer *layer = selected_offline_atlas_layer();
    if (!layer || layer->tile_index.empty() ||
        (direction_x == 0 && direction_y == 0))
        return;
    atlas_selected_tile_ = std::min(atlas_selected_tile_,
                                    layer->tile_index.size() - 1U);
    const OfflineAtlasTile current = layer->tile_index[atlas_selected_tile_];
    const int count = 1 << layer->zoom;
    const int exact_x = wrap_tile_x(current.x + direction_x, layer->zoom);
    const int exact_y = current.y + direction_y;

    std::size_t next = atlas_selected_tile_;
    bool found_exact = false;
    for (std::size_t index = 0; index < layer->tile_index.size(); ++index) {
        const OfflineAtlasTile &candidate = layer->tile_index[index];
        if (candidate.x == exact_x && candidate.y == exact_y) {
            next = index;
            found_exact = true;
            break;
        }
    }
    if (!found_exact) {
        long best_score = 0x7fffffffL;
        bool found_directional = false;
        for (std::size_t index = 0; index < layer->tile_index.size(); ++index) {
            const OfflineAtlasTile &candidate = layer->tile_index[index];
            int dx = candidate.x - current.x;
            if (dx > count / 2) dx -= count;
            if (dx < -count / 2) dx += count;
            const int dy = candidate.y - current.y;
            const int forward = dx * direction_x + dy * direction_y;
            if (forward <= 0) continue;
            const int perpendicular =
                std::abs(dx * direction_y - dy * direction_x);
            const long score = static_cast<long>(perpendicular) * 100000L +
                               static_cast<long>(forward);
            if (!found_directional || score < best_score) {
                found_directional = true;
                best_score = score;
                next = index;
            }
        }
    }
    atlas_selected_tile_ = next;
    const mercator::WorldPoint target =
        atlas_tile_center(*layer, atlas_selected_tile_);
    atlas_world_center_x_.set_target(static_cast<float>(target.x));
    atlas_world_center_y_.set_target(static_cast<float>(target.y));
}

void MapScreen::build_offline_atlas_requests() {
    atlas_tile_requests_.clear();
    const OfflineAtlasLayer *selected = selected_offline_atlas_layer();
    if (!selected || selected->tile_index.empty()) return;

    const std::uint32_t provider_id =
        provider_.style_id(offline_atlas_style_);
    const mercator::WorldPoint anchor = atlas_layer_browse_
        ? mercator::WorldPoint{atlas_world_center_x_.target(),
                               atlas_world_center_y_.target()}
        : atlas_tile_center(*selected, atlas_selected_tile_);
    if (atlas_layer_browse_)
        atlas_selected_tile_ = nearest_atlas_tile(*selected, anchor);
    const OfflineAtlasLayer *layers[32]{};
    std::size_t layer_count = 0U;
    std::size_t selected_index = 0U;
    for (const OfflineAtlasLayer &layer : offline_atlas_.layers) {
        if (layer.provider != provider_id || layer.tile_index.empty() ||
            layer_count >= 32U)
            continue;
        layers[layer_count] = &layer;
        if (layer.zoom == selected->zoom) selected_index = layer_count;
        ++layer_count;
    }

    const std::size_t first = selected_index == 0U ? 0U : selected_index - 1U;
    const std::size_t last = std::min(layer_count - 1U, selected_index + 1U);
    for (std::size_t layer_index = first; layer_index <= last; ++layer_index) {
        const OfflineAtlasLayer &layer = *layers[layer_index];
        const int level_distance = static_cast<int>(
            layer_index > selected_index ? layer_index - selected_index
                                         : selected_index - layer_index);
        const int count = 1 << layer.zoom;
        int center_x = static_cast<int>(std::floor(anchor.x * count));
        int center_y = static_cast<int>(std::floor(anchor.y * count));
        if (level_distance == 0) {
            const std::size_t index = atlas_layer_browse_
                ? nearest_atlas_tile(layer, anchor)
                : std::min(atlas_selected_tile_,
                           layer.tile_index.size() - 1U);
            center_x = layer.tile_index[index].x;
            center_y = layer.tile_index[index].y;
        }
        const int radius = level_distance == 0 ? 2 : 1;
        const std::size_t layer_request_begin = atlas_tile_requests_.size();
        for (int dy = -radius; dy <= radius; ++dy) {
            const int y = center_y + dy;
            if (y < 0 || y >= count) continue;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int x = wrap_tile_x(center_x + dx, layer.zoom);
                if (!atlas_layer_has_tile(layer, x, y)) continue;
                const float distance = static_cast<float>(dx * dx + dy * dy);
                atlas_tile_requests_.push_back(
                    {{provider_id, layer.zoom, x, y},
                     static_cast<float>(level_distance * 4000) + distance,
                     level_distance == 0, true});
            }
        }
        if (atlas_tile_requests_.size() == layer_request_begin) {
            const std::size_t nearest = nearest_atlas_tile(layer, anchor);
            const OfflineAtlasTile &fallback = layer.tile_index[nearest];
            const TileKey fallback_key{provider_id, layer.zoom,
                                       fallback.x, fallback.y};
            atlas_tile_requests_.push_back(
                {fallback_key, static_cast<float>(level_distance * 4000),
                 level_distance == 0, true});
        }
    }
}

bool MapScreen::open_offline_atlas_target() {
    const OfflineAtlasLayer *layer = selected_offline_atlas_layer();
    if (!layer) return false;
    double world_x = (layer->minimum_x + layer->maximum_x) * 0.5;
    double world_y = (layer->minimum_y + layer->maximum_y) * 0.5;
    if (!layer->tile_index.empty()) {
        atlas_selected_tile_ = std::min(atlas_selected_tile_,
                                        layer->tile_index.size() - 1U);
        const mercator::WorldPoint center =
            atlas_tile_center(*layer, atlas_selected_tile_);
        world_x = center.x;
        world_y = center.y;
    }
    const int previous_style = provider_.style_index();
    const bool previous_hiking = preferences_hiking_mode();
    if (!provider_.set_style(offline_atlas_style_)) return false;
    int result = preferences_set_map_style(offline_atlas_style_);
    if (result == 0)
        result = preferences_set_hiking_mode(offline_atlas_style_ == 4);
    if (result < 0) {
        provider_.set_style(previous_style);
        preferences_set_map_style(previous_style);
        preferences_set_hiking_mode(previous_hiking);
        char error[64];
        std::snprintf(error, sizeof(error), ui_text(UiText::Error),
                      static_cast<unsigned>(result));
        show_message(error, true);
        return false;
    }
    const mercator::GeoPoint target =
        mercator::world_to_lat_lon(world_x, world_y);
    camera_.min_zoom = provider_.min_zoom();
    camera_.max_zoom = provider_.max_zoom();
    camera_.set_center(target.latitude, target.longitude);
    camera_.zoom = std::clamp(static_cast<double>(layer->zoom),
                              camera_.min_zoom, camera_.max_zoom);
    camera_.target_zoom = camera_.zoom;
    camera_.velocityX = 0.0;
    camera_.velocityY = 0.0;
    pois_.clear();
    request_mode(ScreenMode::Map);
    log_printf("offline atlas: map target style=%d z=%d lat=%.6f lon=%.6f",
               offline_atlas_style_, layer->zoom,
               target.latitude, target.longitude);
    return true;
}

void MapScreen::update_offline_atlas(double dt, const SceCtrlData &controls,
                                     unsigned int pressed) {
    reset_touch_state();
    if (pressed & SCE_CTRL_CIRCLE) {
        if (atlas_layer_browse_) {
            atlas_layer_browse_ = false;
            atlas_view_scale_.set_target(1.0F);
            const OfflineAtlasLayer *layer = selected_offline_atlas_layer();
            if (layer) {
                const mercator::WorldPoint center = atlas_tile_center(
                    *layer, atlas_selected_tile_);
                atlas_world_center_x_.set_target(
                    static_cast<float>(center.x));
                atlas_world_center_y_.set_target(
                    static_cast<float>(center.y));
            }
            atlas_pan_x_.set_target(0.0F);
            atlas_pan_y_.set_target(0.0F);
        } else {
            request_mode(ScreenMode::Navigation);
        }
        return;
    }
    if (pressed & SCE_CTRL_START) {
        if (manager_.request_offline_atlas()) offline_atlas_valid_ = false;
    }
    if ((pressed & SCE_CTRL_TRIANGLE) && !atlas_layer_browse_) {
        offline_atlas_style_ = (offline_atlas_style_ + 1) %
                               provider_.style_count();
        sync_offline_atlas_selection(true);
        atlas_view_scale_.set_target(1.0F);
        atlas_pan_x_.set_target(0.0F);
        atlas_pan_y_.set_target(0.0F);
    }
    const OfflineAtlasLayer *selected = selected_offline_atlas_layer();
    if (pressed & SCE_CTRL_SQUARE) {
        if (atlas_layer_browse_) {
            atlas_layer_browse_ = false;
            atlas_view_scale_.set_target(1.0F);
            if (selected) {
                const mercator::WorldPoint center = atlas_tile_center(
                    *selected, atlas_selected_tile_);
                atlas_world_center_x_.set_target(
                    static_cast<float>(center.x));
                atlas_world_center_y_.set_target(
                    static_cast<float>(center.y));
            }
            atlas_pan_x_.set_target(0.0F);
            atlas_pan_y_.set_target(0.0F);
        } else if (selected && !selected->tile_index.empty()) {
            atlas_layer_browse_ = true;
            const mercator::WorldPoint center =
                atlas_tile_center(*selected, atlas_selected_tile_);
            atlas_world_center_x_.set_target(static_cast<float>(center.x));
            atlas_world_center_y_.set_target(static_cast<float>(center.y));
            atlas_view_scale_.set_target(1.0F);
            atlas_pan_x_.set_target(0.0F);
            atlas_pan_y_.set_target(0.0F);
        }
    }
    if (atlas_layer_browse_) {
        if (pressed & SCE_CTRL_LEFT) move_offline_atlas_tile(-1, 0);
        else if (pressed & SCE_CTRL_RIGHT) move_offline_atlas_tile(1, 0);
        else if (pressed & SCE_CTRL_UP) move_offline_atlas_tile(0, -1);
        else if (pressed & SCE_CTRL_DOWN) move_offline_atlas_tile(0, 1);
    } else {
        if (pressed & SCE_CTRL_UP) move_offline_atlas_layer(-1);
        else if (pressed & SCE_CTRL_DOWN) move_offline_atlas_layer(1);
        float spacing = atlas_spacing_.target();
        if (pressed & SCE_CTRL_LEFT) spacing -= 3.0F;
        if (pressed & SCE_CTRL_RIGHT) spacing += 3.0F;
        atlas_spacing_.set_target(std::clamp(
            spacing, kAtlasMinimumSpacing, kAtlasMaximumSpacing));
    }
    selected = selected_offline_atlas_layer();
    if ((pressed & SCE_CTRL_CROSS) && open_offline_atlas_target()) return;
    if (pressed & SCE_CTRL_SELECT) {
        atlas_yaw_target_ = kAtlasDefaultYaw;
        atlas_pitch_target_ = kAtlasDefaultPitch;
        atlas_pan_x_.set_target(0.0F);
        atlas_pan_y_.set_target(0.0F);
        atlas_spacing_.set_target(24.0F);
        const AtlasGeometry geometry = atlas_geometry(
            offline_atlas_, provider_.style_id(offline_atlas_style_));
        if (selected && geometry.valid) {
            const mercator::WorldPoint center =
                atlas_tile_center(*selected, atlas_selected_tile_);
            atlas_world_center_x_.set_target(static_cast<float>(center.x));
            atlas_world_center_y_.set_target(static_cast<float>(center.y));
            atlas_view_scale_.set_target(1.0F);
        }
    }

    const float seconds = static_cast<float>(std::clamp(dt, 0.0, 0.05));
    const float yaw_input = static_cast<float>(analog_axis(controls.rx));
    const float pitch_input = static_cast<float>(analog_axis(controls.ry));
    atlas_yaw_target_ = wrap_atlas_angle(
        atlas_yaw_target_ + yaw_input * seconds * 2.15F);
    atlas_pitch_target_ = wrap_atlas_angle(
        atlas_pitch_target_ + pitch_input * seconds * 2.15F);

    float scale_target = atlas_view_scale_.target();
    if (pressed & SCE_CTRL_LTRIGGER) scale_target /= 1.18F;
    if (pressed & SCE_CTRL_RTRIGGER) scale_target *= 1.18F;
    if (controls.buttons & SCE_CTRL_LTRIGGER)
        scale_target *= std::exp(-seconds * 1.7F);
    if (controls.buttons & SCE_CTRL_RTRIGGER)
        scale_target *= std::exp(seconds * 1.7F);
    scale_target = std::clamp(scale_target, kAtlasMinimumViewScale,
                              kAtlasMaximumViewScale);
    atlas_view_scale_.set_target(scale_target);

    const float pan_x_input = static_cast<float>(analog_axis(controls.lx));
    const float pan_y_input = static_cast<float>(analog_axis(controls.ly));
    if (atlas_layer_browse_) {
        const AtlasGeometry geometry = atlas_geometry(
            offline_atlas_, provider_.style_id(offline_atlas_style_));
        if (geometry.valid && selected) {
            const float projection_world_scale =
                atlas_projection_world_scale(*selected, true);
            const float denominator = std::max(
                1.0F, projection_world_scale *
                          atlas_view_scale_.target());
            float world_x = atlas_world_center_x_.target() -
                pan_x_input * seconds * 260.0F / denominator;
            world_x -= std::floor(world_x);
            atlas_world_center_x_.set_target(world_x);
            atlas_world_center_y_.set_target(std::clamp(
                atlas_world_center_y_.target() -
                pan_y_input * seconds * 260.0F / denominator,
                0.0F, 1.0F));
        }
    } else {
        const AtlasGeometry geometry = atlas_geometry(
            offline_atlas_, provider_.style_id(offline_atlas_style_));
        const float projection_world_scale = selected
            ? atlas_projection_world_scale(*selected, false)
            : geometry.world_scale;
        const AtlasPanLimits limits = selected
            ? atlas_pan_limits(geometry, *selected, projection_world_scale,
                               scale_target, atlas_spacing_.target(),
                               atlas_focus_zoom_.target())
            : AtlasPanLimits{};
        const float pan_speed = 310.0F * std::clamp(
            std::sqrt(std::max(1.0F, scale_target)), 1.0F, 8.0F);
        atlas_pan_x_.set_target(std::clamp(
            atlas_pan_x_.target() - pan_x_input * seconds * pan_speed,
            -limits.x, limits.x));
        atlas_pan_y_.set_target(std::clamp(
            atlas_pan_y_.target() - pan_y_input * seconds * pan_speed,
            -limits.y, limits.y));
    }

    const bool reduced = preferences_reduce_motion();
    if (reduced) {
        atlas_yaw_ = atlas_yaw_target_;
        atlas_pitch_ = atlas_pitch_target_;
    } else {
        const float blend = 1.0F - std::exp(-11.0F * seconds);
        atlas_yaw_ = wrap_atlas_angle(
            atlas_yaw_ + wrap_atlas_angle(atlas_yaw_target_ - atlas_yaw_) *
                             blend);
        atlas_pitch_ = wrap_atlas_angle(
            atlas_pitch_ +
            wrap_atlas_angle(atlas_pitch_target_ - atlas_pitch_) * blend);
    }
    atlas_focus_zoom_.tick(dt, reduced);
    atlas_spacing_.tick(dt, reduced);
    atlas_view_scale_.tick(dt, reduced);
    atlas_pan_x_.tick(dt, reduced);
    atlas_pan_y_.tick(dt, reduced);
    atlas_world_center_x_.tick(dt, reduced);
    atlas_world_center_y_.tick(dt, reduced);
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
    poll_gpx_result();
    poll_poi_result();
    poll_elevation_result();

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

    if (mode_ == ScreenMode::Navigation) {
        update_navigation(pressed);
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
    if (mode_ == ScreenMode::Gpx) {
        update_gpx(pressed);
        previous_controls_ = controls;
        return;
    }
    if (mode_ == ScreenMode::OfflineAtlas) {
        update_offline_atlas(dt, controls, pressed);
        previous_controls_ = controls;
        return;
    }
    if (!force_quit && (pressed & SCE_CTRL_START)) {
        request_mode(ScreenMode::Navigation);
        camera_.velocityX = 0.0;
        camera_.velocityY = 0.0;
        reset_touch_state();
        log_printf("main menu opened");
        previous_controls_ = controls;
        return;
    }
    if (pressed & SCE_CTRL_SQUARE) {
        pin_lists_return_mode_ = ScreenMode::Map;
        request_mode(ScreenMode::PinLists);
        selected_list_ = pins_.active_index();
        delete_confirm_armed_ = false;
        pinning_ = false;
        reset_touch_state();
        previous_controls_ = controls;
        return;
    }
    const unsigned int poi_chord = SCE_CTRL_SELECT | SCE_CTRL_TRIANGLE;
    const bool poi_requested = (controls.buttons & poi_chord) == poi_chord &&
                               (pressed & poi_chord) != 0U;
    if (poi_requested) {
        request_pois();
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
        if (settings_result_ == 0)
            settings_result_ = preferences_set_hiking_mode(next == 4);
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
    case SettingRow::Hiking: {
        const bool enabled = !preferences_hiking_mode();
        const int previous = provider_.style_index();
        const int next_style = enabled ? 4 : (previous == 4 ? 0 : previous);
        if (!provider_.set_style(next_style)) {
            settings_result_ = -1;
            break;
        }
        settings_result_ = preferences_set_map_style(next_style);
        if (settings_result_ == 0)
            settings_result_ = preferences_set_hiking_mode(enabled);
        if (settings_result_ < 0) provider_.set_style(previous);
        camera_.min_zoom = provider_.min_zoom();
        camera_.max_zoom = provider_.max_zoom();
        camera_.zoom = std::clamp(camera_.zoom, camera_.min_zoom,
                                  camera_.max_zoom);
        camera_.target_zoom = std::clamp(camera_.target_zoom,
                                         camera_.min_zoom,
                                         camera_.max_zoom);
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
    case SettingRow::MapScale: {
        const bool next = !preferences_scale_always_visible();
        settings_result_ = preferences_set_scale_always_visible(next);
        log_printf("settings: scale always visible=%d", next ? 1 : 0);
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
    if (mode_ == ScreenMode::Map) {
        renderer_.prepare(camera_, viewport_, frame);
    } else if (mode_ == ScreenMode::OfflineAtlas) {
        build_offline_atlas_requests();
        renderer_.prepare_cache_only(atlas_tile_requests_, frame);
    } else {
        manager_.submit_requests({}, frame);
    }
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

void MapScreen::draw_navigation() {
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    for (int x = 360; x < 960; x += 48)
        vita2d_draw_line(static_cast<float>(x), 70.0F,
                         static_cast<float>(x), 478.0F,
                         RGBA8(72, 93, 112, 28));
    for (int y = 86; y < 478; y += 48)
        vita2d_draw_line(340.0F, static_cast<float>(y), 960.0F,
                         static_cast<float>(y), RGBA8(72, 93, 112, 28));
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::MainMenu));
    const int brand_width = ui_font_text_width(font_body_, kFontBody,
                                                "VitaMaps");
    ui_font_draw_text(font_body_, 924 - brand_width, 45, kAccent,
                      kFontBody, "VitaMaps");

    const char *labels[] = {
        ui_text(UiText::Map), ui_text(UiText::OfflineAtlas),
        ui_text(UiText::ListsAndRoutes), ui_text(UiText::Settings)};
    ui_draw_focus_glow(24.0F, navigation_focus_y_.value(), 294.0F,
                       kNavigationRowHeight, kAccent, animation_seconds_,
                       button_feedback_.value());
    for (int row = 0; row < static_cast<int>(NavigationItem::Count); ++row) {
        const float y = navigation_row_y(row);
        const bool selected = row == static_cast<int>(selected_navigation_);
        vita2d_draw_rectangle(24.0F, y, 294.0F, kNavigationRowHeight,
                              selected ? kPanelSelected :
                                         RGBA8(22, 33, 43, 225));
        if (selected)
            vita2d_draw_rectangle(24.0F, y,
                                  5.0F + button_feedback_.value() * 3.0F,
                                  kNavigationRowHeight, kAccent);
        char ordinal[8];
        std::snprintf(ordinal, sizeof(ordinal), "%02d", row + 1);
        ui_font_draw_text(font_small_, 43, static_cast<int>(y) + 37,
                          selected ? kAccent : kSecondaryText,
                          kFontSmall, ordinal);
        char fitted[160];
        ui_font_fit_text(font_body_, kFontBody, labels[row], fitted,
                         sizeof(fitted), 220);
        ui_font_draw_text(font_body_, 86, static_cast<int>(y) + 39,
                          selected ? kPrimaryText : kSecondaryText,
                          kFontBody, fitted);
        vita2d_draw_line(40.0F, y + kNavigationRowHeight,
                         304.0F, y + kNavigationRowHeight,
                         RGBA8(104, 126, 145, 42));
    }

    constexpr float preview_x = 350.0F;
    constexpr float preview_y = 92.0F;
    constexpr float preview_width = 586.0F;
    constexpr float preview_height = 352.0F;
    vita2d_draw_rectangle(preview_x, preview_y, preview_width,
                          preview_height, RGBA8(10, 17, 24, 204));
    vita2d_draw_rectangle(preview_x, preview_y, 4.0F, preview_height,
                          kAccent);
    ui_font_draw_text(font_display_, 382, 133, kPrimaryText, kFontDisplay,
                      labels[static_cast<int>(selected_navigation_)]);

    if (selected_navigation_ == NavigationItem::Map) {
        constexpr float map_left = 405.0F;
        constexpr float map_top = 166.0F;
        constexpr float map_width = 462.0F;
        constexpr float map_height = 210.0F;
        vita2d_draw_rectangle(map_left, map_top, map_width, map_height,
                              RGBA8(24, 42, 52, 255));
        for (int line = 1; line < 6; ++line) {
            const float x = map_left + map_width * line / 6.0F;
            vita2d_draw_line(x, map_top, x, map_top + map_height,
                             RGBA8(88, 190, 255, 65));
        }
        for (int line = 1; line < 4; ++line) {
            const float y = map_top + map_height * line / 4.0F;
            vita2d_draw_line(map_left, y, map_left + map_width, y,
                             RGBA8(88, 190, 255, 65));
        }
        vita2d_draw_line(615.0F, 258.0F, 657.0F, 258.0F, kPrimaryText);
        vita2d_draw_line(636.0F, 237.0F, 636.0F, 279.0F, kPrimaryText);
        vita2d_draw_fill_circle(636.0F, 258.0F, 4.0F, kAccent);
        ui_font_draw_textf(font_small_, 405, 405, kSecondaryText,
                           kFontSmall, "%.5f, %.5f · z%.2f · %s",
                           camera_.latitude, camera_.longitude, camera_.zoom,
                           provider_.name());
    } else if (selected_navigation_ == NavigationItem::OfflineAtlas) {
        const float breathe = preferences_reduce_motion() ? 0.0F :
            std::sin(static_cast<float>(animation_seconds_ * 0.9)) * 4.0F;
        for (int layer = 0; layer < 7; ++layer) {
            const float x = 438.0F + layer * 10.0F;
            const float y = 326.0F - layer * (23.0F + breathe * 0.08F);
            const float skew = 52.0F;
            const unsigned int color = layer == 6 ? kAccent :
                RGBA8(83, 132 + layer * 9, 168 + layer * 7, 145);
            vita2d_draw_line(x, y, x + 330.0F, y - 28.0F, color);
            vita2d_draw_line(x + 330.0F, y - 28.0F,
                             x + 330.0F + skew, y + 44.0F, color);
            vita2d_draw_line(x + 330.0F + skew, y + 44.0F,
                             x + skew, y + 72.0F, color);
            vita2d_draw_line(x + skew, y + 72.0F, x, y, color);
        }
        char cache[96];
        if (cache_status_valid_) {
            char size[32];
            format_cache_size(cache_status_.status.bytes, size, sizeof(size));
            std::snprintf(cache, sizeof(cache), ui_text(UiText::CacheState),
                          size,
                          static_cast<unsigned>(cache_status_.status.entries));
        } else {
            std::snprintf(cache, sizeof(cache), "%s",
                          ui_text(UiText::CacheReading));
        }
        ui_font_draw_text(font_small_, 405, 405, kSecondaryText,
                          kFontSmall, cache);
        const int badge_width = ui_font_text_width(
            font_small_, kFontSmall, ui_text(UiText::CacheOnly));
        vita2d_draw_rectangle(900.0F - badge_width, 112.0F,
                              static_cast<float>(badge_width + 18), 25.0F,
                              RGBA8(34, 74, 88, 230));
        ui_font_draw_text(font_small_, 909 - badge_width, 130, kAccent,
                          kFontSmall, ui_text(UiText::CacheOnly));
    } else if (selected_navigation_ == NavigationItem::PinLists) {
        std::size_t total_pins = 0U;
        for (const PinList &list : pins_.lists()) total_pins += list.pins.size();
        for (std::size_t index = 0;
             index < pins_.lists().size() && index < 5U; ++index) {
            const float y = 174.0F + static_cast<float>(index) * 47.0F;
            vita2d_draw_line(405.0F, y, 850.0F, y,
                             RGBA8(104, 126, 145, 52));
            draw_pin_symbol(427.0F, y + 22.0F, pins_.lists()[index].icon,
                            pin_color(pins_.lists()[index].color, false), 0.72F);
            char fitted[160];
            ui_font_fit_text(font_body_, kFontBody,
                             pins_.lists()[index].name.c_str(), fitted,
                             sizeof(fitted), 320);
            ui_font_draw_text(font_body_, 453, static_cast<int>(y) + 29,
                              kPrimaryText, kFontBody, fitted);
            ui_font_draw_textf(font_small_, 812, static_cast<int>(y) + 27,
                               kSecondaryText, kFontSmall, "%u",
                               static_cast<unsigned>(
                                   pins_.lists()[index].pins.size()));
        }
        ui_font_draw_textf(font_small_, 405, 419, kSecondaryText, kFontSmall,
                           "%u %s · %u pin",
                           static_cast<unsigned>(pins_.lists().size()),
                           ui_text(UiText::Lists),
                           static_cast<unsigned>(total_pins));
    } else {
        const char *categories[] = {
            ui_text(UiText::Map), ui_text(UiText::InterfaceCategory),
            ui_text(UiText::StorageCategory)};
        for (int index = 0; index < 3; ++index) {
            const float y = 180.0F + index * 70.0F;
            ui_font_draw_text(font_small_, 405, static_cast<int>(y) + 5,
                              kAccent, kFontSmall,
                              index == 0 ? "01" : index == 1 ? "02" : "03");
            ui_font_draw_text(font_body_, 458, static_cast<int>(y) + 8,
                              kPrimaryText, kFontBody, categories[index]);
            vita2d_draw_line(405.0F, y + 26.0F, 858.0F, y + 26.0F,
                             RGBA8(104, 126, 145, 52));
        }
    }

    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "UP/DOWN",
                           ui_text(UiText::SelectChange), 0U,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "X", ui_text(UiText::Open),
                           SCE_CTRL_CROSS, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "O", ui_text(UiText::Map),
                           SCE_CTRL_CIRCLE, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "START+SELECT", ui_text(UiText::Quit),
                  0U, last_action_button_, button_feedback_.value(),
                  1.0F, 0.0F);
    draw_message();
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
    const unsigned int connection_color = connected_ ? kSuccess
                                                     : kSecondaryText;
    vita2d_draw_rectangle(802.0F, 7.0F + top_offset, 142.0F, 28.0F,
                          ui_fade_color(connected_ ? RGBA8(24, 66, 65, 238)
                                                   : RGBA8(49, 57, 65, 238),
                                        opacity));
    vita2d_draw_rectangle(802.0F, 7.0F + top_offset, 4.0F, 28.0F,
                          ui_fade_color(connection_color, opacity));
    vita2d_draw_fill_circle(818.0F, 21.0F + top_offset, 4.0F,
                            ui_fade_color(connection_color, opacity));
    ui_font_draw_text(font_small_, 830,
                      static_cast<int>(28.0F + top_offset),
                      ui_fade_color(kPrimaryText, opacity), kFontSmall,
                      ui_text(connected_ ? UiText::Online : UiText::Offline));
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
    hint_x = draw_key_hint(font_small_, hint_x, "START",
                           ui_text(UiText::MainMenu),
                           SCE_CTRL_START, last_action_button_, feedback,
                           opacity, bottom_offset);
    draw_key_hint(font_small_, hint_x, "L/R", ui_text(UiText::Zoom),
                  SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER,
                  last_action_button_, feedback, opacity, bottom_offset);
    draw_key_hint(font_small_, 620, "R-STICK", ui_text(UiText::Rotate),
                  0U, last_action_button_, feedback, opacity,
                  bottom_offset - 52.0F);
    draw_key_hint(font_small_, 430, "SELECT+TRIANGLE", ui_text(UiText::Poi),
                  0U, last_action_button_, feedback, opacity,
                  bottom_offset - 52.0F);
    draw_key_hint(font_small_, 804, "SELECT", ui_text(UiText::North),
                  SCE_CTRL_SELECT, last_action_button_, feedback, opacity,
                  bottom_offset - 52.0F);
}

void MapScreen::draw_attribution() {
    const char *text = provider_.attribution();
    const int width = ui_font_text_width(font_small_, kFontSmall, text);
    const float x = static_cast<float>(std::max(8, 950 - width));
    const float visible = hud_motion_.value();
    const float hidden = 1.0F - visible;

    // With the HUD open, credits live below the top bar instead of sharing
    // the bottom-right key-hint rows. When the HUD disappears, the compact
    // bottom attribution returns and carries the connection state with it.
    if (visible > 0.002F) {
        vita2d_draw_rectangle(x - 6.0F, 44.0F,
                              static_cast<float>(width + 12), 25.0F,
                              ui_fade_color(kHudPanel, visible * 0.90F));
        ui_font_draw_text(font_small_, static_cast<int>(x), 64,
                          ui_fade_color(kPrimaryText, visible), kFontSmall,
                          text);
    }
    if (hidden > 0.002F) {
        const char *state = ui_text(connected_ ? UiText::Online
                                              : UiText::Offline);
        const int state_width = ui_font_text_width(font_small_, kFontSmall,
                                                   state);
        const float state_x = x - static_cast<float>(state_width + 32);
        const unsigned int state_color = connected_ ? kSuccess
                                                    : kSecondaryText;
        vita2d_draw_rectangle(state_x - 6.0F, 518.0F,
                              static_cast<float>(state_width + width + 44),
                              26.0F, ui_fade_color(kHudPanel, hidden));
        vita2d_draw_fill_circle(state_x + 3.0F, 531.0F, 3.5F,
                                ui_fade_color(state_color, hidden));
        ui_font_draw_text(font_small_, static_cast<int>(state_x + 12.0F),
                          538, ui_fade_color(state_color, hidden), kFontSmall,
                          state);
        ui_font_draw_text(font_small_, static_cast<int>(x), 538,
                          ui_fade_color(kPrimaryText, hidden), kFontSmall,
                          text);
    }
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
    const float opacity = scale_motion_.value();
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

    const bool detached = preferences_scale_always_visible();
    if (!detached)
        vita2d_draw_rectangle(12.0F, 460.0F + offset, 166.0F, 46.0F,
                              ui_fade_color(kHudPanel, opacity));
    if (detached) {
        ui_font_draw_text(font_small_, 23, static_cast<int>(482.0F + offset),
                          ui_fade_color(kShadow, opacity), kFontSmall, label);
        vita2d_draw_rectangle(20.0F, 492.0F + offset, pixels + 4.0F, 6.0F,
                              ui_fade_color(kShadow, opacity));
        vita2d_draw_rectangle(20.0F, 487.0F + offset, 6.0F, 13.0F,
                              ui_fade_color(kShadow, opacity));
        vita2d_draw_rectangle(18.0F + pixels, 487.0F + offset, 6.0F, 13.0F,
                              ui_fade_color(kShadow, opacity));
    }
    ui_font_draw_text(font_small_, 22, static_cast<int>(481.0F + offset),
                      ui_fade_color(kPrimaryText, opacity), kFontSmall, label);
    vita2d_draw_rectangle(22.0F, 494.0F + offset, pixels, 2.0F,
                          ui_fade_color(kPrimaryText, opacity));
    vita2d_draw_rectangle(22.0F, 489.0F + offset, 2.0F, 9.0F,
                          ui_fade_color(kPrimaryText, opacity));
    vita2d_draw_rectangle(20.0F + pixels, 489.0F + offset, 2.0F, 9.0F,
                          ui_fade_color(kPrimaryText, opacity));
}

void MapScreen::draw_pois() {
    for (const PointOfInterest &poi : pois_) {
        const auto screen = camera_.geo_to_screen(
            poi.position.latitude, poi.position.longitude, 960.0, 544.0);
        if (screen.x < -20.0 || screen.x > 980.0 ||
            screen.y < -20.0 || screen.y > 564.0)
            continue;
        const float x = static_cast<float>(screen.x);
        const float y = static_cast<float>(screen.y);
        const unsigned int color = poi_color(poi.category);
        vita2d_draw_fill_circle(x, y, 10.0F, kShadow);
        vita2d_draw_fill_circle(x, y, 7.5F, color);
        vita2d_draw_fill_circle(x, y, 3.0F, kSettingsBackground);
        if (poi.category == PoiCategory::Summit)
            draw_pin_symbol(x, y, PinIcon::Summit, color, 0.7F);
        else if (poi.category == PoiCategory::Water)
            draw_pin_symbol(x, y, PinIcon::Water, color, 0.65F);
        else if (poi.category == PoiCategory::Shelter)
            draw_pin_symbol(x, y, PinIcon::Camp, color, 0.65F);
    }
}

void MapScreen::draw_gpx() {
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::GpxTitle));
    if (manager_.gpx_pending())
        ui_font_draw_text(font_small_, 720, 44, kAccent, kFontSmall,
                          ui_text(UiText::CacheReading));

    ui_font_draw_text(font_body_, 48, 96, kPrimaryText, kFontBody,
                      ui_text(UiText::Inbox));
    constexpr std::size_t visible_inbox = 4;
    const std::size_t first = visible_first(selected_gpx_, visible_inbox);
    if (!gpx_inbox_.empty())
        ui_draw_focus_glow(48.0F, gpx_focus_y_.value(),
                           864.0F, 40.0F, kAccent, animation_seconds_,
                           button_feedback_.value());
    for (std::size_t row = 0; row < visible_inbox &&
         first + row < gpx_inbox_.size(); ++row) {
        const GpxInboxEntry &entry = gpx_inbox_[first + row];
        const int y = 108 + static_cast<int>(row) * 48;
        vita2d_draw_rectangle(48.0F, static_cast<float>(y), 864.0F, 40.0F,
                              kPanel);
        char name[180];
        ui_font_fit_text(font_body_, kFontBody, entry.filename.c_str(), name,
                         sizeof(name), 650);
        ui_font_draw_text(font_body_, 66, y + 28, kPrimaryText, kFontBody,
                          name);
        char size[32];
        format_cache_size(entry.bytes, size, sizeof(size));
        const int width = ui_font_text_width(font_small_, kFontSmall, size);
        ui_font_draw_text(font_small_, 890 - width, y + 27,
                          kSecondaryText, kFontSmall, size);
    }
    if (gpx_inbox_.empty())
        ui_font_draw_text(font_small_, 60, 135, kSecondaryText, kFontSmall,
                          VITAMAPS_GPX_INBOX_DIR);

    char history_title[96];
    std::snprintf(history_title, sizeof(history_title), "%s  %u–%u/%u",
                  ui_text(UiText::History),
                  gpx_history_.empty() ? 0U :
                      static_cast<unsigned>(gpx_history_offset_ + 1U),
                  static_cast<unsigned>(std::min(
                      gpx_history_offset_ + 3U, gpx_history_.size())),
                  static_cast<unsigned>(gpx_history_.size()));
    ui_font_draw_text(font_body_, 48, 326, kPrimaryText, kFontBody,
                      history_title);
    for (std::size_t row = 0; row < 3U &&
         gpx_history_offset_ + row < gpx_history_.size(); ++row) {
        const GpxImportRecord &record =
            gpx_history_[gpx_history_offset_ + row];
        const int y = 338 + static_cast<int>(row) * 43;
        vita2d_draw_rectangle(48.0F, static_cast<float>(y), 864.0F, 36.0F,
                              kPanel);
        char details[280];
        std::snprintf(details, sizeof(details), "%s  →  %s  |  %u  |  %s",
                      record.filename.c_str(), record.list_name.c_str(),
                      static_cast<unsigned>(record.points),
                      record.result == 0 ? "OK" : "ERROR");
        char fitted[280];
        ui_font_fit_text(font_small_, kFontSmall, details, fitted,
                         sizeof(fitted), 820);
        ui_font_draw_text(font_small_, 66, y + 25,
                          record.result == 0 ? kSecondaryText : kError,
                          kFontSmall, fitted);
    }

    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "X", ui_text(UiText::Import),
                           SCE_CTRL_CROSS, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SQUARE",
                           ui_text(UiText::Export), SCE_CTRL_SQUARE,
                           last_action_button_, button_feedback_.value(),
                           1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE",
                           ui_text(UiText::Refresh), SCE_CTRL_TRIANGLE,
                           last_action_button_, button_feedback_.value(),
                           1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, 16, "LEFT/RIGHT",
                           ui_text(UiText::History), 0U, last_action_button_,
                           button_feedback_.value(), 1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "O", ui_text(UiText::Lists),
                  SCE_CTRL_CIRCLE, last_action_button_,
                  button_feedback_.value(), 1.0F, 0.0F);
    draw_message();
}

void MapScreen::draw_offline_atlas(std::uint64_t frame) {
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          kSettingsBackground);
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kTopBar);
    vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::OfflineAtlas));
    const char *style_name = provider_.style_name(offline_atlas_style_);
    const int style_width = ui_font_text_width(font_body_, kFontBody,
                                                style_name);
    ui_font_draw_text(font_body_, 924 - style_width, 45, kAccent, kFontBody,
                      style_name);

    if (!offline_atlas_valid_) {
        ui_font_draw_text_centered(font_body_, 480, 285, 760, kSecondaryText,
                                   kFontBody, ui_text(UiText::AtlasLoading));
    } else {
        const std::uint32_t provider_id =
            provider_.style_id(offline_atlas_style_);
        const AtlasGeometry geometry = atlas_geometry(offline_atlas_,
                                                       provider_id);
        const OfflineAtlasLayer *layers[32]{};
        std::size_t layer_count = 0U;
        std::size_t selected_index = 0U;
        for (const OfflineAtlasLayer &layer : offline_atlas_.layers) {
            if (layer.provider != provider_id || layer.tiles == 0U ||
                layer_count >= 32U)
                continue;
            layers[layer_count] = &layer;
            if (layer.zoom == atlas_selected_zoom_)
                selected_index = layer_count;
            ++layer_count;
        }
        if (!geometry.valid || layer_count == 0U) {
            ui_font_draw_text_centered(font_body_, 480, 285, 760,
                                       kSecondaryText, kFontBody,
                                       ui_text(UiText::AtlasEmpty));
        } else {
            // The rail lists only zoom levels that really exist in the cache.
            // Moving between rows therefore snaps to a concrete layer rather
            // than a visually empty integer zoom.
            constexpr std::size_t kVisibleRows = 9U;
            std::size_t first = selected_index < kVisibleRows / 2U
                ? 0U : selected_index - kVisibleRows / 2U;
            if (first + kVisibleRows > layer_count)
                first = layer_count > kVisibleRows
                    ? layer_count - kVisibleRows : 0U;
            vita2d_draw_rectangle(16.0F, 82.0F, 164.0F, 350.0F,
                                  RGBA8(12, 20, 28, 215));
            ui_font_draw_text(font_small_, 28, 105, kSecondaryText,
                              kFontSmall, ui_text(UiText::AtlasLayer));
            float selected_row_y = 118.0F;
            for (std::size_t index = first;
                 index < layer_count && index < first + kVisibleRows;
                 ++index) {
                const float row_y = 116.0F +
                    static_cast<float>(index - first) * 34.0F;
                const bool selected = index == selected_index;
                if (selected) {
                    selected_row_y = row_y;
                    ui_draw_focus_glow(25.0F, row_y, 146.0F, 29.0F,
                                       kAccent, animation_seconds_,
                                       button_feedback_.value(), 1.0F);
                    vita2d_draw_rectangle(25.0F, row_y, 146.0F, 29.0F,
                                          kPanelSelected);
                    vita2d_draw_rectangle(25.0F, row_y, 4.0F, 29.0F,
                                          kAccent);
                }
                char row[64];
                std::snprintf(row, sizeof(row), "z%-2d  %u tile",
                              layers[index]->zoom,
                              static_cast<unsigned>(layers[index]->tiles));
                ui_font_draw_text(font_small_, 36,
                                  static_cast<int>(row_y + 20.0F),
                                  selected ? kPrimaryText : kSecondaryText,
                                  kFontSmall, row);
            }

            const unsigned int layer_colors[] = {
                static_cast<unsigned int>(RGBA8(72, 206, 230, 210)),
                static_cast<unsigned int>(RGBA8(88, 145, 255, 215)),
                static_cast<unsigned int>(RGBA8(177, 126, 255, 215)),
                static_cast<unsigned int>(RGBA8(89, 214, 148, 215))};
            const float projection_world_scale =
                atlas_projection_world_scale(*layers[selected_index],
                                             atlas_layer_browse_);
            const auto project = [&](double world_x, double world_y,
                                     float zoom) {
                return project_atlas_point(
                    projection_world_scale, world_x, world_y,
                    atlas_world_center_x_.value(),
                    atlas_world_center_y_.value(), zoom,
                    atlas_focus_zoom_.value(), atlas_yaw_, atlas_pitch_,
                    atlas_spacing_.value(), atlas_view_scale_.value(),
                    atlas_pan_x_.value(), atlas_pan_y_.value());
            };

            // A center spine makes the varying inter-layer distance explicit.
            AtlasProjectedPoint previous_center{};
            bool have_previous_center = false;
            for (std::size_t index = 0; index < layer_count; ++index) {
                const AtlasProjectedPoint center = project(
                    geometry.center_x, geometry.center_y,
                    static_cast<float>(layers[index]->zoom));
                if (have_previous_center)
                    vita2d_draw_line(previous_center.x, previous_center.y,
                                     center.x, center.y,
                                     RGBA8(112, 137, 158, 125));
                previous_center = center;
                have_previous_center = true;
            }

            // The atlas is not a synthetic coverage diagram: the selected
            // layer and its two neighbours are mosaics of the actual PNGs
            // already present in the persistent cache. No request in this
            // list is allowed to fall through to the network.
            struct AtlasDrawEntry {
                const TileRequest *request{nullptr};
                float depth{0.0F};
            };
            AtlasDrawEntry draw_entries[64]{};
            std::size_t draw_entry_count = 0U;
            std::size_t atlas_ready_textures = 0U;
            for (const TileRequest &request : atlas_tile_requests_) {
                if (draw_entry_count >= 64U) break;
                const double count = std::exp2(
                    static_cast<double>(request.key.zoom));
                const double center_x =
                    (static_cast<double>(request.key.x) + 0.5) / count;
                const double center_y =
                    (static_cast<double>(request.key.y) + 0.5) / count;
                draw_entries[draw_entry_count++] =
                    {&request, project(center_x, center_y,
                                       static_cast<float>(request.key.zoom)).depth};
            }
            std::sort(draw_entries, draw_entries + draw_entry_count,
                      [](const AtlasDrawEntry &left,
                         const AtlasDrawEntry &right) {
                          return left.depth < right.depth;
                      });
            for (std::size_t draw_index = 0;
                 draw_index < draw_entry_count; ++draw_index) {
                const AtlasDrawEntry &entry = draw_entries[draw_index];
                const TileKey &key = entry.request->key;
                const double count = std::exp2(static_cast<double>(key.zoom));
                const double seam = 0.002 / count;
                const double left = static_cast<double>(key.x) / count - seam;
                const double top = static_cast<double>(key.y) / count - seam;
                const double right = static_cast<double>(key.x + 1) / count + seam;
                const double bottom = static_cast<double>(key.y + 1) / count + seam;
                const AtlasProjectedPoint p0 = project(
                    left, top, static_cast<float>(key.zoom));
                const AtlasProjectedPoint p1 = project(
                    right, top, static_cast<float>(key.zoom));
                const AtlasProjectedPoint p2 = project(
                    right, bottom, static_cast<float>(key.zoom));
                const AtlasProjectedPoint p3 = project(
                    left, bottom, static_cast<float>(key.zoom));
                const bool focused_layer = key.zoom == atlas_selected_zoom_;
                if (focused_layer) {
                    const float shadow_x = 4.0F +
                        std::abs(entry.depth) * 0.003F;
                    const float shadow_y = 7.0F +
                        std::abs(entry.depth) * 0.004F;
                    draw_atlas_color_quad(
                        {p0.x + shadow_x, p0.y + shadow_y, p0.depth},
                        {p1.x + shadow_x, p1.y + shadow_y, p1.depth},
                        {p2.x + shadow_x, p2.y + shadow_y, p2.depth},
                        {p3.x + shadow_x, p3.y + shadow_y, p3.depth},
                        RGBA8(3, 8, 12, 125));
                }
                vita2d_texture *texture = renderer_.find_texture(key, frame);
                if (texture) {
                    ++atlas_ready_textures;
                    draw_atlas_texture_quad(
                        texture, p0, p1, p2, p3,
                        focused_layer ? RGBA8(255, 255, 255, 255)
                                      : RGBA8(220, 232, 242, 175));
                } else {
                    draw_atlas_color_quad(
                        p0, p1, p2, p3,
                        focused_layer ? RGBA8(42, 58, 70, 238)
                                      : RGBA8(28, 39, 49, 145));
                }
                vita2d_draw_line(p0.x, p0.y, p1.x, p1.y,
                                 focused_layer ? RGBA8(210, 229, 241, 95)
                                               : RGBA8(126, 151, 170, 50));
                vita2d_draw_line(p1.x, p1.y, p2.x, p2.y,
                                 focused_layer ? RGBA8(210, 229, 241, 95)
                                               : RGBA8(126, 151, 170, 50));
            }

            const auto draw_layer = [&](const OfflineAtlasLayer &layer,
                                        bool selected) {
                const AtlasProjectedPoint p0 = project(
                    layer.minimum_x, layer.minimum_y,
                    static_cast<float>(layer.zoom));
                const AtlasProjectedPoint p1 = project(
                    layer.maximum_x, layer.minimum_y,
                    static_cast<float>(layer.zoom));
                const AtlasProjectedPoint p2 = project(
                    layer.maximum_x, layer.maximum_y,
                    static_cast<float>(layer.zoom));
                const AtlasProjectedPoint p3 = project(
                    layer.minimum_x, layer.maximum_y,
                    static_cast<float>(layer.zoom));
                const int color_index = std::abs(layer.zoom) %
                    static_cast<int>(sizeof(layer_colors) /
                                     sizeof(layer_colors[0]));
                const unsigned int base_color = layer_colors[color_index];
                const unsigned int color = selected ? kPrimaryText
                    : ui_fade_color(base_color, 0.72F);
                if (selected) {
                    vita2d_draw_line(p0.x + 1.0F, p0.y + 1.0F,
                                     p1.x + 1.0F, p1.y + 1.0F, base_color);
                    vita2d_draw_line(p1.x + 1.0F, p1.y + 1.0F,
                                     p2.x + 1.0F, p2.y + 1.0F, base_color);
                    vita2d_draw_line(p2.x + 1.0F, p2.y + 1.0F,
                                     p3.x + 1.0F, p3.y + 1.0F, base_color);
                    vita2d_draw_line(p3.x + 1.0F, p3.y + 1.0F,
                                     p0.x + 1.0F, p0.y + 1.0F, base_color);
                }
                vita2d_draw_line(p0.x, p0.y, p1.x, p1.y, color);
                vita2d_draw_line(p1.x, p1.y, p2.x, p2.y, color);
                vita2d_draw_line(p2.x, p2.y, p3.x, p3.y, color);
                vita2d_draw_line(p3.x, p3.y, p0.x, p0.y, color);
                const std::size_t sample_stride = selected ? 1U :
                    std::max<std::size_t>(
                        1U, (layer.samples.size() + 95U) / 96U);
                for (std::size_t sample_index = 0;
                     !atlas_layer_browse_ && !selected &&
                     sample_index < layer.samples.size();
                     sample_index += sample_stride) {
                    const OfflineAtlasPoint &point =
                        layer.samples[sample_index];
                    const AtlasProjectedPoint sample = project(
                        point.x, point.y, static_cast<float>(layer.zoom));
                    const float size = selected ? 3.6F : 2.5F;
                    vita2d_draw_rectangle(sample.x - size * 0.5F,
                                          sample.y - size * 0.5F,
                                          size, size,
                                          selected ? base_color : color);
                }
            };
            for (std::size_t index = 0; index < layer_count; ++index) {
                const std::size_t distance = index > selected_index
                    ? index - selected_index : selected_index - index;
                if (index != selected_index &&
                    (!atlas_layer_browse_ || distance <= 1U))
                    draw_layer(*layers[index], false);
            }
            draw_layer(*layers[selected_index], true);

            const OfflineAtlasLayer &selected_layer = *layers[selected_index];
            const AtlasProjectedPoint layer_center = project(
                (selected_layer.minimum_x + selected_layer.maximum_x) * 0.5,
                (selected_layer.minimum_y + selected_layer.maximum_y) * 0.5,
                static_cast<float>(selected_layer.zoom));
            vita2d_draw_line(171.0F, selected_row_y + 14.0F,
                             layer_center.x, layer_center.y,
                             RGBA8(88, 190, 255, 150));

            atlas_selected_tile_ = selected_layer.tile_index.empty() ? 0U :
                std::min(atlas_selected_tile_,
                         selected_layer.tile_index.size() - 1U);
            const mercator::WorldPoint selected_world =
                atlas_tile_center(selected_layer, atlas_selected_tile_);
            const double selected_world_x = selected_world.x;
            const double selected_world_y = selected_world.y;
            const std::size_t tile_number = selected_layer.tile_index.empty()
                ? 0U : atlas_selected_tile_ + 1U;
            const AtlasProjectedPoint selected_point = project(
                selected_world_x, selected_world_y,
                static_cast<float>(selected_layer.zoom));
            if (!selected_layer.tile_index.empty()) {
                const OfflineAtlasTile &tile =
                    selected_layer.tile_index[atlas_selected_tile_];
                const double count = std::exp2(
                    static_cast<double>(selected_layer.zoom));
                const AtlasProjectedPoint tile_p0 = project(
                    tile.x / count, tile.y / count,
                    static_cast<float>(selected_layer.zoom));
                const AtlasProjectedPoint tile_p1 = project(
                    (tile.x + 1.0) / count, tile.y / count,
                    static_cast<float>(selected_layer.zoom));
                const AtlasProjectedPoint tile_p2 = project(
                    (tile.x + 1.0) / count, (tile.y + 1.0) / count,
                    static_cast<float>(selected_layer.zoom));
                const AtlasProjectedPoint tile_p3 = project(
                    tile.x / count, (tile.y + 1.0) / count,
                    static_cast<float>(selected_layer.zoom));
                vita2d_draw_line(tile_p0.x, tile_p0.y, tile_p1.x, tile_p1.y,
                                 kAccent);
                vita2d_draw_line(tile_p1.x, tile_p1.y, tile_p2.x, tile_p2.y,
                                 kAccent);
                vita2d_draw_line(tile_p2.x, tile_p2.y, tile_p3.x, tile_p3.y,
                                 kAccent);
                vita2d_draw_line(tile_p3.x, tile_p3.y, tile_p0.x, tile_p0.y,
                                 kAccent);
            }
            const float pulse = 7.0F + (preferences_reduce_motion() ? 0.0F :
                1.5F * std::sin(static_cast<float>(animation_seconds_ * 4.0)));
            vita2d_draw_fill_circle(selected_point.x, selected_point.y,
                                    pulse, kPrimaryText);
            vita2d_draw_fill_circle(selected_point.x, selected_point.y,
                                    pulse - 3.0F, kSettingsBackground);
            vita2d_draw_line(selected_point.x - 11.0F, selected_point.y,
                             selected_point.x + 11.0F, selected_point.y,
                             kAccent);
            vita2d_draw_line(selected_point.x, selected_point.y - 11.0F,
                             selected_point.x, selected_point.y + 11.0F,
                             kAccent);

            // Geographic north/east axes prevent an arbitrary orbit from
            // becoming an abstract stack with no map orientation.
            const double axis_world = 42.0 /
                std::max(1.0F, projection_world_scale *
                                  atlas_view_scale_.value());
            const AtlasProjectedPoint north = project(
                selected_world_x, selected_world_y - axis_world,
                static_cast<float>(selected_layer.zoom));
            const AtlasProjectedPoint east = project(
                selected_world_x + axis_world, selected_world_y,
                static_cast<float>(selected_layer.zoom));
            vita2d_draw_line(selected_point.x, selected_point.y,
                             north.x, north.y, kPrimaryText);
            vita2d_draw_line(selected_point.x, selected_point.y,
                             east.x, east.y, kAccent);
            ui_font_draw_text(font_small_, static_cast<int>(north.x + 3.0F),
                              static_cast<int>(north.y), kPrimaryText,
                              kFontSmall, "N");
            ui_font_draw_text(font_small_, static_cast<int>(east.x + 3.0F),
                              static_cast<int>(east.y), kAccent,
                              kFontSmall, "E");

            const mercator::GeoPoint geographic =
                mercator::world_to_lat_lon(selected_world_x, selected_world_y);
            vita2d_draw_rectangle(650.0F, 82.0F, 286.0F, 107.0F,
                                  RGBA8(12, 20, 28, 220));
            const int tile_x = selected_layer.tile_index.empty() ? 0 :
                selected_layer.tile_index[atlas_selected_tile_].x;
            const int tile_y = selected_layer.tile_index.empty() ? 0 :
                selected_layer.tile_index[atlas_selected_tile_].y;
            ui_font_draw_textf(
                font_body_, 666, 108, kPrimaryText, kFontBody,
                "%s · z%d x%d y%d",
                ui_text(atlas_layer_browse_ ? UiText::AtlasLayerBrowse
                                            : UiText::AtlasOverview),
                selected_layer.zoom, tile_x, tile_y);
            ui_font_draw_textf(font_small_, 666, 132, kSecondaryText,
                               kFontSmall, "%.5f, %.5f",
                               geographic.latitude, geographic.longitude);
            const float yaw_degrees = std::fmod(
                atlas_yaw_ * 180.0F / static_cast<float>(kPi) + 360.0F,
                360.0F);
            const float pitch_degrees = std::fmod(
                atlas_pitch_ * 180.0F / static_cast<float>(kPi) + 360.0F,
                360.0F);
            ui_font_draw_textf(font_small_, 666, 151, kSecondaryText,
                               kFontSmall, "%s %.0f px · X %.0f° Y %.0f°",
                               ui_text(UiText::AtlasSpacing),
                               atlas_spacing_.value(), yaw_degrees,
                               pitch_degrees);
            ui_font_draw_textf(font_small_, 666, 173, kAccent, kFontSmall,
                               "%s %.2fx · %u/%u · %s",
                               ui_text(UiText::AtlasViewZoom),
                               atlas_view_scale_.value(),
                               static_cast<unsigned>(tile_number),
                               static_cast<unsigned>(selected_layer.tile_index.size()),
                               ui_text(UiText::CacheOnly));

            char size[32];
            format_cache_size(geometry.bytes, size, sizeof(size));
            char summary[160];
            std::snprintf(summary, sizeof(summary),
                          "%s · %u tile · %u %s · z%d–z%d · GPU %u/%u",
                          size, static_cast<unsigned>(geometry.tiles),
                          static_cast<unsigned>(geometry.layers),
                          ui_text(UiText::AtlasLayer), geometry.minimum_zoom,
                          geometry.maximum_zoom,
                          static_cast<unsigned>(atlas_ready_textures),
                          static_cast<unsigned>(draw_entry_count));
            ui_font_draw_text(font_small_, 36, 455, kSecondaryText,
                              kFontSmall, summary);
        }
    }
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    int hint_x = 16;
    if (atlas_layer_browse_) {
        hint_x = draw_key_hint(font_small_, hint_x, "D-PAD",
                               ui_text(UiText::AtlasLayerBrowse), 0U,
                               last_action_button_, button_feedback_.value(),
                               1.0F, -28.0F);
    } else {
        hint_x = draw_key_hint(font_small_, hint_x, "UP/DOWN",
                               ui_text(UiText::AtlasLayer),
                               SCE_CTRL_UP | SCE_CTRL_DOWN,
                               last_action_button_, button_feedback_.value(),
                               1.0F, -28.0F);
        hint_x = draw_key_hint(font_small_, hint_x, "LEFT/RIGHT",
                               ui_text(UiText::AtlasSpacing),
                               SCE_CTRL_LEFT | SCE_CTRL_RIGHT,
                               last_action_button_, button_feedback_.value(),
                               1.0F, -28.0F);
        hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE",
                               ui_text(UiText::MapStyle), SCE_CTRL_TRIANGLE,
                               last_action_button_, button_feedback_.value(),
                               1.0F, -28.0F);
    }
    hint_x = draw_key_hint(font_small_, hint_x, "L-STICK",
                           ui_text(UiText::AtlasPan), 0U, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "R-STICK",
                           ui_text(UiText::Rotate), 0U, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    draw_key_hint(font_small_, hint_x, "START", ui_text(UiText::Refresh),
                  SCE_CTRL_START, last_action_button_,
                  button_feedback_.value(), 1.0F, -28.0F);
    hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "L/R",
                           ui_text(UiText::AtlasViewZoom),
                           SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SQUARE",
                           ui_text(atlas_layer_browse_
                                       ? UiText::AtlasLeaveLayer
                                       : UiText::AtlasEnterLayer),
                           SCE_CTRL_SQUARE,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "X",
                           ui_text(UiText::AtlasOpenMap), SCE_CTRL_CROSS,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "SELECT",
                           ui_text(UiText::AtlasResetView), SCE_CTRL_SELECT,
                           last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    draw_key_hint(font_small_, hint_x, "O",
                  ui_text(atlas_layer_browse_ ? UiText::AtlasLeaveLayer
                                              : UiText::Back),
                  SCE_CTRL_CIRCLE, last_action_button_,
                  button_feedback_.value(), 1.0F, 0.0F);
    draw_message();
}

void MapScreen::draw_pin_overlays() {
    const auto &lists = pins_.lists();
    const bool imperial = preferences_imperial_units();
    for (std::size_t list_index = 0; list_index < lists.size(); ++list_index) {
        const auto &list = lists[list_index];
        if (!list.visible) continue;
        const bool active = list_index == pins_.active_index();
        const unsigned int route_color = pin_color(list.color, !active);
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
            draw_pin_symbol(x, y - 1.0F, list.icon, route_color,
                            active ? 1.0F : 0.82F);
            if (active && index < 99) {
                char number[4];
                std::snprintf(number, sizeof(number), "%u",
                              static_cast<unsigned>(index + 1));
                ui_font_draw_text(font_small_, static_cast<int>(x) + 10,
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
                       pin_color(lists[selected_list_].color),
                       animation_seconds_, button_feedback_.value());
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
        draw_pin_symbol(474.0F, static_cast<float>(y + 28),
                        lists[index].icon, pin_color(lists[index].color),
                        0.82F);
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
                          56.0F, pin_color(lists[selected_list_].color));
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);
    int hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "X", ui_text(UiText::Open),
                           SCE_CTRL_CROSS,
                           last_action_button_, button_feedback_.value(), 1.0F,
                           -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "R", ui_text(UiText::ShowHide),
                           SCE_CTRL_RTRIGGER, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "L", "GPX",
                           SCE_CTRL_LTRIGGER, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "TRIANGLE", ui_text(UiText::New),
                           SCE_CTRL_TRIANGLE, last_action_button_,
                           button_feedback_.value(), 1.0F, -28.0F);
    hint_x = 16;
    hint_x = draw_key_hint(font_small_, hint_x, "LEFT", ui_text(UiText::Color),
                           0U, last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
    hint_x = draw_key_hint(font_small_, hint_x, "RIGHT", ui_text(UiText::Icon),
                           0U, last_action_button_, button_feedback_.value(),
                           1.0F, 0.0F);
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
    std::size_t elevation_count = 0;
    double minimum_elevation = 0.0;
    double maximum_elevation = 0.0;
    double ascent = 0.0;
    double descent = 0.0;
    double previous_elevation = 0.0;
    bool open_meteo_elevation = false;
    for (const MapPin &pin : list.pins) {
        if (!pin.has_elevation) continue;
        if (elevation_count == 0) {
            minimum_elevation = maximum_elevation = pin.elevation_meters;
        } else {
            minimum_elevation = std::min(minimum_elevation,
                                         pin.elevation_meters);
            maximum_elevation = std::max(maximum_elevation,
                                         pin.elevation_meters);
            const double delta = pin.elevation_meters - previous_elevation;
            if (delta > 0.0) ascent += delta;
            else descent -= delta;
        }
        previous_elevation = pin.elevation_meters;
        open_meteo_elevation = open_meteo_elevation ||
            pin.elevation_source == ElevationSource::OpenMeteo;
        ++elevation_count;
    }
    if (preferences_hiking_mode()) {
        if (elevation_count > 0) {
            char elevation_summary[256];
            std::snprintf(elevation_summary, sizeof(elevation_summary),
                          "%s  %.0f–%.0f m  +%.0f/−%.0f m",
                          open_meteo_elevation
                              ? ui_text(UiText::ElevationSource) : "GPX",
                          minimum_elevation,
                          maximum_elevation, ascent, descent);
            ui_font_draw_text(font_small_, 36, 123, kSecondaryText,
                              kFontSmall, elevation_summary);
            if (elevation_count >= 2) {
                constexpr float profile_x = 710.0F;
                constexpr float profile_y = 77.0F;
                constexpr float profile_w = 200.0F;
                constexpr float profile_h = 44.0F;
                vita2d_draw_rectangle(profile_x, profile_y, profile_w,
                                      profile_h, kHudPanel);
                const double range = std::max(1.0, maximum_elevation -
                                                   minimum_elevation);
                std::size_t ordinal = 0;
                float previous_x = profile_x;
                float previous_y = profile_y + profile_h;
                for (const MapPin &pin : list.pins) {
                    if (!pin.has_elevation) continue;
                    const float x = profile_x + profile_w *
                        static_cast<float>(ordinal) /
                        static_cast<float>(elevation_count - 1U);
                    const float y = profile_y + profile_h - 4.0F -
                        (profile_h - 8.0F) * static_cast<float>(
                            (pin.elevation_meters - minimum_elevation) / range);
                    if (ordinal > 0)
                        vita2d_draw_line(previous_x, previous_y, x, y, kAccent);
                    previous_x = x;
                    previous_y = y;
                    ++ordinal;
                }
            }
        } else {
            ui_font_draw_text(font_small_, 36, 123, kSecondaryText,
                              kFontSmall, ui_text(UiText::ElevationSource));
        }
    } else {
        ui_font_draw_text(font_small_, 36, 123, kSecondaryText, kFontSmall,
                          ui_text(UiText::RoutingUnavailable));
    }

    constexpr std::size_t visible_rows = 6;
    const std::size_t first = visible_first(selected_pin_, visible_rows);
    if (!list.pins.empty())
        ui_draw_focus_glow(48.0F, pin_focus_y_.value(), 864.0F, 48.0F,
                           pin_color(list.color), animation_seconds_,
                           button_feedback_.value());
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
        if (list.pins[index].has_elevation)
            std::snprintf(details, sizeof(details),
                          "%s | %.0f m | %.5f, %.5f", segment,
                          list.pins[index].elevation_meters,
                          list.pins[index].position.latitude,
                          list.pins[index].position.longitude);
        else
            std::snprintf(details, sizeof(details), "%s | %.5f, %.5f", segment,
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
                              48.0F, pin_color(list.color));
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
    vita2d_draw_rectangle(0.0F, 478.0F, 960.0F, 66.0F, kBottomBar);

    ui_font_draw_text(font_display_, 36, 48, kPrimaryText, kFontDisplay,
                      ui_text(UiText::SettingsTitle));
    const char *category_labels[] = {
        ui_text(UiText::Map), ui_text(UiText::InterfaceCategory),
        ui_text(UiText::StorageCategory)};
    constexpr float category_x[] = {54.0F, 338.0F, 622.0F};
    for (int category = 0;
         category < static_cast<int>(SettingCategory::Count); ++category) {
        const bool selected =
            category == static_cast<int>(selected_setting_category_);
        vita2d_draw_rectangle(category_x[category], 82.0F, 268.0F, 42.0F,
                              selected ? kPanelSelected : kPanel);
        if (selected)
            vita2d_draw_rectangle(category_x[category], 120.0F,
                                  268.0F, 4.0F, kAccent);
        char fitted[128];
        ui_font_fit_text(font_body_, kFontBody, category_labels[category],
                         fitted, sizeof(fitted), 232);
        ui_font_draw_text(font_body_, static_cast<int>(category_x[category]) + 18,
                          111, selected ? kPrimaryText : kSecondaryText,
                          kFontBody, fitted);
    }

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

    const auto setting_label = [&](SettingRow setting) -> const char * {
        switch (setting) {
        case SettingRow::MapStyle: return ui_text(UiText::MapStyle);
        case SettingRow::Hiking: return ui_text(UiText::HikingMode);
        case SettingRow::Language: return ui_text(UiText::Language);
        case SettingRow::Cache: return ui_text(UiText::CacheMaps);
        case SettingRow::HudBehavior: return ui_text(UiText::HudBehavior);
        case SettingRow::MapScale: return ui_text(UiText::MapScale);
        case SettingRow::Crosshair: return ui_text(UiText::Crosshair);
        case SettingRow::Units: return ui_text(UiText::Units);
        case SettingRow::Animations: return ui_text(UiText::Animations);
        case SettingRow::DiskLogging: return ui_text(UiText::PersistentLogs);
        case SettingRow::Count: return "";
        }
        return "";
    };
    const auto setting_value = [&](SettingRow setting) -> const char * {
        switch (setting) {
        case SettingRow::MapStyle: return provider_.name();
        case SettingRow::Hiking:
            return ui_text(preferences_hiking_mode() ? UiText::Enabled
                                                      : UiText::Disabled);
        case SettingRow::Language:
            return ui_language_name(preferences_ui_language());
        case SettingRow::Cache: return cache_value;
        case SettingRow::HudBehavior:
            return ui_text(preferences_hud_auto_hide() ? UiText::HudAuto
                                                        : UiText::AlwaysVisible);
        case SettingRow::MapScale:
            return ui_text(preferences_scale_always_visible()
                               ? UiText::AlwaysVisible : UiText::ScaleWithHud);
        case SettingRow::Crosshair:
            return ui_text(preferences_crosshair_enabled() ? UiText::Visible
                                                            : UiText::Hidden);
        case SettingRow::Units:
            return ui_text(preferences_imperial_units() ? UiText::Imperial
                                                         : UiText::Metric);
        case SettingRow::Animations:
            return ui_text(preferences_reduce_motion() ? UiText::Reduced
                                                        : UiText::Smooth);
        case SettingRow::DiskLogging:
            return ui_text(preferences_disk_logs_enabled() ? UiText::Enabled
                                                            : UiText::Disabled);
        case SettingRow::Count: return "";
        }
        return "";
    };

    const int row_count = setting_category_row_count();
    for (int row = 0; row < row_count; ++row)
        vita2d_draw_rectangle(54.0F, settings_row_y(row), 852.0F,
                              kSettingsRowHeight, kPanel);
    ui_draw_focus_glow(54.0F, settings_focus_y_.value(), 852.0F,
                       kSettingsRowHeight, kAccent, animation_seconds_,
                       button_feedback_.value());
    vita2d_draw_rectangle(54.0F, settings_focus_y_.value(), 852.0F,
                          kSettingsRowHeight, kPanelSelected);
    vita2d_draw_rectangle(54.0F, settings_focus_y_.value(),
                          5.0F + button_feedback_.value() * 3.0F,
                          kSettingsRowHeight, kAccent);

    for (int row = 0; row < row_count; ++row) {
        const SettingRow setting = setting_category_row(row);
        const char *label = setting_label(setting);
        const char *value = setting_value(setting);
        char fitted_label[192];
        char fitted_value[192];
        ui_font_fit_text(font_body_, kFontBody, label, fitted_label,
                         sizeof(fitted_label), 500);
        ui_font_fit_text(font_small_, kFontSmall, value, fitted_value,
                         sizeof(fitted_value), 280);
        ui_font_draw_text(font_body_, 78, static_cast<int>(settings_row_y(row)) + 30,
                          kPrimaryText,
                          kFontBody, fitted_label);
        const int width = ui_font_text_width(font_small_, kFontSmall,
                                             fitted_value);
        ui_font_draw_text(font_small_, 876 - width,
                          static_cast<int>(settings_row_y(row)) + 29,
                          setting == selected_setting_
                              ? kAccent : kSecondaryText,
                          kFontSmall, fitted_value);
    }

    if (selected_setting_category_ == SettingCategory::Storage) {
        vita2d_draw_rectangle(54.0F, 270.0F, 852.0F, 106.0F, kPanel);
        vita2d_draw_rectangle(54.0F, 270.0F, 5.0F, 106.0F, kAccent);
        ui_font_draw_text(font_small_, 78, 299, kAccent, kFontSmall,
                          "APPLICATION ICON");
        ui_font_draw_text(font_body_, 78, 331, kPrimaryText, kFontBody,
                          "Apollo 17 Blue Marble (AS17-148-22727)");
        ui_font_draw_text(font_small_, 78, 358, kSecondaryText, kFontSmall,
                          "Image credit: NASA Johnson Space Center");
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
    hint_x = draw_key_hint(font_small_, hint_x, "UP/DOWN",
                           ui_text(UiText::SelectChange),
                           0U, last_action_button_, button_feedback_.value(),
                           1.0F, -28.0F);
    hint_x = draw_key_hint(
        font_small_, hint_x, "L/R",
        category_labels[static_cast<int>(selected_setting_category_)],
        SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER, last_action_button_,
        button_feedback_.value(), 1.0F, -28.0F);
    hint_x = 16;
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
    if (mode_ == ScreenMode::Navigation) {
        draw_navigation();
    } else if (mode_ == ScreenMode::Settings) {
        draw_settings();
    } else if (mode_ == ScreenMode::PinLists) {
        draw_pin_lists();
    } else if (mode_ == ScreenMode::PinList) {
        draw_pin_list();
    } else if (mode_ == ScreenMode::Gpx) {
        draw_gpx();
    } else if (mode_ == ScreenMode::OfflineAtlas) {
        draw_offline_atlas(frame);
    } else {
        renderer_.draw(camera_, viewport_, frame);
        draw_pois();
        draw_pin_overlays();
        draw_crosshair();
        if (hud_motion_.value() > 0.002F)
            draw_chrome();
        draw_scale();
        draw_attribution();
        draw_message();
    }
    draw_transition_veil();
}

} // namespace vitamaps
