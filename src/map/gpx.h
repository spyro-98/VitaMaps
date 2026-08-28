#pragma once

#include "map/pin_collection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vitamaps {

struct GpxInboxEntry {
    std::string filename;
    std::uint64_t bytes{0};
    std::uint64_t modified{0};
};

struct GpxImportRecord {
    std::string filename;
    std::string list_name;
    std::uint64_t imported_at{0};
    std::uint32_t points{0};
    int result{0};
};

struct GpxOperationResult {
    int error{0};
    std::string path;
    std::string list_name;
    std::uint32_t points{0};
    bool repository_changed{false};
};

// GPX I/O is explicit user-driven filesystem work. Import accepts GPX 1.0/1.1
// waypoint, route-point, and track-point elements and downsamples oversized
// tracks deterministically to the VitaMaps 64-point list bound. Export writes
// standards-compliant GPX 1.1 routes in WGS84/metric units.
class GpxManager {
public:
    int initialize();
    int refresh_inbox();
    GpxOperationResult import_file(std::size_t inbox_index,
                                   PinRepository &repository);
    GpxOperationResult export_list(const PinList &list) const;

    const std::vector<GpxInboxEntry> &inbox() const { return inbox_; }
    const std::vector<GpxImportRecord> &history() const { return history_; }

private:
    int load_history();
    int save_history() const;
    void append_history(GpxImportRecord record);

    std::vector<GpxInboxEntry> inbox_;
    std::vector<GpxImportRecord> history_;
};

} // namespace vitamaps
