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
/* WiFi credentials are NOT hardcoded any more. They live in NVS, in up to
 * five SSID/password slots. Use the Set WiFi app to add credentials, or
 * pre-flash via tools/flash_nvs.sh and tools/nvs_keys.csv. The compiled
 * defaults below are empty so an un-flashed device starts with no creds
 * and shows the Set WiFi prompt. */
#define WIFI_SSID ""
#define WIFI_PASS ""
// USER - CONFIG EDIT

/* NVS namespaces / keys used across the firmware. Keep them stable — they
 * also appear in tools/nvs_keys.csv for pre-flash provisioning.
 *
 * Schema in namespace "wifi":
 *   count     u8     number of valid slots (1..WIFI_SLOT_MAX)
 *   ssid0     str    most-recently-saved SSID (tried first on boot)
 *   pass0     str
 *   ssid1     str    next-most-recently-saved SSID
 *   pass1     str
 *   ...      (up to ssidN/passN where N = WIFI_SLOT_MAX-1)
 *
 * Legacy: if `count` is absent on boot, the loader falls back to reading
 * the original single-slot keys `ssid` / `pass` into slot 0. The next save
 * writes the new multi-slot schema and the legacy keys become stale but
 * inert (NVS doesn't auto-erase, but nothing reads them).
 */
#define NVS_NS_WIFI         "wifi"
#define NVS_KEY_WIFI_COUNT  "count"
#define NVS_KEY_SSID        "ssid"   /* legacy single-slot fallback */
#define NVS_KEY_PASS        "pass"   /* legacy single-slot fallback */
#define WIFI_SLOT_MAX       5

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

        struct WifiCredSlot {
            char ssid[50];
            char password[50];
        };
        WifiCredSlot _wifi_slots[WIFI_SLOT_MAX];
        uint8_t      _wifi_slot_count;

    public:
        Hal()
            : _display(nullptr), _canvas(nullptr), _canvas_system_bar(nullptr), _canvas_keyboard_bar(nullptr),
              _keyboard(nullptr), _mic(nullptr), _speaker(nullptr), _homeButton(nullptr), _sdcard(nullptr),
              _sntp_adjusted(false),_web_redio_runing(false),_wifi_connected(false),
              _wifi_slot_count(0)
        {
            for (uint8_t i = 0; i < WIFI_SLOT_MAX; i++) {
                _wifi_slots[i].ssid[0] = '\0';
                _wifi_slots[i].password[0] = '\0';
            }
        }

        /* Load WiFi creds from NVS (namespace "wifi"). Reads the multi-slot
         * schema (count + ssid0/pass0 .. ssid4/pass4); falls back to the
         * legacy single-slot keys "ssid"/"pass" if `count` is missing, so
         * pre-existing NVS images from the old single-credential firmware
         * keep working. Safe to call repeatedly. */
        inline void loadWifiFromNvs() {
            nvs_handle_t h;
            if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return;

            uint8_t count = 0;
            if (nvs_get_u8(h, NVS_KEY_WIFI_COUNT, &count) == ESP_OK &&
                count > 0 && count <= WIFI_SLOT_MAX)
            {
                _wifi_slot_count = count;
                char key[8];
                for (uint8_t i = 0; i < count; i++) {
                    snprintf(key, sizeof(key), "ssid%u", i);
                    size_t sz = sizeof(_wifi_slots[i].ssid);
                    if (nvs_get_str(h, key, _wifi_slots[i].ssid, &sz) != ESP_OK)
                        _wifi_slots[i].ssid[0] = '\0';
                    snprintf(key, sizeof(key), "pass%u", i);
                    sz = sizeof(_wifi_slots[i].password);
                    if (nvs_get_str(h, key, _wifi_slots[i].password, &sz) != ESP_OK)
                        _wifi_slots[i].password[0] = '\0';
                }
            } else {
                /* Legacy fallback: read the original single-slot keys. */
                size_t sz = sizeof(_wifi_slots[0].ssid);
                if (nvs_get_str(h, NVS_KEY_SSID, _wifi_slots[0].ssid, &sz) == ESP_OK &&
                    _wifi_slots[0].ssid[0] != '\0')
                {
                    sz = sizeof(_wifi_slots[0].password);
                    if (nvs_get_str(h, NVS_KEY_PASS, _wifi_slots[0].password, &sz) != ESP_OK)
                        _wifi_slots[0].password[0] = '\0';
                    _wifi_slot_count = 1;
                }
            }
            nvs_close(h);
        }

        /* Persist `_wifi_slots[0.._wifi_slot_count-1]` to NVS under the
         * multi-slot schema. */
        inline void saveWifiToNvs() {
            nvs_handle_t h;
            if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
            nvs_set_u8(h, NVS_KEY_WIFI_COUNT, _wifi_slot_count);
            char key[8];
            for (uint8_t i = 0; i < _wifi_slot_count; i++) {
                snprintf(key, sizeof(key), "ssid%u", i);
                nvs_set_str(h, key, _wifi_slots[i].ssid);
                snprintf(key, sizeof(key), "pass%u", i);
                nvs_set_str(h, key, _wifi_slots[i].password);
            }
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

        /* Multi-slot accessors. Slot 0 is the most-recently-saved (or most-
         * recently-connected after promoteWifiSlot) — try it first on boot.
         * Out-of-range slots return an empty string. */
        inline uint8_t getWifiSlotCount() const { return _wifi_slot_count; }

        inline const char* getWifiSSID(int slot) const {
            if (slot < 0 || (uint8_t)slot >= _wifi_slot_count) return "";
            return _wifi_slots[slot].ssid;
        }
        inline const char* getWifiPassword(int slot) const {
            if (slot < 0 || (uint8_t)slot >= _wifi_slot_count) return "";
            return _wifi_slots[slot].password;
        }

        /* Backward-compat no-arg getters / setters: operate on slot 0.
         * The legacy "save SSID then save password" flow in app_set_wifi
         * (and app_radio) writes slot 0 in place via these without rotation.
         * For the proper LRU push, callers should use addWifiCredentials()
         * once both fields are known. */
        inline const char* getWifiSSID() const {
            return _wifi_slot_count > 0 ? _wifi_slots[0].ssid : "";
        }
        inline const char* getWifiPassword() const {
            return _wifi_slot_count > 0 ? _wifi_slots[0].password : "";
        }

        inline void setWifiSSID(const char* wifi_ssid) {
            if (!wifi_ssid) return;
            if (_wifi_slot_count == 0) _wifi_slot_count = 1;
            strncpy(_wifi_slots[0].ssid, wifi_ssid, sizeof(_wifi_slots[0].ssid) - 1);
            _wifi_slots[0].ssid[sizeof(_wifi_slots[0].ssid) - 1] = '\0';
            saveWifiToNvs();
        }
        inline void setWifiPassword(const char* wifi_password) {
            if (!wifi_password) return;
            if (_wifi_slot_count == 0) _wifi_slot_count = 1;
            strncpy(_wifi_slots[0].password, wifi_password, sizeof(_wifi_slots[0].password) - 1);
            _wifi_slots[0].password[sizeof(_wifi_slots[0].password) - 1] = '\0';
            saveWifiToNvs();
        }

        /* Add new credentials with LRU rotation.
         *  - Same SSID already at slot 0: just refresh the password in place.
         *  - Same SSID at slot N>0: pull it to slot 0, shift [0..N-1] down by one.
         *  - New SSID, count < WIFI_SLOT_MAX: shift [0..count-1] down, insert at 0,
         *    increment count.
         *  - New SSID, count == WIFI_SLOT_MAX: shift [0..MAX-2] down, drop the
         *    oldest (former slot MAX-1), insert at slot 0. count stays at MAX.
         * Empty ssid is a no-op. */
        inline void addWifiCredentials(const char* ssid, const char* password) {
            if (!ssid || !ssid[0]) return;
            const char* pass = password ? password : "";

            /* Same SSID at slot 0 → just refresh password. */
            if (_wifi_slot_count > 0 && strcmp(_wifi_slots[0].ssid, ssid) == 0) {
                strncpy(_wifi_slots[0].password, pass, sizeof(_wifi_slots[0].password) - 1);
                _wifi_slots[0].password[sizeof(_wifi_slots[0].password) - 1] = '\0';
                saveWifiToNvs();
                return;
            }

            /* Same SSID at slot N>0: bring it forward. Otherwise pick the
             * insertion point: either the first empty slot, or slot MAX-1
             * (evicting the oldest). */
            int found_at = -1;
            for (uint8_t i = 1; i < _wifi_slot_count; i++) {
                if (strcmp(_wifi_slots[i].ssid, ssid) == 0) { found_at = i; break; }
            }
            int shift_from = (found_at >= 0) ? found_at
                                             : (_wifi_slot_count < WIFI_SLOT_MAX
                                                ? (int)_wifi_slot_count
                                                : (int)WIFI_SLOT_MAX - 1);

            for (int i = shift_from; i > 0; i--) {
                _wifi_slots[i] = _wifi_slots[i - 1];
            }

            strncpy(_wifi_slots[0].ssid, ssid, sizeof(_wifi_slots[0].ssid) - 1);
            _wifi_slots[0].ssid[sizeof(_wifi_slots[0].ssid) - 1] = '\0';
            strncpy(_wifi_slots[0].password, pass, sizeof(_wifi_slots[0].password) - 1);
            _wifi_slots[0].password[sizeof(_wifi_slots[0].password) - 1] = '\0';

            if (found_at < 0 && _wifi_slot_count < WIFI_SLOT_MAX) {
                _wifi_slot_count++;
            }
            saveWifiToNvs();
        }

        /* Move slot N to position 0 (everything before it shifts down by one).
         * Used after a successful connect to remember which slot worked, so
         * the next boot tries it first. No-op for slot 0 or out-of-range. */
        inline void promoteWifiSlot(int slot) {
            if (slot <= 0 || (uint8_t)slot >= _wifi_slot_count) return;
            WifiCredSlot tmp = _wifi_slots[slot];
            for (int i = slot; i > 0; i--) {
                _wifi_slots[i] = _wifi_slots[i - 1];
            }
            _wifi_slots[0] = tmp;
            saveWifiToNvs();
        }

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