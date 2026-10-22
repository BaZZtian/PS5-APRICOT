// SPDX-License-Identifier: GPL-3.0-or-later
// Original native compatibility services. Virtual time advances only through the scheduler.
#pragma once
#include "guest.h"
#include "irq.h"
#include "spi.h"
#include "math_unit.h"
#include "video_clock.h"
#include "rtc.h"
#include "vram.h"
#include "display.h"
#include "geometry.h"
#include "geometry_state.h"
#include "card.h"
#include "overlays.h"
#include "sound_math.h"
#include "sound.h"
#include "raster.h"
#include "wireless.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
struct Bus {
    std::vector<uint8_t> ram=std::vector<uint8_t>(4*1024*1024);
    std::vector<uint8_t> shared=std::vector<uint8_t>(32768), private7=std::vector<uint8_t>(65536);
    std::vector<uint8_t> itcm=std::vector<uint8_t>(32768), dtcm=std::vector<uint8_t>(16384);
    std::vector<uint8_t> palette=std::vector<uint8_t>(2048), oam=std::vector<uint8_t>(2048);
    HostVram vram;
    std::vector<uint8_t> own_exception_traps=std::vector<uint8_t>(4096);
    G dtcm_base=0x027e0000, control=0, io_ime[2]{}, io_wram=3;
    G irq_enable[2]{}, irq_flags[2]{}, fifo_control[2]{}, fifo_last[2]{}, sync[2]{};
    std::deque<G> fifo[2];
    uint64_t delay_iterations[2]{};
    G exmem9=0x6000, exmem7=0,wifi_wait=0x30;
    G post[2]={1,1};
    G power_control[2]={1,1};
    G serial_gpio=0x8000,buttons=0,key_control[2]{};
    HostSound sound;bool sound_failed=false;
    HostSpi spi;
    HostMath math;
    HostVideoClock video;
    HostRtc rtc;
    HostWireless wireless;
    HostDisplay display;
    HostGeometry geometry;
    HostCard card;
    std::vector<uint8_t> owner_rom;
    NativeOverlays overlays;
    HostRaster raster;
    std::vector<uint8_t> raster_geometry;uint64_t raster_geometry_frame=UINT64_MAX;
    GuestCpu *current_cpu=nullptr;
    void *write_observer_context=nullptr;
    void (*write_observer)(void*,G,G,G,G,const uint8_t*,G)=nullptr;
    bool halted[2]{};
    struct Timer {G reload=0,counter=0,control=0;uint64_t phase=0;};
    Timer timers[2][4];
    G dma[2][4][3]{};bool dma_running=false;
    G dma_fill[4]{};
    Bus() {
        // Original fail-fast exception placeholders, not a proprietary BIOS.
        // Ordinary SVC services are native host functions; these bytes are not decoded at runtime.
        constexpr G trap=0xe7f000f0u;
        sound.context=this;
        sound.read=[](void *context,uint32_t address,unsigned width,uint32_t *value)->bool {
            auto &bus=*static_cast<Bus*>(context);auto p=bus.map(7,address,width);if(!p)return false;
            *value=0;for(unsigned n=0;n<width;++n)*value|=uint32_t(p[n])<<(n*8);return true;
        };
        for(size_t offset=0;offset<own_exception_traps.size();offset+=4)
            for(unsigned n=0;n<4;++n)own_exception_traps[offset+n]=uint8_t(trap>>(n*8));

    }
    void advance(uint64_t cycles) {
        if(!sound.advance(cycles))sound_failed=true;
        wireless.advance(cycles,irq_flags[1],power_control[1]&2);
        geometry.irq(irq_flags[0]);
        auto before=video.cycles;video.advance(cycles,irq_flags);
        if(HostVideoClock::crossed(before,video.cycles,192*HostVideoClock::line_cycles)) {
            // Raster rows describe the frame just completed. The new polygon
            // list swapped at this VBlank belongs to the following frame.
            if(raster_geometry_frame!=geometry.swaps) {raster_geometry=pack_geometry_state(geometry);raster_geometry_frame=geometry.swaps;}
            geometry.vblank(vram,display.control3d);
        }
        raster.advance(before,video.cycles,display,vram,palette,oam);
        static const unsigned divisors[]={1,64,256,1024};
        for(unsigned cpu=0;cpu<2;++cpu) {
            uint64_t overflow=0;
            for(unsigned channel=0;channel<4;++channel) {
                auto &timer=timers[cpu][channel];uint64_t increments=0;
                if(timer.control&0x80) {
                    if(channel&&(timer.control&4))increments=overflow;
                    else {uint64_t phase=timer.phase+cycles;unsigned div=divisors[timer.control&3];increments=phase/div;timer.phase=phase%div;}
                }
                overflow=0;
                uint64_t remaining=0x10000u-timer.counter;
                if(increments>=remaining) {
                    uint64_t period=0x10000u-timer.reload;
                    overflow=1+(increments-remaining)/period;
                    timer.counter=timer.reload+G((increments-remaining)%period);
                    if(timer.control&0x40)irq_flags[cpu]|=1u<<(3+channel);
                } else timer.counter+=G(increments);
            }
        }
    }
    uint64_t timer_ticks(unsigned cpu,unsigned channel,uint64_t increments) const {
        const auto &timer=timers[cpu][channel];if(!(timer.control&0x80))return UINT64_MAX;
        if(channel&&(timer.control&4)) {
            const auto &previous=timers[cpu][channel-1];uint64_t first=0x10000u-previous.counter,period=0x10000u-previous.reload;
            if(increments-1>(UINT64_MAX-first)/period)return UINT64_MAX;
            return timer_ticks(cpu,channel-1,first+(increments-1)*period);
        }
        static const unsigned divisors[]={1,64,256,1024};unsigned divisor=divisors[timer.control&3];
        if(increments>UINT64_MAX/divisor)return UINT64_MAX;
        uint64_t needed=increments*divisor;return needed<=timer.phase?1:needed-timer.phase;
    }
    uint64_t next_event() const {
        uint64_t next=video.next_event();
        for(unsigned cpu=0;cpu<2;++cpu)for(unsigned channel=0;channel<4;++channel)
            if((timers[cpu][channel].control&0xc0)==0xc0&&(irq_enable[cpu]&(1u<<(3+channel)))) {
                uint64_t wait=timer_ticks(cpu,channel,0x10000u-timers[cpu][channel].counter);
                if(wait<next)next=wait;
            }
        return next;
    }
    G cp15[65536]{};
    uint8_t *map(G core,G address,G width) {
        auto window=[&](std::vector<uint8_t>&v,G offset)->uint8_t* {
            return offset<=v.size()&&width<=v.size()-offset?v.data()+offset:nullptr;
        };
        if(core==9&&address>=dtcm_base&&address-dtcm_base<dtcm.size())return window(dtcm,address-dtcm_base);
        if(core==9&&address<0x02000000)return window(itcm,address&32767);
        if(address>=0x02000000&&address<0x03000000)return window(ram,address&0x3fffff);
        if(address>=0x03000000&&address<(core==9?0x04000000:0x03800000)) {
            if(io_wram==0)return core==9?window(shared,address&32767):window(private7,address&65535);
            if(io_wram==3)return core==7?window(shared,address&32767):nullptr;
            unsigned half=(io_wram==1)==(core==9)?16384:0;
            return window(shared,half+(address&16383));
        }
        if(core==7&&address>=0x03800000&&address<0x04000000)return window(private7,address&65535);
        if(address>=0x05000000&&address<0x05000800)return window(palette,address-0x05000000);
        if(address>=0x07000000&&address<0x07000800)return window(oam,address-0x07000000);
        return nullptr;
    }
    bool card_dma(unsigned cpu) {
        if(!card.ready()||card.owner!=cpu)return true;
        unsigned core=cpu?7:9;
        for(unsigned channel=0;channel<4&&card.ready();++channel) {
            auto &registers=dma[cpu][channel];G control=registers[2];
            G timing=(control>>(cpu?28:27))&(cpu?3:7);
            if(!(control&0x80000000u)||timing!=(cpu?2:5))continue;
            G count=control&(cpu?channel==3?0xffff:0x3fff:0x1fffff);
            if(count!=1||!(control&0x04000000)||((control>>23)&3)!=2||registers[0]!=0x04100010||dma_running) {
                std::fprintf(stderr,"UNIMPLEMENTED_CARD_DMA core=%u channel=%u control=%08x\n",core,channel,control);return false;
            }
            dma_running=true;G destination=registers[1]&~3u,mode=(control>>21)&3;
            do {
                G word=0;
                if(!card.data(word,irq_flags)||!write(this,core,destination,4,word)) {dma_running=false;return false;}
                if(mode!=2&&mode!=3)destination+=mode==1?G(0-4):4;
                if(control&0x40000000)irq_flags[cpu]|=1u<<(8+channel);
                if(!(control&0x02000000))registers[2]&=~0x80000000u;
                if(current_cpu)current_cpu->ticks+=64;
            } while(card.ready()&&(registers[2]&0x80000000u));
            registers[1]=destination;dma_running=false;
        }
        return true;
    }
    static bool read(void *ctx,G core,G address,G width,G *value) {
        auto &b=*static_cast<Bus*>(ctx);*value=0;
        if(core==7&&address>=0x04800000&&address<0x04810000) {
            if(b.wireless.access(address,width,*value,false,b.power_control[1]&2))return true;
            std::fprintf(stderr,"UNIMPLEMENTED_WIRELESS_READ address=%08x reason=%s\n",address,b.wireless.error);return false;
        }
        // Legacy cartridge profile: extended BIOS status is hidden/zero.
        // GBATEK, SCFG_A9ROM: NDS BIOS mapping with SCFG_EXT9.bit31 clear.
        if(core==9&&address>=0x04004000&&address<0x04004004&&(width==1||width==2||width==4)&&width<=0x04004004-address)return true;
        if(b.vram.access(core,address,width,*value,false))return true;
        if(core==9&&address>=0xffff0000u&&address-0xffff0000u<b.own_exception_traps.size()&&width<=b.own_exception_traps.size()-(address-0xffff0000u)) {
            for(G n=0;n<width;++n)*value|=G(b.own_exception_traps[address-0xffff0000u+n])<<(8*n);
            return true;
        }
        if(auto p=b.map(core,address,width)) {for(G i=0;i<width;++i)*value|=G(p[i])<<(8*i);return true;}
        unsigned index=core==9?0:1;
        if(core==9&&b.display.access(address,width,*value,false))return true;
        if(core==9&&b.geometry.access(address,width,*value,false,b.irq_flags[0]))return true;
        if(bool(b.exmem9&0x800)==(core==7)&&b.card.access(index,address,width,*value,false,b.irq_flags))return true;
        if(address==0x04000130&&width==2) {*value=(~b.buttons)&0x3ff;return true;}
        if(address==0x04000132&&width==2) {*value=b.key_control[index];return true;}
        if(core==7&&address==0x04000134&&width==2) {*value=b.serial_gpio;return true;}
        if(core==7&&address==0x04000138&&(width==1||width==2)) {*value=b.rtc.read()&(width==1?255:65535);return true;}
        if(core==7&&address==0x04000136&&width==2) {
            *value=0x7c|((~b.buttons>>10)&3);if(b.spi.pressed)*value&=~0x40u;return true;
        }
        if(address==0x04000004&&width==2) {*value=b.video.status(index);return true;}
        if(address==0x04000006&&width==2) {*value=b.video.line();return true;}
        if(core==7&&b.sound.access(address,width,*value,false))return true;
        if(address==0x04000304&&width==2) {*value=b.power_control[index];return true;}
        if(core==9&&b.math.access(address,width,*value,false))return true;
        if(core==7&&address==0x040001c0&&width==2) {*value=b.spi.control;return true;}
        if(core==7&&address==0x040001c2&&(width==1||width==2)) {*value=b.spi.data;return true;}
        if(address>=0x08000000&&address<0x0b000000) {
            bool selected=bool(b.exmem9&0x80)==(core==7);
            if(!selected)return true;
            for(G n=0;n<width;++n) {
                G at=address+n,half=0xffff;
                if(at<0x0a000000) {
                    G wait=(b.exmem9>>2)&3;
                    half=wait==3?0xffff:((at>>1)&0xffff)|(wait==0?0xfe08:0);
                }
                *value|=((half>>((at&1)*8))&255)<<(n*8);
            }
            return true;
        }
        if(address>=0x040000b0&&address<0x040000e0&&(width==2||width==4)) {
            G offset=address-0x040000b0;G reg=b.dma[index][offset/12][(offset%12)/4];
            *value=(reg>>((offset&3)*8))&(width==2?0xffffu:0xffffffffu);return true;
        }
        if(core==9&&address>=0x040000e0&&address<0x040000f0&&(width==2||width==4)) {
            *value=(b.dma_fill[(address-0x040000e0)/4]>>((address&3)*8))&(width==2?0xffffu:0xffffffffu);return true;
        }
        if(address>=0x04000100&&address<0x04000110&&width==2) {
            auto &timer=b.timers[index][(address-0x04000100)/4];
            *value=(address&2)?timer.control:timer.counter;return true;
        }
        if(address==0x04000208&&width<=4) {*value=b.io_ime[core==9?0:1];return true;}
        if(address==0x04000204&&width==2) {*value=core==9?b.exmem9:(b.exmem9&0xff80)|b.exmem7;return true;}
        if(core==7&&address==0x04000206&&width==2&&(b.power_control[1]&2)) {*value=b.wifi_wait;return true;}
        unsigned i=core==9?0:1, other=1-i;
        if(address==0x04000210&&width==4) {*value=b.irq_enable[i];return true;}
        if(address==0x04000214&&width==4) {*value=b.irq_flags[i];return true;}
        if(address==0x04000180&&width==2) {*value=(b.sync[i]&0x4f00)|((b.sync[other]>>8)&15);return true;}
        if(address==0x04000184&&width==2) {
            *value=b.fifo_control[i]|(b.fifo[i].empty()?1:0)|(b.fifo[i].size()==16?2:0)|
                (b.fifo[other].empty()?0x100:0)|(b.fifo[other].size()==16?0x200:0);return true;
        }
        if(address==0x04100000&&width==4) {
            if(!(b.fifo_control[i]&0x8000)) {*value=b.fifo_last[i];return true;}
            if(b.fifo[other].empty())b.fifo_control[i]|=0x4000;
            else {
                b.fifo_last[i]=b.fifo[other].front();b.fifo[other].pop_front();
                if(b.fifo[other].empty()&&(b.fifo_control[other]&4))b.irq_flags[other]|=1u<<17;
            }
            *value=b.fifo_last[i];return true;
        }
        if(core==9&&address>=0x04000240&&address+width<=0x0400024c) {
            for(G n=0;n<width;++n) {
                unsigned offset=address-0x04000240+n;
                G byte=offset==7?b.io_wram:offset<7?b.vram.control[offset]:offset<10?b.vram.control[offset-1]:0;
                *value|=byte<<(8*n);
            }
            return true;
        }
        if(core==7&&address==0x04000240&&(width==1||width==2||width==4)) {*value=b.vram.status7()|(width>1?b.io_wram<<8:0);return true;}
        if(core==7&&address==0x04000241&&width==1) {*value=b.io_wram;return true;}
        if(address==0x04000300&&(width==1||width==2)) {*value=b.post[index];return true;}
        std::fprintf(stderr,"UNIMPLEMENTED_READ core=%u address=%08x width=%u\n",core,address,width);return false;
    }
    static bool write(void *ctx,G core,G address,G width,G value) {
        auto &b=*static_cast<Bus*>(ctx);
        // ARM7's 16-KiB boot ROM is not writable. In particular, an optional
        // status pointer of zero must not turn an ignored ROM store into a
        // fabricated machine fault. No ROM contents are provided here.
        if(core==7&&address<0x4000&&(width==1||width==2||width==4)&&width<=0x4000-address)return true;
        if(core==7&&address>=0x04800000&&address<0x04810000) {
            if(b.wireless.access(address,width,value,true,b.power_control[1]&2))return true;
            std::fprintf(stderr,"UNIMPLEMENTED_WIRELESS_WRITE address=%08x value=%08x reason=%s\n",address,value,b.wireless.error);return false;
        }
        if(b.vram.access(core,address,width,value,true))return true;
        if(width==1&&((address>=0x05000000&&address<0x05000800)||(address>=0x07000000&&address<0x07000800)))return true;
        if(auto p=b.map(core,address,width)) {
            if(address>=0x02000000&&address<0x03000000&&!(core==9&&address>=b.dtcm_base&&address-b.dtcm_base<b.dtcm.size())) {
                G executing=b.current_cpu&&b.current_cpu->core==9?b.current_cpu->r[15]:0;
                if(!b.overlays.before_write(0x02000000+(address&0x3fffff),width,value,p,executing)) {
                    std::fprintf(stderr,"UNIMPLEMENTED_EXECUTING_CODE_WRITE core=%u address=%08x\n",core,address);return false;
                }
            }
            if(b.write_observer)b.write_observer(b.write_observer_context,core,address,width,b.current_cpu?b.current_cpu->r[15]:0,p,value);
            for(G i=0;i<width;++i)p[i]=uint8_t(value>>(8*i));return true;
        }
        unsigned index=core==9?0:1;
        if(core==9&&b.display.access(address,width,value,true))return true;
        if(core==9&&b.geometry.access(address,width,value,true,b.irq_flags[0]))return true;
        if(bool(b.exmem9&0x800)==(core==7)&&b.card.access(index,address,width,value,true,b.irq_flags)) {
            if(address==0x040001a4)return b.card_dma(index);
            return true;
        }
        if(address==0x04000132&&width==2) {b.key_control[index]=value&0xc3ff;return true;}
        if(core==7&&address==0x04000134&&width==2) {b.serial_gpio=value&0xc1ff;return true;}
        if(core==7&&address==0x04000138&&(width==1||width==2)) {
            if(!b.rtc.write(uint16_t(width==1?(b.rtc.pins&0xff00)|(value&255):value))) {
                std::fprintf(stderr,"UNIMPLEMENTED_RTC command=%02x register=%u index=%u\n",b.rtc.command,b.rtc.reg,b.rtc.index);return false;
            }
            return true;
        }
        if(address==0x04000004&&width==2) {
            if(!b.video.configure(index,uint16_t(value),b.irq_flags)) {
                std::fprintf(stderr,"UNIMPLEMENTED_HBLANK_IRQ core=%u value=%04x\n",core,value);return false;
            }
            return true;
        }
        if(address==0x04000304&&width==2) {b.power_control[index]=value&(core==9?0x820f:3);return true;}
        if(core==7&&address>=0x04000400&&address+width<=0x04000520) {
            if(!b.sound.access(address,width,value,true)) {
                std::fprintf(stderr,"UNIMPLEMENTED_SOUND_OPERATION address=%08x value=%08x sample_fault=%08x\n",address,value,b.sound.fault_address);return false;
            }
            return true;
        }
        if(core==7&&address==0x04000206&&width==2) {if(b.power_control[1]&2)b.wifi_wait=value&0x3f;return true;}
        if(core==9&&b.math.access(address,width,value,true))return true;
        if(core==7&&address==0x040001c0&&width==2) {b.spi.set_control(uint16_t(value));return true;}
        if(core==7&&address==0x040001c2&&(width==1||width==2)) {
            if(!b.spi.transfer(uint8_t(value))) {std::fprintf(stderr,"UNIMPLEMENTED_SPI device=%u command=%02x position=%u\n",b.spi.device,b.spi.command,b.spi.position);return false;}
            if(b.current_cpu)b.current_cpu->ticks+=uint64_t(128)<<(b.spi.control&3);
            if(b.spi.control&0x4000)b.irq_flags[1]|=1u<<23;
            return true;
        }
        if(core==9&&address>=0x040000e0&&address<0x040000f0&&(width==2||width==4)) {
            G shift=(address&3)*8,mask=width==2?0xffffu:0xffffffffu;
            auto &target=b.dma_fill[(address-0x040000e0)/4];target=(target&~(mask<<shift))|((value&mask)<<shift);return true;
        }
        if(address>=0x04000100&&address<0x04000110&&width==2) {
            auto &timer=b.timers[index][(address-0x04000100)/4];
            if(address&2) {
                if(!(timer.control&0x80)&&(value&0x80)) {timer.counter=timer.reload;timer.phase=0;}
                timer.control=value&0xc7;
            } else timer.reload=value&0xffff;
            return true;
        }
        if(address>=0x040000b0&&address<0x040000e0&&(width==2||width==4)) {
            G offset=address-0x040000b0,channel=offset/12,reg=(offset%12)/4;
            auto &registers=b.dma[index][channel];G previous=registers[2];
            G shift=(offset&3)*8,mask=width==2?0xffffu:0xffffffffu;
            registers[reg]=(registers[reg]&~(mask<<shift))|((value&mask)<<shift);
            G control=registers[2];
            if(!(previous&0x80000000u)&&(control&0x80000000u)) {
                G timing=(control>>(core==9?27:28))&(core==9?7:3);
                if(timing==(core==9?5:2))return b.card_dma(index);
                if(timing||b.dma_running) {std::fprintf(stderr,"UNIMPLEMENTED_DMA_TRIGGER core=%u channel=%u timing=%u\n",core,channel,timing);return false;}
                G count=control&(core==9?0x1fffff:channel==3?0xffff:0x3fff);
                if(!count)count=core==9?0x200000:channel==3?0x10000:0x4000;
                G size=(control&0x04000000)?4:2;
                G src_mode=(control>>23)&3,dst_mode=(control>>21)&3;
                if(src_mode==3)return false;
                G source=registers[0]&~(size-1),destination=registers[1]&~(size-1),saved_dest=destination;
                b.dma_running=true;
                for(G n=0;n<count;++n) {
                    G data=0;
                    if(!read(&b,core,source,size,&data)||!write(&b,core,destination,size,data)) {b.dma_running=false;return false;}
                    source+=src_mode==0?size:src_mode==1?G(0-size):0;
                    destination+=dst_mode==1?G(0-size):dst_mode==2?0:size;
                }
                b.dma_running=false;registers[0]=source;registers[1]=dst_mode==3?saved_dest:destination;
                registers[2]&=~0x80000000u;
                if(control&0x40000000) b.irq_flags[index]|=1u<<(8+channel);
            }
            return true;
        }
        if(address==0x04000208&&width<=4) {b.io_ime[core==9?0:1]=value&1;return true;}
        if(address==0x04000204&&width==2) {
            if(core==9)b.exmem9=(value&0xbfff)|0x4000;
            else b.exmem7=value&127;
            return true;
        }
        unsigned i=core==9?0:1, other=1-i;
        if(address==0x04000210&&width==4) {b.irq_enable[i]=value;return true;}
        if(address==0x04000214&&width==4) {b.irq_flags[i]&=~value;return true;}
        if(address==0x04000180&&width==2) {
            b.sync[i]=value&0x4f00;
            if((value&0x2000)&&(b.sync[other]&0x4000))b.irq_flags[other]|=1u<<16;
            return true;
        }
        if(address==0x04000184&&width==2) {
            G previous=b.fifo_control[i];
            if(value&8)b.fifo[i].clear();
            b.fifo_control[i]=(value&0x8404)|((value&0x4000)?0:(previous&0x4000));
            if((value&4)&&!(previous&4)&&b.fifo[i].empty())b.irq_flags[i]|=1u<<17;
            if((value&0x400)&&!(previous&0x400)&&!b.fifo[other].empty())b.irq_flags[i]|=1u<<18;
            return true;
        }
        if(address==0x04000188&&width==4) {
            if(!(b.fifo_control[i]&0x8000))return true;
            if(b.fifo[i].size()==16)b.fifo_control[i]|=0x4000;
            else {
                bool was_empty=b.fifo[i].empty();b.fifo[i].push_back(value);
                if(was_empty&&(b.fifo_control[other]&0x400))b.irq_flags[other]|=1u<<18;
            }
            return true;
        }
        if(core==9&&address>=0x04000240&&address+width<=0x0400024c) {
            for(G n=0;n<width;++n) {
                unsigned offset=address-0x04000240+n,byte=(value>>(8*n))&255;
                if(offset==7)b.io_wram=byte&3;
                else if(offset<10) {unsigned bank=offset-(offset>7);b.vram.control[bank]=uint8_t(byte&HostVram::masks[bank]);}
            }
            return true;
        }
        if(address==0x04000300&&width==1) {b.post[index]|=value&1;return true;}
        if(core==9&&address>=0x04000400&&address<0x04000600)
            std::fprintf(stderr,"GEOMETRY_REJECT command=%02x packed=%02x,%02x,%02x,%02x next=%u words=%u begun=%u primitive=%u list_vertices=%u polygons=%zu deferred=%u\n",
                b.geometry.rejected_command,b.geometry.commands[0],b.geometry.commands[1],b.geometry.commands[2],b.geometry.commands[3],
                b.geometry.next_command,b.geometry.parameter_count,b.geometry.begun,b.geometry.primitive,b.geometry.list_vertices,b.geometry.building.size(),b.geometry.deferred_words);
        std::fprintf(stderr,"UNIMPLEMENTED_WRITE core=%u address=%08x width=%u value=%08x\n",core,address,width,value);return false;
    }
    static bool system(void *ctx,GuestCpu *cpu,G operation,G argument,G *result) {
        auto &b=*static_cast<Bus*>(ctx);G key=operation&0xffff;
        if(operation==0x04000000u)return b.overlays.resolve(cpu->core,argument,b.ram.data(),unsigned(b.ram.size()),*result);
        if((operation&0xff000000)==0x03000000) {
            G number=(cpu->cpsr&32)?operation&255:(operation>>16)&255;
            if(cpu->core==7&&number>=0x1a&&number<=0x1c) {
                G computed=0;if(!HostSoundMath::service(number,cpu->r[0],computed))return false;
                cpu->r[0]=computed;cpu->ticks+=64;return true;
            }
            if(number==3&&cpu->r[0]&&cpu->r[0]<=0x7fffffffu) {
                b.delay_iterations[cpu->core==9?0:1]+=cpu->r[0];
                cpu->ticks+=uint64_t(cpu->r[0])*4*(cpu->core==9?1:2);
                cpu->r[0]=0;*result=G_YIELD;return true;
            }
            if(number==6) {b.halted[cpu->core==9?0:1]=true;*result=G_YIELD;return true;}
            if(number==0x0b||number==0x0c) {
                G count=cpu->r[2]&0x1fffff, width=number==0x0c||(cpu->r[2]&0x04000000)?4:2;
                bool fixed=cpu->r[2]&0x01000000;
                if(number==0x0c&&(count&7))return false;
                G source=cpu->r[0]&~(width-1),destination=cpu->r[1]&~(width-1),value=0;
                for(G n=0;n<count;++n) {
                    if((!fixed||!n)&&!read(&b,cpu->core,source,width,&value))return false;
                    if(!write(&b,cpu->core,destination,width,value))return false;
                    if(!fixed)source+=width;
                    destination+=width;
                }
                cpu->ticks+=uint64_t(count)*(width==4?8:4)*(cpu->core==9?1:2);
                *result=G_YIELD;return true;
            }
            if(number==0x0e) {
                if(cpu->r[2]>1024*1024||uint64_t(cpu->r[1])+cpu->r[2]>0x100000000ull)return false;
                uint16_t crc=uint16_t(cpu->r[0]);
                for(G n=0;n<cpu->r[2];++n) {
                    G byte=0;if(!read(&b,cpu->core,cpu->r[1]+n,1,&byte)) {cpu->fault=cpu->r[1]+n;return false;}
                    uint8_t input=uint8_t(byte);crc=host_crc16(&input,1,crc);
                }
                cpu->r[0]=crc;cpu->ticks+=uint64_t(cpu->r[2])*32*(cpu->core==9?1:2);*result=G_YIELD;return true;
            }
        }
        if((operation&0xffff0000)==0x10000&&cpu->core==9) {
            if(key==0)*result=0x41059461;
            else *result=b.cp15[key];
            return true;
        }
        if((operation&0xffff0000)==0x20000&&cpu->core==9) {
            // No guest cache is present: native code is immutable and coherent.
            if((key>>12)==7) {
                if(key==0x7004) {b.halted[cpu->core==9?0:1]=true;*result=G_YIELD;return true;}
                return true;
            }
            b.cp15[key]=argument;
            if(key==0x9100) {
                if((argument&63)!=0x0a)return false; // This trace supports the explicit 16-KiB DTCM setting.
                b.dtcm_base=argument&0xfffff000u;
            }
            return true;
        }
        std::fprintf(stderr,"UNIMPLEMENTED_SYSTEM core=%u pc=%08x operation=%08x argument=%08x\n",cpu->core,cpu->r[15],operation,argument);
        return false;
    }
};
