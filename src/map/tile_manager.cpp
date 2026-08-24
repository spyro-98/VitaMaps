#include "map/tile_manager.h"

#include "core/log.h"
#include "render/image_decoder.h"

#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include <cstring>
#include <exception>
#include <string>
#include <utility>

namespace vitamaps {
namespace {
// VitaSDK's documented user-thread priority token. Unlike 0x10000180, this is
// accepted by the retail user process profile used by VitaMaps.
constexpr int kTileThreadPriority = 0x10000100;
constexpr SceSize kTileThreadStack = 0x100000;
}

TileManager::TileManager(const MapProvider &provider) : provider_(provider) {}

TileManager::~TileManager() { stop(); }

int TileManager::start(bool network_available) {
    if (started_.exchange(true)) return 0;
    stopping_ = false;
    network_available_ = network_available;
    const int current_priority = sceKernelGetThreadCurrentPriority();
    const int derived_priority =
        current_priority >= 0x40 && current_priority <= 0x7F
            ? (current_priority < 0x77 ? current_priority + 8 : 0x7F)
            : 0x70;
    const int candidates[] = {
        kTileThreadPriority,
        derived_priority,
        0x70,
    };
    int selected_priority = candidates[0];
    worker_thread_ = -1;
    for (std::size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]);
         ++index) {
        bool duplicate = false;
        for (std::size_t previous = 0; previous < index; ++previous)
            duplicate = duplicate || candidates[previous] == candidates[index];
        if (duplicate) continue;
        selected_priority = candidates[index];
        worker_thread_ = sceKernelCreateThread(
            "VitaMapsTileWorker", &TileManager::worker_entry,
            selected_priority, kTileThreadStack, 0, 0, nullptr);
        if (worker_thread_ >= 0) break;
        log_printf("tile worker create attempt priority=0x%X -> 0x%08X",
                   selected_priority, static_cast<unsigned>(worker_thread_));
        if (static_cast<unsigned>(worker_thread_) !=
            static_cast<unsigned>(SCE_KERNEL_ERROR_ILLEGAL_PRIORITY))
            break;
    }
    if (worker_thread_ < 0) {
        const int result = worker_thread_;
        worker_thread_ = -1;
        log_printf("tile worker create failed: 0x%08X",
                   static_cast<unsigned>(result));
        log_save();
        started_ = false;
        return result;
    }
    log_printf("tile worker created uid=0x%08X priority=0x%X stack=0x%X",
               static_cast<unsigned>(worker_thread_), selected_priority,
               static_cast<unsigned>(kTileThreadStack));
    TileManager *self = this;
    const int result = sceKernelStartThread(worker_thread_, sizeof(self), &self);
    if (result < 0) {
        log_printf("tile worker start failed: 0x%08X",
                   static_cast<unsigned>(result));
        log_save();
        sceKernelDeleteThread(worker_thread_);
        worker_thread_ = -1;
        started_ = false;
        return result;
    }
    return 0;
}

void TileManager::stop() {
    if (!started_.exchange(false)) return;
    stopping_ = true;
    cancel_flag_ = 1;
    scheduler_.shutdown();
    if (worker_thread_ >= 0) {
        int worker_status = 0;
        const int wait_result =
            sceKernelWaitThreadEnd(worker_thread_, &worker_status, nullptr);
        log_printf("tile worker join -> 0x%08X status=0x%08X",
                   static_cast<unsigned>(wait_result),
                   static_cast<unsigned>(worker_status));
        sceKernelDeleteThread(worker_thread_);
        worker_thread_ = -1;
    }
    std::lock_guard<SpinMutex> upload_lock(upload_mutex_);
    uploads_.clear();
    std::lock_guard<SpinMutex> geocode_lock(geocode_mutex_);
    geocode_query_.clear();
    geocode_language_.clear();
    geocode_requested_ = false;
    geocode_in_progress_ = false;
    geocode_results_.clear();
    std::lock_guard<SpinMutex> cache_lock(cache_operation_mutex_);
    cache_operation_requested_ = false;
    cache_operation_in_progress_ = false;
    cache_operation_results_.clear();
}

int TileManager::worker_entry(SceSize argument_size, void *arguments) {
    TileManager *manager = nullptr;
    if (arguments && argument_size == sizeof(manager))
        std::memcpy(&manager, arguments, sizeof(manager));
    if (!manager) return sceKernelExitThread(-1);
    manager->worker_main();
    return sceKernelExitThread(0);
}

bool TileManager::wanted_locked(const TileKey &key) const {
    return wanted_.find(key) != wanted_.end();
}

bool TileManager::still_wanted(const TileKey &key) const {
    std::lock_guard<SpinMutex> lock(state_mutex_);
    return wanted_locked(key);
}

void TileManager::submit_requests(const std::vector<TileRequest> &requests,
                                  std::uint64_t generation) {
    std::vector<TileRequest> queued_tasks;
    {
        std::lock_guard<SpinMutex> lock(state_mutex_);
        current_generation_ = generation;
        wanted_.clear();
        wanted_.reserve(requests.size());
        for (const auto &request : requests) wanted_.insert(request.key);

        for (auto &item : records_) {
            if (item.second.state == TileState::Queued &&
                !wanted_locked(item.first)) {
                item.second.state = TileState::Missing;
            }
        }

        for (const auto &request : requests) {
            Record &record = records_[request.key];
            const bool can_retry = record.state == TileState::Failed &&
                                   generation >= record.retry_after_generation;
            if (record.state == TileState::Missing || can_retry) {
                record.state = TileState::Queued;
                record.error = 0;
            }
            if (record.state == TileState::Queued) queued_tasks.push_back(request);
        }
        bool active_wanted = false;
        bool active_visible_now = false;
        bool visible_waiting = false;
        for (const auto &request : requests) {
            if (active_valid_ && request.key == active_key_) {
                active_wanted = true;
                active_visible_now = request.visible;
            }
            const auto found = records_.find(request.key);
            if (request.visible &&
                (!active_valid_ || request.key != active_key_) &&
                found != records_.end() &&
                found->second.state == TileState::Queued) {
                visible_waiting = true;
            }
        }
        // Rebuild happens every frame. A prefetch request may remain inside
        // the look-ahead ring, but it must still yield immediately when the
        // new viewport contains a missing visible tile.
        if (active_valid_ &&
            (!active_wanted || (!active_visible_now && visible_waiting))) {
            cancel_flag_ = 1;
        }
        if (generation % 300U == 0U) {
            for (auto it = records_.begin(); it != records_.end();) {
                if (!wanted_locked(it->first) &&
                    (it->second.state == TileState::Missing ||
                     it->second.state == TileState::Failed)) {
                    it = records_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    scheduler_.replace(queued_tasks, generation);
}

bool TileManager::take_decoded(DecodedTile &tile) {
    std::lock_guard<SpinMutex> upload_lock(upload_mutex_);
    while (!uploads_.empty()) {
        DecodedTile candidate = std::move(uploads_.front());
        uploads_.pop_front();
        if (still_wanted(candidate.key)) {
            tile = std::move(candidate);
            return true;
        }
        set_state(candidate.key, TileState::Missing);
    }
    return false;
}

void TileManager::mark_ready(const TileKey &key) {
    set_state(key, TileState::Ready);
}

void TileManager::mark_evicted(const TileKey &key) {
    std::lock_guard<SpinMutex> lock(state_mutex_);
    const auto found = records_.find(key);
    if (found != records_.end() &&
        (found->second.state == TileState::Ready ||
         found->second.state == TileState::Decoding))
        found->second.state = TileState::Missing;
}

bool TileManager::request_geocode(const std::string &query,
                                  const std::string &language_tag) {
    if (!started_ || query.empty()) return false;
    {
        std::lock_guard<SpinMutex> lock(geocode_mutex_);
        if (geocode_requested_ || geocode_in_progress_) return false;
        geocode_query_ = query;
        geocode_language_ = language_tag;
        geocode_requested_ = true;
        geocode_results_.clear();
    }
    // An explicit search is user-visible work and may preempt a tile transfer.
    std::lock_guard<SpinMutex> lock(state_mutex_);
    if (active_valid_) cancel_flag_ = 1;
    return true;
}

bool TileManager::take_geocode_result(GeocodeResult &result) {
    std::lock_guard<SpinMutex> lock(geocode_mutex_);
    if (geocode_results_.empty()) return false;
    result = std::move(geocode_results_.front());
    geocode_results_.pop_front();
    return true;
}

bool TileManager::geocode_pending() const {
    std::lock_guard<SpinMutex> lock(geocode_mutex_);
    return geocode_requested_ || geocode_in_progress_;
}

bool TileManager::request_cache_status() {
    if (!started_) return false;
    std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
    if (cache_operation_requested_ || cache_operation_in_progress_) return false;
    cache_operation_requested_ = true;
    cache_operation_clear_ = false;
    return true;
}

bool TileManager::request_cache_clear() {
    if (!started_) return false;
    {
        std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
        if (cache_operation_requested_ || cache_operation_in_progress_)
            return false;
        cache_operation_requested_ = true;
        cache_operation_clear_ = true;
        cache_operation_results_.clear();
    }
    {
        std::lock_guard<SpinMutex> state_lock(state_mutex_);
        cancel_flag_ = 1;
        wanted_.clear();
        for (auto &record : records_) record.second.state = TileState::Missing;
    }
    {
        std::lock_guard<SpinMutex> upload_lock(upload_mutex_);
        uploads_.clear();
    }
    scheduler_.replace({}, current_generation_);
    return true;
}

bool TileManager::take_cache_result(TileCacheOperationResult &result) {
    std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
    if (cache_operation_results_.empty()) return false;
    result = cache_operation_results_.front();
    cache_operation_results_.pop_front();
    return true;
}

bool TileManager::cache_operation_pending() const {
    std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
    return cache_operation_requested_ || cache_operation_in_progress_;
}

bool TileManager::take_cache_request(bool &clear) {
    std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
    if (!cache_operation_requested_) return false;
    clear = cache_operation_clear_;
    cache_operation_requested_ = false;
    cache_operation_in_progress_ = true;
    return true;
}

bool TileManager::take_geocode_request(std::string &query,
                                       std::string &language_tag) {
    std::lock_guard<SpinMutex> lock(geocode_mutex_);
    if (!geocode_requested_) return false;
    query = std::move(geocode_query_);
    language_tag = std::move(geocode_language_);
    geocode_query_.clear();
    geocode_language_.clear();
    geocode_requested_ = false;
    geocode_in_progress_ = true;
    return true;
}

void TileManager::set_state(const TileKey &key, TileState state, int error) {
    std::lock_guard<SpinMutex> lock(state_mutex_);
    Record &record = records_[key];
    record.state = state;
    record.error = error;
    if (state == TileState::Failed) {
        record.retry_after_generation = current_generation_ + 180;
        counters_.last_error = error;
    }
}

TileManagerStats TileManager::stats() const {
    std::lock_guard<SpinMutex> lock(state_mutex_);
    TileManagerStats result = counters_;
    result.queued = scheduler_.size();
    result.downloading = 0;
    result.decoding = 0;
    result.ready = 0;
    result.failed = 0;
    for (const auto &item : records_) {
        switch (item.second.state) {
        case TileState::Downloading: ++result.downloading; break;
        case TileState::Decoding: ++result.decoding; break;
        case TileState::Ready: ++result.ready; break;
        case TileState::Failed: ++result.failed; break;
        default: break;
        }
    }
    return result;
}

void TileManager::worker_main() {
    log_printf("tile worker started network=%d", network_available_ ? 1 : 0);
    try {
        if (network_available_ && !http_.initialize()) {
            network_available_ = false;
            log_printf("tile worker: vita-https client creation failed");
            log_save();
        }
        disk_cache_.enforce_budget();
        while (!stopping_) {
            std::string geocode_query;
            std::string geocode_language;
            if (take_geocode_request(geocode_query, geocode_language)) {
                cancel_flag_ = 0;
                GeocodeResult result =
                    geocoder_.search(http_, geocode_query, geocode_language,
                                     &cancel_flag_);
                {
                    std::lock_guard<SpinMutex> lock(geocode_mutex_);
                    geocode_in_progress_ = false;
                    if (!stopping_) geocode_results_.push_back(std::move(result));
                }
                continue;
            }
            bool clear_cache = false;
            if (take_cache_request(clear_cache)) {
                TileCacheOperationResult result;
                result.cleared = clear_cache;
                if (clear_cache) {
                    memory_cache_.clear();
                    result.error = disk_cache_.clear_all();
                }
                result.status = disk_cache_.status();
                log_printf("tile cache: clear=%d error=0x%08X bytes=%llu "
                           "entries=%u styles=%u",
                           clear_cache ? 1 : 0,
                           static_cast<unsigned>(result.error),
                           static_cast<unsigned long long>(result.status.bytes),
                           static_cast<unsigned>(result.status.entries),
                           static_cast<unsigned>(result.status.styles));
                {
                    std::lock_guard<SpinMutex> lock(cache_operation_mutex_);
                    cache_operation_in_progress_ = false;
                    if (!stopping_)
                        cache_operation_results_.push_back(result);
                }
                continue;
            }
            ScheduledTile task;
            if (scheduler_.try_pop(task)) {
                process_task(task);
            } else {
                sceKernelDelayThread(8U * 1000U);
            }
        }
    } catch (const std::exception &error) {
        log_printf("tile worker exception: %s", error.what());
        log_save();
    } catch (...) {
        log_printf("tile worker exception: unknown");
        log_save();
    }
    http_.shutdown();
    log_printf("tile worker stopped");
}

void TileManager::process_task(const ScheduledTile &task) {
    const TileKey key = task.request.key;
    {
        std::lock_guard<SpinMutex> lock(state_mutex_);
        const auto found = records_.find(key);
        if (found == records_.end() || found->second.state != TileState::Queued)
            return;
        if (!wanted_locked(key)) {
            found->second.state = TileState::Missing;
            return;
        }
        found->second.state = TileState::DiskLookup;
        active_key_ = key;
        active_valid_ = true;
        cancel_flag_ = 0;
    }

    std::vector<std::uint8_t> encoded;
    const bool from_memory = memory_cache_.get(key, encoded);
    bool from_disk = false;
    bool from_network = false;
    if (from_memory) {
        std::lock_guard<SpinMutex> lock(state_mutex_);
        ++counters_.memory_hits;
    } else {
        from_disk = disk_cache_.read(key, encoded);
        if (from_disk) {
            std::lock_guard<SpinMutex> lock(state_mutex_);
            ++counters_.disk_hits;
        }
    }

    if (!still_wanted(key)) {
        set_state(key, TileState::Missing);
        std::lock_guard<SpinMutex> lock(state_mutex_);
        active_valid_ = false;
        return;
    }

    if (encoded.empty()) {
        if (!network_available_) {
            set_state(key, TileState::Failed, VITA_HTTPS_ERROR_NOT_INITIALIZED);
            std::lock_guard<SpinMutex> lock(state_mutex_);
            active_valid_ = false;
            return;
        }
        set_state(key, TileState::Downloading);
        long status = 0;
        const int result = http_.download(
            provider_.tile_url(key.provider, key.zoom, key.x, key.y),
            &cancel_flag_, encoded,
            status);
        if (result < 0 || cancel_flag_ || !still_wanted(key)) {
            if (result < 0 && !cancel_flag_) {
                log_printf("tile network failed z=%d x=%d y=%d result=0x%08X http=%ld",
                           key.zoom, key.x, key.y,
                           static_cast<unsigned>(result), status);
            }
            set_state(key, cancel_flag_ ? TileState::Missing : TileState::Failed,
                      result);
            std::lock_guard<SpinMutex> lock(state_mutex_);
            active_valid_ = false;
            return;
        }
        from_network = true;
        {
            std::lock_guard<SpinMutex> lock(state_mutex_);
            ++counters_.downloaded_tiles;
        }
        set_state(key, TileState::Downloaded);
    }

    set_state(key, TileState::Decoding);
    DecodedTile decoded;
    decoded.key = key;
    decoded.generation = task.generation;
    std::string decode_error;
    if (!decode_png_rgba(encoded, provider_.tile_size(), decoded.width,
                         decoded.height, decoded.rgba, decode_error)) {
        log_printf("tile decode failed z=%d x=%d y=%d source=%s error=%s",
                   key.zoom, key.x, key.y,
                   from_disk ? "disk" : from_memory ? "ram" : "network",
                   decode_error.c_str());
        if (from_disk) disk_cache_.erase(key);
        set_state(key, TileState::Failed, -2001);
        std::lock_guard<SpinMutex> lock(state_mutex_);
        active_valid_ = false;
        return;
    }

    memory_cache_.put(key, encoded);
    if (from_network && !disk_cache_.write(key, encoded)) {
        log_printf("tile cache write failed z=%d x=%d y=%d", key.zoom, key.x,
                   key.y);
    }
    if (!still_wanted(key)) {
        set_state(key, TileState::Missing);
    } else {
        std::lock_guard<SpinMutex> upload_lock(upload_mutex_);
        uploads_.push_back(std::move(decoded));
    }
    std::lock_guard<SpinMutex> lock(state_mutex_);
    active_valid_ = false;
}

} // namespace vitamaps
