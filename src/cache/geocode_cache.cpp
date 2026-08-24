#include "cache/geocode_cache.h"

#include "app/app_paths.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace vitamaps {
namespace {
constexpr unsigned char kMagic[8] = {'V', 'M', 'G', 'E', 'O', '0', '0', '1'};
constexpr std::uint32_t kVersion = 1U;
constexpr std::size_t kMaximumQueryBytes = 191U;
constexpr std::size_t kMaximumNameBytes = 511U;
constexpr std::size_t kMaximumEntries = 128U;

struct CacheHeader {
    unsigned char magic[8];
    std::uint32_t version;
    std::uint32_t query_size;
    std::uint32_t name_size;
    std::uint32_t checksum;
    std::uint64_t query_hash;
    double latitude;
    double longitude;
};

struct FileEntry {
    std::string path;
    std::uint64_t modified{0};
};

std::uint32_t crc32_update(std::uint32_t crc, const unsigned char *data,
                           std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-
                static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

std::uint32_t checksum(const CacheHeader &header, const std::string &query,
                       const std::string &name) {
    CacheHeader copy = header;
    copy.checksum = 0;
    std::uint32_t crc = crc32_update(
        0xFFFFFFFFU, reinterpret_cast<const unsigned char *>(&copy),
        sizeof(copy));
    crc = crc32_update(crc,
        reinterpret_cast<const unsigned char *>(query.data()), query.size());
    crc = crc32_update(crc,
        reinterpret_cast<const unsigned char *>(name.data()), name.size());
    return ~crc;
}

bool read_all(SceUID file, void *data, std::size_t size) {
    auto *bytes = static_cast<unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoRead(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool write_all(SceUID file, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoWrite(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}
} // namespace

std::string GeocodeCache::normalize_query(const std::string &query) {
    std::string normalized;
    normalized.reserve(std::min(query.size(), kMaximumQueryBytes));
    bool pending_space = false;
    for (unsigned char byte : query) {
        if (std::isspace(byte)) {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space && normalized.size() < kMaximumQueryBytes)
            normalized.push_back(' ');
        pending_space = false;
        if (normalized.size() >= kMaximumQueryBytes) break;
        normalized.push_back(byte < 0x80U
            ? static_cast<char>(std::tolower(byte))
            : static_cast<char>(byte));
    }
    return normalized;
}

unsigned long long GeocodeCache::hash_query(const std::string &normalized) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : normalized) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string GeocodeCache::path_for(unsigned long long hash) {
    char path[192];
    std::snprintf(path, sizeof(path), VITAMAPS_GEOCODE_CACHE_DIR
                  "/%016llx.bin", hash);
    return path;
}

bool GeocodeCache::read(const std::string &query,
                        CachedGeocode &result) const {
    const std::string normalized = normalize_query(query);
    if (normalized.empty()) return false;
    const std::uint64_t hash = hash_query(normalized);
    const std::string path = path_for(hash);
    const SceUID file = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (file < 0) return false;
    CacheHeader header{};
    bool valid = read_all(file, &header, sizeof(header));
    valid = valid && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 &&
            header.version == kVersion && header.query_hash == hash &&
            header.query_size > 0 && header.query_size <= kMaximumQueryBytes &&
            header.name_size > 0 && header.name_size <= kMaximumNameBytes;
    std::string stored_query;
    std::string name;
    if (valid) {
        stored_query.resize(header.query_size);
        name.resize(header.name_size);
        valid = read_all(file, stored_query.data(), stored_query.size()) &&
                read_all(file, name.data(), name.size());
    }
    sceIoClose(file);
    valid = valid && stored_query == normalized &&
            header.checksum == checksum(header, stored_query, name) &&
            header.latitude >= -90.0 && header.latitude <= 90.0 &&
            header.longitude >= -180.0 && header.longitude <= 180.0;
    if (!valid) {
        sceIoRemove(path.c_str());
        return false;
    }
    result.point = {header.latitude, header.longitude};
    result.display_name = std::move(name);
    return true;
}

bool GeocodeCache::write(const std::string &query,
                         const CachedGeocode &result) const {
    const std::string normalized = normalize_query(query);
    std::string name = result.display_name;
    if (normalized.empty() || normalized.size() > kMaximumQueryBytes ||
        name.empty())
        return false;
    if (name.size() > kMaximumNameBytes) name.resize(kMaximumNameBytes);
    const std::uint64_t hash = hash_query(normalized);
    CacheHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.query_size = static_cast<std::uint32_t>(normalized.size());
    header.name_size = static_cast<std::uint32_t>(name.size());
    header.query_hash = hash;
    header.latitude = result.point.latitude;
    header.longitude = result.point.longitude;
    header.checksum = checksum(header, normalized, name);

    sceIoMkdir(VITAMAPS_DATA_DIR, 0777);
    sceIoMkdir(VITAMAPS_GEOCODE_CACHE_DIR, 0777);
    const std::string path = path_for(hash);
    const std::string temporary = path + ".tmp";
    sceIoRemove(temporary.c_str());
    const SceUID file = sceIoOpen(temporary.c_str(),
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (file < 0) return false;
    bool valid = write_all(file, &header, sizeof(header)) &&
                 write_all(file, normalized.data(), normalized.size()) &&
                 write_all(file, name.data(), name.size());
    if (valid) valid = sceIoSyncByFd(file, 0) >= 0;
    sceIoClose(file);
    if (!valid) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    sceIoRemove(path.c_str());
    if (sceIoRename(temporary.c_str(), path.c_str()) < 0) {
        sceIoRemove(temporary.c_str());
        return false;
    }
    enforce_entry_limit();
    return true;
}

void GeocodeCache::enforce_entry_limit() {
    const SceUID directory = sceIoDopen(VITAMAPS_GEOCODE_CACHE_DIR);
    if (directory < 0) return;
    std::vector<FileEntry> entries;
    SceIoDirent item{};
    while (sceIoDread(directory, &item) > 0) {
        const std::size_t length = std::strlen(item.d_name);
        if (SCE_S_ISREG(item.d_stat.st_mode) && length > 4 &&
            std::strcmp(item.d_name + length - 4, ".bin") == 0) {
            SceRtcTick tick{};
            sceRtcGetTick(&item.d_stat.st_mtime, &tick);
            entries.push_back({std::string(VITAMAPS_GEOCODE_CACHE_DIR) + "/" +
                                   item.d_name,
                               tick.tick});
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
    if (entries.size() <= kMaximumEntries) return;
    std::sort(entries.begin(), entries.end(),
              [](const FileEntry &left, const FileEntry &right) {
                  return left.modified < right.modified;
              });
    for (std::size_t index = 0;
         index < entries.size() - kMaximumEntries; ++index)
        sceIoRemove(entries[index].path.c_str());
}

} // namespace vitamaps
