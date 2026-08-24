#include "render/map_renderer.h"

#include "core/log.h"
#include "map/mercator.h"

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace vitamaps {
namespace {
constexpr unsigned int kPlaceholderA = RGBA8(35, 43, 52, 255);
constexpr double kPi = 3.14159265358979323846;

void draw_rotated_quad(float center_x, float center_y, float width,
                       float height, float rotation, unsigned int color) {
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    constexpr float z = 0.5F;
    vita2d_color_vertex vertices[4];
    const float local_x[4] = {-half_width, half_width,
                              -half_width, half_width};
    const float local_y[4] = {-half_height, -half_height,
                              half_height, half_height};
    for (int index = 0; index < 4; ++index) {
        vertices[index].x = center_x + local_x[index] * cosine -
                            local_y[index] * sine;
        vertices[index].y = center_y + local_x[index] * sine +
                            local_y[index] * cosine;
        vertices[index].z = z;
        vertices[index].color = color;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, vertices, 4);
}

void rotate_offset(float dx, float dy, float rotation, float &x, float &y) {
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    x = dx * cosine - dy * sine;
    y = dx * sine + dy * cosine;
}

void draw_tile_loader(float center_x, float center_y, float width, float height,
                      int leading) {
    const float radius = std::clamp(
        std::min(width, height) * 0.09F, 10.0F, 19.0F);
    constexpr int dots = 10;
    constexpr float unit_x[dots] = {
        1.0F, 0.809017F, 0.309017F, -0.309017F, -0.809017F,
        -1.0F, -0.809017F, -0.309017F, 0.309017F, 0.809017F};
    constexpr float unit_y[dots] = {
        0.0F, 0.587785F, 0.951057F, 0.951057F, 0.587785F,
        0.0F, -0.587785F, -0.951057F, -0.951057F, -0.587785F};
    for (int index = 0; index < dots; ++index) {
        const int distance = (index - leading + dots) % dots;
        const int alpha = std::max(38, 235 - distance * 21);
        const float dot_x = center_x + radius * unit_x[index];
        const float dot_y = center_y + radius * unit_y[index];
        const float dot_radius = distance == 0 ? 3.4F : 2.5F;
        vita2d_draw_fill_circle(dot_x, dot_y, dot_radius,
                                RGBA8(88, 190, 255, alpha));
    }
    vita2d_draw_fill_circle(center_x, center_y, 2.0F,
                            RGBA8(242, 246, 250, 190));
}
}

MapRenderer::MapRenderer(const MapProvider &provider, TileManager &manager)
    : provider_(provider), manager_(manager) {}

std::vector<TileRequest> MapRenderer::build_requests(
    const MapCamera &camera, const MapViewport &viewport) const {
    const auto center = camera.world();
    const double level_size = mercator::world_size(tile_zoom_);
    const double scale = std::exp2(camera.zoom - tile_zoom_);
    const double center_x = center.x * level_size;
    const double center_y = center.y * level_size;
    const double rotation = camera.bearing * kPi / 180.0;
    const double cosine = std::abs(std::cos(rotation));
    const double sine = std::abs(std::sin(rotation));
    const double half_width =
        (cosine * viewport.width + sine * viewport.height) / (2.0 * scale);
    const double half_height =
        (sine * viewport.width + cosine * viewport.height) / (2.0 * scale);
    const double left = center_x - half_width;
    const double top = center_y - half_height;
    const double right = center_x + half_width;
    const double bottom = center_y + half_height;
    const int minimum_x = static_cast<int>(std::floor(left / kTileSize)) - 1;
    const int maximum_x = static_cast<int>(std::floor((right - 0.001) /
                                                       kTileSize)) + 1;
    const int minimum_y = static_cast<int>(std::floor(top / kTileSize)) - 1;
    const int maximum_y = static_cast<int>(std::floor((bottom - 0.001) /
                                                       kTileSize)) + 1;
    const int count = 1 << tile_zoom_;

    std::unordered_map<TileKey, TileRequest, TileKeyHash> unique;
    for (int raw_y = minimum_y; raw_y <= maximum_y; ++raw_y) {
        if (raw_y < 0 || raw_y >= count) continue;
        for (int raw_x = minimum_x; raw_x <= maximum_x; ++raw_x) {
            const double local_x =
                (raw_x * kTileSize + kTileSize * 0.5 - center_x) * scale;
            const double local_y =
                (raw_y * kTileSize + kTileSize * 0.5 - center_y) * scale;
            const double rotation_cosine = std::cos(rotation);
            const double rotation_sine = std::sin(rotation);
            const double screen_x = viewport.x + viewport.width * 0.5 +
                rotation_cosine * local_x - rotation_sine * local_y;
            const double screen_y = viewport.y + viewport.height * 0.5 +
                rotation_sine * local_x + rotation_cosine * local_y;
            const double tile_half = kTileSize * scale * 0.5;
            const double extent_x =
                (std::abs(rotation_cosine) + std::abs(rotation_sine)) *
                tile_half;
            const double extent_y = extent_x;
            const bool visible =
                screen_x + extent_x >= viewport.x &&
                screen_x - extent_x <= viewport.x + viewport.width &&
                screen_y + extent_y >= viewport.y &&
                screen_y - extent_y <= viewport.y + viewport.height;
            const double dx = screen_x - (viewport.x + viewport.width * 0.5);
            const double dy = screen_y - (viewport.y + viewport.height * 0.5);
            float priority = static_cast<float>(dx * dx + dy * dy) * 0.001F;
            if (!visible) priority += 5000.0F;
            const double directional = dx * camera.velocityX + dy * camera.velocityY;
            priority -= static_cast<float>(std::clamp(directional * 0.002,
                                                      -800.0, 800.0));
            TileRequest request{{provider_.id(), tile_zoom_,
                                 wrap_tile_x(raw_x, tile_zoom_), raw_y},
                                priority, visible};
            const auto found = unique.find(request.key);
            if (found == unique.end() || priority < found->second.priority)
                unique[request.key] = request;
        }
    }
    std::vector<TileRequest> requests;
    requests.reserve(unique.size());
    for (const auto &item : unique) requests.push_back(item.second);
    return requests;
}

void MapRenderer::pump_uploads(std::uint64_t frame, int maximum) {
    for (int index = 0; index < maximum; ++index) {
        DecodedTile tile;
        if (!manager_.take_decoded(tile)) break;
        std::vector<TileKey> evicted;
        if (textures_.insert(tile, frame, evicted)) {
            manager_.mark_ready(tile.key);
        } else {
            log_printf("GPU texture upload failed z=%d x=%d y=%d bytes=%u",
                       tile.key.zoom, tile.key.x, tile.key.y,
                       static_cast<unsigned>(tile.rgba.size()));
            manager_.mark_evicted(tile.key);
        }
        for (const auto &key : evicted) manager_.mark_evicted(key);
    }
}

void MapRenderer::prepare(const MapCamera &camera,
                          const MapViewport &viewport, std::uint64_t frame) {
    const int base = static_cast<int>(std::floor(camera.zoom));
    // Keep the current raster level for the whole continuous transition.
    // Switching to base + 1 just after an integer made its baked-in labels
    // shrink to almost 50% on the Vita screen. At the next integer the new
    // level replaces the scaled parent, while the existing fallback path
    // keeps the transition populated until those textures are ready.
    tile_zoom_ = base;
    tile_zoom_ = std::clamp(tile_zoom_, provider_.min_zoom(),
                            provider_.max_zoom());
    const std::vector<TileRequest> requests = build_requests(camera, viewport);
    manager_.submit_requests(requests, frame);
    // Protect textures needed by this frame before upload-time LRU eviction.
    for (const auto &request : requests) {
        if (!request.visible) continue;
        textures_.find(request.key, frame);
        for (int levels = 1;
             levels <= 3 && request.key.zoom - levels >= provider_.min_zoom();
             ++levels) {
            const int divisor = 1 << levels;
            textures_.find({request.key.provider, request.key.zoom - levels,
                            request.key.x / divisor, request.key.y / divisor},
                           frame);
        }
        if (request.key.zoom < provider_.max_zoom()) {
            for (int child_y = 0; child_y < 2; ++child_y) {
                for (int child_x = 0; child_x < 2; ++child_x) {
                    textures_.find(
                        {request.key.provider, request.key.zoom + 1,
                         request.key.x * 2 + child_x,
                         request.key.y * 2 + child_y}, frame);
                }
            }
        }
    }
    pump_uploads(frame, 2);
}

bool MapRenderer::draw_parent_fallback(const TileKey &key, float center_x,
                                       float center_y, float width,
                                       float height, float rotation,
                                       std::uint64_t frame) {
    for (int levels = 1; levels <= 3 && key.zoom - levels >= provider_.min_zoom();
         ++levels) {
        const int divisor = 1 << levels;
        const TileKey parent{key.provider, key.zoom - levels,
                             key.x / divisor, key.y / divisor};
        vita2d_texture *texture = textures_.find(parent, frame);
        if (!texture) continue;
        const float source_size = static_cast<float>(kTileSize) / divisor;
        const float source_x = (key.x % divisor) * source_size;
        const float source_y = (key.y % divisor) * source_size;
        vita2d_draw_texture_part_scale_rotate(
            texture, center_x, center_y, source_x, source_y,
            source_size, source_size, width / source_size,
            height / source_size, rotation);
        return true;
    }
    return false;
}

bool MapRenderer::draw_child_fallback(const TileKey &key, float center_x,
                                      float center_y, float width,
                                      float height, float rotation,
                                      std::uint64_t frame) {
    if (key.zoom >= provider_.max_zoom()) return false;
    bool drew_any = false;
    const float child_width = width * 0.5F;
    const float child_height = height * 0.5F;
    for (int child_y = 0; child_y < 2; ++child_y) {
        for (int child_x = 0; child_x < 2; ++child_x) {
            const TileKey child{key.provider, key.zoom + 1,
                                key.x * 2 + child_x,
                                key.y * 2 + child_y};
            vita2d_texture *texture = textures_.find(child, frame);
            if (!texture) continue;
            const float local_x = (child_x == 0 ? -1.0F : 1.0F) *
                                  width * 0.25F;
            const float local_y = (child_y == 0 ? -1.0F : 1.0F) *
                                  height * 0.25F;
            float offset_x = 0.0F;
            float offset_y = 0.0F;
            rotate_offset(local_x, local_y, rotation, offset_x, offset_y);
            vita2d_draw_texture_part_scale_rotate(
                texture, center_x + offset_x, center_y + offset_y,
                0.0F, 0.0F, static_cast<float>(kTileSize),
                static_cast<float>(kTileSize),
                child_width / static_cast<float>(kTileSize),
                child_height / static_cast<float>(kTileSize), rotation);
            drew_any = true;
        }
    }
    return drew_any;
}

void MapRenderer::draw(const MapCamera &camera, const MapViewport &viewport,
                       std::uint64_t frame) {
    const auto center = camera.world();
    const double level_size = mercator::world_size(tile_zoom_);
    const double scale = std::exp2(camera.zoom - tile_zoom_);
    const double center_x = center.x * level_size;
    const double center_y = center.y * level_size;
    const float rotation = static_cast<float>(camera.bearing * kPi / 180.0);
    const double cosine = std::abs(std::cos(rotation));
    const double sine = std::abs(std::sin(rotation));
    const double half_width =
        (cosine * viewport.width + sine * viewport.height) / (2.0 * scale);
    const double half_height =
        (sine * viewport.width + cosine * viewport.height) / (2.0 * scale);
    const double left = center_x - half_width;
    const double top = center_y - half_height;
    const double right = center_x + half_width;
    const double bottom = center_y + half_height;
    const int minimum_x = static_cast<int>(std::floor(left / kTileSize));
    const int maximum_x = static_cast<int>(std::floor((right - 0.001) /
                                                       kTileSize));
    const int minimum_y = static_cast<int>(std::floor(top / kTileSize));
    const int maximum_y = static_cast<int>(std::floor((bottom - 0.001) /
                                                       kTileSize));
    const int count = 1 << tile_zoom_;
    const int loader_leading = static_cast<int>(
        (sceKernelGetProcessTimeWide() / 125000ULL) % 10ULL);
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(static_cast<int>(viewport.x),
                              static_cast<int>(viewport.y),
                              static_cast<int>(viewport.x + viewport.width),
                              static_cast<int>(viewport.y + viewport.height));
    for (int raw_y = minimum_y; raw_y <= maximum_y; ++raw_y) {
        for (int raw_x = minimum_x; raw_x <= maximum_x; ++raw_x) {
            const float local_x = static_cast<float>(
                (raw_x * kTileSize + kTileSize * 0.5 - center_x) * scale);
            const float local_y = static_cast<float>(
                (raw_y * kTileSize + kTileSize * 0.5 - center_y) * scale);
            float rotated_x = 0.0F;
            float rotated_y = 0.0F;
            rotate_offset(local_x, local_y, rotation, rotated_x, rotated_y);
            const float tile_center_x = viewport.x + viewport.width * 0.5F +
                                        rotated_x;
            const float tile_center_y = viewport.y + viewport.height * 0.5F +
                                        rotated_y;
            // Slightly overlap adjacent rotated quads. This preserves the
            // existing seam protection under fractional scale without
            // snapping independently rotated corners to different pixels.
            const float drawn_width =
                std::max(1.0F, static_cast<float>(kTileSize * scale) + 1.5F);
            const float drawn_height = drawn_width;
            if (raw_y < 0 || raw_y >= count) {
                draw_rotated_quad(tile_center_x, tile_center_y, drawn_width,
                                  drawn_height, rotation, kPlaceholderA);
                continue;
            }
            const TileKey key{provider_.id(), tile_zoom_,
                              wrap_tile_x(raw_x, tile_zoom_), raw_y};
            vita2d_texture *texture = textures_.find(key, frame);
            if (texture) {
                vita2d_draw_texture_part_scale_rotate(
                    texture, tile_center_x, tile_center_y, 0.0F, 0.0F,
                    static_cast<float>(kTileSize),
                    static_cast<float>(kTileSize),
                    drawn_width / static_cast<float>(kTileSize),
                    drawn_height / static_cast<float>(kTileSize), rotation);
            } else if (!draw_parent_fallback(
                           key, tile_center_x, tile_center_y, drawn_width,
                           drawn_height, rotation, frame)) {
                draw_rotated_quad(tile_center_x, tile_center_y, drawn_width,
                                  drawn_height, rotation, kPlaceholderA);
                draw_child_fallback(key, tile_center_x, tile_center_y,
                                    drawn_width, drawn_height, rotation, frame);
                draw_tile_loader(tile_center_x, tile_center_y,
                                 drawn_width, drawn_height,
                                 loader_leading);
            }
        }
    }
    vita2d_disable_clipping();
}

} // namespace vitamaps
