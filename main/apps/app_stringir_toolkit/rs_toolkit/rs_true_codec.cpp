/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rs_true_codec.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "RSCodec";

namespace {

// Evaluate polynomial with coefficients in ascending order at point x in GF(256)
uint8_t evaluate_poly(const std::vector<uint8_t>& poly, uint8_t x)
{
    uint8_t result = 0;
    uint8_t term   = 1;
    for (uint8_t coef : poly) {
        if (coef != 0) {
            result ^= GaloisField::multiply(coef, term);
        }
        term = GaloisField::multiply(term, x);
    }
    return result;
}

void trim_trailing_zeros(std::vector<uint8_t>& poly)
{
    while (poly.size() > 1 && poly.back() == 0) {
        poly.pop_back();
    }
}

}  // namespace

// GF(256) primitive polynomial: x^8 + x^4 + x^3 + x^2 + 1 (0x11D)
#define GF_POLY 0x11D

// Static members
bool GaloisField::initialized = false;
uint8_t GaloisField::exp_table[512];
uint8_t GaloisField::log_table[256];

void GaloisField::init()
{
    if (initialized) return;

    // Generate exp and log tables for GF(256)
    uint16_t x = 1;
    for (int i = 0; i < 255; i++) {
        exp_table[i] = (uint8_t)x;
        log_table[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) {
            x ^= GF_POLY;
        }
    }

    // Extend exp table for easy lookups
    for (int i = 255; i < 512; i++) {
        exp_table[i] = exp_table[i - 255];
    }

    log_table[0] = 0;  // Undefined, but set to 0
    initialized  = true;

    ESP_LOGI(TAG, "Galois Field GF(256) initialized");
}

uint8_t GaloisField::multiply(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return exp_table[log_table[a] + log_table[b]];
}

uint8_t GaloisField::divide(uint8_t a, uint8_t b)
{
    if (a == 0) return 0;
    if (b == 0) {
        ESP_LOGE(TAG, "Division by zero in GF");
        return 0;
    }
    return exp_table[(log_table[a] + 255 - log_table[b]) % 255];
}

uint8_t GaloisField::power(uint8_t base, uint8_t exp)
{
    if (base == 0) return 0;
    return exp_table[(log_table[base] * exp) % 255];
}

uint8_t GaloisField::inverse(uint8_t a)
{
    if (a == 0) return 0;
    return exp_table[255 - log_table[a]];
}

// Reed-Solomon Codec implementation
ReedSolomonCodec::ReedSolomonCodec(int n, int k) : n(n), k(k)
{
    GaloisField::init();
    generate_polynomial();
    ESP_LOGI(TAG, "RS(%d,%d) codec initialized, can correct up to %d errors", n, k, getMaxErrors());
}

void ReedSolomonCodec::generate_polynomial()
{
    // Generator polynomial: g(x) = (x-α^0)(x-α^1)...(x-α^(n-k-1))
    generator_poly.clear();
    generator_poly.push_back(1);  // Start with g(x) = 1

    for (int i = 0; i < (n - k); i++) {
        // Multiply by (x - α^i)
        std::vector<uint8_t> term = {1, GaloisField::power(2, i)};
        std::vector<uint8_t> result;
        poly_multiply(generator_poly, term, result);
        generator_poly = result;
    }

    ESP_LOGD(TAG, "Generator polynomial has %d coefficients", generator_poly.size());
}

void ReedSolomonCodec::poly_multiply(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b,
                                     std::vector<uint8_t>& result)
{
    result.assign(a.size() + b.size() - 1, 0);
    for (size_t i = 0; i < a.size(); i++) {
        for (size_t j = 0; j < b.size(); j++) {
            result[i + j] ^= GaloisField::multiply(a[i], b[j]);
        }
    }
}

bool ReedSolomonCodec::encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& output)
{
    if (data.size() > (size_t)k) {
        ESP_LOGE(TAG, "Data too large: %d bytes (max %d)", data.size(), k);
        return false;
    }

    // Pad data to k bytes if needed
    std::vector<uint8_t> padded_data = data;
    while (padded_data.size() < (size_t)k) {
        padded_data.push_back(0);
    }

    // Calculate parity: divide data*x^(n-k) by generator polynomial
    std::vector<uint8_t> dividend = padded_data;
    dividend.resize(n, 0);  // Shift by n-k positions

    // Polynomial division to get remainder (parity)
    for (int i = 0; i < k; i++) {
        uint8_t coef = dividend[i];
        if (coef != 0) {
            for (size_t j = 0; j < generator_poly.size(); j++) {
                dividend[i + j] ^= GaloisField::multiply(generator_poly[j], coef);
            }
        }
    }

    // Codeword = data + parity
    output = padded_data;
    for (int i = k; i < n; i++) {
        output.push_back(dividend[i]);
    }

    ESP_LOGD(TAG, "Encoded %d bytes to %d bytes", data.size(), output.size());
    return true;
}

bool ReedSolomonCodec::calculate_syndromes(const std::vector<uint8_t>& codeword, std::vector<uint8_t>& syndromes)
{
    syndromes.clear();
    syndromes.resize(n - k, 0);

    bool has_errors = false;
    for (int i = 0; i < (n - k); i++) {
        uint8_t syndrome = 0;
        uint8_t alpha_i  = GaloisField::power(2, i);
        uint8_t x        = 1;

        for (int j = static_cast<int>(codeword.size()) - 1; j >= 0; --j) {
            syndrome ^= GaloisField::multiply(codeword[j], x);
            x = GaloisField::multiply(x, alpha_i);
        }

        syndromes[i] = syndrome;
        if (syndrome != 0) has_errors = true;
    }

    return has_errors;
}

bool ReedSolomonCodec::decode(const std::vector<uint8_t>& codeword, std::vector<uint8_t>& output, int* errors_corrected)
{
    if (codeword.size() != (size_t)n) {
        ESP_LOGE(TAG, "Invalid codeword size: %d (expected %d)", codeword.size(), n);
        return false;
    }

    // Calculate syndromes
    std::vector<uint8_t> syndromes;
    bool has_errors = calculate_syndromes(codeword, syndromes);

    if (!has_errors) {
        // No errors detected
        output.assign(codeword.begin(), codeword.begin() + k);
        if (errors_corrected) *errors_corrected = 0;
        ESP_LOGD(TAG, "No errors detected");
        return true;
    }

    // Find error locator polynomial using Berlekamp-Massey algorithm
    std::vector<uint8_t> error_locator;
    std::vector<uint8_t> error_evaluator;
    if (!find_error_locator(syndromes, error_locator, error_evaluator)) {
        ESP_LOGE(TAG, "Failed to find error locator");
        return false;
    }

    // Find error positions
    std::vector<int> error_positions;
    std::vector<uint8_t> evaluation_points;
    if (!find_error_positions(error_locator, error_positions, evaluation_points)) {
        ESP_LOGE(TAG, "Failed to find error positions");
        return false;
    }

    if (error_positions.size() > (size_t)getMaxErrors()) {
        ESP_LOGE(TAG, "Too many errors: %d (max correctable: %d)", error_positions.size(), getMaxErrors());
        return false;
    }

    // Correct errors
    std::vector<uint8_t> corrected = codeword;
    if (!correct_errors(corrected, error_positions, evaluation_points, error_locator, error_evaluator)) {
        ESP_LOGE(TAG, "Failed to correct errors");
        return false;
    }

    std::vector<uint8_t> post_syndromes;
    if (calculate_syndromes(corrected, post_syndromes)) {
        ESP_LOGE(TAG, "Residual syndromes detected after correction");
        return false;
    }

    output.assign(corrected.begin(), corrected.begin() + k);
    if (errors_corrected) *errors_corrected = error_positions.size();

    ESP_LOGI(TAG, "Corrected %d errors", error_positions.size());
    return true;
}

bool ReedSolomonCodec::find_error_locator(const std::vector<uint8_t>& syndromes, std::vector<uint8_t>& error_locator,
                                          std::vector<uint8_t>& error_evaluator)
{
    const int twoT = n - k;
    error_locator.assign(1, 1);
    std::vector<uint8_t> prev_locator(1, 1);
    int L              = 0;
    int m              = 1;
    uint8_t prev_delta = 1;

    for (int i = 0; i < twoT; ++i) {
        uint8_t delta = syndromes[i];
        for (int j = 1; j <= L; ++j) {
            delta ^= GaloisField::multiply(error_locator[j], syndromes[i - j]);
        }

        if (delta == 0) {
            ++m;
            continue;
        }

        if (prev_delta == 0) {
            ESP_LOGE(TAG, "Invalid previous discrepancy value");
            return false;
        }

        std::vector<uint8_t> locator_copy = error_locator;
        uint8_t scale                     = GaloisField::multiply(delta, GaloisField::inverse(prev_delta));
        if (scale == 0) {
            ESP_LOGE(TAG, "Failed to scale locator update");
            return false;
        }

        size_t required_size = prev_locator.size() + static_cast<size_t>(m);
        if (required_size > static_cast<size_t>(twoT) + 1) {
            required_size = static_cast<size_t>(twoT) + 1;
        }
        if (error_locator.size() < required_size) {
            error_locator.resize(required_size, 0);
        }

        for (size_t j = 0; j < prev_locator.size(); ++j) {
            size_t idx = j + static_cast<size_t>(m);
            if (idx >= error_locator.size()) {
                break;
            }
            error_locator[idx] ^= GaloisField::multiply(scale, prev_locator[j]);
        }

        if (2 * L <= i) {
            L            = i + 1 - L;
            prev_locator = locator_copy;
            if (prev_locator.size() > static_cast<size_t>(twoT) + 1) {
                prev_locator.resize(static_cast<size_t>(twoT) + 1);
            }
            prev_delta = delta;
            m          = 1;
        } else {
            ++m;
        }
    }

    trim_trailing_zeros(error_locator);

    if (L > getMaxErrors()) {
        ESP_LOGE(TAG, "Error locator degree %d exceeds capability %d", L, getMaxErrors());
        return false;
    }

    std::vector<uint8_t> product(error_locator.size() + syndromes.size() - 1, 0);
    for (size_t i = 0; i < error_locator.size(); ++i) {
        if (error_locator[i] == 0) continue;
        for (size_t j = 0; j < syndromes.size(); ++j) {
            if (syndromes[j] == 0) continue;
            product[i + j] ^= GaloisField::multiply(error_locator[i], syndromes[j]);
        }
    }

    if (product.size() < static_cast<size_t>(twoT)) {
        product.resize(twoT, 0);
    }

    error_evaluator.assign(product.begin(), product.begin() + twoT);
    trim_trailing_zeros(error_evaluator);

    ESP_LOGD(TAG, "Error locator degree %d", L);
    return true;
}

bool ReedSolomonCodec::find_error_positions(const std::vector<uint8_t>& error_locator, std::vector<int>& positions,
                                            std::vector<uint8_t>& evaluation_points)
{
    positions.clear();
    evaluation_points.clear();

    const int degree = static_cast<int>(error_locator.size()) - 1;
    if (degree <= 0) {
        ESP_LOGE(TAG, "Error locator degree %d is invalid", degree);
        return false;
    }

    for (int i = 0; i < n; ++i) {
        uint8_t x = GaloisField::power(2, static_cast<uint8_t>((255 - i) % 255));
        if (evaluate_poly(error_locator, x) == 0) {
            positions.push_back(n - 1 - i);
            evaluation_points.push_back(x);
        }
    }

    if (static_cast<int>(positions.size()) != degree) {
        ESP_LOGE(TAG, "Expected %d error positions but found %zu", degree, positions.size());
        return false;
    }

    ESP_LOGD(TAG, "Located %zu error positions", positions.size());
    return true;
}

bool ReedSolomonCodec::correct_errors(std::vector<uint8_t>& codeword, const std::vector<int>& error_positions,
                                      const std::vector<uint8_t>& evaluation_points,
                                      const std::vector<uint8_t>& error_locator,
                                      const std::vector<uint8_t>& error_evaluator)
{
    const size_t error_count = error_positions.size();
    if (error_count == 0) {
        return true;
    }

    if (error_count != evaluation_points.size()) {
        ESP_LOGE(TAG, "Mismatch between error positions (%zu) and evaluation points (%zu)", error_count,
                 evaluation_points.size());
        return false;
    }

    for (size_t idx = 0; idx < error_count; ++idx) {
        const int position = error_positions[idx];
        if (position < 0 || position >= n) {
            ESP_LOGE(TAG, "Invalid error position %d", position);
            return false;
        }

        const uint8_t x  = evaluation_points[idx];
        const uint8_t Xi = GaloisField::inverse(x);
        if (Xi == 0) {
            ESP_LOGE(TAG, "Invalid evaluation point inverse at index %zu", idx);
            return false;
        }

        const uint8_t numerator = evaluate_poly(error_evaluator, x);

        uint8_t denominator = 0;
        uint8_t x_pow       = 1;
        for (size_t i = 1; i < error_locator.size(); ++i) {
            if (error_locator[i] != 0 && (i & 0x01) == 1) {
                denominator ^= GaloisField::multiply(error_locator[i], x_pow);
            }
            x_pow = GaloisField::multiply(x_pow, x);
        }

        if (denominator == 0) {
            ESP_LOGE(TAG, "Zero denominator while computing error magnitude at position %d", position);
            return false;
        }

        uint8_t magnitude = GaloisField::divide(GaloisField::multiply(numerator, Xi), denominator);
        codeword[position] ^= magnitude;
    }

    ESP_LOGD(TAG, "Applied corrections to %zu positions", error_count);
    return true;
}
