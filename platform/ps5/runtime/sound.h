// SPDX-License-Identifier: GPL-3.0-or-later
// Original native sound-device adapter. All sample data comes from guest memory.
// Capture and manual PCM termination are deliberately unsupported. The output
// clock is the base hardware clock / 1024; a platform sink must resample it.
#pragma once
#include <stdint.h>
#include <array>
struct HostSound {
    using Reader=bool (*)(void*,uint32_t,unsigned,uint32_t*);
    using Sink=void (*)(void*,int16_t,int16_t);
    void *context=nullptr,*sink_context=nullptr;Reader read=nullptr;Sink sink=nullptr;
    uint8_t regs[0x120]{};
    uint64_t frames=0,nonzero_frames=0;unsigned output_phase=0,peak=0;
    uint32_t fault_address=0;bool failed=false;
    struct Channel {
        uint32_t source=0,offset=0,end=0,loop=0;
        uint64_t phase=0;
        int sample=0,predictor=0,index=0,loop_predictor=0,loop_index=0;
        unsigned format=0,startup=0,nibble=0,wave=0,noise=0x7fff;
        bool playing=false,tail=false,hung=false,loop_saved=false;
    } channels[16];
    HostSound() {regs[0x105]=2;}
    uint32_t reg(unsigned offset,unsigned width=4) const {
        uint32_t value=0;for(unsigned n=0;n<width;++n)value|=uint32_t(regs[offset+n])<<(n*8);return value;
    }
    void set(unsigned offset,uint32_t value,unsigned width=4) {
        for(unsigned n=0;n<width;++n)regs[offset+n]=uint8_t(value>>(n*8));
    }
    bool memory(uint32_t address,unsigned width,uint32_t &value) {
        if(!read||!read(context,address,width,&value)) {failed=true;fault_address=address;return false;}
        return true;
    }
    static unsigned level(unsigned value) {return value==127?128:value;}
    static int signed16(uint32_t value) {return int(value&0x7fff)-int(value&0x8000);}
    static unsigned step(unsigned index) {
        // Generate the standard IMA quantizer numerically, without BIOS bytes.
        static const auto table=[] {
            std::array<unsigned,89> result{};uint64_t q=0x776d2;
            for(unsigned n=0;n<89;++n) {result[n]=unsigned(q>>16);q+=q/10;}
            result[3]=10;result[4]=11;result[88]=32767;return result;
        }();
        return table[index];
    }
    static int decode(unsigned nibble,int &predictor,int &index) {
        unsigned size=step(unsigned(index)),delta=size>>3;
        if(nibble&1)delta+=size>>2;if(nibble&2)delta+=size>>1;if(nibble&4)delta+=size;
        if(nibble&8) {predictor-=int(delta);if(predictor< -32767)predictor=-32767;}
        else {predictor+=int(delta);if(predictor>32767)predictor=32767;}
        int change=nibble&7;change=change<4?-1:(change-3)*2;
        index+=change;if(index<0)index=0;if(index>88)index=88;
        return predictor;
    }
    bool start(unsigned number) {
        auto &c=channels[number];unsigned base=number*16;uint32_t control=reg(base);
        unsigned format=(control>>29)&3,repeat=(control>>27)&3;
        if((format==3&&number<8)||(format!=3&&(repeat==0||repeat==3)))return false;
        int held=(control&0x8000)?c.sample:0;c=Channel{};c.sample=held;c.playing=true;c.format=format;
        c.source=reg(base+4)&0x07fffffc;c.loop=reg(base+10,2)*4;
        c.end=c.loop+(reg(base+12)&0x3fffff)*4;c.hung=format!=3&&c.end<16;
        c.startup=format==3?1:format==2?11:3;
        if(format==2&&!c.hung) {
            uint32_t header=0;if(!memory(c.source,4,header))return false;
            c.predictor=signed16(header);c.index=(header>>16)&127;
            if(c.index>88)return false;c.offset=4;
        }
        return true;
    }
    bool sample(unsigned number) {
        auto &c=channels[number];unsigned base=number*16;uint32_t control=reg(base);
        if(c.tail) {if(!(control&0x8000)) {c.sample=0;c.tail=false;}return true;}
        if(!c.playing||c.hung)return true;
        if(c.startup) {--c.startup;if(c.startup) {c.sample=0;return true;}}
        if(c.format==3) {
            if(number>=14) {
                unsigned carry=c.noise&1;c.noise>>=1;if(carry)c.noise^=0x6000;
                c.sample=carry?-32767:32767;
            } else {
                unsigned duty=(control>>24)&7;
                c.sample=duty!=7&&c.wave>=7-duty?32767:-32767;c.wave=(c.wave+1)&7;
            }
            return true;
        }
        uint32_t value=0;
        if(c.format==0) {
            if(!memory(c.source+c.offset,1,value))return false;
            c.sample=(int(value&127)-int(value&128))*256;++c.offset;
        } else if(c.format==1) {
            if(!memory(c.source+c.offset,2,value))return false;
            c.sample=signed16(value);c.offset+=2;
        } else {
            if(c.offset==c.loop&&!c.nibble) {c.loop_predictor=c.predictor;c.loop_index=c.index;c.loop_saved=true;}
            if(!memory(c.source+c.offset,1,value))return false;
            c.sample=decode((value>>(c.nibble*4))&15,c.predictor,c.index);
            if(c.nibble)++c.offset;c.nibble^=1;
        }
        if(c.offset>=c.end) {
            if(((control>>27)&3)==1) {
                if(c.loop>=c.end)return false;
                c.offset=c.loop;
                if(c.format==2) {
                    if(!c.loop_saved)return false;
                    c.predictor=c.loop_predictor;c.index=c.loop_index;c.nibble=0;
                }
            } else {
                c.playing=false;c.tail=true;set(base,control&~0x80000000u);
            }
        }
        return true;
    }
    void mix() {
        uint32_t master=reg(0x100,2);int64_t side[2][16]{};
        if(master&0x8000)for(unsigned n=0;n<16;++n) {
            uint32_t control=reg(n*16);unsigned divide=(control>>8)&3;
            unsigned divisor=divide==3?16:1u<<divide,volume=level(control&127),pan=level((control>>16)&127);
            int64_t amplitude=int64_t(channels[n].sample)*16/int(divisor)*volume;
            side[0][n]=(amplitude*int(128-pan))>>10;side[1][n]=(amplitude*int(pan))>>10;
        }
        int16_t output[2]{};
        for(unsigned s=0;s<2;++s) {
            unsigned route=(master>>(8+s*2))&3;int64_t sum=0;
            if(route) {if(route&1)sum+=side[s][1];if(route&2)sum+=side[s][3];}
            else for(unsigned n=0;n<16;++n)if(!((n==1&&(master&0x1000))||(n==3&&(master&0x2000))))sum+=side[s][n];
            int dac=int((sum*int64_t(level(master&127)))>>21)+int(reg(0x104,2)&1023);
            if(dac<0)dac=0;if(dac>1023)dac=1023;output[s]=int16_t((dac-512)*64);
            unsigned absolute=output[s]<0?unsigned(-int(output[s])):unsigned(output[s]);if(absolute>peak)peak=absolute;
        }
        ++frames;if(output[0]||output[1])++nonzero_frames;
        if(sink)sink(sink_context,output[0],output[1]);
    }
    bool advance(uint64_t cycles) {
        while(cycles) {
            unsigned chunk=unsigned(cycles<1024-output_phase?cycles:1024-output_phase);
            for(unsigned n=0;n<16;++n) {
                auto &c=channels[n];if(!c.playing&&!c.tail)continue;
                uint64_t period=uint64_t(65536-reg(n*16+8,2))*2;
                c.phase+=chunk;
                while(c.phase>=period) {c.phase-=period;if(!sample(n)) {failed=true;return false;}}
            }
            output_phase+=chunk;cycles-=chunk;
            if(output_phase==1024) {output_phase=0;mix();}
        }
        return true;
    }
    bool access(uint32_t address,unsigned width,uint32_t &value,bool writing) {
        if(address<0x04000400||address>=0x04000520||width>4||!width||width>0x04000520-address)return false;
        unsigned offset=address-0x04000400;
        if(!writing) {
            value=0;for(unsigned n=0;n<width;++n) {
                unsigned at=offset+n;
                if(at>=0x100||(at%16)<4)value|=uint32_t(regs[at])<<(8*n);
            }
            return true;
        }
        uint8_t before[4]{};uint32_t previous[16];for(unsigned n=0;n<16;++n)previous[n]=reg(n*16);
        for(unsigned n=0;n<width;++n) {before[n]=regs[offset+n];regs[offset+n]=uint8_t(value>>(n*8));}
        if(reg(0x108,2)!=0) {for(unsigned n=0;n<width;++n)regs[offset+n]=before[n];return false;}
        for(unsigned n=0;n<16;++n) {
            unsigned base=n*16;set(base,reg(base)&0xff7f837f);set(base+4,reg(base+4)&0x07fffffc);set(base+12,reg(base+12)&0x3fffff);
            uint32_t control=reg(base);
            if(!(previous[n]&0x80000000u)&&(control&0x80000000u)) {
                if(!start(n))return false;
            } else if((previous[n]&0x80000000u)&&!(control&0x80000000u)) {
                channels[n].playing=channels[n].tail=false;channels[n].sample=0;
            } else if(channels[n].tail&&!(control&0x8000)) {channels[n].tail=false;channels[n].sample=0;}
        }
        set(0x100,reg(0x100,2)&0xbf7f,2);set(0x104,reg(0x104,2)&0x3ff,2);
        return true;
    }
};
