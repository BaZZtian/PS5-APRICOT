// SPDX-License-Identifier: GPL-3.0-or-later
// Optional, user-local exact-image rejection accelerator; no game bytes built in.
#pragma once
#include "game_module.h"
#include "overlays.h"
inline bool load_identity_references(const std::vector<uint8_t>& data,const GameIdentity& identity,NativeOverlays& overlays){
    auto u32=[&](size_t at){uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(data[at+n])<<(8*n);return v;};
    auto u64=[&](size_t at){uint64_t v=0;for(unsigned n=0;n<8;++n)v|=uint64_t(data[at+n])<<(8*n);return v;};
    if(data.size()<64||data.size()>64*1024*1024||memcmp(data.data(),"APRF0001",8)||memcmp(data.data()+16,identity.sha256,32))return false;
    unsigned count=u32(8);if(!count||count>overlays.count||u32(12))return false;
    for(unsigned n=48;n<64;++n)if(data[n])return false;
    size_t end=64+size_t(count)*24;if(end>data.size())return false;
    std::vector<uint8_t> seen(overlays.count,0);
    // Validate every extent/digest before modifying live reference state.
    for(unsigned n=0;n<count;++n){size_t at=64+n*24;unsigned index=u32(at);uint64_t offset=u64(at+8),bytes=u64(at+16);
        if(index>=overlays.count||seen[index]||u32(at+4)||offset!=end||bytes!=overlays.catalog[index].bytes||bytes>data.size()-end)return false;
        seen[index]=1;OqSha256Stream hash;hash.update(data.data()+end,size_t(bytes));uint8_t digest[32];hash.digest(digest);
        if(memcmp(digest,overlays.catalog[index].sha256,32))return false;
        end+=size_t(bytes);
    }
    if(end!=data.size())return false;
    for(unsigned n=0;n<count;++n){size_t at=64+n*24;unsigned index=u32(at);size_t offset=size_t(u64(at+8)),bytes=size_t(u64(at+16));
        overlays.reference_images[index].assign(data.data()+offset,data.data()+offset+bytes);
    }
    return true;
}
