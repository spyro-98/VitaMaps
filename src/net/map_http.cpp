#include "net/map_http.h"

namespace vitamaps {
namespace {
constexpr std::size_t kMaximumResponseBytes = 4U * 1024U * 1024U;
}

MapHttp::~MapHttp() { shutdown(); }

bool MapHttp::initialize() {
    if (client_) return true;
    VitaHttpsClientConfig config{};
    config.user_agent =
        "VitaMaps/0.1 (PS Vita homebrew; https://github.com/spyro-98/VitaMaps)";
    config.connect_timeout_ms = 10000;
    config.request_timeout_ms = 25000;
    config.low_speed_bytes_per_second = 128;
    config.low_speed_seconds = 12;
    client_ = vita_https_client_create(&config);
    return client_ != nullptr;
}

void MapHttp::shutdown() {
    if (!client_) return;
    vita_https_client_destroy(client_);
    client_ = nullptr;
}

std::size_t MapHttp::write_callback(const void *data, std::size_t size,
                                    void *opaque) {
    auto *context = static_cast<WriteContext *>(opaque);
    if (!context || !context->bytes || !data ||
        (context->cancel_flag && *context->cancel_flag)) {
        return 0;
    }
    if (context->bytes->size() > context->limit ||
        size > context->limit - context->bytes->size()) {
        return 0;
    }
    const auto *begin = static_cast<const std::uint8_t *>(data);
    context->bytes->insert(context->bytes->end(), begin, begin + size);
    return size;
}

int MapHttp::download(const std::string &url, volatile int *cancel_flag,
                      std::vector<std::uint8_t> &bytes, long &status_code,
                      const char *const *headers) {
    bytes.clear();
    status_code = 0;
    if (!client_ || url.empty()) return VITA_HTTPS_ERROR_NOT_INITIALIZED;
    WriteContext context{&bytes, kMaximumResponseBytes, cancel_flag};
    VitaHttpsRequest request{};
    request.method = "GET";
    request.url = url.c_str();
    request.headers = headers;
    request.write = &MapHttp::write_callback;
    request.write_opaque = &context;
    request.cancel_flag = cancel_flag;
    VitaHttpsResponse response{};
    const int result = vita_https_perform(client_, &request, &response);
    status_code = response.status_code;
    if (result < 0) bytes.clear();
    return result;
}

} // namespace vitamaps
