/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

class AppTorch : public mooncake::AppAbility {
public:
    AppTorch();
    ~AppTorch();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void draw();
    void apply_brightness();
    void on_key(int keyCode, bool isSpace);

    int _key_slot_id      = -1;
    uint8_t _brightness   = 255;  // 0..255
    uint32_t _color_idx   = 0;    // 0=white, 1=red, 2=green, 3=blue
    bool _on              = true;
    uint8_t _prev_brightness = 100;
};
