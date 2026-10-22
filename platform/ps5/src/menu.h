// SPDX-License-Identifier: GPL-3.0-or-later
// Small original 5x7 uppercase font for boot messages and ROM selection.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
static const uint8_t menuFont[][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
{14,17,17,15,1,1,14},
{0,0,0,0,0,0,0},{0,0,0,31,0,0,0},{0,0,0,0,0,0,4},{14,17,1,2,4,0,4}
};
static void menuText(std::vector<uint32_t>& image,int x,int y,const std::string& text,uint32_t color,int scale=2,int width=768) {
    for(unsigned char ch:text) {
        if(ch>='a' && ch<='z')ch-=32;
        int index=ch>='A'&&ch<='Z'?ch-'A':ch>='0'&&ch<='9'?26+ch-'0':ch==' '?36:ch=='-'?37:ch=='.'?38:39;
        for(int dy=0;dy<7;dy++)for(int dx=0;dx<5;dx++)if(menuFont[index][dy]&(16>>dx))
            for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++) {
                int px=x+dx*scale+sx,py=y+dy*scale+sy;
                if(px>=0&&px<width&&py>=0&&size_t(py)<image.size()/width)image[py*width+px]=color;
            }
        x+=6*scale;
    }
}
