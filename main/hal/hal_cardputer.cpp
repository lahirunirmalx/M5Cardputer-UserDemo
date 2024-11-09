/**
 * @file hal_cardputer.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-22
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "hal_cardputer.h"
#include "display/hal_display.hpp"
#include <mooncake.h>
#include "../apps/utils/common_define.h"
#include "bat/adc_read.h"

using namespace HAL;

void HalCardputer::_display_init()
{
    spdlog::info("init display");

    // Display
 
    _display = new M5GFX;
    _display->init();
 

    // Canvas
    _canvas = new LGFX_Sprite(_display);
 
    _canvas->createSprite(206, 109);

    _canvas_keyboard_bar = new LGFX_Sprite(_display);
    _canvas_keyboard_bar->createSprite(_display->width() - _canvas->width(), display()->height());

    _canvas_system_bar = new LGFX_Sprite(_display);
    _canvas_system_bar->createSprite(_canvas->width(), _display->height() - _canvas->height());
}

void HalCardputer::_keyboard_init()
{
    _keyboard = new KEYBOARD::Keyboard;
    _keyboard->init();
}

void HalCardputer::_mic_init()
{
    spdlog::info("init mic");

    _mic = new m5::Mic_Class;

    // Configs
    auto cfg = _mic->config();
    cfg.pin_data_in = 46;
    cfg.pin_ws = 43;
    cfg.magnification = 4;

    cfg.task_priority = 15; 

    cfg.i2s_port = i2s_port_t::I2S_NUM_0;
    _mic->config(cfg);
}

void HalCardputer::_speaker_init()
{
    spdlog::info("init speaker");

    _speaker = new m5::Speaker_Class;

    auto cfg = _speaker->config();
    cfg.pin_data_out = 42;
    cfg.pin_bck = 41;
    cfg.pin_ws = 43;
    cfg.i2s_port = i2s_port_t::I2S_NUM_1; 
    _speaker->config(cfg);
    _speaker->begin();
}

void HalCardputer::_button_init()
{
    _homeButton = new Button(0);
    _homeButton->begin();
}

void HalCardputer::_bat_init() { adc_read_init(); }

void HalCardputer::_sdcard_init() { _sdcard = new SDCard; }

void HalCardputer::init()
{
    spdlog::info("hal init");

    _display_init();
    _keyboard_init();
    _speaker_init();
    _mic_init();
    _button_init();
    _bat_init();
    _sdcard_init();
}
 
float __cardputer_hal_bat_v = 0;
uint8_t HalCardputer::getBatLevel()
{
 
    // https://docs.m5stack.com/zh_CN/core/basic_v2.7
    __cardputer_hal_bat_v = static_cast<float>(adc_read_get_value()) * 2 / 1000;
  
    uint8_t result = 0;
    if (__cardputer_hal_bat_v >= 3.90)
        result = 100;
    else if (__cardputer_hal_bat_v >= 3.80)
        result = 75;
    else if (__cardputer_hal_bat_v >= 3.65)
        result = 50;
    else if (__cardputer_hal_bat_v >= 3.45)
        result = 25;
    else
        result = 0;
    return result;
}

void HalCardputer::MicTest(HalCardputer* hal)
{
 

    int16_t mic_buffer[256];

    while (1)
    {
        hal->mic()->record(mic_buffer, 256);
        while (hal->mic()->isRecording())
        {
            vTaskDelay(5);
        }

        for (int i = 0; i < 256; i++)
            printf("m:%d\n", mic_buffer[i]);
    }
}

#include "../apps/utils/boot_sound/boot_sound_1.h"
#include "../apps/utils/boot_sound/boot_sound_2.h"

void HalCardputer::SpeakerTest(HalCardputer* hal)
{
    spdlog::info("speaker test");

    hal->Speaker()->setVolume(32);
 

    while (1)
    {
 
        spdlog::info("boot 1");
        hal->Speaker()->playWav(boot_sound_1, sizeof(boot_sound_1));
        while (hal->Speaker()->isPlaying())
            delay(5);
        spdlog::info("boot 1");
        delay(1000);

        spdlog::info("boot 2");
        hal->Speaker()->playWav(boot_sound_2, sizeof(boot_sound_2));
        while (hal->Speaker()->isPlaying())
            delay(5);
        spdlog::info("boot 2");
        delay(1000);
    }
}

void HalCardputer::LcdBgLightTest(HalCardputer* hal)
{
    hal->display()->setTextSize(3);

    std::vector<int> color_list = {TFT_RED, TFT_GREEN, TFT_BLUE};
    for (auto i : color_list)
    {
        hal->display()->fillScreen(i);

        for (int i = 0; i < 256; i++)
        {
            hal->display()->setCursor(0, 0);
            hal->display()->printf("%d", i);
            hal->display()->setBrightness(i);
            delay(20);
        }
        delay(1000);

        for (int i = 255; i >= 0; i--)
        {
            hal->display()->setCursor(0, 0);
            hal->display()->printf("%d", i);
            hal->display()->setBrightness(i);
            delay(20);
        }
    }
}
