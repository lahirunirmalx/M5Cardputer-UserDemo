/**
 * @file app_resistor.h
 * @brief 4-band resistor calculator: pick colors, show value and tolerance.
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/resistor_big.h"
#include "assets/resistor_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppResistor : public APP_BASE
{
    struct Data_t
    {
        HAL::Hal* hal = nullptr;
        uint8_t band[4] = { 1, 0, 2, 2 };  /* digit1, digit2, multiplier, tolerance; default 10 Ω ±5% */
        uint8_t selected_band = 0;          /* 0..3 */
        size_t last_key_num = 0;
    };
    Data_t _data;

    void _draw();
    double _compute_ohm() const;
    static double _multiplier(uint8_t m);

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppResistor_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Resistor"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_resistor_big, image_data_resistor_small)); }
    void* newApp() override { return new AppResistor; }
    void deleteApp(void* app) override { delete (AppResistor*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
