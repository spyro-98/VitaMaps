#pragma once

#include <vita_https.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vitamaps {

class MapHttp {
public:
    MapHttp() = default;
    ~MapHttp();
    MapHttp(const MapHttp &) = delete;
    MapHttp &operator=(const MapHttp &) = delete;

    bool initialize();
    void shutdown();
    int download(const std::string &url, volatile int *cancel_flag,
                 std::vector<std::uint8_t> &bytes, long &status_code,
                 const char *const *headers = nullptr);

private:
    struct WriteContext {
        std::vector<std::uint8_t> *bytes{nullptr};
        std::size_t limit{0};
        volatile int *cancel_flag{nullptr};
    };
    static std::size_t write_callback(const void *data, std::size_t size,
                                      void *opaque);

    VitaHttpsClient *client_{nullptr};
};

} // namespace vitamaps
