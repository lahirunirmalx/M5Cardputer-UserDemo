/**
 * @file app_claudemeter.h
 * @brief Claude Meter — polls /usage on an internal HTTP service every 5 min
 *        and shows 5h + 7d usage as big bars with countdowns.
 *
 * Polling and LED-blink/beep alert live in a static FreeRTOS task that
 * survives `destroyApp()`, so the meter keeps running in the background
 * after HOME (same pattern as app_radio's decode task).
 */
#pragma once
#include <mooncake.h>
#include <string>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/claudemeter_big.h"
#include "assets/claudemeter_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppClaudeMeter : public APP_BASE
{
    enum FetchState { Fetch_Idle, Fetch_Busy, Fetch_OK, Fetch_Err };
    enum Screen { S_Summary = 0, S_FiveHour, S_SevenDay, S_OpusSonnet,
                  S_Stats, S_Token, S_Detail, S_Settings, S_COUNT };
    enum SettingsField { SF_Bearer = 0, SF_Base, SF_COUNT };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t   last_key_num = 0;
        uint32_t last_redraw_ms = 0;
        int      screen = S_Summary;

        /* Settings-screen edit state */
        bool     editing = false;
        int      edit_field = SF_Bearer;
        std::string edit_buffer;
    };
    Data_t _data;

    void _draw();
    void _draw_summary();
    void _draw_big_pct_screen(const char* title, float pct);
    void _draw_opus_sonnet();
    void _draw_detail();
    void _draw_stats();
    void _draw_token();
    void _draw_settings();
    void _draw_title_bar(const char* title);
    void _draw_footer();
    void _draw_bar(int x, int y, int w, int h, float pct, uint32_t fill);
    void _next_screen(int delta);

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppClaudeMeter_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Claude"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_claudemeter_big, image_data_claudemeter_small)); }
    void* newApp() override { return new AppClaudeMeter; }
    void deleteApp(void* app) override { delete (AppClaudeMeter*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
