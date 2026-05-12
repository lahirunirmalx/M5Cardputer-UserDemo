/**
 * @file codes.h
 * @brief Midea AC IR codes (48-bit Midea protocol).
 *
 * These run on top of the standard Shirriff WORLD_IR_CODES.h database which
 * covers TV power-off codes; the Midea codes are sent as a separate protocol
 * by app_tvbgone.cpp.
 */
#pragma once
#include <stdint.h>

typedef struct {
    const char* label;
    uint32_t    hi;   /* upper 32 bits */
    uint32_t    lo;   /* lower 16 bits used (lo[15:0]) */
} midea_code_t;

/* User-supplied Midea AC functions */
static const midea_code_t MIDEA_CODES[] = {
    { "Midea OFF",       0xA20DFFFFu, 0xFF70u },
    { "Midea Heat OFF",  0xA20FFFFFu, 0xFF73u },
    { "Midea Silent ON", 0xA212FFFFu, 0xFF6Eu },
    { "Midea Silent OFF",0xA213FFFFu, 0xFF6Fu },
};
static const int MIDEA_CODE_COUNT = (int)(sizeof(MIDEA_CODES) / sizeof(MIDEA_CODES[0]));
