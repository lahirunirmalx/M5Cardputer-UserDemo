/**
 * AudioFileSourcePath - read from path using fopen (VFS /sdcard)
 * For use with ESP8266Audio when SD is mounted via esp_vfs_fat.
 */
#ifndef AUDIO_FILE_SOURCE_PATH_H
#define AUDIO_FILE_SOURCE_PATH_H

#include "AudioFileSource.h"
#include <cstdio>

class AudioFileSourcePath : public AudioFileSource
{
public:
    AudioFileSourcePath() : f(nullptr) {}
    explicit AudioFileSourcePath(const char* filename);
    virtual ~AudioFileSourcePath() override;

    virtual bool open(const char* filename) override;
    virtual uint32_t read(void* data, uint32_t len) override;
    virtual bool seek(int32_t pos, int dir) override;
    virtual bool close() override;
    virtual bool isOpen() override;
    virtual uint32_t getSize() override;
    virtual uint32_t getPos() override;

private:
    FILE* f;
};

#endif
