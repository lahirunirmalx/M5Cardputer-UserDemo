/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_files.h"
#include "assets/files_big.h"
#include "assets/files_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace mooncake;

#define MOUNT_POINT "/sdcard"

static constexpr int TITLE_Y     = 1;
static constexpr int CHIP_Y      = 5;
static constexpr int PANEL_Y     = 19;
static constexpr int PANEL_H     = 78;
static constexpr int LIST_PAD_X  = 5;
static constexpr int LIST_PAD_Y  = 4;
static constexpr int LIST_LINE_H = 10;
static constexpr int LIST_LINES  = 7;
static constexpr int LIST_Y0     = PANEL_Y + LIST_PAD_Y;
static constexpr int FOOTER_Y    = 100;

static const uint32_t COLOR_ACCENT    = 0x99FF00;
static const uint32_t COLOR_PANEL_BG  = 0x1E1E22;
static const uint32_t COLOR_SEL_BG    = 0x3A3A60;
static const uint32_t COLOR_DIR       = 0x94B2;
static const uint32_t COLOR_DIR_DIM   = 0x6275;
static const uint32_t COLOR_FILE_TEXT = 0xE6E6E6;
static const uint32_t COLOR_DIM_TEXT  = 0x9A9A9A;
static const uint32_t COLOR_WARN      = 0xFFB060;
static const uint32_t COLOR_ERR       = 0xFF6464;

AppFilesManager::AppFilesManager()
{
    setAppInfo().name     = "Files";
    setAppInfo().userData = new AppIcon_t(image_data_filemanager_big, image_data_filemanager_small);
}

AppFilesManager::~AppFilesManager()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

std::string AppFilesManager::path_full(const std::string& rel) const
{
    if (rel.empty()) {
        return MOUNT_POINT;
    }
    return std::string(MOUNT_POINT "/") + rel;
}

std::string AppFilesManager::path_join(const std::string& rel, const std::string& name) const
{
    std::string r = rel.empty() ? name : (rel + "/" + name);
    return path_full(r);
}

void AppFilesManager::refresh_list()
{
    _entries.clear();
    std::string full = path_full(_current_path);
    DIR* d           = opendir(full.c_str());
    if (!d) {
        return;
    }

    struct dirent* ent;
    std::vector<FileEntry> dirs, files;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        std::string item_path = path_join(_current_path, ent->d_name);
        struct stat st;
        if (stat(item_path.c_str(), &st) != 0) {
            continue;
        }
        FileEntry e;
        e.name   = ent->d_name;
        e.is_dir = S_ISDIR(st.st_mode);
        e.size   = static_cast<size_t>(st.st_size);
        e.mtime  = st.st_mtime;
        if (e.is_dir) {
            dirs.push_back(e);
        } else {
            files.push_back(e);
        }
    }
    closedir(d);

    for (const auto& e : dirs) _entries.push_back(e);
    for (const auto& e : files) _entries.push_back(e);

    if (_selected_index >= static_cast<int>(_entries.size())) {
        _selected_index = _entries.empty() ? 0 : static_cast<int>(_entries.size()) - 1;
    }
    if (_selected_index < 0) {
        _selected_index = 0;
    }
    _scroll_offset = 0;
    if (_selected_index >= LIST_LINES) {
        _scroll_offset = _selected_index - LIST_LINES + 1;
    }
}

void AppFilesManager::format_size(const FileEntry& e, char* out, size_t out_size)
{
    if (e.is_dir) {
        snprintf(out, out_size, "<DIR>");
        return;
    }
    if (e.size < 1024) {
        snprintf(out, out_size, "%uB", (unsigned)e.size);
    } else if (e.size < 1024UL * 1024UL) {
        snprintf(out, out_size, "%.1fK", (double)e.size / 1024.0);
    } else if (e.size < 1024UL * 1024UL * 1024UL) {
        snprintf(out, out_size, "%.1fM", (double)e.size / (1024.0 * 1024.0));
    } else {
        snprintf(out, out_size, "%.1fG", (double)e.size / (1024.0 * 1024.0 * 1024.0));
    }
}

void AppFilesManager::draw_title(const char* title)
{
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, TITLE_Y);
    GetHAL().canvas.print(title);
}

void AppFilesManager::draw_list()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    draw_title("Files");

    std::string path_display = _current_path.empty() ? "/" : ("/" + _current_path);
    if (path_display.size() > 22) {
        path_display = "..." + path_display.substr(path_display.size() - 19);
    }
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(path_display.c_str(), cw - 4, CHIP_Y, FONT_SMALL);

    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    if (_entries.empty()) {
        GetHAL().canvas.setFont(FONT_SMALL);
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        const char* msg = "(empty)";
        int tw          = GetHAL().canvas.textWidth(msg);
        GetHAL().canvas.setCursor((cw - tw) / 2, PANEL_Y + PANEL_H / 2 - 4);
        GetHAL().canvas.print(msg);
    } else {
        GetHAL().canvas.setFont(FONT_SMALL);
        int n = static_cast<int>(_entries.size());
        for (int i = 0; i < LIST_LINES; i++) {
            int idx = _scroll_offset + i;
            if (idx >= n) break;
            const FileEntry& e = _entries[idx];
            int y              = LIST_Y0 + i * LIST_LINE_H;
            bool selected      = (idx == _selected_index);

            uint32_t bg = COLOR_PANEL_BG;
            if (selected) {
                bg = COLOR_SEL_BG;
                GetHAL().canvas.fillSmoothRoundRect(4, y - 1, cw - 8, LIST_LINE_H, 2, bg);
            }

            uint32_t caret_col = selected ? COLOR_ACCENT : bg;
            GetHAL().canvas.setTextColor(caret_col, bg);
            GetHAL().canvas.setCursor(LIST_PAD_X, y + 1);
            GetHAL().canvas.print(selected ? ">" : " ");

            uint32_t name_col = e.is_dir ? COLOR_DIR : COLOR_FILE_TEXT;
            std::string nm    = e.name;
            if (e.is_dir) nm += "/";
            if (nm.size() > 22) nm = nm.substr(0, 19) + "...";
            GetHAL().canvas.setTextColor(name_col, bg);
            GetHAL().canvas.setCursor(LIST_PAD_X + 7, y + 1);
            GetHAL().canvas.print(nm.c_str());

            char sbuf[16];
            format_size(e, sbuf, sizeof(sbuf));
            uint32_t size_col = e.is_dir ? COLOR_DIR_DIM : COLOR_DIM_TEXT;
            GetHAL().canvas.setTextColor(size_col, bg);
            GetHAL().canvas.drawRightString(sbuf, cw - 8, y + 1, FONT_SMALL);
        }

        if (_scroll_offset > 0) {
            GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
            GetHAL().canvas.setCursor(cw - 12, PANEL_Y + 1);
            GetHAL().canvas.print("^");
        }
        if (_scroll_offset + LIST_LINES < n) {
            GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
            GetHAL().canvas.setCursor(cw - 12, PANEL_Y + PANEL_H - 9);
            GetHAL().canvas.print("v");
        }
    }

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    if (!_clipboard_path.empty()) {
        const char* base = strrchr(_clipboard_path.c_str(), '/');
        base             = base ? base + 1 : _clipboard_path.c_str();
        char fbuf[40];
        snprintf(fbuf, sizeof(fbuf), "%s %.20s | P paste",
                 _clipboard_is_move ? "MV" : "CP", base);
        GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
        GetHAL().canvas.print(fbuf);
    } else {
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        GetHAL().canvas.print("UP DN ENT BK  N I C M R D P  HOME");
    }

    GetHAL().pushCanvas();
}

void AppFilesManager::draw_confirm_delete()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    draw_title("Delete?");
    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, PANEL_Y + 8);
    GetHAL().canvas.print("Permanently delete this item?");

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextColor(COLOR_WARN, COLOR_PANEL_BG);
    std::string nm = _pending_delete_name;
    if (nm.size() > 24) nm = nm.substr(0, 21) + "...";
    int tw = GetHAL().canvas.textWidth(nm.c_str());
    GetHAL().canvas.setCursor((cw - tw) / 2, PANEL_Y + 28);
    GetHAL().canvas.print(nm.c_str());

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_ERR, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, PANEL_Y + 54);
    GetHAL().canvas.print("This cannot be undone.");

    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Y delete   N / Esc cancel");

    GetHAL().pushCanvas();
}

void AppFilesManager::draw_detail(const FileEntry& e)
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    draw_title(e.is_dir ? "Folder Info" : "File Info");
    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    GetHAL().canvas.setFont(FONT_SMALL);
    int y = PANEL_Y + 6;
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, y);
    GetHAL().canvas.print("Name:");
    GetHAL().canvas.setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    std::string nm = e.name;
    if (nm.size() <= 32) {
        GetHAL().canvas.setCursor(6, y + 10);
        GetHAL().canvas.print(nm.c_str());
    } else {
        GetHAL().canvas.setCursor(6, y + 10);
        GetHAL().canvas.print(nm.substr(0, 32).c_str());
        GetHAL().canvas.setCursor(6, y + 20);
        std::string rest = nm.substr(32);
        if (rest.size() > 32) rest = rest.substr(0, 29) + "...";
        GetHAL().canvas.print(rest.c_str());
    }

    int y2 = PANEL_Y + 38;
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, y2);
    GetHAL().canvas.print("Type:");
    GetHAL().canvas.setTextColor(e.is_dir ? COLOR_DIR : COLOR_FILE_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(40, y2);
    GetHAL().canvas.print(e.is_dir ? "Folder" : "File");

    int y3 = y2 + 10;
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, y3);
    GetHAL().canvas.print("Size:");
    char sbuf[24];
    if (e.is_dir) {
        snprintf(sbuf, sizeof(sbuf), "-");
    } else {
        snprintf(sbuf, sizeof(sbuf), "%u B", (unsigned)e.size);
    }
    GetHAL().canvas.setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(40, y3);
    GetHAL().canvas.print(sbuf);
    if (!e.is_dir && e.size >= 1024) {
        char hbuf[16];
        format_size(e, hbuf, sizeof(hbuf));
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        GetHAL().canvas.print("  (");
        GetHAL().canvas.print(hbuf);
        GetHAL().canvas.print(")");
    }

    int y4 = y3 + 10;
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, y4);
    GetHAL().canvas.print("Modified:");
    GetHAL().canvas.setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(60, y4);
    char tbuf[24];
    struct tm tm_buf;
    time_t t = e.mtime;
    if (t > 0 && gmtime_r(&t, &tm_buf) != nullptr) {
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm_buf);
    } else {
        snprintf(tbuf, sizeof(tbuf), "-");
    }
    GetHAL().canvas.print(tbuf);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Enter / Esc  back");

    GetHAL().pushCanvas();
}

void AppFilesManager::draw_input_prompt(const char* prompt)
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    draw_title(prompt);

    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, PANEL_Y + 6);
    GetHAL().canvas.print("Type name, Enter to confirm:");

    int input_y = PANEL_Y + 22;
    int input_h = 18;
    GetHAL().canvas.fillSmoothRoundRect(6, input_y, cw - 12, input_h, 3, (uint32_t)0x252528);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, (uint32_t)0x252528);
    std::string shown = _input_line;
    if (shown.size() > 22) shown = "..." + shown.substr(shown.size() - 19);
    GetHAL().canvas.setCursor(10, input_y + 1);
    GetHAL().canvas.print(shown.c_str());
    GetHAL().canvas.print("_");

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    char info[40];
    snprintf(info, sizeof(info), "%u chars", (unsigned)_input_line.size());
    GetHAL().canvas.setCursor(6, PANEL_Y + 50);
    GetHAL().canvas.print(info);

    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Enter OK  Bksp del  Esc cancel");

    GetHAL().pushCanvas();
}

void AppFilesManager::draw_need_sd()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();
    draw_title("Files");
    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextColor(COLOR_WARN, COLOR_PANEL_BG);
    const char* m1 = "Insert SD card";
    int t1         = GetHAL().canvas.textWidth(m1);
    GetHAL().canvas.setCursor((cw - t1) / 2, PANEL_Y + 18);
    GetHAL().canvas.print(m1);
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    const char* m2 = "Press Enter to retry";
    int t2         = GetHAL().canvas.textWidth(m2);
    GetHAL().canvas.setCursor((cw - t2) / 2, PANEL_Y + 44);
    GetHAL().canvas.print(m2);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Enter retry  HOME exit");
    GetHAL().pushCanvas();
}

void AppFilesManager::message(const char* msg)
{
    _message_text = msg;
    _state        = State::Message;
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();
    draw_title("Files");
    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    bool is_err = (strstr(msg, "fail") != nullptr) || (strstr(msg, "Fail") != nullptr) ||
                  (strstr(msg, "error") != nullptr) || (strstr(msg, "Error") != nullptr);
    uint32_t col = is_err ? COLOR_ERR : COLOR_WARN;

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextColor(col, COLOR_PANEL_BG);
    int tw = GetHAL().canvas.textWidth(msg);
    if (tw > cw - 16) {
        GetHAL().canvas.setCursor(8, PANEL_Y + PANEL_H / 2 - 8);
    } else {
        GetHAL().canvas.setCursor((cw - tw) / 2, PANEL_Y + PANEL_H / 2 - 8);
    }
    GetHAL().canvas.print(msg);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Enter  close");
    GetHAL().pushCanvas();
}

bool AppFilesManager::do_mkdir(const char* path)  { return mkdir(path, 0755) == 0; }
bool AppFilesManager::do_rename(const char* old_path, const char* new_path) { return rename(old_path, new_path) == 0; }

bool AppFilesManager::do_copy(const char* src, const char* dst)
{
    FILE* fin = fopen(src, "rb");
    if (!fin) return false;
    FILE* fout = fopen(dst, "wb");
    if (!fout) {
        fclose(fin);
        return false;
    }
    char buf[256];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            ok = false;
            break;
        }
    }
    fclose(fin);
    fclose(fout);
    if (!ok) unlink(dst);
    return ok;
}

bool AppFilesManager::do_delete(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) return rmdir(path) == 0;
    return unlink(path) == 0;
}

void AppFilesManager::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _state              = State::Init;
    _current_path.clear();
    _selected_index     = 0;
    _scroll_offset      = 0;
    _clipboard_path.clear();
    _input_line.clear();
    _rename_old_path.clear();
    _entries.clear();

    auto probe = GetHAL().sdCardProbe();
    if (!probe.is_mounted) {
        _state = State::NeedSdCard;
        draw_need_sd();
    } else {
        _state = State::List;
        refresh_list();
        draw_list();
    }

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) {
                return;
            }
            on_key(keyEvent.keyCode, keyEvent.keyName);
        });
}

void AppFilesManager::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppFilesManager::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}

void AppFilesManager::on_key(int keyCode, const char* keyName)
{
    switch (_state) {
        case State::List:           on_key_list(keyCode); break;
        case State::Detail:         on_key_detail(keyCode); break;
        case State::NewFolder:      on_key_input_mode(keyCode, keyName, "New folder name:"); break;
        case State::Rename:         on_key_input_mode(keyCode, keyName, "Rename to:"); break;
        case State::Message:        on_key_message(keyCode); break;
        case State::ConfirmDelete:  on_key_confirm_delete(keyCode); break;
        case State::NeedSdCard:     on_key_need_sd(keyCode); break;
        case State::Init:           break;
    }
}

void AppFilesManager::on_key_need_sd(int keyCode)
{
    if (keyCode == KEY_ENTER) {
        auto probe = GetHAL().sdCardProbe();
        if (probe.is_mounted) {
            _state = State::List;
            refresh_list();
            draw_list();
        }
    }
}

void AppFilesManager::on_key_list(int keyCode)
{
    if (keyCode == KEY_UP) {
        if (_selected_index > 0) {
            _selected_index--;
            if (_scroll_offset > _selected_index) _scroll_offset = _selected_index;
            draw_list();
        }
    } else if (keyCode == KEY_DOWN) {
        if (_selected_index < static_cast<int>(_entries.size()) - 1) {
            _selected_index++;
            if (_selected_index >= _scroll_offset + LIST_LINES)
                _scroll_offset = _selected_index - LIST_LINES + 1;
            draw_list();
        }
    } else if (keyCode == KEY_ENTER && !_entries.empty()) {
        const FileEntry& e = _entries[_selected_index];
        if (e.is_dir) {
            _current_path  = _current_path.empty() ? e.name : (_current_path + "/" + e.name);
            _selected_index = 0;
            _scroll_offset  = 0;
            refresh_list();
            draw_list();
        } else {
            _state = State::Detail;
            draw_detail(e);
        }
    } else if (keyCode == KEY_BACKSPACE) {
        size_t pos = _current_path.find_last_of('/');
        if (pos == std::string::npos) {
            _current_path.clear();
        } else {
            _current_path = _current_path.substr(0, pos);
        }
        _selected_index = 0;
        _scroll_offset  = 0;
        refresh_list();
        draw_list();
    } else if (keyCode == KEY_N) {
        _state = State::NewFolder;
        _input_line.clear();
        draw_input_prompt("New folder name:");
    } else if (keyCode == KEY_I) {
        if (!_entries.empty()) {
            _state = State::Detail;
            draw_detail(_entries[_selected_index]);
        }
    } else if (keyCode == KEY_C || keyCode == KEY_M) {
        if (!_entries.empty()) {
            _clipboard_path    = path_join(_current_path, _entries[_selected_index].name);
            _clipboard_is_move = (keyCode == KEY_M);
            draw_list();
        }
    } else if (keyCode == KEY_P) {
        if (!_clipboard_path.empty()) {
            std::string dest_dir = path_full(_current_path);
            const char* base     = strrchr(_clipboard_path.c_str(), '/');
            base                 = base ? base + 1 : _clipboard_path.c_str();
            std::string dst      = dest_dir + "/" + base;
            bool ok              = do_copy(_clipboard_path.c_str(), dst.c_str());
            if (ok && _clipboard_is_move) ok = do_delete(_clipboard_path.c_str());
            _clipboard_path.clear();
            refresh_list();
            draw_list();
            if (!ok) message("Paste failed");
        }
    } else if (keyCode == KEY_R) {
        if (!_entries.empty()) {
            _rename_old_path = path_join(_current_path, _entries[_selected_index].name);
            _input_line      = _entries[_selected_index].name;
            _state           = State::Rename;
            draw_input_prompt("Rename to:");
        }
    } else if (keyCode == KEY_D) {
        if (!_entries.empty()) {
            _pending_delete_path = path_join(_current_path, _entries[_selected_index].name);
            _pending_delete_name = _entries[_selected_index].name;
            _state               = State::ConfirmDelete;
            draw_confirm_delete();
        }
    }
}

void AppFilesManager::on_key_detail(int keyCode)
{
    if (keyCode == KEY_ENTER || keyCode == KEY_ESC || keyCode == KEY_BACKSPACE) {
        _state = State::List;
        draw_list();
    }
}

void AppFilesManager::on_key_input_mode(int keyCode, const char* keyName, const char* prompt)
{
    if (keyCode == KEY_ENTER) {
        if (!_input_line.empty()) {
            if (_state == State::NewFolder) {
                std::string full = path_join(_current_path, _input_line);
                if (do_mkdir(full.c_str())) {
                    _state = State::List;
                    refresh_list();
                    draw_list();
                } else {
                    message("Create folder failed");
                }
            } else {  // Rename
                std::string new_full = path_join(_current_path, _input_line);
                if (do_rename(_rename_old_path.c_str(), new_full.c_str())) {
                    _state = State::List;
                    refresh_list();
                    draw_list();
                } else {
                    message("Rename failed");
                }
            }
        }
        return;
    }

    if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
        if (!_input_line.empty()) {
            _input_line.pop_back();
            draw_input_prompt(prompt);
        }
        return;
    }

    if (keyCode == KEY_ESC) {
        _state = State::List;
        draw_list();
        return;
    }

    if (keyName != nullptr && keyName[0] != '\0' && keyName[1] == '\0') {
        _input_line += keyName;
        draw_input_prompt(prompt);
    }
}

void AppFilesManager::on_key_message(int keyCode)
{
    if (keyCode == KEY_ENTER) {
        _state = State::List;
        draw_list();
    }
}

void AppFilesManager::on_key_confirm_delete(int keyCode)
{
    if (keyCode == KEY_Y) {
        bool ok = do_delete(_pending_delete_path.c_str());
        _pending_delete_path.clear();
        _pending_delete_name.clear();
        if (ok) {
            _state = State::List;
            refresh_list();
            draw_list();
        } else {
            message("Delete failed");
        }
    } else if (keyCode == KEY_N || keyCode == KEY_ESC) {
        _pending_delete_path.clear();
        _pending_delete_name.clear();
        _state = State::List;
        draw_list();
    }
}
