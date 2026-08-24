#include "app/app.h"

#include "core/log.h"
#include "system/fatal_screen.h"

#include <psp2/kernel/processmgr.h>

#include <exception>
#include <string>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    vitamaps::App app;
    if (!app.initialize()) {
        const std::string stage = app.failure_stage();
        const std::string detail = app.failure_detail();
        const int code = app.failure_code();
        app.shutdown();
        vitamaps::fatal_screen_show(stage.c_str(), code, detail.c_str());
        sceKernelExitProcess(0);
        return 0;
    }
    int result = 0;
    try {
        result = app.run();
    } catch (const std::exception &error) {
        app.record_failure("main_loop_exception", -1, error.what());
        result = -1;
    } catch (...) {
        app.record_failure("main_loop_exception", -2, "unknown exception");
        result = -1;
    }
    if (result < 0) {
        const std::string stage = app.failure_stage();
        const std::string detail = app.failure_detail();
        const int code = app.failure_code();
        app.shutdown();
        vitamaps::fatal_screen_show(stage.c_str(), code, detail.c_str());
    } else {
        app.shutdown();
    }
    sceKernelExitProcess(0);
    return 0;
}
