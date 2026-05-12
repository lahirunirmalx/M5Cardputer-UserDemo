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
#include "nvs.h"
#include "nvs_flash.h"
#include <iostream>
#include <string>
#include <cstring>


// USER - CONFIG EDIT
#define TONE_CHANNEL 6
#define TIME_ZONE "UTC-5:30"
/* WiFi credentials are NOT hardcoded any more. They live in NVS:
 *   namespace "wifi", keys "ssid" and "pass".
 * Use the Set WiFi app to edit them, or pre-flash via tools/flash_nvs.sh and
 * tools/nvs_keys.csv. The compiled defaults below are empty so an un-flashed
 * device starts with no creds and shows the Set WiFi prompt. */
#define WIFI_SSID ""
#define WIFI_PASS ""
// USER - CONFIG EDIT

/* NVS namespaces / keys used across the firmware. Keep them stable — they
 * also appear in tools/nvs_keys.csv for pre-flash provisioning. */
#define NVS_NS_WIFI    "wifi"
#define NVS_KEY_SSID   "ssid"
#define NVS_KEY_PASS   "pass"

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
              _sntp_adjusted(false),_web_redio_runing(false),_wifi_connected(false)
        {
            _wifi_ssid[0] = '\0';
            _wifi_password[0] = '\0';
        }

        /* Load WiFi creds from NVS (namespace "wifi"). Falls through silently
         * if the namespace or keys are missing, leaving the buffers as they
         * were (empty on a fresh boot). Safe to call repeatedly. */
        inline void loadWifiFromNvs() {
            nvs_handle_t h;
            if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return;
            size_t sz = sizeof(_wifi_ssid);
            if (nvs_get_str(h, NVS_KEY_SSID, _wifi_ssid, &sz) != ESP_OK) _wifi_ssid[0] = '\0';
            sz = sizeof(_wifi_password);
            if (nvs_get_str(h, NVS_KEY_PASS, _wifi_password, &sz) != ESP_OK) _wifi_password[0] = '\0';
            nvs_close(h);
        }

        /* Persist whatever is currently in `_wifi_ssid` / `_wifi_password` to NVS. */
        inline void saveWifiToNvs() {
            nvs_handle_t h;
            if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
            nvs_set_str(h, NVS_KEY_SSID, _wifi_ssid);
            nvs_set_str(h, NVS_KEY_PASS, _wifi_password);
            nvs_commit(h);
            nvs_close(h);
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
          saveWifiToNvs();
        }
        inline const char* getWifiSSID() const {return _wifi_ssid;}

        inline void setWifiPassword(const char* wifi_password) {
          strncpy(_wifi_password, wifi_password, sizeof(_wifi_password) - 1);
          _wifi_password[sizeof(_wifi_password) - 1] = '\0';
          saveWifiToNvs();
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