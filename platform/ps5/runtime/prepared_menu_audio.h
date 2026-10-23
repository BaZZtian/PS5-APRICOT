// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_module.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <atomic>
struct PreparedMenuAudio {
 std::vector<int16_t> samples;uint32_t cursor=0,frames=0;
 bool load(const std::vector<uint8_t>& data,const GameIdentity& identity){
   if(data.size()<64||data.size()>64*1024*1024||memcmp(data.data(),"APAU0001",8)||memcmp(data.data()+32,identity.sha256,32))return false;
   auto u32=[&](unsigned at){uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(data[at+n])<<(8*n);return v;};
   uint32_t count=u32(16);
   if(u32(8)!=48000||data[12]!=2||data[13]||data[14]!=16||data[15]||!count||count>48000*180||u32(20)||u32(24)!=count||u32(28)||data.size()!=64+size_t(count)*4)return false;
   std::vector<int16_t> decoded(size_t(count)*2);
   for(size_t n=0;n<decoded.size();++n){uint16_t v=uint16_t(data[64+n*2])|uint16_t(data[65+n*2])<<8;decoded[n]=int16_t(v);}
   samples=std::move(decoded);frames=count;cursor=0;return true;
 }
 static void fill(int16_t* output,int count,void* context){auto& q=*static_cast<PreparedMenuAudio*>(context);
   for(int n=0;n<count;++n){if(!q.frames){output[n*2]=output[n*2+1]=0;continue;}
     unsigned fade=q.frames<256?q.frames/2:128;if(!fade)fade=1;
     unsigned gain=std::min({fade,q.cursor+1,q.frames-q.cursor});
     output[n*2]=int16_t(int64_t(q.samples[q.cursor*2])*gain/fade);
     output[n*2+1]=int16_t(int64_t(q.samples[q.cursor*2+1])*gain/fade);
     if(++q.cursor==q.frames)q.cursor=0;
   }
 }
};
// Load tracks on the main thread BEFORE audio_start. After that only the audio
// callback owns cursors; selection is a lock-free request, with no I/O/reopen.
struct PreparedMenuMixer {
 PreparedMenuAudio tracks[2];
 std::atomic<unsigned> requested{0};
 unsigned active=0,previous=0,transition=1024;
 void select(unsigned index){if(index<2)requested.store(index,std::memory_order_release);}
 static void fill(int16_t* output,int count,void* context){
   auto& m=*static_cast<PreparedMenuMixer*>(context);
   unsigned next=m.requested.load(std::memory_order_acquire);
   if(next!=m.active){m.previous=m.active;m.active=next;m.transition=0;}
   PreparedMenuAudio::fill(output,count,&m.tracks[m.active]);
   for(int n=0;n<count&&m.transition<1024;++n,++m.transition){
     int16_t old[2]{};PreparedMenuAudio::fill(old,1,&m.tracks[m.previous]);
     for(unsigned c=0;c<2;++c)output[n*2+c]=int16_t((int32_t(old[c])*int32_t(1024-m.transition)+int32_t(output[n*2+c])*int32_t(m.transition))/1024);
   }
 }
};
