/**
 * @file app_torch.cpp
 * @brief Flashlight: full-screen color fill, adjustable brightness, color cycle.
 *
 * Keys:  ^ / v   brightness up/down
 *        SPACE   toggle on/off
 *        C       cycle color (white/red/green/blue)
 *        HOME    exit
 */
#include "app_torch.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <cstdio>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)
#define _display _data.hal->display()

static const uint32_t COLORS[] = {
    (uint32_t)0xFFFFFF,   /* white */
    (uint32_t)0xFF4040,   /* red */
    (uint32_t)0x40FF40,   /* green */
    (uint32_t)0x4080FF,   /* blue */
};
static const char* COLOR_NAMES[] = { "WHITE", "RED", "GREEN", "BLUE" };
static constexpr int N_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);

static const uint32_t COLOR_ACCENT   = (uint32_t)0x99FF00;
static const uint32_t COLOR_DIM_TEXT = (uint32_t)0x9A9A9A;

void AppTorch::_apply_brightness()
{
    _display->setBrightness(_data.on ? _data.brightness : 8);
}

void AppTorch::_draw()
{
    uint32_t fill = _data.on ? COLORS[_data.color_idx] : (uint32_t)0x101010;
    _canvas->fillScreen(fill);

    /* Status info in top-left */
    int cw = _canvas->width();
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextSize(1);
    uint32_t fg = _data.on ? (uint32_t)0x303030 : COLOR_ACCENT;
    _canvas->setTextColor(fg, fill);
    char buf[40];
    snprintf(buf, sizeof(buf), "TORCH  %s  %u%%",
             COLOR_NAMES[_data.color_idx],
             (unsigned)((_data.brightness * 100) / 255));
    _canvas->setCursor(3, 2);
    _canvas->print(buf);

    /* Brightness bar near bottom */
    int bar_y = _canvas->height() - 14;
    int bar_w = cw - 16;
    _canvas->drawRect(8, bar_y, bar_w, 6, fg);
    int fill_w = (int)((bar_w - 2) * _data.brightness / 255);
    if (fill_w > 0) _canvas->fillRect(9, bar_y + 1, fill_w, 4, fg);

    /* Footer */
    _canvas->setCursor(3, _canvas->height() - 7);
    _canvas->print("^v bright  SPC on/off  C color  HOME");

    _canvas_update();
}

void AppTorch::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppTorch::onResume()
{
    ANIM_APP_OPEN();
    _data.prev_brightness = 100;
    _data.on = true;
    _data.brightness = 255;
    _apply_brightness();
    _draw();
}

void AppTorch::onRunning()
{
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();

            bool change = false;
            for (int k : st.hidKey) {
                if (k == KEY_UP) {
                    int b = _data.brightness + 16;
                    _data.brightness = (b > 255) ? 255 : (uint8_t)b;
                    _data.on = true;
                    change = true;
                } else if (k == KEY_DOWN) {
                    int b = _data.brightness - 16;
                    _data.brightness = (b < 8) ? 8 : (uint8_t)b;
                    change = true;
                } else if (k == KEY_C) {
                    _data.color_idx = (_data.color_idx + 1) % N_COLORS;
                    change = true;
                }
            }
            if (st.space) {
                _data.on = !_data.on;
                change = true;
            }
            if (change) {
                _apply_brightness();
                _draw();
            }
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppTorch::onDestroy()
{
    _display->setBrightness(_data.prev_brightness);
}
