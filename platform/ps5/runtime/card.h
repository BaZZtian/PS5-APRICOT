// SPDX-License-Identifier: GPL-3.0-or-later
// Original post-boot, plaintext card-data adapter. No BIOS keys or ROM bytes.
// Encrypted boot commands are intentionally unsupported; storage uses own code.
#pragma once
#include <stdint.h>
#include "save_flash.h"
struct HostCard {
    void *context=nullptr;
    bool (*read_rom)(void*,uint32_t,uint8_t*,unsigned)=nullptr;
    uint32_t rom_size=0,chip_id=0;
    uint32_t control=0x20000000,remaining=0,position=0,begin=0,last=0,seed[2][3]{};
    uint16_t auxiliary=0;
    uint8_t command[8]{},spi_data=0;
    unsigned owner=0;
    bool sector_only=false;
    uint64_t transfers=0,read_bytes=0;
    HostSaveFlash save;
    bool ready() const {return remaining!=0;}
    // Serialized card-bus time in the native scheduler's 66-MHz ticks.
    // ROMCTRL selects a 33-MHz /5 or /8 byte clock. Commands use eight
    // clocks, data words four; gap2 also precedes each 512-byte block.
    uint64_t clock_ticks() const {return control&0x08000000u?16:10;}
    uint64_t command_ticks() const {
        return (8+(control&0x1fff)+(ready()?((control>>16)&63):0))*clock_ticks();
    }
    uint64_t data_ticks() const {
        return ready()?(4+(position&&position%512==0?((control>>16)&63):0))*clock_ticks():0;
    }
    void complete(uint32_t *flags) {
        control&=~0x80800000u;
        if(auxiliary&0x4000)flags[owner]|=1u<<19;
    }
    bool start(unsigned cpu,uint32_t value,uint32_t *flags) {
        if(ready()||value&0x40000000)return false;
        control=(value&~0x80808000u)|(control&0x20000000u);
        if(!(value&0x80000000))return true;
        if(!(auxiliary&0x8000)||(auxiliary&0x2000))return false;
        unsigned op=command[0];
        if(op!=0xb7&&op!=0xb8&&op!=0x9f)return false;
        unsigned size=(value>>24)&7;
        remaining=size==0?0:size==7?4:0x100u<<size;
        begin=uint32_t(command[1])<<24|uint32_t(command[2])<<16|uint32_t(command[3])<<8|command[4];
        if(sector_only&&op==0xb7)begin&=~0x1ffu;
        position=0;owner=cpu;
        ++transfers;
        if(remaining)control|=0x80800000u;
        else complete(flags);
        return true;
    }
    bool data(uint32_t &value,uint32_t *flags) {
        if(!remaining) {value=last;return true;}
        uint32_t word=0;
        if(command[0]==0xb8)word=chip_id;
        else if(command[0]==0x9f)word=0xffffffffu;
        else {
            if(!rom_size||(rom_size&(rom_size-1))||!read_rom)return false;
            uint8_t bytes[4]{};
            for(unsigned n=0;n<4;++n) {
                if(sector_only&&position+n>=512) {word|=255u<<(8*n);continue;}
                // Normal ROM windows wrap within the current 4-KiB page.
                uint32_t address=((begin&~0xfffu)|((begin+position+n)&0xfff))&(rom_size-1);
                if(address<0x8000)address=0x8000+(address&0x1ff);
                if(!read_rom(context,address,bytes+n,1))return false;
                word|=uint32_t(bytes[n])<<(8*n);
            }
        }
        last=value=word;remaining-=4;position+=4;
        read_bytes+=4;
        if(!remaining)complete(flags);
        return true;
    }
    bool access(unsigned cpu,uint32_t address,unsigned width,uint32_t &value,bool writing,uint32_t *flags) {
        if(width!=1&&width!=2&&width!=4)return false;
        if(address>=0x040001a0&&address<0x040001a2&&width<=0x040001a2-address) {
            unsigned shift=(address-0x040001a0)*8;uint32_t mask=width==2?65535:255;
            if(writing)auxiliary=uint16_t(((auxiliary&~(mask<<shift))|((value&mask)<<shift))&0xe043);
            else value=(auxiliary>>shift)&mask;
            return true;
        }
        if(address==0x040001a2&&(width==1||width==2)) {
            if(writing) {
                if((auxiliary&0xa000)!=0xa000)return false;
                return save.transfer(uint8_t(value),bool(auxiliary&0x40),spi_data);
            }
            value=spi_data;return true;
        }
        if(address==0x040001a4&&width==4) {
            if(writing)return start(cpu,value,flags);
            value=control;return true;
        }
        if(address>=0x040001a8&&address<0x040001b0&&width<=0x040001b0-address) {
            uint32_t result=0;
            for(unsigned n=0;n<width;++n) {
                unsigned index=address-0x040001a8+n;
                if(writing)command[index]=uint8_t(value>>(8*n));else result|=uint32_t(command[index])<<(8*n);
            }
            if(!writing)value=result;return true;
        }
        if(address>=0x040001b0&&address<0x040001bc&&writing&&width<=4&&width<=0x040001bc-address) {
            for(unsigned n=0;n<width;++n) {
                unsigned offset=address-0x040001b0+n,index=offset/4,shift=(offset%4)*8;
                seed[cpu][index]=(seed[cpu][index]&~(255u<<shift))|(((value>>(n*8))&255)<<shift);
            }
            return true;
        }
        if(address==0x04100010&&width==4&&!writing)return data(value,flags);
        return false;
    }
};
