#pragma once

#include <cstdint>
#include <string>

namespace vitamaps {

class MapProvider {
public:
    virtual ~MapProvider() = default;
    virtual std::uint32_t id() const = 0;
    virtual const char *name() const = 0;
    virtual const char *attribution() const = 0;
    virtual int min_zoom() const = 0;
    virtual int max_zoom() const = 0;
    virtual int tile_size() const = 0;
    // The variant id is part of TileKey so an in-flight request remains bound
    // to the style that produced it even if the UI switches style meanwhile.
    virtual std::string tile_url(std::uint32_t variant_id, int zoom, int x,
                                 int y) const = 0;
    virtual int style_count() const = 0;
    virtual int style_index() const = 0;
    virtual const char *style_name(int index) const = 0;
    virtual bool set_style(int index) = 0;
};

} // namespace vitamaps
