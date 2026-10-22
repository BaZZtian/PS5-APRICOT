// SPDX-License-Identifier: GPL-3.0-or-later
// Original digital radio initialization services. No firmware/calibration dump,
// packet transport, RF connection or peer is supplied by this adapter.
#pragma once
#include <array>
#include <stdint.h>
struct HostWireless {
    std::array<uint16_t,0x180> regs{},masks{},initial{};
    std::array<bool,0x180> known{};
    std::array<uint8_t,8192> ram{};
    std::array<uint8_t,256> bb{},rf3{};
    std::array<uint32_t,32> rf2{};
    uint64_t microseconds=0,phase=0,random_cycles=0,accesses=0;
    uint16_t random=1,random_latched=1;
    bool irq_level=false,irq_edge=false;
    const char *error="none";
    uint16_t &r(unsigned offset) {return regs[offset/2];}
    void define(unsigned offset,unsigned mask,unsigned value=0) {
        known[offset/2]=true;masks[offset/2]=uint16_t(mask);initial[offset/2]=regs[offset/2]=uint16_t(value);
    }
    HostWireless() {
        define(0,0,0x1440);define(4,0x9fff);define(6,0x7f);
        for(unsigned a:{8u,10u,0x18u,0x1au,0x1cu,0x20u,0x22u,0x24u})define(a,0xffff);
        define(0x10,0xfbff);define(0x12,0xffff);
        define(0x28,15);define(0x2a,0x7ff);define(0x2c,0xffff,0x0707);define(0x2e,1);
        define(0x30,0xff0e);define(0x32,0xffff);define(0x34,0);
        define(0x36,3,1);define(0x38,7,3);define(0x3c,3,0x200);define(0x40,0x8001);
        define(0x44,0);define(0x48,3);define(0x50,0xffff,0x4000);define(0x52,0xffff,0x4800);
        define(0x54,0);define(0x56,0xfff);define(0x58,0x1ffe);define(0x5a,0xfff);define(0x5c,0xfff);
        define(0x60,0);define(0x62,0x1ffe);define(0x64,0xfff);define(0x68,0x1ffe);define(0x6c,0xfff);
        define(0x70,0xffff);define(0x74,0x1ffe);define(0x76,0xfff);define(0x78,0);
        define(0x80,0xffff);define(0x84,0xff);define(0x88,0xff);define(0x8c,0x3ff,0x64);define(0x8e,0xff);
        for(unsigned a:{0x90u,0x94u,0xa0u,0xa4u,0xa8u,0xc0u,0xc4u})define(a,0xffff);
        define(0x98,0);define(0x9c,0xffff,0x50);define(0xac,0);define(0xae,0);define(0xb0,0,0x10);
        define(0xb4,0);define(0xb6,0);define(0xb8,0);define(0xba,0);define(0xbc,3,1);define(0xc8,0);
        define(0xd0,0x1fff,0x401);define(0xd4,3,1);define(0xd8,0xfff,4);define(0xda,0xffff,0x602);define(0xe0,15,8);
        define(0xe8,1);define(0xea,1);define(0xec,0x3f1f,0x3f03);define(0xee,1,1);
        define(0xf0,0xfc00,0xfc00);for(unsigned a=0xf2;a<0xf8;a+=2)define(a,0xffff,0xffff);
        for(unsigned a=0xf8;a<0x100;a+=2)define(a,0xffff);
        for(unsigned a=0x100;a<=0x106;a+=2)define(a,0);
        for(unsigned a:{0x10cu,0x110u,0x118u,0x11cu,0x122u,0x124u,0x128u,0x140u})define(a,0xffff);
        define(0x120,0x81ff,0x48);define(0x122,0xffff,0x4840);define(0x130,0xfff,0x142);define(0x132,0x8fff,0x8064);
        define(0x134,0xffff,0xffff);define(0x142,0xffff,0x2443);define(0x144,0xff,0x42);
        for(unsigned a:{0x146u,0x148u,0x14au})define(a,0xff,0x16);
        define(0x14c,0xffff,0x162c);define(0x150,0xff3f,0x204);define(0x154,0x7a7f,0x58);
        define(0x158,0);define(0x15a,0xff);define(0x15c,0);define(0x15e,0);define(0x160,0x4100,0x100);define(0x168,0x800f,0x800d);
        for(unsigned a:{0x16au,0x170u,0x172u,0x174u,0x176u,0x178u})define(a,0);
        define(0x17c,0xffff,0x800);define(0x17e,0xffff,0xc008);define(0x180,0);define(0x184,0x413f,0x18);
        define(0x190,0xffff);define(0x194,7);define(0x198,15);define(0x19c,0,4);
        define(0x1a0,0x933);define(0x1a2,3,1);define(0x1a4,0xffff);
        define(0x1a8,0);define(0x1aa,0xffff);define(0x1ac,0);define(0x1ae,0xffff);
        for(unsigned a=0x1b0;a<=0x1c0;a+=2)define(a,(a==0x1b2||a==0x1b6||a==0x1bc||a==0x1be)?0xffff:0xff);
        define(0x1c4,0);for(unsigned a=0x1d0;a<=0x1de;a+=2)define(a,a==0x1d0?0xff00:0xffff);
        define(0x1f0,3);define(0x204,0);define(0x208,0);define(0x20c,0);define(0x210,0);define(0x214,0,9);
        define(0x21c,0);define(0x220,0xffff);define(0x224,3,3);define(0x228,0);define(0x230,0xff,0x47);
        define(0x234,0xeff,0xeff);define(0x238,0xffff);define(0x23c,0);define(0x244,0xffff);define(0x248,0xffff);
        for(unsigned a:{0x24cu,0x24eu,0x250u,0x258u,0x25cu,0x264u,0x270u})define(a,0);
        define(0x254,0,0xffff);define(0x260,0,0xfef);define(0x268,0,5);define(0x274,0,1);define(0x278,0xffff,15);define(0x27c,0,10);
        define(0x290,0,0xffff);define(0x298,0);define(0x2a0,0xffff);define(0x2a2,0,0x7fff);define(0x2a4,0);
        define(0x2a8,0);define(0x2ac,0,0x38);define(0x2b0,0);define(0x2b4,0x103);define(0x2b8,0);define(0x2c0,1);
        define(0x2c4,0,10);define(0x2c8,0);define(0x2cc,0);define(0x2d0,0);
        for(unsigned a=0x2f0;a<=0x2f6;a+=2)define(a,0xffff);
        bb[0]=0x6d;bb[0x5d]=1;bb[0x64]=0xff;
    }
    static uint16_t random_step(uint16_t value) {return uint16_t((((value<<1)|(value>>10))^(value&1))&0x7ff);}
    static uint16_t random_jump(uint16_t value,uint64_t count) {
        std::array<uint16_t,11> map{};for(unsigned n=0;n<11;++n)map[n]=random_step(uint16_t(1u<<n));
        auto apply=[](const std::array<uint16_t,11> &m,uint16_t v) {uint16_t out=0;for(unsigned n=0;n<11;++n)if(v&(1u<<n))out^=m[n];return out;};
        while(count) {if(count&1)value=apply(map,value);auto previous=map;for(unsigned n=0;n<11;++n)map[n]=apply(previous,previous[n]);count>>=1;}return value;
    }
    bool bb_writable(unsigned n) const {
        return n>=1&&n<=12||n>=0x13&&n<=0x15||n>=0x1b&&n<=0x26||n>=0x28&&n<=0x4c||n>=0x4e&&n<=0x5c||n==0x62||n==0x63||n==0x65||n==0x67||n==0x68;
    }
    void wake() {r(0x3c)&=~0x300u;r(0x10)|=0x800;r(0x19c)=4;r(0x214)=9;}
    bool transmitting() const {
        if((regs[0x90/2]&0x8000)&&(regs[0xb0/2]&8))return true;
        if(!(regs[4/2]&1))return false;
        for(unsigned n=0;n<3;++n)if((regs[(0xa0+n*4)/2]&0x8000)&&(regs[0xb0/2]&(1u<<n)))return true;
        return (regs[0x80/2]&0x8000)&&(regs[0xb0/2]&16);
    }
    void update_irq() {
        bool level=(r(0x10)&r(0x12))!=0;
        if(level&&!irq_level)irq_edge=true;irq_level=level;
    }
    void advance(uint64_t cycles,uint32_t &irq,bool powered) {
        if(!powered)return;random_cycles+=cycles;
        if((r(0xe8)&1)&&!(r(0x36)&1)) {
            uint64_t before=microseconds;phase+=cycles*1000000u;microseconds+=phase/33513982u;phase%=33513982u;
            uint64_t compare=0;for(unsigned n=0;n<4;++n)compare|=uint64_t(r(0xf0+n*2))<<(n*16);
            if((r(0xea)&1)&&compare>before&&compare<=microseconds)r(0x10)|=0x4000;
        }
        update_irq();if(irq_edge) {irq|=1u<<24;irq_edge=false;}
    }
    bool half(unsigned offset,uint16_t &value,bool writing) {
        if(offset>=0x4000&&offset<0x6000) {
            unsigned at=offset-0x4000;
            if(writing) {ram[at]=uint8_t(value);ram[at+1]=uint8_t(value>>8);}
            else value=uint16_t(ram[at]|unsigned(ram[at+1])<<8);return true;
        }
        if(offset>=0x300||!known[offset/2]) {error="unknown wireless register";return false;}
        if(!writing) {
            if(offset==0x44) {value=random_latched;random=random_jump(random,random_cycles);random_cycles=0;random_latched=random;return true;}
            if(offset>=0xf8&&offset<=0xfe) {value=uint16_t(microseconds>>((offset-0xf8)*8));return true;}
            if(offset==0x158)offset=0x15c;if(offset==0x178)offset=0x17c;if(offset==0x78)offset=0x68;
            if(offset==0x60) {
                unsigned at=r(0x58)&0x1ffe;value=uint16_t(ram[at]|unsigned(ram[at+1])<<8);r(0x58)=(at+2)&0x1ffe;
                if(r(0x5c)) {--r(0x5c);if(!r(0x5c))r(0x10)|=0x200;}return true;
            }
            value=r(offset);return true;
        }
        if(offset==0x10) {r(offset)&=~value;return true;}
        if(offset==0x21c) {r(0x10)|=value&0xfbff;return true;}
        if(offset==0xac||offset==0xae) {if(offset==0xac)r(0xb0)&=~(value&31);else r(0xb0)|=value&31;}
        else if(offset==0xb4) {
            for(unsigned n=0;n<3;++n)if(value&(1u<<n))r(0xa0+n*4)&=0x7fff;
            if(value&8)r(0x90)&=0x7fff;if(value&16)r(0x80)&=0x7fff;
        } else if(offset==0x158) {
            // A disabled serial port starts no transfer and leaves its previous
            // receive latch intact. Completion timing remains provisional.
            if((r(0x168)&0x800f)||(r(0x36)&1))return true;
            unsigned index=value&255,command=value>>12;if(value&0xf00||(command!=5&&command!=6)) {error="unknown baseband command";return false;}
            if(command==5) {if(bb_writable(index))bb[index]=uint8_t(r(0x15a));}else r(0x15c)=bb[index];
        } else if(offset==0x70) {
            unsigned at=r(0x68)&0x1ffe;ram[at]=uint8_t(value);ram[at+1]=uint8_t(value>>8);
            unsigned next=(at+2)&0x1ffe;if(next==r(0x74))next=(next+r(0x76)*2)&0x1ffe;r(0x68)=next;
            if(r(0x6c)) {--r(0x6c);if(!r(0x6c))r(0x10)|=0x100;}
        } else if(offset>=0xf8&&offset<=0xfe) {
            unsigned shift=(offset-0xf8)*8;microseconds=(microseconds&~(uint64_t(65535)<<shift))|uint64_t(value)<<shift;
        } else if(offset==0x2d0) {
            if(!(r(0x3c)&0x100)||value) {error="inactive wireless power acknowledge";return false;}wake();
        } else {
            uint16_t previous=r(offset);r(offset)=uint16_t((previous&~masks[offset/2])|(value&masks[offset/2]));
            if(offset==4) {
                if(value&0x2000) {for(unsigned a:{0x56u,0xc0u,0xc4u,0x1a4u})r(a)=0;r(0x278)=15;}
                if(value&0x4000)for(unsigned a:{6u,8u,10u,0x18u,0x1au,0x1cu,0x20u,0x22u,0x24u,0x28u,0x2au,0x2cu,0x2eu,0x50u,0x52u,0x84u,0xbcu,0xd0u,0xd4u,0xe0u,0xecu,0x194u,0x198u,0x1a2u,0x224u,0x230u})r(a)=initial[a/2];
                if(!(previous&1)&&(value&1)) {r(0x34)=2;r(0x19c)=0x46;r(0x214)=9;r(0x27c)=5;}
                if((previous&1)&&!(value&1))r(0x27c)=10;
            }
            if(offset==0x30) {if(value&1)r(0x54)=r(0x56);if(value&128) {r(0x98)=r(0x94);r(0x94)=0;}}
            if(offset==0x3c&&(value&2)&&!(r(0x36)&1)&&(r(4)&1))wake();
            if(offset==0x40&&(value&0x8000)) {
                if(value&1) {r(0x3c)=(r(0x3c)&3)|0x200;r(0x34)=2;r(0xb0)=0;r(0x19c)=0x46;r(0x214)=9;}
                else if(r(0x3c)&0x200)r(0x3c)|=0x100;
            }
            if(offset==0x17c) {
                unsigned length=r(0x184)&63;
                if(length==24&&!(r(0x184)&0x100)) {
                    unsigned index=(value>>2)&31;
                    if(value&128) {r(0x17e)=uint16_t(rf2[index]);r(0x17c)=uint16_t((value&~3)|(rf2[index]>>16));}
                    else rf2[index]=(uint32_t(value&3)<<16)|r(0x17e);
                } else if(length==20&&(r(0x184)&0x100)) {
                    unsigned index=(r(0x17e)>>8)&63;
                    if((value&15)==5)rf3[index]=uint8_t(r(0x17e));
                    else if((value&15)==6)r(0x17e)=uint16_t((r(0x17e)&0xff00)|rf3[index]);
                    else {error="unknown RF command";return false;}
                } else {error="unsupported RF framing";return false;}
            }
        }
        if(transmitting()) {error="wireless packet transport unavailable";return false;}return true;
    }
    bool access(uint32_t address,unsigned width,uint32_t &value,bool writing,bool powered) {
        if(address<0x04800000||address>=0x04810000||(width!=1&&width!=2&&width!=4))return false;
        ++accesses;if(!powered)return writing;
        if(width==1&&writing)return true; // Hardware ignores STRB, including RAM.
        unsigned offset=(address-0x04800000)&0x7fff;
        if(width!=1&&(offset&1))return false;
        if(width==1) {uint16_t v=0;if(!half(offset&~1u,v,false))return false;update_irq();value=(v>>((offset&1)*8))&255;return true;}
        uint32_t result=0;for(unsigned n=0;n<width;n+=2) {uint16_t v=uint16_t(value>>(n*8));if(!half(offset+n,v,writing))return false;update_irq();result|=uint32_t(v)<<(n*8);}
        if(!writing)value=result;return true;
    }
};
