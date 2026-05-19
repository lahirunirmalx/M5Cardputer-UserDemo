/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct FileEntry {
    std::string name;
    bool is_dir;
    size_t size;
    time_t mtime;
};

class AppFilesManager : public mooncake::AppAbility {
public:
    AppFilesManager();
    ~AppFilesManager();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class State {
        Init,
        List,
        Detail,
        NewFolder,
        Rename,
        Message,
        ConfirmDelete,
        NeedSdCard,
    };

    void on_key(int keyCode, const char* keyName);
    void on_key_list(int keyCode);
    void on_key_detail(int keyCode);
    void on_key_input_mode(int keyCode, const char* keyName, const char* prompt);
    void on_key_message(int keyCode);
    void on_key_confirm_delete(int keyCode);
    void on_key_need_sd(int keyCode);

    void refresh_list();
    void draw_list();
    void draw_detail(const FileEntry& e);
    void draw_input_prompt(const char* prompt);
    void draw_title(const char* title);
    void draw_confirm_delete();
    void draw_need_sd();
    void message(const char* msg);

    static void format_size(const FileEntry& e, char* out, size_t out_size);

    std::string path_full(const std::string& rel) const;
    std::string path_join(const std::string& rel, const std::string& name) const;

    bool do_mkdir(const char* path);
    bool do_copy(const char* src, const char* dst);
    bool do_rename(const char* old_path, const char* new_path);
    bool do_delete(const char* path);

    int  _key_slot_id          = -1;
    State _state               = State::Init;
    std::string _current_path  = "";   // relative to /sdcard, empty == root
    std::vector<FileEntry> _entries;
    int _selected_index        = 0;
    int _scroll_offset         = 0;
    std::string _clipboard_path;       // full path
    bool _clipboard_is_move    = false;
    std::string _input_line;
    std::string _rename_old_path;
    std::string _message_text;
    std::string _pending_delete_path;
    std::string _pending_delete_name;
};
