#include "map/gpx.h"

#include "app/app_paths.h"
#include "core/log.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace vitamaps {
namespace {
constexpr std::size_t kMaximumGpxBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumInboxFiles = 64U;
constexpr std::size_t kMaximumHistory = 64U;
constexpr std::size_t kMaximumFilenameBytes = 128U;
constexpr std::size_t kMaximumListNameBytes = 96U;
constexpr std::uint32_t kHistoryVersion = 1U;
constexpr unsigned char kHistoryMagic[8] = {
    'V', 'M', 'G', 'P', 'X', '0', '0', '1'};
constexpr char kHistoryTemporary[] = VITAMAPS_GPX_DIR "/history.tmp";

struct HistoryHeader {
    unsigned char magic[8];
    std::uint32_t version;
    std::uint32_t payload_size;
    std::uint32_t checksum;
};

struct ParsedPoint {
    mercator::GeoPoint position{};
    std::string name;
    double elevation{0.0};
    bool has_elevation{false};
};

std::uint32_t crc32(const unsigned char *data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void append_u32(std::vector<unsigned char> &bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}

void append_u64(std::vector<unsigned char> &bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}

void append_string(std::vector<unsigned char> &bytes, const std::string &value,
                   std::size_t maximum) {
    const std::size_t size = std::min(value.size(), maximum);
    append_u32(bytes, static_cast<std::uint32_t>(size));
    bytes.insert(bytes.end(), value.begin(), value.begin() + size);
}

bool read_u32(const std::vector<unsigned char> &bytes, std::size_t &offset,
              std::uint32_t &value) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    return true;
}

bool read_u64(const std::vector<unsigned char> &bytes, std::size_t &offset,
              std::uint64_t &value) {
    if (offset > bytes.size() || bytes.size() - offset < 8U) return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    return true;
}

bool read_string(const std::vector<unsigned char> &bytes, std::size_t &offset,
                 std::string &value, std::size_t maximum) {
    std::uint32_t size = 0;
    if (!read_u32(bytes, offset, size) || size > maximum ||
        offset > bytes.size() || bytes.size() - offset < size)
        return false;
    value.assign(reinterpret_cast<const char *>(bytes.data() + offset), size);
    offset += size;
    return true;
}

int read_all(SceUID file, void *data, std::size_t size) {
    auto *bytes = static_cast<unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoRead(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return count < 0 ? static_cast<int>(count) : -1;
        offset += static_cast<std::size_t>(count);
    }
    return 0;
}

int write_all(SceUID file, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const SceSSize count = sceIoWrite(
            file, bytes + offset, static_cast<SceSize>(size - offset));
        if (count <= 0) return count < 0 ? static_cast<int>(count) : -1;
        offset += static_cast<std::size_t>(count);
    }
    return 0;
}

bool ends_with_gpx(const char *name) {
    if (!name) return false;
    const std::size_t size = std::strlen(name);
    if (size < 5U) return false;
    const char *suffix = name + size - 4U;
    return suffix[0] == '.' &&
           std::tolower(static_cast<unsigned char>(suffix[1])) == 'g' &&
           std::tolower(static_cast<unsigned char>(suffix[2])) == 'p' &&
           std::tolower(static_cast<unsigned char>(suffix[3])) == 'x';
}

std::string local_name(const std::string &name) {
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1U);
}

bool parse_number(const std::string &value, double &number) {
    errno = 0;
    char *end = nullptr;
    number = std::strtod(value.c_str(), &end);
    return errno == 0 && end && *end == '\0' && std::isfinite(number);
}

bool attribute(const std::string &tag, const char *wanted,
               std::string &value) {
    std::size_t offset = 0;
    while (offset < tag.size()) {
        while (offset < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[offset])))
            ++offset;
        const std::size_t begin = offset;
        while (offset < tag.size() && tag[offset] != '=' &&
               !std::isspace(static_cast<unsigned char>(tag[offset])))
            ++offset;
        const std::string name = local_name(tag.substr(begin, offset - begin));
        while (offset < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[offset])))
            ++offset;
        if (offset >= tag.size() || tag[offset] != '=') {
            while (offset < tag.size() &&
                   !std::isspace(static_cast<unsigned char>(tag[offset])))
                ++offset;
            continue;
        }
        ++offset;
        while (offset < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[offset])))
            ++offset;
        if (offset >= tag.size() || (tag[offset] != '\'' && tag[offset] != '"'))
            return false;
        const char quote = tag[offset++];
        const std::size_t value_begin = offset;
        const std::size_t end = tag.find(quote, value_begin);
        if (end == std::string::npos) return false;
        if (name == wanted) {
            value = tag.substr(value_begin, end - value_begin);
            return true;
        }
        offset = end + 1U;
    }
    return false;
}

std::string xml_unescape(std::string value) {
    struct Entity { const char *encoded; const char *plain; };
    constexpr Entity entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"},
    };
    for (const auto &entity : entities) {
        std::size_t offset = 0;
        while ((offset = value.find(entity.encoded, offset)) !=
               std::string::npos) {
            value.replace(offset, std::strlen(entity.encoded), entity.plain);
            offset += std::strlen(entity.plain);
        }
    }
    return value;
}

bool child_text(const std::string &body, const char *wanted,
                std::string &value) {
    std::size_t cursor = 0;
    while ((cursor = body.find('<', cursor)) != std::string::npos) {
        if (cursor + 1U >= body.size() || body[cursor + 1U] == '/') {
            ++cursor;
            continue;
        }
        const std::size_t name_begin = cursor + 1U;
        std::size_t name_end = name_begin;
        while (name_end < body.size() && body[name_end] != '>' &&
               !std::isspace(static_cast<unsigned char>(body[name_end])))
            ++name_end;
        const std::string full_name = body.substr(name_begin,
                                                  name_end - name_begin);
        const std::size_t open_end = body.find('>', name_end);
        if (open_end == std::string::npos) return false;
        if (local_name(full_name) != wanted) {
            cursor = open_end + 1U;
            continue;
        }
        const std::string close = "</" + full_name + ">";
        const std::size_t close_at = body.find(close, open_end + 1U);
        if (close_at == std::string::npos) return false;
        value = xml_unescape(body.substr(open_end + 1U,
                                         close_at - open_end - 1U));
        return true;
    }
    return false;
}

bool parse_gpx(const std::string &xml, std::vector<ParsedPoint> &points,
               std::string &list_name) {
    if (xml.find("<gpx") == std::string::npos &&
        xml.find(":gpx") == std::string::npos)
        return false;
    child_text(xml, "name", list_name);
    std::vector<ParsedPoint> waypoints;
    std::vector<ParsedPoint> route_points;
    std::vector<ParsedPoint> track_points;
    std::size_t cursor = 0;
    while ((cursor = xml.find('<', cursor)) != std::string::npos) {
        if (cursor + 1U >= xml.size() || xml[cursor + 1U] == '/' ||
            xml[cursor + 1U] == '!' || xml[cursor + 1U] == '?') {
            ++cursor;
            continue;
        }
        const std::size_t name_begin = cursor + 1U;
        std::size_t name_end = name_begin;
        while (name_end < xml.size() && xml[name_end] != '>' &&
               xml[name_end] != '/' &&
               !std::isspace(static_cast<unsigned char>(xml[name_end])))
            ++name_end;
        const std::string full_name = xml.substr(name_begin,
                                                 name_end - name_begin);
        const std::string name = local_name(full_name);
        if (name != "wpt" && name != "rtept" && name != "trkpt") {
            ++cursor;
            continue;
        }
        const std::size_t open_end = xml.find('>', name_end);
        if (open_end == std::string::npos) return false;
        const std::string tag = xml.substr(name_end,
                                           open_end - name_end);
        std::string latitude;
        std::string longitude;
        ParsedPoint point;
        if (!attribute(tag, "lat", latitude) ||
            !attribute(tag, "lon", longitude) ||
            !parse_number(latitude, point.position.latitude) ||
            !parse_number(longitude, point.position.longitude) ||
            point.position.latitude < -mercator::kMaxLatitude ||
            point.position.latitude > mercator::kMaxLatitude ||
            point.position.longitude < -180.0 ||
            point.position.longitude > 180.0)
            return false;
        point.position.longitude =
            mercator::wrap_longitude(point.position.longitude);
        const std::string close = "</" + full_name + ">";
        const std::size_t close_at = xml.find(close, open_end + 1U);
        std::string body;
        if (close_at != std::string::npos)
            body = xml.substr(open_end + 1U, close_at - open_end - 1U);
        child_text(body, "name", point.name);
        std::string elevation;
        if (child_text(body, "ele", elevation) &&
            parse_number(elevation, point.elevation) &&
            point.elevation >= -500.0 && point.elevation <= 10000.0)
            point.has_elevation = true;
        if (name == "rtept") route_points.push_back(std::move(point));
        else if (name == "trkpt") track_points.push_back(std::move(point));
        else waypoints.push_back(std::move(point));
        cursor = close_at == std::string::npos ? open_end + 1U
                                               : close_at + close.size();
        if (waypoints.size() + route_points.size() + track_points.size() >
            200000U)
            return false;
    }
    if (!route_points.empty()) points = std::move(route_points);
    else if (!track_points.empty()) points = std::move(track_points);
    else points = std::move(waypoints);
    return !points.empty();
}

std::vector<ParsedPoint> bounded_points(const std::vector<ParsedPoint> &source) {
    if (source.size() <= kMaximumPinsPerList) return source;
    std::vector<ParsedPoint> result;
    result.reserve(kMaximumPinsPerList);
    for (std::size_t index = 0; index < kMaximumPinsPerList; ++index) {
        const std::size_t source_index =
            index * (source.size() - 1U) / (kMaximumPinsPerList - 1U);
        result.push_back(source[source_index]);
    }
    return result;
}

std::string file_stem(const std::string &filename) {
    const std::size_t dot = filename.find_last_of('.');
    return filename.substr(0, dot == std::string::npos ? filename.size() : dot);
}

std::string xml_escape(const std::string &value) {
    std::string output;
    output.reserve(value.size() + 16U);
    for (const char character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '\"': output += "&quot;"; break;
        case '\'': output += "&apos;"; break;
        default: output.push_back(character); break;
        }
    }
    return output;
}

std::uint64_t current_tick() {
    SceRtcTick tick{};
    return sceRtcGetCurrentTick(&tick) >= 0 ? tick.tick : 0U;
}
} // namespace

int GpxManager::initialize() {
    sceIoMkdir(VITAMAPS_DATA_DIR, 0777);
    sceIoMkdir(VITAMAPS_GPX_DIR, 0777);
    sceIoMkdir(VITAMAPS_GPX_INBOX_DIR, 0777);
    sceIoMkdir(VITAMAPS_GPX_EXPORT_DIR, 0777);
    const int history_result = load_history();
    const int inbox_result = refresh_inbox();
    log_printf("gpx: init history=0x%08X inbox=0x%08X files=%u records=%u",
               static_cast<unsigned>(history_result),
               static_cast<unsigned>(inbox_result),
               static_cast<unsigned>(inbox_.size()),
               static_cast<unsigned>(history_.size()));
    return history_result < 0 ? history_result : inbox_result;
}

int GpxManager::refresh_inbox() {
    inbox_.clear();
    const SceUID directory = sceIoDopen(VITAMAPS_GPX_INBOX_DIR);
    if (directory < 0) return static_cast<int>(directory);
    SceIoDirent item{};
    while (inbox_.size() < kMaximumInboxFiles &&
           sceIoDread(directory, &item) > 0) {
        if (SCE_S_ISREG(item.d_stat.st_mode) && ends_with_gpx(item.d_name) &&
            item.d_stat.st_size > 0 &&
            static_cast<std::uint64_t>(item.d_stat.st_size) <= kMaximumGpxBytes) {
            std::uint64_t modified = 0;
            SceRtcTick tick{};
            if (sceRtcGetTick(&item.d_stat.st_mtime, &tick) >= 0)
                modified = tick.tick;
            inbox_.push_back({item.d_name,
                static_cast<std::uint64_t>(item.d_stat.st_size), modified});
        }
        std::memset(&item, 0, sizeof(item));
    }
    sceIoDclose(directory);
    std::sort(inbox_.begin(), inbox_.end(),
              [](const GpxInboxEntry &left, const GpxInboxEntry &right) {
                  if (left.modified != right.modified)
                      return left.modified > right.modified;
                  return left.filename < right.filename;
              });
    return 0;
}

GpxOperationResult GpxManager::import_file(
    std::size_t inbox_index, PinRepository &repository) {
    GpxOperationResult result;
    if (inbox_index >= inbox_.size()) {
        result.error = -4101;
        return result;
    }
    const GpxInboxEntry &entry = inbox_[inbox_index];
    const std::string path = std::string(VITAMAPS_GPX_INBOX_DIR) + "/" +
                             entry.filename;
    result.path = path;
    const SceUID file = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (file < 0) {
        result.error = static_cast<int>(file);
    } else {
        std::vector<char> data(static_cast<std::size_t>(entry.bytes) + 1U, 0);
        result.error = read_all(file, data.data(),
                                static_cast<std::size_t>(entry.bytes));
        sceIoClose(file);
        if (result.error == 0) {
            std::vector<ParsedPoint> parsed;
            std::string list_name;
            if (!parse_gpx(std::string(data.data(), entry.bytes), parsed,
                           list_name)) {
                result.error = -4102;
            } else {
                std::vector<ParsedPoint> points = bounded_points(parsed);
                if (list_name.empty()) list_name = file_stem(entry.filename);
                if (!repository.add_list(list_name)) {
                    result.error = -4103;
                } else {
                    result.list_name = repository.active().name;
                    repository.set_list_color(repository.active_index(),
                                              PinColor::Green);
                    repository.set_list_icon(repository.active_index(),
                                             PinIcon::Flag);
                    for (std::size_t index = 0; index < points.size(); ++index) {
                        if (!repository.add_pin(points[index].position,
                                                points[index].name)) {
                            result.error = -4104;
                            break;
                        }
                        if (points[index].has_elevation)
                            repository.set_pin_elevation(
                                repository.active_index(), index,
                                points[index].elevation,
                                ElevationSource::Gpx);
                    }
                    result.points = static_cast<std::uint32_t>(
                        repository.active().pins.size());
                    result.repository_changed = result.error == 0;
                    if (result.error == 0) result.error = repository.save();
                }
            }
        }
    }
    append_history({entry.filename, result.list_name, current_tick(),
                    result.points, result.error});
    const int history_error = save_history();
    if (result.error == 0 && history_error < 0) result.error = history_error;
    log_printf("gpx import: file=%s result=0x%08X points=%u list=%s",
               entry.filename.c_str(), static_cast<unsigned>(result.error),
               static_cast<unsigned>(result.points),
               result.list_name.c_str());
    return result;
}

GpxOperationResult GpxManager::export_list(const PinList &list) const {
    GpxOperationResult result;
    result.list_name = list.name;
    result.points = static_cast<std::uint32_t>(list.pins.size());
    char filename[64];
    std::snprintf(filename, sizeof(filename), "/list-%08X.gpx",
                  static_cast<unsigned>(list.id));
    result.path = std::string(VITAMAPS_GPX_EXPORT_DIR) + filename;
    const std::string temporary = result.path + ".tmp";
    sceIoRemove(temporary.c_str());
    const SceUID file = sceIoOpen(temporary.c_str(),
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) {
        result.error = static_cast<int>(file);
        return result;
    }
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"VitaMaps 1.0.0\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 "
        "https://www.topografix.com/GPX/1/1/gpx.xsd\">\n"
        "  <rte>\n    <name>" + xml_escape(list.name) + "</name>\n";
    for (const MapPin &pin : list.pins) {
        char coordinates[128];
        std::snprintf(coordinates, sizeof(coordinates),
                      "    <rtept lat=\"%.8f\" lon=\"%.8f\">\n",
                      pin.position.latitude, pin.position.longitude);
        xml += coordinates;
        if (pin.has_elevation) {
            char elevation[64];
            std::snprintf(elevation, sizeof(elevation), "      <ele>%.2f</ele>\n",
                          pin.elevation_meters);
            xml += elevation;
        }
        xml += "      <name>" + xml_escape(pin.name) + "</name>\n";
        xml += "    </rtept>\n";
    }
    xml += "  </rte>\n</gpx>\n";
    result.error = write_all(file, xml.data(), xml.size());
    if (result.error == 0) result.error = sceIoSyncByFd(file, 0);
    const int close_error = sceIoClose(file);
    if (result.error == 0 && close_error < 0) result.error = close_error;
    if (result.error == 0) {
        sceIoRemove(result.path.c_str());
        result.error = sceIoRename(temporary.c_str(), result.path.c_str());
    }
    if (result.error < 0) sceIoRemove(temporary.c_str());
    log_printf("gpx export: path=%s result=0x%08X points=%u",
               result.path.c_str(), static_cast<unsigned>(result.error),
               static_cast<unsigned>(result.points));
    return result;
}

void GpxManager::append_history(GpxImportRecord record) {
    history_.insert(history_.begin(), std::move(record));
    if (history_.size() > kMaximumHistory) history_.resize(kMaximumHistory);
}

int GpxManager::load_history() {
    history_.clear();
    const SceUID file = sceIoOpen(VITAMAPS_GPX_HISTORY_PATH, SCE_O_RDONLY, 0);
    if (file < 0) return 0;
    HistoryHeader header{};
    int result = read_all(file, &header, sizeof(header));
    std::vector<unsigned char> bytes;
    if (result == 0 && std::memcmp(header.magic, kHistoryMagic,
                                   sizeof(kHistoryMagic)) == 0 &&
        header.version == kHistoryVersion &&
        header.payload_size <= 64U * 1024U) {
        bytes.resize(header.payload_size);
        result = bytes.empty() ? 0 : read_all(file, bytes.data(), bytes.size());
    } else if (result == 0) {
        result = -1;
    }
    sceIoClose(file);
    if (result < 0 || crc32(bytes.data(), bytes.size()) != header.checksum)
        return -1;
    std::size_t offset = 0;
    std::uint32_t count = 0;
    if (!read_u32(bytes, offset, count) || count > kMaximumHistory) return -1;
    for (std::uint32_t index = 0; index < count; ++index) {
        GpxImportRecord record;
        std::uint32_t encoded_result = 0;
        if (!read_string(bytes, offset, record.filename, kMaximumFilenameBytes) ||
            !read_string(bytes, offset, record.list_name, kMaximumListNameBytes) ||
            !read_u64(bytes, offset, record.imported_at) ||
            !read_u32(bytes, offset, record.points) ||
            !read_u32(bytes, offset, encoded_result))
            return -1;
        record.result = static_cast<int>(encoded_result);
        history_.push_back(std::move(record));
    }
    return offset == bytes.size() ? 0 : -1;
}

int GpxManager::save_history() const {
    std::vector<unsigned char> bytes;
    append_u32(bytes, static_cast<std::uint32_t>(history_.size()));
    for (const auto &record : history_) {
        append_string(bytes, record.filename, kMaximumFilenameBytes);
        append_string(bytes, record.list_name, kMaximumListNameBytes);
        append_u64(bytes, record.imported_at);
        append_u32(bytes, record.points);
        append_u32(bytes, static_cast<std::uint32_t>(record.result));
    }
    HistoryHeader header{};
    std::memcpy(header.magic, kHistoryMagic, sizeof(kHistoryMagic));
    header.version = kHistoryVersion;
    header.payload_size = static_cast<std::uint32_t>(bytes.size());
    header.checksum = crc32(bytes.data(), bytes.size());
    sceIoRemove(kHistoryTemporary);
    const SceUID file = sceIoOpen(kHistoryTemporary,
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) return static_cast<int>(file);
    int result = write_all(file, &header, sizeof(header));
    if (result == 0 && !bytes.empty())
        result = write_all(file, bytes.data(), bytes.size());
    if (result == 0) result = sceIoSyncByFd(file, 0);
    const int close_error = sceIoClose(file);
    if (result == 0 && close_error < 0) result = close_error;
    if (result == 0) {
        sceIoRemove(VITAMAPS_GPX_HISTORY_PATH);
        result = sceIoRename(kHistoryTemporary, VITAMAPS_GPX_HISTORY_PATH);
    }
    if (result < 0) sceIoRemove(kHistoryTemporary);
    return result;
}

} // namespace vitamaps
