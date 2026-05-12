/**
 * @file app_tvbgone.cpp
 * @brief Universal IR power-off cycler.
 *
 * Code set:
 *   - 143 NA + 140 EU TV codes from Ken Shirriff's Arduino-TV-B-Gone
 *     (world_ir_codes.h) — each entry encodes timing pairs with 2-4 bit
 *     index compression, and the carrier frequency varies per code
 *     (~36/38/40/56kHz)
 *   - 4 Midea AC codes (48-bit Midea protocol, user-supplied)
 *
 * IR output: GPIO 44, RMT TX channel 1 (channel 0 stays reserved for app_ir).
 * RMT runs at 1us tick (clk_div 80). Carrier is reconfigured per code via
 * rmt_set_tx_carrier() so we hit the right kHz for each TV brand.
 *
 * Keys:
 *   Up/Down  : move cursor
 *   Left/Right or 1/2/3 : switch group (NA / EU / MIDEA)
 *   S        : send selected
 *   Enter/F  : fire whole current group
 *   Esc      : stop firing
 *   HOME     : exit
 */
#include "app_tvbgone.h"
#include "codes.h"
#include "world_ir_codes.h"
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

static constexpr rmt_channel_t TX_CH   = (rmt_channel_t)1;
static constexpr gpio_num_t    TX_GPIO = (gpio_num_t)44;

static constexpr int TITLE_Y    = 1;
static constexpr int CHIP_Y     = 5;
static constexpr int TABS_Y     = 17;
static constexpr int PANEL_Y    = 30;
static constexpr int PANEL_H    = 49;
static constexpr int STATUS_Y   = 82;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT    = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_DIM_TEXT  = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_FIRING    = (uint32_t)0xFF6464;
static const uint32_t COLOR_OK        = (uint32_t)0x99FF00;
static const uint32_t COLOR_TAB_BG    = (uint32_t)0x3A3A60;

/* ---------- raw RMT helpers ---------- */
static inline void emit_pair(rmt_item32_t* items, int& n,
                             uint16_t mark_us, uint32_t space_us)
{
    /* Each RMT duration field is 15-bit (max 32767us). Split long spaces
     * into a leading (mark, 32000us) item plus N follow-up "level-low only"
     * items that hold the line low for up to 32000us each. */
    uint16_t mark_clamped = (mark_us > 32000) ? 32000 : mark_us;
    uint16_t first_space  = (space_us > 32000) ? 32000 : (uint16_t)space_us;
    items[n].level0    = 1;
    items[n].duration0 = mark_clamped;
    items[n].level1    = 0;
    items[n].duration1 = first_space;
    n++;
    uint32_t left = (space_us > first_space) ? (space_us - first_space) : 0;
    while (left > 0) {
        uint16_t chunk = (left > 32000) ? 32000 : (uint16_t)left;
        uint16_t half  = chunk / 2;
        items[n].level0    = 0;
        items[n].duration0 = half;
        items[n].level1    = 0;
        items[n].duration1 = (uint16_t)(chunk - half);
        n++;
        left -= chunk;
    }
}

/* ---------- TX channel lifecycle ---------- */
void AppTvbgone::_set_carrier(uint32_t hz)
{
    if (hz == _data.cur_carrier_hz) return;
    /* high+low periods in source-clock ticks. clk_div 80 → 1 tick = 1us,
     * but the carrier counter uses the un-divided 80MHz clock so 1 tick
     * here is 12.5ns. ESP-IDF helpers express this in "RMT carrier ticks"
     * which we compute from Hz. */
    if (hz < 30000) hz = 30000;
    if (hz > 60000) hz = 60000;
    uint32_t total_ticks = 80000000UL / hz;        /* 80MHz APB / freq */
    uint32_t high_ticks  = total_ticks / 3;        /* 33% duty */
    uint32_t low_ticks   = total_ticks - high_ticks;
    /* rmt_set_tx_carrier expects 16-bit periods, total must be < 65536 */
    if (high_ticks > 65000) high_ticks = 65000;
    if (low_ticks  > 65000) low_ticks  = 65000;
    rmt_set_tx_carrier(TX_CH, true, (uint16_t)high_ticks, (uint16_t)low_ticks,
                       RMT_CARRIER_LEVEL_HIGH);
    _data.cur_carrier_hz = hz;
}

void AppTvbgone::_begin_rmt()
{
    if (_data.rmt_inited) return;
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(TX_GPIO, TX_CH);
    cfg.clk_div = 80;                  /* 1us per data tick */
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
    _data.cur_carrier_hz = 38000;
    _data.rmt_inited = true;
}

void AppTvbgone::_end_rmt()
{
    if (!_data.rmt_inited) return;
    rmt_driver_uninstall(TX_CH);
    _data.rmt_inited = false;
    _data.cur_carrier_hz = 0;
}

/* ---------- Shirriff format player ---------- */
static int _read_bits(const uint8_t* codes, int bit_offset, int bit_count)
{
    /* Big-endian within each byte (MSB-first), spanning across bytes. */
    int value = 0;
    for (int i = 0; i < bit_count; i++) {
        int b = bit_offset + i;
        int byte_idx = b >> 3;
        int bit_in_byte = 7 - (b & 7);
        value = (value << 1) | ((codes[byte_idx] >> bit_in_byte) & 1);
    }
    return value;
}

static void send_shirriff(const struct IrCode& c, AppTvbgone* self,
                          void (AppTvbgone::*set_carrier)(uint32_t))
{
    (self->*set_carrier)(c.timer_val);
    /* worst-case items: numpairs + 4 split chunks per pair */
    static rmt_item32_t items[600];
    int n = 0;
    int bit_pos = 0;
    for (int p = 0; p < c.numpairs; p++) {
        int idx = _read_bits(c.codes, bit_pos, c.bitcompression);
        bit_pos += c.bitcompression;
        uint16_t mark  = (uint16_t)(c.times[idx * 2]     * 10);
        uint32_t space = (uint32_t)(c.times[idx * 2 + 1] * 10);
        emit_pair(items, n, mark, space);
        if (n > (int)(sizeof(items)/sizeof(items[0])) - 8) break;  /* safety */
    }
    rmt_write_items(TX_CH, items, n, true);
}

/* ---------- Midea 48-bit sender ---------- */
static void send_midea(uint32_t hi, uint32_t lo, AppTvbgone* self,
                       void (AppTvbgone::*set_carrier)(uint32_t))
{
    (self->*set_carrier)(38000);
    uint8_t bytes[6] = {
        (uint8_t)((hi >> 24) & 0xFF),
        (uint8_t)((hi >> 16) & 0xFF),
        (uint8_t)((hi >>  8) & 0xFF),
        (uint8_t)((hi >>  0) & 0xFF),
        (uint8_t)((lo >>  8) & 0xFF),
        (uint8_t)((lo >>  0) & 0xFF),
    };
    rmt_item32_t items[160];
    int n = 0;
    auto header = [&]() {
        emit_pair(items, n, 4400, 4400);
    };
    auto put_byte = [&](uint8_t b) {
        for (int i = 7; i >= 0; i--) {
            bool v = (b >> i) & 1;
            emit_pair(items, n, 540, v ? 1620 : 540);
        }
    };
    header();
    for (int i = 0; i < 6; i++) put_byte(bytes[i]);
    emit_pair(items, n, 540, 5200);
    header();
    for (int i = 0; i < 6; i++) put_byte((uint8_t)~bytes[i]);
    emit_pair(items, n, 540, 30000);
    rmt_write_items(TX_CH, items, n, true);
}

/* ---------- group / dispatch ---------- */
int AppTvbgone::_group_size() const
{
    switch (_data.group) {
        case G_NA:    return num_NAcodes;
        case G_EU:    return num_EUcodes;
        case G_MIDEA: return MIDEA_CODE_COUNT;
    }
    return 0;
}

void AppTvbgone::_send_at(int idx)
{
    if (!_data.rmt_inited) return;
    switch (_data.group) {
        case G_NA: {
            if (idx < 0 || idx >= num_NAcodes) return;
            const struct IrCode* p = NApowerCodes[idx];
            snprintf(_data.last_label, sizeof(_data.last_label),
                     "NA #%d  %ukHz", idx + 1, (unsigned)(p->timer_val / 1000));
            send_shirriff(*p, this, &AppTvbgone::_set_carrier);
            break;
        }
        case G_EU: {
            if (idx < 0 || idx >= num_EUcodes) return;
            const struct IrCode* p = EUpowerCodes[idx];
            snprintf(_data.last_label, sizeof(_data.last_label),
                     "EU #%d  %ukHz", idx + 1, (unsigned)(p->timer_val / 1000));
            send_shirriff(*p, this, &AppTvbgone::_set_carrier);
            break;
        }
        case G_MIDEA: {
            if (idx < 0 || idx >= MIDEA_CODE_COUNT) return;
            const midea_code_t& m = MIDEA_CODES[idx];
            snprintf(_data.last_label, sizeof(_data.last_label), "%s", m.label);
            send_midea(m.hi, m.lo, this, &AppTvbgone::_set_carrier);
            break;
        }
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
    int gsize = _group_size();
    int active = (_data.mode == M_FIRING) ? _data.fire_idx : _data.cursor;
    snprintf(chip, sizeof(chip), "%d/%d", active + 1, gsize);
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(_data.mode == M_FIRING ? COLOR_FIRING : COLOR_DIM_TEXT,
                          THEME_COLOR_BG);
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);

    /* Group tabs */
    static const char* TAB_NAMES[] = { "NA TVs", "EU TVs", "Midea AC" };
    int tab_w = cw / 3;
    for (int i = 0; i < 3; i++) {
        int tx = i * tab_w;
        bool sel = (i == (int)_data.group);
        uint32_t bg = sel ? COLOR_TAB_BG : THEME_COLOR_BG;
        uint32_t fg = sel ? COLOR_ACCENT : COLOR_DIM_TEXT;
        _canvas->fillSmoothRoundRect(tx + 2, TABS_Y - 1, tab_w - 4, 11, 2, bg);
        _canvas->setTextColor(fg, bg);
        int twpx = _canvas->textWidth(TAB_NAMES[i]);
        _canvas->setCursor(tx + (tab_w - twpx) / 2, TABS_Y + 1);
        _canvas->print(TAB_NAMES[i]);
    }

    /* Panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
    _canvas->setFont(FONT_SMALL);
    int window = 4;
    int start = active - window / 2;
    if (start < 0) start = 0;
    if (start > gsize - window) start = gsize - window;
    if (start < 0) start = 0;
    int line_h = (PANEL_H - 6) / window;
    for (int i = 0; i < window; i++) {
        int idx = start + i;
        if (idx >= gsize) break;
        int y = PANEL_Y + 3 + i * line_h;
        bool is_active = (idx == active);
        uint32_t row_bg = COLOR_PANEL_BG;
        if (is_active) {
            row_bg = (_data.mode == M_FIRING) ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60;
            _canvas->fillSmoothRoundRect(4, y - 1, cw - 8, line_h, 2, row_bg);
        }
        char label[24];
        uint32_t freq_hz = 38000;
        switch (_data.group) {
            case G_NA: {
                const struct IrCode* p = NApowerCodes[idx];
                snprintf(label, sizeof(label), "NA #%d", idx + 1);
                freq_hz = p->timer_val;
                break;
            }
            case G_EU: {
                const struct IrCode* p = EUpowerCodes[idx];
                snprintf(label, sizeof(label), "EU #%d", idx + 1);
                freq_hz = p->timer_val;
                break;
            }
            case G_MIDEA: {
                snprintf(label, sizeof(label), "%s", MIDEA_CODES[idx].label);
                freq_hz = 38000;
                break;
            }
        }
        _canvas->setTextColor(is_active ? COLOR_ACCENT : (uint32_t)0xE6E6E6, row_bg);
        _canvas->setCursor(8, y + 1);
        _canvas->print(label);
        char freqbuf[8];
        snprintf(freqbuf, sizeof(freqbuf), "%ukHz", (unsigned)(freq_hz / 1000));
        _canvas->setTextColor(COLOR_DIM_TEXT, row_bg);
        _canvas->drawRightString(freqbuf, cw - 8, y + 1, FONT_SMALL);
    }

    /* Status */
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
        _canvas->print("^v pick  <> tab  S send  F fire");

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
    _data.group = G_NA;
    _data.cursor = 0;
    _data.fire_idx = 0;
    _data.mode = M_IDLE;
    _data.next_send_ms = 0;
    _data.last_label[0] = '\0';
    _begin_rmt();
    _draw();
}

void AppTvbgone::onRunning()
{
    if (_data.mode == M_FIRING) {
        uint32_t now = (uint32_t)millis();
        if (now >= _data.next_send_ms) {
            _send_at(_data.fire_idx);
            _data.next_send_ms = now + 150;     /* ~7 codes/sec */
            _draw();
            _data.fire_idx++;
            if (_data.fire_idx >= _group_size()) {
                _data.mode = M_IDLE;
                strncpy(_data.last_label, "done", sizeof(_data.last_label));
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
                if (k == KEY_DOWN) { if (_data.cursor < _group_size() - 1) _data.cursor++; handled = true; }
                if (k == KEY_LEFT) {
                    if (_data.group > 0) _data.group = (Group)((int)_data.group - 1);
                    _data.cursor = 0;
                    handled = true;
                }
                if (k == KEY_RIGHT) {
                    if (_data.group < G_MIDEA) _data.group = (Group)((int)_data.group + 1);
                    _data.cursor = 0;
                    handled = true;
                }
                if (k == KEY_1) { _data.group = G_NA;    _data.cursor = 0; handled = true; }
                if (k == KEY_2) { _data.group = G_EU;    _data.cursor = 0; handled = true; }
                if (k == KEY_3) { _data.group = G_MIDEA; _data.cursor = 0; handled = true; }
                if (k == KEY_S) {
                    _send_at(_data.cursor);
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
