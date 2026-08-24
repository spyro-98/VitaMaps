#pragma once

#include "map/mercator.h"

#include <string>

namespace vitamaps {

struct CachedGeocode {
    mercator::GeoPoint point{};
    std::string display_name;
};

// Small persistent cache for explicit, user-triggered searches. Each query is
// stored atomically in its own hashed record and the directory is bounded to
// 128 entries, keeping repeated searches off the public geocoder.
class GeocodeCache {
public:
    bool read(const std::string &query, CachedGeocode &result) const;
    bool write(const std::string &query, const CachedGeocode &result) const;

private:
    static std::string normalize_query(const std::string &query);
    static unsigned long long hash_query(const std::string &normalized);
    static std::string path_for(unsigned long long hash);
    static void enforce_entry_limit();
};

} // namespace vitamaps
