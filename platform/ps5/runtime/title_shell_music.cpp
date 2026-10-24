// SPDX-License-Identifier: GPL-3.0-or-later
#include "title_shell_music.h"
#include "sha256.h"
#include "../../vendor/at9/at9.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <exception>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
namespace {
bool read(const std::string&p,std::vector<uint8_t>&b,size_t limit){FILE*f=fopen(p.c_str(),"rb");if(!f)return false;bool ok=!fseek(f,0,SEEK_END);long n=ftell(f);ok=ok&&n>=0&&size_t(n)<=limit&&!fseek(f,0,SEEK_SET);if(ok){b.resize(n);ok=fread(b.data(),1,b.size(),f)==b.size();}fclose(f);return ok;}
bool write(const std::string&p,const std::vector<uint8_t>&b){FILE*f=fopen(p.c_str(),"wbx");if(!f)return false;bool ok=fwrite(b.data(),1,b.size(),f)==b.size();if(fflush(f)||fsync(fileno(f)))ok=false;if(fclose(f))ok=false;std::vector<uint8_t>v;return ok&&read(p,v,b.size())&&v==b;}
bool replace(const std::string&p,const std::vector<uint8_t>&b){
 struct stat st{};if(!lstat(p.c_str(),&st)&&!S_ISREG(st.st_mode))return false;
 std::vector<uint8_t>old;if(read(p,old,2*1024*1024)&&old==b)return true;
 if(!access(p.c_str(),F_OK)){
  if(old.empty())return false;
  std::string backup=p+".apricot-before-shellmusic";
  if(access(backup.c_str(),F_OK)&&!write(backup,old))return false;
 }
 std::string tmp=p+".apricot-shellmusic-new";
 if(!write(tmp,b))return false;
 return !rename(tmp.c_str(),p.c_str());
}
bool install(const std::string&dir,const char*title,const std::vector<uint8_t>&at9,const std::string&level){
 std::vector<uint8_t>b;if(!read(dir+"/param.json",b,256*1024))return false;
 std::string json(b.begin(),b.end());if(json.find(std::string("\"")+title+"\"")==std::string::npos)return false;
 const std::string key="\"loudnessSnd0\"";auto k=json.find(key);if(k==std::string::npos||json.find(key,k+key.size())!=std::string::npos)return false;
 auto colon=json.find(':',k+key.size());if(colon==std::string::npos)return false;
 auto a=json.find_first_not_of(" \r\n\t",colon+1);if(a==std::string::npos||json[a]!='\"')return false;
 auto z=json.find('"',a+1);if(z==std::string::npos)return false;
 json.replace(a+1,z-a-1,level);b.assign(json.begin(),json.end());
 return replace(dir+"/snd0.at9",at9)&&replace(dir+"/param.json",b);
}
}
bool prepare_shell_music(const std::vector<uint8_t>&pcm,const std::string&persistent,const std::string&app,const std::string&cache,const char*title,std::string&error){
 try{
  auto u32=[&](size_t i){return uint32_t(pcm[i])|uint32_t(pcm[i+1])<<8|uint32_t(pcm[i+2])<<16|uint32_t(pcm[i+3])<<24;};
  if(pcm.size()<64||memcmp(pcm.data(),"APAU0001",8)||u32(8)!=48000||pcm[12]!=2||pcm[13]||pcm[14]!=16||pcm[15]||u32(16)==0||u32(16)>48000*180||pcm.size()!=64+size_t(u32(16))*4){error="Invalid shell music PCM";return false;}
  uint8_t hash[32];oq_sha256(pcm.data(),pcm.size(),hash);char hex[65];for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",hash[i]);
  std::string base=persistent+"/shell-music-v1-"+hex;std::vector<uint8_t>encoded,level;
  if(!read(base+".at9",encoded,2*1024*1024)||!read(base+".loudness",level,32)){
   at9::Audio audio;audio.rate=48000;audio.channels=2;size_t frames=std::min(size_t(u32(16)),size_t(48000*60));audio.pcm.reserve(frames*2);
   for(size_t i=64;i<64+frames*4;i+=2)audio.pcm.push_back(float(int16_t(uint16_t(pcm[i])|uint16_t(pcm[i+1])<<8))/32768.f);
   at9::Options options;options.duration_sec=60;options.loop=true;
   auto result=at9::detail::make_snd0_ex(audio,options,nullptr,nullptr,1);
   if(!std::isfinite(result.loudness_lufs)){error="Silent shell music";return false;}
   encoded=std::move(result.at9);level.assign(result.loudness_param.begin(),result.loudness_param.end());
   if(!replace(base+".at9",encoded)||!replace(base+".loudness",level)){error="Shell music cache write failed";return false;}
  }
  if(encoded.size()<12||memcmp(encoded.data(),"RIFF",4)||memcmp(encoded.data()+8,"WAVE",4)){error="Invalid cached shell music";return false;}
  std::string loudness(level.begin(),level.end());if(loudness.empty()||loudness.find_first_not_of("-0123456789.")!=std::string::npos){error="Invalid shell loudness";return false;}
  if(!install(app,title,encoded,loudness)){error="Own-title app media install incomplete";return false;}
  struct stat dir{};
  if(lstat(cache.c_str(),&dir)){
   // Homebrew shell music is served from its app sce_sys folder. Unlike a
   // packaged game it need not have an appmeta cache. Never invent that cache
   // or require access outside the title's filesystem view.
   if(errno==ENOENT)return true;
   error="Own-title appmeta directory unavailable";return false;
  }else if(!S_ISDIR(dir.st_mode)){error="Own-title appmeta is not a directory";return false;}
  // A newly registered homebrew tester may not have a shell metadata copy yet.
  // Seed only this title's missing param from its already verified app metadata.
  if(access((cache+"/param.json").c_str(),F_OK)){
   std::vector<uint8_t>metadata;if(!read(app+"/param.json",metadata,256*1024)||!write(cache+"/param.json",metadata)){error="Own-title appmeta seed failed";return false;}
  }
  if(!install(cache,title,encoded,loudness)){error="Own-title cache media install incomplete";return false;}
  return true;
 }catch(const std::exception&e){error=e.what();return false;}
}
