// SPDX-License-Identifier: GPL-3.0-or-later
// Original packed GPU-state layout. Bytes are supplied at runtime, never built in.
#pragma once
#include <stdint.h>
#include <vector>
#include <cstring>
static inline std::vector<uint8_t> pack_video_state(const uint8_t *display,const uint8_t *banks,const uint8_t *vram,const uint8_t *palette,const uint8_t *oam,const uint8_t *raster=nullptr,const uint32_t *geometry=nullptr) {
    constexpr unsigned display_at=64,banks_at=display_at+224,vram_at=banks_at+12,palette_at=vram_at+0xa4000,oam_at=palette_at+2048,total=oam_at+2048;
    unsigned geometry_at=total+(raster?192*4332:0),bytes=geometry_at+(geometry?256*192*4:0);
    std::vector<uint8_t> out(bytes);uint32_t header[11]={display_at,banks_at,vram_at,palette_at,oam_at,256,192,bytes,raster?total:0,4332,geometry?geometry_at:0};
    for(unsigned n=0;n<11;++n)for(unsigned b=0;b<4;++b)out[n*4+b]=uint8_t(header[n]>>(b*8));
    std::memcpy(out.data()+display_at,display,224);std::memcpy(out.data()+banks_at,banks,9);
    std::memcpy(out.data()+vram_at,vram,0xa4000);std::memcpy(out.data()+palette_at,palette,2048);std::memcpy(out.data()+oam_at,oam,2048);
    if(raster)std::memcpy(out.data()+total,raster,192*4332);
    if(geometry)std::memcpy(out.data()+geometry_at,geometry,256*192*4);
    return out;
}
