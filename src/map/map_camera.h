#pragma once

#include "map/mercator.h"

namespace vitamaps {

struct MapScreenPoint {
    double x{0.0};
    double y{0.0};
};

struct MapCamera {
    double latitude{41.9028};
    double longitude{12.4964};
    double zoom{5.0};
    // Clockwise map rotation in degrees. Zero keeps geographic north at the
    // top of the display.
    double bearing{0.0};
    double velocityX{0.0};
    double velocityY{0.0};

    double target_zoom{5.0};
    double target_bearing{0.0};
    double min_zoom{1.0};
    double max_zoom{19.0};

    mercator::WorldPoint world() const;
    void set_center(double new_latitude, double new_longitude);
    void pan_by_screen_pixels(double dx, double dy);
    void set_zoom_target(double requested_zoom);
    void zoom_immediate(double delta);
    void set_bearing_target(double requested_bearing);
    void rotate_immediate(double delta_degrees);
    void update(double dt, bool apply_inertia);
    mercator::GeoPoint screen_to_geo(double screen_x, double screen_y,
                                     double viewport_width,
                                     double viewport_height) const;
    MapScreenPoint geo_to_screen(double latitude, double longitude,
                                 double viewport_width,
                                 double viewport_height) const;
};

} // namespace vitamaps
