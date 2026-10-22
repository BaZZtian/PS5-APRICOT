/* Copyright (C) 2026 Mihawk-99 */
/* Adapted for Orqenva, 2026-10-16. SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Video {
constexpr unsigned Width=3840,Height=2160,Blocks=(Width+127)/128,Rows=(Height+127)/128;
constexpr size_t FrameBytes=size_t(Blocks)*Rows*65536;
inline size_t tiled(unsigned x,unsigned y,unsigned blocks=Blocks) {
    const unsigned offset=((y<<4)&0x70)^((y<<5)&0xf00)^((y<<9)&0x1000)^((y<<8)&0x4000)^
        ((x<<2)&0xc)^((x<<5)&0x380)^((x<<4)&0x400)^((x<<6)&0x800)^((x<<9)&0xa000);
    return (size_t((y>>7)*blocks+(x>>7))<<16)+offset;
}
inline bool cursorTouchesTile(int x,int y,unsigned bx,unsigned by) {
    return x>=0&&y>=0&&x<int(bx*128+128)&&x+12>int(bx*128)&&y<int(by*128+128)&&y+19>int(by*128);
}
// Compare the exact source rectangle sampled by a 128x128 output tile.
// Each of the three display buffers has its own previous source snapshot.
inline bool tileUnchanged(const uint8_t* current,const uint8_t* previous,unsigned w,unsigned h,unsigned pitch,unsigned bx,unsigned by) {
    unsigned left=bx*128*w/Width,right=(std::min(Width,bx*128+128)-1)*w/Width;
    unsigned top=by*128*h/Height,bottom=(std::min(Height,by*128+128)-1)*h/Height;
    for(unsigned y=top;y<=bottom;y++) {
        size_t at=size_t(y)*pitch+left*4;
        if(memcmp(current+at,previous+at,(right-left+1)*4))return false;
    }
    return true;
}
}
