/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>

// RS Encoding Configuration
// Using true Reed-Solomon error correction with 50% data loss tolerance
inline constexpr size_t RS_LENGTH_FIELD_BYTES = 2;
inline constexpr const char* RS_MODE_NAME     = "RS True Correction";

/**
 * @brief Reed-Solomon encoder for StringIR payload delivery.
 *
 * Provides true Reed-Solomon error correction encoding for sharing text scenes
 * over IR with StringIR-compatible receivers. Uses adaptive coding with
 * configurable payload buckets.
 */
class RSEncoder {
public:
    struct Profile {
        uint16_t codeword_length;
        uint16_t data_bytes;
        uint16_t parity_bytes;
        uint8_t maxErrors;
        const char* name;
        uint16_t codec_n;
        uint16_t codec_k;

        size_t maxPayload() const
        {
            return static_cast<size_t>(data_bytes) - RS_LENGTH_FIELD_BYTES;
        }
        bool usesExtendedParity() const
        {
            return codeword_length > codec_n;
        }
    };

    static const Profile& selectProfile(size_t payload_length);
    static const Profile* findProfileByCodewordLength(size_t codeword_length);
    static size_t maxPayloadAcrossProfiles();

    /**
     * @brief Encode a string with Reed-Solomon error correction
     *
     * @param input Input string to encode
     * @param output Vector to store encoded bytes
     * @return true if encoding successful, false otherwise
     */
    static bool encode(const std::string& input, std::vector<uint8_t>& output);
};
