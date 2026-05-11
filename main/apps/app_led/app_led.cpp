/**
 * @file app_led.cpp
 * @brief NeoLED control UI. LCD shows a big color swatch that mirrors the
 *        live NeoPixel color, plus a hue scrubber and R/G/B/A shortcuts.
 *
 * Keys: A auto rainbow, R/G/B fixed primary, ; / . tweak hue, HOME exit.
 */
#include "app_led.h"
#include "lgfx/v1/misc/enum.hpp"
#include "spdlog/spdlog.h"
#include <cstdint>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include "neoled.h"

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y       = 1;
static constexpr int CHIP_Y        = 5;
static constexpr int SWATCH_Y      = 19;
static constexpr int SWATCH_H      = 44;
static constexpr int HUE_Y         = 68;
static constexpr int HUE_H         = 14;
static constexpr int FOOTER_Y      = 100;

static const uint32_t COLOR_ACCENT    = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_DIM_TEXT  = (uint32_t)0x9A9A9A;

static uint8_t _pluck_red(uint32_t c)   { return (c >> 16) & 0xFF; }
static uint8_t _pluck_green(uint32_t c) { return (c >>  8) & 0xFF; }
static uint8_t _pluck_blue(uint32_t c)  { return  c        & 0xFF; }

/* Pick black or white text depending on swatch brightness, for legible labels. */
static uint32_t _readable_text(uint32_t bg_rgb)
{
    uint32_t r = _pluck_red(bg_rgb);
    uint32_t g = _pluck_green(bg_rgb);
    uint32_t b = _pluck_blue(bg_rgb);
    /* perceived luminance */
    uint32_t y = (r * 299 + g * 587 + b * 114) / 1000;
    return (y > 140) ? 0x101010 : 0xFFFFFF;
}

static void _push_neo(uint8_t hue, uint32_t* hex_out)
{
    NeoLED::Pixel p = NeoLED::colorWheel(hue);
    NeoLED::update(&p);
    if (hex_out) *hex_out = NeoLED::hexValue(p);
}

static void _draw_ui(MOONCAKE::APPS::AppLed* self,
                     HAL::Hal* hal, uint8_t hue, bool auto_mode);

void AppLed::onCreate()
{
    spdlog::info("{} onCreate", getAppName());
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppLed::onResume()
{
    ANIM_APP_OPEN();
    _data.current_state = state_init;
    _data.neo_inited = false;
    _data.mic_was_running = false;
    _data._hue_val = 85;   /* start red */
    _data._last_update = 0;
    _data.last_key_num = 0;
}

void AppLed::onRunning()
{
    /* One-time NeoLED init (release mic which shares I2S0). */
    if (!_data.neo_inited) {
        if (_data.hal->mic()) {
            if (_data.hal->mic()->isRunning()) {
                _data.hal->mic()->end();
                _data.mic_was_running = true;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (NeoLED::init()) {
            _data.neo_inited = true;
        } else {
            spdlog::error("NeoLED init failed (I2S in use?)");
        }
        /* push initial color */
        uint32_t hex;
        _push_neo(_data._hue_val, &hex);
        _draw_ui(this, _data.hal, _data._hue_val, _data.current_state == state_auto);
    }

    /* Auto-cycle hue */
    if (_data.current_state == state_auto) {
        int64_t now = (int64_t)millis();
        if (now - _data._last_update >= 80) {
            _data._last_update = now;
            _data._hue_val = (uint8_t)((_data._hue_val + 1) & 0xFF);
            uint32_t hex;
            _push_neo(_data._hue_val, &hex);
            _draw_ui(this, _data.hal, _data._hue_val, true);
        }
    }

    /* Input */
    _keyboard->updateKeyList();
    if (_data.last_key_num != (int)_keyboard->keyList().size()) {
        if (_keyboard->keyList().size() > 0) {
            auto key = _keyboard->getKey();
            int v = _keyboard->getKeyValue(key).value_num_first;
            bool changed = false;
            if (v == KEY_R) { _data._hue_val = 85;  _data.current_state = state_manual; changed = true; }
            else if (v == KEY_G) { _data._hue_val = 255; _data.current_state = state_manual; changed = true; }
            else if (v == KEY_B) { _data._hue_val = 170; _data.current_state = state_manual; changed = true; }
            else if (v == KEY_A) { _data.current_state = state_auto; _data._last_update = 0; changed = true; }
            else if (v == KEY_SEMICOLON) {
                _data._hue_val = (uint8_t)(_data._hue_val + 1);
                _data.current_state = state_manual;
                changed = true;
            } else if (v == KEY_DOT) {
                _data._hue_val = (uint8_t)(_data._hue_val - 1);
                _data.current_state = state_manual;
                changed = true;
            }
            if (changed && _data.current_state != state_auto) {
                uint32_t hex;
                _push_neo(_data._hue_val, &hex);
                _draw_ui(this, _data.hal, _data._hue_val, false);
            }
        }
        _data.last_key_num = (int)_keyboard->keyList().size();
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        NeoLED::Pixel off = NeoLED::makePixel(0, 0, 0);
        NeoLED::update(&off);
        NeoLED::destroy();
        if (_data.mic_was_running && _data.hal->mic()) {
            _data.hal->mic()->begin();
        }
        delay(50);
        destroyApp();
    }
}

void AppLed::onDestroy()
{
    NeoLED::destroy();
    if (_data.mic_was_running && _data.hal && _data.hal->mic()) {
        _data.hal->mic()->begin();
    }
}

/* ---------- UI ---------- */
static void _draw_ui(AppLed* /*self*/, HAL::Hal* hal, uint8_t hue, bool auto_mode)
{
    LGFX_Sprite* c = hal->canvas();
    int cw = c->width();

    NeoLED::Pixel p = NeoLED::colorWheel(hue);
    uint32_t hex = NeoLED::hexValue(p);
    uint8_t r = _pluck_red(hex);
    uint8_t g = _pluck_green(hex);
    uint8_t b = _pluck_blue(hex);

    c->fillScreen(THEME_COLOR_BG);

    /* Title */
    c->setFont(FONT_REPL);
    c->setTextSize(1);
    c->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    c->setCursor(3, TITLE_Y);
    c->print("NEO LED");

    /* Mode chip (top-right) */
    c->setFont(FONT_SMALL);
    c->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    char chip[16];
    snprintf(chip, sizeof(chip), "%s h%u", auto_mode ? "AUTO" : "FIX", (unsigned)hue);
    c->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);

    /* Big swatch — synced to live NeoPixel color */
    c->fillSmoothRoundRect(2, SWATCH_Y, cw - 4, SWATCH_H, 4, hex);
    c->drawRoundRect(2, SWATCH_Y, cw - 4, SWATCH_H, 4, (uint32_t)0x000000);

    /* RGB readout overlaid on swatch with readable contrast */
    uint32_t txt = _readable_text(hex);
    c->setFont(FONT_REPL);
    c->setTextColor(txt, hex);
    char rgb[24];
    snprintf(rgb, sizeof(rgb), "R %3u  G %3u  B %3u", (unsigned)r, (unsigned)g, (unsigned)b);
    c->setCursor(10, SWATCH_Y + (SWATCH_H - 16) / 2);
    c->print(rgb);

    /* Hue gradient strip */
    int hx0 = 4, hx1 = cw - 4;
    int strip_w = hx1 - hx0;
    for (int i = 0; i < strip_w; i++) {
        uint8_t hv = (uint8_t)((uint32_t)i * 255 / (strip_w - 1));
        NeoLED::Pixel hp = NeoLED::colorWheel(hv);
        uint32_t hcol = NeoLED::hexValue(hp);
        c->drawFastVLine(hx0 + i, HUE_Y + 2, HUE_H - 4, hcol);
    }
    c->drawRect(hx0, HUE_Y + 1, strip_w, HUE_H - 2, (uint32_t)0x000000);

    /* Hue cursor (triangle pointer at current hue) */
    int cur_x = hx0 + (int)((uint32_t)hue * (strip_w - 1) / 255);
    c->fillTriangle(cur_x - 3, HUE_Y - 2, cur_x + 3, HUE_Y - 2, cur_x, HUE_Y + 2, COLOR_ACCENT);
    c->drawFastVLine(cur_x, HUE_Y + 1, HUE_H - 2, (uint32_t)0xFFFFFF);

    /* Footer */
    c->setFont(FONT_SMALL);
    c->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    c->setCursor(3, FOOTER_Y);
    c->print("A auto  R G B  ;/. hue  HOME");

    hal->canvas_update();
}
