/**
 * @file app_tvbgone.cpp
 * @brief TV-B-Gone style universal IR remote.
 *
 * Builds raw RMT items per protocol (NEC / RC5 / Sony SIRC / Midea AC) and
 * blasts them out the on-board IR LED (GPIO 44, RMT TX ch 1 - we use ch1
 * to avoid clashing with app_ir's existing RX setup on ch 0).
 *
 * Keys:
 *   Space / Enter / F  : start firing the full list (auto-cycles)
 *   Up / Down          : pick a single entry
 *   S                  : send just the selected entry
 *   Esc                : stop firing
 *   HOME               : exit
 */
#include "app_tvbgone.h"
#include "codes.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include "driver/rmt.h"
#include "spdlog/spdlog.h"
#include <cstdio>
#include <cstring>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Use TX channel 1 + GPIO 44 (M5Cardputer's IR LED). Channel 0 is reserved
 * for app_ir's NEC builder so the two apps don't fight over the same
 * channel state. 1 tick = 1us with the default 80MHz APB / div=80. */
static constexpr rmt_channel_t TX_CH       = (rmt_channel_t)1;
static constexpr gpio_num_t    TX_GPIO     = (gpio_num_t)44;
static constexpr int           TICK_US     = 1;

/* Layout */
static constexpr int TITLE_Y      = 1;
static constexpr int CHIP_Y       = 5;
static constexpr int PANEL_Y      = 19;
static constexpr int PANEL_H      = 56;
static constexpr int STATUS_Y     = 79;
static constexpr int FOOTER_Y     = 100;

static const uint32_t COLOR_ACCENT    = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_DIM_TEXT  = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_FIRING    = (uint32_t)0xFF6464;
static const uint32_t COLOR_OK        = (uint32_t)0x99FF00;

/* ---------- raw RMT helpers ---------- */
static inline void rmt_pair(rmt_item32_t& it, uint32_t mark_us, uint32_t space_us)
{
    it.level0    = 1;
    it.duration0 = (uint16_t)(mark_us);
    it.level1    = 0;
    it.duration1 = (uint16_t)(space_us);
}

static void tx_send(rmt_item32_t* items, size_t n)
{
    rmt_write_items(TX_CH, items, (int)n, true);   /* wait until done */
}

/* NEC frame:
 *   header: 9000us mark, 4500us space
 *   bit 0 : 562 mark,  562 space
 *   bit 1 : 562 mark, 1687 space
 *   trailer: 562 mark
 *   With std NEC, address (LSB first) + ~address + command (LSB first) + ~command. */
static void send_nec(uint32_t addr, uint32_t cmd, bool extended)
{
    rmt_item32_t items[68];
    int n = 0;
    rmt_pair(items[n++], 9000, 4500);
    uint32_t payload;
    if (extended) {
        /* 16-bit address (no complement) + 8-bit cmd + ~cmd */
        payload  = (addr & 0xFFFF) |
                   (((cmd & 0xFF) << 16)) |
                   (((~cmd) & 0xFF) << 24);
    } else {
        payload  =  (addr & 0xFF) |
                   (((~addr) & 0xFF) << 8) |
                   (((cmd  & 0xFF) << 16)) |
                   (((~cmd) & 0xFF) << 24);
    }
    for (int i = 0; i < 32; i++) {
        bool b = (payload >> i) & 1;
        rmt_pair(items[n++], 562, b ? 1687 : 562);
    }
    rmt_pair(items[n++], 562, 30000);  /* trailer + inter-frame gap (RMT duration is 15-bit) */
    tx_send(items, n);
}

/* RC5 (Manchester). Carrier is 36kHz officially but 38kHz usually works.
 *  bit duration = 1778us, each bit is two half-bits:
 *    bit 1 = space then mark
 *    bit 0 = mark then space
 *  14-bit frame: S1 S2 T A4..A0 C5..C0 (we send fixed start=11 toggle=0). */
static void send_rc5(uint8_t addr, uint8_t cmd)
{
    static int rc5_toggle = 0;
    rc5_toggle ^= 1;
    /* Build 14 bits MSB-first */
    uint16_t frame = (1 << 13) | (1 << 12) |
                     ((rc5_toggle & 1) << 11) |
                     ((addr & 0x1F) << 6) |
                     (cmd & 0x3F);
    /* Emit each bit as two half-bits (889us each) */
    rmt_item32_t items[64];
    int n = 0;
    bool level = false;  /* track current pin level for merging */
    uint32_t run = 0;
    auto flush = [&]() {
        if (run == 0) return;
        if (n == 0) {
            items[n].level0 = level;
            items[n].duration0 = (uint16_t)run;
            items[n].level1 = !level;
            items[n].duration1 = 0;
            n++;
        } else {
            if (items[n-1].duration1 == 0 && items[n-1].level1 == level) {
                items[n-1].duration1 = (uint16_t)run;
            } else {
                items[n].level0 = level;
                items[n].duration0 = (uint16_t)run;
                items[n].level1 = !level;
                items[n].duration1 = 0;
                n++;
            }
        }
        run = 0;
    };
    auto half_bit = [&](bool lvl) {
        if (level == lvl) {
            run += 889;
        } else {
            flush();
            level = lvl;
            run = 889;
        }
    };
    for (int i = 13; i >= 0; i--) {
        bool b = (frame >> i) & 1;
        /* RC5 1 = space then mark; 0 = mark then space.
         * Carrier-modulated mark = level 1 in our RMT. */
        if (b) { half_bit(false); half_bit(true); }
        else   { half_bit(true);  half_bit(false); }
    }
    flush();
    /* close last item with a trailing gap */
    if (n > 0 && items[n-1].duration1 == 0) {
        items[n-1].level1 = 0;
        items[n-1].duration1 = 30000;
    } else {
        rmt_pair(items[n++], 0, 30000);
    }
    tx_send(items, n);
}

/* Sony SIRC: 40kHz carrier ideally, 38kHz mostly works.
 *   header 2400us mark, 600 space
 *   bit 0  600 mark, 600 space
 *   bit 1  1200 mark, 600 space
 *   LSB first, then trailer/inter-frame ~25-45ms. Re-send 3x for reliability. */
static void send_sirc(uint32_t data, int nbits)
{
    rmt_item32_t items[44];
    for (int rep = 0; rep < 3; rep++) {
        int n = 0;
        rmt_pair(items[n++], 2400, 600);
        for (int i = 0; i < nbits; i++) {
            bool b = (data >> i) & 1;
            rmt_pair(items[n++], b ? 1200 : 600, 600);
        }
        items[n - 1].duration1 = 30000;  /* inter-frame gap (RMT duration max 32767) */
        tx_send(items, n);
    }
}

/* Midea 48-bit AC:
 *   header 4400 mark, 4400 space
 *   bit 0  540 mark,  540 space
 *   bit 1  540 mark, 1620 space
 *   trailer 540 mark, 5200 space
 *   then 48-bit complement frame, then 540 mark trailer
 *   Single-shot is enough for "send command once". */
static void send_midea(uint32_t hi, uint32_t lo)
{
    /* 6 bytes MSB-first: hi[31:24], hi[23:16], hi[15:8], hi[7:0], lo[15:8], lo[7:0] */
    uint8_t bytes[6] = {
        (uint8_t)((hi >> 24) & 0xFF),
        (uint8_t)((hi >> 16) & 0xFF),
        (uint8_t)((hi >>  8) & 0xFF),
        (uint8_t)((hi >>  0) & 0xFF),
        (uint8_t)((lo >>  8) & 0xFF),
        (uint8_t)((lo >>  0) & 0xFF),
    };
    rmt_item32_t items[110];
    int n = 0;

    auto put_header = [&]() {
        rmt_pair(items[n++], 4400, 4400);
    };
    auto put_byte = [&](uint8_t b) {
        for (int i = 7; i >= 0; i--) {
            bool v = (b >> i) & 1;
            rmt_pair(items[n++], 540, v ? 1620 : 540);
        }
    };

    /* primary frame */
    put_header();
    for (int i = 0; i < 6; i++) put_byte(bytes[i]);
    /* separator: mark + 5.2ms space */
    rmt_pair(items[n++], 540, 5200);
    /* complement frame */
    put_header();
    for (int i = 0; i < 6; i++) put_byte((uint8_t)~bytes[i]);
    /* final trailer */
    rmt_pair(items[n++], 540, 30000);

    tx_send(items, n);
}

/* ---------- TX channel lifecycle ---------- */
void AppTvbgone::_begin_rmt()
{
    if (_data.rmt_inited) return;
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(TX_GPIO, TX_CH);
    cfg.clk_div = 80;                  /* 1us per tick */
    cfg.tx_config.carrier_en = true;
    cfg.tx_config.carrier_freq_hz = 38000;
    cfg.tx_config.carrier_duty_percent = 33;
    cfg.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
    cfg.tx_config.idle_output_en = true;
    cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    if (rmt_config(&cfg) != ESP_OK) { spdlog::error("tvbgone rmt_config failed"); return; }
    if (rmt_driver_install(TX_CH, 0, 0) != ESP_OK) {
        spdlog::error("tvbgone rmt_driver_install failed");
        return;
    }
    _data.rmt_inited = true;
}

void AppTvbgone::_end_rmt()
{
    if (!_data.rmt_inited) return;
    rmt_driver_uninstall(TX_CH);
    _data.rmt_inited = false;
}

/* ---------- send dispatcher ---------- */
void AppTvbgone::_send_current()
{
    if (_data.fire_idx < 0 || _data.fire_idx >= TVBGONE_CODE_COUNT) return;
    const tvbgone_code_t& c = TVBGONE_CODES[_data.fire_idx];
    _data.last_label = c.label;
    switch (c.proto) {
        case PROTO_NEC:     send_nec(c.data_hi, c.data_lo, false); break;
        case PROTO_NEC_EXT: send_nec(c.data_hi, c.data_lo, true);  break;
        case PROTO_RC5:     send_rc5((uint8_t)c.data_hi, (uint8_t)c.data_lo); break;
        case PROTO_SIRC12:  send_sirc(c.data_lo, 12); break;
        case PROTO_SIRC15:  send_sirc(c.data_lo, 15); break;
        case PROTO_SIRC20:  send_sirc(c.data_lo, 20); break;
        case PROTO_MIDEA:   send_midea(c.data_hi, c.data_lo); break;
    }
}

/* ---------- UI ---------- */
void AppTvbgone::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("TV-B-Gone");

    char chip[24];
    snprintf(chip, sizeof(chip), "%d/%d",
             _data.mode == M_FIRING ? _data.fire_idx + 1 : _data.cursor + 1,
             TVBGONE_CODE_COUNT);
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(_data.mode == M_FIRING ? COLOR_FIRING : COLOR_DIM_TEXT,
                          THEME_COLOR_BG);
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);

    /* Panel: list 5 entries centered on cursor (or fire_idx in firing mode) */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
    _canvas->setFont(FONT_SMALL);
    int active = (_data.mode == M_FIRING) ? _data.fire_idx : _data.cursor;
    int window = 5;
    int start = active - window / 2;
    if (start < 0) start = 0;
    if (start > TVBGONE_CODE_COUNT - window) start = TVBGONE_CODE_COUNT - window;
    if (start < 0) start = 0;
    int line_h = (PANEL_H - 6) / window;
    for (int i = 0; i < window; i++) {
        int idx = start + i;
        if (idx >= TVBGONE_CODE_COUNT) break;
        const tvbgone_code_t& c = TVBGONE_CODES[idx];
        int y = PANEL_Y + 3 + i * line_h;
        bool is_active = (idx == active);
        if (is_active) {
            uint32_t hl = (_data.mode == M_FIRING) ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60;
            _canvas->fillSmoothRoundRect(4, y - 1, cw - 8, line_h, 2, hl);
        }
        const char* tag = "";
        switch (c.proto) {
            case PROTO_NEC: case PROTO_NEC_EXT:  tag = "NEC";   break;
            case PROTO_RC5:                       tag = "RC5";   break;
            case PROTO_SIRC12: case PROTO_SIRC15: case PROTO_SIRC20: tag = "SIRC"; break;
            case PROTO_MIDEA:                     tag = "MIDEA"; break;
        }
        _canvas->setTextColor(is_active ? COLOR_ACCENT : (uint32_t)0xE6E6E6,
                              is_active ? ((_data.mode == M_FIRING) ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60)
                                        : COLOR_PANEL_BG);
        _canvas->setCursor(8, y + 1);
        _canvas->print(c.label);
        _canvas->setTextColor(COLOR_DIM_TEXT,
                              is_active ? ((_data.mode == M_FIRING) ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60)
                                        : COLOR_PANEL_BG);
        _canvas->drawRightString(tag, cw - 8, y + 1, FONT_SMALL);
    }

    /* Status line below panel */
    _canvas->setFont(FONT_SMALL);
    if (_data.mode == M_FIRING) {
        _canvas->setTextColor(COLOR_FIRING, THEME_COLOR_BG);
        _canvas->setCursor(3, STATUS_Y);
        char sbuf[40];
        snprintf(sbuf, sizeof(sbuf), "Firing: %s", _data.last_label);
        _canvas->print(sbuf);
    } else if (_data.mode == M_SINGLE) {
        _canvas->setTextColor(COLOR_OK, THEME_COLOR_BG);
        _canvas->setCursor(3, STATUS_Y);
        char sbuf[40];
        snprintf(sbuf, sizeof(sbuf), "Sent: %s", _data.last_label);
        _canvas->print(sbuf);
    } else {
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        _canvas->setCursor(3, STATUS_Y);
        _canvas->print("Ready - point at device");
    }

    /* Footer */
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    if (_data.mode == M_FIRING)
        _canvas->print("Esc stop   HOME exit");
    else
        _canvas->print("^v pick  S send  F fire all  HOME");

    _canvas_update();
}

/* ---------- lifecycle ---------- */
void AppTvbgone::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppTvbgone::onResume()
{
    ANIM_APP_OPEN();
    _data.cursor = 0;
    _data.fire_idx = 0;
    _data.mode = M_IDLE;
    _data.last_label = "";
    _data.next_send_ms = 0;
    _begin_rmt();
    _draw();
}

void AppTvbgone::onRunning()
{
    /* Firing loop - send one code, wait ~250ms, advance. */
    if (_data.mode == M_FIRING) {
        uint32_t now = (uint32_t)millis();
        if (now >= _data.next_send_ms) {
            _send_current();
            _data.next_send_ms = now + 250;
            _draw();
            _data.fire_idx++;
            if (_data.fire_idx >= TVBGONE_CODE_COUNT) {
                _data.mode = M_IDLE;
                _data.last_label = "done";
                _draw();
            }
        }
    }

    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();
            bool handled = false;
            for (int k : st.hidKey) {
                if (k == KEY_UP)   { if (_data.cursor > 0) _data.cursor--; handled = true; }
                if (k == KEY_DOWN) { if (_data.cursor < TVBGONE_CODE_COUNT - 1) _data.cursor++; handled = true; }
                if (k == KEY_S) {
                    _data.fire_idx = _data.cursor;
                    _send_current();
                    _data.mode = M_SINGLE;
                    handled = true;
                }
                if (k == KEY_F) {
                    _data.mode = M_FIRING;
                    _data.fire_idx = 0;
                    _data.next_send_ms = (uint32_t)millis();
                    handled = true;
                }
                if (k == KEY_ESC) {
                    _data.mode = M_IDLE;
                    handled = true;
                }
            }
            if (st.enter && _data.mode != M_FIRING) {
                _data.mode = M_FIRING;
                _data.fire_idx = 0;
                _data.next_send_ms = (uint32_t)millis();
                handled = true;
            }
            if (handled) _draw();
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

void AppTvbgone::onDestroy()
{
    _end_rmt();
}
