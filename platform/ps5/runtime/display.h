// SPDX-License-Identifier: GPL-3.0-or-later
// Original 2D register state for a future native rendering backend.
// Configuring registers is supported; producing a frame is not claimed here.
#pragma once
#include <stdint.h>
struct HostDisplay {
    uint8_t engine[2][0x70]{};
    uint32_t capture=0;
    uint16_t control3d=0;
    static uint32_t mask(unsigned screen,unsigned offset) {
        if(offset<4)return ((screen?0xc0b3fff7u:0xffffffffu)>>(8*offset))&255;
        if(offset>=8&&offset<0x10)return 255;
        if(offset>=0x10&&offset<0x20)return offset&1?1:255;
        if(offset>=0x20&&offset<0x40) {
            unsigned part=(offset-0x20)%16;
            return part==11||part==15?15:255;
        }
        if(offset>=0x40&&offset<0x48)return 255;
        if(offset>=0x48&&offset<0x4c)return 63;
        if(offset>=0x4c&&offset<0x4e)return 255;
        if(offset>=0x50&&offset<0x52)return offset&1?63:255;
        if(offset>=0x52&&offset<0x54)return 31;
        if(offset==0x54)return 31;
        if(offset==0x6c)return 31;
        if(offset==0x6d)return 0xc0;
        return 0;
    }
    bool access(uint32_t address,unsigned width,uint32_t &value,bool writing) {
        if(width!=1&&width!=2&&width!=4)return false;
        unsigned screen=address>=0x04001000?1:0;
        uint32_t base=screen?0x04001000:0x04000000;
        unsigned offset=address-base;
        bool ordinary=(offset<4&&width<=4-offset)||
            (screen&&offset>=4&&offset<0x70&&width<=0x70-offset)||
            (offset>=8&&offset<0x60&&width<=0x60-offset)||
            (offset>=0x6c&&offset<0x70&&width<=0x70-offset);
        if(ordinary) {
            uint32_t result=0;
            for(unsigned n=0;n<width;++n) {
                if(writing)engine[screen][offset+n]=uint8_t((value>>(8*n))&mask(screen,offset+n));
                else result|=uint32_t(engine[screen][offset+n])<<(8*n);
            }
            if(!writing)value=result;
            return true;
        }
        if(address>=0x04000060&&address<0x04000064&&width<=0x04000064-address) {
            unsigned shift=(address-0x04000060)*8;
            uint32_t span=width==4?0xffffffffu:(1u<<(8*width))-1;
            if(writing)control3d=uint16_t(((control3d&~(span<<shift))|((value&span)<<shift))&0x4fff);
            else value=(uint32_t(control3d)>>shift)&span;
            return true;
        }
        if(address==0x04000064&&width==4) {
            if(writing) {if(value&0x80000000)return false;capture=value&0x7f3f1f1f;}
            else value=capture;
            return true;
        }
        return false;
    }
};
