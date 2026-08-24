#include "map/tile_scheduler.h"

namespace vitamaps {

void TileScheduler::push(const TileRequest &request,
                         std::uint64_t generation) {
    std::lock_guard<SpinMutex> lock(mutex_);
    if (stopping_) return;
    queue_.push({request, generation, sequence_++});
}

void TileScheduler::replace(const std::vector<TileRequest> &requests,
                            std::uint64_t generation) {
    {
        std::lock_guard<SpinMutex> lock(mutex_);
        if (stopping_) return;
        while (!queue_.empty()) queue_.pop();
        for (const auto &request : requests)
            queue_.push({request, generation, sequence_++});
    }
}

bool TileScheduler::wait_pop(ScheduledTile &task) {
    for (;;) {
        {
            std::lock_guard<SpinMutex> lock(mutex_);
            if (stopping_) return false;
            if (!queue_.empty()) {
                task = queue_.top();
                queue_.pop();
                return true;
            }
        }
        // One worker only. An 8 ms idle poll avoids the broken libstdc++
        // condition-variable activation probe and remains well below a frame.
        sceKernelDelayThread(8U * 1000U);
    }
}

bool TileScheduler::try_pop(ScheduledTile &task) {
    std::lock_guard<SpinMutex> lock(mutex_);
    if (stopping_ || queue_.empty()) return false;
    task = queue_.top();
    queue_.pop();
    return true;
}

void TileScheduler::shutdown() {
    {
        std::lock_guard<SpinMutex> lock(mutex_);
        stopping_ = true;
        while (!queue_.empty()) queue_.pop();
    }
}

std::size_t TileScheduler::size() const {
    std::lock_guard<SpinMutex> lock(mutex_);
    return queue_.size();
}

} // namespace vitamaps
