#pragma once

#include <psp2/kernel/threadmgr.h>

namespace vitamaps {

// VitaSDK's libstdc++ checks for pthread_cancel through a weak symbol before
// enabling its gthread layer. With the static Vita pthread archive that check
// can report "single threaded" even though pthread_create is present. Keep the
// small map-engine critical sections independent from that runtime probe.
class SpinMutex {
public:
    SpinMutex() = default;
    SpinMutex(const SpinMutex &) = delete;
    SpinMutex &operator=(const SpinMutex &) = delete;

    void lock() {
        while (__sync_lock_test_and_set(&locked_, 1))
            sceKernelDelayThread(100);
    }

    void unlock() { __sync_lock_release(&locked_); }

private:
    volatile int locked_{0};
};

} // namespace vitamaps
