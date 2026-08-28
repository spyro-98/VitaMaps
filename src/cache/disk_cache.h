#pragma once

#include "map/tile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vitamaps {

struct DiskCacheStatus {
    std::uint64_t bytes{0};
    std::uint32_t entries{0};
    std::uint32_t styles{0};
};

struct OfflineAtlasPoint {
    float x{0.0F};
    float y{0.0F};
};

struct OfflineAtlasTile {
    int x{0};
    int y{0};
};

struct OfflineAtlasLayer {
    std::uint32_t provider{0};
    int zoom{0};
    std::uint32_t tiles{0};
    std::uint64_t bytes{0};
    double minimum_x{1.0};
    double maximum_x{0.0};
    double minimum_y{1.0};
    double maximum_y{0.0};
    std::vector<OfflineAtlasPoint> samples;
    // Complete compact tile index for cache-only navigation and rendering.
    // Entries are ordered by y then x by the worker scan.
    std::vector<OfflineAtlasTile> tile_index;
};

struct OfflineAtlasSnapshot {
    DiskCacheStatus status{};
    std::vector<OfflineAtlasLayer> layers;
};

// Persistent L3 cache. Writes are atomic (.part + rename). Entries have no
// time expiry and remain readable until the per-style budget requires an
// eviction; one provider can never evict another style's files.
class DiskCache {
public:
    DiskCache(std::string root, std::uint64_t budget_bytes);

    bool read(const TileKey &key, std::vector<std::uint8_t> &bytes) const;
    bool write(const TileKey &key, const std::vector<std::uint8_t> &bytes);
    void erase(const TileKey &key) const;
    void enforce_budget();
    DiskCacheStatus status() const;
    OfflineAtlasSnapshot atlas() const;
    int clear_all();
    const std::string &root() const { return root_; }

private:
    struct Entry {
        std::string path;
        std::uint64_t size{0};
        std::uint64_t modified{0};
        TileKey key{};
        bool tile_key_valid{false};
    };

    std::string path_for(const TileKey &key) const;
    bool ensure_parent_directories(const TileKey &key) const;
    void scan_directory(const std::string &path, int depth,
                        std::vector<Entry> &entries) const;
    int clear_directory(const std::string &path, int depth,
                        bool remove_directory) const;
    std::uint64_t enforce_entries_budget(std::vector<Entry> &entries) const;
    bool reserve_provider_space(std::uint32_t provider,
                                const std::string &replacement_path,
                                std::uint64_t replacement_size,
                                std::uint64_t incoming_size);
    void enforce_provider_budget(std::uint32_t provider);

    std::string root_;
    std::uint64_t budget_bytes_{0};
    std::unordered_map<std::uint32_t, std::uint64_t> provider_bytes_;
    unsigned int writes_since_scan_{0};
};

} // namespace vitamaps
