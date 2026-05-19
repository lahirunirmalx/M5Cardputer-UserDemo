/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

class AppResistor : public mooncake::AppAbility {
public:
    AppResistor();
    ~AppResistor();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void draw();
    double compute_ohm() const;
    static double multiplier(uint8_t m);
    void on_key(int keyCode, const char* keyName);

    int _key_slot_id = -1;
    uint8_t _band[4]      = {1, 0, 2, 2};  // 10 Ω ±5% default
    uint8_t _selected_band = 0;
};
