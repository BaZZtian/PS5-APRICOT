// SPDX-License-Identifier: GPL-3.0-or-later
// Original 512-KiB serial-flash storage adapter, reached through the IR prefix.
// Empty memory is generated; no existing user save is read or overwritten here.
#pragma once
#include <stdint.h>
#include <vector>
struct HostSaveFlash {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(512*1024,255);
    uint8_t page[256]{};bool touched[256]{};
    uint8_t command=0,status=0;
    uint32_t address=0;
    unsigned prefix=0,position=0,payload=0;
    bool selected=false,asleep=false,dirty=false;
    uint64_t transactions=0,read_bytes=0,write_bytes=0;
    uint64_t ir_packets_sent=0,ir_bytes_sent=0;
    bool finish() {
        if(prefix==1) {
            if(command==6&&!asleep)status|=2;
            if(command==4&&!asleep)status&=~2u;
            if(command==0xb9)asleep=true;
            if(command==0xab)asleep=false;
            bool erase=command==0xdb||command==0xd8||command==0x20;
            if(!asleep&&(status&2)&&position>=4&&((payload&&(command==2||command==0x0a))||erase)) {
                if(status&0x1c)return false; // Protection behavior is not silently ignored.
                if(erase) {
                    unsigned size=command==0xdb?256:command==0xd8?65536:4096;
                    unsigned begin=(address&unsigned(bytes.size()-1))&~(size-1);
                    for(unsigned n=0;n<size;++n)bytes[begin+n]=255;
                    write_bytes+=size;
                } else {
                    unsigned begin=(address&unsigned(bytes.size()-1))&~255u;
                    for(unsigned n=0;n<256;++n)if(touched[n]) {
                        if(command==2)bytes[begin+n]&=page[n];else bytes[begin+n]=page[n];
                        ++write_bytes;
                    }
                }
                status&=~2u;dirty=true;
            }
        }
        if(prefix==4&&payload) {++ir_packets_sent;ir_bytes_sent+=payload;}
        selected=false;++transactions;return true;
    }
    bool transfer(uint8_t input,bool hold,uint8_t &output) {
        output=255;
        if(!selected) {
            selected=true;prefix=position=payload=0;command=0;address=0;
            for(unsigned n=0;n<256;++n) {page[n]=255;touched[n]=false;}
        }
        if(!prefix) {
            if(input==0)prefix=1;
            else if(input==1)prefix=2; // No external IR peer: the receive queue is empty.
            else if(input==8)prefix=3; // New-generation IR cartridge controller version query.
            else if(input==2)prefix=4; // Transmit a packet, with no attached remote peer.
            else return false;
        } else if(prefix==2)output=0;
        else if(prefix==3) {if(position)return false;output=0xaa;++position;}
        else if(prefix==4) {if(payload>=0xb8)return false;++payload;}
        else if(!position) {
            command=input;position=1;
            switch(command) {
            case 2:case 3:case 4:case 5:case 6:case 0x0a:case 0x0b:
            case 0x20:case 0x9f:case 0xab:case 0xb9:case 0xd8:case 0xdb:break;
            default:return false;
            }
        } else if(asleep&&command!=0xab)++position;
        else if(command==5) {output=status;++position;}
        else if(command==0x9f) {
            static constexpr uint8_t identity[]={0x20,0x80,0x13};
            output=position<=3?identity[position-1]:255;++position;
        } else if(command==3||command==0x0b||command==2||command==0x0a||command==0xd8||command==0xdb||command==0x20) {
            if(position<=3)address=(address<<8)|input;
            else if(command==3||(command==0x0b&&position>4)) {
                output=bytes[(address+payload)&(bytes.size()-1)];++payload;++read_bytes;
            } else if(command==2||command==0x0a) {
                unsigned offset=(address+payload)&255;page[offset]=input;touched[offset]=true;++payload;
            }
            ++position;
        } else ++position;
        return hold||finish();
    }
};
