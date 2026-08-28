#include "providers/osm_provider.h"

#include <cstdio>

namespace vitamaps {
namespace {
struct OsmStyle {
    std::uint32_t id;
    const char *name;
    const char *attribution;
    const char *url_format;
    int maximum_zoom;
};

// These are interactive raster services backed by OpenStreetMap data. Keep
// their ids stable: they are also the on-disk cache namespaces.
constexpr OsmStyle kStyles[] = {
    {1U, "OSM Standard", "© OpenStreetMap contributors",
     "https://tile.openstreetmap.org/%d/%d/%d.png", 19},
    {2U, "CyclOSM", "© OSM contributors | CyclOSM",
     "https://a.tile-cyclosm.openstreetmap.fr/cyclosm/%d/%d/%d.png", 20},
    {3U, "OSM France", "© OSM contributors | OSM France",
     "https://a.tile.openstreetmap.fr/osmfr/%d/%d/%d.png", 20},
    {4U, "Humanitarian", "© OSM contributors | Humanitarian",
     "https://a.tile.openstreetmap.fr/hot/%d/%d/%d.png", 18},
    {5U, "OpenTopoMap",
     "© OSM contributors | SRTM/Copernicus | OpenTopoMap CC-BY-SA",
     "https://a.tile.opentopomap.org/%d/%d/%d.png", 17},
};

const OsmStyle &style_at(int index) {
    if (index < 0 || index >= static_cast<int>(sizeof(kStyles) / sizeof(kStyles[0])))
        return kStyles[0];
    return kStyles[index];
}

const OsmStyle &style_for_id(std::uint32_t id) {
    for (const auto &style : kStyles) {
        if (style.id == id) return style;
    }
    return kStyles[0];
}
} // namespace

std::uint32_t OsmProvider::id() const { return style_at(style_index_).id; }

const char *OsmProvider::name() const { return style_at(style_index_).name; }

const char *OsmProvider::attribution() const {
    return style_at(style_index_).attribution;
}

int OsmProvider::max_zoom() const {
    return style_at(style_index_).maximum_zoom;
}

int OsmProvider::style_count() const {
    return static_cast<int>(sizeof(kStyles) / sizeof(kStyles[0]));
}

std::uint32_t OsmProvider::style_id(int index) const {
    return style_at(index).id;
}

const char *OsmProvider::style_name(int index) const {
    return style_at(index).name;
}

bool OsmProvider::set_style(int index) {
    if (index < 0 || index >= style_count()) return false;
    style_index_ = index;
    return true;
}

std::string OsmProvider::tile_url(std::uint32_t variant_id, int zoom, int x,
                                  int y) const {
    char url[192];
    std::snprintf(url, sizeof(url), style_for_id(variant_id).url_format,
                  zoom, x, y);
    return url;
}

} // namespace vitamaps
