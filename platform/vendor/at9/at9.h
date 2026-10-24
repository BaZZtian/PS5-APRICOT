// PS5 snd0.at9 maker: audio decoding, excerpt preparation and ATRAC9 encoding.
//
// C++ port of PS5 AT9 Converter v0.1.0 (ps5at9/{cli,dsp,at9file,atrac9,
// atrac9_encoder}.py, https://github.com/blackbearreloaded/ps5-at9-converter).
// Copyright (C) 2026 BlackBearReloaded (original Python implementation)
// C++ port: part of PSGFX.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input decoding uses minimp3 (CC0) and dr_wav / dr_flac (public domain or
// MIT-0); the ATRAC9 tables come from LibAtrac9 (MIT). See NOTICES.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace at9 {

// Decoded audio: interleaved float samples in [-1, 1].
struct Audio {
    int rate = 0;
    int channels = 0;
    std::vector<float> pcm;
};

// Decode MP3, WAV (PCM 8/16/24/32-bit, float; RIFF/RF64), AIFF/AIFC or FLAC.
// The format is detected from the content, like the Python tool does.
bool decode_file(const std::vector<uint8_t> &bytes, Audio &out, std::string &err);

// Defaults mirror the Python CLI (ps5_at9.py) defaults.
struct Options {
    int bitrate_kbps = 192;      // 72, 96, 120, 144, 168 or 192
    bool loop = true;            // whole-track loop (smpl chunk); CLI --no-loop sets false
    double start_sec = 0;        // excerpt start
    double duration_sec = 15;    // excerpt length; <= 0 means "max" (longest that fits 2 MiB)
    double fade_in = 0.01;       // seconds
    double fade_out = 0.01;      // seconds
    double lufs = -28;           // integrated loudness target (param.json loudnessSnd0)
    double true_peak_db = -2;    // true-peak ceiling, dBTP
    bool normalize = true;       // false = keep source level, only limit peaks
};

struct Result {
    std::vector<uint8_t> at9;        // complete snd0.at9 file
    double loudness_lufs = 0;        // integrated loudness of the processed excerpt (-inf if silent)
    double seconds = 0;              // excerpt length
    std::string loudness_param;      // "%.2f" of loudness_lufs: value for pubtools.loudnessSnd0
    std::vector<float> processed;    // what was encoded: 48 kHz interleaved stereo, after
                                     // resample, cut, loudness, limiting and fades
};

// Whole CLI pipeline: to stereo, resample to 48 kHz, cut the excerpt, loudness
// normalise, true-peak limit, fades, ATRAC9 encode, RIFF build. Throws
// std::runtime_error (readable message) on bad options, an empty excerpt or a
// file above the 2 MiB PS5 limit.
Result make_snd0(const Audio &in, const Options &o);

// 16-bit PCM WAV of interleaved 48 kHz stereo float samples (for previews).
std::vector<uint8_t> wav_bytes(const std::vector<float> &stereo48k);

// Lower-level pieces, used by the test harness.
namespace detail {
// Same as make_snd0; optionally returns the processed excerpt in double
// precision (planar, 2 channels) and lets the caller choose the thread count
// (0 = hardware concurrency). Output bytes do not depend on the thread count.
Result make_snd0_ex(const Audio &in, const Options &o, std::vector<double> *left,
                    std::vector<double> *right, unsigned threads);
// ATRAC9-encode 48 kHz stereo (full scale = 1.0); returns the data chunk.
std::vector<uint8_t> encode_atrac9(const std::vector<double> &left, const std::vector<double> &right,
                                   int bitrate_kbps, unsigned threads);
// Complete .at9 file around a data chunk.
std::vector<uint8_t> build_at9(int bitrate_kbps, uint32_t sample_count, const std::vector<uint8_t> &data,
                               bool loop);
} // namespace detail

} // namespace at9
