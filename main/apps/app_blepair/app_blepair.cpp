/**
 * @file app_blepair.cpp
 * @brief BLE pairing-prompt demonstrator.
 *
 * Broadcasts well-known manufacturer-specific BLE adverts that nearby
 * iPhones / Pixels / Galaxies interpret as "device available to pair".
 * One payload is broadcast at a time; the user must explicitly start
 * the broadcast with Space/Enter and a 60-second auto-stop fires no
 * matter what. The intent is to demonstrate the protocol on a phone
 * you control - do not spam strangers' devices.
 *
 * Keys: arrows pick payload, Space/Enter toggle broadcast, HOME exit.
 */
#include "app_blepair.h"
#include "payloads.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include "spdlog/spdlog.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <cstdio>
#include <string>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Layout */
static constexpr int TITLE_Y    = 1;
static constexpr int CHIP_Y     = 5;
static constexpr int PANEL_Y    = 19;
static constexpr int PANEL_H    = 56;
static constexpr int STATUS_Y   = 79;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG   = (uint32_t)0x1E1E22;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_ON_AIR     = (uint32_t)0xFF6464;
static const uint32_t COLOR_VENDOR_AP  = (uint32_t)0xC8C8C8;
static const uint32_t COLOR_VENDOR_GG  = (uint32_t)0x66CC66;
static const uint32_t COLOR_VENDOR_SS  = (uint32_t)0x6699FF;

static BLEAdvertising* g_advertising = nullptr;

static const char* vendor_tag(ble_vendor_t v) {
    switch (v) {
        case BLE_VENDOR_APPLE:   return "AAPL";
        case BLE_VENDOR_GOOGLE:  return "GOOG";
        case BLE_VENDOR_SAMSUNG: return "SAMS";
    }
    return "?";
}
static uint32_t vendor_color(ble_vendor_t v) {
    switch (v) {
        case BLE_VENDOR_APPLE:   return COLOR_VENDOR_AP;
        case BLE_VENDOR_GOOGLE:  return COLOR_VENDOR_GG;
        case BLE_VENDOR_SAMSUNG: return COLOR_VENDOR_SS;
    }
    return COLOR_DIM_TEXT;
}

void AppBlePair::_init_ble()
{
    if (_data.ble_inited) return;
    BLEDevice::init("");
    g_advertising = BLEDevice::getAdvertising();
    if (g_advertising) {
        g_advertising->setMinInterval(0xA0);   /* 100ms */
        g_advertising->setMaxInterval(0xC0);   /* 120ms */
    }
    _data.ble_inited = true;
}

void AppBlePair::_deinit_ble()
{
    if (!_data.ble_inited) return;
    if (_data.broadcasting && g_advertising) {
        g_advertising->stop();
    }
    /* Note: BLEDevice::deinit may not fully release everything in this
     * arduino-esp32 version; calling it is still the right hygiene. */
    BLEDevice::deinit(true);
    g_advertising = nullptr;
    _data.ble_inited = false;
    _data.broadcasting = false;
}

void AppBlePair::_start_broadcast()
{
    if (!_data.ble_inited) _init_ble();
    if (!g_advertising) return;
    const ble_payload_t& p = BLE_PAYLOADS[_data.cursor];
    BLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setManufacturerData(std::string((const char*)p.mfg_data, p.mfg_len));
    g_advertising->setAdvertisementData(advData);
    g_advertising->start();
    _data.broadcasting = true;
    _data.broadcast_start_ms = (uint32_t)millis();
    _data.last_label = p.label;
}

void AppBlePair::_stop_broadcast()
{
    if (g_advertising) g_advertising->stop();
    _data.broadcasting = false;
}

void AppBlePair::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("BLE Pair");

    /* Status chip top-right */
    _canvas->setFont(FONT_SMALL);
    char chip[24];
    if (_data.broadcasting) {
        uint32_t elapsed = ((uint32_t)millis() - _data.broadcast_start_ms) / 1000;
        snprintf(chip, sizeof(chip), "ON-AIR %us", (unsigned)elapsed);
        _canvas->setTextColor(COLOR_ON_AIR, THEME_COLOR_BG);
    } else {
        snprintf(chip, sizeof(chip), "%d/%d", _data.cursor + 1, BLE_PAYLOAD_COUNT);
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    }
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);

    /* List panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
    int window = 5;
    int start = _data.cursor - window / 2;
    if (start < 0) start = 0;
    if (start > BLE_PAYLOAD_COUNT - window) start = BLE_PAYLOAD_COUNT - window;
    if (start < 0) start = 0;
    int line_h = (PANEL_H - 6) / window;
    for (int i = 0; i < window; i++) {
        int idx = start + i;
        if (idx >= BLE_PAYLOAD_COUNT) break;
        const ble_payload_t& p = BLE_PAYLOADS[idx];
        int y = PANEL_Y + 3 + i * line_h;
        bool active = (idx == _data.cursor);
        uint32_t row_bg = COLOR_PANEL_BG;
        if (active) {
            row_bg = _data.broadcasting ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60;
            _canvas->fillSmoothRoundRect(4, y - 1, cw - 8, line_h, 2, row_bg);
        }
        _canvas->setTextColor(active ? COLOR_ACCENT : (uint32_t)0xE6E6E6, row_bg);
        _canvas->setCursor(8, y + 1);
        _canvas->print(p.label);
        _canvas->setTextColor(vendor_color(p.vendor), row_bg);
        _canvas->drawRightString(vendor_tag(p.vendor), cw - 8, y + 1, FONT_SMALL);
    }

    /* Status / warning */
    _canvas->setFont(FONT_SMALL);
    _canvas->setCursor(3, STATUS_Y);
    if (_data.broadcasting) {
        _canvas->setTextColor(COLOR_ON_AIR, THEME_COLOR_BG);
        char sbuf[40];
        snprintf(sbuf, sizeof(sbuf), "Broadcasting: %s", _data.last_label);
        _canvas->print(sbuf);
    } else {
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        _canvas->print("Affects nearby phones. Use responsibly.");
    }

    /* Footer */
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    if (_data.broadcasting)
        _canvas->print("SPACE stop   HOME exit");
    else
        _canvas->print("^v pick  SPACE start  HOME");

    _canvas_update();
}

void AppBlePair::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppBlePair::onResume()
{
    ANIM_APP_OPEN();
    _data.cursor = 0;
    _data.broadcasting = false;
    _data.last_label = "";
    _init_ble();
    _draw();
}

void AppBlePair::onRunning()
{
    /* Hard-stop after auto_stop_ms to prevent accidental long-running adverts */
    if (_data.broadcasting &&
        ((uint32_t)millis() - _data.broadcast_start_ms) > _data.auto_stop_ms) {
        _stop_broadcast();
        _draw();
    }

    /* Tick the on-air timer every ~500ms */
    static uint32_t s_last_tick = 0;
    uint32_t now = (uint32_t)millis();
    if (_data.broadcasting && (now - s_last_tick) > 500) {
        s_last_tick = now;
        _draw();
    }

    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();
            bool changed = false;
            for (int k : st.hidKey) {
                if (k == KEY_UP && _data.cursor > 0) {
                    if (_data.broadcasting) _stop_broadcast();
                    _data.cursor--;
                    changed = true;
                }
                if (k == KEY_DOWN && _data.cursor < BLE_PAYLOAD_COUNT - 1) {
                    if (_data.broadcasting) _stop_broadcast();
                    _data.cursor++;
                    changed = true;
                }
                if (k == KEY_ESC && _data.broadcasting) {
                    _stop_broadcast();
                    changed = true;
                }
            }
            if (st.enter || st.space) {
                if (_data.broadcasting) _stop_broadcast();
                else                    _start_broadcast();
                changed = true;
            }
            if (changed) _draw();
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppBlePair::onDestroy()
{
    _deinit_ble();
}
