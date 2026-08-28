#include "net/overpass.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita_https.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace vitamaps {
namespace {
constexpr char kEndpoint[] = "https://overpass-api.de/api/interpreter";
constexpr unsigned long long kMinimumIntervalUs = 10000000ULL;
constexpr std::size_t kMaximumPois = 64U;
constexpr int kInvalidResponse = -4201;
constexpr int kHttpResponseError = -4202;

bool number_after(const std::string &json, const char *key,
                  std::size_t begin, std::size_t end, double &value) {
    std::size_t offset = json.find(key, begin);
    if (offset == std::string::npos || offset >= end) return false;
    offset = json.find(':', offset + std::strlen(key));
    if (offset == std::string::npos || offset >= end) return false;
    ++offset;
    while (offset < end &&
           std::isspace(static_cast<unsigned char>(json[offset])))
        ++offset;
    errno = 0;
    char *number_end = nullptr;
    value = std::strtod(json.c_str() + offset, &number_end);
    return errno == 0 && number_end && number_end != json.c_str() + offset &&
           static_cast<std::size_t>(number_end - json.c_str()) <= end &&
           std::isfinite(value);
}

bool unsigned_after(const std::string &json, const char *key,
                    std::size_t begin, std::size_t end,
                    std::uint64_t &value) {
    double number = 0.0;
    if (!number_after(json, key, begin, end, number) || number < 0.0)
        return false;
    value = static_cast<std::uint64_t>(number);
    return true;
}

std::string unescape(const std::string &value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1U >= value.size()) {
            output.push_back(value[index]);
            continue;
        }
        const char escaped = value[++index];
        if (escaped == 'n' || escaped == 'r' || escaped == 't')
            output.push_back(' ');
        else if (escaped == '\\' || escaped == '"' || escaped == '/')
            output.push_back(escaped);
    }
    return output;
}

bool string_after(const std::string &json, const char *key,
                  std::size_t begin, std::size_t end, std::string &value) {
    std::size_t offset = json.find(key, begin);
    if (offset == std::string::npos || offset >= end) return false;
    offset = json.find(':', offset + std::strlen(key));
    if (offset == std::string::npos || offset >= end) return false;
    ++offset;
    while (offset < end &&
           std::isspace(static_cast<unsigned char>(json[offset])))
        ++offset;
    if (offset >= end || json[offset++] != '"') return false;
    std::string encoded;
    bool escaped = false;
    for (; offset < end; ++offset) {
        const char character = json[offset];
        if (!escaped && character == '"') {
            value = unescape(encoded);
            return true;
        }
        encoded.push_back(character);
        if (!escaped && character == '\\') escaped = true;
        else escaped = false;
    }
    return false;
}

std::size_t object_end(const std::string &json, std::size_t begin) {
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t index = begin; index < json.size(); ++index) {
        const char character = json[index];
        if (quoted) {
            if (!escaped && character == '"') quoted = false;
            if (!escaped && character == '\\') escaped = true;
            else escaped = false;
            continue;
        }
        if (character == '"') quoted = true;
        else if (character == '{') ++depth;
        else if (character == '}' && --depth == 0) return index + 1U;
    }
    return std::string::npos;
}

PoiCategory category_for(const std::string &amenity,
                         const std::string &tourism,
                         const std::string &natural) {
    if (amenity == "drinking_water" || natural == "spring")
        return PoiCategory::Water;
    if (amenity == "shelter") return PoiCategory::Shelter;
    if (amenity == "restaurant" || amenity == "cafe" ||
        amenity == "fast_food" || amenity == "pub")
        return PoiCategory::Food;
    if (natural == "peak") return PoiCategory::Summit;
    if (!natural.empty()) return PoiCategory::Nature;
    if (!tourism.empty()) return PoiCategory::Tourism;
    return PoiCategory::Amenity;
}

bool parse_response(const std::vector<std::uint8_t> &bytes,
                    std::vector<PointOfInterest> &points) {
    const std::string json(bytes.begin(), bytes.end());
    std::size_t elements = json.find("\"elements\"");
    if (elements == std::string::npos) return false;
    elements = json.find('[', elements);
    if (elements == std::string::npos) return false;
    std::size_t cursor = elements + 1U;
    while (points.size() < kMaximumPois &&
           (cursor = json.find('{', cursor)) != std::string::npos) {
        const std::size_t end = object_end(json, cursor);
        if (end == std::string::npos) return false;
        PointOfInterest point;
        double latitude = 0.0;
        double longitude = 0.0;
        bool position = number_after(json, "\"lat\"", cursor, end,
                                     latitude) &&
                        number_after(json, "\"lon\"", cursor, end,
                                     longitude);
        if (!position) {
            const std::size_t center = json.find("\"center\"", cursor);
            if (center != std::string::npos && center < end)
                position = number_after(json, "\"lat\"", center, end,
                                        latitude) &&
                           number_after(json, "\"lon\"", center, end,
                                        longitude);
        }
        if (position && latitude >= -mercator::kMaxLatitude &&
            latitude <= mercator::kMaxLatitude && longitude >= -180.0 &&
            longitude <= 180.0) {
            std::string amenity;
            std::string tourism;
            std::string natural;
            string_after(json, "\"name\"", cursor, end, point.name);
            string_after(json, "\"amenity\"", cursor, end, amenity);
            string_after(json, "\"tourism\"", cursor, end, tourism);
            string_after(json, "\"natural\"", cursor, end, natural);
            point.kind = !amenity.empty() ? amenity
                       : !tourism.empty() ? tourism : natural;
            if (point.name.empty()) point.name = point.kind;
            if (!point.name.empty()) {
                unsigned_after(json, "\"id\"", cursor, end, point.osm_id);
                point.position = {latitude,
                    mercator::wrap_longitude(longitude)};
                point.category = category_for(amenity, tourism, natural);
                points.push_back(std::move(point));
            }
        }
        cursor = end;
        const std::size_t array_end = json.find(']', cursor);
        const std::size_t next_object = json.find('{', cursor);
        if (array_end != std::string::npos &&
            (next_object == std::string::npos || array_end < next_object))
            break;
    }
    return true;
}
} // namespace

OverpassResult OverpassClient::search_pois(
    MapHttp &http, const mercator::GeoPoint &center, double radius_meters,
    volatile int *cancel_flag) {
    OverpassResult result;
    result.center = center;
    result.radius_meters = std::clamp(radius_meters, 250.0, 5000.0);
    const unsigned long long now = sceKernelGetProcessTimeWide();
    if (last_request_us_ != 0 && now < last_request_us_ + kMinimumIntervalUs) {
        unsigned long long remaining = last_request_us_ + kMinimumIntervalUs - now;
        while (remaining > 0 && (!cancel_flag || !*cancel_flag)) {
            const unsigned int slice = static_cast<unsigned int>(
                std::min<unsigned long long>(remaining, 50000ULL));
            sceKernelDelayThread(slice);
            remaining -= slice;
        }
    }
    if (cancel_flag && *cancel_flag) return result;
    char query[1024];
    std::snprintf(query, sizeof(query),
        "[out:json][timeout:15];("
        "nwr(around:%.0f,%.7f,%.7f)[\"amenity\"];"
        "nwr(around:%.0f,%.7f,%.7f)[\"tourism\"];"
        "nwr(around:%.0f,%.7f,%.7f)[\"natural\"~\"peak|spring|cave_entrance|water\"];"
        ");out center 64;",
        result.radius_meters, center.latitude, center.longitude,
        result.radius_meters, center.latitude, center.longitude,
        result.radius_meters, center.latitude, center.longitude);
    char *escaped = vita_https_escape(query, std::strlen(query));
    if (!escaped) {
        result.error = VITA_HTTPS_ERROR_OUT_OF_MEMORY;
        return result;
    }
    const std::string url = std::string(kEndpoint) + "?data=" + escaped;
    vita_https_free(escaped);
    const char *headers[] = {"Accept: application/json", nullptr};
    std::vector<std::uint8_t> bytes;
    last_request_us_ = sceKernelGetProcessTimeWide();
    result.error = http.download(url, cancel_flag, bytes, result.http_status,
                                 headers);
    if (result.error < 0) return result;
    if (result.http_status < 200 || result.http_status >= 300) {
        result.error = kHttpResponseError;
        return result;
    }
    if (!parse_response(bytes, result.points)) result.error = kInvalidResponse;
    return result;
}

} // namespace vitamaps
