// SPDX-License-Identifier: GPL-3.0-or-later
// Private runtime snapshots only. No fixed game data.
#pragma once
#include "geometry.h"
static inline std::vector<uint8_t> pack_geometry_state(const HostGeometry &g) {
    uint32_t count=uint32_t(g.completed.size()),polygons=64,banks=polygons+count*sizeof(GeometryPolygon);
    uint32_t vram=banks+12,settings=vram+0xa4000,total=settings+160;
    std::vector<uint8_t> out(total,0);
    uint32_t header[10]={count,polygons,banks,vram,settings,g.completed_control,g.completed_parameters,total,256,192};
    for(unsigned n=0;n<10;++n)for(unsigned b=0;b<4;++b)out[n*4+b]=uint8_t(header[n]>>(8*b));
    if(count)std::memcpy(out.data()+polygons,g.completed.data(),count*sizeof(GeometryPolygon));
    std::memcpy(out.data()+banks,g.completed_banks,9);std::memcpy(out.data()+vram,g.completed_vram.data(),0xa4000);
    std::memcpy(out.data()+settings,g.completed_settings,160);
    out[settings]=uint8_t(g.completed_dot_depth);out[settings+1]=uint8_t(g.completed_dot_depth>>8);return out;
}
