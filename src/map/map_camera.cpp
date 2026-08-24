#include "map/map_camera.h"

#include <algorithm>
#include <cmath>

namespace vitamaps {
namespace {
constexpr double kPi = 3.14159265358979323846;

double normalize_bearing(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

double shortest_bearing_delta(double from, double to) {
    double delta = normalize_bearing(to) - normalize_bearing(from);
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return delta;
}
} // namespace

mercator::WorldPoint MapCamera::world() const {
    return mercator::lat_lon_to_world(latitude, longitude);
}

void MapCamera::set_center(double new_latitude, double new_longitude) {
    latitude = mercator::clamp_latitude(new_latitude);
    longitude = mercator::wrap_longitude(new_longitude);
}

void MapCamera::pan_by_screen_pixels(double dx, double dy) {
    const double size = mercator::world_size(zoom);
    const double radians = bearing * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    // The rendered map is rotated clockwise. Convert a screen-relative pan
    // back into unrotated Web Mercator axes before moving the camera.
    const double world_dx = cosine * dx + sine * dy;
    const double world_dy = -sine * dx + cosine * dy;
    auto point = world();
    point.x += world_dx / size;
    point.y = std::clamp(point.y + world_dy / size, 0.0, 1.0);
    const auto geo = mercator::world_to_lat_lon(point.x, point.y);
    set_center(geo.latitude, geo.longitude);
}

void MapCamera::set_zoom_target(double requested_zoom) {
    target_zoom = std::clamp(requested_zoom, min_zoom, max_zoom);
}

void MapCamera::zoom_immediate(double delta) {
    zoom = std::clamp(zoom + delta, min_zoom, max_zoom);
    target_zoom = zoom;
}

void MapCamera::set_bearing_target(double requested_bearing) {
    target_bearing = normalize_bearing(requested_bearing);
}

void MapCamera::rotate_immediate(double delta_degrees) {
    bearing = normalize_bearing(bearing + delta_degrees);
    target_bearing = bearing;
}

void MapCamera::update(double dt, bool apply_inertia) {
    dt = std::clamp(dt, 0.0, 0.05);
    const double zoom_blend = 1.0 - std::exp(-10.0 * dt);
    zoom += (target_zoom - zoom) * zoom_blend;
    if (std::abs(target_zoom - zoom) < 0.001) zoom = target_zoom;

    const double bearing_blend = 1.0 - std::exp(-12.0 * dt);
    const double bearing_delta = shortest_bearing_delta(bearing,
                                                        target_bearing);
    bearing = normalize_bearing(bearing + bearing_delta * bearing_blend);
    if (std::abs(bearing_delta) < 0.05)
        bearing = target_bearing = normalize_bearing(target_bearing);

    if (apply_inertia) {
        pan_by_screen_pixels(velocityX * dt, velocityY * dt);
        const double damping = std::exp(-5.2 * dt);
        velocityX *= damping;
        velocityY *= damping;
        if (std::abs(velocityX) < 0.5) velocityX = 0.0;
        if (std::abs(velocityY) < 0.5) velocityY = 0.0;
    }
}

mercator::GeoPoint MapCamera::screen_to_geo(double screen_x, double screen_y,
                                             double viewport_width,
                                             double viewport_height) const {
    auto point = world();
    const double size = mercator::world_size(zoom);
    const double screen_dx = screen_x - viewport_width * 0.5;
    const double screen_dy = screen_y - viewport_height * 0.5;
    const double radians = bearing * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    point.x += (cosine * screen_dx + sine * screen_dy) / size;
    point.y += (-sine * screen_dx + cosine * screen_dy) / size;
    return mercator::world_to_lat_lon(point.x, point.y);
}

MapScreenPoint MapCamera::geo_to_screen(double point_latitude,
                                         double point_longitude,
                                         double viewport_width,
                                         double viewport_height) const {
    const auto center = world();
    const auto point = mercator::lat_lon_to_world(point_latitude,
                                                  point_longitude);
    double delta_x = point.x - center.x;
    if (delta_x > 0.5) delta_x -= 1.0;
    if (delta_x < -0.5) delta_x += 1.0;
    const double size = mercator::world_size(zoom);
    const double world_dx = delta_x * size;
    const double world_dy = (point.y - center.y) * size;
    const double radians = bearing * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {viewport_width * 0.5 + cosine * world_dx - sine * world_dy,
            viewport_height * 0.5 + sine * world_dx + cosine * world_dy};
}

} // namespace vitamaps
