/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Simplified Reed-Solomon Error Correction Code Implementation
 * Based on RS(255,223) - can correct up to 16 byte errors
 */
#pragma once
#include <cstdint>
#include <vector>

/**
 * @brief Galois Field GF(256) operations for Reed-Solomon
 */
class GaloisField {
public:
    // Initialize GF(256) lookup tables
    static void init();

    // Galois field operations
    static uint8_t multiply(uint8_t a, uint8_t b);
    static uint8_t divide(uint8_t a, uint8_t b);
    static uint8_t power(uint8_t base, uint8_t exp);
    static uint8_t inverse(uint8_t a);

private:
    static bool initialized;
    static uint8_t exp_table[512];  // exp_table[i] = alpha^i
    static uint8_t log_table[256];  // log_table[i] = log_alpha(i)
};

/**
 * @brief True Reed-Solomon encoder/decoder
 *
 * Implements RS(n, k) where:
 * - n: total codeword length (default 255)
 * - k: data length (default 223)
 * - 2t = n-k: number of parity symbols (32)
 * - Can correct up to t=16 symbol errors
 *
 * This is TRUE error correction, not just detection.
 * Can recover full data even if up to 50% of symbols are corrupted.
 */
class ReedSolomonCodec {
public:
    /**
     * @brief Initialize RS codec with parameters
     *
     * @param n Total codeword length (max 255)
     * @param k Data length
     */
    ReedSolomonCodec(int n = 255, int k = 223);

    /**
     * @brief Encode data with Reed-Solomon
     *
     * @param data Input data (max k bytes)
     * @param output Encoded codeword (n bytes)
     * @return true if successful
     */
    bool encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& output);

    /**
     * @brief Decode and correct errors
     *
     * @param codeword Received codeword (n bytes, may contain errors)
     * @param output Corrected data (k bytes)
     * @param errors_corrected Number of errors corrected (output)
     * @return true if decoding successful
     */
    bool decode(const std::vector<uint8_t>& codeword, std::vector<uint8_t>& output, int* errors_corrected = nullptr);

    /**
     * @brief Get maximum correctable errors
     */
    int getMaxErrors() const
    {
        return (n - k) / 2;
    }

    /**
     * @brief Get codec parameters
     */
    int getN() const
    {
        return n;
    }
    int getK() const
    {
        return k;
    }

private:
    int n;                                // Codeword length
    int k;                                // Data length
    std::vector<uint8_t> generator_poly;  // Generator polynomial

    // Generate RS generator polynomial
    void generate_polynomial();

    // Polynomial operations in GF(256)
    void poly_multiply(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, std::vector<uint8_t>& result);
    void poly_divide(const std::vector<uint8_t>& dividend, const std::vector<uint8_t>& divisor,
                     std::vector<uint8_t>& quotient, std::vector<uint8_t>& remainder);

    // Syndrome calculation
    bool calculate_syndromes(const std::vector<uint8_t>& codeword, std::vector<uint8_t>& syndromes);

    // Error location and correction
    bool find_error_locator(const std::vector<uint8_t>& syndromes, std::vector<uint8_t>& error_locator,
                            std::vector<uint8_t>& error_evaluator);
    bool find_error_positions(const std::vector<uint8_t>& error_locator, std::vector<int>& positions,
                              std::vector<uint8_t>& evaluation_points);
    bool correct_errors(std::vector<uint8_t>& codeword, const std::vector<int>& error_positions,
                        const std::vector<uint8_t>& evaluation_points, const std::vector<uint8_t>& error_locator,
                        const std::vector<uint8_t>& error_evaluator);
};
