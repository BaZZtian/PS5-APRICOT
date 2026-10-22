// SPDX-License-Identifier: GPL-3.0-or-later
// Original serial RTC adapter. Clock data is supplied by the native host.
#pragma once
#include <stdint.h>
struct HostRtc {
    uint16_t pins=0;
    uint8_t status1=2,status2=0,adjustment=0,free_register=0;
    uint8_t calendar[7]={0,1,1,6,0,0,0},alarm1[3]{},alarm2[3]{};
    bool (*snapshot)(void*,uint8_t*)=nullptr;void *clock_context=nullptr;
    unsigned bits=0,command=0,index=0,length=0,reg=0,pending=0;
    uint8_t payload[7]{};bool reading=false;unsigned output=1;
    static unsigned reverse3(unsigned n) {return ((n&1)<<2)|(n&2)|((n&4)>>2);}
    uint16_t read() const {return uint16_t((pins&~1u)|((pins&16)?pins&1:output));}
    bool decode() {
        if((command&15)!=6)return false;
        reg=reverse3((command>>4)&7);reading=command&128;index=0;pending=0;
        length=reg==2?7:reg==3?3:reg==5||reg==4&&(status2&4)?3:1;
        if(!reading)return true;
        if((reg==2||reg==3)&&snapshot&&!snapshot(clock_context,calendar))return false;
        if(reg==0) {payload[0]=status1;status1&=15;}
        else if(reg==1)payload[0]=status2;
        else if(reg==2||reg==3) {
            uint8_t values[7];for(unsigned n=0;n<7;++n)values[n]=calendar[n];
            unsigned hour=(values[4]>>4)*10+(values[4]&15),pm=hour>=12?0x40:0;
            if(!(status1&2))hour%=12;
            values[4]=uint8_t((hour/10)*16+hour%10+pm);
            for(unsigned n=0;n<length;++n)payload[n]=values[n+(reg==3?4:0)];
        } else if(reg==4) {
            for(unsigned n=0;n<length;++n)payload[n]=alarm1[n+(length==1?2:0)];
        } else if(reg==5)for(unsigned n=0;n<3;++n)payload[n]=alarm2[n];
        else payload[0]=reg==6?adjustment:free_register;
        return true;
    }
    bool accept(uint8_t value,unsigned byte) {
        if(reg==0) {
            if(value&1) {status1=0;status2=0;adjustment=0;free_register=0;}
            status1=(status1&0xf0)|(value&14);return true;
        }
        if(reg==1) {
            if(value&0xcf)return false; // Alarm/periodic IRQ modes still require a native event adapter.
            status2=value;return true;
        }
        if(reg==2||reg==3)return false; // No host clock is ever changed by guest writes.
        if(reg==4)alarm1[byte+(length==1?2:0)]=value;
        else if(reg==5)alarm2[byte]=value;
        else if(reg==6)adjustment=value;
        else free_register=value;
        return true;
    }
    bool write(uint16_t value) {
        bool previous_cs=(pins&0x44)==0x44,previous_clock=pins&2;
        pins=value;
        bool cs=(pins&0x44)==0x44,clock=pins&2;
        if(!cs) {bits=0;command=0;index=0;output=1;return true;}
        if(!previous_cs) {bits=0;command=0;index=0;pending=0;output=1;return true;}
        if(previous_clock&&!clock&&(pins&32)) {
            if(bits<8) {
                if(!(pins&16))return false;
                command|=(pins&1)<<bits;++bits;
                if(bits==8&&!decode())return false;
            } else if(reading&&!(pins&16)) {
                output=index<length*8?(payload[index/8]>>(index%8))&1:1;++index;
            } else if(!reading&&(pins&16)) {
                if(index>=length*8)return false;
                pending|=(pins&1)<<(index%8);++index;
                if(index%8==0) {if(!accept(uint8_t(pending),index/8-1))return false;pending=0;}
            }
        }
        return true;
    }
};
