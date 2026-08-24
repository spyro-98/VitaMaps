#include "map/map_camera.h"
#include "map/mercator.h"
#include "map/pin_collection.h"

#include <cassert>
#include <cmath>

namespace {
bool near(double left, double right, double epsilon = 1e-8) {
    return std::abs(left - right) <= epsilon;
}
}

int main() {
    using namespace vitamaps;
    const auto origin = mercator::lat_lon_to_world(0.0, 0.0);
    assert(near(origin.x, 0.5));
    assert(near(origin.y, 0.5));

    const auto rome = mercator::lat_lon_to_world(41.9028, 12.4964);
    const auto round_trip = mercator::world_to_lat_lon(rome.x, rome.y);
    assert(near(round_trip.latitude, 41.9028));
    assert(near(round_trip.longitude, 12.4964));
    assert(near(mercator::wrap_longitude(190.0), -170.0));
    assert(near(mercator::clamp_latitude(90.0), mercator::kMaxLatitude));

    MapCamera camera;
    const auto center_screen = camera.geo_to_screen(
        camera.latitude, camera.longitude, 960.0, 544.0);
    assert(near(center_screen.x, 480.0));
    assert(near(center_screen.y, 272.0));
    const auto before = camera.world();
    camera.pan_by_screen_pixels(256.0, 0.0);
    const auto after = camera.world();
    assert(near(after.x - before.x, 1.0 / 32.0));

    camera.set_center(41.9028, 12.4964);
    camera.rotate_immediate(90.0);
    const auto east = camera.geo_to_screen(41.9028, 12.5964, 960.0, 544.0);
    assert(east.y > 272.0);
    const auto rotated_round_trip = camera.screen_to_geo(
        east.x, east.y, 960.0, 544.0);
    assert(near(rotated_round_trip.latitude, 41.9028, 1e-6));
    assert(near(rotated_round_trip.longitude, 12.5964, 1e-6));

    mercator::GeoPoint parsed;
    assert(parse_coordinates("41.9028, 12.4964", parsed));
    assert(near(parsed.latitude, 41.9028));
    assert(near(parsed.longitude, 12.4964));
    assert(parse_coordinates("41.9028 12.4964", parsed));
    assert(!parse_coordinates("Roma", parsed));
    assert(!parse_coordinates("90, 12", parsed));

    PinList route;
    route.pins.push_back({1, {0.0, 0.0}, "A", {}});
    route.pins.push_back({2, {0.0, 1.0}, "B", {}});
    const double one_degree = pin_path_distance_meters(route);
    assert(one_degree > 111000.0 && one_degree < 111300.0);
    route.pins.push_back({3, {1.0, 1.0}, "C", {}});
    assert(pin_polygon_area_square_meters(route) == 0.0);
    const double open_distance = pin_path_distance_meters(route);
    route.closed = true;
    assert(pin_path_distance_meters(route) > open_distance);
    assert(pin_polygon_area_square_meters(route) > 6.0e9);

    PinList date_line_square;
    date_line_square.closed = true;
    date_line_square.pins.push_back({4, {0.0, 179.5}, "A", {}});
    date_line_square.pins.push_back({5, {0.0, -179.5}, "B", {}});
    date_line_square.pins.push_back({6, {1.0, -179.5}, "C", {}});
    date_line_square.pins.push_back({7, {1.0, 179.5}, "D", {}});
    const double date_line_area =
        pin_polygon_area_square_meters(date_line_square);
    assert(date_line_area > 1.2e10 && date_line_area < 1.3e10);

    return 0;
}
