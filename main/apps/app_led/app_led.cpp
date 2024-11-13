/**
 * @file app_led.cpp
 * @author lahiru
 * @brief
 * @version 0.1
 * @date 2024-11-08
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "app_led.h"
#include "lgfx/v1/misc/enum.hpp"
#include "spdlog/spdlog.h"
#include <cstdint>
#include "../utils/theme/theme_define.h"
#include "neoled.h"

 
 

using namespace MOONCAKE::APPS;


#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)
#define GPIO_LED 21
#define NUM_LEDS   1 

  

void AppLed::onCreate()
{
    spdlog::info("{} onCreate", getAppName());

    // Get hal
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    
}


void AppLed::onResume()
{
    ANIM_APP_OPEN();

    _data.current_state = state_init;
      spdlog::info("onResume"); 
}


void AppLed::onRunning()
{
    
    if (_data.current_state == state_init) {
        _canvas_clear();
        _canvas->setBaseColor(THEME_COLOR_BG);
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas->setFont(FONT_REPL);
        _canvas->setTextSize(FONT_SIZE_REPL);
        _canvas->setCursor(0, 0);
        _canvas->printf("App Started");
        _canvas_update(); 
         NeoLED::init();
         _data.current_state = state_auto;

          
         

    } else if (_data.current_state == state_auto) {
         int64_t currentMillis = millis();

    // Check if 1 second has passed
        if (currentMillis - _data._last_update >= 100) {
            _data._last_update = currentMillis;   
            NeoLED::Pixel pixel = NeoLED::colorWheel(_data._hue_val);
            NeoLED::update(&pixel); 
            _data._hue_val++;

            uint32_t hexValue = ((uint32_t)pixel.red << 16) | ((uint32_t)pixel.green << 8) | (uint32_t)pixel.blue;
        
            _canvas_clear();
            _canvas->setBaseColor(THEME_COLOR_BG);
            _canvas->setTextColor(THEME_COLOR_REPL_TEXT, hexValue);
            _canvas->setFont(FONT_BASIC);
            _canvas->setTextSize(2);
            _canvas->setCursor(70, 40);
            _canvas->printf("LED set to  %d",_data._hue_val);
            _canvas_update(); 

            if(_data._hue_val > 254){
                _data._hue_val = 0;
            }
        }
    } else if (_data.current_state == state_manual) {
        NeoLED::Pixel pixel = NeoLED::colorWheel(_data._hue_val);
        NeoLED::update(&pixel); 
        

          uint32_t hexValue = ((uint32_t)pixel.red << 16) | ((uint32_t)pixel.green << 8) | (uint32_t)pixel.blue;
    
        _canvas_clear();
        _canvas->setBaseColor(THEME_COLOR_BG);
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, hexValue);
        _canvas->setFont(FONT_BASIC);
        _canvas->setTextSize(2);
        _canvas->setCursor(70, 40);
        _canvas->printf("LED set to  %d",_data._hue_val);
        _canvas_update(); 

    }

    _data.hal->keyboard()->updateKeyList();
    if (_data.last_key_num != _data.hal->keyboard()->keyList().size())
    {
        auto pressing_key = _data.hal->keyboard()->getKey();
        if (!_data.last_key_num)
        {
            if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_R)
            {
                _canvas_clear();
                _canvas->setBaseColor(THEME_COLOR_BG);
                _canvas->setTextColor(THEME_COLOR_REPL_TEXT, TFT_RED);
                _canvas->setFont(FONT_REPL);
                _canvas->setTextSize(FONT_SIZE_REPL);
                _canvas->setCursor(0, 0);
                _canvas->printf("Key R press");
                _canvas_update();
                _data._hue_val = HUE_RED;
                _data.current_state = state_manual;
            }
            else if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_G)
            {
                _canvas_clear();
                _canvas->setBaseColor(THEME_COLOR_BG);
                _canvas->setTextColor(THEME_COLOR_REPL_TEXT, TFT_GREEN);
                _canvas->setFont(FONT_REPL);
                _canvas->setTextSize(FONT_SIZE_REPL);
                _canvas->setCursor(0, 0);
                _canvas->printf("Key G press");
                _canvas_update();
                _data._hue_val = HUE_GREEN;
                _data.current_state = state_manual;
            }
            else if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_B)
            {
                _canvas_clear();
                _canvas->setBaseColor(THEME_COLOR_BG);
                _canvas->setTextColor(THEME_COLOR_REPL_TEXT, TFT_BLUE);
                _canvas->setFont(FONT_REPL);
                _canvas->setTextSize(FONT_SIZE_REPL);
                _canvas->setCursor(0, 0);
                _canvas->printf("Key B press");
                _canvas_update();
                _data._hue_val = HUE_BLUE;
                _data.current_state = state_manual;
            }
            else if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_A)
            {
                _data.current_state = state_auto;
            }
            else if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_UP)
            {
                _data.current_state = state_manual;
                if (_data._hue_val < 254)
                {
                    _data._hue_val++;
                }
                else
                {
                    _data._hue_val = 0;
                }
            }
            else if (_data.hal->keyboard()->getKeyValue(pressing_key).value_num_first == KEY_DOWN)
            {
                _data.current_state = state_manual;
                if (_data._hue_val > 0)
                {
                    _data._hue_val--;
                }
                else
                {
                    _data._hue_val = 255;
                }
            }

           
        }
         _data.last_key_num = _data.hal->keyboard()->keyList().size();
    }

    if (_data.hal->homeButton()->pressed())
    {
        _data.hal->playNextSound();
        spdlog::info("quit LED"); 
        destroyApp();
    }
}


void AppLed::onDestroy()
{
    NeoLED::destroy();
    NeoLED::Pixel green_pixel = NeoLED::makePixel(0,0,0);
    NeoLED::update(&green_pixel); 
    spdlog::info(" Disto");
}
