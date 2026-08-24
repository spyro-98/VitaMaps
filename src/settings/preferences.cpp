#include "settings/preferences.h"

#include "app/app_paths.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vitamaps {
namespace {
constexpr std::uint32_t kPreferencesVersion = 1U;
constexpr std::uint32_t kDiskLogs = 1U << 0U;
// Absence means the requested default: a HUD that auto-hides after 2.5 s.
constexpr std::uint32_t kHudAlwaysVisible = 1U << 1U;
constexpr std::uint32_t kCrosshair = 1U << 2U;
// Absence means metric units.
constexpr std::uint32_t kImperialUnits = 1U << 3U;
// Absence keeps the VitaTube-style motion system enabled by default.
constexpr std::uint32_t kReduceMotion = 1U << 4U;
constexpr unsigned int kMapStyleShift = 8U;
constexpr std::uint32_t kMapStyleMask = 0x0FU << kMapStyleShift;
constexpr unsigned int kUiLanguageShift = 12U;
constexpr std::uint32_t kUiLanguageMask = 0x0FU << kUiLanguageShift;
constexpr char kTemporaryPath[] = VITAMAPS_DATA_DIR "/settings.tmp";
constexpr char kBackupPath[] = VITAMAPS_DATA_DIR "/settings.bak";
constexpr unsigned char kMagic[8] = {'V', 'M', 'S', 'E', 'T', '0', '0', '1'};

struct PreferencesDisk {
    unsigned char magic[8];
    std::uint32_t version;
    std::uint32_t record_size;
    std::uint32_t flags;
    std::uint32_t checksum;
};

static_assert(sizeof(PreferencesDisk) == 24,
              "preferences layout must remain stable");

bool g_loaded = false;
std::uint32_t g_flags = 0;

std::uint32_t crc32(const unsigned char *data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-
                    static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

std::uint32_t checksum(const PreferencesDisk &record) {
    PreferencesDisk copy = record;
    copy.checksum = 0;
    return crc32(reinterpret_cast<const unsigned char *>(&copy), sizeof(copy));
}

int read_all(SceUID file, void *data, std::size_t size) {
    auto *bytes = static_cast<unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoRead(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return count < 0 ? static_cast<int>(count) : -1;
        offset += static_cast<std::size_t>(count);
    }
    return 0;
}

int write_all(SceUID file, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoWrite(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return count < 0 ? static_cast<int>(count) : -1;
        offset += static_cast<std::size_t>(count);
    }
    return 0;
}

int load_path(const char *path, std::uint32_t &flags) {
    const SceUID file = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (file < 0) return static_cast<int>(file);
    PreferencesDisk record{};
    int result = read_all(file, &record, sizeof(record));
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0) result = close_result;
    if (result == 0 &&
        (std::memcmp(record.magic, kMagic, sizeof(kMagic)) != 0 ||
         record.version != kPreferencesVersion ||
         record.record_size != sizeof(record) ||
         record.checksum != checksum(record))) {
        result = -2;
    }
    if (result == 0) flags = record.flags;
    return result;
}

bool path_exists(const char *path) {
    SceIoStat stat{};
    return sceIoGetstat(path, &stat) >= 0;
}

int persist(std::uint32_t flags) {
    PreferencesDisk record{};
    std::memcpy(record.magic, kMagic, sizeof(kMagic));
    record.version = kPreferencesVersion;
    record.record_size = sizeof(record);
    record.flags = flags;
    record.checksum = checksum(record);

    sceIoMkdir(VITAMAPS_DATA_DIR, 0777);
    sceIoRemove(kTemporaryPath);
    const SceUID file = sceIoOpen(kTemporaryPath,
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) return static_cast<int>(file);
    int result = write_all(file, &record, sizeof(record));
    if (result == 0) result = sceIoSyncByFd(file, 0);
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0) result = close_result;
    if (result < 0) {
        sceIoRemove(kTemporaryPath);
        return result;
    }

    sceIoRemove(kBackupPath);
    const bool had_previous = path_exists(VITAMAPS_SETTINGS_PATH);
    if (had_previous &&
        sceIoRename(VITAMAPS_SETTINGS_PATH, kBackupPath) < 0) {
        sceIoRemove(kTemporaryPath);
        return -1;
    }
    result = sceIoRename(kTemporaryPath, VITAMAPS_SETTINGS_PATH);
    if (result < 0) {
        if (had_previous) sceIoRename(kBackupPath, VITAMAPS_SETTINGS_PATH);
        sceIoRemove(kTemporaryPath);
        return result;
    }
    sceIoSync("ux0:", 0);
    if (had_previous) sceIoRemove(kBackupPath);
    return 0;
}
} // namespace

bool preferences_debug_default() {
#if defined(VITAMAPS_DEBUG_BUILD)
    return true;
#else
    return false;
#endif
}

int preferences_init() {
    g_flags = preferences_debug_default() ? kDiskLogs : 0U;
    g_loaded = true;
    std::uint32_t loaded_flags = 0;
    int result = load_path(VITAMAPS_SETTINGS_PATH, loaded_flags);
    if (result == 0) {
        g_flags = loaded_flags;
        return 0;
    }
    if (load_path(kBackupPath, loaded_flags) == 0) {
        g_flags = loaded_flags;
        return 0;
    }
    return path_exists(VITAMAPS_SETTINGS_PATH) ? result : 0;
}

bool preferences_disk_logs_enabled() {
    if (!g_loaded) preferences_init();
    return (g_flags & kDiskLogs) != 0;
}

int preferences_set_disk_logs_enabled(bool enabled) {
    if (!g_loaded) preferences_init();
    const std::uint32_t next =
        enabled ? (g_flags | kDiskLogs) : (g_flags & ~kDiskLogs);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

int preferences_map_style() {
    if (!g_loaded) preferences_init();
    return static_cast<int>((g_flags & kMapStyleMask) >> kMapStyleShift);
}

int preferences_set_map_style(int index) {
    if (!g_loaded) preferences_init();
    if (index < 0 || index > 15) return -1;
    const std::uint32_t next =
        (g_flags & ~kMapStyleMask) |
        (static_cast<std::uint32_t>(index) << kMapStyleShift);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

int preferences_ui_language() {
    if (!g_loaded) preferences_init();
    return static_cast<int>((g_flags & kUiLanguageMask) >> kUiLanguageShift);
}

int preferences_set_ui_language(int index) {
    if (!g_loaded) preferences_init();
    if (index < 0 || index > 7) return -1;
    const std::uint32_t next =
        (g_flags & ~kUiLanguageMask) |
        (static_cast<std::uint32_t>(index) << kUiLanguageShift);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

bool preferences_hud_auto_hide() {
    if (!g_loaded) preferences_init();
    return (g_flags & kHudAlwaysVisible) == 0;
}

int preferences_set_hud_auto_hide(bool enabled) {
    if (!g_loaded) preferences_init();
    const std::uint32_t next = enabled
        ? (g_flags & ~kHudAlwaysVisible)
        : (g_flags | kHudAlwaysVisible);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

bool preferences_crosshair_enabled() {
    if (!g_loaded) preferences_init();
    return (g_flags & kCrosshair) != 0;
}

int preferences_set_crosshair_enabled(bool enabled) {
    if (!g_loaded) preferences_init();
    const std::uint32_t next =
        enabled ? (g_flags | kCrosshair) : (g_flags & ~kCrosshair);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

bool preferences_imperial_units() {
    if (!g_loaded) preferences_init();
    return (g_flags & kImperialUnits) != 0;
}

int preferences_set_imperial_units(bool enabled) {
    if (!g_loaded) preferences_init();
    const std::uint32_t next =
        enabled ? (g_flags | kImperialUnits) : (g_flags & ~kImperialUnits);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

bool preferences_reduce_motion() {
    if (!g_loaded) preferences_init();
    return (g_flags & kReduceMotion) != 0;
}

int preferences_set_reduce_motion(bool enabled) {
    if (!g_loaded) preferences_init();
    const std::uint32_t next =
        enabled ? (g_flags | kReduceMotion) : (g_flags & ~kReduceMotion);
    if (next == g_flags) return 0;
    const int result = persist(next);
    if (result == 0) g_flags = next;
    return result;
}

} // namespace vitamaps
