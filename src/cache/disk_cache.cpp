#include "cache/disk_cache.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

namespace vitamaps {
namespace {
constexpr std::size_t kMaximumTileBytes = 4U * 1024U * 1024U;

bool cache_time(const SceDateTime &accessed, const SceDateTime &modified,
                std::uint64_t &tick) {
    SceRtcTick access_tick{};
    SceRtcTick file_tick{};
    const bool have_access = sceRtcGetTick(&accessed, &access_tick) >= 0;
    const bool have_modified = sceRtcGetTick(&modified, &file_tick) >= 0;
    if (!have_access && !have_modified) {
        tick = 0;
        return false;
    }
    tick = std::max(have_access ? access_tick.tick : 0U,
                    have_modified ? file_tick.tick : 0U);
    return true;
}

bool is_dot_entry(const char *name) {
    return std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0;
}
} // namespace

DiskCache::DiskCache(std::string root, std::uint64_t budget_bytes)
    : root_(std::move(root)), budget_bytes_(budget_bytes) {}

std::string DiskCache::path_for(const TileKey &key) const {
    char suffix[128];
    std::snprintf(suffix, sizeof(suffix), "/%u/%d/%d/%d.png", key.provider,
                  key.zoom, key.x, key.y);
    return root_ + suffix;
}

bool DiskCache::ensure_parent_directories(const TileKey &key) const {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%u", root_.c_str(), key.provider);
    sceIoMkdir(path, 0777);
    std::snprintf(path, sizeof(path), "%s/%u/%d", root_.c_str(), key.provider,
                  key.zoom);
    sceIoMkdir(path, 0777);
    std::snprintf(path, sizeof(path), "%s/%u/%d/%d", root_.c_str(),
                  key.provider, key.zoom, key.x);
    sceIoMkdir(path, 0777);
    SceIoStat stat{};
    return sceIoGetstat(path, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode);
}

bool DiskCache::read(const TileKey &key,
                     std::vector<std::uint8_t> &bytes) const {
    const std::string path = path_for(key);
    SceIoStat stat{};
    if (sceIoGetstat(path.c_str(), &stat) < 0 || stat.st_size <= 0 ||
        static_cast<std::uint64_t>(stat.st_size) > kMaximumTileBytes) {
        return false;
    }
    const SceUID file = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (file < 0) return false;
    bytes.resize(static_cast<std::size_t>(stat.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const SceSSize count = sceIoRead(
            file, bytes.data() + offset,
            static_cast<SceSize>(bytes.size() - offset));
        if (count <= 0) {
            bytes.clear();
            sceIoClose(file);
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    sceIoClose(file);
    // Refresh only metadata: a successful offline read is real LRU use and
    // must not rewrite the PNG payload or consume extra flash cycles.
    SceIoStat touched{};
    if (sceRtcGetCurrentClock(&touched.st_atime, 0) >= 0)
        sceIoChstat(path.c_str(), &touched, SCE_CST_AT);
    return true;
}

bool DiskCache::write(const TileKey &key,
                      const std::vector<std::uint8_t> &bytes) {
    if (bytes.empty() || bytes.size() > kMaximumTileBytes ||
        !ensure_parent_directories(key)) {
        return false;
    }
    const std::string path = path_for(key);
    SceIoStat previous_stat{};
    std::uint64_t previous_size =
        sceIoGetstat(path.c_str(), &previous_stat) >= 0
            ? static_cast<std::uint64_t>(previous_stat.st_size) : 0U;
    const std::string temporary = path + ".part";
    sceIoRemove(temporary.c_str());
    const SceUID file = sceIoOpen(temporary.c_str(),
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) return false;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const SceSSize count = sceIoWrite(
            file, bytes.data() + offset,
            static_cast<SceSize>(bytes.size() - offset));
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    const int sync_result = offset == bytes.size()
        ? sceIoSyncByFd(file, 0) : -1;
    const int close_result = sceIoClose(file);
    if (offset != bytes.size() || sync_result < 0 || close_result < 0) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    if (!reserve_provider_space(key.provider, path, previous_size,
                                static_cast<std::uint64_t>(bytes.size()))) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    sceIoRemove(path.c_str());
    if (sceIoRename(temporary.c_str(), path.c_str()) < 0) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    const std::uint64_t current = provider_bytes_[key.provider];
    provider_bytes_[key.provider] =
        (current >= previous_size ? current - previous_size : 0U) +
        static_cast<std::uint64_t>(bytes.size());
    if (++writes_since_scan_ >= 16U) {
        writes_since_scan_ = 0;
        enforce_provider_budget(key.provider);
    }
    return true;
}

void DiskCache::erase(const TileKey &key) const {
    sceIoRemove(path_for(key).c_str());
}

void DiskCache::scan_directory(const std::string &path, int depth,
                               std::vector<Entry> &entries) const {
    if (depth > 6) return;
    const SceUID directory = sceIoDopen(path.c_str());
    if (directory < 0) return;
    SceIoDirent item{};
    while (sceIoDread(directory, &item) > 0) {
        if (!is_dot_entry(item.d_name)) {
            const std::string child = path + "/" + item.d_name;
            if (SCE_S_ISDIR(item.d_stat.st_mode)) {
                scan_directory(child, depth + 1, entries);
            } else if (SCE_S_ISREG(item.d_stat.st_mode)) {
                const std::size_t length = child.size();
                if (length >= 4 && child.compare(length - 4, 4, ".png") == 0) {
                    std::uint64_t modified = 0;
                    cache_time(item.d_stat.st_atime, item.d_stat.st_mtime,
                               modified);
                    Entry entry;
                    entry.path = child;
                    entry.size = static_cast<std::uint64_t>(item.d_stat.st_size);
                    entry.modified = modified;
                    unsigned int provider = 0;
                    int zoom = 0;
                    int x = 0;
                    int y = 0;
                    const char *relative = child.c_str() + root_.size();
                    if (std::sscanf(relative, "/%u/%d/%d/%d.png", &provider,
                                    &zoom, &x, &y) == 4 && zoom >= 0 &&
                        zoom <= 22 && x >= 0 && y >= 0) {
                        entry.key = {provider, zoom, x, y};
                        entry.tile_key_valid = true;
                    }
                    entries.push_back(std::move(entry));
                }
            }
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
}

std::uint64_t DiskCache::enforce_entries_budget(
    std::vector<Entry> &entries) const {
    std::uint64_t total = 0;
    for (const auto &entry : entries) total += entry.size;
    if (total <= budget_bytes_) return total;
    std::sort(entries.begin(), entries.end(),
              [](const Entry &left, const Entry &right) {
                  return left.modified < right.modified;
              });
    for (const auto &entry : entries) {
        if (total <= budget_bytes_) break;
        if (sceIoRemove(entry.path.c_str()) >= 0) total -= entry.size;
    }
    return total;
}

bool DiskCache::reserve_provider_space(
    std::uint32_t provider, const std::string &replacement_path,
    std::uint64_t replacement_size, std::uint64_t incoming_size) {
    if (incoming_size > budget_bytes_) return false;
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%u", root_.c_str(), provider);
    std::vector<Entry> entries;
    scan_directory(path, 0, entries);
    std::uint64_t total = 0;
    for (const auto &entry : entries) total += entry.size;
    replacement_size = std::min(replacement_size, total);
    std::uint64_t final_size = total - replacement_size + incoming_size;
    std::sort(entries.begin(), entries.end(),
              [](const Entry &left, const Entry &right) {
                  return left.modified < right.modified;
              });
    for (const auto &entry : entries) {
        if (final_size <= budget_bytes_) break;
        if (entry.path == replacement_path) continue;
        if (sceIoRemove(entry.path.c_str()) >= 0) {
            total -= entry.size;
            final_size -= entry.size;
        }
    }
    provider_bytes_[provider] = total;
    return final_size <= budget_bytes_;
}

void DiskCache::enforce_provider_budget(std::uint32_t provider) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%u", root_.c_str(), provider);
    std::vector<Entry> entries;
    scan_directory(path, 0, entries);
    provider_bytes_[provider] = enforce_entries_budget(entries);
}

void DiskCache::enforce_budget() {
    // This method is called by the tile worker. Each style/provider owns an
    // independent namespace; one style can never evict another.
    sceIoMkdir("ux0:data/VitaMaps", 0777);
    sceIoMkdir(root_.c_str(), 0777);
    provider_bytes_.clear();
    const SceUID directory = sceIoDopen(root_.c_str());
    if (directory < 0) return;
    SceIoDirent item{};
    while (sceIoDread(directory, &item) > 0) {
        if (!is_dot_entry(item.d_name) && SCE_S_ISDIR(item.d_stat.st_mode)) {
            const std::string child = root_ + "/" + item.d_name;
            std::vector<Entry> entries;
            scan_directory(child, 0, entries);
            const unsigned long provider = std::strtoul(item.d_name, nullptr, 10);
            if (provider <= 0xFFFFFFFFUL)
                provider_bytes_[static_cast<std::uint32_t>(provider)] =
                    enforce_entries_budget(entries);
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
}

DiskCacheStatus DiskCache::status() const {
    DiskCacheStatus result;
    const SceUID directory = sceIoDopen(root_.c_str());
    if (directory < 0) return result;
    SceIoDirent item{};
    while (sceIoDread(directory, &item) > 0) {
        if (!is_dot_entry(item.d_name) && SCE_S_ISDIR(item.d_stat.st_mode)) {
            std::vector<Entry> entries;
            scan_directory(root_ + "/" + item.d_name, 0, entries);
            if (!entries.empty()) ++result.styles;
            for (const Entry &entry : entries) {
                result.bytes += entry.size;
                ++result.entries;
            }
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
    return result;
}

OfflineAtlasSnapshot DiskCache::atlas() const {
    OfflineAtlasSnapshot result;
    std::vector<Entry> entries;
    scan_directory(root_, 0, entries);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                 [](const Entry &entry) { return !entry.tile_key_valid; }),
                 entries.end());
    std::sort(entries.begin(), entries.end(),
              [](const Entry &left, const Entry &right) {
                  if (left.key.provider != right.key.provider)
                      return left.key.provider < right.key.provider;
                  if (left.key.zoom != right.key.zoom)
                      return left.key.zoom < right.key.zoom;
                  if (left.key.y != right.key.y)
                      return left.key.y < right.key.y;
                  return left.key.x < right.key.x;
              });
    for (const Entry &entry : entries) {
        if (result.layers.empty() ||
            result.layers.back().provider != entry.key.provider ||
            result.layers.back().zoom != entry.key.zoom) {
            result.layers.push_back({});
            result.layers.back().provider = entry.key.provider;
            result.layers.back().zoom = entry.key.zoom;
        }
        OfflineAtlasLayer &layer = result.layers.back();
        const double world = std::exp2(static_cast<double>(entry.key.zoom));
        // Bounds describe the complete cached tile footprint, not just the
        // centers. This keeps single-tile layers visible and lets the atlas
        // project every zoom level back onto the same Mercator reference.
        const double minimum_x = static_cast<double>(entry.key.x) / world;
        const double maximum_x = static_cast<double>(entry.key.x + 1) / world;
        const double minimum_y = static_cast<double>(entry.key.y) / world;
        const double maximum_y = static_cast<double>(entry.key.y + 1) / world;
        layer.minimum_x = std::min(layer.minimum_x, minimum_x);
        layer.maximum_x = std::max(layer.maximum_x, maximum_x);
        layer.minimum_y = std::min(layer.minimum_y, minimum_y);
        layer.maximum_y = std::max(layer.maximum_y, maximum_y);
        ++layer.tiles;
        layer.bytes += entry.size;
        result.status.bytes += entry.size;
        ++result.status.entries;
    }
    std::size_t entry_index = 0;
    for (OfflineAtlasLayer &layer : result.layers) {
        constexpr std::size_t kMaximumSamples = 220U;
        const std::size_t stride = std::max<std::size_t>(
            1U, (static_cast<std::size_t>(layer.tiles) +
                 kMaximumSamples - 1U) / kMaximumSamples);
        layer.samples.reserve(std::min<std::size_t>(layer.tiles,
                                                    kMaximumSamples));
        layer.tile_index.reserve(layer.tiles);
        for (std::size_t ordinal = 0; ordinal < layer.tiles;
             ++ordinal, ++entry_index) {
            const Entry &entry = entries[entry_index];
            layer.tile_index.push_back({entry.key.x, entry.key.y});
            if (ordinal % stride != 0U) continue;
            const double world = std::exp2(
                static_cast<double>(entry.key.zoom));
            layer.samples.push_back({
                static_cast<float>((static_cast<double>(entry.key.x) + 0.5) /
                                   world),
                static_cast<float>((static_cast<double>(entry.key.y) + 0.5) /
                                   world)});
        }
    }
    std::uint32_t previous_provider = 0;
    for (const auto &layer : result.layers) {
        if (layer.provider != previous_provider) {
            ++result.status.styles;
            previous_provider = layer.provider;
        }
    }
    return result;
}

int DiskCache::clear_directory(const std::string &path, int depth,
                               bool remove_directory) const {
    if (depth > 7) return -1;
    const SceUID directory = sceIoDopen(path.c_str());
    if (directory < 0) return 0;
    int result = 0;
    SceIoDirent item{};
    while (sceIoDread(directory, &item) > 0) {
        if (!is_dot_entry(item.d_name)) {
            const std::string child = path + "/" + item.d_name;
            int operation = 0;
            if (SCE_S_ISDIR(item.d_stat.st_mode))
                operation = clear_directory(child, depth + 1, true);
            else if (SCE_S_ISREG(item.d_stat.st_mode))
                operation = sceIoRemove(child.c_str());
            if (result == 0 && operation < 0) result = operation;
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
    if (remove_directory) {
        const int removed = sceIoRmdir(path.c_str());
        if (result == 0 && removed < 0) result = removed;
    }
    return result;
}

int DiskCache::clear_all() {
    const int result = clear_directory(root_, 0, false);
    provider_bytes_.clear();
    writes_since_scan_ = 0;
    sceIoMkdir(root_.c_str(), 0777);
    return result;
}

} // namespace vitamaps
