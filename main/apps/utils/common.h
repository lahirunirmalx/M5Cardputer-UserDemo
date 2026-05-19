/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>

#define FW_VERSION "V0.3"

#define ANIM_APP_OPEN()                                                                                     \
    for (int i = 10; i < 123; i += 8) {                                                                     \
        GetHAL().canvas.fillSmoothCircle(GetHAL().canvas.width() / 2, GetHAL().canvas.height() / 2 - 10, i, \
                                         THEME_COLOR_BG);                                                   \
        GetHAL().pushCanvas();                                                                              \
    }

#define ANIM_APP_CLOSE()                                                                                    \
    for (int i = 123; i > 10; i -= 8) {                                                                     \
        update_menu(false);                                                                                 \
        GetHAL().canvas.fillSmoothCircle(GetHAL().canvas.width() / 2, GetHAL().canvas.height() / 2 - 10, i, \
                                         THEME_COLOR_BG);                                                   \
        GetHAL().pushCanvas();                                                                              \
    }

struct AppIcon_t {
public:
    AppIcon_t(const uint16_t* iconBig, const uint16_t* iconSmall)
    {
        this->iconBig   = iconBig;
        this->iconSmall = iconSmall;
    }

    const uint16_t* iconBig;
    const uint16_t* iconSmall;
};
