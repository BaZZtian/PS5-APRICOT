// ATRAC9 encoder (48 kHz stereo/mono) and .at9 RIFF writer.
//
// C++ port of ps5at9/atrac9_encoder.py, ps5at9/atrac9.py (the decoder rules the
// encoder recomputes) and ps5at9/at9file.py from PS5 AT9 Converter v0.1.0
// (https://github.com/blackbearreloaded/ps5-at9-converter).
// Copyright (C) 2026 BlackBearReloaded (original Python implementation)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The encoder recomputes everything an ATRAC9 decoder derives (precisions,
// codebook sets, scale factor predictions) with the decoder's own integer
// rules, so it knows the exact size and quantisation noise of every choice.
// Per 256-sample frame and channel it computes MDCT coefficients, tight scale
// factors and, for every word length, the exact Huffman cost and noise of
// each quantisation unit. Bit allocation searches the decoder's gradient
// parameters, weighting noise by a psychoacoustic masking threshold, and a
// knapsack over the four frames of a superframe spends the fixed superframe
// size where it lowers the weighted noise most. Superframes are independent
// and are encoded in parallel; the output does not depend on the thread count.
//
// Numerical order (sums, products) follows the numpy reference where it
// matters for decisions, so identical input normally gives identical bytes.

#include "at9.h"
#include "at9_internal.h"
#include "at9_tables.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>

namespace at9 {
namespace internal {

int frame_bytes_for(int bitrate_kbps) {
    long long num = (long long)bitrate_kbps * 1000 * 256;
    long long value = num / (8 * 48000);
    if (bitrate_kbps <= 0 || value * 8 * 48000 != num)
        throw std::runtime_error("unsupported bit rate " + std::to_string(bitrate_kbps) + " kb/s");
    return (int)value;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
double pairwise_sum_large(const double *a, std::ptrdiff_t n, std::ptrdiff_t stride) {
    std::ptrdiff_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum(a, n2, stride) + pairwise_sum(a + n2 * stride, n - n2, stride);
}

uint32_t header_size(bool loop) { return 12 + 60 + 20 + (loop ? 68 : 0) + 8; }

uint64_t file_size(uint64_t sample_count, uint32_t superframe_bytes, bool loop) {
    uint64_t superframes = (sample_count + ENCODER_DELAY + SUPERFRAME_SAMPLES - 1) / SUPERFRAME_SAMPLES;
    return header_size(loop) + superframes * superframe_bytes;
}

} // namespace internal

namespace {

using namespace internal;
namespace T = tables;

constexpr int N = 256;
constexpr int UNITS = 30;
constexpr int FPS = 4;  // frames per superframe
constexpr double INF = std::numeric_limits<double>::infinity();

const int BAND_UNITS[19] = {0, 4, 8, 10, 12, 13, 14, 15, 16, 18, 20, 21, 22, 23, 24, 25, 26, 28, 30};
const int QCNT[30] = {2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8,
                      16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
const int QIDX[31] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56,
                      64, 72, 80, 88, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 256};
const int CBI[30] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
                     3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

const int STEREO_BITRATES[] = {72, 96, 120, 144, 168, 192};
const int MONO_BITRATES[] = {36, 48, 60, 72, 84, 96, 120, 144};

constexpr int D_MIN = -40, D_MAX = 50, D_SPAN = D_MAX - D_MIN + 1;
constexpr int P_CAP = 17;

struct Overflow {};  // the superframe does not fit at the requested bandwidth

// ---------------------------------------------------------------- decoder rules

void gradient_curve(int mode, int start_unit, int end_unit, int start_value, int end_value,
                    int unit_count, int grad[31]) {
    (void)mode;
    for (int i = 0; i < 31; i++) grad[i] = start_value;
    for (int i = end_unit; i <= std::min(unit_count, 30); i++) grad[i] = end_value;
    int units = end_unit - start_unit;
    int values = end_value - start_value;
    if (units > 0 && values != 0) {
        // GRADIENT_CURVES[units - 1][i] = GRADIENT_MAIN[i * 48 // units]
        if (values < 0) {
            double scale = (-values - 1) / 31.0;
            for (int i = start_unit; i < end_unit; i++) {
                int c = T::GRADIENT_MAIN[(i - start_unit) * 48 / units];
                grad[i] = start_value - 1 - (int)(c * scale);
            }
        } else {
            double scale = (values - 1) / 31.0;
            for (int i = start_unit; i < end_unit; i++) {
                int c = T::GRADIENT_MAIN[(i - start_unit) * 48 / units];
                grad[i] = start_value + 1 + (int)(c * scale);
            }
        }
    }
}

void precision_mask(const int *sf, int unit_count, int mask[30]) {
    for (int i = 0; i < 30; i++) mask[i] = 0;
    for (int i = 1; i < unit_count; i++) {
        int delta = sf[i] - sf[i - 1];
        if (delta > 1)
            mask[i] += std::min(delta - 1, 5);
        else if (delta < -1)
            mask[i - 1] += std::min(-delta - 1, 5);
    }
}

int floordiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

void precisions(const int *sf, const int *grad, int mode, int boundary, int unit_count, int coarse[30],
                int fine[30]) {
    for (int i = 0; i < 30; i++) coarse[i] = 0;
    if (mode) {
        int mask[30];
        precision_mask(sf, unit_count, mask);
        for (int i = 0; i < unit_count; i++) {
            int p = sf[i] + mask[i] - grad[i];
            if (p > 0) p = mode == 1 ? p / 2 : mode == 2 ? (3 * p) / 8 : p / 4;
            coarse[i] = p;
        }
    } else {
        for (int i = 0; i < unit_count; i++) coarse[i] = sf[i] - grad[i];
    }
    for (int i = 0; i < 30; i++) fine[i] = 0;
    for (int i = 0; i < unit_count; i++) {
        if (coarse[i] < 1) coarse[i] = 1;
        if (i < boundary) coarse[i] += 1;
        if (coarse[i] > 15) {
            fine[i] = coarse[i] - 15;
            coarse[i] = 15;
        }
    }
}

void codebook_sets(const int *sf, int coded_units, int sets[30]) {
    for (int i = 0; i < 30; i++) sets[i] = 0;
    if (coded_units <= 1) return;
    int s[31];
    for (int i = 0; i < coded_units; i++) s[i] = sf[i];
    s[coded_units] = sf[coded_units - 1];
    int avg = 0;
    if (coded_units > 12) {
        int sum = 0;
        for (int i = 0; i < 12; i++) sum += s[i];
        avg = floordiv(sum + 6, 12);
    }
    for (int i = 8; i < coded_units; i++) {
        int lo = std::min(s[i - 1], s[i + 1]);
        if (s[i] - lo >= 3 || 2 * s[i] - s[i - 1] - s[i + 1] >= 3) sets[i] = 1;
    }
    for (int i = 12; i < coded_units; i++) {
        if (!sets[i]) {
            int lo = std::min(s[i - 1], s[i + 1]);
            if (s[i] - lo >= 2 && s[i] >= avg - (QCNT[i] == 16 ? 1 : 0)) sets[i] = 1;
        }
    }
}

// ---------------------------------------------------------------- static data

struct SpectrumBooks {
    int wl = 0, mask = 0, size = 0;
    std::vector<int> bits, codes;  // [set][cbi][size]
    std::vector<std::array<int, 4>> idx;
    std::vector<int> unit;
    int group_start[UNITS + 1] = {};
    int bit(int s, int c, int sym) const { return bits[((size_t)s * 4 + c) * size + sym]; }
    int code(int s, int c, int sym) const { return codes[((size_t)s * 4 + c) * size + sym]; }
};

struct Candidates {
    int units = 0;
    int count = 0;
    std::vector<std::array<int, 5>> params;  // mode, start unit, end unit, start value, end value
    std::vector<uint8_t> mode0;
    std::vector<int> grad;    // [count][units]
    std::vector<int> offset;  // [count][units]
    std::vector<int> header_bits;
};

struct Psycho {
    double spread[UNITS][UNITS];  // [maskee][masker]
    double z[UNITS];
    double ath[UNITS];
    int bin_lo[UNITS], bin_hi[UNITS];
};

struct Static {
    SpectrumBooks books[8];  // index by word length 2..7
    int p_of_d[4][D_SPAN];
    int p1_of_d[4][D_SPAN];
    std::map<int, Candidates> cands;  // by coded units
    Psycho psycho;
    std::vector<double> basis_t;  // [k][n] (n = 0..511)
    double hann[2 * N];
    double tw_re[N], tw_im[N];    // FFT twiddles for size 512
    double step[16], inv_step[16];
};

double bark(double f) {
    return 13.0 * std::atan(0.00076 * f) + 3.5 * std::atan((f / 7500.0) * (f / 7500.0));
}

double ath_db(double f) {
    double k = std::max(f, 20.0) / 1000.0;
    double t = k - 3.3;
    return 3.64 * std::pow(k, -0.8) - 6.5 * std::exp(-0.6 * (t * t)) + 1e-3 * std::pow(k, 4.0);
}

int mode_precision(int mode, int d) {
    if (d > 0) {
        if (mode == 1)
            d = d / 2;
        else if (mode == 2)
            d = (3 * d) / 8;
        else if (mode == 3)
            d = d / 4;
    }
    return std::min(std::max(d, 1), P_CAP);
}

Candidates make_candidates(int units) {
    Candidates c;
    c.units = units;
    std::set<std::tuple<int, int, int>> keys;
    for (int m = 1; m <= 3; m++)
        for (int s = std::max(1, units - 12); s <= std::min(30, units + 2); s++)
            for (int a = 0; a < 27; a++) keys.insert({m, s, a});
    for (int a = 0; a < 31; a++) {
        keys.insert({1, 1, a});
        for (int s : {std::max(1, units - 8), std::max(1, units - 4), std::min(30, units)}) keys.insert({0, s, a});
    }
    c.count = (int)keys.size();
    c.grad.resize((size_t)c.count * units);
    c.offset.resize((size_t)c.count * units);
    int k = 0;
    for (const auto &key : keys) {
        int m = std::get<0>(key), s = std::get<1>(key), a = std::get<2>(key);
        c.params.push_back({m, s, 31, a, 31});
        c.mode0.push_back(m == 0);
        int grad[31];
        gradient_curve(m, s, 31, a, 31, 30, grad);
        for (int u = 0; u < units; u++) {
            c.grad[(size_t)k * units + u] = grad[u];
            c.offset[(size_t)k * units + u] = (m * units + u) * D_SPAN - D_MIN;
        }
        c.header_bits.push_back(m == 0 ? 2 + 6 + 6 + 5 + 5 + 4 : 2 + 5 + 5 + 4);
        k++;
    }
    return c;
}

const Static &statics() {
    static const Static *S = [] {
        Static *s = new Static();
        for (int i = 0; i < 16; i++) {
            s->step[i] = 2.0 / ((1 << (i + 1)) - 1);
            s->inv_step[i] = 1.0 / s->step[i];
        }
        // Spectrum codebooks per word length.
        for (int wl = 2; wl <= 7; wl++) {
            SpectrumBooks &b = s->books[wl];
            b.wl = wl;
            b.mask = (1 << wl) - 1;
            b.size = 1 << (wl == 2 ? 4 * wl : wl <= 4 ? 2 * wl : wl);
            b.bits.assign((size_t)2 * 4 * b.size, 0);
            b.codes.assign((size_t)2 * 4 * b.size, 0);
            int vcp[4] = {0, 0, 0, 0};
            for (int set = 0; set < 2; set++)
                for (int c = 0; c < 4; c++) {
                    const T::Huff &h = T::SPECTRUM[set][wl][c];
                    if (!h.size) continue;
                    for (int i = 0; i < h.size; i++) {
                        b.bits[((size_t)set * 4 + c) * b.size + i] = h.bits[i];
                        b.codes[((size_t)set * 4 + c) * b.size + i] = h.codes[i];
                    }
                    vcp[c] = h.value_count_power;
                }
            for (int u = 0; u < UNITS; u++) {
                int vc = 1 << vcp[CBI[u]];
                for (int g = 0; g < QCNT[u] / vc; g++) {
                    int start = QIDX[u] + g * vc;
                    std::array<int, 4> row;
                    for (int j = 0; j < 4; j++) row[j] = j < vc ? start + j : N;
                    b.idx.push_back(row);
                    b.unit.push_back(u);
                }
            }
            for (int u = 0; u <= UNITS; u++) {
                int g = 0;
                while (g < (int)b.unit.size() && b.unit[g] < u) g++;
                b.group_start[u] = g;
            }
        }
        for (int m = 0; m < 4; m++)
            for (int i = 0; i < D_SPAN; i++) {
                s->p_of_d[m][i] = mode_precision(m, D_MIN + i);
                s->p1_of_d[m][i] = std::min(s->p_of_d[m][i] + 1, P_CAP);
            }
        for (int band = 3; band <= 18; band++) {
            int units = BAND_UNITS[band];
            if (!s->cands.count(units)) s->cands.emplace(units, make_candidates(units));
        }
        // Psychoacoustic model.
        Psycho &p = s->psycho;
        const double bin_hz = 48000 / 2.0 / N;
        double lo[UNITS], hi[UNITS];
        for (int u = 0; u < UNITS; u++) {
            lo[u] = QIDX[u] * bin_hz;
            hi[u] = QIDX[u + 1] * bin_hz;
            p.z[u] = bark((lo[u] + hi[u]) / 2.0);
        }
        for (int a = 0; a < UNITS; a++)
            for (int b = 0; b < UNITS; b++) {
                double dz = p.z[a] - p.z[b];
                double atten = dz < 0 ? 27.0 * -dz : 12.0 * dz;
                p.spread[a][b] = std::pow(10.0, -atten / 10.0);
            }
        for (int u = 0; u < UNITS; u++) {
            double step = (hi[u] - lo[u]) / 8;
            double mn = INF;
            for (int i = 0; i < 9; i++) {
                double f = i == 8 ? hi[u] : i * step + lo[u];
                mn = std::min(mn, ath_db(f));
            }
            double ath = mn - 110.0;
            p.ath[u] = 32768.0 * 32768.0 * std::pow(10.0, ath / 10.0);
            p.bin_lo[u] = std::max(QIDX[u] - 1, 0);
            p.bin_hi[u] = std::min(QIDX[u + 1] + 2, N + 1);
        }
        // MDCT basis with the analysis window, scaled by 2/N.
        double analysis[N];
        for (int n = 0; n < N; n++) analysis[n] = (std::sin(((n + 0.5) / N - 0.5) * PI) + 1.0) * 0.5;
        s->basis_t.resize((size_t)N * 2 * N);
        for (int k = 0; k < N; k++)
            for (int n = 0; n < 2 * N; n++) {
                double w = n < N ? analysis[n] : analysis[2 * N - 1 - n];
                double arg = (PI / N * (k + 0.5)) * ((n + 0.5) + N / 2.0);
                s->basis_t[(size_t)k * 2 * N + n] = (std::cos(arg) * w) * (2.0 / N);
            }
        for (int i = 0; i < 2 * N; i++) {
            double n = 1 - 2 * N + 2 * i;
            s->hann[i] = 0.5 + 0.5 * std::cos(PI * n / (2 * N - 1));
        }
        for (int i = 0; i < N; i++) {
            s->tw_re[i] = std::cos(-2.0 * PI * i / (2 * N));
            s->tw_im[i] = std::sin(-2.0 * PI * i / (2 * N));
        }
        return s;
    }();
    return *S;
}

// ---------------------------------------------------------------- analysis

// Power spectrum |rfft(x)|^2 of 512 real samples (257 bins).
void power_spectrum(const Static &S, const double *x, double *out) {
    const int M = 2 * N;
    double re[M], im[M];
    // bit-reversed copy, then iterative radix-2 butterflies
    for (int i = 0; i < M; i++) {
        int r = 0;
        for (int b = 0; b < 9; b++)
            if (i & (1 << b)) r |= 1 << (8 - b);
        re[r] = x[i];
        im[r] = 0.0;
    }
    for (int len = 2; len <= M; len <<= 1) {
        int half = len >> 1;
        int tstep = M / len;
        for (int i = 0; i < M; i += len)
            for (int j = 0; j < half; j++) {
                double wr = S.tw_re[j * tstep], wi = S.tw_im[j * tstep];
                double ar = re[i + j + half], ai = im[i + j + half];
                double tr = ar * wr - ai * wi, ti = ar * wi + ai * wr;
                re[i + j + half] = re[i + j] - tr;
                im[i + j + half] = im[i + j] - ti;
                re[i + j] += tr;
                im[i + j] += ti;
            }
    }
    for (int k = 0; k <= N; k++) {
        double h = std::hypot(re[k], im[k]);
        out[k] = h * h;
    }
}

struct Analysis {
    int channels = 0, frames = 0;
    std::vector<double> spectra;  // [ch][frame][N]
    std::vector<double> energy;   // [ch][frame][UNITS]
    std::vector<double> masks;    // [ch][frame][UNITS]
    std::vector<int> sfs;         // [ch][frame][UNITS]
    size_t row(int c, int f) const { return (size_t)c * frames + f; }
};

void analyse_frame(const Static &S, const double *block, double *spec, double *energy, double *mask, int *sf) {
    // MDCT (block @ basis)
    // Four outputs at a time for instruction-level parallelism; each output
    // is still summed sequentially over n.
    for (int k = 0; k < N; k += 4) {
        const double *b0 = &S.basis_t[(size_t)k * 2 * N];
        const double *b1 = b0 + 2 * N, *b2 = b1 + 2 * N, *b3 = b2 + 2 * N;
        double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        for (int n = 0; n < 2 * N; n++) {
            const double x = block[n];
            a0 += x * b0[n];
            a1 += x * b1[n];
            a2 += x * b2[n];
            a3 += x * b3[n];
        }
        spec[k] = a0;
        spec[k + 1] = a1;
        spec[k + 2] = a2;
        spec[k + 3] = a3;
    }
    double win[2 * N], power[N + 1];
    for (int n = 0; n < 2 * N; n++) win[n] = block[n] * S.hann[n];
    power_spectrum(S, win, power);
    double sq[N];
    for (int k = 0; k < N; k++) sq[k] = spec[k] * spec[k];
    for (int u = 0; u < UNITS; u++) {
        energy[u] = reduceat_sum(sq + QIDX[u], QCNT[u]);
        double peak = 0.0;
        for (int k = QIDX[u]; k < QIDX[u + 1]; k++) peak = std::max(peak, std::fabs(spec[k]));
        int v = 0;
        if (peak > 0) {
            double l = std::ceil(std::log2(std::max(peak, 1e-300))) + 15;
            v = (int)std::min(std::max(l, 0.0), 31.0);
        }
        sf[u] = v;
    }
    // Masking thresholds.
    const Psycho &P = S.psycho;
    double logp[N + 1];
    for (int k = 0; k <= N; k++) logp[k] = std::log(power[k] + 1e-12);
    const double db = 10.0 / std::log(10.0);
    for (int u = 0; u < UNITS; u++) {
        int a = P.bin_lo[u], b = P.bin_hi[u];
        double gm = pairwise_sum(logp + a, b - a) / (b - a);
        double am = std::log(pairwise_sum(power + a, b - a) / (b - a) + 1e-12);
        double sfm_db = db * (gm - am);
        double tonal = std::min(std::max(sfm_db / -25.0, 0.0), 1.0);
        double offset = tonal * std::min(14.5 + P.z[u], 25.0) + (1.0 - tonal) * 5.5;
        double spread = 0.0;
        for (int m = 0; m < UNITS; m++) spread += energy[m] * P.spread[u][m];
        double masked = spread * std::pow(10.0, -offset / 10.0);
        mask[u] = std::max(masked, P.ath[u]);
    }
}

// ---------------------------------------------------------------- bit writer

struct BitWriter {
    std::vector<uint8_t> out;
    uint64_t acc = 0;
    int count = 0;  // bits in acc
    size_t total = 0;
    void put(int64_t value, int bits) {
        if (!bits) return;
        uint64_t v = (uint64_t)value & ((bits >= 64) ? ~0ull : ((1ull << bits) - 1));
        acc = (acc << bits) | v;
        count += bits;
        total += bits;
        while (count >= 8) {
            out.push_back((uint8_t)(acc >> (count - 8)));
            count -= 8;
        }
        acc &= (count ? ((1ull << count) - 1) : 0);
    }
    std::vector<uint8_t> bytes() {
        std::vector<uint8_t> r = out;
        if (count) r.push_back((uint8_t)(acc << (8 - count)));
        return r;
    }
};

// ---------------------------------------------------------------- scale factor coding

enum SfKind { SF_CLC, SF_DELTA, SF_DISTANCE, SF_OFFSET };

struct SfCoding {
    bool valid = false;
    int bits = 0;
    int mode = 0;
    SfKind kind = SF_CLC;
    int length = 0, base = 0, w = 0, count = 0;
    const int *baseline = nullptr;
};

SfCoding sf_clc(const int *sf, int units) {
    int lo = *std::min_element(sf, sf + units), hi = *std::max_element(sf, sf + units);
    SfCoding c;
    c.valid = true;
    c.mode = 1;
    c.kind = SF_CLC;
    for (int length = 2; length <= 4; length++)
        if (hi - lo < (1 << length)) {
            c.bits = 2 + 2 + 5 + units * length;
            c.length = length;
            c.base = lo;
            return c;
        }
    c.bits = 2 + 2 + units * 5;
    c.length = 5;
    c.base = 0;
    return c;
}

SfCoding sf_delta_offset(const int *sf, int units, int mode) {
    int t[8][30], base[8];
    for (int w = 0; w < 8; w++) {
        int mn = INT32_MAX;
        for (int u = 0; u < units; u++) {
            t[w][u] = sf[u] + T::SF_WEIGHTS[w][u];
            mn = std::min(mn, t[w][u]);
        }
        base[w] = std::min(std::max(mn, 0), 31);
    }
    SfCoding best;
    for (int length = 3; length <= 6; length++) {
        const T::Huff &h = T::SF_UNSIGNED[length];
        int lim = 1 << length, msk = lim - 1;
        bool any_ok = false;
        int bits[8];
        for (int w = 0; w < 8; w++) {
            int mn = INT32_MAX, mx = INT32_MIN;
            for (int u = 0; u < units; u++) {
                int v = t[w][u] - base[w];
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            bool ok = mn >= 0 && mx < lim;
            any_ok |= ok;
            if (!ok) {
                bits[w] = 1 << 30;
                continue;
            }
            int sum = 0;
            for (int u = 1; u < units; u++) sum += h.bits[((t[w][u] - base[w]) - (t[w][u - 1] - base[w])) & msk];
            bits[w] = sum + (2 + 3 + 5 + 2 + length);
        }
        if (!any_ok) continue;
        int w = (int)(std::min_element(bits, bits + 8) - bits);
        if (!best.valid || bits[w] < best.bits) {
            best.valid = true;
            best.bits = bits[w];
            best.mode = mode;
            best.kind = SF_DELTA;
            best.w = w;
            best.base = base[w];
            best.length = length;
        }
    }
    return best;
}

SfCoding sf_distance(const int *sf, int units, const int *baseline, int baseline_len, int mode) {
    int count = std::min(units, baseline_len);
    int lo = 0, hi = 0;
    if (count) {
        lo = INT32_MAX;
        hi = INT32_MIN;
        for (int i = 0; i < count; i++) {
            int d = sf[i] - baseline[i];
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
    }
    SfCoding best;
    for (int length = 2; length <= 5; length++) {
        int half = 1 << (length - 1);
        if (lo < -half || hi >= half) continue;
        const T::Huff &h = T::SF_SIGNED[length];
        int msk = (1 << length) - 1;
        bool ok = true;
        int sum = 0;
        for (int i = 0; i < count; i++) {
            int b = h.bits[(sf[i] - baseline[i]) & msk];
            if (!b) ok = false;
            sum += b;
        }
        if (count && !ok) continue;
        int bits = 2 + 2 + sum + (units - count) * 5;
        if (!best.valid || bits < best.bits) {
            best.valid = true;
            best.bits = bits;
            best.mode = mode;
            best.kind = SF_DISTANCE;
            best.length = length;
            best.baseline = baseline;
            best.count = count;
        }
    }
    return best;
}

SfCoding sf_delta_baseline(const int *sf, int units, const int *baseline, int baseline_len, int mode) {
    SfCoding best;
    int count = std::min(units, baseline_len);
    if (count < 1) return best;
    int r[30];
    int mn = INT32_MAX;
    for (int i = 0; i < count; i++) {
        r[i] = sf[i] - baseline[i];
        mn = std::min(mn, r[i]);
    }
    int base = std::min(std::max(mn, -16), 15);
    int umin = INT32_MAX, umax = INT32_MIN;
    for (int i = 0; i < count; i++) {
        umin = std::min(umin, r[i] - base);
        umax = std::max(umax, r[i] - base);
    }
    if (umin < 0) return best;
    for (int length = 1; length <= 4; length++) {
        if (umax >= (1 << length)) continue;
        const T::Huff &h = T::SF_UNSIGNED[length];
        int msk = (1 << length) - 1;
        int sum = 0;
        for (int i = 1; i < count; i++) sum += h.bits[((r[i] - base) - (r[i - 1] - base)) & msk];
        int bits = 2 + 5 + 2 + length + sum + (units - count) * 5;
        if (!best.valid || bits < best.bits) {
            best.valid = true;
            best.bits = bits;
            best.mode = mode;
            best.kind = SF_OFFSET;
            best.length = length;
            best.base = base;
            best.baseline = baseline;
            best.count = count;
        }
    }
    return best;
}

SfCoding choose_sf_coding(const int *sf, int units, int channel, bool first, const int *prev_sf, int prev_units,
                          const int *ch0_sf) {
    SfCoding opts[4];
    int n = 0;
    if (channel == 0) {
        opts[n++] = sf_delta_offset(sf, units, 0);
        opts[n++] = sf_clc(sf, units);
        if (!first) {
            opts[n++] = sf_distance(sf, units, prev_sf, prev_units, 2);
            opts[n++] = sf_delta_baseline(sf, units, prev_sf, prev_units, 3);
        }
    } else {
        opts[n++] = sf_delta_offset(sf, units, 0);
        opts[n++] = sf_distance(sf, units, ch0_sf, units, 1);
        opts[n++] = sf_delta_baseline(sf, units, ch0_sf, units, 2);
        if (!first) opts[n++] = sf_distance(sf, units, prev_sf, prev_units, 3);
    }
    SfCoding best;
    for (int i = 0; i < n; i++)
        if (opts[i].valid && (!best.valid || opts[i].bits < best.bits)) best = opts[i];
    return best;
}

void write_sf(BitWriter &out, const int *sf, int units, const SfCoding &c) {
    out.put(c.mode, 2);
    if (c.kind == SF_CLC) {
        out.put(c.length - 2, 2);
        if (c.length < 5) out.put(c.base, 5);
        for (int u = 0; u < units; u++) out.put(sf[u] - c.base, c.length);
    } else if (c.kind == SF_DELTA) {
        int u[30];
        for (int i = 0; i < units; i++) u[i] = sf[i] + T::SF_WEIGHTS[c.w][i] - c.base;
        out.put(c.w, 3);
        out.put(c.base, 5);
        out.put(c.length - 3, 2);
        out.put(u[0], c.length);
        const T::Huff &h = T::SF_UNSIGNED[c.length];
        int msk = (1 << c.length) - 1;
        for (int i = 1; i < units; i++) {
            int d = (u[i] - u[i - 1]) & msk;
            out.put(h.codes[d], h.bits[d]);
        }
    } else if (c.kind == SF_DISTANCE) {
        out.put(c.length - 2, 2);
        const T::Huff &h = T::SF_SIGNED[c.length];
        int msk = (1 << c.length) - 1;
        for (int i = 0; i < c.count; i++) {
            int s = (sf[i] - c.baseline[i]) & msk;
            out.put(h.codes[s], h.bits[s]);
        }
        for (int i = c.count; i < units; i++) out.put(sf[i], 5);
    } else {
        out.put(c.base + 16, 5);
        out.put(c.length - 1, 2);
        int u[30] = {};
        for (int i = 0; i < c.count; i++) u[i] = sf[i] - c.baseline[i] - c.base;
        out.put(u[0], c.length);
        const T::Huff &h = T::SF_UNSIGNED[c.length];
        int msk = (1 << c.length) - 1;
        for (int i = 1; i < c.count; i++) {
            int d = (u[i] - u[i - 1]) & msk;
            out.put(h.codes[d], h.bits[d]);
        }
        for (int i = c.count; i < units; i++) out.put(sf[i], 5);
    }
}

// ---------------------------------------------------------------- channel frame

void fix_minimum(int *q /* [15][N] */, const double *xn) {
    for (int p = 1; p <= 3; p++) {
        int *row = q + (size_t)(p - 1) * N;
        int need = 1 << (p - 1);
        for (int u = 0; u < 8; u++) {
            int a = row[2 * u], b = row[2 * u + 1];
            if (std::max(std::abs(a), std::abs(b)) < need) {
                int j = std::fabs(xn[2 * u + 1]) > std::fabs(xn[2 * u]) ? 1 : 0;
                row[2 * u + j] = xn[2 * u + j] >= 0 ? need : -need;
            }
        }
    }
    int *row = q;
    for (int u = 0; u < 4; u++) {
        int *g = row + 16 + 4 * u;
        if (!g[0] && !g[1] && !g[2] && !g[3]) {
            const double *xs = xn + 16 + 4 * u;
            int j = 0;
            for (int i = 1; i < 4; i++)
                if (std::fabs(xs[i]) > std::fabs(xs[j])) j = i;
            g[j] = xs[j] >= 0 ? 1 : -1;
        }
    }
}

struct ChannelFrame {
    int sf[UNITS];
    int sets[UNITS];
    int mask[UNITS];
    std::vector<int> q;               // [15][N]
    std::vector<int> sym[7];          // sym[p] for p = 1..6 (word length p + 1)
    std::vector<double> lut0[2], lut1[2];  // [cost|noise][(mode * units + u) * D_SPAN + d - D_MIN]
    SfCoding coding;

    void init(const Static &S, const double *x, const int *sf_in, int units, const double *mask_in) {
        for (int u = 0; u < UNITS; u++) sf[u] = u < units ? sf_in[u] : 0;
        codebook_sets(sf, units, sets);
        // _precision_mask (encoder version: identical to the decoder's)
        precision_mask(sf, units, mask);
        double scale[N], xn[N];
        for (int u = 0; u < UNITS; u++) {
            double sc = std::ldexp(1.0, sf[u] - 15);
            for (int k = QIDX[u]; k < QIDX[u + 1]; k++) {
                scale[k] = sc;
                xn[k] = x[k] / sc;
            }
        }
        q.assign((size_t)15 * N, 0);
        for (int p = 1; p <= 15; p++) {
            int maxq = (1 << p) - 1;
            int *row = &q[(size_t)(p - 1) * N];
            for (int k = 0; k < N; k++) {
                double v = std::nearbyint(xn[k] * S.inv_step[p]);
                v = std::min(std::max(v, (double)-maxq), (double)maxq);
                row[k] = (int)v;
            }
        }
        fix_minimum(q.data(), xn);
        // noise[p][u] and cost[p][u]
        double table[2][UNITS][P_CAP + 1];
        for (int t = 0; t < 2; t++)
            for (int u = 0; u < UNITS; u++)
                for (int p = 0; p <= P_CAP; p++) table[t][u][p] = INF;
        double err2[N];
        for (int p = 1; p <= 15; p++) {
            const int *row = &q[(size_t)(p - 1) * N];
            for (int k = 0; k < N; k++) {
                double e = (xn[k] - row[k] * S.step[p]) * scale[k];
                err2[k] = e * e;
            }
            for (int u = 0; u < units; u++)
                table[1][u][p] = reduceat_sum(err2 + QIDX[u], QCNT[u]) * (1.0 / mask_in[u]);
        }
        for (int p = 1; p <= 6; p++) {
            const SpectrumBooks &b = S.books[p + 1];
            const int *row = &q[(size_t)(p - 1) * N];
            std::vector<int> &s = sym[p];
            s.resize(b.idx.size());
            double cost[UNITS] = {};
            for (size_t g = 0; g < b.idx.size(); g++) {
                int v = 0;
                for (int j = 0; j < 4; j++) {
                    int i = b.idx[g][j];
                    int qv = i < N ? row[i] : 0;
                    v += (qv & b.mask) << (j * b.wl);
                }
                s[g] = v;
                int u = b.unit[g];
                cost[u] += b.bit(sets[u], CBI[u], v);
            }
            for (int u = 0; u < units; u++) table[0][u][p] = cost[u];
        }
        for (int p = 7; p <= 15; p++)
            for (int u = 0; u < units; u++) table[0][u][p] = (double)(QCNT[u] * (p + 1));
        for (int t = 0; t < 2; t++) {
            lut0[t].resize((size_t)4 * units * D_SPAN);
            lut1[t].resize((size_t)4 * units * D_SPAN);
            for (int m = 0; m < 4; m++)
                for (int u = 0; u < units; u++) {
                    double *a = &lut0[t][((size_t)m * units + u) * D_SPAN];
                    double *b = &lut1[t][((size_t)m * units + u) * D_SPAN];
                    for (int d = 0; d < D_SPAN; d++) {
                        a[d] = table[t][u][S.p_of_d[m][d]];
                        b[d] = table[t][u][S.p1_of_d[m][d]];
                    }
                }
        }
    }
};

struct FrameOptions {
    std::vector<int> b;     // bytes
    std::vector<double> d;  // weighted noise
    std::vector<int> flat;  // candidate * (kmax + 1) + boundary
    int kmax = 0;
};

// ---------------------------------------------------------------- encoder

class Encoder {
  public:
    Encoder(int channels, int bitrate) : S(statics()), channels_(channels), bitrate_(bitrate) {
        if (channels != 1 && channels != 2) throw std::runtime_error("ATRAC9 encoder supports mono or stereo");
        bool ok = false;
        if (channels == 2) {
            for (int r : STEREO_BITRATES) ok |= r == bitrate;
        } else {
            for (int r : MONO_BITRATES) ok |= r == bitrate;
        }
        if (!ok) throw std::runtime_error("unsupported ATRAC9 bit rate " + std::to_string(bitrate) + " kb/s");
        frame_bytes_ = frame_bytes_for(bitrate);
        superframe_bytes_ = frame_bytes_ * FPS;
        int per_channel = bitrate / channels;
        static const int table[][2] = {{96, 16}, {84, 16}, {72, 15}, {60, 14}, {48, 13}, {36, 12}};
        max_band_ = 12;
        for (auto &e : table)
            if (per_channel >= e[0]) {
                max_band_ = e[1];
                break;
            }
    }

    int superframe_bytes() const { return superframe_bytes_; }

    std::vector<uint8_t> encode(const std::vector<const double *> &pcm, size_t samples, unsigned threads) {
        const int superframes = (int)((samples + N + (size_t)N * FPS - 1) / ((size_t)N * FPS));
        const int frames = superframes * FPS;
        if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
        Analysis A;
        A.channels = channels_;
        A.frames = frames;
        A.spectra.assign((size_t)channels_ * frames * N, 0.0);
        A.energy.assign((size_t)channels_ * frames * UNITS, 0.0);
        A.masks.assign((size_t)channels_ * frames * UNITS, 0.0);
        A.sfs.assign((size_t)channels_ * frames * UNITS, 0);
        // Analysis: frame f covers padded samples [f*N, f*N + 2N), where the
        // input starts at padded index N (the encoder delay).
        parallel_for(frames, 64, threads, [&](int f) {
            double block[2 * N];
            for (int c = 0; c < channels_; c++) {
                for (int n = 0; n < 2 * N; n++) {
                    long long i = (long long)f * N + n - N;
                    block[n] = (i >= 0 && (size_t)i < samples) ? pcm[c][i] * 32768.0 : 0.0;
                }
                size_t r = A.row(c, f);
                analyse_frame(S, block, &A.spectra[r * N], &A.energy[r * UNITS], &A.masks[r * UNITS],
                              &A.sfs[r * UNITS]);
            }
        });
        std::vector<int> bands = choose_bands(A, superframes);
        std::vector<uint8_t> out((size_t)superframes * superframe_bytes_);
        parallel_for(superframes, 8, threads, [&](int s) {
            int band = bands[s];
            for (;;) {
                try {
                    std::vector<uint8_t> data = superframe(A, s, band);
                    std::memcpy(&out[(size_t)s * superframe_bytes_], data.data(), data.size());
                    break;
                } catch (const Overflow &) {
                    if (band <= 3) throw std::runtime_error("superframe cannot be encoded within its size");
                    band -= 1;  // narrow the coded bandwidth until the superframe fits
                }
            }
        });
        return out;
    }

  private:
    const Static &S;
    int channels_, bitrate_, frame_bytes_ = 0, superframe_bytes_ = 0, max_band_ = 16;

    template <class F> static void parallel_for(int count, int chunk, unsigned threads, F fn) {
        std::atomic<int> next(0);
        std::exception_ptr error;
        std::mutex error_mutex;
        auto worker = [&] {
            for (;;) {
                int start = next.fetch_add(chunk);
                if (start >= count) return;
                int stop = std::min(count, start + chunk);
                try {
                    for (int i = start; i < stop; i++) fn(i);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(error_mutex);
                    if (!error) error = std::current_exception();
                    next.store(count);
                    return;
                }
            }
        };
        unsigned n = std::min<unsigned>(threads, (unsigned)std::max(1, (count + chunk - 1) / chunk));
        if (n <= 1) {
            worker();
        } else {
            std::vector<std::thread> pool;
            for (unsigned i = 0; i < n; i++) pool.emplace_back(worker);
            for (auto &t : pool) t.join();
        }
        if (error) std::rethrow_exception(error);
    }

    std::vector<int> choose_bands(const Analysis &A, int superframes) const {
        std::vector<int> need(superframes);
        for (int s = 0; s < superframes; s++) {
            int top = 0;
            for (int u = UNITS - 1; u >= 0 && !top; u--)
                for (int c = 0; c < channels_ && !top; c++)
                    for (int f = 0; f < FPS; f++) {
                        size_t r = A.row(c, s * FPS + f) * UNITS + u;
                        if (A.energy[r] > A.masks[r]) {
                            top = u + 1;
                            break;
                        }
                    }
            int b = 18;
            for (int x = 3; x < 19; x++)
                if (BAND_UNITS[x] >= top) {
                    b = x;
                    break;
                }
            need[s] = b;
        }
        std::vector<int> out(superframes);
        for (int s = 0; s < superframes; s++) {
            int v = need[s];
            for (int d = -2; d <= 2; d++)
                if (s + d >= 0 && s + d < superframes) v = std::max(v, need[s + d]);
            out[s] = std::min(std::max(v, 3), max_band_);
        }
        return out;
    }

    FrameOptions frame_options(const ChannelFrame *chans, const Candidates &C, int fixed_bits, int units) const {
        const int kmax = std::min(15, units);
        const int W = kmax + 1;
        std::vector<double> bits((size_t)C.count * W, 0.0), dist((size_t)C.count * W, 0.0);
        double c0[UNITS], n0[UNITS];
        for (int ch = 0; ch < channels_; ch++) {
            const ChannelFrame &cf = chans[ch];
            int base_m[UNITS], base_0[UNITS];
            for (int u = 0; u < units; u++) {
                base_0[u] = cf.sf[u];
                base_m[u] = cf.sf[u] + cf.mask[u];
            }
            for (int k = 0; k < C.count; k++) {
                const int *grad = &C.grad[(size_t)k * units];
                const int *off = &C.offset[(size_t)k * units];
                const int *base = C.mode0[k] ? base_0 : base_m;
                int idx[UNITS];
                for (int u = 0; u < units; u++) {
                    int d = std::min(std::max(base[u] - grad[u], D_MIN), D_MAX);
                    idx[u] = off[u] + d;
                    c0[u] = cf.lut0[0][idx[u]];
                    n0[u] = cf.lut0[1][idx[u]];
                }
                double b0 = pairwise_sum(c0, units);
                double d0 = pairwise_sum(n0, units);
                double *b = &bits[(size_t)k * W];
                double *dd = &dist[(size_t)k * W];
                double bv[16], dv[16];
                bv[0] = b0;
                dv[0] = d0;
                double cb = 0.0, cn = 0.0;
                for (int j = 0; j < kmax; j++) {
                    double dc = cf.lut1[0][idx[j]] - c0[j];
                    double dn = cf.lut1[1][idx[j]] - n0[j];
                    cb = j ? cb + dc : dc;
                    cn = j ? cn + dn : dn;
                    bv[j + 1] = cb + b0;
                    dv[j + 1] = cn + d0;
                }
                if (ch == 0) {
                    for (int j = 0; j < W; j++) {
                        b[j] = bv[j];
                        dd[j] = dv[j];
                    }
                } else {
                    for (int j = 0; j < W; j++) {
                        b[j] = b[j] + bv[j];
                        dd[j] = dd[j] + dv[j];
                    }
                }
            }
        }
        // Best (lowest noise, first candidate on ties) option for every byte size.
        const int cap = superframe_bytes_;
        std::vector<double> best_d(cap + 1, 0.0);
        std::vector<int> best_flat(cap + 1, -1);
        bool any = false;
        for (int k = 0; k < C.count; k++) {
            double extra = (double)(fixed_bits + C.header_bits[k]);
            for (int j = 0; j < W; j++) {
                size_t flat = (size_t)k * W + j;
                double nb = std::ceil((bits[flat] + extra) / 8.0);
                if (!(nb <= cap)) continue;
                any = true;
                int by = (int)nb;
                double d = dist[flat];
                if (best_flat[by] < 0 || d < best_d[by]) {
                    best_flat[by] = (int)flat;
                    best_d[by] = d;
                }
            }
        }
        if (!any) throw Overflow();
        FrameOptions o;
        o.kmax = kmax;
        double running = INF;
        bool first = true;
        for (int by = 0; by <= cap; by++) {
            if (best_flat[by] < 0) continue;
            double d = best_d[by];
            if (first || d < running) {
                o.b.push_back(by);
                o.d.push_back(d);
                o.flat.push_back(best_flat[by]);
            }
            running = first ? d : std::min(running, d);
            first = false;
        }
        return o;
    }

    std::vector<int> allocate(const std::vector<FrameOptions> &frames) const {
        const int cap = superframe_bytes_;
        std::vector<double> dp(cap + 1, INF), nw(cap + 1);
        dp[0] = 0.0;
        std::vector<std::vector<int>> back;
        for (const FrameOptions &fo : frames) {
            std::fill(nw.begin(), nw.end(), INF);
            std::vector<int> arg(cap + 1, -1);
            for (size_t i = 0; i < fo.b.size(); i++) {
                int size = fo.b[i];
                for (int p = 0; p + size <= cap; p++) {
                    double cand = dp[p] + fo.d[i];
                    if (cand < nw[p + size]) {
                        nw[p + size] = cand;
                        arg[p + size] = (int)i;
                    }
                }
            }
            back.push_back(std::move(arg));
            dp.swap(nw);
        }
        bool finite = false;
        for (double v : dp) finite |= std::isfinite(v);
        if (!finite) throw Overflow();
        int pos = 0;
        for (int i = 1; i <= cap; i++)
            if (dp[i] < dp[pos]) pos = i;
        std::vector<int> choice(frames.size(), 0);
        for (int f = (int)frames.size() - 1; f >= 0; f--) {
            int i = back[f][pos];
            choice[f] = i;
            pos -= frames[f].b[i];
        }
        return choice;
    }

    std::vector<uint8_t> superframe(const Analysis &A, int s, int band) const {
        const int units = BAND_UNITS[band];
        const Candidates &C = S.cands.at(units);
        const bool stereo = channels_ == 2;
        std::vector<ChannelFrame> chans((size_t)FPS * channels_);
        std::vector<FrameOptions> opts(FPS);
        for (int f = 0; f < FPS; f++) {
            const bool first = f == 0;
            int fixed = 2 + (first ? (stereo ? 9 : 5) : 0) + (stereo ? 2 : 0) + 1;
            for (int c = 0; c < channels_; c++) {
                size_t r = A.row(c, s * FPS + f);
                ChannelFrame &cf = chans[(size_t)f * channels_ + c];
                cf.init(S, &A.spectra[r * N], &A.sfs[r * UNITS], units, &A.masks[r * UNITS]);
                const int *prev = first ? nullptr : chans[(size_t)(f - 1) * channels_ + c].sf;
                const int *ch0 = c ? chans[(size_t)f * channels_].sf : nullptr;
                cf.coding = choose_sf_coding(cf.sf, units, c, first, prev, units, ch0);
                fixed += cf.coding.bits;
            }
            opts[f] = frame_options(&chans[(size_t)f * channels_], C, fixed, units);
        }
        std::vector<int> choice = allocate(opts);
        std::vector<uint8_t> data;
        for (int f = 0; f < FPS; f++) {
            std::vector<uint8_t> fr = write_frame(&chans[(size_t)f * channels_], opts[f], choice[f], f == 0, band,
                                                  units, C);
            data.insert(data.end(), fr.begin(), fr.end());
        }
        if ((int)data.size() > superframe_bytes_) throw std::runtime_error("internal error: superframe overflow");
        // Sony's decoder rejects superframes whose unused tail is not 0x01 bytes.
        data.resize(superframe_bytes_, 0x01);
        return data;
    }

    std::vector<uint8_t> write_frame(const ChannelFrame *chans, const FrameOptions &fo, int index, bool first,
                                     int band, int units, const Candidates &C) const {
        const int flat = fo.flat[index];
        const int cand = flat / (fo.kmax + 1), boundary = flat % (fo.kmax + 1);
        const auto &pr = C.params[cand];
        const int mode = pr[0], start_unit = pr[1], end_unit = pr[2], start_value = pr[3], end_value = pr[4];
        BitWriter out;
        out.put(first ? 0 : 1, 1);
        out.put(first ? 0 : 1, 1);
        if (first) {
            out.put(band - 3, 4);
            if (channels_ == 2) out.put(band - 3, 4);
            out.put(0, 1);
        }
        out.put(mode, 2);
        if (mode) {
            out.put(start_unit, 5);
            out.put(start_value, 5);
        } else {
            out.put(start_unit, 6);
            out.put(end_unit - 1, 6);
            out.put(start_value, 5);
            out.put(end_value, 5);
        }
        out.put(boundary, 4);
        if (channels_ == 2) {
            out.put(0, 1);  // primary channel 0
            out.put(0, 1);  // no intensity-stereo signs
        }
        out.put(0, 1);  // no band-extension data
        int grad[31];
        gradient_curve(mode, start_unit, end_unit, start_value, end_value, units, grad);
        for (int c = 0; c < channels_; c++) {
            const ChannelFrame &cf = chans[c];
            int coarse[30], fine[30];
            precisions(cf.sf, grad, mode, boundary, units, coarse, fine);
            for (int u = 0; u < units; u++)
                if (fine[u]) throw std::runtime_error("internal error: fine precision selected");
            write_sf(out, cf.sf, units, cf.coding);
            for (int u = 0; u < units; u++) {
                int p = coarse[u];
                int wl = p + 1;
                if (wl <= 7) {
                    const SpectrumBooks &b = S.books[wl];
                    int s = cf.sets[u];
                    for (int g = b.group_start[u]; g < b.group_start[u + 1]; g++) {
                        int sym = cf.sym[p][g];
                        int len = b.bit(s, CBI[u], sym);
                        if (!len) throw std::runtime_error("internal error: invalid spectrum symbol");
                        out.put(b.code(s, CBI[u], sym), len);
                    }
                } else {
                    const int *row = &cf.q[(size_t)(p - 1) * N];
                    for (int k = QIDX[u]; k < QIDX[u + 1]; k++) out.put(row[k], wl);
                }
            }
        }
        std::vector<uint8_t> data = out.bytes();
        if ((int)data.size() != fo.b[index])
            throw std::runtime_error("internal error: frame size mismatch (" + std::to_string(data.size()) +
                                     " != " + std::to_string(fo.b[index]) + ")");
        return data;
    }
};

void put_u16(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)x);
    v.push_back((uint8_t)(x >> 8));
}
void put_u32(std::vector<uint8_t> &v, uint32_t x) {
    put_u16(v, x & 0xFFFF);
    put_u16(v, x >> 16);
}
void put_tag(std::vector<uint8_t> &v, const char *t) { v.insert(v.end(), t, t + 4); }

} // namespace

namespace detail {

std::vector<uint8_t> encode_atrac9(const std::vector<double> &left, const std::vector<double> &right,
                                   int bitrate_kbps, unsigned threads) {
    if (left.size() != right.size()) throw std::runtime_error("channel lengths differ");
    Encoder enc(2, bitrate_kbps);
    return enc.encode({left.data(), right.data()}, left.size(), threads);
}

std::vector<uint8_t> build_at9(int bitrate_kbps, uint32_t sample_count, const std::vector<uint8_t> &data, bool loop) {
    const int channels = 2;
    const uint32_t frame_bytes = (uint32_t)frame_bytes_for(bitrate_kbps);
    const uint32_t superframe_bytes = frame_bytes * FPS;
    if (data.size() % superframe_bytes) throw std::runtime_error("data must contain whole superframes");
    // Configuration word: 0xFE, sample rate index 7, channel config 2 (stereo),
    // frame bytes - 1, superframe index 2 (four frames).
    uint32_t word = (0xFEu << 24) | (7u << 20) | (2u << 17) | ((frame_bytes - 1) << 5) | (2u << 3);
    const uint32_t rate = 48000, superframe_samples = 1024;
    const uint32_t byte_rate = (uint32_t)((uint64_t)superframe_bytes * rate / superframe_samples);
    std::vector<uint8_t> fmt;
    put_u16(fmt, 0xFFFE);
    put_u16(fmt, channels);
    put_u32(fmt, rate);
    put_u32(fmt, byte_rate);
    put_u16(fmt, superframe_bytes);
    put_u16(fmt, 0);   // bits per sample
    put_u16(fmt, 34);  // cbSize
    put_u16(fmt, superframe_samples);
    put_u32(fmt, 0x3);  // speaker mask (stereo)
    static const uint8_t guid[16] = {0xD2, 0x42, 0xE1, 0x47, 0xBA, 0x36, 0x8D, 0x4D,
                                     0x88, 0xFC, 0x61, 0x65, 0x4F, 0x8C, 0x83, 0x6C};
    fmt.insert(fmt.end(), guid, guid + 16);
    put_u32(fmt, 1);  // version
    fmt.push_back((uint8_t)(word >> 24));
    fmt.push_back((uint8_t)(word >> 16));
    fmt.push_back((uint8_t)(word >> 8));
    fmt.push_back((uint8_t)word);
    put_u32(fmt, 0);
    std::vector<uint8_t> fact;
    put_u32(fact, sample_count);
    put_u32(fact, ENCODER_DELAY);
    put_u32(fact, ENCODER_DELAY);
    std::vector<uint8_t> smpl;
    if (loop) {
        uint32_t period = (uint32_t)std::nearbyint(1e9 / rate);
        const uint32_t head[9] = {0, 0, period, 60, 0, 0, 0, 1, 24};
        for (uint32_t x : head) put_u32(smpl, x);
        const uint32_t lp[6] = {0, 0, (uint32_t)ENCODER_DELAY, sample_count + ENCODER_DELAY - 1, 0, 0};
        for (uint32_t x : lp) put_u32(smpl, x);
    }
    std::vector<uint8_t> body;
    put_tag(body, "WAVE");
    auto chunk = [&](const char *id, const std::vector<uint8_t> &payload) {
        put_tag(body, id);
        put_u32(body, (uint32_t)payload.size());
        body.insert(body.end(), payload.begin(), payload.end());
    };
    chunk("fmt ", fmt);
    chunk("fact", fact);
    if (loop) chunk("smpl", smpl);
    chunk("data", data);
    std::vector<uint8_t> out;
    out.reserve(body.size() + 8);
    put_tag(out, "RIFF");
    put_u32(out, (uint32_t)body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace detail
} // namespace at9
