/**
 * @file app_files.h
 * @brief File Manager: list folders/files, view details, new folder, copy/move/rename/delete
 */
#pragma once
#include <mooncake.h>
#include <string>
#include <vector>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/files_big.h"
#include "assets/files_small.h"

namespace MOONCAKE
{
namespace APPS
{

struct FileEntry {
    std::string name;
    bool is_dir;
    size_t size;
    time_t mtime;
};

class AppFilesManager : public APP_BASE
{
    enum State_t {
        state_init,
        state_list,      /* browsing directory */
        state_detail,    /* file/folder info */
        state_newfolder, /* input new folder name */
        state_rename,    /* input new name */
        state_message    /* show message, Enter to close */
    };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        State_t current_state = state_init;
        std::string current_path;           /* relative to mount, empty = root */
        std::vector<FileEntry> entries;
        int selected_index = 0;
        int scroll_offset = 0;
        size_t last_key_num = 0;
        /* clipboard for copy/move */
        std::string clipboard_path;         /* full path */
        bool clipboard_is_move = false;
        /* line input for newfolder / rename */
        std::string input_line;
        std::string rename_old_path;         /* full path when renaming */
        std::string message_text;           /* for state_message */
    };
    Data_t _data;

    void _refresh_list();
    void _draw_list();
    void _draw_detail(const FileEntry& e);
    void _draw_input_prompt(const char* prompt);
    void _draw_title(const char* title);
    static void _format_size(const FileEntry& e, char* out, size_t out_size);
    bool _path_full(std::string& out, const std::string& rel) const;
    bool _path_join(std::string& out, const std::string& rel, const std::string& name) const;
    bool _do_mkdir(const char* path);
    bool _do_copy(const char* src, const char* dst);
    bool _do_rename(const char* old_path, const char* new_path);
    bool _do_delete(const char* path);
    void _message(const char* msg);

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppFilesManager_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "File M"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_filemanager_big, image_data_filemanager_small)); }
    void* newApp() override { return new AppFilesManager; }
    void deleteApp(void* app) override { delete (AppFilesManager*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
