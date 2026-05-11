/**
 * @file app_sysinfo.h
 * @brief System info: heap, uptime, WiFi, battery, chip stats.
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/sysinfo_big.h"
#include "assets/sysinfo_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppSysinfo : public APP_BASE
{
    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        uint32_t last_redraw_ms = 0;
    };
    Data_t _data;

    void _draw();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppSysinfo_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "SysInfo"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_sysinfo_big, image_data_sysinfo_small)); }
    void* newApp() override { return new AppSysinfo; }
    void deleteApp(void* app) override { delete (AppSysinfo*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
