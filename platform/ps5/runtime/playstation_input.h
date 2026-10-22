// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated-player mapping; physical controls also drive the displayed legend.
#pragma once
#include "platform.h"
#include <stdint.h>
#include <algorithm>
struct NativePadInput {
    float touch_x=128,touch_y=96;bool touch=false,exit=false;
    uint32_t keys=0;
    void update(const pad &p,double seconds) {
        keys=0;
        const uint32_t from[]={PAD_CROSS,PAD_CIRCLE,PAD_SQUARE,PAD_OPTIONS,PAD_RIGHT,PAD_LEFT,PAD_UP,PAD_DOWN,PAD_R1,PAD_L1,PAD_TRIANGLE,PAD_TOUCH_PAD};
        for(unsigned n=0;n<12;++n)if(p.held&from[n])keys|=1u<<n;
        if(p.left_x>0.45f)keys|=1u<<4;if(p.left_x<-.45f)keys|=1u<<5;
        if(p.left_y<-.45f)keys|=1u<<6;if(p.left_y>.45f)keys|=1u<<7;
        float distance=float(std::clamp(seconds,0.0,0.05)*180);
        touch_x=std::clamp(touch_x+p.right_x*distance,0.0f,255.0f);
        touch_y=std::clamp(touch_y+p.right_y*distance,0.0f,191.0f);
        touch=p.r2>0.5f;exit=(p.held&(PAD_L3|PAD_R3))==(PAD_L3|PAD_R3);
    }
};
