#pragma once

#include "map/mercator.h"

#include <string>

namespace vitamaps {

// Resolves an exact populated-place name from the bundled local table. The
// table is used only for search and never rendered over provider tiles.
bool find_local_place(const char *query, mercator::GeoPoint &point,
                      std::string &display_name);

} // namespace vitamaps
