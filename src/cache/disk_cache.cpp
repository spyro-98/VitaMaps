#include "cache/disk_cache.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

namespace vitamaps {
namespace {
constexpr std::size_t kMaximumTileBytes = 4U * 1024U * 1024U;

constexpr std::uint64_t kMinimumCacheSeconds = 7U * 24U * 60U * 60U;

bool cache_time(const SceDateTime &modified, std::uint64_t &tick,
                bool &protected_by_ttl) {
    SceRtcTick file_tick{};
    SceRtcTick current_tick{};
    if (sceRtcGetTick(&modified, &file_tick) < 0 ||
        sceRtcGetCurrentTick(&current_tick) < 0) {
        tick = 0;
        protected_by_ttl = true;
        return false;
    }
    tick = file_tick.tick;
    const std::uint64_t lifetime =
        kMinimumCacheSeconds * sceRtcGetTickResolution();
    protected_by_ttl = current_tick.tick <= file_tick.tick ||
                       current_tick.tick - file_tick.tick < lifetime;
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
    if (provider_bytes_.find(key.provider) == provider_bytes_.end())
        enforce_provider_budget(key.provider);
    std::uint64_t current = provider_bytes_[key.provider];
    const std::uint64_t retained = current >= previous_size
        ? current - previous_size : 0U;
    if (retained + bytes.size() > budget_bytes_) {
        enforce_provider_budget(key.provider);
        std::memset(&previous_stat, 0, sizeof(previous_stat));
        previous_size = sceIoGetstat(path.c_str(), &previous_stat) >= 0
            ? static_cast<std::uint64_t>(previous_stat.st_size) : 0U;
        current = provider_bytes_[key.provider];
        const std::uint64_t refreshed = current >= previous_size
            ? current - previous_size : 0U;
        // Recent provider tiles are protected for seven days. Do not exceed
        // the per-style cap just to admit a new entry when none is evictable.
        if (refreshed + bytes.size() > budget_bytes_) return false;
    }
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
    sceIoClose(file);
    if (offset != bytes.size()) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    sceIoRemove(path.c_str());
    if (sceIoRename(temporary.c_str(), path.c_str()) < 0) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    current = provider_bytes_[key.provider];
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
                    bool protected_by_ttl = true;
                    cache_time(item.d_stat.st_mtime, modified, protected_by_ttl);
                    entries.push_back({child,
                        static_cast<std::uint64_t>(item.d_stat.st_size),
                        modified, protected_by_ttl});
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
        if (entry.protected_by_ttl) continue;
        if (sceIoRemove(entry.path.c_str()) >= 0) total -= entry.size;
    }
    return total;
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
    // independent 96 MiB namespace; one style can never evict another.
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
