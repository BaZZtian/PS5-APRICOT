// SPDX-License-Identifier: GPL-3.0-or-later
// Original analytic substitutes, not BIOS table bytes. These follow documented
// ranges/scales; exhaustive bit-for-bit agreement with a BIOS is not established.
#pragma once
#include <stdint.h>
struct HostSoundMath {
    static double power_two(double x) {
        int whole=int(x);if(x<whole)--whole;
        double reduced=(x-whole)*0.69314718055994530942,term=1,result=1;
        for(unsigned n=1;n<=14;++n) {term*=reduced/n;result+=term;}
        while(whole<0) {result*=0.5;++whole;}
        while(whole>0) {result*=2;--whole;}
        return result;
    }
    static unsigned divisor(unsigned index) {
        int db=int(index)-723;
        return db< -240?16:db< -120?4:db< -60?2:1;
    }
    static bool service(unsigned number,unsigned index,uint32_t &value) {
        if(number==0x1a) {
            if(index>=64)return false;
            double x=index*0.02454369260617025968,term=x,result=x;
            for(unsigned n=1;n<=12;++n) {term*=-x*x/((2*n)*(2*n+1));result+=term;}
            value=uint32_t(result*32767+0.5);return true;
        }
        if(number==0x1b) {
            if(index>=768)return false;
            value=uint32_t((power_two(index/768.0)-1)*65536+0.5);return true;
        }
        if(number==0x1c) {
            if(index>=724)return false;
            double amplitude=128*power_two((int(index)-723)*3.32192809488736234787/200)*divisor(index);
            value=uint32_t(amplitude+0.5);if(value>127)value=127;return true;
        }
        return false;
    }
};
