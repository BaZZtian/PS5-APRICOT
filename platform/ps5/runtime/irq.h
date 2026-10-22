// SPDX-License-Identifier: GPL-3.0-or-later
// Independently written native IRQ ABI adapter. No BIOS instruction bytes.
#pragma once
#include "guest.h"
constexpr G G_IRQ_RETURN9=0xffff0100u, G_IRQ_RETURN7=0x00000100u;
static inline bool guest_irq_enter(GuestCpu &s,const GuestHost &h,G handler) {
    if(!handler||guest_bank(s.cpsr)==6)return false;
    G saved=s.cpsr,return_address=s.r[15]+4;
    G values[]={s.r[0],s.r[1],s.r[2],s.r[3],s.r[12],return_address};
    if(!guest_psr(s,(saved&~0x3fu)|0x92u,0xffffffffu,false))return false;
    s.spsr[2]=saved;s.r[13]-=24;
    for(unsigned n=0;n<6;++n)
        if(!guest_store(s,h,s.r[13]+n*4,4,values[n]))return false;
    s.r[0]=0x04000000;s.r[14]=s.core==9?G_IRQ_RETURN9:G_IRQ_RETURN7;
    guest_pc(s,handler,s.core==9);s.ticks+=G(s.core==9?12:24);return true;
}
static inline bool guest_irq_leave(GuestCpu &s,const GuestHost &h) {
    if((s.cpsr&31)!=0x12)return false;
    G values[6]{};
    for(unsigned n=0;n<6;++n)
        if(!guest_load(s,h,s.r[13]+n*4,4,values[n]))return false;
    s.r[13]+=24;
    for(unsigned n=0;n<4;++n)s.r[n]=values[n];
    s.r[12]=values[4];s.r[14]=values[5];
    G target=values[5]-4;
    if(!guest_psr(s,s.spsr[2],0xffffffffu,false))return false;
    guest_pc(s,target,false);s.ticks+=G(s.core==9?12:24);return true;
}
