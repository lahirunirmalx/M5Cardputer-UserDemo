/**
 * @file app_files.cpp
 * @brief File Manager: list folders/files, details, new folder, copy/move/rename/delete
 */
#include "app_files.h"
#include "spdlog/spdlog.h"
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)
#define _sdcard _data.hal->sdcard()

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y      = 1;
static constexpr int CHIP_Y       = 5;
static constexpr int PANEL_Y      = 19;
static constexpr int PANEL_H      = 78;
static constexpr int LIST_PAD_X   = 5;
static constexpr int LIST_PAD_Y   = 4;
static constexpr int LIST_LINE_H  = 10;
static constexpr int LIST_LINES   = 7;
static constexpr int LIST_Y0      = PANEL_Y + LIST_PAD_Y;
static constexpr int FOOTER_Y     = 100;

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG   = (uint32_t)0x1E1E22;
static const uint32_t COLOR_SEL_BG     = (uint32_t)0x3A3A60;
static const uint32_t COLOR_DIR        = (uint32_t)0x94B2;     /* light blue */
static const uint32_t COLOR_DIR_DIM    = (uint32_t)0x6275;
static const uint32_t COLOR_FILE_TEXT  = (uint32_t)0xE6E6E6;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_WARN       = (uint32_t)0xFFB060;
static const uint32_t COLOR_ERR        = (uint32_t)0xFF6464;

bool AppFilesManager::_path_full(std::string& out, const std::string& rel) const
{
    char* p = _sdcard->get_filepath(rel.empty() ? "" : rel.c_str());
    if (!p) return false;
    out = p;
    free(p);
    if (!out.empty() && out.back() == '/') out.pop_back();
    return true;
}

bool AppFilesManager::_path_join(std::string& out, const std::string& rel, const std::string& name) const
{
    std::string r = rel.empty() ? name : (rel + "/" + name);
    return _path_full(out, r);
}

void AppFilesManager::_refresh_list()
{
    _data.entries.clear();
    std::string full;
    if (!_path_full(full, _data.current_path)) return;
    DIR* d = opendir(full.c_str());
    if (!d) return;
    struct dirent* ent;
    std::vector<FileEntry> dirs, files;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string item_path;
        if (!_path_join(item_path, _data.current_path, ent->d_name)) continue;
        struct stat st;
        if (stat(item_path.c_str(), &st) != 0) continue;
        FileEntry e;
        e.name = ent->d_name;
        e.is_dir = S_ISDIR(st.st_mode);
        e.size = (size_t)st.st_size;
        e.mtime = st.st_mtime;
        if (e.is_dir) dirs.push_back(e);
        else files.push_back(e);
    }
    closedir(d);
    for (const auto& e : dirs) _data.entries.push_back(e);
    for (const auto& e : files) _data.entries.push_back(e);
    if (_data.selected_index >= (int)_data.entries.size())
        _data.selected_index = _data.entries.empty() ? 0 : (int)_data.entries.size() - 1;
    if (_data.selected_index < 0) _data.selected_index = 0;
    _data.scroll_offset = 0;
    if (_data.selected_index >= LIST_LINES)
        _data.scroll_offset = _data.selected_index - LIST_LINES + 1;
}

void AppFilesManager::_format_size(const FileEntry& e, char* out, size_t out_size)
{
    if (e.is_dir) { snprintf(out, out_size, "<DIR>"); return; }
    if (e.size < 1024)
        snprintf(out, out_size, "%uB", (unsigned)e.size);
    else if (e.size < 1024UL * 1024UL)
        snprintf(out, out_size, "%.1fK", (double)e.size / 1024.0);
    else if (e.size < 1024UL * 1024UL * 1024UL)
        snprintf(out, out_size, "%.1fM", (double)e.size / (1024.0 * 1024.0));
    else
        snprintf(out, out_size, "%.1fG", (double)e.size / (1024.0 * 1024.0 * 1024.0));
}

void AppFilesManager::_draw_title(const char* title)
{
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print(title);
}

void AppFilesManager::_draw_list()
{
    _canvas_clear();
    int cw = _canvas->width();

    /* Title + path chip on right */
    _draw_title("Files");
    std::string path_display = _data.current_path.empty() ? "/" : ("/" + _data.current_path);
    if (path_display.size() > 22)
        path_display = "..." + path_display.substr(path_display.size() - 19);
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->drawRightString(path_display.c_str(), cw - 4, CHIP_Y, FONT_SMALL);

    /* Clipboard indicator just under title-right when set */
    if (!_data.clipboard_path.empty()) {
        const char* base = strrchr(_data.clipboard_path.c_str(), '/');
        base = base ? base + 1 : _data.clipboard_path.c_str();
        char cbbuf[28];
        snprintf(cbbuf, sizeof(cbbuf), "%s:%s",
                 _data.clipboard_is_move ? "MV" : "CP", base);
        if (strlen(cbbuf) > 26) cbbuf[26] = '\0';
        _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
        _canvas->drawRightString(cbbuf, cw - 4, CHIP_Y + 10, FONT_SMALL);
    }

    /* List panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    if (_data.entries.empty()) {
        _canvas->setFont(FONT_SMALL);
        _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        const char* msg = "(empty)";
        int tw = _canvas->textWidth(msg);
        _canvas->setCursor((cw - tw) / 2, PANEL_Y + PANEL_H / 2 - 4);
        _canvas->print(msg);
    } else {
        _canvas->setFont(FONT_SMALL);
        int n = (int)_data.entries.size();
        for (int i = 0; i < LIST_LINES; i++) {
            int idx = _data.scroll_offset + i;
            if (idx >= n) break;
            const FileEntry& e = _data.entries[idx];
            int y = LIST_Y0 + i * LIST_LINE_H;
            bool selected = (idx == _data.selected_index);

            uint32_t bg = COLOR_PANEL_BG;
            if (selected) {
                bg = COLOR_SEL_BG;
                _canvas->fillSmoothRoundRect(4, y - 1, cw - 8, LIST_LINE_H, 2, bg);
            }

            /* Selection caret */
            uint32_t caret_col = selected ? COLOR_ACCENT : bg;
            _canvas->setTextColor(caret_col, bg);
            _canvas->setCursor(LIST_PAD_X, y + 1);
            _canvas->print(selected ? ">" : " ");

            /* Name (with trailing '/' for dirs) */
            uint32_t name_col = e.is_dir ? COLOR_DIR : COLOR_FILE_TEXT;
            std::string nm = e.name;
            if (e.is_dir) nm += "/";
            if (nm.size() > 22) nm = nm.substr(0, 19) + "...";
            _canvas->setTextColor(name_col, bg);
            _canvas->setCursor(LIST_PAD_X + 7, y + 1);
            _canvas->print(nm.c_str());

            /* Right-aligned size / <DIR> */
            char sbuf[16];
            _format_size(e, sbuf, sizeof(sbuf));
            uint32_t size_col = e.is_dir ? COLOR_DIR_DIM : COLOR_DIM_TEXT;
            _canvas->setTextColor(size_col, bg);
            _canvas->drawRightString(sbuf, cw - 8, y + 1, FONT_SMALL);
        }

        /* Scroll indicators */
        if (_data.scroll_offset > 0) {
            _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
            _canvas->setCursor(cw - 12, PANEL_Y + 1);
            _canvas->print("^");
        }
        if (_data.scroll_offset + LIST_LINES < n) {
            _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
            _canvas->setCursor(cw - 12, PANEL_Y + PANEL_H - 9);
            _canvas->print("v");
        }
    }

    /* Footer hint */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("^v ENT BK  N I C M R D P  HOME");

    _canvas_update();
}

void AppFilesManager::_draw_detail(const FileEntry& e)
{
    _canvas_clear();
    int cw = _canvas->width();

    _draw_title(e.is_dir ? "Folder Info" : "File Info");

    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    _canvas->setFont(FONT_SMALL);
    int y = PANEL_Y + 6;
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(6, y);
    _canvas->print("Name:");
    _canvas->setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    /* Wrap name to 2 lines if needed */
    std::string nm = e.name;
    if (nm.size() <= 32) {
        _canvas->setCursor(6, y + 10);
        _canvas->print(nm.c_str());
    } else {
        _canvas->setCursor(6, y + 10);
        _canvas->print(nm.substr(0, 32).c_str());
        _canvas->setCursor(6, y + 20);
        std::string rest = nm.substr(32);
        if (rest.size() > 32) rest = rest.substr(0, 29) + "...";
        _canvas->print(rest.c_str());
    }

    int y2 = PANEL_Y + 38;
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(6, y2);
    _canvas->print("Type:");
    _canvas->setTextColor(e.is_dir ? COLOR_DIR : COLOR_FILE_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(40, y2);
    _canvas->print(e.is_dir ? "Folder" : "File");

    int y3 = y2 + 10;
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(6, y3);
    _canvas->print("Size:");
    char sbuf[24];
    if (e.is_dir)
        snprintf(sbuf, sizeof(sbuf), "-");
    else
        snprintf(sbuf, sizeof(sbuf), "%u B", (unsigned)e.size);
    _canvas->setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(40, y3);
    _canvas->print(sbuf);
    if (!e.is_dir && e.size >= 1024) {
        char hbuf[16];
        FileEntry tmp = e;
        _format_size(tmp, hbuf, sizeof(hbuf));
        _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        _canvas->print("  (");
        _canvas->print(hbuf);
        _canvas->print(")");
    }

    int y4 = y3 + 10;
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(6, y4);
    _canvas->print("Modified:");
    _canvas->setTextColor(COLOR_FILE_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(60, y4);
    char tbuf[24];
    snprintf(tbuf, sizeof(tbuf), "%lu", (unsigned long)e.mtime);
    _canvas->print(tbuf);

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Enter / Esc  back");

    _canvas_update();
}

void AppFilesManager::_draw_input_prompt(const char* prompt)
{
    _canvas_clear();
    int cw = _canvas->width();

    _draw_title(prompt);

    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    _canvas->setCursor(6, PANEL_Y + 6);
    _canvas->print("Type name, Enter to confirm:");

    /* Input row inside panel */
    int input_y = PANEL_Y + 22;
    int input_h = 18;
    _canvas->fillSmoothRoundRect(6, input_y, cw - 12, input_h, 3, (uint32_t)0x252528);
    _canvas->setFont(FONT_REPL);
    _canvas->setTextColor(COLOR_ACCENT, (uint32_t)0x252528);
    std::string shown = _data.input_line;
    if (shown.size() > 22) shown = "..." + shown.substr(shown.size() - 19);
    _canvas->setCursor(10, input_y + 1);
    _canvas->print(shown.c_str());
    _canvas->print("_");

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
    char info[40];
    snprintf(info, sizeof(info), "%u chars", (unsigned)_data.input_line.size());
    _canvas->setCursor(6, PANEL_Y + 50);
    _canvas->print(info);

    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Enter OK  Bksp del  Esc cancel");

    _canvas_update();
}

void AppFilesManager::_message(const char* msg)
{
    _data.message_text = msg;
    _data.current_state = state_message;
    _canvas_clear();
    int cw = _canvas->width();

    _draw_title("Files");

    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    /* Pick color based on simple keywords */
    bool is_err = (strstr(msg, "fail") != nullptr) || (strstr(msg, "Fail") != nullptr) ||
                  (strstr(msg, "error") != nullptr) || (strstr(msg, "Error") != nullptr);
    uint32_t col = is_err ? COLOR_ERR : COLOR_WARN;

    _canvas->setFont(FONT_REPL);
    _canvas->setTextColor(col, COLOR_PANEL_BG);
    int tw = _canvas->textWidth(msg);
    if (tw > cw - 16) {
        _canvas->setCursor(8, PANEL_Y + PANEL_H / 2 - 8);
        _canvas->print(msg);
    } else {
        _canvas->setCursor((cw - tw) / 2, PANEL_Y + PANEL_H / 2 - 8);
        _canvas->print(msg);
    }

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Enter  close");

    _canvas_update();
}

bool AppFilesManager::_do_mkdir(const char* path)
{
    return mkdir(path, 0755) == 0;
}

bool AppFilesManager::_do_copy(const char* src, const char* dst)
{
    FILE* fin = fopen(src, "rb");
    if (!fin) return false;
    FILE* fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return false; }
    char buf[256];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
        if (fwrite(buf, 1, n, fout) != n) { ok = false; break; }
    fclose(fin);
    fclose(fout);
    if (!ok) unlink(dst);
    return ok;
}

bool AppFilesManager::_do_rename(const char* old_path, const char* new_path)
{
    return rename(old_path, new_path) == 0;
}

bool AppFilesManager::_do_delete(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) return rmdir(path) == 0;
    return unlink(path) == 0;
}

void AppFilesManager::onCreate()
{
    spdlog::info("{} onCreate", getAppName());
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppFilesManager::onResume()
{
    ANIM_APP_OPEN();
    _data.current_state = state_init;
    _data.current_path.clear();
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.last_key_num = 0;
    _data.clipboard_path.clear();
    _data.input_line.clear();
    _data.rename_old_path.clear();
}

void AppFilesManager::onRunning()
{
    if (!_data.hal || !_sdcard) return;

    if (_data.current_state == state_init) {
        if (!_sdcard->mount(false)) {
            _canvas_clear();
            int cw = _canvas->width();
            _draw_title("Files");
            _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
            _canvas->setFont(FONT_REPL);
            _canvas->setTextColor(COLOR_WARN, COLOR_PANEL_BG);
            const char* m1 = "Insert SD card";
            int t1 = _canvas->textWidth(m1);
            _canvas->setCursor((cw - t1) / 2, PANEL_Y + 18);
            _canvas->print(m1);
            _canvas->setFont(FONT_SMALL);
            _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
            const char* m2 = "Press Enter to retry";
            int t2 = _canvas->textWidth(m2);
            _canvas->setCursor((cw - t2) / 2, PANEL_Y + 44);
            _canvas->print(m2);
            _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
            _canvas->setCursor(3, FOOTER_Y);
            _canvas->print("Enter retry  HOME exit");
            _canvas_update();
            _keyboard->updateKeyList();
            if (_data.last_key_num != _keyboard->keyList().size() && _keyboard->keyList().size() > 0) {
                if (_keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ENTER) {
                    _data.last_key_num = _keyboard->keyList().size();
                    if (_sdcard->mount(false)) {
                        _data.current_state = state_list;
                        _refresh_list();
                        _draw_list();
                    }
                }
            }
        } else {
            _data.current_state = state_list;
            _refresh_list();
            _draw_list();
        }
    }

    if (_data.current_state == state_list) {
        _keyboard->updateKeyList();
        if (_data.last_key_num != _keyboard->keyList().size() && _keyboard->keyList().size() > 0) {
            auto key = _keyboard->getKey();
            int v = _keyboard->getKeyValue(key).value_num_first;
            if (v == KEY_UP) {
                if (_data.selected_index > 0) {
                    _data.selected_index--;
                    if (_data.scroll_offset > _data.selected_index)
                        _data.scroll_offset = _data.selected_index;
                    _draw_list();
                }
            } else if (v == KEY_DOWN) {
                if (_data.selected_index < (int)_data.entries.size() - 1) {
                    _data.selected_index++;
                    if (_data.selected_index >= _data.scroll_offset + LIST_LINES)
                        _data.scroll_offset = _data.selected_index - LIST_LINES + 1;
                    _draw_list();
                }
            } else if (v == KEY_ENTER) {
                if (_data.entries.empty()) { _data.last_key_num = _keyboard->keyList().size(); return; }
                const FileEntry& e = _data.entries[_data.selected_index];
                if (e.is_dir) {
                    _data.current_path = _data.current_path.empty() ? e.name : (_data.current_path + "/" + e.name);
                    _refresh_list();
                    _draw_list();
                } else {
                    _data.current_state = state_detail;
                    _draw_detail(e);
                }
            } else if (v == KEY_BACKSPACE) {
                size_t pos = _data.current_path.find_last_of('/');
                if (pos == std::string::npos)
                    _data.current_path.clear();
                else
                    _data.current_path = _data.current_path.substr(0, pos);
                _refresh_list();
                _draw_list();
            } else if (v == KEY_N) {
                _data.current_state = state_newfolder;
                _data.input_line.clear();
                _draw_input_prompt("New folder name:");
            } else if (v == KEY_I) {
                if (!_data.entries.empty()) {
                    _data.current_state = state_detail;
                    _draw_detail(_data.entries[_data.selected_index]);
                }
            } else if (v == KEY_C || v == KEY_M) {
                if (!_data.entries.empty()) {
                    std::string full;
                    _path_join(full, _data.current_path, _data.entries[_data.selected_index].name);
                    _data.clipboard_path = full;
                    _data.clipboard_is_move = (v == KEY_M);
                    _draw_list();
                }
            } else if (v == KEY_P) {
                if (!_data.clipboard_path.empty()) {
                    std::string dest_dir;
                    _path_full(dest_dir, _data.current_path);
                    const char* src = _data.clipboard_path.c_str();
                    const char* base = strrchr(src, '/');
                    base = base ? base + 1 : src;
                    std::string dst = dest_dir + "/" + base;
                    bool ok = _do_copy(src, dst.c_str());
                    if (ok && _data.clipboard_is_move) ok = _do_delete(src);
                    _data.clipboard_path.clear();
                    _refresh_list();
                    _draw_list();
                    if (!ok) _message("Paste failed");
                }
            } else if (v == KEY_R) {
                if (!_data.entries.empty()) {
                    _data.rename_old_path.clear();
                    _path_join(_data.rename_old_path, _data.current_path, _data.entries[_data.selected_index].name);
                    _data.input_line = _data.entries[_data.selected_index].name;
                    _data.current_state = state_rename;
                    _draw_input_prompt("Rename to:");
                }
            } else if (v == KEY_D) {
                if (!_data.entries.empty()) {
                    std::string full;
                    _path_join(full, _data.current_path, _data.entries[_data.selected_index].name);
                    if (_do_delete(full.c_str())) {
                        _refresh_list();
                        _draw_list();
                    } else {
                        _message("Delete failed");
                    }
                }
            }
            _data.last_key_num = _keyboard->keyList().size();
        }
    }

    else if (_data.current_state == state_detail) {
        _keyboard->updateKeyList();
        if (_data.last_key_num != _keyboard->keyList().size() && _keyboard->keyList().size() > 0) {
            if (_keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ENTER ||
                _keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ESC) {
                _data.current_state = state_list;
                _draw_list();
            }
            _data.last_key_num = _keyboard->keyList().size();
        }
    }

    else if (_data.current_state == state_newfolder) {
        _keyboard->updateKeyList();
        _keyboard->updateKeysState();
        if (_keyboard->keysState().enter) {
            if (!_data.input_line.empty()) {
                std::string full;
                _path_join(full, _data.current_path, _data.input_line);
                if (_do_mkdir(full.c_str())) {
                    _data.current_state = state_list;
                    _refresh_list();
                    _draw_list();
                } else {
                    _message("Create folder failed");
                }
            }
            _data.last_key_num = _keyboard->keyList().size();
        } else if (_keyboard->keysState().del && !_data.input_line.empty()) {
            _data.input_line.pop_back();
            _draw_input_prompt("New folder name:");
        } else if (_keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ESC) {
            _data.current_state = state_list;
            _draw_list();
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            auto key = _keyboard->getKey();
            const char* vf = _keyboard->getKeyValue(key).value_first;
            if (vf && strlen(vf) == 1 && _keyboard->keyList().size() != _data.last_key_num) {
                _data.input_line += vf;
                _draw_input_prompt("New folder name:");
            }
            _data.last_key_num = _keyboard->keyList().size();
        }
    }

    else if (_data.current_state == state_rename) {
        _keyboard->updateKeyList();
        _keyboard->updateKeysState();
        if (_keyboard->keysState().enter) {
            if (!_data.input_line.empty()) {
                std::string new_full;
                _path_join(new_full, _data.current_path, _data.input_line);
                if (_do_rename(_data.rename_old_path.c_str(), new_full.c_str())) {
                    _data.current_state = state_list;
                    _refresh_list();
                    _draw_list();
                } else {
                    _message("Rename failed");
                }
            }
            _data.last_key_num = _keyboard->keyList().size();
        } else if (_keyboard->keysState().del && !_data.input_line.empty()) {
            _data.input_line.pop_back();
            _draw_input_prompt("Rename to:");
        } else if (_keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ESC) {
            _data.current_state = state_list;
            _draw_list();
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            auto key = _keyboard->getKey();
            const char* vf = _keyboard->getKeyValue(key).value_first;
            if (vf && strlen(vf) == 1 && _keyboard->keyList().size() != _data.last_key_num) {
                _data.input_line += vf;
                _draw_input_prompt("Rename to:");
            }
            _data.last_key_num = _keyboard->keyList().size();
        }
    }

    else if (_data.current_state == state_message) {
        _keyboard->updateKeyList();
        if (_data.last_key_num != _keyboard->keyList().size() && _keyboard->keyList().size() > 0) {
            if (_keyboard->getKeyValue(_keyboard->getKey()).value_num_first == KEY_ENTER) {
                _data.current_state = state_list;
                _draw_list();
            }
            _data.last_key_num = _keyboard->keyList().size();
        }
    }

    if (_data.current_state != state_init) {
        if (_data.hal->homeButton()->pressed()) {
            _data.hal->playNextSound();
            destroyApp();
        }
    }
}

void AppFilesManager::onDestroy()
{
}
