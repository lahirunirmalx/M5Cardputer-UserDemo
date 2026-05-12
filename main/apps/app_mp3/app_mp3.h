/**
 * @file app_mp3.h
 * @brief WinAmp-style MP3 player (port of VolosR/M5Mp3). SD card MP3 list, play/pause/next/prev/vol.
 */
#pragma once
#include <mooncake.h>
#include <string>
#include <vector>
#include <AudioOutput.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceBuffer.h>

#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "AudioFileSourcePath.h"
#include "assets/mp3_big.h"
#include "assets/mp3_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AudioOutputM5Speaker : public AudioOutput
{
public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t ch = 0) : _m5sound(m5sound), _virtual_ch(ch) {}
    virtual bool begin(void) override { return true; }
    virtual bool ConsumeSample(int16_t sample[2]) override;
    virtual void flush(void) override;
    virtual bool stop(void) override;
    m5::Speaker_Class* _m5sound;
private:
    uint8_t _virtual_ch;
    static constexpr size_t tri_buf_size = 1280;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
};

class AppMp3 : public APP_BASE
{
    struct Data_t {
        HAL::Hal* hal = nullptr;
        std::vector<std::string> files;
        size_t current_index = 0;
        int scroll_offset = 0;
        size_t last_key_num = 0;
        int volume = 10;
        bool playing = false;
        bool stopped = true;
        bool next_pending = false;
        uint32_t play_start_ms = 0;
        uint32_t last_redraw_ms = 0;
        void* preallocate_buffer = nullptr;
        void* preallocate_codec = nullptr;
        AudioFileSourcePath* file_src = nullptr;
        AudioFileSourceBuffer* buff = nullptr;
        AudioGeneratorMP3* decoder = nullptr;
        AudioOutputM5Speaker* output = nullptr;
    };
    Data_t _data;

    void _list_mp3();
    void _play_current();
    void _stop();
    void _draw();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppMp3_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "MP3"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_mp3_big, image_data_mp3_small)); }
    void* newApp() override { return new AppMp3; }
    void deleteApp(void* app) override { delete (AppMp3*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
