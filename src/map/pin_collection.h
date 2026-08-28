#pragma once

#include "map/mercator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vitamaps {

constexpr std::size_t kMaximumPinLists = 16;
constexpr std::size_t kMaximumPinsPerList = 64;

enum class PinIcon : std::uint8_t {
    Pin = 0,
    Star,
    Flag,
    Camp,
    Water,
    Summit,
    Count,
};

enum class PinColor : std::uint8_t {
    Coral = 0,
    Cyan,
    Green,
    Violet,
    White,
    Blue,
    Count,
};

enum class ElevationSource : std::uint8_t {
    None = 0,
    Gpx,
    OpenMeteo,
    Count,
};

struct MapPin {
    std::uint32_t id{0};
    mercator::GeoPoint position{};
    std::string name;
    // Empty until a configured geocoder explicitly resolves it.
    std::string address;
    double elevation_meters{0.0};
    bool has_elevation{false};
    ElevationSource elevation_source{ElevationSource::None};
};

struct PinList {
    std::uint32_t id{0};
    std::string name;
    bool visible{true};
    bool closed{false};
    std::vector<MapPin> pins;
    PinColor color{PinColor::Coral};
    PinIcon icon{PinIcon::Pin};
};

double pin_distance_meters(const MapPin &left, const MapPin &right);
double pin_path_distance_meters(const PinList &list);
double pin_polygon_area_square_meters(const PinList &list);
bool parse_coordinates(const char *text, mercator::GeoPoint &point);

class PinRepository {
public:
    int load(const std::string &default_list_name);
    int save() const;

    const std::vector<PinList> &lists() const { return lists_; }
    std::vector<PinList> &lists() { return lists_; }
    std::size_t active_index() const { return active_index_; }
    bool set_active(std::size_t index);
    const PinList &active() const;
    PinList &active();

    bool add_list(const std::string &name);
    bool rename_list(std::size_t index, const std::string &name);
    bool remove_list(std::size_t index);
    bool set_list_visible(std::size_t index, bool visible);
    bool set_list_closed(std::size_t index, bool closed);
    bool set_list_color(std::size_t index, PinColor color);
    bool set_list_icon(std::size_t index, PinIcon icon);
    bool add_pin(const mercator::GeoPoint &position,
                 const std::string &name = std::string());
    bool rename_pin(std::size_t list_index, std::size_t pin_index,
                    const std::string &name);
    bool remove_pin(std::size_t list_index, std::size_t pin_index);
    bool move_pin(std::size_t list_index, std::size_t pin_index,
                  int direction);
    bool set_pin_elevation(std::size_t list_index, std::size_t pin_index,
                           double meters,
                           ElevationSource source = ElevationSource::OpenMeteo);

private:
    void reset_default(const std::string &default_list_name);
    std::vector<PinList> lists_;
    std::size_t active_index_{0};
    std::uint32_t next_list_id_{1};
    std::uint32_t next_pin_id_{1};
};

} // namespace vitamaps
