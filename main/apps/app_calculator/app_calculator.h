/**
 * @file app_calculator.h
 * @brief Calculator: digits, decimal, sign toggle, + - * /, =, backspace, clear.
 */
#pragma once
#include <mooncake.h>
#include <string>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/calc_big.h"
#include "assets/calc_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppCalculator : public APP_BASE
{
    struct Data_t
    {
        HAL::Hal* hal = nullptr;
        std::string display_str;
        std::string input_str;
        double stored_val = 0;
        char pending_op = 0;
        bool error = false;
        bool result_shown = false;
        size_t last_key_num = 0;
    };
    Data_t _data;

    void _draw();
    void _draw_grid();
    void _draw_7seg_char(char c, int x, int y, uint32_t on, uint32_t off);
    void _draw_7seg_str(const char* s, int right_x, int y, uint32_t on, uint32_t off);
    void _apply_op();
    void _do_equals();
    void _clear();
    void _handle_key();
    void _on_digit(char c);
    void _on_op(char op);
    void _on_sign();
    void _on_backspace();
    void _on_clear();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppCalculator_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Calc"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_calc_big, image_data_calc_small)); }
    void* newApp() override { return new AppCalculator; }
    void deleteApp(void* app) override { delete (AppCalculator*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
