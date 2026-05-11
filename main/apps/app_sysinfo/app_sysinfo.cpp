/**
 * @file app_sysinfo.cpp
 * @brief System info dashboard: heap, uptime, WiFi, battery, chip.
 */
#include "app_sysinfo.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <cstdio>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

static constexpr int TITLE_Y    = 1;
static constexpr int PANEL_Y    = 19;
static constexpr int PANEL_H    = 78;
static constexpr int ROW_H      = 10;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT    = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_LABEL     = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_VALUE     = (uint32_t)0xE6E6E6;
static const uint32_t COLOR_OK        = (uint32_t)0x99FF00;
static const uint32_t COLOR_WARN      = (uint32_t)0xFFB060;

void AppSysinfo::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("System Info");

    /* Chip name in top-right */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->drawRightString("ESP32-S3", cw - 4, 5, FONT_SMALL);

    /* Panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    /* Rows */
    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = PANEL_Y + 4 + idx * ROW_H;
        _canvas->setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        _canvas->setCursor(6, y);
        _canvas->print(label);
        _canvas->setTextColor(value_color, COLOR_PANEL_BG);
        _canvas->setCursor(72, y);
        _canvas->print(value);
    };

    /* Uptime */
    uint32_t up_s = (uint32_t)(millis() / 1000);
    int h = up_s / 3600;
    int m = (up_s / 60) % 60;
    int s = up_s % 60;
    char buf[40];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    row(0, "Uptime:", buf, COLOR_VALUE);

    /* Free heap (kB) */
    uint32_t free_h = esp_get_free_heap_size();
    uint32_t min_h  = esp_get_minimum_free_heap_size();
    snprintf(buf, sizeof(buf), "%u K (min %u)", (unsigned)(free_h / 1024), (unsigned)(min_h / 1024));
    row(1, "Free heap:", buf, (free_h < 30 * 1024) ? COLOR_WARN : COLOR_VALUE);

    /* WiFi */
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    if (wifi_ok) {
        String ssid = WiFi.SSID();
        snprintf(buf, sizeof(buf), "%.16s", ssid.c_str());
        row(2, "WiFi:", buf, COLOR_OK);
        IPAddress ip = WiFi.localIP();
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
        row(3, "IP:", buf, COLOR_VALUE);
        snprintf(buf, sizeof(buf), "%ld dBm", (long)WiFi.RSSI());
        row(4, "RSSI:", buf, COLOR_VALUE);
    } else {
        row(2, "WiFi:", "disconnected", COLOR_WARN);
        row(3, "IP:", "-", COLOR_LABEL);
        row(4, "RSSI:", "-", COLOR_LABEL);
    }

    /* Battery */
    uint8_t bat = _data.hal->getBatLevel();
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)bat);
    row(5, "Battery:", buf, (bat < 20) ? COLOR_WARN : COLOR_OK);

    /* CPU info */
    esp_chip_info_t info;
    esp_chip_info(&info);
    snprintf(buf, sizeof(buf), "%d core @ %u MHz", info.cores, (unsigned)(getCpuFrequencyMhz()));
    row(6, "CPU:", buf, COLOR_VALUE);

    /* Footer */
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Live refresh every 1s  HOME exit");

    _canvas_update();
}

void AppSysinfo::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppSysinfo::onResume()
{
    ANIM_APP_OPEN();
    _data.last_redraw_ms = 0;
    _draw();
}

void AppSysinfo::onRunning()
{
    uint32_t now = (uint32_t)millis();
    if ((now - _data.last_redraw_ms) >= 1000) {
        _data.last_redraw_ms = now;
        _draw();
    }

    if (_keyboard->keyList().size() != _data.last_key_num) {
        _data.last_key_num = _keyboard->keyList().size();
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppSysinfo::onDestroy() {}
