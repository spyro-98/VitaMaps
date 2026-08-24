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
    int clear_all();
    const std::string &root() const { return root_; }

private:
    struct Entry {
        std::string path;
        std::uint64_t size{0};
        std::uint64_t modified{0};
        bool protected_by_ttl{true};
    };

    std::string path_for(const TileKey &key) const;
    bool ensure_parent_directories(const TileKey &key) const;
    void scan_directory(const std::string &path, int depth,
                        std::vector<Entry> &entries) const;
    int clear_directory(const std::string &path, int depth,
                        bool remove_directory) const;
    std::uint64_t enforce_entries_budget(std::vector<Entry> &entries) const;
    void enforce_provider_budget(std::uint32_t provider);

    std::string root_;
    std::uint64_t budget_bytes_{0};
    std::unordered_map<std::uint32_t, std::uint64_t> provider_bytes_;
    unsigned int writes_since_scan_{0};
};

} // namespace vitamaps
