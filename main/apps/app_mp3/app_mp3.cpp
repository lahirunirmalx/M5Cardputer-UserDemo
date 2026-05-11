/**
 * @file app_mp3.cpp
 * @brief WinAmp-style MP3 player (port of VolosR/M5Mp3).
 *
 * UI: title bar, current track name, big 7-segment elapsed-time display,
 *     progress bar, 3-line playlist, footer.
 * Keys: A play/stop, N next, P prev, ; / . list move, V volume cycle, HOME exit.
 */
#include "app_mp3.h"
#include "spdlog/spdlog.h"
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

static constexpr int preallocateBufferSize = 32 * 512;
static constexpr int preallocateCodecSize = 85332;

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y      = 1;
static constexpr int CHIP_Y       = 5;
static constexpr int TRACK_Y      = 19;
static constexpr int TIME_Y       = 29;
static constexpr int PROG_Y       = 51;
static constexpr int PROG_H       = 5;
static constexpr int LIST_Y0      = 60;
static constexpr int LIST_LINE_H  = 11;
static constexpr int LIST_LINES   = 3;
static constexpr int FOOTER_Y     = 100;

/* 7-segment digit geometry (same as calculator/resistor) */
static constexpr int SEG_DW    = 12;
static constexpr int SEG_DH    = 19;
static constexpr int SEG_ST    = 3;
static constexpr int SEG_GAP   = 2;
static constexpr int SEG_DOT_W = 3;
static constexpr int SEG_HALF  = (SEG_DH - SEG_ST) / 2;
static constexpr int SEG_VLEN  = SEG_HALF - SEG_ST;

static const uint32_t COLOR_ACCENT      = (uint32_t)0x99FF00;
static const uint32_t COLOR_DISPLAY_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_SEG_OFF     = (uint32_t)0x252528;
static const uint32_t COLOR_DIM_TEXT    = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_PROG_BG     = (uint32_t)0x303035;
static const uint32_t COLOR_STATE_STOP  = (uint32_t)0x808080;

/* ----- AudioOutputM5Speaker ----- */
bool AudioOutputM5Speaker::ConsumeSample(int16_t sample[2])
{
    if (_tri_buffer_index < tri_buf_size) {
        _tri_buffer[_tri_index][_tri_buffer_index    ] = sample[0];
        _tri_buffer[_tri_index][_tri_buffer_index + 1] = sample[1];
        _tri_buffer_index += 2;
        return true;
    }
    flush();
    return false;
}

void AudioOutputM5Speaker::flush(void)
{
    if (_tri_buffer_index) {
        _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
        _tri_buffer_index = 0;
    }
}

bool AudioOutputM5Speaker::stop(void)
{
    flush();
    _m5sound->stop(_virtual_ch);
    for (size_t i = 0; i < 3; ++i)
        memset(_tri_buffer[i], 0, tri_buf_size * sizeof(int16_t));
    return true;
}

/* ----- 7-segment helpers ----- */
void AppMp3::_draw_7seg_char(char c, int x, int y, uint32_t on, uint32_t off)
{
    static const uint8_t SEGS[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    uint8_t m;
    if (c >= '0' && c <= '9') m = SEGS[c - '0'];
    else if (c == '-')        m = 0x40;
    else                      m = 0;

    uint32_t ca = (m & 0x01) ? on : off;
    uint32_t cb = (m & 0x02) ? on : off;
    uint32_t cc = (m & 0x04) ? on : off;
    uint32_t cd = (m & 0x08) ? on : off;
    uint32_t ce = (m & 0x10) ? on : off;
    uint32_t cf = (m & 0x20) ? on : off;
    uint32_t cg = (m & 0x40) ? on : off;

    int iw = SEG_DW - 2 * SEG_ST;
    _canvas->fillRect(x + SEG_ST,          y,                     iw, SEG_ST,   ca);
    _canvas->fillRect(x,                   y + SEG_ST,            SEG_ST, SEG_VLEN, cf);
    _canvas->fillRect(x + SEG_DW - SEG_ST, y + SEG_ST,            SEG_ST, SEG_VLEN, cb);
    _canvas->fillRect(x + SEG_ST,          y + SEG_HALF,          iw, SEG_ST,   cg);
    _canvas->fillRect(x,                   y + SEG_HALF + SEG_ST, SEG_ST, SEG_VLEN, ce);
    _canvas->fillRect(x + SEG_DW - SEG_ST, y + SEG_HALF + SEG_ST, SEG_ST, SEG_VLEN, cc);
    _canvas->fillRect(x + SEG_ST,          y + SEG_DH - SEG_ST,   iw, SEG_ST,   cd);
}

int AppMp3::_seg_str_width(const char* s)
{
    int n = (int)strlen(s);
    int total = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == ':')      total += SEG_ST + SEG_GAP;
        else if (c == '.') total += SEG_DOT_W + SEG_GAP;
        else if (c == ' ') total += SEG_DW / 2 + SEG_GAP;
        else               total += SEG_DW + SEG_GAP;
    }
    if (total > 0) total -= SEG_GAP;
    return total;
}

void AppMp3::_draw_7seg_str(const char* s, int right_x, int y, uint32_t on, uint32_t off)
{
    int total_w = _seg_str_width(s);
    int x = right_x - total_w;
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == ':') {
            /* two small square dots inside top and bottom halves */
            int dot_y1 = y + SEG_ST + (SEG_VLEN - SEG_ST) / 2;
            int dot_y2 = y + SEG_HALF + SEG_ST + (SEG_VLEN - SEG_ST) / 2;
            _canvas->fillRect(x, dot_y1, SEG_ST, SEG_ST, on);
            _canvas->fillRect(x, dot_y2, SEG_ST, SEG_ST, on);
            x += SEG_ST + SEG_GAP;
        } else if (c == '.') {
            _canvas->fillRect(x, y + SEG_DH - SEG_ST, SEG_DOT_W, SEG_ST, on);
            x += SEG_DOT_W + SEG_GAP;
        } else if (c == ' ') {
            x += SEG_DW / 2 + SEG_GAP;
        } else {
            _draw_7seg_char(c, x, y, on, off);
            x += SEG_DW + SEG_GAP;
        }
    }
}

/* ----- AppMp3 ----- */
void AppMp3::_list_mp3()
{
    _data.files.clear();
    char* root = _data.hal->sdcard()->get_filepath("");
    if (!root) return;
    DIR* d = opendir(root);
    free(root);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len < 4) continue;
        if (strcasecmp(ent->d_name + len - 4, ".mp3") != 0) continue;
        char* full = _data.hal->sdcard()->get_filepath(ent->d_name);
        if (full) {
            _data.files.push_back(full);
            free(full);
        }
    }
    closedir(d);
    if (_data.current_index >= _data.files.size())
        _data.current_index = _data.files.empty() ? 0 : _data.files.size() - 1;
}

void AppMp3::_stop()
{
    if (_data.decoder)  _data.decoder->stop();
    if (_data.buff)     _data.buff->close();
    if (_data.file_src) _data.file_src->close();
    if (_data.output)   _data.output->stop();
    _data.playing = false;
    _data.stopped = true;
}

void AppMp3::_play_current()
{
    if (_data.files.empty()) return;
    _stop();
    if (_data.current_index >= _data.files.size()) _data.current_index = 0;
    const std::string& path = _data.files[_data.current_index];

    if (!_data.preallocate_buffer) _data.preallocate_buffer = malloc(preallocateBufferSize);
    if (!_data.preallocate_codec)  _data.preallocate_codec  = malloc(preallocateCodecSize);
    if (!_data.preallocate_buffer || !_data.preallocate_codec) return;

    delete _data.file_src;
    _data.file_src = new AudioFileSourcePath(path.c_str());
    if (!_data.file_src->open(path.c_str())) {
        spdlog::error("mp3 open failed: {}", path);
        return;
    }
    delete _data.buff;
    _data.buff = new AudioFileSourceBuffer(_data.file_src, _data.preallocate_buffer, preallocateBufferSize);
    delete _data.decoder;
    _data.decoder = new AudioGeneratorMP3(_data.preallocate_codec, preallocateCodecSize);
    if (!_data.output)
        _data.output = new AudioOutputM5Speaker(_data.hal->Speaker(), 0);
    _data.output->SetGain(_data.volume / 10.0f);
    if (!_data.decoder->begin(_data.buff, _data.output)) {
        spdlog::error("mp3 decoder begin failed");
        return;
    }
    _data.playing = true;
    _data.stopped = false;
    _data.next_pending = false;
    _data.play_start_ms = (uint32_t)millis();
}

void AppMp3::_draw()
{
    _canvas_clear();
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    int cw = _canvas->width();

    /* Title */
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("WinAmp");

    /* State + volume chip on top-right */
    char chip[16];
    const char* state_str = _data.playing ? "PLAY" : "STOP";
    uint32_t state_color = _data.playing ? COLOR_ACCENT : COLOR_STATE_STOP;
    snprintf(chip, sizeof(chip), "V%d", _data.volume);
    _canvas->setFont(FONT_SMALL);
    int vol_w = _canvas->textWidth(chip);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(cw - 4 - vol_w, CHIP_Y);
    _canvas->print(chip);
    int state_w = _canvas->textWidth(state_str);
    _canvas->setTextColor(state_color, THEME_COLOR_BG);
    _canvas->setCursor(cw - 4 - vol_w - 6 - state_w, CHIP_Y);
    _canvas->print(state_str);

    /* Current track name (truncated to fit) */
    std::string name;
    if (!_data.files.empty() && _data.current_index < _data.files.size()) {
        const std::string& path = _data.files[_data.current_index];
        size_t slash = path.rfind('/');
        name = slash != std::string::npos ? path.substr(slash + 1) : path;
        /* strip .mp3 extension for cleanliness */
        if (name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".mp3") == 0)
            name.resize(name.size() - 4);
    } else {
        name = "<no MP3 on SD>";
    }
    if (name.size() > 32) name = name.substr(0, 29) + "...";
    _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, TRACK_Y);
    _canvas->print(name.c_str());

    /* 7-segment elapsed time (centered) */
    uint32_t now = (uint32_t)millis();
    uint32_t elapsed_ms = _data.playing ? (now - _data.play_start_ms) : 0;
    uint32_t elapsed_s  = elapsed_ms / 1000;
    int mn = (int)(elapsed_s / 60);
    int sc = (int)(elapsed_s % 60);
    if (mn > 99) { mn = 99; sc = 59; }
    char tbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mn, sc);
    int tw = _seg_str_width(tbuf);
    int t_right = (cw + tw) / 2;
    uint32_t t_on = _data.playing ? COLOR_ACCENT : COLOR_DIM_TEXT;
    _draw_7seg_str(tbuf, t_right, TIME_Y, t_on, COLOR_SEG_OFF);

    /* Progress bar */
    float progress = 0.0f;
    if (_data.file_src) {
        uint32_t sz = _data.file_src->getSize();
        if (sz > 0) progress = (float)_data.file_src->getPos() / (float)sz;
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
    }
    int prog_w = cw - 8;
    _canvas->fillSmoothRoundRect(4, PROG_Y, prog_w, PROG_H, 2, COLOR_PROG_BG);
    int fill_w = (int)((prog_w - 2) * progress);
    if (fill_w > 0)
        _canvas->fillSmoothRoundRect(5, PROG_Y + 1, fill_w, PROG_H - 2, 1, COLOR_ACCENT);

    /* Playlist (3 lines centered on current_index) */
    _canvas->setFont(FONT_SMALL);
    int n = (int)_data.files.size();
    if (n > 0) {
        if (_data.scroll_offset > (int)_data.current_index)
            _data.scroll_offset = (int)_data.current_index;
        if (_data.scroll_offset + LIST_LINES <= (int)_data.current_index)
            _data.scroll_offset = (int)_data.current_index - LIST_LINES + 1;
        if (_data.scroll_offset < 0) _data.scroll_offset = 0;

        for (int i = 0; i < LIST_LINES; i++) {
            int idx = _data.scroll_offset + i;
            if (idx >= n) break;
            const std::string& path = _data.files[idx];
            size_t slash = path.rfind('/');
            std::string entry = slash != std::string::npos ? path.substr(slash + 1) : path;
            if (entry.size() > 4 && strcasecmp(entry.c_str() + entry.size() - 4, ".mp3") == 0)
                entry.resize(entry.size() - 4);
            if (entry.size() > 30) entry = entry.substr(0, 27) + "...";

            int y = LIST_Y0 + i * LIST_LINE_H;
            if (idx == (int)_data.current_index) {
                _canvas->fillSmoothRoundRect(2, y - 1, cw - 4, LIST_LINE_H, 2, THEME_COLOR_KB_BAR_ICON_BG);
                _canvas->setTextColor(_data.playing ? COLOR_ACCENT : (uint32_t)THEME_COLOR_REPL_TEXT,
                                      THEME_COLOR_KB_BAR_ICON_BG);
                _canvas->setCursor(5, y + 1);
                _canvas->print(_data.playing ? ">" : "=");
                _canvas->setCursor(15, y + 1);
                _canvas->print(entry.c_str());
            } else {
                _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
                _canvas->setCursor(15, y + 1);
                _canvas->print(entry.c_str());
            }
        }
    }

    /* Footer */
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("A play  N/P trk  V vol  HOME");

    _canvas_update();
}

void AppMp3::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppMp3::onResume()
{
    ANIM_APP_OPEN();
    _canvas_clear();
    _list_mp3();
    _data.scroll_offset = 0;
    if (_data.current_index >= (size_t)LIST_LINES)
        _data.scroll_offset = (int)_data.current_index - LIST_LINES + 1;
    _draw();
}

void AppMp3::onRunning()
{
    /* Advance to next on EOF */
    if (_data.decoder && _data.playing && !_data.decoder->isRunning())
        _data.next_pending = true;
    if (_data.next_pending) {
        _data.next_pending = false;
        if (!_data.files.empty()) {
            _data.current_index = (_data.current_index + 1) % _data.files.size();
            _play_current();
            _draw();
        }
    }

    /* Decoder loop */
    if (_data.decoder && _data.decoder->isRunning()) {
        if (!_data.decoder->loop())
            _data.decoder->stop();
    }

    /* Periodic redraw while playing so the 7-seg time and progress bar tick */
    uint32_t now_ms = (uint32_t)millis();
    if (_data.playing && (now_ms - _data.last_redraw_ms) >= 500) {
        _data.last_redraw_ms = now_ms;
        _draw();
    }

    /* Keys */
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& hid = _keyboard->keysState().hidKey;
            bool key_a = false, key_n = false, key_p = false, key_v = false;
            bool key_semicolon = false, key_dot = false;
            for (int k : hid) {
                if (k == KEY_A) key_a = true;
                else if (k == KEY_N) key_n = true;
                else if (k == KEY_P) key_p = true;
                else if (k == KEY_V) key_v = true;
                else if (k == KEY_SEMICOLON) key_semicolon = true;
                else if (k == KEY_DOT) key_dot = true;
            }
            if (_keyboard->keysState().enter) {
                _play_current();
                _draw();
            } else if (key_a) {
                if (_data.playing) { _stop(); _data.playing = false; }
                else               { _play_current(); }
                _draw();
            } else if (key_n) {
                if (!_data.files.empty()) {
                    _data.current_index = (_data.current_index + 1) % _data.files.size();
                    _play_current();
                    _draw();
                }
            } else if (key_p) {
                if (!_data.files.empty()) {
                    _data.current_index = _data.current_index == 0
                        ? _data.files.size() - 1 : _data.current_index - 1;
                    _play_current();
                    _draw();
                }
            } else if (key_v) {
                _data.volume = (_data.volume + 1) % 11;
                if (_data.output) _data.output->SetGain(_data.volume / 10.0f);
                _draw();
            } else if (key_semicolon) {
                if (_data.current_index > 0) {
                    _data.current_index--;
                    _draw();
                }
            } else if (key_dot) {
                if (_data.current_index + 1 < _data.files.size()) {
                    _data.current_index++;
                    _draw();
                }
            }
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

void AppMp3::onDestroy()
{
    _stop();
    if (_data.output)             { delete _data.output;   _data.output   = nullptr; }
    if (_data.decoder)            { delete _data.decoder;  _data.decoder  = nullptr; }
    if (_data.buff)               { delete _data.buff;     _data.buff     = nullptr; }
    if (_data.file_src)           { delete _data.file_src; _data.file_src = nullptr; }
    if (_data.preallocate_buffer) { free(_data.preallocate_buffer); _data.preallocate_buffer = nullptr; }
    if (_data.preallocate_codec)  { free(_data.preallocate_codec);  _data.preallocate_codec  = nullptr; }
}
