#pragma once

#include "map/mercator.h"
#include "net/map_http.h"

#include <vector>

namespace vitamaps {

struct ElevationResult {
    std::vector<double> meters;
    int error{0};
    long http_status{0};
};

class ElevationClient {
public:
    ElevationResult lookup(MapHttp &http,
                           const std::vector<mercator::GeoPoint> &points,
                           volatile int *cancel_flag) const;
};

} // namespace vitamaps
