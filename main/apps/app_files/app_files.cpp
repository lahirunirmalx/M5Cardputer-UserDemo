/**
 * @file app_scales.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.6
 * @date 2023-09-20
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "app_files.h"
#include "lgfx/v1/misc/enum.hpp"
#include "spdlog/spdlog.h"
#include <cstdint>
#include "../utils/theme/theme_define.h" 



using namespace MOONCAKE::APPS;


#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)


void AppFilesManager::onCreate()
{
    spdlog::info("{} onCreate", getAppName());

    // Get hal
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}


void AppFilesManager::onResume()
{
    ANIM_APP_OPEN();
    _data.current_state = state_init;
   
}


void AppFilesManager::onRunning()
{
    if (_data.current_state == state_init) {
        _canvas_clear();
        _canvas->setBaseColor(THEME_COLOR_BG);
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas->setFont(FONT_REPL);
        _canvas->setTextSize(FONT_SIZE_REPL);
        _canvas->setCursor(0, 0);
        _canvas->printf("not connected");
        _canvas_update();

         
    }

    if (_data.hal->homeButton()->pressed())
    {
        _data.hal->playNextSound();
        spdlog::info("quit scales");
        destroyApp();
    }
}


void AppFilesManager::onDestroy()
{

}
