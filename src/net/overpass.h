#pragma once

#include "map/mercator.h"
#include "net/map_http.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vitamaps {

enum class PoiCategory : std::uint8_t {
    Amenity,
    Food,
    Water,
    Shelter,
    Tourism,
    Summit,
    Nature,
};

struct PointOfInterest {
    std::uint64_t osm_id{0};
    mercator::GeoPoint position{};
    PoiCategory category{PoiCategory::Amenity};
    std::string name;
    std::string kind;
};

struct OverpassResult {
    std::vector<PointOfInterest> points;
    mercator::GeoPoint center{};
    double radius_meters{0.0};
    int error{0};
    long http_status{0};
};

class OverpassClient {
public:
    OverpassResult search_pois(MapHttp &http,
                               const mercator::GeoPoint &center,
                               double radius_meters,
                               volatile int *cancel_flag);

private:
    unsigned long long last_request_us_{0};
};

} // namespace vitamaps
