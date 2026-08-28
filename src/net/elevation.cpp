#include "net/elevation.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace vitamaps {
namespace {
constexpr char kEndpoint[] = "https://api.open-meteo.com/v1/elevation";
constexpr std::size_t kMaximumPoints = 64U;
constexpr int kInvalidResponse = -4301;
constexpr int kHttpResponseError = -4302;

bool parse_elevations(const std::vector<std::uint8_t> &bytes,
                      std::size_t expected, std::vector<double> &meters) {
    const std::string json(bytes.begin(), bytes.end());
    std::size_t offset = json.find("\"elevation\"");
    if (offset == std::string::npos) return false;
    offset = json.find('[', offset);
    if (offset == std::string::npos) return false;
    ++offset;
    while (offset < json.size() && meters.size() < expected) {
        while (offset < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[offset])) ||
                json[offset] == ','))
            ++offset;
        if (offset >= json.size() || json[offset] == ']') break;
        errno = 0;
        char *end = nullptr;
        const double value = std::strtod(json.c_str() + offset, &end);
        if (errno != 0 || !end || end == json.c_str() + offset ||
            !std::isfinite(value) || value < -500.0 || value > 10000.0)
            return false;
        meters.push_back(value);
        offset = static_cast<std::size_t>(end - json.c_str());
    }
    return meters.size() == expected;
}
} // namespace

ElevationResult ElevationClient::lookup(
    MapHttp &http, const std::vector<mercator::GeoPoint> &points,
    volatile int *cancel_flag) const {
    ElevationResult result;
    if (points.empty() || points.size() > kMaximumPoints) {
        result.error = VITA_HTTPS_ERROR_INVALID_ARGUMENT;
        return result;
    }
    std::string latitudes;
    std::string longitudes;
    char number[40];
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0) {
            latitudes.push_back(',');
            longitudes.push_back(',');
        }
        std::snprintf(number, sizeof(number), "%.7f", points[index].latitude);
        latitudes += number;
        std::snprintf(number, sizeof(number), "%.7f", points[index].longitude);
        longitudes += number;
    }
    const std::string url = std::string(kEndpoint) + "?latitude=" + latitudes +
                            "&longitude=" + longitudes;
    const char *headers[] = {"Accept: application/json", nullptr};
    std::vector<std::uint8_t> bytes;
    result.error = http.download(url, cancel_flag, bytes, result.http_status,
                                 headers);
    if (result.error < 0) return result;
    if (result.http_status < 200 || result.http_status >= 300) {
        result.error = kHttpResponseError;
        return result;
    }
    if (!parse_elevations(bytes, points.size(), result.meters))
        result.error = kInvalidResponse;
    return result;
}

} // namespace vitamaps
