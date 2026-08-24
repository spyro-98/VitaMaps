#pragma once

#include "map/tile.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace vitamaps {

// L2 cache: compressed PNG payloads. It intentionally lives on the tile
// worker and therefore needs no locking or duplicate pixel allocation.
class MemoryCache {
public:
    explicit MemoryCache(std::size_t budget_bytes);

    bool get(const TileKey &key, std::vector<std::uint8_t> &bytes);
    void put(const TileKey &key, std::vector<std::uint8_t> bytes);
    void clear();
    std::size_t used_bytes() const { return used_bytes_; }
    std::size_t entry_count() const { return entries_.size(); }

private:
    struct Entry {
        std::vector<std::uint8_t> bytes;
        std::list<TileKey>::iterator lru;
    };

    void touch(Entry &entry, const TileKey &key);
    void trim();

    std::size_t budget_bytes_{0};
    std::size_t used_bytes_{0};
    std::list<TileKey> lru_;
    std::unordered_map<TileKey, Entry, TileKeyHash> entries_;
};

} // namespace vitamaps
