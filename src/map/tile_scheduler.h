#pragma once

#include "core/spin_mutex.h"
#include "map/tile.h"

#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

namespace vitamaps {

struct ScheduledTile {
    TileRequest request;
    std::uint64_t generation{0};
    std::uint64_t sequence{0};
};

class TileScheduler {
public:
    void push(const TileRequest &request, std::uint64_t generation);
    void replace(const std::vector<TileRequest> &requests,
                 std::uint64_t generation);
    bool try_pop(ScheduledTile &task);
    bool wait_pop(ScheduledTile &task);
    void shutdown();
    std::size_t size() const;

private:
    struct LowerPriority {
        bool operator()(const ScheduledTile &left,
                        const ScheduledTile &right) const {
            if (left.request.priority != right.request.priority)
                return left.request.priority > right.request.priority;
            return left.sequence > right.sequence;
        }
    };

    mutable SpinMutex mutex_;
    std::priority_queue<ScheduledTile, std::vector<ScheduledTile>, LowerPriority>
        queue_;
    std::uint64_t sequence_{0};
    bool stopping_{false};
};

} // namespace vitamaps
