#pragma once

#include "map/tile_manager.h"
#include "providers/osm_provider.h"
#include "render/map_renderer.h"
#include "ui/map_screen.h"

#include <memory>
#include <string>

namespace vitamaps {

class App {
public:
    App() = default;
    ~App();

    bool initialize();
    int run();
    void shutdown();
    void record_failure(const char *stage, int error_code,
                        const char *detail = nullptr);
    const char *failure_stage() const { return failure_stage_.c_str(); }
    const char *failure_detail() const { return failure_detail_.c_str(); }
    int failure_code() const { return failure_code_; }

private:
    OsmProvider provider_;
    std::unique_ptr<TileManager> manager_;
    std::unique_ptr<MapRenderer> renderer_;
    std::unique_ptr<MapScreen> screen_;
    bool vita2d_initialized_{false};
    bool https_initialized_{false};
    bool pgf_available_{false};
    bool shutdown_complete_{false};
    std::string failure_stage_;
    std::string failure_detail_;
    int failure_code_{0};
};

} // namespace vitamaps
