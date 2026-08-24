#pragma once

#include <cstddef>

namespace vitamaps {

// Diagnostic history is always retained in a bounded RAM ring. The setting
// controls only atomic persistence to ux0, never whether diagnostics exist.
void log_init();
void log_set_disk_enabled(bool enabled);
bool log_disk_enabled();
void log_printf(const char *format, ...);
int log_save();
std::size_t log_size();
const char *log_path();

} // namespace vitamaps
