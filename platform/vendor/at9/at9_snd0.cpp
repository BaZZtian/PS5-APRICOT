// snd0.at9 pipeline: channel mapping, resampling, loudness (BS.1770-4),
// true-peak limiting, fades, and the make_snd0 driver; preview WAV writer.
//
// C++ port of ps5at9/dsp.py, ps5at9/cli.py and parts of ps5at9/audio_io.py and
// ps5at9/wav.py from PS5 AT9 Converter v0.1.0
// (https://github.com/blackbearreloaded/ps5-at9-converter).
// Copyright (C) 2026 BlackBearReloaded (original Python implementation)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "at9.h"
#include "at9_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>

namespace at9 {
namespace {

using namespace internal;
using Planar = std::vector<std::vector<double>>;

// Run fn(begin, end) over [0, n) split across threads. Every output element
// is computed independently, so results do not depend on the thread count.
template <class F> void parallel_chunks(size_t n, unsigned threads, F fn) {
    const size_t min_chunk = 16384;
    unsigned t = std::max(1u, std::min<unsigned>(threads, (unsigned)((n + min_chunk - 1) / min_chunk)));
    if (t <= 1) {
        fn((size_t)0, n);
        return;
    }
    std::vector<std::thread> pool;
    size_t step = (n + t - 1) / t;
    for (unsigned i = 0; i < t; i++) {
        size_t b = i * step, e = std::min(n, b + step);
        if (b < e) pool.emplace_back([=] { fn(b, e); });
    }
    for (auto &th : pool) th.join();
}

// ---------------------------------------------------------------- numpy.i0 (Cephes)

const double I0A[30] = {
    -4.4153416464793395e-18, 3.3307945188222384e-17, -2.431279846547955e-16, 1.715391285555133e-15,
    -1.1685332877993451e-14, 7.676185498604936e-14, -4.856446783111929e-13, 2.95505266312964e-12,
    -1.726826291441556e-11, 9.675809035373237e-11, -5.189795601635263e-10, 2.6598237246823866e-09,
    -1.300025009986248e-08, 6.046995022541919e-08, -2.670793853940612e-07, 1.1173875391201037e-06,
    -4.4167383584587505e-06, 1.6448448070728896e-05, -5.754195010082104e-05, 0.00018850288509584165,
    -0.0005763755745385824, 0.0016394756169413357, -0.004324309995050576, 0.010546460394594998,
    -0.02373741480589947, 0.04930528423967071, -0.09490109704804764, 0.17162090152220877,
    -0.3046826723431984, 0.6767952744094761};

const double I0B[25] = {
    -7.233180487874754e-18, -4.830504485944182e-18, 4.46562142029676e-17, 3.461222867697461e-17,
    -2.8276239805165836e-16, -3.425485619677219e-16, 1.7725601330565263e-15, 3.8116806693526224e-15,
    -9.554846698828307e-15, -4.150569347287222e-14, 1.54008621752141e-14, 3.8527783827421426e-13,
    7.180124451383666e-13, -1.7941785315068062e-12, -1.3215811840447713e-11, -3.1499165279632416e-11,
    1.1889147107846439e-11, 4.94060238822497e-10, 3.3962320257083865e-09, 2.266668990498178e-08,
    2.0489185894690638e-07, 2.8913705208347567e-06, 6.889758346916825e-05, 0.0033691164782556943,
    0.8044904110141088};

double chbevl(double x, const double *vals, int n) {
    double b0 = vals[0], b1 = 0.0, b2 = 0.0;
    for (int i = 1; i < n; i++) {
        b2 = b1;
        b1 = b0;
        b0 = x * b1 - b2 + vals[i];
    }
    return 0.5 * (b0 - b2);
}

double i0(double x) {
    x = std::fabs(x);
    if (x <= 8.0) return std::exp(x) * chbevl(x / 2.0 - 2, I0A, 30);
    return std::exp(x) * chbevl(32.0 / x - 2.0, I0B, 25) / std::sqrt(x);
}

double sinc(double x) {
    double y = PI * x;
    if (y == 0) y = std::numeric_limits<double>::epsilon();
    return std::sin(y) / y;
}

// ---------------------------------------------------------------- resampling

// Polyphase Kaiser-windowed low-pass h[phase][tap] (taps = 2 * half).
std::vector<double> kaiser_phases(int up, double rate_in, double pass_hz, double stop_hz, double atten_db,
                                  int &half) {
    double beta = 0.1102 * (atten_db - 8.7);
    double width = (stop_hz - pass_hz) / rate_in;
    int taps = (int)std::ceil((atten_db - 8.0) / (2.285 * 2.0 * PI * width));
    half = std::max(8, taps / 2 + 1);
    double cutoff = (pass_hz + stop_hz) / 2.0 / rate_in;
    const int L = 2 * half;
    std::vector<double> h((size_t)up * L);
    double ib = i0(beta);
    for (int p = 0; p < up; p++) {
        double phase = (double)p / up;
        double *row = &h[(size_t)p * L];
        for (int j = 0; j < L; j++) {
            double t = (double)(-half + 1 + j) - phase;
            double v = 2.0 * cutoff * sinc(2.0 * cutoff * t);
            double r = t / half;
            double c = std::min(std::max(1.0 - r * r, 0.0), 1.0);
            double win = i0(beta * std::sqrt(c)) / ib;
            row[j] = v * win;
        }
        double sum = pairwise_sum(row, L);
        for (int j = 0; j < L; j++) row[j] /= sum;
    }
    return h;
}

long long gcd_ll(long long a, long long b) {
    while (b) {
        long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

std::vector<double> resample_channel(const std::vector<double> &x, const std::vector<double> &h, int half, int up,
                                     int down, size_t n_out, unsigned threads) {
    const size_t n_in = x.size();
    const int L = 2 * half;
    std::vector<double> pad(n_in + 2 * half + 2, 0.0);
    std::copy(x.begin(), x.end(), pad.begin() + half);
    std::vector<double> out(n_out);
    parallel_chunks(n_out, threads, [&](size_t begin, size_t end) {
        for (size_t n = begin; n < end; n++) {
            long long pos = (long long)n * down;
            long long base = pos / up;
            int phase = (int)(pos % up);
            const double *c = &h[(size_t)phase * L];
            const double *s = &pad[base + 1];
            double acc = 0.0;
            for (int j = 0; j < L; j++) acc += s[j] * c[j];
            out[n] = acc;
        }
    });
    return out;
}

Planar resample(const Planar &pcm, int rate_in, int rate_out, unsigned threads) {
    if (rate_in == rate_out) return pcm;
    long long g = gcd_ll(rate_in, rate_out);
    int up = (int)(rate_out / g), down = (int)(rate_in / g);
    double nyquist = std::min(rate_in, rate_out) / 2.0;
    double pass_hz = std::min(20000.0, 0.9 * nyquist);
    int half = 0;
    std::vector<double> h = kaiser_phases(up, rate_in, pass_hz, nyquist, 100.0, half);
    size_t n_in = pcm[0].size();
    size_t n_out = (size_t)std::ceil((double)((long long)n_in * up) / (double)down);
    Planar out;
    for (const auto &ch : pcm) out.push_back(resample_channel(ch, h, half, up, down, n_out, threads));
    return out;
}

// ---------------------------------------------------------------- loudness

std::vector<double> k_weight(const std::vector<double> &x) {
    // ITU-R BS.1770-4 K-weighting at 48 kHz: high shelf, then high-pass.
    static const double sb[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285};
    static const double sa[3] = {1.0, -1.69065929318241, 0.73248077421585};
    static const double hb[3] = {1.0, -2.0, 1.0};
    static const double ha[3] = {1.0, -1.99004745483398, 0.99007225036621};
    std::vector<double> y(x.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t n = 0; n < x.size(); n++) {
        double x0 = x[n];
        double y0 = sb[0] * x0 + sb[1] * x1 + sb[2] * x2 - sa[1] * y1 - sa[2] * y2;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
        y[n] = y0;
    }
    x1 = x2 = y1 = y2 = 0;
    for (size_t n = 0; n < y.size(); n++) {
        double x0 = y[n];
        double y0 = hb[0] * x0 + hb[1] * x1 + hb[2] * x2 - ha[1] * y1 - ha[2] * y2;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
        y[n] = y0;
    }
    return y;
}

double integrated_loudness(const Planar &pcm) {
    const size_t block = (size_t)(0.4 * 48000), step = (size_t)(0.1 * 48000);
    const size_t n = pcm[0].size();
    Planar w;
    for (const auto &ch : pcm) {
        std::vector<double> k = k_weight(ch);
        for (double &v : k) v = v * v;
        w.push_back(std::move(k));
    }
    if (n < block) {
        double power = 0.0;
        for (const auto &ch : w) power += pairwise_sum(ch.data(), (std::ptrdiff_t)n) / (double)n;
        return power > 0 ? -0.691 + 10.0 * std::log10(power) : -std::numeric_limits<double>::infinity();
    }
    Planar csum;
    for (const auto &ch : w) {
        std::vector<double> c(n + 1);
        c[0] = 0.0;
        double acc = 0.0;
        for (size_t i = 0; i < n; i++) {
            acc += ch[i];
            c[i + 1] = acc;
        }
        csum.push_back(std::move(c));
    }
    std::vector<double> z, lk;
    for (size_t s = 0; s + block <= n; s += step) {
        double v = 0.0;
        for (size_t c = 0; c < csum.size(); c++) {
            double e = (csum[c][s + block] - csum[c][s]) / (double)block;
            v = c ? v + e : e;
        }
        z.push_back(v);
        lk.push_back(-0.691 + 10.0 * std::log10(v));
    }
    std::vector<double> gated;
    for (size_t i = 0; i < z.size(); i++)
        if (lk[i] > -70.0) gated.push_back(z[i]);
    if (gated.empty()) return -std::numeric_limits<double>::infinity();
    double relative =
        -0.691 + 10.0 * std::log10(pairwise_sum(gated.data(), (std::ptrdiff_t)gated.size()) / gated.size()) - 10.0;
    std::vector<double> fin;
    for (size_t i = 0; i < z.size(); i++)
        if (lk[i] > -70.0 && lk[i] > relative) fin.push_back(z[i]);
    return -0.691 + 10.0 * std::log10(pairwise_sum(fin.data(), (std::ptrdiff_t)fin.size()) / fin.size());
}

// ---------------------------------------------------------------- true peak

struct Oversampler {
    std::vector<double> h;
    int half = 0;
    Oversampler() { h = kaiser_phases(4, 48000, 18000.0, 24000.0, 60.0, half); }
};

const Oversampler &oversampler() {
    static const Oversampler o;
    return o;
}

// Per-sample true peak (max |x| over 4x oversampled points), max over channels.
std::vector<double> true_peak_envelope(const Planar &pcm, unsigned threads) {
    const Oversampler &O = oversampler();
    const int half = O.half, L = 2 * half;
    const size_t n = pcm[0].size();
    std::vector<double> env(n, 0.0);
    std::vector<double> pad(n + 2 * half + 2);
    for (size_t c = 0; c < pcm.size(); c++) {
        std::fill(pad.begin(), pad.end(), 0.0);
        std::copy(pcm[c].begin(), pcm[c].end(), pad.begin() + half);
        parallel_chunks(n, threads, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; i++) {
                const double *s = &pad[i + 1];
                double m = 0.0;
                for (int p = 0; p < 4; p++) {
                    const double *k = &O.h[(size_t)p * L];
                    double acc = 0.0;
                    for (int j = 0; j < L; j++) acc += s[j] * k[j];
                    m = std::max(m, std::fabs(acc));
                }
                env[i] = c ? std::max(env[i], m) : m;
            }
        });
    }
    return env;
}

double max_of(const std::vector<double> &v) {
    double m = -std::numeric_limits<double>::infinity();
    for (double x : v) m = std::max(m, x);
    return m;
}

// Minimum over the centred window [n - width, n + width] (edges extended).
std::vector<double> sliding_min(const std::vector<double> &x, int width) {
    if (width <= 0) return x;
    const long long n = (long long)x.size();
    std::vector<double> out(n);
    std::deque<long long> dq;  // indices into x with increasing values
    long long next = 0;
    for (long long i = 0; i < n; i++) {
        long long hi = std::min(n - 1, i + width);
        while (next <= hi) {
            while (!dq.empty() && x[dq.back()] >= x[next]) dq.pop_back();
            dq.push_back(next++);
        }
        long long lo = i - width;
        while (dq.front() < lo) dq.pop_front();
        out[i] = x[dq.front()];
    }
    return out;
}

// Mean over the centred window [n - width, n + width] (edges extended).
std::vector<double> moving_average(const std::vector<double> &x, int width) {
    if (width <= 0) return x;
    const size_t n = x.size();
    const size_t w = 2 * (size_t)width + 1;
    std::vector<double> c(n + 2 * width + 1);
    c[0] = 0.0;
    double acc = 0.0;
    for (size_t i = 0; i < n + 2 * (size_t)width; i++) {
        double v = i < (size_t)width ? x[0] : (i - width < n ? x[i - width] : x[n - 1]);
        acc += v;
        c[i + 1] = acc;
    }
    std::vector<double> out(n);
    for (size_t i = 0; i < n; i++) out[i] = (c[i + w] - c[i]) / (double)w;
    return out;
}

bool limit_true_peak(Planar &pcm, double ceiling_db, unsigned threads, int rate = 48000, double attack = 0.002,
                     double release = 0.05) {
    std::vector<double> env = true_peak_envelope(pcm, threads);
    const double ceiling = std::pow(10.0, ceiling_db / 20.0);
    if (max_of(env) <= ceiling) return false;
    double target = ceiling_db;
    int a = std::max(1, (int)(attack * rate));
    int r = std::max(1, (int)(release * rate));
    for (int it = 0; it < 8; it++) {
        double t = std::pow(10.0, target / 20.0);
        std::vector<double> need(env.size());
        for (size_t i = 0; i < env.size(); i++) need[i] = std::min(1.0, t / std::max(env[i], 1e-12));
        std::vector<double> gain = moving_average(sliding_min(need, a), a / 2);
        gain = moving_average(sliding_min(gain, r), r / 2);
        for (auto &ch : pcm)
            for (size_t i = 0; i < ch.size(); i++) ch[i] = ch[i] * gain[i];
        env = true_peak_envelope(pcm, threads);
        if (max_of(env) <= ceiling) break;
        target -= 0.05;
    }
    return true;
}

// ---------------------------------------------------------------- fades

void apply_fades(Planar &pcm, int rate, double fade_in, double fade_out) {
    const long long n = (long long)pcm[0].size();
    long long fi = std::min(n, (long long)std::nearbyint(fade_in * rate));
    long long fo = std::min(n, (long long)std::nearbyint(fade_out * rate));
    for (auto &ch : pcm) {
        for (long long i = 0; i < fi; i++) ch[i] *= 0.5 - 0.5 * std::cos(PI * (i + 0.5) / fi);
        for (long long i = 0; i < fo; i++) ch[n - fo + i] *= 0.5 + 0.5 * std::cos(PI * (i + 0.5) / fo);
    }
}

// ---------------------------------------------------------------- helpers

Planar to_stereo(const Audio &in, size_t a, size_t b) {
    const int ch = in.channels;
    const size_t frames = b - a;
    Planar out(2, std::vector<double>(frames));
    auto at = [&](size_t i, int c) { return (double)in.pcm[(a + i) * ch + c]; };
    if (ch == 1) {
        for (size_t i = 0; i < frames; i++) out[0][i] = out[1][i] = at(i, 0);
    } else if (ch == 6) {  // FL FR FC LFE BL BR, ITU-R BS.775 downmix
        const double k = 1.0 / std::sqrt(2.0);
        const double norm = 1.0 + 2.0 * k;
        for (size_t i = 0; i < frames; i++) {
            out[0][i] = (at(i, 0) + k * at(i, 2) + k * at(i, 4)) / norm;
            out[1][i] = (at(i, 1) + k * at(i, 2) + k * at(i, 5)) / norm;
        }
    } else {
        for (size_t i = 0; i < frames; i++) {
            out[0][i] = at(i, 0);
            out[1][i] = at(i, 1);
        }
    }
    return out;
}

std::string fmt_time(double seconds) {
    double minutes = std::floor(seconds / 60.0);
    double sec = seconds - minutes * 60.0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%06.3f", (int)minutes, sec);
    return buf;
}

uint64_t max_samples_for(int bitrate, bool loop) {
    uint64_t sfb = (uint64_t)frame_bytes_for(bitrate) * 4;
    uint64_t superframes = (PS5_SIZE_LIMIT - header_size(loop)) / sfb;
    return superframes * SUPERFRAME_SAMPLES - ENCODER_DELAY;
}

} // namespace

namespace detail {

Result make_snd0_ex(const Audio &in, const Options &o, std::vector<double> *left, std::vector<double> *right,
                    unsigned threads) {
    static const int rates[] = {72, 96, 120, 144, 168, 192};
    if (std::find(std::begin(rates), std::end(rates), o.bitrate_kbps) == std::end(rates))
        throw std::runtime_error("bit rate must be 72, 96, 120, 144, 168 or 192 kb/s");
    if (in.rate <= 0 || in.channels <= 0 || in.pcm.size() < (size_t)in.channels)
        throw std::runtime_error("the input has no audio");
    if (!(o.start_sec >= 0) || !std::isfinite(o.start_sec)) throw std::runtime_error("start must be positive");
    const bool max_dur = !(o.duration_sec > 0);
    const size_t total = in.pcm.size() / in.channels;

    // audio_io.load: decode from up to 0.25 s before the start so resampling
    // can be trimmed cleanly afterwards.
    const double margin = 0.25;
    const double begin = std::max(0.0, o.start_sec - margin);
    const double offset = o.start_sec - begin;
    size_t a = (size_t)std::floor(begin * in.rate);
    size_t b = total;
    if (!max_dur) {
        double length = o.duration_sec + offset + margin;
        b = (size_t)std::min<double>((double)total, (double)a + std::ceil(length * in.rate));
    }
    if (a >= total || b <= a) throw std::runtime_error("the start time is beyond the end of the input");

    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    Planar pcm = resample(to_stereo(in, a, b), in.rate, RATE, threads);
    const size_t skip = (size_t)std::nearbyint(offset * RATE);
    const size_t available = pcm[0].size() > skip ? pcm[0].size() - skip : 0;
    const bool loop = o.loop;
    const uint64_t max_samples = max_samples_for(o.bitrate_kbps, loop);
    size_t count;
    if (max_dur)
        count = (size_t)std::min<uint64_t>(available, max_samples);
    else
        count = std::min(available, (size_t)std::nearbyint(o.duration_sec * RATE));
    if (count == 0) throw std::runtime_error("the selected excerpt is empty");
    for (auto &ch : pcm) ch = std::vector<double>(ch.begin() + skip, ch.begin() + skip + count);

    const uint32_t sfb = (uint32_t)frame_bytes_for(o.bitrate_kbps) * 4;
    const uint64_t size = file_size(count, sfb, loop);
    if (size > PS5_SIZE_LIMIT) {
        char msg[256];
        std::snprintf(msg, sizeof msg,
                      "%.3f s at %d kb/s makes a %llu-byte file, above the 2 MiB PS5 limit; use at most %.3f s "
                      "(or a lower bit rate)",
                      count / (double)RATE, o.bitrate_kbps, (unsigned long long)size, max_samples / (double)RATE);
        throw std::runtime_error(msg);
    }

    const double source_lufs = integrated_loudness(pcm);
    if (o.normalize && std::isfinite(source_lufs)) {
        double gain_db = o.lufs - source_lufs;
        double g = std::pow(10.0, gain_db / 20.0);
        for (auto &ch : pcm)
            for (double &v : ch) v = v * g;
    }
    limit_true_peak(pcm, o.true_peak_db, threads, RATE);
    apply_fades(pcm, RATE, o.fade_in, o.fade_out);
    const double final_lufs = integrated_loudness(pcm);

    Result r;
    std::vector<uint8_t> data = encode_atrac9(pcm[0], pcm[1], o.bitrate_kbps, threads);
    r.at9 = build_at9(o.bitrate_kbps, (uint32_t)count, data, loop);
    if (r.at9.size() != size) throw std::runtime_error("internal error: unexpected file size");
    r.loudness_lufs = final_lufs;
    r.seconds = count / (double)RATE;
    char buf[64];
    if (std::isfinite(final_lufs))
        std::snprintf(buf, sizeof buf, "%.2f", final_lufs);
    else
        std::snprintf(buf, sizeof buf, "%s", final_lufs < 0 ? "-inf" : "inf");  // Python's "%.2f" % -inf
    r.loudness_param = buf;
    r.processed.resize(count * 2);
    for (size_t i = 0; i < count; i++) {
        r.processed[2 * i] = (float)pcm[0][i];
        r.processed[2 * i + 1] = (float)pcm[1][i];
    }
    if (left) *left = pcm[0];
    if (right) *right = pcm[1];
    return r;
}

} // namespace detail

Result make_snd0(const Audio &in, const Options &o) { return detail::make_snd0_ex(in, o, nullptr, nullptr, 0); }

std::vector<uint8_t> wav_bytes(const std::vector<float> &stereo48k) {
    const uint32_t channels = 2, rate = 48000, bits = 16;
    const size_t frames = stereo48k.size() / 2;
    const uint32_t payload = (uint32_t)(frames * channels * 2);
    std::vector<uint8_t> out;
    out.reserve(44 + payload + 1);
    auto u16 = [&](uint32_t x) {
        out.push_back((uint8_t)x);
        out.push_back((uint8_t)(x >> 8));
    };
    auto u32 = [&](uint32_t x) {
        u16(x & 0xFFFF);
        u16(x >> 16);
    };
    auto tag = [&](const char *t) { out.insert(out.end(), t, t + 4); };
    tag("RIFF");
    u32(4 + 8 + 16 + 8 + payload + (payload & 1));
    tag("WAVE");
    tag("fmt ");
    u32(16);
    u16(1);
    u16(channels);
    u32(rate);
    u32(rate * channels * bits / 8);
    u16(channels * bits / 8);
    u16(bits);
    tag("data");
    u32(payload);
    for (size_t i = 0; i < frames * 2; i++) {
        double v = std::nearbyint((double)stereo48k[i] * 32768.0);
        v = std::min(std::max(v, -32768.0), 32767.0);
        int s = (int)v;
        u16((uint32_t)(uint16_t)(int16_t)s);
    }
    if (payload & 1) out.push_back(0);
    return out;
}

} // namespace at9
