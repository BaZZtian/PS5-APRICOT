// Internal helpers shared by the at9 sources.
// Copyright (C) 2026 BlackBearReloaded (original Python implementation)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace at9 {
namespace internal {

constexpr double PI = 3.141592653589793238462643383279502884;
constexpr int RATE = 48000;
constexpr int SUPERFRAME_SAMPLES = 1024;
constexpr int ENCODER_DELAY = 256;
constexpr uint32_t PS5_SIZE_LIMIT = 2u * 1024u * 1024u;

// numpy's pairwise summation (used by np.sum / np.mean along a contiguous
// axis). Reproducing its order keeps results bit-identical to the reference
// where the inputs are identical.
double pairwise_sum_large(const double *a, std::ptrdiff_t n, std::ptrdiff_t stride);  // n > 128

inline double pairwise_sum(const double *a, std::ptrdiff_t n, std::ptrdiff_t stride = 1) {
    if (n < 8) {
        double res = 0.;
        for (std::ptrdiff_t i = 0; i < n; i++) res += a[i * stride];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (int j = 0; j < 8; j++) r[j] = a[j * stride];
        std::ptrdiff_t i;
        for (i = 8; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; j++) r[j] += a[(i + j) * stride];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i * stride];
        return res;
    }
    return pairwise_sum_large(a, n, stride);
}

// np.add.reduceat segment: first element, then the pairwise sum of the rest.
inline double reduceat_sum(const double *a, std::ptrdiff_t n) {
    return n <= 0 ? 0.0 : a[0] + pairwise_sum(a + 1, n - 1);
}

int frame_bytes_for(int bitrate_kbps);  // throws on unsupported rates
uint32_t header_size(bool loop);
uint64_t file_size(uint64_t sample_count, uint32_t superframe_bytes, bool loop);

} // namespace internal
} // namespace at9
