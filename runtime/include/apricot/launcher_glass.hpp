// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace apricot {
// Static background only: blur/tint once before the frame loop. The PS5 canvas
// supports cutout transparency, so blend glass against the actual artwork here.
inline void launcher_glass(std::vector<uint32_t>& canvas,const std::vector<uint8_t>& rgba,unsigned source_width,unsigned source_height,int left=580,int top=562,int right=1248,int bottom=710,bool output_bgra=true){
 if(canvas.size()!=1280*720||!source_width||!source_height||source_width>3840||source_height>2160||rgba.size()!=size_t(source_width)*source_height*4)return;
 if(left<0||top<0||right>1280||bottom>720||right-left<28||bottom-top<28)return;
 constexpr int radius=14;
 for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
   int dx=std::max({left+radius-x,0,x-(right-radius-1)}),dy=std::max({top+radius-y,0,y-(bottom-radius-1)});
   if(dx*dx+dy*dy>radius*radius)continue;
   unsigned sum[3]{};
   for(int by=-2;by<=2;++by)for(int bx=-2;bx<=2;++bx){
     unsigned sx=unsigned(std::clamp(x+bx*3,0,1279))*source_width/1280;
     unsigned sy=unsigned(std::clamp(y+by*3,0,719))*source_height/720;
     auto p=rgba.data()+(size_t(sy)*source_width+sx)*4;
     for(unsigned c=0;c<3;++c)sum[c]+=p[c];
   }
   unsigned tint[3]={231,242,247};unsigned alpha=y<top+2?200:112;
   uint32_t out=0xff000000u;
   for(unsigned c=0;c<3;++c){unsigned value=((sum[c]/25)*(255-alpha)+tint[c]*alpha+127)/255;out|=value<<((output_bgra?2-c:c)*8);}
   canvas[size_t(y)*1280+x]=out;
 }
}
inline int centered_text_x(float center,size_t letters,int scale){return int(center)-int(letters)*6*scale/2;}
inline double confirmation_spin(double base,double progress){
 double t=std::clamp(progress,0.,1.);return base+12.566370614359172*(1.-(1.-t)*(1.-t)*(1.-t));
}
}
