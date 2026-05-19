/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_resistor.h"
#include "assets/resistor_big.h"
#include "assets/resistor_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <apps/utils/seven_seg/seven_seg.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace mooncake;

static constexpr int TITLE_Y      = 1;
static constexpr int CHIP_Y       = 5;
static constexpr int DISPLAY_Y    = 19;
static constexpr int DISPLAY_H    = 25;
static constexpr int SEG_Y        = 22;
static constexpr int RES_AREA_Y   = 47;
static constexpr int RES_AREA_H   = 46;
static constexpr int BODY_W       = 160;
static constexpr int BODY_H       = 30;
static constexpr int BODY_X       = (206 - BODY_W) / 2;
static constexpr int BODY_Y       = RES_AREA_Y + (RES_AREA_H - BODY_H) / 2;
static constexpr int BODY_R       = 8;
static constexpr int BAND_W       = 18;
static constexpr int BAND_GAP     = 8;
static constexpr int BAND_INSET_Y = 3;
static constexpr int BAND_Y       = BODY_Y + BAND_INSET_Y;
static constexpr int BAND_H       = BODY_H - 2 * BAND_INSET_Y;
static constexpr int BANDS_TOTAL_W = 4 * BAND_W + 3 * BAND_GAP;
static constexpr int BANDS_X0     = BODY_X + (BODY_W - BANDS_TOTAL_W) / 2;
static constexpr int LEAD_Y       = BODY_Y + BODY_H / 2;
static constexpr int LEAD_T       = 2;
static constexpr int FOOTER_Y     = 100;

static const uint32_t COLOR_ACCENT     = 0x99FF00;
static const uint32_t COLOR_BODY       = 0xC4A574;
static const uint32_t COLOR_LEAD       = 0xC0C0C0;
static const uint32_t COLOR_DISPLAY_BG = 0x1E1E22;
static const uint32_t COLOR_SEG_OFF    = 0x252528;
static const uint32_t COLOR_DIM_TEXT   = 0x9A9A9A;

static const uint32_t BAND_COLORS[] = {
    0x000000, 0x6F4F1F, 0xE60000, 0xFF8000, 0xFFFF00, 0x00A000,
    0x0040FF, 0x8000FF, 0x808080, 0xFFFFFF, 0xD4AF37, 0xC0C0C0,
};
static const char* COLOR_NAMES[] = {
    "BLACK", "BROWN", "RED", "ORANGE", "YELLOW", "GREEN",
    "BLUE",  "VIOLET", "GREY", "WHITE", "GOLD",  "SILVER",
};
static const uint8_t TOL_COLOR_IDX[] = {1, 2, 10, 11};
static const char*   TOL_LABELS[]    = {"+-1%", "+-2%", "+-5%", "+-10%"};

AppResistor::AppResistor()
{
    setAppInfo().name     = "Resistor";
    setAppInfo().userData = new AppIcon_t(image_data_resistor_big, image_data_resistor_small);
}

AppResistor::~AppResistor()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

double AppResistor::multiplier(uint8_t m)
{
    if (m <= 9) return pow(10.0, (double)m);
    if (m == 10) return 0.1;
    return 0.01;
}

double AppResistor::compute_ohm() const
{
    double base = (double)(_band[0] * 10 + _band[1]);
    return base * multiplier(_band[2]);
}

void AppResistor::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    int cw = GetHAL().canvas.width();

    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, TITLE_Y);
    GetHAL().canvas.print("Resistor");

    char chip[24];
    uint8_t tol_idx = _band[3] < 4 ? _band[3] : 2;
    if (_selected_band == 3) {
        snprintf(chip, sizeof(chip), "TOL %s", TOL_LABELS[tol_idx]);
    } else {
        uint8_t ci    = _band[_selected_band];
        const char* cn = (ci < 12) ? COLOR_NAMES[ci] : "?";
        snprintf(chip, sizeof(chip), "B%d %s", _selected_band + 1, cn);
    }
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);
    GetHAL().canvas.setFont(FONT_REPL);

    GetHAL().canvas.fillSmoothRoundRect(2, DISPLAY_Y, cw - 4, DISPLAY_H, 4, COLOR_DISPLAY_BG);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_DISPLAY_BG);
    GetHAL().canvas.setCursor(8, SEG_Y + 7);
    GetHAL().canvas.print(TOL_LABELS[tol_idx]);

    double ohm = compute_ohm();
    char num_buf[16];
    char unit_buf[4] = {0};
    double dv        = ohm;
    if (ohm >= 1e6) {
        dv = ohm / 1e6;
        strcpy(unit_buf, "M");
    } else if (ohm >= 1e3) {
        dv = ohm / 1e3;
        strcpy(unit_buf, "k");
    }
    if (dv < 100.0)       snprintf(num_buf, sizeof(num_buf), "%.2f", dv);
    else if (dv < 1000.0) snprintf(num_buf, sizeof(num_buf), "%.1f", dv);
    else                  snprintf(num_buf, sizeof(num_buf), "%.0f", dv);

    GetHAL().canvas.setFont(FONT_REPL);
    int unit_w    = unit_buf[0] ? GetHAL().canvas.textWidth(unit_buf) : 0;
    int unit_pad  = unit_buf[0] ? 4 : 0;
    int seg_right = cw - 6 - unit_w - unit_pad;
    SEVEN_SEG::draw_str(&GetHAL().canvas, num_buf, seg_right, SEG_Y, COLOR_ACCENT, COLOR_SEG_OFF);
    if (unit_buf[0]) {
        GetHAL().canvas.setTextColor(COLOR_ACCENT, COLOR_DISPLAY_BG);
        GetHAL().canvas.setCursor(cw - 6 - unit_w, SEG_Y + 2);
        GetHAL().canvas.print(unit_buf);
    }

    GetHAL().canvas.fillRect(0, LEAD_Y - LEAD_T / 2, BODY_X, LEAD_T, COLOR_LEAD);
    GetHAL().canvas.fillRect(BODY_X + BODY_W, LEAD_Y - LEAD_T / 2, cw - (BODY_X + BODY_W), LEAD_T, COLOR_LEAD);
    GetHAL().canvas.fillSmoothRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, BODY_R, COLOR_BODY);

    static const char* BAND_TAGS[4] = {"D1", "D2", "xN", "+-"};
    for (int i = 0; i < 4; i++) {
        int x       = BANDS_X0 + i * (BAND_W + BAND_GAP);
        uint8_t idx = (i == 3) ? TOL_COLOR_IDX[tol_idx] : _band[i];
        if (idx < 12) {
            uint32_t c = BAND_COLORS[idx];
            GetHAL().canvas.fillRect(x, BAND_Y, BAND_W, BAND_H, c);
            if (i == _selected_band) {
                GetHAL().canvas.drawRect(x - 2, BAND_Y - 2, BAND_W + 4, BAND_H + 4, COLOR_ACCENT);
                GetHAL().canvas.drawRect(x - 1, BAND_Y - 1, BAND_W + 2, BAND_H + 2, COLOR_ACCENT);
                int cx_arrow = x + BAND_W / 2;
                int ay       = BAND_Y - 6;
                GetHAL().canvas.fillTriangle(cx_arrow - 3, ay, cx_arrow + 3, ay, cx_arrow, ay + 4, COLOR_ACCENT);
            }
        }
        GetHAL().canvas.setFont(FONT_SMALL);
        uint32_t tag_col = (i == _selected_band) ? COLOR_ACCENT : COLOR_DIM_TEXT;
        GetHAL().canvas.setTextColor(tag_col, THEME_COLOR_BG);
        int tw = GetHAL().canvas.textWidth(BAND_TAGS[i]);
        GetHAL().canvas.setCursor(x + (BAND_W - tw) / 2, BAND_Y + BAND_H + 2);
        GetHAL().canvas.print(BAND_TAGS[i]);
    }

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("<>band  ^v val  0-9 set  G/S  HOME");

    GetHAL().pushCanvas();
}

void AppResistor::on_key(int keyCode, const char* keyName)
{
    // Navigation: left/right switches band
    if (keyCode == KEY_LEFT) {
        if (_selected_band > 0) _selected_band--;
        draw();
        return;
    }
    if (keyCode == KEY_RIGHT) {
        if (_selected_band < 3) _selected_band++;
        draw();
        return;
    }

    int max_idx = (_selected_band == 2) ? 11
                : (_selected_band == 3) ? 3
                                        : 9;

    if (keyCode == KEY_UP) {
        uint8_t v             = _band[_selected_band];
        v                     = (v >= max_idx) ? 0 : (uint8_t)(v + 1);
        _band[_selected_band] = v;
        draw();
        return;
    }
    if (keyCode == KEY_DOWN) {
        uint8_t v             = _band[_selected_band];
        v                     = (v == 0) ? (uint8_t)max_idx : (uint8_t)(v - 1);
        _band[_selected_band] = v;
        draw();
        return;
    }

    // Direct numeric entry — match by character (works regardless of shift)
    if (keyName != nullptr && keyName[0] != '\0' && keyName[1] == '\0') {
        char c = keyName[0];
        if (c >= '0' && c <= '9') {
            int digit = c - '0';
            if (_selected_band <= 2) {
                if (digit <= max_idx) _band[_selected_band] = (uint8_t)digit;
            } else if (_selected_band == 3) {
                // tolerance: 1→1% (idx 0), 2→2% (1), 5→5% (2), 0→10% (3)
                if (c == '0')      _band[3] = 3;
                else if (c == '1') _band[3] = 0;
                else if (c == '2') _band[3] = 1;
                else if (c == '5') _band[3] = 2;
            }
            draw();
            return;
        }
    }

    if ((keyCode == KEY_G || keyCode == KEY_S) && _selected_band == 2) {
        _band[2] = (keyCode == KEY_G) ? 10 : 11;
        draw();
        return;
    }
}

void AppResistor::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    draw();
    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) return;
            on_key(keyEvent.keyCode, keyEvent.keyName);
        });
}

void AppResistor::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppResistor::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}
