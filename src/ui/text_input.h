#pragma once

#include <cstddef>
#include <vita2d.h>

namespace vitamaps {

// Direct sceIme keyboard. It deliberately does not initialize AppUtil or a
// CommonDialog, matching the startup-safe path already proven by VitaTube.
int ui_text_input(vita2d_font *small_font, vita2d_font *body_font,
                  vita2d_font *display_font, const char *title,
                  const char *initial, char *output,
                  std::size_t output_capacity);

} // namespace vitamaps
