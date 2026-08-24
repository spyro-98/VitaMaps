#include "render/texture_cache.h"

#include <psp2/gxm.h>

#include <cstring>
#include <limits>

namespace vitamaps {

TextureCache::TextureCache(std::size_t budget_bytes)
    : budget_bytes_(budget_bytes) {}

TextureCache::~TextureCache() { clear(); }

vita2d_texture *TextureCache::find(const TileKey &key, std::uint64_t frame) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return nullptr;
    found->second.last_used_frame = frame;
    return found->second.texture;
}

bool TextureCache::reserve(std::size_t required, std::uint64_t frame,
                           std::vector<TileKey> &evicted) {
    while (used_bytes_ + required > budget_bytes_) {
        auto victim = entries_.end();
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.last_used_frame == frame) continue;
            if (it->second.last_used_frame < oldest) {
                oldest = it->second.last_used_frame;
                victim = it;
            }
        }
        if (victim == entries_.end()) return false;
        used_bytes_ -= victim->second.bytes;
        vita2d_free_texture(victim->second.texture);
        evicted.push_back(victim->first);
        entries_.erase(victim);
    }
    return true;
}

bool TextureCache::insert(const DecodedTile &tile, std::uint64_t frame,
                          std::vector<TileKey> &evicted) {
    if (tile.width <= 0 || tile.height <= 0 ||
        tile.rgba.size() != static_cast<std::size_t>(tile.width) *
                                static_cast<std::size_t>(tile.height) * 4U) {
        return false;
    }
    const auto previous = entries_.find(tile.key);
    if (previous != entries_.end()) {
        previous->second.last_used_frame = frame;
        return true;
    }
    const std::size_t bytes = tile.rgba.size();
    if (bytes > budget_bytes_ || !reserve(bytes, frame, evicted)) return false;

    vita2d_texture *texture = vita2d_create_empty_texture_format(
        static_cast<unsigned int>(tile.width),
        static_cast<unsigned int>(tile.height),
        // libpng writes byte-ordered RGBA. On little-endian GXM memory that
        // corresponds to the ABGR swizzle (the same mapping used by Vita
        // RGBA software decoders); RGBA here swaps red and blue on hardware.
        SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    if (!texture) return false;
    const unsigned int stride = vita2d_texture_get_stride(texture);
    auto *destination = static_cast<std::uint8_t *>(vita2d_texture_get_datap(texture));
    const std::size_t row_bytes = static_cast<std::size_t>(tile.width) * 4U;
    for (int row = 0; row < tile.height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * stride,
                    tile.rgba.data() + static_cast<std::size_t>(row) * row_bytes,
                    row_bytes);
    }
    vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR,
                               SCE_GXM_TEXTURE_FILTER_LINEAR);
    entries_.emplace(tile.key, Entry{texture, bytes, frame});
    used_bytes_ += bytes;
    return true;
}

void TextureCache::clear() {
    for (auto &entry : entries_) vita2d_free_texture(entry.second.texture);
    entries_.clear();
    used_bytes_ = 0;
}

} // namespace vitamaps
