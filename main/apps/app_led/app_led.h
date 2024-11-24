/**
 * @file app_scales.h
 * @author Wu23333
 * @brief
 * @version 0.1
 * @date 2024-01-09
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "driver/rmt.h"
 
#include "assets/led_big.h"
#include "assets/led_small.h"

#define FILTER_WINDOW_SIZE 256

namespace MOONCAKE
{
    namespace APPS
    {
        class AppLed : public APP_BASE
        {
            private:
                enum State_t
                {
                    state_init = 0,
                    state_auto,
                    state_manual,
                };

                struct Data_t
                {
                    HAL::Hal* hal = nullptr;
                    State_t current_state = state_init; 
                    int64_t _last_update = 0;
                    float filter_window[FILTER_WINDOW_SIZE];
                    uint16_t _filter_window_i = 0;
                    uint8_t _hue_val = 0;
                    float offset = 0;
                    int last_key_num = 0;
                };
                Data_t _data; 
            public:
                void onCreate() override;
                void onResume() override;
                void onRunning() override;
                void onDestroy() override;
        };

        class AppLed_Packer : public APP_PACKER_BASE
        {
            std::string getAppName() override { return "NEO LED"; }
            void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_led_big, image_data_led_small)); }
            void* newApp() override { return new AppLed; }
            void deleteApp(void *app) override { delete (AppLed*)app; }
        };
    }
}
