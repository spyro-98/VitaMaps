#include "render/image_decoder.h"

#include <png.h>

#include <climits>

namespace vitamaps {

bool decode_png_rgba(const std::vector<std::uint8_t> &encoded,
                     int expected_size, int &width, int &height,
                     std::vector<std::uint8_t> &rgba, std::string &error) {
    width = 0;
    height = 0;
    rgba.clear();
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(INT_MAX)) {
        error = "invalid encoded PNG size";
        return false;
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, encoded.data(),
                                          encoded.size())) {
        error = image.message;
        return false;
    }
    if (image.width != static_cast<png_uint_32>(expected_size) ||
        image.height != static_cast<png_uint_32>(expected_size)) {
        error = "unexpected tile dimensions";
        png_image_free(&image);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    rgba.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
        error = image.message;
        rgba.clear();
        png_image_free(&image);
        return false;
    }
    width = static_cast<int>(image.width);
    height = static_cast<int>(image.height);
    png_image_free(&image);
    return true;
}

} // namespace vitamaps
