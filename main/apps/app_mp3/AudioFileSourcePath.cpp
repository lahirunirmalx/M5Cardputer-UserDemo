/**
 * AudioFileSourcePath - stdio implementation for VFS paths
 */
#include "AudioFileSourcePath.h"
#include <cstring>

AudioFileSourcePath::AudioFileSourcePath(const char* filename) : f(nullptr)
{
    open(filename);
}

AudioFileSourcePath::~AudioFileSourcePath()
{
    close();
}

bool AudioFileSourcePath::open(const char* filename)
{
    if (f) fclose(f);
    f = fopen(filename, "rb");
    return f != nullptr;
}

uint32_t AudioFileSourcePath::read(void* data, uint32_t len)
{
    if (!f) return 0;
    return (uint32_t)fread(data, 1, len, f);
}

bool AudioFileSourcePath::seek(int32_t pos, int dir)
{
    if (!f) return false;
    int whence = (dir == SEEK_SET) ? SEEK_SET : ((dir == SEEK_CUR) ? SEEK_CUR : SEEK_END);
    return fseek(f, (long)pos, whence) == 0;
}

bool AudioFileSourcePath::close()
{
    if (f) {
        fclose(f);
        f = nullptr;
    }
    return true;
}

bool AudioFileSourcePath::isOpen()
{
    return f != nullptr;
}

uint32_t AudioFileSourcePath::getSize()
{
    if (!f) return 0;
    long p = ftell(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, p, SEEK_SET);
    return (uint32_t)len;
}

uint32_t AudioFileSourcePath::getPos()
{
    return f ? (uint32_t)ftell(f) : 0;
}
