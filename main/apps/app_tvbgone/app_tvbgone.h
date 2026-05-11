/**
 * @file app_tvbgone.h
 * @brief TV-B-Gone style universal IR power-off cycler. Includes Midea AC codes.
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/tvbgone_big.h"
#include "assets/tvbgone_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppTvbgone : public APP_BASE
{
    enum Mode : uint8_t { M_IDLE, M_FIRING, M_SINGLE };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        bool   rmt_inited = false;
        int    cursor = 0;          /* selected entry for SINGLE mode */
        int    fire_idx = 0;        /* current code while firing */
        Mode   mode = M_IDLE;
        uint32_t next_send_ms = 0;
        const char* last_label = "";
    };
    Data_t _data;

    void _draw();
    void _send_current();          /* one code at fire_idx */
    void _begin_rmt();
    void _end_rmt();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppTvbgone_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "TV-B-Gone"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_tvbgone_big, image_data_tvbgone_small)); }
    void* newApp() override { return new AppTvbgone; }
    void deleteApp(void* app) override { delete (AppTvbgone*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
