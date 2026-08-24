#pragma once

#include "app/app_paths.h"
#include "cache/disk_cache.h"
#include "cache/memory_cache.h"
#include "core/spin_mutex.h"
#include "map/tile.h"
#include "map/tile_scheduler.h"
#include "net/map_http.h"
#include "net/geocoder.h"
#include "providers/map_provider.h"

#include <atomic>
#include <psp2/types.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vitamaps {

struct TileManagerStats {
    std::size_t queued{0};
    std::size_t downloading{0};
    std::size_t decoding{0};
    std::size_t ready{0};
    std::size_t failed{0};
    std::uint64_t downloaded_tiles{0};
    std::uint64_t disk_hits{0};
    std::uint64_t memory_hits{0};
    int last_error{0};
};

struct TileCacheOperationResult {
    DiskCacheStatus status{};
    bool cleared{false};
    int error{0};
};

class TileManager {
public:
    explicit TileManager(const MapProvider &provider);
    ~TileManager();
    TileManager(const TileManager &) = delete;
    TileManager &operator=(const TileManager &) = delete;

    int start(bool network_available);
    void stop();
    void submit_requests(const std::vector<TileRequest> &requests,
                         std::uint64_t generation);
    bool take_decoded(DecodedTile &tile);
    void mark_ready(const TileKey &key);
    void mark_evicted(const TileKey &key);
    bool request_geocode(const std::string &query,
                         const std::string &language_tag);
    bool take_geocode_result(GeocodeResult &result);
    bool geocode_pending() const;
    bool request_cache_status();
    bool request_cache_clear();
    bool take_cache_result(TileCacheOperationResult &result);
    bool cache_operation_pending() const;
    TileManagerStats stats() const;

private:
    struct Record {
        TileState state{TileState::Missing};
        std::uint64_t retry_after_generation{0};
        int error{0};
    };

    bool wanted_locked(const TileKey &key) const;
    bool still_wanted(const TileKey &key) const;
    void set_state(const TileKey &key, TileState state, int error = 0);
    static int worker_entry(SceSize argument_size, void *arguments);
    void worker_main();
    void process_task(const ScheduledTile &task);
    bool take_geocode_request(std::string &query, std::string &language_tag);
    bool take_cache_request(bool &clear);

    const MapProvider &provider_;
    TileScheduler scheduler_;
    MemoryCache memory_cache_{6U * 1024U * 1024U};
    DiskCache disk_cache_{VITAMAPS_CACHE_DIR, 96U * 1024U * 1024U};
    MapHttp http_;
    Geocoder geocoder_;

    mutable SpinMutex state_mutex_;
    std::unordered_map<TileKey, Record, TileKeyHash> records_;
    std::unordered_set<TileKey, TileKeyHash> wanted_;
    TileKey active_key_{};
    bool active_valid_{false};
    std::uint64_t current_generation_{0};
    TileManagerStats counters_{};

    mutable SpinMutex upload_mutex_;
    std::deque<DecodedTile> uploads_;
    SceUID worker_thread_{-1};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopping_{false};
    bool network_available_{false};
    volatile int cancel_flag_{0};

    mutable SpinMutex geocode_mutex_;
    std::string geocode_query_;
    std::string geocode_language_;
    bool geocode_requested_{false};
    bool geocode_in_progress_{false};
    std::deque<GeocodeResult> geocode_results_;

    mutable SpinMutex cache_operation_mutex_;
    bool cache_operation_requested_{false};
    bool cache_operation_clear_{false};
    bool cache_operation_in_progress_{false};
    std::deque<TileCacheOperationResult> cache_operation_results_;
};

} // namespace vitamaps
