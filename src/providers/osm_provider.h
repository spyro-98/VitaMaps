#pragma once

#include "providers/map_provider.h"

namespace vitamaps {

class OsmProvider final : public MapProvider {
public:
    std::uint32_t id() const override;
    const char *name() const override;
    const char *attribution() const override;
    int min_zoom() const override { return 1; }
    int max_zoom() const override;
    int tile_size() const override { return 256; }
    std::string tile_url(std::uint32_t variant_id, int zoom, int x,
                         int y) const override;
    int style_count() const override;
    int style_index() const override { return style_index_; }
    std::uint32_t style_id(int index) const override;
    const char *style_name(int index) const override;
    bool set_style(int index) override;
private:
    int style_index_{0};
};

} // namespace vitamaps
