// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace apricot {
struct EditionBundle {
    uint32_t rom_bytes=0,media_revision=0;
    uint8_t identity[40]{}; // game[4], maker[2], revision, zero, ROM SHA-256
    uint8_t module_sha[32]{},label_sha[32]{},audio_sha[32]{},refs_sha[32]{};
    bool gold()const{return identity[2]=='K';}
    bool load(const std::vector<uint8_t>& data){
        *this={};if(data.size()!=256||memcmp(data.data(),"APED0001",8))return false;
        auto u32=[](const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
        if(u32(data.data()+8)!=256)return false;
        uint32_t bytes=u32(data.data()+12);
        if(bytes<1024*1024||bytes>0x20000000||(bytes&(bytes-1)))return false;
        auto p=data.data()+16;
        if(p[0]!='I'||p[1]!='P'||(p[2]!='K'&&p[2]!='G')||(p[3]!='D'&&p[3]!='E')||p[4]!='0'||p[5]!='1'||p[6]||p[7])return false;
        if(u32(data.data()+184)>1)return false;
        for(unsigned i=188;i<256;++i)if(data[i])return false;
        auto nonzero=[](const uint8_t*p){return std::any_of(p,p+32,[](uint8_t b){return b!=0;});};
        if(!nonzero(data.data()+24)||!nonzero(data.data()+56)||!nonzero(data.data()+88))return false;
        rom_bytes=bytes;media_revision=u32(data.data()+184);memcpy(identity,p,40);memcpy(module_sha,data.data()+56,32);memcpy(label_sha,data.data()+88,32);
        memcpy(audio_sha,data.data()+120,32);memcpy(refs_sha,data.data()+152,32);return true;
    }
};
}
