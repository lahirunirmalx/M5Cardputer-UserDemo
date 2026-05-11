/**
 * @file codes.h
 * @brief Power-off IR codes for common TV brands + Midea AC codes.
 *
 * Each entry is a brand label + protocol type + protocol-specific bits.
 * Protocols implemented in app_tvbgone.cpp:
 *   - PROTO_NEC      : standard 32-bit NEC (8-bit addr LSB-first + ~addr + 8-bit cmd + ~cmd)
 *   - PROTO_NEC_EXT  : extended NEC (16-bit address sent as-is, no complement)
 *   - PROTO_RC5      : 14-bit RC5 (Philips)
 *   - PROTO_SIRC12   : Sony 12-bit
 *   - PROTO_SIRC15   : Sony 15-bit
 *   - PROTO_SIRC20   : Sony 20-bit
 *   - PROTO_MIDEA    : 48-bit Midea AC frame (data + complement)
 *
 * The TV codes here are typical power-toggle codes for each brand; not every
 * model will respond, which is why TV-B-Gone fires the whole list.
 */
#pragma once
#include <stdint.h>

typedef enum {
    PROTO_NEC = 0,
    PROTO_NEC_EXT,
    PROTO_RC5,
    PROTO_SIRC12,
    PROTO_SIRC15,
    PROTO_SIRC20,
    PROTO_MIDEA,
} ir_proto_t;

typedef struct {
    const char* label;
    ir_proto_t  proto;
    uint32_t    data_hi;   /* upper bits / Midea bytes 0-3 */
    uint32_t    data_lo;   /* lower bits / Midea bytes 4-5 (low 16 bits used) */
} tvbgone_code_t;

/* Indexed list. Mostly TV power; the last block is Midea AC functions. */
static const tvbgone_code_t TVBGONE_CODES[] = {
    /* TV brands - NEC */
    { "Samsung",      PROTO_NEC,     0x07,        0x02       },
    { "Samsung 2",    PROTO_NEC_EXT, 0xE0E0,      0x40BF     },
    { "LG",           PROTO_NEC,     0x04,        0x08       },
    { "LG 2",         PROTO_NEC_EXT, 0x20DF,      0x10EF     },
    { "Panasonic",    PROTO_NEC_EXT, 0x4004,      0x100BCBD  },
    { "Toshiba",      PROTO_NEC,     0x40,        0x12       },
    { "Sharp",        PROTO_NEC,     0xC8,        0x22       },
    { "Vizio",        PROTO_NEC_EXT, 0x20DF,      0x10EF     },
    { "JVC",          PROTO_NEC_EXT, 0x03,        0x40       },
    { "Hitachi",      PROTO_NEC_EXT, 0x40BF,      0x12ED     },
    { "RCA",          PROTO_NEC,     0x10,        0xAF       },
    { "Insignia",     PROTO_NEC_EXT, 0xF807,      0xFE01     },
    { "Element",      PROTO_NEC,     0x0A,        0xF0       },
    { "Sceptre",      PROTO_NEC,     0x9F,        0x08       },
    { "Sanyo",        PROTO_NEC,     0xB4,        0x4B       },
    { "Pioneer",      PROTO_NEC,     0xA5,        0x5A       },
    { "Mitsubishi",   PROTO_NEC,     0xE0,        0x40       },
    { "Emerson",      PROTO_NEC,     0xA8,        0x58       },
    { "Memorex",      PROTO_NEC,     0x86,        0xE0       },
    { "Magnavox",     PROTO_RC5,     0x00,        0x0C       },

    /* Philips and other RC5 */
    { "Philips",      PROTO_RC5,     0x00,        0x0C       },

    /* Sony */
    { "Sony 12",      PROTO_SIRC12,  0x00,        0xA90      },
    { "Sony 15",      PROTO_SIRC15,  0x00,        0x2A50     },
    { "Sony 20",      PROTO_SIRC20,  0x00,        0xA90AB    },

    /* Midea AC - codes provided by user (data is 48 bits = 6 bytes) */
    { "Midea OFF",    PROTO_MIDEA,   0xA20DFFFF,  0xFF70     },
    { "Midea Heat",   PROTO_MIDEA,   0xA20FFFFF,  0xFF73     },
    { "Midea Silent", PROTO_MIDEA,   0xA212FFFF,  0xFF6E     },
    { "Midea Silent-",PROTO_MIDEA,   0xA213FFFF,  0xFF6F     },
};
static const int TVBGONE_CODE_COUNT = (int)(sizeof(TVBGONE_CODES) / sizeof(TVBGONE_CODES[0]));
