/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

class AppSysinfo : public mooncake::AppAbility {
public:
    AppSysinfo();
    ~AppSysinfo();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void draw();

    uint32_t _last_redraw_ms = 0;
};
