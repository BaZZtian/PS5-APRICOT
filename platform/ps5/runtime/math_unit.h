// SPDX-License-Identifier: GPL-3.0-or-later
// Native integer services for the documented divider/square-root register ABI.
#pragma once
#include <stdint.h>
struct HostMath {
    uint32_t mode=0,sqrt_mode=0,root=0;
    uint64_t numerator=0,denominator=0,quotient=0,remainder=0,parameter=0;
    static uint32_t square_root(uint64_t value) {
        uint64_t result=0,bit=uint64_t(1)<<62;
        while(bit>value)bit>>=2;
        while(bit) {
            if(value>=result+bit) {value-=result+bit;result=(result>>1)+bit;}
            else result>>=1;
            bit>>=2;
        }
        return uint32_t(result);
    }
    bool divide() {
        if(mode==3)return false; // Reserved mode has no claimed behavior in this build.
        int64_t n=mode==0?int64_t(int32_t(numerator)):int64_t(numerator);
        int64_t d=mode==2?int64_t(denominator):int64_t(int32_t(denominator));
        bool overflow=false;
        if(!d) {quotient=n<0?1:~uint64_t(0);remainder=uint64_t(n);overflow=true;}
        else if(n==(mode==0?int64_t(-2147483647)-1:INT64_MIN)&&d==-1) {
            quotient=uint64_t(n);remainder=0;overflow=true;
        } else {quotient=uint64_t(n/d);remainder=uint64_t(n%d);}
        if(mode==0&&overflow)quotient^=0xffffffff00000000ull;
        return true;
    }
    bool access(uint32_t address,uint32_t width,uint32_t &value,bool write) {
        if((width!=2&&width!=4)||(address&(width-1)))return false;
        uint64_t mask=width==2?0xffff:0xffffffff;
        auto wide=[&](uint64_t &reg,uint32_t base,bool writable)->bool {
            if(address<base||address+width>base+8)return false;
            unsigned shift=(address-base)*8;
            if(write) {if(writable)reg=(reg&~(mask<<shift))|((uint64_t(value)&mask)<<shift);}
            else value=uint32_t((reg>>shift)&mask);
            return true;
        };
        if(address>=0x04000280&&address+width<=0x04000284) {
            if(write) {if(address==0x04000280)mode=value&3;return divide();}
            value=address==0x04000280?mode|(denominator==0?0x4000:0):0;return true;
        }
        if(wide(numerator,0x04000290,true)||wide(denominator,0x04000298,true))return !write||divide();
        if(wide(quotient,0x040002a0,false)||wide(remainder,0x040002a8,false))return true;
        if(address>=0x040002b0&&address+width<=0x040002b4) {
            if(write) {if(address==0x040002b0)sqrt_mode=value&1;root=square_root(sqrt_mode?parameter:uint32_t(parameter));}
            else value=address==0x040002b0?sqrt_mode:0;
            return true;
        }
        if(address>=0x040002b4&&address+width<=0x040002b8) {
            if(!write)value=(root>>((address-0x040002b4)*8))&uint32_t(mask);
            return true;
        }
        if(wide(parameter,0x040002b8,true)) {if(write)root=square_root(sqrt_mode?parameter:uint32_t(parameter));return true;}
        return false;
    }
};
