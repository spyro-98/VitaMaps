#include "core/log.h"

#include "app/app_paths.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace vitamaps {
namespace {
constexpr std::size_t kLogCapacity = 256U * 1024U;
constexpr std::size_t kLineCapacity = 768U;

char g_buffer[kLogCapacity];
std::size_t g_length = 0;
volatile int g_lock = 0;
bool g_disk_enabled = false;

void lock_log() {
    while (__sync_lock_test_and_set(&g_lock, 1)) sceKernelDelayThread(100);
}

void unlock_log() { __sync_lock_release(&g_lock); }

void make_room(std::size_t incoming) {
    if (incoming >= kLogCapacity) incoming = kLogCapacity - 1U;
    if (g_length + incoming < kLogCapacity) return;
    const std::size_t required = g_length + incoming - kLogCapacity + 1U;
    std::size_t remove = required;
    while (remove < g_length && g_buffer[remove - 1U] != '\n') ++remove;
    if (remove > g_length) remove = g_length;
    std::memmove(g_buffer, g_buffer + remove, g_length - remove);
    g_length -= remove;
    g_buffer[g_length] = '\0';
}

int write_all(SceUID file, const char *bytes, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoWrite(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return count < 0 ? static_cast<int>(count) : -1;
        offset += static_cast<std::size_t>(count);
    }
    return 0;
}
} // namespace

void log_init() {
    g_lock = 0;
    g_length = 0;
    g_disk_enabled = false;
    g_buffer[0] = '\0';
}

void log_set_disk_enabled(bool enabled) {
    lock_log();
    g_disk_enabled = enabled;
    unlock_log();
}

bool log_disk_enabled() {
    lock_log();
    const bool enabled = g_disk_enabled;
    unlock_log();
    return enabled;
}

void log_printf(const char *format, ...) {
    if (!format) return;
    char line[kLineCapacity];
    va_list arguments;
    va_start(arguments, format);
    int written = std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (written < 0) return;
    std::size_t length = std::min(static_cast<std::size_t>(written),
                                  sizeof(line) - 2U);
    if (length == 0 || line[length - 1U] != '\n') line[length++] = '\n';
    line[length] = '\0';

    lock_log();
    make_room(length);
    std::memcpy(g_buffer + g_length, line, length);
    g_length += length;
    g_buffer[g_length] = '\0';
    unlock_log();
}

int log_save() {
    lock_log();
    if (!g_disk_enabled) {
        unlock_log();
        return 0;
    }
    sceIoMkdir(VITAMAPS_DATA_DIR, 0777);
    const char *temporary = VITAMAPS_DATA_DIR "/session_log.tmp";
    const char *backup = VITAMAPS_DATA_DIR "/session_log.bak";
    sceIoRemove(temporary);
    const SceUID file = sceIoOpen(temporary,
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) {
        unlock_log();
        return static_cast<int>(file);
    }
    int result = write_all(file, g_buffer, g_length);
    if (result == 0) result = sceIoSyncByFd(file, 0);
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0) result = close_result;
    if (result < 0) {
        sceIoRemove(temporary);
        unlock_log();
        return result;
    }

    SceIoStat previous{};
    const bool had_previous =
        sceIoGetstat(VITAMAPS_SESSION_LOG_PATH, &previous) >= 0;
    sceIoRemove(backup);
    if (had_previous && sceIoRename(VITAMAPS_SESSION_LOG_PATH, backup) < 0) {
        sceIoRemove(temporary);
        unlock_log();
        return -1;
    }
    result = sceIoRename(temporary, VITAMAPS_SESSION_LOG_PATH);
    if (result < 0) {
        if (had_previous) sceIoRename(backup, VITAMAPS_SESSION_LOG_PATH);
        sceIoRemove(temporary);
    } else {
        sceIoSync("ux0:", 0);
        if (had_previous) sceIoRemove(backup);
    }
    unlock_log();
    return result;
}

std::size_t log_size() {
    lock_log();
    const std::size_t size = g_length;
    unlock_log();
    return size;
}

const char *log_path() { return VITAMAPS_SESSION_LOG_PATH; }

} // namespace vitamaps
