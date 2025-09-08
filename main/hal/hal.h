/**
 * @file hal.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include "../../components/M5GFX/src/M5GFX.h"
#include "keyboard/keyboard.h"
#include "mic/Mic_Class.hpp"
#include "speaker/Speaker_Class.hpp"
#include "button/Button.h"
#include "sdcard/sdcard.h"
#include <iostream>
#include <string>


// USER - CONFIG EDIT
#define TONE_CHANNEL 6
#define TIME_ZONE "UTC-5:30"
#define WIFI_SSID "M5Stak"
#define WIFI_PASS  "abc1234"
// USER - CONFIG EDIT

namespace HAL
{
    /**
     * @brief Hal base for DI
     *
     */
    class Hal
    {
    protected:
        LGFX_Device* _display;
        LGFX_Sprite* _canvas;
        LGFX_Sprite* _canvas_system_bar;
        LGFX_Sprite* _canvas_keyboard_bar;

        KEYBOARD::Keyboard* _keyboard;
        m5::Mic_Class* _mic;
        m5::Speaker_Class* _speaker;
        Button* _homeButton;
        SDCard* _sdcard;

        bool _sntp_adjusted;
        bool _web_redio_runing;
        bool _wifi_connected;

        char _wifi_ssid[50];
        char _wifi_password[50];

    public:
        Hal()
            : _display(nullptr), _canvas(nullptr), _canvas_system_bar(nullptr), _canvas_keyboard_bar(nullptr),
              _keyboard(nullptr), _mic(nullptr), _speaker(nullptr), _homeButton(nullptr), _sdcard(nullptr),
              _sntp_adjusted(false),_web_redio_runing(false),_wifi_connected(false),_wifi_ssid(WIFI_SSID),
              _wifi_password(WIFI_PASS)
        {
        }

        // Getter
        inline LGFX_Device* display() { return _display; }
        inline LGFX_Sprite* canvas() { return _canvas; }
        inline LGFX_Sprite* canvas_system_bar() { return _canvas_system_bar; }
        inline LGFX_Sprite* canvas_keyboard_bar() { return _canvas_keyboard_bar; }
        inline KEYBOARD::Keyboard* keyboard() { return _keyboard; }
        inline m5::Mic_Class* mic() { return _mic; }
        inline m5::Speaker_Class* Speaker() { return _speaker; }
        inline Button* homeButton() { return _homeButton; }
        inline SDCard* sdcard() { return _sdcard; }

        inline void setSntpAdjusted(bool isAdjusted) { _sntp_adjusted = isAdjusted; }
        inline bool isSntpAdjusted(void) { return _sntp_adjusted; }

        inline void setWebRedioRuning(bool isWebRedioRuning) { _web_redio_runing = isWebRedioRuning; }
        inline bool isWebRedioRuning(void) { return _web_redio_runing; }

        inline void setWifiConnected(bool isWifiConnected) { _wifi_connected = isWifiConnected; }
        inline bool isWifiConnected(void) { return _wifi_connected; }

        inline void setWifiSSID(const char* wifi_ssid) {
          strncpy(_wifi_ssid, wifi_ssid, sizeof(_wifi_ssid) - 1);
         _wifi_ssid[sizeof(_wifi_ssid) - 1] = '\0';  
        }
        inline const char* getWifiSSID() const {return _wifi_ssid;}

        inline void setWifiPassword(const char* wifi_password) {
          strncpy(_wifi_password, wifi_password, sizeof(_wifi_password) - 1);
         _wifi_password[sizeof(_wifi_password) - 1] = '\0';  
        }
        inline const char* getWifiPassword() const {return _wifi_password;}

        // Canvas
        inline void canvas_system_bar_update() { _canvas_system_bar->pushSprite(_canvas_keyboard_bar->width(), 0); }
        inline void canvas_keyboard_bar_update() { _canvas_keyboard_bar->pushSprite(0, 0); }
        inline void canvas_update() { _canvas->pushSprite(_canvas_keyboard_bar->width(), _canvas_system_bar->height()); }

        // Override
        virtual std::string type() { return "null"; }
        virtual void init() {}

        virtual void playLastSound() {}
        virtual void playNextSound() {}
        virtual void playKeyboardSound() {}

        virtual uint8_t getBatLevel() { return 100; }
    };
} 