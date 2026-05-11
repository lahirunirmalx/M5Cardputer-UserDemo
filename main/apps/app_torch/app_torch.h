/**
 * @file app_torch.h
 * @brief Flashlight: white full-screen with adjustable brightness.
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/torch_big.h"
#include "assets/torch_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppTorch : public APP_BASE
{
    struct Data_t {
        HAL::Hal* hal = nullptr;
        uint8_t brightness = 255;     /* 0..255 */
        uint32_t color_idx = 0;       /* 0=white, 1=red, 2=green, 3=blue */
        bool on = true;
        size_t last_key_num = 0;
        uint8_t prev_brightness = 100;  /* to restore on exit */
    };
    Data_t _data;

    void _draw();
    void _apply_brightness();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppTorch_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Torch"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_torch_big, image_data_torch_small)); }
    void* newApp() override { return new AppTorch; }
    void deleteApp(void* app) override { delete (AppTorch*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
