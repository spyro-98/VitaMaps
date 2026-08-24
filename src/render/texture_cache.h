#pragma once

#include "map/tile.h"

#include <vita2d.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vitamaps {

// L1 cache. All methods must be called on the render thread.
class TextureCache {
public:
    explicit TextureCache(std::size_t budget_bytes);
    ~TextureCache();
    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;

    vita2d_texture *find(const TileKey &key, std::uint64_t frame);
    bool insert(const DecodedTile &tile, std::uint64_t frame,
                std::vector<TileKey> &evicted);
    void clear();
    std::size_t used_bytes() const { return used_bytes_; }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        vita2d_texture *texture{nullptr};
        std::size_t bytes{0};
        std::uint64_t last_used_frame{0};
    };

    bool reserve(std::size_t required, std::uint64_t frame,
                 std::vector<TileKey> &evicted);

    std::size_t budget_bytes_{0};
    std::size_t used_bytes_{0};
    std::unordered_map<TileKey, Entry, TileKeyHash> entries_;
};

} // namespace vitamaps
