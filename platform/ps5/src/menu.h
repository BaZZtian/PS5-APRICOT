// SPDX-License-Identifier: GPL-3.0-or-later
// Native UI text using the licensed Oxanium release atlas.
#pragma once
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>
#include "ui_font_atlas.h"
// Area-filter once per requested size. Nearest sampling of the large atlas
// discarded thin strokes and made even antialiased source glyphs look jagged.
static const std::vector<unsigned char>& menuCoverage(int scale) {
    static std::vector<unsigned char> cache[17];auto& out=cache[scale];
    if(!out.empty())return out;
    const unsigned w=6*scale,h=8*scale;out.resize(95*w*h);
    for(unsigned g=0;g<95;++g)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const unsigned x0=x*48,x1=(x+1)*48,y0=y*64,y1=(y+1)*64;
        unsigned sum=0;
        for(unsigned sy=y0/h;sy<(y1+h-1)/h;++sy)for(unsigned sx=x0/w;sx<(x1+w-1)/w;++sx){
            unsigned wx=std::min(x1,(sx+1)*w)-std::max(x0,sx*w);
            unsigned wy=std::min(y1,(sy+1)*h)-std::max(y0,sy*h);
            sum+=apricotFontAtlas[g][sy*48+sx]*wx*wy;
        }
        out[(g*h+y)*w+x]=static_cast<unsigned char>((sum+1536)/3072);
    }return out;
}
static void menuText(std::vector<uint32_t>& image,int x,int y,const std::string& text,uint32_t color,int scale=2,int width=768) {
    if(scale<1||scale>16||width<1)return;
    const auto& coverage=menuCoverage(scale);
    for(unsigned char ch:text) {
        unsigned glyph=ch>=32&&ch<=126?ch-32:'?'-32;
        for(int dy=0;dy<8*scale;++dy)for(int dx=0;dx<6*scale;++dx){
            int px=x+dx,py=y+dy;if(px<0||px>=width||py<0||size_t(py)>=image.size()/width)continue;
            unsigned alpha=coverage[(glyph*8*scale+dy)*6*scale+dx];if(!alpha)continue;
            auto&pixel=image[size_t(py)*width+px];if(!(pixel>>24)){pixel=(color&0xffffffu)|(alpha<<24);continue;}
            uint32_t mixed=0xff000000u;for(unsigned c=0;c<3;++c)mixed|=((((color>>(8*c))&255)*alpha+((pixel>>(8*c))&255)*(255-alpha)+127)/255)<<(8*c);pixel=mixed;
        }
        x+=6*scale;
    }
}
static const std::vector<std::pair<int,int>>& menuGlyphWidths(int scale){
    static std::vector<std::pair<int,int>> cache[17];auto& result=cache[scale];if(!result.empty())return result;
    const auto& pixels=menuCoverage(scale);int w=6*scale,h=8*scale;result.resize(95);
    for(int g=0;g<95;++g){int first=w,last=-1;for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(pixels[(g*h+y)*w+x]){first=std::min(first,x);last=std::max(last,x);}
        result[g]=last<0?std::make_pair(0,3*scale):std::make_pair(first,last-first+1+std::max(1,scale*2/3));
    }return result;
}
static int menuTextWidth(const std::string& text,int scale=3){
    if(scale<1||scale>16)return 0;const auto& widths=menuGlyphWidths(scale);int width=0;
    for(unsigned char c:text)width+=widths[c>=32&&c<=126?c-32:'?'-32].second;return width;
}
static void menuProportionalText(std::vector<uint32_t>& image,int x,int y,const std::string& text,uint32_t color,int scale=3,int width=1280){
    if(scale<1||scale>16)return;const auto& widths=menuGlyphWidths(scale);
    for(unsigned char c:text){auto span=widths[c>=32&&c<=126?c-32:'?'-32];menuText(image,x-span.first,y,std::string(1,char(c)),color,scale,width);x+=span.second;}
}
