// SPDX-License-Identifier: GPL-3.0-or-later
// Original banked video-memory adapter. No renderer or game data.
#pragma once
#include <stdint.h>
#include <vector>
struct HostVram {
    static constexpr unsigned starts[9]={0,0x20000,0x40000,0x60000,0x80000,0x90000,0x94000,0x98000,0xa0000};
    static constexpr unsigned sizes[9]={0x20000,0x20000,0x20000,0x20000,0x10000,0x4000,0x4000,0x8000,0x4000};
    static constexpr uint8_t masks[9]={0x9b,0x9b,0x9f,0x9f,0x87,0x9f,0x9f,0x83,0x83};
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0xa4000);
    uint8_t control[9]={0,0,0x82,0,0,0,0,0,0}; // Loader assigns bank C to ARM7 for its autoload section.
    uint8_t status7() const {
        return uint8_t(((control[2]&0x87)==0x82?1:0)|((control[3]&0x87)==0x82?2:0));
    }
    // Resolve one CPU-visible byte to a physical bank. Several banks may overlap.
    bool locate(unsigned bank,unsigned core,uint32_t address,unsigned &physical) const {
        unsigned c=control[bank],mode=c&7,offset=(c>>3)&3,local=0;
        if(!(c&0x80))return false;
        auto span=[&](unsigned position,unsigned base,unsigned size) {
            if(position<base||position-base>=size)return false;
            local=position-base;return true;
        };
        bool found=false;
        if(core==7) {
            if((bank!=2&&bank!=3)||mode!=2||offset>1)return false;
            found=span(address&0x3ffff,offset*0x20000,0x20000);
        } else if(address>=0x06800000) {
            found=mode==0&&span((address-0x06800000)&0xfffff,starts[bank],sizes[bank]);
        } else {
            unsigned region=(address-0x06000000)>>21;
            if(region==0) {
                unsigned p=address&0x7ffff;
                if(bank<4&&mode==1)found=span(p,offset*0x20000,0x20000);
                if(bank==4&&mode==1)found=span(p,0,0x10000);
                if((bank==5||bank==6)&&mode==1)found=span(p,(offset&1)*0x4000+(offset&2)*0x8000,0x4000);
            } else if(region==1) {
                unsigned p=address&0x1ffff;
                if(bank==2&&mode==4)found=span(p,0,0x20000);
                if(bank==7&&mode==1)found=span(p&0xffff,0,0x8000);
                if(bank==8&&mode==1) {found=(p&0x8000)!=0;local=p&0x3fff;}
            } else if(region==2) {
                unsigned p=address&0x3ffff;
                if(bank<2&&mode==2&&offset<2)found=span(p,offset*0x20000,0x20000);
                if(bank==4&&mode==2)found=span(p,0,0x10000);
                if((bank==5||bank==6)&&mode==2)found=span(p,(offset&1)*0x4000+(offset&2)*0x8000,0x4000);
            } else if(region==3) {
                unsigned p=address&0x1ffff;
                if(bank==3&&mode==4)found=span(p,0,0x20000);
                if(bank==8&&mode==2) {found=true;local=p&0x3fff;}
            }
        }
        if(found)physical=starts[bank]+local;
        return found;
    }
    bool access(unsigned core,uint32_t address,unsigned width,uint32_t &value,bool writing) {
        if(address<0x06000000||address>=0x07000000||width>4||!width||width>0x07000000-address)return false;
        if(writing&&width==1&&core==9)return true;
        uint32_t result=0;
        for(unsigned n=0;n<width;++n) {
            uint8_t combined=0;
            for(unsigned bank=0;bank<9;++bank) {
                unsigned physical=0;
                if(!locate(bank,core,address+n,physical))continue;
                if(writing)bytes[physical]=uint8_t(value>>(8*n));
                else combined|=bytes[physical];
            }
            result|=uint32_t(combined)<<(8*n);
        }
        if(!writing)value=result;
        return true;
    }
};
