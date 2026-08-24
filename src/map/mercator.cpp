#include "map/mercator.h"

#include <algorithm>
#include <cmath>

namespace vitamaps::mercator {
namespace {
constexpr double kPi = 3.1415926535897932384626433832795;
}

double clamp_latitude(double latitude) {
    return std::clamp(latitude, -kMaxLatitude, kMaxLatitude);
}

double wrap_longitude(double longitude) {
    double wrapped = std::fmod(longitude + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

WorldPoint lat_lon_to_world(double latitude, double longitude) {
    const double lat = clamp_latitude(latitude) * kPi / 180.0;
    const double x = (wrap_longitude(longitude) + 180.0) / 360.0;
    const double y = (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) /
                                kPi) *
                     0.5;
    return {x, std::clamp(y, 0.0, 1.0)};
}

GeoPoint world_to_lat_lon(double world_x, double world_y) {
    double x = std::fmod(world_x, 1.0);
    if (x < 0.0) x += 1.0;
    const double y = std::clamp(world_y, 0.0, 1.0);
    const double longitude = wrap_longitude(x * 360.0 - 180.0);
    const double latitude =
        std::atan(std::sinh(kPi * (1.0 - 2.0 * y))) * 180.0 / kPi;
    return {clamp_latitude(latitude), longitude};
}

double world_size(double zoom, int tile_size) {
    return static_cast<double>(tile_size) * std::exp2(zoom);
}

} // namespace vitamaps::mercator
