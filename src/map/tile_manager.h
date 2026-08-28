#pragma once

#include "app/app_paths.h"
#include "cache/disk_cache.h"
#include "cache/memory_cache.h"
#include "core/spin_mutex.h"
#include "map/tile.h"
#include "map/gpx.h"
#include "map/tile_scheduler.h"
#include "net/map_http.h"
#include "net/geocoder.h"
#include "net/elevation.h"
#include "net/overpass.h"
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
    OfflineAtlasSnapshot atlas{};
    bool cleared{false};
    bool atlas_loaded{false};
    int error{0};
};

struct ElevationOperationResult {
    std::uint32_t list_id{0};
    ElevationResult elevation{};
};

enum class GpxRequestType {
    Refresh,
    Import,
    Export,
};

struct GpxWorkerResult {
    GpxRequestType type{GpxRequestType::Refresh};
    GpxOperationResult operation{};
    std::vector<GpxInboxEntry> inbox;
    std::vector<GpxImportRecord> history;
    PinRepository repository{};
    bool repository_changed{false};
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
    bool request_pois(const mercator::GeoPoint &center, double radius_meters);
    bool take_poi_result(OverpassResult &result);
    bool poi_pending() const;
    bool request_elevation(std::uint32_t list_id,
                           const std::vector<mercator::GeoPoint> &points);
    bool take_elevation_result(ElevationOperationResult &result);
    bool elevation_pending() const;
    bool request_gpx_refresh();
    bool request_gpx_import(std::size_t inbox_index,
                            const PinRepository &repository);
    bool request_gpx_export(const PinList &list);
    bool take_gpx_result(GpxWorkerResult &result);
    bool gpx_pending() const;
    bool request_cache_status();
    bool request_cache_clear();
    bool request_offline_atlas();
    bool take_cache_result(TileCacheOperationResult &result);
    bool cache_operation_pending() const;
    TileManagerStats stats() const;

private:
    struct Record {
        TileState state{TileState::Missing};
        std::uint64_t retry_after_generation{0};
        int error{0};
        std::uint8_t failures{0};
    };

    bool wanted_locked(const TileKey &key) const;
    bool still_wanted(const TileKey &key) const;
    void set_state(const TileKey &key, TileState state, int error = 0);
    static int worker_entry(SceSize argument_size, void *arguments);
    void worker_main();
    void process_task(const ScheduledTile &task);
    bool take_geocode_request(std::string &query, std::string &language_tag);
    bool take_poi_request(mercator::GeoPoint &center, double &radius_meters);
    bool take_elevation_request(std::uint32_t &list_id,
                                std::vector<mercator::GeoPoint> &points);
    bool take_gpx_request(GpxRequestType &type, std::size_t &inbox_index,
                          PinRepository &repository, PinList &list);
    bool take_cache_request(bool &clear, bool &atlas);

    const MapProvider &provider_;
    TileScheduler scheduler_;
    MemoryCache memory_cache_{6U * 1024U * 1024U};
    DiskCache disk_cache_{VITAMAPS_CACHE_DIR, 200U * 1024U * 1024U};
    MapHttp http_;
    Geocoder geocoder_;
    OverpassClient overpass_;
    ElevationClient elevation_;
    GpxManager gpx_;

    mutable SpinMutex state_mutex_;
    std::unordered_map<TileKey, Record, TileKeyHash> records_;
    std::unordered_set<TileKey, TileKeyHash> wanted_;
    TileKey active_key_{};
    bool active_valid_{false};
    bool active_cache_only_{false};
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

    mutable SpinMutex poi_mutex_;
    mercator::GeoPoint poi_center_{};
    double poi_radius_meters_{0.0};
    bool poi_requested_{false};
    bool poi_in_progress_{false};
    std::deque<OverpassResult> poi_results_;

    mutable SpinMutex elevation_mutex_;
    std::uint32_t elevation_list_id_{0};
    std::vector<mercator::GeoPoint> elevation_points_;
    bool elevation_requested_{false};
    bool elevation_in_progress_{false};
    std::deque<ElevationOperationResult> elevation_results_;

    mutable SpinMutex gpx_mutex_;
    GpxRequestType gpx_request_type_{GpxRequestType::Refresh};
    std::size_t gpx_inbox_index_{0};
    PinRepository gpx_repository_{};
    PinList gpx_list_{};
    bool gpx_requested_{false};
    bool gpx_in_progress_{false};
    std::deque<GpxWorkerResult> gpx_results_;

    mutable SpinMutex cache_operation_mutex_;
    bool cache_operation_requested_{false};
    bool cache_operation_clear_{false};
    bool cache_operation_atlas_{false};
    bool cache_operation_in_progress_{false};
    std::deque<TileCacheOperationResult> cache_operation_results_;
};

} // namespace vitamaps
