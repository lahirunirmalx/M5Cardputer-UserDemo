/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rs_decoder.h"
#include "rs_true_codec.h"
#include "esp_log.h"

static const char* TAG = "RSDecoder";

bool RSDecoder::decodeToBytes(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    if (input.empty()) {
        ESP_LOGE(TAG, "input is empty");
        return false;
    }

    // Use true Reed-Solomon codec
    const auto* profile = RSEncoder::findProfileByCodewordLength(input.size());
    if (profile == nullptr) {
        ESP_LOGE(TAG, "%s invalid codeword size: %zu", RS_MODE_NAME, input.size());
        return false;
    }
    const char* mode_name = profile->name;

    std::vector<uint8_t> working_codeword(input.begin(), input.end());
    if (profile->usesExtendedParity()) {
        if (working_codeword.size() != profile->codeword_length) {
            ESP_LOGE(TAG, "%s received %zu bytes but expected %u", mode_name, working_codeword.size(),
                     profile->codeword_length);
            return false;
        }

        uint8_t parity_check = 0;
        for (uint8_t byte : working_codeword) {
            parity_check ^= byte;
        }
        if (parity_check != 0) {
            ESP_LOGW(TAG, "%s overall parity mismatch (xor=0x%02X)", mode_name, parity_check);
        }

        working_codeword.resize(profile->codec_n);
    }

    ReedSolomonCodec codec(profile->codec_n, profile->codec_k);
    int errors_corrected = 0;
    std::vector<uint8_t> decoded_block;

    if (!codec.decode(working_codeword, decoded_block, &errors_corrected)) {
        ESP_LOGE(TAG, "%s decoding failed (too many errors)", mode_name);
        return false;
    }

    if (decoded_block.size() != static_cast<size_t>(profile->codec_k)) {
        ESP_LOGE(TAG, "%s decoded block size mismatch: %zu (expected %u)", mode_name, decoded_block.size(),
                 profile->codec_k);
        return false;
    }

    if (decoded_block.size() < RS_LENGTH_FIELD_BYTES) {
        ESP_LOGE(TAG, "%s decoded block missing length header", mode_name);
        return false;
    }

    const uint16_t payload_len = static_cast<uint16_t>(decoded_block[0] << 8) | static_cast<uint16_t>(decoded_block[1]);

    if (payload_len > profile->maxPayload()) {
        ESP_LOGE(TAG, "%s decoded invalid payload length %u (max %zu)", mode_name, payload_len, profile->maxPayload());
        return false;
    }

    if (payload_len > decoded_block.size() - RS_LENGTH_FIELD_BYTES) {
        ESP_LOGE(TAG, "%s decoded payload length %u exceeds block capacity %zu", mode_name, payload_len,
                 decoded_block.size() - RS_LENGTH_FIELD_BYTES);
        return false;
    }

    output.assign(decoded_block.begin() + RS_LENGTH_FIELD_BYTES,
                  decoded_block.begin() + RS_LENGTH_FIELD_BYTES + payload_len);

    if (errors_corrected > 0) {
        ESP_LOGW(TAG, "%s decoded with %d errors corrected", mode_name, errors_corrected);
    } else {
        ESP_LOGI(TAG, "%s decoded successfully (no errors), payload %u bytes", mode_name, payload_len);
    }

    return true;
}

bool RSDecoder::decode(const std::vector<uint8_t>& input, std::string& output)
{
    std::vector<uint8_t> buffer;
    if (!decodeToBytes(input, buffer)) {
        return false;
    }

    const size_t max_payload = RSEncoder::maxPayloadAcrossProfiles();
    if (buffer.size() > max_payload) {
        ESP_LOGE(TAG, "%s decoded data too large (%zu bytes, max %zu)", RS_MODE_NAME, buffer.size(), max_payload);
        return false;
    }

    output.assign(buffer.begin(), buffer.end());
    return true;
}
