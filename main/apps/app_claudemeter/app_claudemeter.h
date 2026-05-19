/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 *
 * Claude Meter — polls /usage on an internal HTTP service every 5 min and
 * shows 5h + 7d usage as big bars with countdowns. Polling lives in a
 * static FreeRTOS task that survives `close()`, so the meter keeps
 * running in the background after HOME / B. Press X to stop polling +
 * close.
 *
 * dev-main's NeoLED blink-on-refresh is dropped (no equivalent hardware
 * on Cardputer-ADV); the 880 Hz beep on danger-severity refresh is kept
 * via M5.Speaker, suppressible with M.
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>

class AppClaudeMeter : public mooncake::AppAbility {
public:
    AppClaudeMeter();
    ~AppClaudeMeter();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum Screen {
        S_Summary = 0,
        S_FiveHour,
        S_SevenDay,
        S_OpusSonnet,
        S_Stats,
        S_Token,
        S_Detail,
        S_Settings,
        S_COUNT
    };
    enum SettingsField { SF_Bearer = 0, SF_Base, SF_COUNT };

    void draw();
    void draw_summary();
    void draw_big_pct_screen(const char* title, float pct);
    void draw_opus_sonnet();
    void draw_detail();
    void draw_stats();
    void draw_token();
    void draw_settings();
    void draw_title_bar(const char* title);
    void draw_footer();
    void draw_bar(int x, int y, int w, int h, float pct, uint32_t fill);
    void next_screen(int delta);

    void on_key(int keyCode, const char* keyName);

    int  _key_slot_id     = -1;
    uint32_t _last_redraw_ms = 0;
    int  _screen          = S_Summary;

    bool _editing         = false;
    int  _edit_field      = SF_Bearer;
    std::string _edit_buffer;
};
