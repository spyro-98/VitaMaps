#include "cache/memory_cache.h"

#include <utility>

namespace vitamaps {

MemoryCache::MemoryCache(std::size_t budget_bytes)
    : budget_bytes_(budget_bytes) {}

void MemoryCache::touch(Entry &entry, const TileKey &key) {
    lru_.erase(entry.lru);
    lru_.push_front(key);
    entry.lru = lru_.begin();
}

bool MemoryCache::get(const TileKey &key, std::vector<std::uint8_t> &bytes) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return false;
    touch(found->second, key);
    bytes = found->second.bytes;
    return true;
}

void MemoryCache::put(const TileKey &key, std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > budget_bytes_) return;
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
        used_bytes_ -= found->second.bytes.size();
        found->second.bytes = std::move(bytes);
        used_bytes_ += found->second.bytes.size();
        touch(found->second, key);
    } else {
        lru_.push_front(key);
        Entry entry{std::move(bytes), lru_.begin()};
        used_bytes_ += entry.bytes.size();
        entries_.emplace(key, std::move(entry));
    }
    trim();
}

void MemoryCache::trim() {
    while (used_bytes_ > budget_bytes_ && !lru_.empty()) {
        const TileKey victim = lru_.back();
        lru_.pop_back();
        const auto found = entries_.find(victim);
        if (found == entries_.end()) continue;
        used_bytes_ -= found->second.bytes.size();
        entries_.erase(found);
    }
}

void MemoryCache::clear() {
    entries_.clear();
    lru_.clear();
    used_bytes_ = 0;
}

} // namespace vitamaps
