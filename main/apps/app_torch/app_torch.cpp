/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_torch.h"
#include "assets/torch_big.h"
#include "assets/torch_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>

using namespace mooncake;

static const uint32_t COLORS[]      = {0xFFFFFF, 0xFF4040, 0x40FF40, 0x4080FF};
static const char* COLOR_NAMES[]    = {"WHITE", "RED", "GREEN", "BLUE"};
static constexpr int N_COLORS       = sizeof(COLORS) / sizeof(COLORS[0]);
static const uint32_t COLOR_ACCENT  = 0x99FF00;

AppTorch::AppTorch()
{
    setAppInfo().name     = "Torch";
    setAppInfo().userData = new AppIcon_t(image_data_torch_big, image_data_torch_small);
}

AppTorch::~AppTorch()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppTorch::apply_brightness()
{
    GetHAL().display.setBrightness(_on ? _brightness : 8);
}

void AppTorch::draw()
{
    uint32_t fill = _on ? COLORS[_color_idx] : 0x101010;
    GetHAL().canvas.fillScreen(fill);

    int cw = GetHAL().canvas.width();
    int ch = GetHAL().canvas.height();
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextSize(1);
    uint32_t fg = _on ? (uint32_t)0x303030 : COLOR_ACCENT;
    GetHAL().canvas.setTextColor(fg, fill);
    char buf[40];
    snprintf(buf, sizeof(buf), "TORCH  %s  %u%%",
             COLOR_NAMES[_color_idx],
             (unsigned)((_brightness * 100) / 255));
    GetHAL().canvas.setCursor(3, 2);
    GetHAL().canvas.print(buf);

    int bar_y  = ch - 14;
    int bar_w  = cw - 16;
    GetHAL().canvas.drawRect(8, bar_y, bar_w, 6, fg);
    int fill_w = (bar_w - 2) * _brightness / 255;
    if (fill_w > 0) {
        GetHAL().canvas.fillRect(9, bar_y + 1, fill_w, 4, fg);
    }

    GetHAL().canvas.setCursor(3, ch - 7);
    GetHAL().canvas.print("^v bright  SPC on/off  C color  HOME");

    GetHAL().pushCanvas();
}

void AppTorch::on_key(int keyCode, bool isSpace)
{
    bool change = false;
    if (keyCode == KEY_UP) {
        int b        = _brightness + 16;
        _brightness  = (b > 255) ? 255 : (uint8_t)b;
        _on          = true;
        change       = true;
    } else if (keyCode == KEY_DOWN) {
        int b        = _brightness - 16;
        _brightness  = (b < 8) ? 8 : (uint8_t)b;
        change       = true;
    } else if (keyCode == KEY_C) {
        _color_idx = (_color_idx + 1) % N_COLORS;
        change     = true;
    } else if (isSpace) {
        _on    = !_on;
        change = true;
    }
    if (change) {
        apply_brightness();
        draw();
    }
}

void AppTorch::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _prev_brightness = 100;
    _on              = true;
    _brightness      = 255;
    apply_brightness();
    draw();

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) return;
            on_key(keyEvent.keyCode, keyEvent.keyCode == KEY_SPACE);
        });
}

void AppTorch::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppTorch::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
    GetHAL().display.setBrightness(_prev_brightness);
}
