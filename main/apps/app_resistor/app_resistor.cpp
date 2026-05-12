/**
 * @file app_resistor.cpp
 * @brief 4-band resistor calculator with 7-segment value display.
 *
 * Keys:  1-4   select band
 *        0-9   set band value (band 3 uses 1,2,5,0 → +-1/2/5/10 %)
 *        G/S   gold/silver multiplier (only on band 3, the multiplier band)
 *        HOME  exit
 */
#include "app_resistor.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y       = 1;
static constexpr int CHIP_Y        = 5;
static constexpr int DISPLAY_Y     = 19;
static constexpr int DISPLAY_H     = 25;
static constexpr int SEG_Y         = 22;
static constexpr int RES_AREA_Y    = 47;
static constexpr int RES_AREA_H    = 46;
static constexpr int BODY_W        = 160;
static constexpr int BODY_H        = 30;
static constexpr int BODY_X        = (206 - BODY_W) / 2;                     /* 23 */
static constexpr int BODY_Y        = RES_AREA_Y + (RES_AREA_H - BODY_H) / 2; /* 55 */
static constexpr int BODY_R        = 8;
static constexpr int BAND_W        = 18;
static constexpr int BAND_GAP      = 8;
static constexpr int BAND_INSET_Y  = 3;
static constexpr int BAND_Y        = BODY_Y + BAND_INSET_Y;                  /* 58 */
static constexpr int BAND_H        = BODY_H - 2 * BAND_INSET_Y;              /* 24 */
static constexpr int BANDS_TOTAL_W = 4 * BAND_W + 3 * BAND_GAP;              /* 96 */
static constexpr int BANDS_X0      = BODY_X + (BODY_W - BANDS_TOTAL_W) / 2;  /* 55 */
static constexpr int LEAD_Y        = BODY_Y + BODY_H / 2;                    /* 70 */
static constexpr int LEAD_T        = 2;
static constexpr int FOOTER_Y      = 100;

/* 7-segment geometry lives in the shared SEVEN_SEG namespace */
#include "../utils/seven_seg/seven_seg.h"

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_BODY       = (uint32_t)0xC4A574;
static const uint32_t COLOR_LEAD       = (uint32_t)0xC0C0C0;
static const uint32_t COLOR_DISPLAY_BG = (uint32_t)0x1E1E22;
static const uint32_t COLOR_SEG_OFF    = (uint32_t)0x252528;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;

/* Standard resistor color-band values (RGB888). Order matches the digit
 * the band encodes: 0 Black, 1 Brown, 2 Red, 3 Orange, 4 Yellow, 5 Green,
 * 6 Blue, 7 Violet, 8 Grey, 9 White, 10 Gold, 11 Silver. The canvas is
 * 24-bit so these are full RGB hex, not RGB565. */
static const uint32_t BAND_COLORS[] = {
    0x000000, /* 0  Black  */
    0x6F4F1F, /* 1  Brown  */
    0xE60000, /* 2  Red    */
    0xFF8000, /* 3  Orange */
    0xFFFF00, /* 4  Yellow */
    0x00A000, /* 5  Green  */
    0x0040FF, /* 6  Blue   */
    0x8000FF, /* 7  Violet */
    0x808080, /* 8  Grey   */
    0xFFFFFF, /* 9  White  */
    0xD4AF37, /* 10 Gold   */
    0xC0C0C0  /* 11 Silver */
};

static const char* COLOR_NAMES[] = {
    "BLACK", "BROWN", "RED", "ORANGE", "YELLOW", "GREEN",
    "BLUE",  "VIOLET", "GREY", "WHITE", "GOLD",  "SILVER"
};

/* Tolerance index (0..3) → real band color and label.
 * +-1% brown, +-2% red, +-5% gold, +-10% silver. */
static const uint8_t TOL_COLOR_IDX[] = { 1, 2, 10, 11 };
static const char*   TOL_LABELS[]    = { "+-1%", "+-2%", "+-5%", "+-10%" };

double AppResistor::_multiplier(uint8_t m)
{
    if (m <= 9) return pow(10.0, (double)m);
    if (m == 10) return 0.1;   /* gold */
    return 0.01;               /* silver */
}

double AppResistor::_compute_ohm() const
{
    double base = (double)(_data.band[0] * 10 + _data.band[1]);
    return base * _multiplier(_data.band[2]);
}

void AppResistor::_draw()
{
    _canvas_clear();
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    int cw = _canvas->width();

    /* Title */
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("Resistor");

    /* Selected band chip on the top-right (name and color) */
    char chip[24];
    uint8_t tol_idx = _data.band[3] < 4 ? _data.band[3] : 2;
    if (_data.selected_band == 3) {
        snprintf(chip, sizeof(chip), "TOL %s", TOL_LABELS[tol_idx]);
    } else {
        uint8_t ci = _data.band[_data.selected_band];
        const char* cn = (ci < 12) ? COLOR_NAMES[ci] : "?";
        snprintf(chip, sizeof(chip), "B%d %s", _data.selected_band + 1, cn);
    }
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);
    _canvas->setFont(FONT_REPL);

    /* Display panel */
    _canvas->fillSmoothRoundRect(2, DISPLAY_Y, cw - 4, DISPLAY_H, 4, COLOR_DISPLAY_BG);

    /* Tolerance label (left side of display) */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_DISPLAY_BG);
    _canvas->setCursor(8, SEG_Y + 7);
    _canvas->print(TOL_LABELS[tol_idx]);

    /* Format value with SI prefix */
    double ohm = _compute_ohm();
    char num_buf[16];
    char unit_buf[4] = { 0 };
    double dv = ohm;
    if (ohm >= 1e6)      { dv = ohm / 1e6; strcpy(unit_buf, "M"); }
    else if (ohm >= 1e3) { dv = ohm / 1e3; strcpy(unit_buf, "k"); }
    if (dv < 100.0)      snprintf(num_buf, sizeof(num_buf), "%.2f", dv);
    else if (dv < 1000.0) snprintf(num_buf, sizeof(num_buf), "%.1f", dv);
    else                  snprintf(num_buf, sizeof(num_buf), "%.0f", dv);

    /* Unit suffix (small font) right after the digits */
    _canvas->setFont(FONT_REPL);
    int unit_w = unit_buf[0] ? _canvas->textWidth(unit_buf) : 0;
    int unit_pad = unit_buf[0] ? 4 : 0;
    int seg_right = cw - 6 - unit_w - unit_pad;
    SEVEN_SEG::draw_str(_canvas, num_buf, seg_right, SEG_Y, COLOR_ACCENT, COLOR_SEG_OFF);
    if (unit_buf[0]) {
        _canvas->setTextColor(COLOR_ACCENT, COLOR_DISPLAY_BG);
        _canvas->setCursor(cw - 6 - unit_w, SEG_Y + 2);
        _canvas->print(unit_buf);
    }

    /* Resistor visual: leads, body, bands */
    _canvas->fillRect(0, LEAD_Y - LEAD_T / 2, BODY_X, LEAD_T, COLOR_LEAD);
    _canvas->fillRect(BODY_X + BODY_W, LEAD_Y - LEAD_T / 2, cw - (BODY_X + BODY_W), LEAD_T, COLOR_LEAD);
    _canvas->fillSmoothRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, BODY_R, COLOR_BODY);

    static const char* BAND_TAGS[4] = { "D1", "D2", "xN", "+-" };
    for (int i = 0; i < 4; i++) {
        int x = BANDS_X0 + i * (BAND_W + BAND_GAP);
        uint8_t idx;
        if (i == 3) idx = TOL_COLOR_IDX[tol_idx];
        else        idx = _data.band[i];
        if (idx < 12) {
            uint32_t c = BAND_COLORS[idx];
            _canvas->fillRect(x, BAND_Y, BAND_W, BAND_H, c);
            if (i == _data.selected_band) {
                /* Thick accent border + downward caret above the band */
                _canvas->drawRect(x - 2, BAND_Y - 2, BAND_W + 4, BAND_H + 4, COLOR_ACCENT);
                _canvas->drawRect(x - 1, BAND_Y - 1, BAND_W + 2, BAND_H + 2, COLOR_ACCENT);
                int cx_arrow = x + BAND_W / 2;
                int ay = BAND_Y - 6;
                _canvas->fillTriangle(cx_arrow - 3, ay, cx_arrow + 3, ay, cx_arrow, ay + 4, COLOR_ACCENT);
            }
        }
        /* Small role tag under the band (D1/D2/xN/+-) */
        _canvas->setFont(FONT_SMALL);
        uint32_t tag_col = (i == _data.selected_band) ? COLOR_ACCENT : COLOR_DIM_TEXT;
        _canvas->setTextColor(tag_col, THEME_COLOR_BG);
        int tw = _canvas->textWidth(BAND_TAGS[i]);
        _canvas->setCursor(x + (BAND_W - tw) / 2, BAND_Y + BAND_H + 2);
        _canvas->print(BAND_TAGS[i]);
    }

    /* Footer */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("<>band  ^v val  0-9 set  G/S  HOME");

    _canvas_update();
}

void AppResistor::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppResistor::onResume()
{
    ANIM_APP_OPEN();
    _draw();
}

void AppResistor::onRunning()
{
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& hid = _keyboard->keysState().hidKey;

            for (int k : hid) {
                /* Navigation: left/right switches band */
                if (k == KEY_LEFT || k == KEY_COMMA) {
                    if (_data.selected_band > 0) _data.selected_band--;
                    _draw();
                    goto key_done;
                }
                if (k == KEY_RIGHT || k == KEY_KPSLASH) {
                    if (_data.selected_band < 3) _data.selected_band++;
                    _draw();
                    goto key_done;
                }

                /* Value: up/down increments/decrements the current band's index.
                 * Range per band:
                 *   0,1 (digit bands)  -> 0..9
                 *   2   (multiplier)   -> 0..11 (incl. gold=10, silver=11)
                 *   3   (tolerance)    -> 0..3 (+-1, +-2, +-5, +-10) */
                int max_idx = (_data.selected_band == 2) ? 11
                            : (_data.selected_band == 3) ? 3 : 9;
                if (k == KEY_UP || k == KEY_SEMICOLON) {
                    uint8_t v = _data.band[_data.selected_band];
                    v = (v >= max_idx) ? 0 : (uint8_t)(v + 1);
                    _data.band[_data.selected_band] = v;
                    _draw();
                    goto key_done;
                }
                if (k == KEY_DOWN || k == KEY_DOT) {
                    uint8_t v = _data.band[_data.selected_band];
                    v = (v == 0) ? (uint8_t)max_idx : (uint8_t)(v - 1);
                    _data.band[_data.selected_band] = v;
                    _draw();
                    goto key_done;
                }

                /* Direct numeric entry (works on all bands now since band selection
                 * uses arrows, not 1-4 keys). */
                if (k >= KEY_0 && k <= KEY_9) {
                    int digit = (k == KEY_0) ? 0 : (k - KEY_1 + 1);
                    if (_data.selected_band <= 2) {
                        _data.band[_data.selected_band] = (uint8_t)(digit <= 9 ? digit : 0);
                    } else if (_data.selected_band == 3) {
                        /* tolerance: 1->1% (idx 0), 2->2% (1), 5->5% (2), 0->10% (3) */
                        if (k == KEY_0)      _data.band[3] = 3;
                        else if (k == KEY_1) _data.band[3] = 0;
                        else if (k == KEY_2) _data.band[3] = 1;
                        else if (k == KEY_5) _data.band[3] = 2;
                    }
                    _draw();
                    goto key_done;
                }
                if (k == KEY_G && _data.selected_band == 2) { _data.band[2] = 10; _draw(); goto key_done; }
                if (k == KEY_S && _data.selected_band == 2) { _data.band[2] = 11; _draw(); goto key_done; }
            }
key_done:
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

void AppResistor::onDestroy() {}
