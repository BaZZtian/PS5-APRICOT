// SPDX-License-Identifier: GPL-3.0-or-later
// Original visible-line state capture for a native GPU compositor.
#pragma once
#include "display.h"
#include "vram.h"
#include "video_clock.h"
#include <vector>
#include <cstring>
struct HostRaster {
    static constexpr unsigned display_bytes=224,banks_at=224,palette_at=236,oam_at=2284,stride=4332;
    std::vector<uint8_t> working=std::vector<uint8_t>(192*stride),rows=std::vector<uint8_t>(192*stride);
    std::vector<uint8_t> vram=std::vector<uint8_t>(0xa4000),palette=std::vector<uint8_t>(2048),oam=std::vector<uint8_t>(2048);
    uint64_t frames=0;bool started=false,ready=false;
    void capture(unsigned line,const HostDisplay &display,const HostVram &video,const std::vector<uint8_t> &pal,const std::vector<uint8_t> &objects) {
        auto row=working.data()+line*stride;
        for(unsigned screen=0;screen<2;++screen)std::memcpy(row+screen*112,display.engine[screen],112);
        std::memcpy(row+banks_at,video.control,9);std::memcpy(row+palette_at,pal.data(),2048);std::memcpy(row+oam_at,objects.data(),2048);
    }
    void advance(uint64_t before,uint64_t after,const HostDisplay &display,const HostVram &video,const std::vector<uint8_t> &pal,const std::vector<uint8_t> &objects) {
        if(!started) {capture(0,display,video,pal,objects);started=true;}
        for(uint64_t at=(before/HostVideoClock::line_cycles+1)*HostVideoClock::line_cycles;at<=after;at+=HostVideoClock::line_cycles) {
            unsigned line=unsigned((at/HostVideoClock::line_cycles)%263);
            if(line<192)capture(line,display,video,pal,objects);
            if(line==192) {rows=working;vram=video.bytes;palette=pal;oam=objects;ready=true;++frames;}
        }
    }
};
