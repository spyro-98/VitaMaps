#include "system/fatal_screen.h"

#include <cstdint>

#include <debugScreen.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdio>
#include <cstring>

namespace vitamaps {

void fatal_screen_show(const char *stage, int error_code, const char *detail) {
    if (psvDebugScreenInit() < 0) {
        sceKernelDelayThread(10 * 1000 * 1000);
        return;
    }
    psvDebugScreenPuts("\x1b[2J\x1b[1;1H");
    psvDebugScreenPuts("VitaMaps - errore di avvio\n\n");
    psvDebugScreenPrintf("Stage: %s\n", stage ? stage : "sconosciuto");
    psvDebugScreenPrintf("Codice: 0x%08X\n", static_cast<unsigned>(error_code));
    if (detail && detail[0]) psvDebugScreenPrintf("Dettaglio: %s\n", detail);
    psvDebugScreenPuts("\nLog: ux0:data/VitaMaps/session_log.txt\n");
    psvDebugScreenPuts("\nPremi X, Cerchio o START per chiudere.");

    SceCtrlData previous{};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    sceCtrlPeekBufferPositive(0, &previous, 1);
    for (;;) {
        SceCtrlData controls{};
        if (sceCtrlPeekBufferPositive(0, &controls, 1) >= 0) {
            const unsigned int pressed = controls.buttons & ~previous.buttons;
            previous = controls;
            if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START))
                break;
        }
        sceKernelDelayThread(16 * 1000);
    }
    psvDebugScreenFinish();
}

} // namespace vitamaps
