/* Copyright (C) 2026 Mihawk-99 */
/* Modified for Orqenva, 2026-10-16: 4K output and source cache state. */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PS5_GPU_DISPLAY_H
#define PS5_GPU_DISPLAY_H
#include <stdint.h>
#include <stdio.h>
#include <vector>
/* Only the owning title uses this display. Input is completed, linear RGBA8. */
struct Ps5GpuDisplay {
    int handle=-1, back=0;
    int64_t physical=-1;
    void *mapped=nullptr;
    uint64_t flips=0,last_copy_ns=0;
    bool registered=false;
    std::vector<uint8_t> source_cache[3];
    std::vector<uint8_t> native_background_rgba;
    bool set_background(std::vector<uint8_t> image) {
        if(!image.empty()&&image.size()!=3840u*2160u*4u)return false;
        native_background_rgba.swap(image);
        for(auto& cache:source_cache)cache.clear();
        return true;
    }
    unsigned cache_w[3]{},cache_h[3]{},cache_pitch[3]{};
    bool cache_bgra[3]{};
    int cache_cursor_x[3]={-1,-1,-1},cache_cursor_y[3]={-1,-1,-1};
    /* cursor_x, cursor_y: where to draw the pointer on the 3840x2160 screen, or -1 for none. */
    int present(const void *pixels,unsigned width,unsigned height,unsigned pitch,FILE *log,const char *capture,bool bgra=false,
                int cursor_x=-1,int cursor_y=-1);
    bool close(FILE *log);
    /* A picture of the buffer the last present flipped to, written to path. */
    bool capture_last(const char *path);
};
#endif
