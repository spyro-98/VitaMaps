#pragma once

namespace vitamaps::mercator {

constexpr double kMaxLatitude = 85.0511287798066;

struct GeoPoint {
    double latitude{0.0};
    double longitude{0.0};
};

struct WorldPoint {
    double x{0.5}; // Normalized world coordinate; x wraps at 1.0.
    double y{0.5}; // Normalized and clamped to [0, 1].
};

double clamp_latitude(double latitude);
double wrap_longitude(double longitude);
WorldPoint lat_lon_to_world(double latitude, double longitude);
GeoPoint world_to_lat_lon(double world_x, double world_y);
double world_size(double zoom, int tile_size = 256);

} // namespace vitamaps::mercator
