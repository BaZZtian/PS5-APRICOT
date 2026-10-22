// SPDX-License-Identifier: GPL-3.0-or-later
// Native display-event adapter. No rendering or graphics assets here.
#pragma once
#include <stdint.h>
struct HostVideoClock {
    uint64_t cycles=0;
    uint16_t control[2]{};
    static constexpr uint64_t line_cycles=2130,frame_cycles=line_cycles*263;
    unsigned line() const {return unsigned((cycles/line_cycles)%263);}
    unsigned compare(unsigned cpu) const {return (control[cpu]>>8)|((control[cpu]&0x80)<<1);}
    uint16_t status(unsigned cpu) const {
        unsigned current=line();bool hblank=cycles%line_cycles>=(cpu?1613:1606);
        return uint16_t(control[cpu]|(current>=192&&current<262?1:0)|(hblank?2:0)|(current==compare(cpu)?4:0));
    }
    static bool crossed(uint64_t before,uint64_t after,uint64_t offset) {
        auto count=[&](uint64_t n) {return n<offset?uint64_t(0):1+(n-offset)/frame_cycles;};
        return count(after)>count(before);
    }
    static bool crossed_line_event(uint64_t before,uint64_t after,uint64_t offset) {
        auto count=[&](uint64_t n) {return n<offset?uint64_t(0):1+(n-offset)/line_cycles;};
        return count(after)>count(before);
    }
    uint64_t next_event() const {
        uint64_t phase=cycles%line_cycles,remaining=line_cycles-phase;
        for(unsigned cpu=0;cpu<2;++cpu)if(control[cpu]&16) {
            uint64_t edge=cpu?1613:1606;
            uint64_t wait=phase<edge?edge-phase:line_cycles-phase+edge;
            if(wait<remaining)remaining=wait;
        }
        return remaining;
    }
    void advance(uint64_t elapsed,uint32_t *irq_flags) {
        uint64_t before=cycles;cycles+=elapsed;
        for(unsigned cpu=0;cpu<2;++cpu) {
            if((control[cpu]&16)&&crossed_line_event(before,cycles,cpu?1613:1606))irq_flags[cpu]|=2;
            if((control[cpu]&8)&&crossed(before,cycles,192*line_cycles))irq_flags[cpu]|=1;
            unsigned target=compare(cpu);
            if((control[cpu]&32)&&target<263&&crossed(before,cycles,target*line_cycles))irq_flags[cpu]|=4;
        }
    }
    bool configure(unsigned cpu,uint16_t value,uint32_t *irq_flags) {
        bool was_match=line()==compare(cpu),was_enabled=control[cpu]&32;
        control[cpu]=value&0xffb8;
        if((control[cpu]&32)&&line()==compare(cpu)&&(!was_match||!was_enabled))irq_flags[cpu]|=4;
        return true;
    }
};
