/**
 * @file app_blepair.h
 * @brief BLE proximity-pairing advert demonstrator (Apple / Google / Samsung).
 */
#pragma once
#include <mooncake.h>
#include <string>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/blepair_big.h"
#include "assets/blepair_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppBlePair : public APP_BASE
{
    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        int  cursor = 0;
        bool ble_inited = false;
        bool broadcasting = false;
        uint32_t broadcast_start_ms = 0;
        uint32_t auto_stop_ms = 60000;     /* hard safety cap */
        const char* last_label = "";
    };
    Data_t _data;

    void _draw();
    void _start_broadcast();
    void _stop_broadcast();
    void _init_ble();
    void _deinit_ble();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppBlePair_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "BLE Pair"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_blepair_big, image_data_blepair_small)); }
    void* newApp() override { return new AppBlePair; }
    void deleteApp(void* app) override { delete (AppBlePair*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
