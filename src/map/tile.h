#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace vitamaps {

constexpr int kTileSize = 256;

struct TileKey {
    std::uint32_t provider{0};
    int zoom{0};
    int x{0};
    int y{0};

    bool operator==(const TileKey &other) const noexcept {
        return provider == other.provider && zoom == other.zoom &&
               x == other.x && y == other.y;
    }
    bool operator!=(const TileKey &other) const noexcept {
        return !(*this == other);
    }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey &key) const noexcept {
        std::size_t value = std::hash<std::uint32_t>{}(key.provider);
        value ^= std::hash<int>{}(key.zoom) + 0x9e3779b9U + (value << 6U) +
                 (value >> 2U);
        value ^= std::hash<int>{}(key.x) + 0x9e3779b9U + (value << 6U) +
                 (value >> 2U);
        value ^= std::hash<int>{}(key.y) + 0x9e3779b9U + (value << 6U) +
                 (value >> 2U);
        return value;
    }
};

enum class TileState {
    Missing,
    Queued,
    DiskLookup,
    Downloading,
    Downloaded,
    Decoding,
    Ready,
    Failed
};

struct TileRequest {
    TileKey key;
    float priority{0.0F}; // Lower values are processed first.
    bool visible{false};
};

struct DecodedTile {
    TileKey key;
    int width{0};
    int height{0};
    std::uint64_t generation{0};
    std::vector<std::uint8_t> rgba;
};

inline int wrap_tile_x(int x, int zoom) {
    const int count = 1 << zoom;
    int wrapped = x % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

} // namespace vitamaps
