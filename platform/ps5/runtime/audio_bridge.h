// SPDX-License-Identifier: GPL-3.0-or-later
// Original non-blocking single-producer/single-consumer audio bridge.
#pragma once
#include <atomic>
#include <stdint.h>
#include <algorithm>
struct NativeAudioBridge {
    struct Sample {int16_t l,r;};
    static constexpr uint32_t capacity=8192,denominator=48000*1024,numerator=33513982;
    static constexpr uint32_t resume_samples=1024,start_samples=3072,low_samples=1536,fade_frames=128;
    Sample ring[capacity]{};std::atomic<uint32_t> written{0},read{0};
    std::atomic<uint32_t> overruns{0},underruns{0};
    std::atomic<uint32_t> starvations{0};
    Sample previous{},next{};uint32_t phase=0;bool primed=false;
    Sample last_output{},fade_start{};uint32_t gain=0,fade_remaining=0;
    uint32_t queued() const {auto r=read.load(std::memory_order_acquire),w=written.load(std::memory_order_acquire);return w-r;}
    static void push(void *context,int16_t left,int16_t right) {
        auto &q=*static_cast<NativeAudioBridge*>(context);auto w=q.written.load(std::memory_order_relaxed),r=q.read.load(std::memory_order_acquire);
        if(w-r>=capacity) {q.overruns.fetch_add(1,std::memory_order_relaxed);return;}
        q.ring[w&(capacity-1)]={left,right};q.written.store(w+1,std::memory_order_release);
    }
    bool pop(Sample &sample) {
        auto r=read.load(std::memory_order_relaxed),w=written.load(std::memory_order_acquire);if(r==w)return false;
        sample=ring[r&(capacity-1)];read.store(r+1,std::memory_order_release);return true;
    }
    static void fill(int16_t *output,int frames,void *context) {
        auto &q=*static_cast<NativeAudioBridge*>(context);
        for(int n=0;n<frames;++n) {
            if(!q.primed) {
                auto r=q.read.load(std::memory_order_relaxed),w=q.written.load(std::memory_order_acquire);
                if(w-r<resume_samples||q.fade_remaining) {
                    q.last_output={int16_t(int64_t(q.fade_start.l)*q.fade_remaining/fade_frames),int16_t(int64_t(q.fade_start.r)*q.fade_remaining/fade_frames)};
                    if(q.fade_remaining)--q.fade_remaining;
                    output[n*2]=q.last_output.l;output[n*2+1]=q.last_output.r;
                    q.underruns.fetch_add(1,std::memory_order_relaxed);continue;
                }
                q.pop(q.previous);q.pop(q.next);q.phase=0;q.primed=true;q.gain=0;
            }
            if(q.gain<fade_frames)++q.gain;
            output[n*2]=int16_t(((int64_t(q.previous.l)*(denominator-q.phase)+int64_t(q.next.l)*q.phase)/denominator)*q.gain/fade_frames);
            output[n*2+1]=int16_t(((int64_t(q.previous.r)*(denominator-q.phase)+int64_t(q.next.r)*q.phase)/denominator)*q.gain/fade_frames);
            q.last_output={output[n*2],output[n*2+1]};
            q.phase+=numerator;
            if(q.phase>=denominator) {
                q.phase-=denominator;q.previous=q.next;
                if(!q.pop(q.next)) {q.primed=false;q.fade_start=q.last_output;q.fade_remaining=fade_frames;q.starvations.fetch_add(1,std::memory_order_relaxed);q.underruns.fetch_add(1,std::memory_order_relaxed);}
            }
        }
    }
};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"Audio callback requires lock-free indices");
