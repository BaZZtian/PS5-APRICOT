// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
namespace apricot {
inline std::vector<std::uint32_t> scale_screen_nearest(const std::uint32_t* source,unsigned w,unsigned h){
  if(!source||!w||!h||w>1280||h>720)return {};
  std::vector<std::uint32_t> out(w*h);unsigned previous=192;
  for(unsigned y=0;y<h;++y){unsigned sy=y*192/h;
    if(sy==previous){std::copy_n(out.data()+(y-1)*w,w,out.data()+y*w);continue;}
    for(unsigned x=0;x<w;++x)out[y*w+x]=source[sy*256+x*256/w];previous=sy;
  }
  return out;
}
// Center-aligned bilinear presentation of a native 256x192 screen.
// This filters display pixels, without claiming a higher-resolution renderer.
inline std::vector<std::uint32_t> scale_screen(const std::uint32_t* source,unsigned w,unsigned h) {
  if(!source||!w||!h||w>1280||h>720)return {};
  std::vector<std::uint32_t> out(w*h);
  struct Column {unsigned a,b,weight;};
  std::vector<Column> columns(w);
  for(unsigned x=0;x<w;++x){
    int fx=int((std::uint64_t(2*x+1)*256*256)/(2*w))-128;
    if(fx<0)fx=0;if(fx>255*256)fx=255*256;
    unsigned a=unsigned(fx)>>8;columns[x]={a,a<255?a+1:a,unsigned(fx)&255};
  }
  using Row=std::vector<std::array<std::uint16_t,4>>;
  Row row0(w),row1(w);int cached0=-1,cached1=-1;
  auto horizontal=[&](Row& row,unsigned sy){
    for(unsigned x=0;x<w;++x){auto c=columns[x];auto a=source[sy*256+c.a],b=source[sy*256+c.b];
      for(unsigned channel=0;channel<4;++channel){unsigned shift=channel*8;
        row[x][channel]=std::uint16_t(((a>>shift)&255)*(256-c.weight)+((b>>shift)&255)*c.weight);
      }
    }
  };
  for(unsigned y=0;y<h;++y) {
    int fy=int((std::uint64_t(2*y+1)*192*256)/(2*h))-128;
    if(fy<0)fy=0;if(fy>191*256)fy=191*256;
    unsigned y0=unsigned(fy)>>8,y1=y0<191?y0+1:y0,wy=unsigned(fy)&255;
    if(cached0!=int(y0)){
      if(cached1==int(y0)){row0.swap(row1);int previous=cached0;cached0=cached1;cached1=previous;}
      else {horizontal(row0,y0);cached0=int(y0);}
    }
    if(y1!=y0&&cached1!=int(y1)){horizontal(row1,y1);cached1=int(y1);}
    const Row& bottom=y1==y0?row0:row1;
    for(unsigned x=0;x<w;++x) {
      std::uint32_t pixel=0;
      for(unsigned channel=0;channel<4;++channel) {
        unsigned value=(unsigned(row0[x][channel])*(256-wy)+unsigned(bottom[x][channel])*wy+32768)>>16;
        pixel|=value<<(channel*8);
      }
      out[y*w+x]=pixel;
    }
  }
  return out;
}
}
