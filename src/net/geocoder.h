#pragma once

#include "cache/geocode_cache.h"
#include "net/map_http.h"

#include <string>

namespace vitamaps {

enum class GeocodeStatus {
    Found,
    NotFound,
    Failed,
};

struct GeocodeResult {
    GeocodeStatus status{GeocodeStatus::Failed};
    mercator::GeoPoint point{};
    std::string display_name;
    bool from_cache{false};
    int error{0};
    long http_status{0};
};

// Performs only explicit, single-result searches. It never autocompletes or
// batches queries, caches successful responses, and rate-limits remote calls.
class Geocoder {
public:
    GeocodeResult search(MapHttp &http, const std::string &query,
                         const std::string &language_tag,
                         volatile int *cancel_flag);

private:
    static std::string endpoint();
    static bool parse_response(const std::vector<std::uint8_t> &bytes,
                               CachedGeocode &result);

    GeocodeCache cache_;
    unsigned long long last_remote_request_us_{0};
};

} // namespace vitamaps
