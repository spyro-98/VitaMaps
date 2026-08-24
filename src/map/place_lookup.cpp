#include "map/place_lookup.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace vitamaps {
namespace {

struct PlaceData {
    const char *name;
    float world_x;
    float world_y;
    std::uint32_t population;
    std::uint8_t minimum_zoom_tenths;
    std::uint8_t rank;
};

constexpr PlaceData kPlaces[] = {
#include "render/place_label_data.inc"
};

std::string normalized_place_name(const char *text) {
    if (!text) return {};
    std::string result;
    bool pending_space = false;
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(text);
         *cursor; ++cursor) {
        const unsigned char byte = *cursor;
        if (byte == ',') break;
        if (std::isspace(byte)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result.push_back(' ');
        pending_space = false;
        result.push_back(byte < 0x80U
            ? static_cast<char>(std::tolower(byte))
            : static_cast<char>(byte));
    }
    return result;
}

} // namespace

bool find_local_place(const char *query, mercator::GeoPoint &point,
                      std::string &display_name) {
    const std::string wanted = normalized_place_name(query);
    if (wanted.empty()) return false;
    for (const PlaceData &place : kPlaces) {
        if (normalized_place_name(place.name) != wanted) continue;
        point = mercator::world_to_lat_lon(place.world_x, place.world_y);
        display_name = place.name;
        return true;
    }
    return false;
}

} // namespace vitamaps
