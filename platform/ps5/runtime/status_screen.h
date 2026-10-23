#pragma once
#include "apricot/launcher_glass.hpp"
namespace apricot {
// Status screens use RGBA, matching Ps5GpuDisplay::present's default contract.
inline std::vector<uint32_t> status_background(const std::vector<uint8_t>&rgba,unsigned w,unsigned h,bool glass=true){
 std::vector<uint32_t> out(1280*720,0xff201810u);
 if(!w||!h||w>3840||h>2160||rgba.size()!=size_t(w)*h*4)return out;
 for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x){auto p=rgba.data()+(size_t(y*h/720)*w+x*w/1280)*4;out[y*1280+x]=0xff000000u|uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16;}
 if(glass)launcher_glass(out,rgba,w,h,20,180,1260,590,false);return out;
}
inline void status_logo(std::vector<uint32_t>&canvas,const std::vector<uint8_t>&bgra,unsigned left=16,unsigned top=12,unsigned size=48,bool output_bgra=false,unsigned source_size=128){
 if(canvas.size()!=1280*720||!source_size||source_size>512||bgra.size()!=size_t(source_size)*source_size*4||!size||size>128||left>1280-size||top>720-size)return;
 for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){auto p=bgra.data()+((y*source_size/size)*source_size+x*source_size/size)*4;auto&dst=canvas[(top+y)*1280+left+x];
  if(!p[3])continue;
  // Keep straight alpha when the canvas is a transparent overlay on the UHD
  // artwork. Forcing alpha=255 here produced the reported black rectangle.
  unsigned a=p[3],da=dst>>24,den=a*255+da*(255-a);uint32_t color=((den+127)/255)<<24;
  for(unsigned c=0;c<3;++c){unsigned value=(unsigned(p[output_bgra?c:2-c])*a*255+((dst>>(8*c))&255)*da*(255-a)+den/2)/den;color|=value<<(8*c);}dst=color;
 }
}
}
