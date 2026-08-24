#include "map/pin_collection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vitamaps {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusMeters = 6371008.8;

double radians(double degrees) { return degrees * kPi / 180.0; }
} // namespace

double pin_distance_meters(const MapPin &left, const MapPin &right) {
    const double lat1 = radians(left.position.latitude);
    const double lat2 = radians(right.position.latitude);
    const double delta_latitude = lat2 - lat1;
    const double delta_longitude =
        radians(mercator::wrap_longitude(right.position.longitude -
                                         left.position.longitude));
    const double sin_latitude = std::sin(delta_latitude * 0.5);
    const double sin_longitude = std::sin(delta_longitude * 0.5);
    const double a = sin_latitude * sin_latitude +
                     std::cos(lat1) * std::cos(lat2) *
                         sin_longitude * sin_longitude;
    return 2.0 * kEarthRadiusMeters *
           std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
}

double pin_path_distance_meters(const PinList &list) {
    double total = 0.0;
    for (std::size_t index = 1; index < list.pins.size(); ++index)
        total += pin_distance_meters(list.pins[index - 1], list.pins[index]);
    if (list.closed && list.pins.size() >= 3)
        total += pin_distance_meters(list.pins.back(), list.pins.front());
    return total;
}

double pin_polygon_area_square_meters(const PinList &list) {
    if (!list.closed || list.pins.size() < 3) return 0.0;
    // Chamberlain-Duquette spherical polygon area. Longitude deltas are
    // wrapped, so a polygon close to the date line remains local.
    double sum = 0.0;
    for (std::size_t index = 0; index < list.pins.size(); ++index) {
        const auto &left = list.pins[index].position;
        const auto &right = list.pins[(index + 1) % list.pins.size()].position;
        const double delta_longitude = radians(mercator::wrap_longitude(
            right.longitude - left.longitude));
        sum += delta_longitude *
               (std::sin(radians(left.latitude)) +
                std::sin(radians(right.latitude)));
    }
    return std::abs(sum) * kEarthRadiusMeters * kEarthRadiusMeters * 0.5;
}

bool parse_coordinates(const char *text, mercator::GeoPoint &point) {
    if (!text) return false;
    char *end = nullptr;
    const double latitude = std::strtod(text, &end);
    if (end == text || !std::isfinite(latitude)) return false;
    bool had_space = false;
    while (*end == ' ' || *end == '\t') {
        had_space = true;
        ++end;
    }
    if (*end == ',' || *end == ';' || *end == '/') {
        ++end;
    } else if (!had_space) {
        return false;
    }
    while (*end == ' ' || *end == '\t') ++end;
    char *final = nullptr;
    const double longitude = std::strtod(end, &final);
    if (final == end || !std::isfinite(longitude)) return false;
    while (*final == ' ' || *final == '\t') ++final;
    if (*final != '\0' || latitude < -mercator::kMaxLatitude ||
        latitude > mercator::kMaxLatitude || longitude < -180.0 ||
        longitude > 180.0)
        return false;
    point = {latitude, mercator::wrap_longitude(longitude)};
    return true;
}

} // namespace vitamaps
