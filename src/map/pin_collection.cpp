#include "map/pin_collection.h"

#include "app/app_paths.h"
#include "core/log.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace vitamaps {
namespace {
constexpr std::uint32_t kVersion = 3U;
constexpr std::uint32_t kMinimumSupportedVersion = 1U;
constexpr std::uint32_t kListVisible = 1U << 0U;
constexpr std::uint32_t kListClosed = 1U << 1U;
constexpr std::size_t kMaximumPayloadBytes = 512U * 1024U;
constexpr std::size_t kMaximumNameBytes = 96U;
constexpr std::size_t kMaximumAddressBytes = 240U;
constexpr unsigned char kMagic[8] = {'V', 'M', 'P', 'I', 'N', '0', '0', '1'};
constexpr char kTemporaryPath[] = VITAMAPS_DATA_DIR "/pin_collections.tmp";
constexpr char kBackupPath[] = VITAMAPS_DATA_DIR "/pin_collections.bak";

struct DiskHeader {
    unsigned char magic[8];
    std::uint32_t version;
    std::uint32_t payload_size;
    std::uint32_t checksum;
};

static_assert(sizeof(DiskHeader) == 20, "pin header layout changed");

std::string bounded_text(const std::string &value, std::size_t maximum) {
    if (value.size() <= maximum) return value;
    // Never retain a partial UTF-8 continuation at the end.
    std::size_t size = maximum;
    while (size > 0 &&
           (static_cast<unsigned char>(value[size]) & 0xC0U) == 0x80U)
        --size;
    return value.substr(0, size);
}

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

void append_double(std::vector<unsigned char> &bytes, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double is not 64-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned shift = 0; shift < 64; shift += 8)
        bytes.push_back(static_cast<unsigned char>(bits >> shift));
}

void append_string(std::vector<unsigned char> &bytes, const std::string &value,
                   std::size_t maximum) {
    const std::string text = bounded_text(value, maximum);
    append_u32(bytes, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

bool read_u32(const std::vector<unsigned char> &bytes, std::size_t &offset,
              std::uint32_t &value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    return true;
}

bool read_double(const std::vector<unsigned char> &bytes, std::size_t &offset,
                 double &value) {
    if (offset > bytes.size() || bytes.size() - offset < 8) return false;
    std::uint64_t bits = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        bits |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
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

bool exists(const char *path) {
    SceIoStat stat{};
    return sceIoGetstat(path, &stat) >= 0;
}

bool decode_payload(const std::vector<unsigned char> &bytes,
                    std::vector<PinList> &lists, std::size_t &active,
                    std::uint32_t &next_list, std::uint32_t &next_pin,
                    std::uint32_t version) {
    std::size_t offset = 0;
    std::uint32_t active_disk = 0;
    std::uint32_t list_count = 0;
    if (!read_u32(bytes, offset, active_disk) ||
        !read_u32(bytes, offset, next_list) ||
        !read_u32(bytes, offset, next_pin) ||
        !read_u32(bytes, offset, list_count) || list_count == 0 ||
        list_count > kMaximumPinLists)
        return false;
    std::vector<PinList> decoded;
    decoded.reserve(list_count);
    for (std::uint32_t list_index = 0; list_index < list_count; ++list_index) {
        PinList list;
        std::uint32_t flags = kListVisible;
        std::uint32_t pin_count = 0;
        if (!read_u32(bytes, offset, list.id) || list.id == 0 ||
            !read_string(bytes, offset, list.name, kMaximumNameBytes) ||
            list.name.empty())
            return false;
        if (version >= 2U && !read_u32(bytes, offset, flags))
            return false;
        list.visible = (flags & kListVisible) != 0U;
        list.closed = (flags & kListClosed) != 0U;
        if (version >= 3U) {
            std::uint32_t color = 0;
            std::uint32_t icon = 0;
            if (!read_u32(bytes, offset, color) ||
                !read_u32(bytes, offset, icon) ||
                color >= static_cast<std::uint32_t>(PinColor::Count) ||
                icon >= static_cast<std::uint32_t>(PinIcon::Count))
                return false;
            list.color = static_cast<PinColor>(color);
            list.icon = static_cast<PinIcon>(icon);
        }
        if (!read_u32(bytes, offset, pin_count) ||
            pin_count > kMaximumPinsPerList)
            return false;
        list.pins.reserve(pin_count);
        for (std::uint32_t pin_index = 0; pin_index < pin_count; ++pin_index) {
            MapPin pin;
            if (!read_u32(bytes, offset, pin.id) || pin.id == 0 ||
                !read_double(bytes, offset, pin.position.latitude) ||
                !read_double(bytes, offset, pin.position.longitude) ||
                !read_string(bytes, offset, pin.name, kMaximumNameBytes) ||
                !read_string(bytes, offset, pin.address,
                             kMaximumAddressBytes) ||
                pin.position.latitude < -mercator::kMaxLatitude ||
                pin.position.latitude > mercator::kMaxLatitude ||
                pin.position.longitude < -180.0 ||
                pin.position.longitude >= 180.0)
                return false;
            if (version >= 3U) {
                std::uint32_t elevation_source = 0;
                if (!read_u32(bytes, offset, elevation_source) ||
                    !read_double(bytes, offset, pin.elevation_meters) ||
                    elevation_source >= static_cast<std::uint32_t>(
                        ElevationSource::Count) ||
                    (elevation_source != 0U &&
                     (pin.elevation_meters < -500.0 ||
                      pin.elevation_meters > 10000.0)))
                    return false;
                pin.elevation_source = static_cast<ElevationSource>(
                    elevation_source);
                pin.has_elevation = elevation_source != 0U;
            }
            list.pins.push_back(std::move(pin));
        }
        if (list.pins.size() < 3) list.closed = false;
        decoded.push_back(std::move(list));
    }
    if (offset != bytes.size() || active_disk >= decoded.size()) return false;
    lists = std::move(decoded);
    active = active_disk;
    if (next_list == 0) next_list = 1;
    if (next_pin == 0) next_pin = 1;
    return true;
}

int load_path(const char *path, std::vector<PinList> &lists,
              std::size_t &active, std::uint32_t &next_list,
              std::uint32_t &next_pin) {
    const SceUID file = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (file < 0) return static_cast<int>(file);
    DiskHeader header{};
    int result = read_all(file, &header, sizeof(header));
    std::vector<unsigned char> payload;
    if (result == 0) {
        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
            header.version < kMinimumSupportedVersion ||
            header.version > kVersion || header.payload_size == 0 ||
            header.payload_size > kMaximumPayloadBytes) {
            result = -2;
        } else {
            payload.resize(header.payload_size);
            result = read_all(file, payload.data(), payload.size());
        }
    }
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0) result = close_result;
    if (result == 0 &&
        (crc32(payload.data(), payload.size()) != header.checksum ||
         !decode_payload(payload, lists, active, next_list, next_pin,
                         header.version)))
        result = -3;
    return result;
}
} // namespace

void PinRepository::reset_default(const std::string &default_list_name) {
    lists_.clear();
    lists_.push_back({1U, default_list_name.empty()
                             ? std::string("Favorites")
                             : bounded_text(default_list_name,
                                            kMaximumNameBytes), true, false,
                      {}});
    active_index_ = 0;
    next_list_id_ = 2;
    next_pin_id_ = 1;
}

int PinRepository::load(const std::string &default_list_name) {
    reset_default(default_list_name);
    std::vector<PinList> loaded;
    std::size_t active = 0;
    std::uint32_t next_list = 1;
    std::uint32_t next_pin = 1;
    int result = load_path(VITAMAPS_PIN_COLLECTIONS_PATH, loaded, active,
                           next_list, next_pin);
    if (result != 0)
        result = load_path(kBackupPath, loaded, active, next_list, next_pin);
    if (result == 0) {
        lists_ = std::move(loaded);
        active_index_ = active;
        next_list_id_ = next_list;
        next_pin_id_ = next_pin;
    } else if (!exists(VITAMAPS_PIN_COLLECTIONS_PATH)) {
        result = 0;
    }
    log_printf("pin repository: load=0x%08X lists=%u active=%u",
               static_cast<unsigned>(result),
               static_cast<unsigned>(lists_.size()),
               static_cast<unsigned>(active_index_));
    return result;
}

int PinRepository::save() const {
    std::vector<unsigned char> payload;
    payload.reserve(1024);
    append_u32(payload, static_cast<std::uint32_t>(active_index_));
    append_u32(payload, next_list_id_);
    append_u32(payload, next_pin_id_);
    append_u32(payload, static_cast<std::uint32_t>(lists_.size()));
    for (const auto &list : lists_) {
        append_u32(payload, list.id);
        append_string(payload, list.name, kMaximumNameBytes);
        append_u32(payload, (list.visible ? kListVisible : 0U) |
                            (list.closed ? kListClosed : 0U));
        append_u32(payload, static_cast<std::uint32_t>(list.color));
        append_u32(payload, static_cast<std::uint32_t>(list.icon));
        append_u32(payload, static_cast<std::uint32_t>(list.pins.size()));
        for (const auto &pin : list.pins) {
            append_u32(payload, pin.id);
            append_double(payload, pin.position.latitude);
            append_double(payload, pin.position.longitude);
            append_string(payload, pin.name, kMaximumNameBytes);
            append_string(payload, pin.address, kMaximumAddressBytes);
            append_u32(payload, static_cast<std::uint32_t>(
                pin.has_elevation ? pin.elevation_source
                                  : ElevationSource::None));
            append_double(payload, pin.elevation_meters);
        }
    }
    if (payload.empty() || payload.size() > kMaximumPayloadBytes) return -1;
    DiskHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.payload_size = static_cast<std::uint32_t>(payload.size());
    header.checksum = crc32(payload.data(), payload.size());

    sceIoMkdir(VITAMAPS_DATA_DIR, 0777);
    sceIoRemove(kTemporaryPath);
    const SceUID file = sceIoOpen(kTemporaryPath,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (file < 0) return static_cast<int>(file);
    int result = write_all(file, &header, sizeof(header));
    if (result == 0) result = write_all(file, payload.data(), payload.size());
    if (result == 0) result = sceIoSyncByFd(file, 0);
    const int close_result = sceIoClose(file);
    if (result == 0 && close_result < 0) result = close_result;
    if (result < 0) {
        sceIoRemove(kTemporaryPath);
        return result;
    }
    sceIoRemove(kBackupPath);
    const bool had_previous = exists(VITAMAPS_PIN_COLLECTIONS_PATH);
    if (had_previous &&
        sceIoRename(VITAMAPS_PIN_COLLECTIONS_PATH, kBackupPath) < 0) {
        sceIoRemove(kTemporaryPath);
        return -1;
    }
    result = sceIoRename(kTemporaryPath, VITAMAPS_PIN_COLLECTIONS_PATH);
    if (result < 0) {
        if (had_previous)
            sceIoRename(kBackupPath, VITAMAPS_PIN_COLLECTIONS_PATH);
        sceIoRemove(kTemporaryPath);
        return result;
    }
    sceIoSync("ux0:", 0);
    if (had_previous) sceIoRemove(kBackupPath);
    return 0;
}

bool PinRepository::set_active(std::size_t index) {
    if (index >= lists_.size()) return false;
    active_index_ = index;
    return true;
}

const PinList &PinRepository::active() const { return lists_[active_index_]; }
PinList &PinRepository::active() { return lists_[active_index_]; }

bool PinRepository::add_list(const std::string &name) {
    if (lists_.size() >= kMaximumPinLists || name.empty()) return false;
    lists_.push_back({next_list_id_++, bounded_text(name, kMaximumNameBytes),
                      true, false, {}});
    active_index_ = lists_.size() - 1;
    return true;
}

bool PinRepository::rename_list(std::size_t index, const std::string &name) {
    if (index >= lists_.size() || name.empty()) return false;
    lists_[index].name = bounded_text(name, kMaximumNameBytes);
    return true;
}

bool PinRepository::remove_list(std::size_t index) {
    if (lists_.size() <= 1 || index >= lists_.size()) return false;
    lists_.erase(lists_.begin() + static_cast<std::ptrdiff_t>(index));
    if (active_index_ == index) active_index_ = 0;
    else if (active_index_ > index) --active_index_;
    return true;
}

bool PinRepository::set_list_visible(std::size_t index, bool visible) {
    if (index >= lists_.size()) return false;
    lists_[index].visible = visible;
    return true;
}

bool PinRepository::set_list_closed(std::size_t index, bool closed) {
    if (index >= lists_.size() || (closed && lists_[index].pins.size() < 3))
        return false;
    lists_[index].closed = closed;
    return true;
}

bool PinRepository::set_list_color(std::size_t index, PinColor color) {
    if (index >= lists_.size() || color >= PinColor::Count) return false;
    lists_[index].color = color;
    return true;
}

bool PinRepository::set_list_icon(std::size_t index, PinIcon icon) {
    if (index >= lists_.size() || icon >= PinIcon::Count) return false;
    lists_[index].icon = icon;
    return true;
}

bool PinRepository::add_pin(const mercator::GeoPoint &position,
                            const std::string &name) {
    if (active().pins.size() >= kMaximumPinsPerList) return false;
    // Adding a new stop extends the route, so a previously closed polygon is
    // reopened explicitly instead of silently moving its closing edge.
    active().closed = false;
    const std::size_t number = active().pins.size() + 1;
    std::string final_name = name;
    if (final_name.empty()) final_name = "Punto " + std::to_string(number);
    active().pins.push_back({next_pin_id_++,
        {mercator::clamp_latitude(position.latitude),
         mercator::wrap_longitude(position.longitude)},
        bounded_text(final_name, kMaximumNameBytes), {}, 0.0, false,
        ElevationSource::None});
    return true;
}

bool PinRepository::rename_pin(std::size_t list_index, std::size_t pin_index,
                               const std::string &name) {
    if (list_index >= lists_.size() ||
        pin_index >= lists_[list_index].pins.size() || name.empty())
        return false;
    lists_[list_index].pins[pin_index].name =
        bounded_text(name, kMaximumNameBytes);
    return true;
}

bool PinRepository::remove_pin(std::size_t list_index, std::size_t pin_index) {
    if (list_index >= lists_.size() ||
        pin_index >= lists_[list_index].pins.size())
        return false;
    auto &pins = lists_[list_index].pins;
    pins.erase(pins.begin() + static_cast<std::ptrdiff_t>(pin_index));
    if (pins.size() < 3) lists_[list_index].closed = false;
    return true;
}

bool PinRepository::move_pin(std::size_t list_index, std::size_t pin_index,
                             int direction) {
    if (list_index >= lists_.size() || direction == 0) return false;
    auto &pins = lists_[list_index].pins;
    if (pin_index >= pins.size()) return false;
    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(pin_index) +
                                  (direction < 0 ? -1 : 1);
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(pins.size()))
        return false;
    std::swap(pins[pin_index], pins[static_cast<std::size_t>(target)]);
    return true;
}

bool PinRepository::set_pin_elevation(std::size_t list_index,
                                      std::size_t pin_index, double meters,
                                      ElevationSource source) {
    if (list_index >= lists_.size() ||
        pin_index >= lists_[list_index].pins.size() ||
        !std::isfinite(meters) || meters < -500.0 || meters > 10000.0 ||
        source == ElevationSource::None || source >= ElevationSource::Count)
        return false;
    MapPin &pin = lists_[list_index].pins[pin_index];
    pin.elevation_meters = meters;
    pin.has_elevation = true;
    pin.elevation_source = source;
    return true;
}

} // namespace vitamaps
