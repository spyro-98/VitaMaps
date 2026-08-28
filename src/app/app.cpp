#include "app/app.h"

#include "core/log.h"
#include "settings/preferences.h"
#include "ui/localization.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>
#include <vita_https.h>

#include <algorithm>
#include <cstdint>
#include <exception>

#ifndef VITAMAPS_BUILD_LABEL
#define VITAMAPS_BUILD_LABEL "Unknown"
#endif

namespace vitamaps {

App::~App() { shutdown(); }

void App::record_failure(const char *stage, int error_code,
                         const char *detail) {
    failure_stage_ = stage ? stage : "unknown";
    failure_detail_ = detail ? detail : "";
    failure_code_ = error_code;
    log_printf("FATAL stage=%s code=0x%08X detail=%s", failure_stage_.c_str(),
               static_cast<unsigned int>(failure_code_),
               failure_detail_.empty() ? "-" : failure_detail_.c_str());
    log_save();
}

bool App::initialize() {
    ui_localization_init(0);
    log_init();
    // Before the first frame, use the compile-time default without touching
    // ux0. If graphics fails, a Debug build can still persist the fatal log.
    log_set_disk_enabled(preferences_debug_default());
    log_printf("VitaMaps bootstrap build=%s default_disk_logs=%d",
               VITAMAPS_BUILD_LABEL, log_disk_enabled() ? 1 : 0);

    const int graphics = vita2d_init();
    log_printf("vita2d_init -> 0x%08X context=%p framebuffer=%p",
               static_cast<unsigned int>(graphics), vita2d_get_context(),
               vita2d_get_current_fb());
    if (graphics <= 0 || !vita2d_get_context() || !vita2d_get_current_fb()) {
        record_failure("vita2d_init", graphics);
        return false;
    }
    vita2d_initialized_ = true;
    vita2d_set_clear_color(RGBA8(28, 35, 43, 255));

    // Present a frame before font, disk scanning, TLS, or worker startup. A
    // device that reaches this point must never look like an instant exit.
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                          RGBA8(16, 24, 33, 255));
    vita2d_draw_rectangle(240.0F, 260.0F, 480.0F, 4.0F,
                          RGBA8(43, 67, 85, 255));
    vita2d_draw_rectangle(240.0F, 260.0F, 120.0F, 4.0F,
                          RGBA8(88, 190, 255, 255));
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
    log_printf("first boot frame presented");

    const int preferences_result = preferences_init();
    ui_localization_init(preferences_ui_language());
    log_set_disk_enabled(preferences_disk_logs_enabled());
    log_printf("preferences -> 0x%08X disk_logs=%d",
               static_cast<unsigned int>(preferences_result),
               log_disk_enabled() ? 1 : 0);
    if (!provider_.set_style(preferences_map_style())) {
        provider_.set_style(0);
        preferences_set_map_style(0);
    }
    // OpenTopoMap is the key-free hiking style. Persisted hiking mode always
    // restores it; directly selecting that style also enables the matching
    // hiking tools so map semantics and controls cannot drift apart.
    constexpr int kOpenTopoMapStyle = 4;
    if (preferences_hiking_mode() &&
        provider_.style_index() != kOpenTopoMapStyle) {
        provider_.set_style(kOpenTopoMapStyle);
        preferences_set_map_style(kOpenTopoMapStyle);
    } else if (provider_.style_index() == kOpenTopoMapStyle &&
               !preferences_hiking_mode()) {
        preferences_set_hiking_mode(true);
    }
    log_printf("map style: index=%d id=%u name=%s",
               provider_.style_index(),
               static_cast<unsigned>(provider_.id()), provider_.name());
    log_save();

    const int pgf_result = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
    pgf_available_ = pgf_result >= 0;
    log_printf("sceSysmoduleLoadModule(PGF) -> 0x%08X",
               static_cast<unsigned int>(pgf_result));

    const int https_result = vita_https_init();
    https_initialized_ = https_result >= 0;
    log_printf("vita_https_init -> 0x%08X", static_cast<unsigned>(https_result));
    try {
        manager_ = std::make_unique<TileManager>(provider_);
        const int worker_result = manager_->start(https_initialized_);
        if (worker_result < 0) {
            record_failure("tile_worker_start", worker_result);
            return false;
        }
        renderer_ = std::make_unique<MapRenderer>(provider_, *manager_);
        screen_ = std::make_unique<MapScreen>(provider_, *manager_, *renderer_,
                                              https_initialized_, pgf_available_);
        if (!screen_->initialize()) {
            record_failure("map_screen_init", -1);
            return false;
        }
    } catch (const std::exception &error) {
        record_failure("startup_exception", -1, error.what());
        return false;
    } catch (...) {
        record_failure("startup_exception", -2, "unknown exception");
        return false;
    }
    log_printf("startup complete");
    log_save();
    return true;
}

int App::run() {
    if (!screen_) return -1;
    bool quit = false;
    std::uint64_t frame = 1;
    std::uint64_t previous_time = sceKernelGetProcessTimeWide();
    while (!quit) {
        const std::uint64_t now = sceKernelGetProcessTimeWide();
        const double dt = std::clamp((now - previous_time) / 1000000.0,
                                     1.0 / 240.0, 0.05);
        previous_time = now;

        screen_->update(dt, quit);
        screen_->prepare(frame);
        vita2d_start_drawing();
        vita2d_clear_screen();
        screen_->draw(frame);
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        ++frame;
    }
    log_printf("main loop exited normally frame=%llu",
               static_cast<unsigned long long>(frame));
    return 0;
}

void App::shutdown() {
    if (shutdown_complete_) return;
    shutdown_complete_ = true;
    if (manager_) manager_->stop();
    log_printf("shutdown begin");
    log_save();
    if (vita2d_initialized_) vita2d_wait_rendering_done();
    screen_.reset();
    renderer_.reset();
    manager_.reset();
    if (https_initialized_) {
        vita_https_shutdown();
        https_initialized_ = false;
    }
    if (vita2d_initialized_) {
        vita2d_fini();
        vita2d_initialized_ = false;
    }
}

} // namespace vitamaps
