// SPDX-License-Identifier: MIT
#pragma once
#include "controller_art.hpp"
#include <cstddef>
#include <vector>
namespace apricot {
enum class ControlIcon : unsigned {Cross, Circle, Square, Triangle, Options, L1, R1,
    R2, LeftStick, RightStick, Touchpad, Dpad, L3, R3, Count};
// Input pixels are straight-alpha RGBA; callers explicitly select their canvas order.
inline void controller_icon(std::vector<uint32_t>& canvas, unsigned width, unsigned height,
                            int left, int top, ControlIcon icon, bool bgra=false) {
    if (!width || !height || canvas.size()/width < height || unsigned(icon)>=unsigned(ControlIcon::Count)) return;
    const auto* pixels=controller_pixels+unsigned(icon)*40*40;
    for(int y=0;y<40;++y)for(int x=0;x<40;++x){
        const int px=left+x,py=top+y;
        if(px<0||py<0||unsigned(px)>=width||unsigned(py)>=height)continue;
        uint32_t s=pixels[y*40+x];const unsigned a=s>>24;
        if(!a)continue;
        if(bgra)s=(s&0xff00ff00u)|((s&255)<<16)|((s>>16)&255);
        auto& d=canvas[size_t(py)*width+unsigned(px)];
        const unsigned da=d>>24,alpha=a+(da*(255-a)+127)/255;
        uint32_t out=alpha<<24;
        for(unsigned shift:{0u,8u,16u}){
            const unsigned premul=((s>>shift)&255)*a+(((d>>shift)&255)*da*(255-a)+127)/255;
            out|=((premul+alpha/2)/alpha)<<shift;
        }
        d=out;
    }
}
}
