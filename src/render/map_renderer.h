#pragma once

#include "map/map_camera.h"
#include "map/tile_manager.h"
#include "providers/map_provider.h"
#include "render/texture_cache.h"

#include <cstdint>
#include <vector>

namespace vitamaps {

struct MapViewport {
    float x{0.0F};
    float y{0.0F};
    float width{960.0F};
    float height{544.0F};
};

class MapRenderer {
public:
    MapRenderer(const MapProvider &provider, TileManager &manager);

    void prepare(const MapCamera &camera, const MapViewport &viewport,
                 std::uint64_t frame);
    void draw(const MapCamera &camera, const MapViewport &viewport,
              std::uint64_t frame);
    std::size_t texture_count() const { return textures_.size(); }
    std::size_t texture_bytes() const { return textures_.used_bytes(); }
    int tile_zoom() const { return tile_zoom_; }
    void clear_cache() { textures_.clear(); }

private:
    std::vector<TileRequest> build_requests(const MapCamera &camera,
                                            const MapViewport &viewport) const;
    bool draw_parent_fallback(const TileKey &key, float center_x,
                              float center_y, float width, float height,
                              float rotation,
                              std::uint64_t frame);
    bool draw_child_fallback(const TileKey &key, float center_x,
                             float center_y, float width, float height,
                             float rotation,
                             std::uint64_t frame);
    void pump_uploads(std::uint64_t frame, int maximum);

    const MapProvider &provider_;
    TileManager &manager_;
    TextureCache textures_{12U * 1024U * 1024U};
    int tile_zoom_{1};
};

} // namespace vitamaps
