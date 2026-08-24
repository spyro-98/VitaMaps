#include "ui/text_input.h"

#include "core/log.h"
#include "settings/preferences.h"
#include "ui/font.h"
#include "ui/localization.h"
#include "ui/motion.h"

#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/libime.h>
#include <psp2/sysmodule.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace vitamaps {
namespace {
constexpr std::size_t kMaximumCharacters = 255;
constexpr int kTimeoutFrames = 60 * 120;
constexpr unsigned int kBackground = RGBA8(18, 27, 36, 255);
constexpr unsigned int kPanel = RGBA8(30, 43, 55, 255);
constexpr unsigned int kPrimary = RGBA8(242, 246, 250, 255);
constexpr unsigned int kSecondary = RGBA8(174, 188, 202, 255);
constexpr unsigned int kAccent = RGBA8(88, 190, 255, 255);

bool g_module_loaded = false;
SceUInt32 g_work[SCE_IME_WORK_BUFFER_SIZE / sizeof(SceUInt32)]
    __attribute__((aligned(64)));
SceWChar16 g_initial[kMaximumCharacters + 1];
SceWChar16 g_input[kMaximumCharacters + SCE_IME_MAX_PREEDIT_LENGTH + 1];
volatile int g_entered = 0;
volatile int g_cancelled = 0;
volatile SceUInt32 g_caret = 0;

std::size_t decode_utf8(const char *input, std::uint32_t &codepoint) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(input);
    codepoint = 0xFFFDU;
    if (!bytes || !bytes[0]) return 0;
    if (bytes[0] < 0x80U) {
        codepoint = bytes[0];
        return 1;
    }
    if (bytes[0] >= 0xC2U && bytes[0] <= 0xDFU &&
        (bytes[1] & 0xC0U) == 0x80U) {
        codepoint = ((bytes[0] & 0x1FU) << 6U) | (bytes[1] & 0x3FU);
        return 2;
    }
    if (bytes[0] >= 0xE0U && bytes[0] <= 0xEFU && bytes[1] && bytes[2] &&
        (bytes[1] & 0xC0U) == 0x80U && (bytes[2] & 0xC0U) == 0x80U) {
        const std::uint32_t decoded = ((bytes[0] & 0x0FU) << 12U) |
            ((bytes[1] & 0x3FU) << 6U) | (bytes[2] & 0x3FU);
        if (decoded >= 0x800U &&
            !(decoded >= 0xD800U && decoded <= 0xDFFFU)) {
            codepoint = decoded;
            return 3;
        }
    }
    if (bytes[0] >= 0xF0U && bytes[0] <= 0xF4U && bytes[1] && bytes[2] &&
        bytes[3] && (bytes[1] & 0xC0U) == 0x80U &&
        (bytes[2] & 0xC0U) == 0x80U && (bytes[3] & 0xC0U) == 0x80U) {
        const std::uint32_t decoded = ((bytes[0] & 0x07U) << 18U) |
            ((bytes[1] & 0x3FU) << 12U) |
            ((bytes[2] & 0x3FU) << 6U) | (bytes[3] & 0x3FU);
        if (decoded >= 0x10000U && decoded <= 0x10FFFFU) {
            codepoint = decoded;
            return 4;
        }
    }
    return 1;
}

void utf8_to_utf16(const char *input, SceWChar16 *output,
                   std::size_t output_count) {
    std::size_t input_offset = 0;
    std::size_t output_offset = 0;
    while (input && input[input_offset] && output_offset + 1 < output_count) {
        std::uint32_t codepoint = 0;
        const std::size_t length = decode_utf8(input + input_offset, codepoint);
        if (!length) break;
        input_offset += length;
        if (codepoint <= 0xFFFFU) {
            output[output_offset++] = static_cast<SceWChar16>(codepoint);
        } else {
            if (output_offset + 2 >= output_count) break;
            codepoint -= 0x10000U;
            output[output_offset++] = static_cast<SceWChar16>(
                0xD800U + (codepoint >> 10U));
            output[output_offset++] = static_cast<SceWChar16>(
                0xDC00U + (codepoint & 0x3FFU));
        }
    }
    output[output_offset] = 0;
}

int utf16_to_utf8(const SceWChar16 *input, char *output,
                  std::size_t capacity) {
    if (!output || capacity == 0) return -1;
    std::size_t offset = 0;
    for (std::size_t index = 0; input && input[index]; ++index) {
        std::uint32_t value = input[index];
        if (value >= 0xD800U && value <= 0xDBFFU &&
            input[index + 1] >= 0xDC00U && input[index + 1] <= 0xDFFFU) {
            value = 0x10000U + ((value - 0xD800U) << 10U) +
                    (input[++index] - 0xDC00U);
        } else if (value >= 0xD800U && value <= 0xDFFFU) {
            value = 0xFFFDU;
        }
        const std::size_t needed = value < 0x80U ? 1U
            : value < 0x800U ? 2U : value < 0x10000U ? 3U : 4U;
        if (offset + needed >= capacity) {
            output[0] = '\0';
            return -1;
        }
        if (value < 0x80U) {
            output[offset++] = static_cast<char>(value);
        } else if (value < 0x800U) {
            output[offset++] = static_cast<char>(0xC0U | (value >> 6U));
            output[offset++] = static_cast<char>(0x80U | (value & 0x3FU));
        } else if (value < 0x10000U) {
            output[offset++] = static_cast<char>(0xE0U | (value >> 12U));
            output[offset++] = static_cast<char>(0x80U |
                                                  ((value >> 6U) & 0x3FU));
            output[offset++] = static_cast<char>(0x80U | (value & 0x3FU));
        } else {
            output[offset++] = static_cast<char>(0xF0U | (value >> 18U));
            output[offset++] = static_cast<char>(0x80U |
                                                  ((value >> 12U) & 0x3FU));
            output[offset++] = static_cast<char>(0x80U |
                                                  ((value >> 6U) & 0x3FU));
            output[offset++] = static_cast<char>(0x80U | (value & 0x3FU));
        }
    }
    output[offset] = '\0';
    return 0;
}

void event_handler(void *, const SceImeEventData *event) {
    if (!event) return;
    if (event->id == SCE_IME_EVENT_UPDATE_TEXT)
        g_caret = event->param.text.caretIndex;
    else if (event->id == SCE_IME_EVENT_UPDATE_CARET)
        g_caret = event->param.caretIndex;
    else if (event->id == SCE_IME_EVENT_PRESS_ENTER)
        g_entered = 1;
    else if (event->id == SCE_IME_EVENT_PRESS_CLOSE)
        g_cancelled = 1;
}
} // namespace

int ui_text_input(vita2d_font *small_font, vita2d_font *body_font,
                  vita2d_font *display_font, const char *title,
                  const char *initial, char *output,
                  std::size_t output_capacity) {
    if (!output || output_capacity < 2 || !vita2d_get_context() ||
        !vita2d_get_current_fb())
        return -1;
    utf8_to_utf16(initial ? initial : "", g_initial,
                  sizeof(g_initial) / sizeof(g_initial[0]));
    std::memset(g_input, 0, sizeof(g_input));
    std::memcpy(g_input, g_initial, sizeof(g_initial));
    output[0] = '\0';
    g_entered = 0;
    g_cancelled = 0;
    g_caret = 0;
    while (g_initial[g_caret]) ++g_caret;

    if (!g_module_loaded) {
        const int module = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        log_printf("sceSysmoduleLoadModule(IME) -> 0x%08X",
                   static_cast<unsigned>(module));
        if (module < 0) return module;
        g_module_loaded = true;
    }

    SceImeParam parameter;
    sceImeParamInit(&parameter);
    // Keep the input screen coherent with the selected UI language. Do not
    // expose a simultaneous multi-language sample/selector page.
    parameter.supportedLanguages = ui_language_ime_mask();
    parameter.languagesForced = SCE_TRUE;
    parameter.type = SCE_IME_TYPE_DEFAULT;
    parameter.work = g_work;
    parameter.handler = event_handler;
    parameter.initialText = g_initial;
    parameter.inputTextBuffer = g_input;
    parameter.maxTextLength = static_cast<SceUInt32>(std::min(
        output_capacity - 1, kMaximumCharacters));
    parameter.enterLabel = SCE_IME_ENTER_LABEL_DEFAULT;
    const int opened = sceImeOpen(&parameter);
    log_printf("sceImeOpen(%s) -> 0x%08X", title ? title : "input",
               static_cast<unsigned>(opened));
    if (opened < 0) return opened;

    char live[(kMaximumCharacters + SCE_IME_MAX_PREEDIT_LENGTH) * 4 + 1];
    bool too_long = false;
    int result = -1;
    bool open = true;
    UiMotionValue entrance(0.0F, 18.0F);
    entrance.set_target(1.0F);
    std::uint64_t previous_tick = sceKernelGetProcessTimeWide();
    double animation_seconds = 0.0;
    for (int frame = 0; frame < kTimeoutFrames; ++frame) {
        const std::uint64_t now = sceKernelGetProcessTimeWide();
        const double dt = std::clamp(
            static_cast<double>(now - previous_tick) / 1000000.0,
            1.0 / 240.0, 0.05);
        previous_tick = now;
        entrance.tick(dt, preferences_reduce_motion());
        if (!preferences_reduce_motion()) animation_seconds += dt;
        utf16_to_utf8(g_input, live, sizeof(live));
        char fitted[512];
        ui_font_fit_text(body_font, 20, live, fitted, sizeof(fitted), 780);
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F, kBackground);
        vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 70.0F, kPanel);
        vita2d_draw_rectangle(0.0F, 69.0F, 960.0F, 1.0F, kAccent);
        ui_font_draw_text(display_font, 36, 48, kPrimary, 28,
                          title ? title : ui_text(UiText::TextEntry));
        ui_draw_focus_glow(70.0F, 118.0F, 820.0F, 82.0F, kAccent,
                           animation_seconds, 0.0F, entrance.value());
        vita2d_draw_rectangle(70.0F, 118.0F, 820.0F, 82.0F, kPanel);
        ui_font_draw_text(body_font, 92, 168, kPrimary, 20,
                          fitted[0] ? fitted : " ");
        const int caret_x = std::min(850,
            92 + ui_font_text_width(body_font, 20, fitted));
        if (preferences_reduce_motion() ||
            ((sceKernelGetProcessTimeWide() / 500000ULL) & 1U) == 0U)
            vita2d_draw_rectangle(static_cast<float>(caret_x), 140.0F,
                                  2.0F, 32.0F, kAccent);
        ui_font_draw_text(small_font, 72, 235,
                          too_long ? RGBA8(255, 116, 116, 255) : kSecondary,
                          16, ui_text(too_long ? UiText::TextTooLong
                                             : UiText::TextConfirm));
        const float veil = 1.0F - entrance.value();
        if (veil > 0.001F)
            vita2d_draw_rectangle(0.0F, 0.0F, 960.0F, 544.0F,
                                  RGBA8(6, 12, 18,
                                      static_cast<int>(235.0F * veil)));
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        const int update = sceImeUpdate();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();

        if (g_entered) {
            if (utf16_to_utf8(g_input, output, output_capacity) < 0) {
                g_entered = 0;
                too_long = true;
                continue;
            }
            sceImeClose();
            open = false;
            result = output[0] ? 1 : 0;
            break;
        }
        if (g_cancelled) {
            sceImeClose();
            open = false;
            result = 0;
            break;
        }
        if (update < 0) {
            log_printf("sceImeUpdate frame=%d -> 0x%08X", frame,
                       static_cast<unsigned>(update));
            sceImeClose();
            open = false;
            result = update;
            break;
        }
    }
    if (open) sceImeClose();
    return result;
}

} // namespace vitamaps
