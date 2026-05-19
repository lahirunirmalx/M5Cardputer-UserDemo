/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Include encoder header to get RS mode configuration
#include "rs_encoder.h"

/**
 * @brief Reed-Solomon decoder counterpart for the StringIR encoder.
 *
 * Performs full Reed-Solomon error correction with automatic error detection
 * and correction capabilities.
 */
class RSDecoder {
public:
    /**
     * @brief Decode Reed-Solomon encoded payload back into a byte vector.
     *
     * @param input Encoded bytes with Reed-Solomon error correction.
     * @param output Vector populated with decoded payload bytes.
     * @return true if decoding successful, false if too many errors to correct.
     */
    static bool decodeToBytes(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

    /**
     * @brief Decode Reed-Solomon encoded payload and return it as a std::string.
     *
     * @param input Encoded bytes with Reed-Solomon error correction.
     * @param output String populated with decoded payload.
     * @return true if decoding successful, false if too many errors to correct.
     */
    static bool decode(const std::vector<uint8_t>& input, std::string& output);

    /**
     * @brief Get the current RS mode name for logging/debugging.
     *
     * @return const char* RS mode name
     */
    static const char* getModeName()
    {
        return RS_MODE_NAME;
    }

    /**
     * @brief Get the maximum data size for current RS mode.
     *
     * @return size_t Maximum data bytes that can be decoded
     */
    static size_t getMaxDataSize()
    {
        return RSEncoder::maxPayloadAcrossProfiles();
    }
};
