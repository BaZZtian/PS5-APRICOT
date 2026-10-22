// SPDX-License-Identifier: GPL-3.0-or-later
// Original native serial-device adapters and generated personal settings.
// No firmware program, BIOS program, calibration dump or game asset included.
#pragma once
#include <stdint.h>
#include <vector>
static inline uint16_t host_crc16(const uint8_t *data,unsigned size,uint16_t initial=0xffff) {
    uint16_t crc=initial;
    for(unsigned n=0;n<size;++n) {
        crc^=data[n];
        for(unsigned bit=0;bit<8;++bit)crc=uint16_t((crc>>1)^((crc&1)?0xa001:0));
    }
    return crc;
}
struct HostSpi {
    uint16_t control=0,data=0;
    bool selected=false,write_enabled=false;
    unsigned device=0,position=0,address=0,command=0;
    uint8_t pending_page[256]{};bool pending_byte[256]{};
    unsigned page_base=0,page_cursor=0,page_count=0;
    uint8_t power[8]={0x0d,0,1,0,3,0,0,0};
    std::vector<uint8_t> flash=std::vector<uint8_t>(0x40000,0xff);
    std::vector<uint8_t> personal=std::vector<uint8_t>(0x100);
    bool pressed=false;unsigned touch_x=128,touch_y=96,touch_result=0,touch_phase=2;
    HostSpi() {set_language(1);}
    void set_language(unsigned language) {
        personal.assign(0x100,0);
        auto put=[&](unsigned offset,unsigned value) {personal[offset]=uint8_t(value);personal[offset+1]=uint8_t(value>>8);};
        put(0,5);personal[2]=9;personal[3]=1;personal[4]=1;
        const char nickname[]="Orqenva";
        for(unsigned n=0;n<sizeof(nickname)-1;++n)put(6+n*2,unsigned(nickname[n]));
        put(0x1a,sizeof(nickname)-1);
        put(0x58,128);put(0x5a,128);personal[0x5c]=0;personal[0x5d]=0;
        put(0x5e,3872);put(0x60,3872);personal[0x62]=255;personal[0x63]=191;
        put(0x64,(language&7)|0x70);
        put(0x70,0);put(0x72,host_crc16(personal.data(),0x70));
        for(unsigned n=0;n<0x100;++n)flash[0x3fe00+n]=personal[n];
        put(0x70,1);
        for(unsigned n=0;n<0x100;++n)flash[0x3ff00+n]=personal[n];
        flash[0x20]=0xc0;flash[0x21]=0x7f;
        // Locally administered synthetic address, not a console identity.
        const uint8_t local_address[]={2,0,0,0,0,1};
        for(unsigned n=0;n<6;++n)flash[0x36+n]=local_address[n];
        // Independently generated settings for our digital radio. These are
        // hardware-reset values, not console calibration or firmware code.
        // No physical RF calibration entries or access-point credentials exist.
        for(unsigned n=0x2a;n<0x164;++n)flash[n]=0;
        auto config16=[&](unsigned at,unsigned value) {flash[at]=uint8_t(value);flash[at+1]=uint8_t(value>>8);};
        config16(0x2c,0x138);
        for(unsigned n=0;n<6;++n)flash[0x36+n]=local_address[n];
        config16(0x3c,0x3ffe);config16(0x3e,0xffff);
        flash[0x40]=2;flash[0x41]=24;flash[0x42]=0;flash[0x43]=1;
        const uint16_t initial_ports[]={0x16,0x16,0x16,0x162c,0x48,0x4840,0x58,0x42,0x142,0x8064,0,0x2443,3,0,0,0x204};
        for(unsigned n=0;n<sizeof(initial_ports)/sizeof(*initial_ports);++n)config16(0x44+n*2,initial_ports[n]);
        flash[0x64]=0x6d;flash[0x64+0x5d]=1;flash[0x64+0x64]=0xff;
        config16(0x2a,host_crc16(flash.data()+0x2c,0x138,0));
    }
    void finish_selection() {
        if(device==1&&(command==2||command==0x0a)&&page_count&&write_enabled) {
            for(unsigned n=0;n<256;++n)if(pending_byte[n]) {
                unsigned index=(page_base+n)&(unsigned(flash.size())-1);
                if(command==2)flash[index]&=pending_page[n];else flash[index]=pending_page[n];
            }
            write_enabled=false;
        }
        selected=false;
    }
    void set_control(uint16_t value) {
        unsigned next=(value>>8)&3;
        if(selected&&(!(value&0x8000)||next!=device))finish_selection();
        device=next;control=value&0xcf03;
    }
    bool transfer(uint8_t outgoing) {
        if(!(control&0x8000)) {data=0;return true;}
        if(control&0x400)return false; // Sixteen-bit serial framing is not yet implemented.
        if(!selected) {
            selected=true;position=0;address=0;command=0;touch_phase=2;page_count=0;
            if(device==1)for(unsigned n=0;n<256;++n)pending_byte[n]=false;
        }
        uint8_t incoming=0;
        if(device==0) {
            if(position==0)command=outgoing;
            else {
                unsigned reg=command&0x7f;
                if(reg>=sizeof(power))return false;
                if(command&0x80)incoming=power[reg];
                else power[reg]=outgoing;
            }
        } else if(device==1) {
            if(position==0) {
                command=outgoing;
                if(command==6)write_enabled=true;
                else if(command==4)write_enabled=false;
                else if(command!=3&&command!=0x0b&&command!=5&&command!=0x9f&&command!=2&&command!=0x0a)return false;
            } else if(command==5)incoming=write_enabled?2:0;
            else if(command==0x9f) {
                const uint8_t identifier[]={0x20,0x40,0x12};incoming=identifier[(position-1)%3];
            } else if(command==3||command==0x0b||command==2||command==0x0a) {
                if(position<=3) {
                    address=(address<<8)|outgoing;
                    if(position==3) {page_base=address&~255u;page_cursor=address&255;}
                }
                else if(command==0x0b&&position==4) {}
                else {
                    unsigned index=address&(unsigned(flash.size())-1);
                    if(command==2||command==0x0a) {
                        pending_page[page_cursor]=outgoing;pending_byte[page_cursor]=true;
                        page_cursor=(page_cursor+1)&255;++page_count;
                    } else {incoming=flash[index];++address;}
                }
            }
        } else if(device==2) {
            incoming=touch_phase==0?uint8_t(touch_result>>5):touch_phase==1?uint8_t(touch_result<<3):0;
            if(touch_phase<2)++touch_phase;
            if(outgoing&0x80) {
                switch((outgoing>>4)&7) {
                case 1:touch_result=pressed?128+(touch_y>191?191:touch_y)*3744/191:4095;break;
                case 5:touch_result=pressed?128+(touch_x>255?255:touch_x)*3744/255:0;break;
                case 3:touch_result=pressed?0x500:0;break;
                case 4:touch_result=pressed?0xa00:4095;break;
                case 0:touch_result=0x600;break;
                case 7:touch_result=0x800;break;
                default:touch_result=0x800;break;
                }
                touch_phase=0;
            }
        } else return false;
        ++position;data=incoming;
        if(!(control&0x800))finish_selection();
        return true;
    }
};
