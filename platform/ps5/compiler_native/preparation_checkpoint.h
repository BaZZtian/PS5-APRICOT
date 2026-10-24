// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../runtime/game_module.h"
#include "../runtime/guest.h"
#include "../runtime/sha256.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace apricot::aot {
// Bump this whenever preparation semantics change. Checkpoints are private,
// disposable compiler output; they are never game saves.
constexpr uint32_t PREPARATION_COMPILER_EPOCH=2;
namespace checkpoint_detail {
inline void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
inline void put64(uint8_t*p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=uint8_t(n>>(8*i));}
inline uint32_t get32(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint64_t get64(const uint8_t*p){return get32(p)|uint64_t(get32(p+4))<<32;}
inline void identity_bytes(const GameIdentity&i,uint8_t*out){memset(out,0,40);memcpy(out,i.game_code,4);memcpy(out+4,i.maker_code,2);out[6]=i.revision;memcpy(out+8,i.sha256,32);}
inline bool sync_parent(const std::string&path){auto slash=path.find_last_of('/');std::string dir=slash==std::string::npos?".":(slash?path.substr(0,slash):"/");int fd=open(dir.c_str(),O_RDONLY);if(fd<0)return false;bool ok=fsync(fd)==0;close(fd);return ok;}
}
inline bool save_preparation_checkpoint(const std::string&path,const GameIdentity&id,const std::vector<uint8_t>&module){
    if(module.size()<256||module.size()>256u*1024*1024+16u*1024*1024)return false;
    uint8_t h[144]{};memcpy(h,"APCP0001",8);checkpoint_detail::put32(h+8,1);checkpoint_detail::put32(h+12,PREPARATION_COMPILER_EPOCH);checkpoint_detail::put32(h+16,GUEST_ABI);checkpoint_detail::put64(h+24,module.size());checkpoint_detail::identity_bytes(id,h+32);oq_sha256(module.data(),module.size(),h+72);oq_sha256(h,sizeof(h),h+104,104,32);
    std::string tmp=path+".tmp-"+std::to_string(getpid());int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    FILE*f=fdopen(fd,"wb");if(!f){close(fd);unlink(tmp.c_str());return false;}bool ok=fwrite(h,1,sizeof(h),f)==sizeof(h)&&fwrite(module.data(),1,module.size(),f)==module.size()&&fflush(f)==0&&fsync(fd)==0;ok=(fclose(f)==0)&&ok;
    if(ok)ok=rename(tmp.c_str(),path.c_str())==0&&checkpoint_detail::sync_parent(path);if(!ok)unlink(tmp.c_str());return ok;
}
inline bool load_preparation_checkpoint(const std::string&path,const GameIdentity&id,std::vector<uint8_t>&module){
    module.clear();FILE*f=fopen(path.c_str(),"rb");if(!f)return false;uint8_t h[144],expected_id[40],actual[32];bool ok=fread(h,1,sizeof(h),f)==sizeof(h);
    if(ok){checkpoint_detail::identity_bytes(id,expected_id);uint8_t copy[144];memcpy(copy,h,sizeof(copy));memset(copy+104,0,32);oq_sha256(copy,sizeof(copy),actual);uint64_t n=checkpoint_detail::get64(h+24);ok=!memcmp(h,"APCP0001",8)&&checkpoint_detail::get32(h+8)==1&&checkpoint_detail::get32(h+12)==PREPARATION_COMPILER_EPOCH&&checkpoint_detail::get32(h+16)==GUEST_ABI&&!checkpoint_detail::get32(h+20)&&n>=256&&n<=256u*1024*1024+16u*1024*1024&&n<=SIZE_MAX&&memcmp(h+32,expected_id,40)==0&&memcmp(h+104,actual,32)==0;}
    if(ok){uint64_t n=checkpoint_detail::get64(h+24);module.resize(size_t(n));ok=fread(module.data(),1,module.size(),f)==module.size()&&fgetc(f)==EOF;oq_sha256(module.data(),module.size(),actual);ok=ok&&!memcmp(actual,h+72,32);}
    if(ok)for(unsigned i=136;i<144;++i)if(h[i])ok=false;
    ok=(fclose(f)==0)&&ok;if(!ok)module.clear();return ok;
}
inline bool remove_preparation_checkpoint(const std::string&path){if(unlink(path.c_str())==0)return checkpoint_detail::sync_parent(path);return errno==ENOENT;}
}
