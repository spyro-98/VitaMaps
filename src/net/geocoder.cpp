#include "net/geocoder.h"

#include "app/app_paths.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita_https.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace vitamaps {
namespace {
constexpr char kDefaultEndpoint[] =
    "https://nominatim.openstreetmap.org/search";
constexpr unsigned long long kMinimumRemoteIntervalUs = 1000000ULL;
constexpr int kNotFound = -3101;
constexpr int kInvalidResponse = -3102;

void append_utf8(std::string &output, unsigned int codepoint) {
    if (codepoint <= 0x7FU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0x10FFFFU) {
        output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

bool parse_hex4(const std::string &json, std::size_t offset,
                unsigned int &value) {
    if (offset + 4U > json.size()) return false;
    value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        const int digit = hex_value(json[offset + index]);
        if (digit < 0) return false;
        value = (value << 4U) | static_cast<unsigned int>(digit);
    }
    return true;
}

bool json_string_field(const std::string &json, const char *field,
                       std::string &value) {
    const std::string key = std::string("\"") + field + "\"";
    std::size_t offset = json.find(key);
    if (offset == std::string::npos) return false;
    offset = json.find(':', offset + key.size());
    if (offset == std::string::npos) return false;
    ++offset;
    while (offset < json.size() &&
           std::isspace(static_cast<unsigned char>(json[offset])))
        ++offset;
    if (offset >= json.size() || json[offset++] != '"') return false;
    value.clear();
    while (offset < json.size()) {
        const char character = json[offset++];
        if (character == '"') return true;
        if (character != '\\') {
            value.push_back(character);
            continue;
        }
        if (offset >= json.size()) return false;
        const char escaped = json[offset++];
        switch (escaped) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back(' '); break;
        case 'r': value.push_back(' '); break;
        case 't': value.push_back(' '); break;
        case 'u': {
            unsigned int first = 0;
            if (!parse_hex4(json, offset, first)) return false;
            offset += 4U;
            unsigned int codepoint = first;
            if (first >= 0xD800U && first <= 0xDBFFU &&
                offset + 6U <= json.size() && json[offset] == '\\' &&
                json[offset + 1U] == 'u') {
                unsigned int second = 0;
                if (!parse_hex4(json, offset + 2U, second) ||
                    second < 0xDC00U || second > 0xDFFFU)
                    return false;
                offset += 6U;
                codepoint = 0x10000U + ((first - 0xD800U) << 10U) +
                            (second - 0xDC00U);
            } else if (first >= 0xD800U && first <= 0xDFFFU) {
                return false;
            }
            append_utf8(value, codepoint);
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool parse_number(const std::string &text, double &value) {
    errno = 0;
    char *end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return errno == 0 && end && *end == '\0' && std::isfinite(value);
}
} // namespace

std::string Geocoder::endpoint() {
    const SceUID file = sceIoOpen(VITAMAPS_GEOCODER_ENDPOINT_PATH,
                                  SCE_O_RDONLY, 0);
    if (file < 0) return kDefaultEndpoint;
    char buffer[512]{};
    const SceSSize count = sceIoRead(file, buffer, sizeof(buffer) - 1U);
    sceIoClose(file);
    if (count <= 0) return kDefaultEndpoint;
    std::string result(buffer, static_cast<std::size_t>(count));
    while (!result.empty() &&
           std::isspace(static_cast<unsigned char>(result.back())))
        result.pop_back();
    std::size_t first = 0;
    while (first < result.size() &&
           std::isspace(static_cast<unsigned char>(result[first])))
        ++first;
    result.erase(0, first);
    if (result.compare(0, 8, "https://") != 0) return kDefaultEndpoint;
    return result;
}

bool Geocoder::parse_response(const std::vector<std::uint8_t> &bytes,
                              CachedGeocode &result) {
    if (bytes.empty()) return false;
    const std::string json(bytes.begin(), bytes.end());
    std::string latitude;
    std::string longitude;
    if (!json_string_field(json, "lat", latitude) ||
        !json_string_field(json, "lon", longitude) ||
        !json_string_field(json, "display_name", result.display_name) ||
        !parse_number(latitude, result.point.latitude) ||
        !parse_number(longitude, result.point.longitude))
        return false;
    result.point.latitude = mercator::clamp_latitude(result.point.latitude);
    result.point.longitude = mercator::wrap_longitude(result.point.longitude);
    return !result.display_name.empty();
}

GeocodeResult Geocoder::search(MapHttp &http, const std::string &query,
                               const std::string &language_tag,
                               volatile int *cancel_flag) {
    GeocodeResult result;
    CachedGeocode cached;
    const std::string language = language_tag.empty() ? "en" : language_tag;
    const std::string cache_key = language + "\x1f" + query;
    if (cache_.read(cache_key, cached)) {
        result.status = GeocodeStatus::Found;
        result.point = cached.point;
        result.display_name = std::move(cached.display_name);
        result.from_cache = true;
        return result;
    }

    const unsigned long long now = sceKernelGetProcessTimeWide();
    if (last_remote_request_us_ != 0 &&
        now < last_remote_request_us_ + kMinimumRemoteIntervalUs) {
        unsigned long long remaining =
            last_remote_request_us_ + kMinimumRemoteIntervalUs - now;
        while (remaining > 0 && (!cancel_flag || !*cancel_flag)) {
            const unsigned int slice = static_cast<unsigned int>(
                std::min<unsigned long long>(remaining, 50000ULL));
            sceKernelDelayThread(slice);
            remaining -= slice;
        }
    }
    if (cancel_flag && *cancel_flag) {
        result.error = VITA_HTTPS_ERROR_INVALID_ARGUMENT;
        return result;
    }

    char *escaped = vita_https_escape(query.c_str(), query.size());
    if (!escaped) {
        result.error = VITA_HTTPS_ERROR_OUT_OF_MEMORY;
        return result;
    }
    std::string encoded_language;
    for (const char character : language) {
        if (character == ',') encoded_language += "%2C";
        else encoded_language.push_back(character);
    }
    const std::string url = endpoint() +
        "?format=jsonv2&limit=1&addressdetails=0&accept-language=" +
        encoded_language + "&q=" + escaped;
    vita_https_free(escaped);
    const std::string accept_language = "Accept-Language: " + language;
    const char *headers[] = {
        "Accept: application/json",
        accept_language.c_str(),
        nullptr,
    };
    std::vector<std::uint8_t> bytes;
    long status = 0;
    last_remote_request_us_ = sceKernelGetProcessTimeWide();
    const int error = http.download(url, cancel_flag, bytes, status, headers);
    result.error = error;
    result.http_status = status;
    if (error < 0) return result;

    const std::string body(bytes.begin(), bytes.end());
    if (body.find_first_not_of(" \t\r\n") == std::string::npos ||
        body.find('[') == std::string::npos ||
        body.find('{') == std::string::npos) {
        result.status = GeocodeStatus::NotFound;
        result.error = kNotFound;
        return result;
    }
    if (!parse_response(bytes, cached)) {
        result.error = kInvalidResponse;
        return result;
    }
    cache_.write(cache_key, cached);
    result.status = GeocodeStatus::Found;
    result.point = cached.point;
    result.display_name = std::move(cached.display_name);
    return result;
}

} // namespace vitamaps
