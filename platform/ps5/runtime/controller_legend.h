// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "menu.h"
#include "apricot/controller_prompts.hpp"
inline void gameplay_controls(std::vector<uint32_t>& canvas) {
    using I=apricot::ControlIcon;
    struct Prompt {I icon;const char* label;};
    const Prompt left[]={{I::Cross,"A"},{I::Circle,"B"},{I::Triangle,"X"},
                         {I::Touchpad,"Y"},{I::Square,"SELECT"},{I::Options,"START"}};
    const Prompt right[]={{I::L1,"L"},{I::R1,"R"},{I::Dpad,"BEWEGEN"},
                          {I::LeftStick,"BEWEGEN"},{I::RightStick,"ZEIGER"},{I::R2,"TIPPEN"}};
    for(unsigned n=0;n<6;++n)for(unsigned column=0;column<2;++column){
        const auto& p=column?right[n]:left[n];const int x=column?1054:838,y=405+int(n)*40;
        apricot::controller_icon(canvas,1280,720,x,y,p.icon);
        menuText(canvas,x+46,y+13,p.label,0xff362d17,2,1280);
    }
    apricot::controller_icon(canvas,1280,720,858,658,I::L3);
    for(unsigned n=0;n<12;++n)for(unsigned t=0;t<2;++t){
        canvas[(674+t)*1280+904+n]=0xff362d17;
        canvas[(669+n)*1280+909+t]=0xff362d17;
    }
    apricot::controller_icon(canvas,1280,720,926,658,I::R3);
    menuText(canvas,978,671,"BEENDEN",0xff362d17,2,1280);
}
