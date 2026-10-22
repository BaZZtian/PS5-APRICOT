// SPDX-License-Identifier: GPL-3.0-or-later
// Cooperative execution of statically compiled native entries. No CPU decoder.
#pragma once
#include "bus.h"
struct NativeMachine {
    Bus bus;
    GuestHost host{GUEST_ABI,sizeof(GuestHost),&bus,Bus::read,Bus::write,Bus::system};
    GuestCpu cpus[2]{};GuestEntry entries[2]{};
    G stops[2]={G_TRANSFER,G_TRANSFER};uint64_t executed[2]{},hardware_time=0;
    bool active[2]={false,false},initialized=false;unsigned irq_depth[2]{};
    const char *error="none";
    bool configure(GuestEntry native9,GuestEntry native7,G pc9,G pc7,const NativeOverlayInfo *catalog,unsigned count) {
        if(initialized||!native9||!native7||pc9&3||pc7&3||count>256||count&&!catalog) {error="invalid native configuration";return false;}
        entries[0]=native9;entries[1]=native7;bus.overlays.configure(catalog,count);
        for(unsigned i=0;i<2;++i) {cpus[i].core=i?7:9;cpus[i].cpsr=0x1f;cpus[i].r[15]=i?pc7:pc9;active[i]=true;}
        initialized=true;return true;
    }
    bool step() {
        if(!initialized||!active[0]||!active[1])return false;
        uint64_t now=bus.halted[0]?(bus.halted[1]?hardware_time+bus.next_event():cpus[1].ticks/2):
            bus.halted[1]?cpus[0].ticks/2:std::min(cpus[0].ticks,cpus[1].ticks)/2;
        if(now<hardware_time) {error="native clock regressed";active[0]=false;stops[0]=G_SYSTEM;return false;}
        bus.advance(now-hardware_time);hardware_time=now;
        if(bus.geometry.failed) {error="geometry device stopped";stops[0]=G_UNSUPPORTED;active[0]=false;return false;}
        if(bus.sound_failed) {
            error="sound device stopped";cpus[1].fault=bus.sound.fault_address;
            stops[1]=bus.sound.fault_address?G_MEMORY:G_UNSUPPORTED;active[1]=false;return false;
        }
        for(unsigned i=0;i<2;++i)if(bus.halted[i]) {
            cpus[i].ticks=std::max(cpus[i].ticks,now*2);
            if((bus.irq_enable[i]&bus.irq_flags[i])&&(i==1||bus.io_ime[i]))bus.halted[i]=false;
        }
        if(bus.halted[0]&&bus.halted[1])return true;
        unsigned i=bus.halted[0]?1:bus.halted[1]?0:cpus[0].ticks<=cpus[1].ticks?0:1;
        auto &cpu=cpus[i];
        if(!(cpu.cpsr&0x80)&&bus.io_ime[i]&&(bus.irq_enable[i]&bus.irq_flags[i])) {
            G vector=i?0x0380fffc:bus.dtcm_base+0x3ffc,handler=0;
            if(irq_depth[i]>=8||!Bus::read(&bus,cpu.core,vector,4,&handler)||!guest_irq_enter(cpu,host,handler)) {
                error="native IRQ entry failed";stops[i]=G_SYSTEM;active[i]=false;return false;
            }
            ++irq_depth[i];
        }
        bus.current_cpu=&cpu;cpu.budget=32;G status=G_YIELD;
        while(cpu.budget) {
            if(irq_depth[i]&&cpu.r[15]==(i?G_IRQ_RETURN7:G_IRQ_RETURN9)) {
                if(!guest_irq_leave(cpu,host)) {status=G_SYSTEM;break;}
                --irq_depth[i];continue;
            }
            G budget=cpu.budget;status=entries[i](&cpu,&host);
            if((status==G_UNMAPPED||status==G_TRANSFER)&&cpu.budget<budget)continue;
            break;
        }
        stops[i]=cpu.budget?status:G_YIELD;executed[i]+=32-cpu.budget;
        if(cpu.budget&&status!=G_YIELD) {active[i]=false;error=status==G_UNMAPPED?"native target absent":"native service stopped";return false;}
        return true;
    }
    NativeMachine()=default;
    NativeMachine(const NativeMachine&)=delete;
    NativeMachine& operator=(const NativeMachine&)=delete;
};
