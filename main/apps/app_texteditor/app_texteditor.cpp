/*
 * SPDX-FileCopyrightText: 2024 Anderson Antunes
 * SPDX-FileContributor: ported to CardputerADV from M5Cardputer dev-main
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_texteditor.h"
#include "assets/texteditor_big.h"
#include "assets/texteditor_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

using namespace mooncake;

#define NOTE_PATH "/sdcard/note.txt"

AppTextEditor::AppTextEditor()
{
    setAppInfo().name     = "Notepad";
    setAppInfo().userData = new AppIcon_t(image_data_texteditor_big, image_data_texteditor_small);
}

AppTextEditor::~AppTextEditor()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppTextEditor::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _text_buffer.clear();
    _lines                 = 1;
    _waiting_user_input    = false;
    _dialog_action         = DialogAction::None;
    _cursor_blink_last_ms  = GetHAL().millis();
    _cursor_visible        = false;

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextScroll(true);
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(FONT_SIZE_REPL);
    GetHAL().canvas.setCursor(0, 0);

    auto probe   = GetHAL().sdCardProbe();
    _sd_mounted  = probe.is_mounted;

    if (!_sd_mounted) {
        render_dialog("SD card mount failed\nInsert card and re-open the app\nor press Home to quit", false);
    } else {
        load_buffer_from_disk();
    }

    GetHAL().pushCanvas();

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (keyEvent.state == false) {
                return;
            }
            on_key_event(keyEvent.keyCode, keyEvent.isModifier, keyEvent.keyName);
        });
}

void AppTextEditor::onRunning()
{
    blink_cursor();

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        mclog::tagInfo(getAppInfo().name, "quit");
        if (_sd_mounted && !_text_buffer.empty()) {
            append_buffer_to_disk();
        }
        close();
    }
}

void AppTextEditor::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
    GetHAL().canvas.setTextScroll(false);
}

void AppTextEditor::load_buffer_from_disk()
{
    if (access(NOTE_PATH, F_OK) != 0) {
        // File does not exist yet — start with line 1 prompt.
        GetHAL().canvas.print(" 1 ");
        _lines = 2;
        return;
    }

    FILE* f = fopen(NOTE_PATH, "r");
    if (!f) {
        render_dialog("Failed to open note.txt for reading", true);
        return;
    }

    _lines = 1;
    char line[80];
    while (fgets(line, sizeof(line), f)) {
        if (_lines < 10) {
            GetHAL().canvas.print(" ");
        }
        GetHAL().canvas.print(_lines);
        GetHAL().canvas.print(" ");
        GetHAL().canvas.print(line);
        _lines++;
    }
    fclose(f);

    if (GetHAL().canvas.getCursorX() == 0) {
        if (_lines < 10) {
            GetHAL().canvas.print(" ");
        }
        GetHAL().canvas.print(_lines);
        GetHAL().canvas.print(" ");
    }
    _lines++;
}

void AppTextEditor::render_dialog(const std::string& message, bool can_skip)
{
    mclog::tagInfo(getAppInfo().name, "dialog: {}", message);
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 0);
    _text_buffer.clear();
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    GetHAL().canvas.print(message.c_str());
    _waiting_user_input = true;
    _dialog_skippable   = can_skip;
    _dialog_action      = DialogAction::None;
    if (can_skip) {
        GetHAL().canvas.print("\nPress Enter to continue\n");
    }
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().pushCanvas();
}

void AppTextEditor::on_key_event(int keyCode, bool isModifier, const char* keyName)
{
    if (isModifier) {
        return;
    }

    uint8_t mods    = GetHAL().keyboard.getModifierMask();
    bool ctrl_down  = mods & (KEY_MOD_LCTRL | KEY_MOD_RCTRL);

    // Ctrl+Backspace: delete-file confirmation
    if (ctrl_down && keyCode == KEY_BACKSPACE) {
        render_dialog("Delete note.txt? [y/N]: ", false);
        _dialog_action = DialogAction::DeleteFile;
        return;
    }

    if (keyCode == KEY_ENTER) {
        GetHAL().canvas.print(" \n");
        if (_lines < 10) {
            GetHAL().canvas.print(" ");
        }
        GetHAL().canvas.print(_lines);
        GetHAL().canvas.print(" ");
        _lines++;

        if (_waiting_user_input) {
            _waiting_user_input = false;
            bool confirmed      = (_text_buffer == "y" || _text_buffer == "Y");
            DialogAction action = _dialog_action;
            _dialog_action      = DialogAction::None;
            _text_buffer.clear();

            if (action == DialogAction::DeleteFile && confirmed) {
                if (access(NOTE_PATH, F_OK) == 0) {
                    if (remove(NOTE_PATH) != 0) {
                        render_dialog("Failed to delete note.txt", true);
                        return;
                    }
                    mclog::tagInfo(getAppInfo().name, "note.txt deleted");
                }
            }

            // Re-render the editor body after dismissing the dialog.
            GetHAL().canvas.fillScreen(THEME_COLOR_BG);
            GetHAL().canvas.setCursor(0, 0);
            _lines = 1;
            if (_sd_mounted) {
                load_buffer_from_disk();
            }
            GetHAL().pushCanvas();
            return;
        }

        if (_sd_mounted) {
            append_buffer_to_disk();
            _text_buffer.clear();
        }

        GetHAL().pushCanvas();
        return;
    }

    if (keyCode == KEY_SPACE) {
        GetHAL().canvas.print(' ');
        _text_buffer += ' ';
        GetHAL().pushCanvas();
        return;
    }

    if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
        if (!_text_buffer.empty()) {
            _text_buffer.pop_back();
            erase_last_char_on_canvas();
            GetHAL().pushCanvas();
        }
        return;
    }

    // Normal printable character path. Keyboard layer hands us the post-shift
    // character in keyName; ignore zero-length names (modifier-only, F-keys, etc.).
    if (keyName != nullptr && keyName[0] != '\0') {
        GetHAL().canvas.print(keyName);
        _text_buffer += keyName;
        GetHAL().pushCanvas();
    }
}

void AppTextEditor::erase_last_char_on_canvas()
{
    int cx = GetHAL().canvas.getCursorX();
    int cy = GetHAL().canvas.getCursorY();
    if (cx - FONT_REPL_WIDTH < 0) {
        cy -= FONT_REPL_HEIGHT;
        cx = GetHAL().canvas.width() - FONT_REPL_WIDTH;
    } else {
        cx -= FONT_REPL_WIDTH;
    }
    GetHAL().canvas.setCursor(cx, cy);
    GetHAL().canvas.print("  ");
    GetHAL().canvas.setCursor(cx, cy);
}

void AppTextEditor::blink_cursor()
{
    if (GetHAL().millis() - _cursor_blink_last_ms <= _cursor_blink_period_ms) {
        return;
    }

    if (GetHAL().canvas.getCursorX() == 0 ||
        GetHAL().canvas.width() - GetHAL().canvas.getCursorX() < FONT_REPL_WIDTH) {
        GetHAL().canvas.print(" ");
        GetHAL().canvas.setCursor(0, GetHAL().canvas.getCursorY());
    }

    int cx = GetHAL().canvas.getCursorX();
    int cy = GetHAL().canvas.getCursorY();
    GetHAL().canvas.print(_cursor_visible ? '_' : ' ');
    GetHAL().canvas.setCursor(cx, cy);
    GetHAL().pushCanvas();

    _cursor_visible       = !_cursor_visible;
    _cursor_blink_last_ms = GetHAL().millis();
}

void AppTextEditor::append_buffer_to_disk()
{
    if (_text_buffer.empty()) {
        return;
    }
    FILE* f = fopen(NOTE_PATH, "a");
    if (!f) {
        render_dialog("Failed to open note.txt for writing", true);
        return;
    }
    fprintf(f, "%s\n", _text_buffer.c_str());
    fclose(f);
    mclog::tagInfo(getAppInfo().name, "wrote {} bytes to {}", _text_buffer.size(), NOTE_PATH);
}
