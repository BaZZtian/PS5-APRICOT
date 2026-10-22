// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_module.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#ifndef OQ_PS5
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace {
constexpr uint64_t maximum=256u*1024*1024,maximum_relocations=1048576;
uint32_t read32(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint64_t read64(const uint8_t*p){return read32(p)|uint64_t(read32(p+4))<<32;}
bool inside(const void *base,size_t bytes,const void *pointer,size_t count) {
    auto begin=reinterpret_cast<uintptr_t>(base),at=reinterpret_cast<uintptr_t>(pointer);
    return at>=begin&&at-begin<=bytes&&count<=bytes-(at-begin);
}
}
bool NativeGameModule::load(const char *path,const GameIdentity &expected) {
    close();FILE *f=fopen(path,"rb");if(!f){error="native game image unavailable";return false;}
    bool ok=!fseek(f,0,SEEK_END);long n=ok?ftell(f):-1;
    if(n<256||uint64_t(n)>256+maximum+maximum_relocations*16){fclose(f);error="native image length invalid";return false;}
    if(fseek(f,0,SEEK_SET)){fclose(f);error="native image seek failed";return false;}
    std::vector<uint8_t> data(size_t(n),0);
    ok=fread(data.data(),1,data.size(),f)==data.size();ok=(!fclose(f))&&ok;
    if(!ok){error="native image read failed";return false;}
    return from_bytes(data.data(),data.size(),expected);
}
bool NativeGameModule::from_bytes(const uint8_t*d,size_t n,const GameIdentity &expected) {
    close();error="invalid native game header";
    if(!d||n<256||memcmp(d,"OQGAME03",8)||read32(d+8)!=3||read32(d+12)!=62||read32(d+16)!=256||read32(d+20)!=1||
       read32(d+72)!=GUEST_ABI||read32(d+76)!=sizeof(GuestCpu)||read32(d+80)!=sizeof(GuestHost)||read32(d+84)||d[95])return false;
    for(unsigned i=160;i<256;++i)if(d[i])return false;
    uint64_t length=read64(d+24),relocations=read64(d+32),offset9=read64(d+40),offset7=read64(d+48),catalog_offset=read64(d+56);
    if(!length||length>maximum||relocations>maximum_relocations||n!=256+length+relocations*16||
       offset9>=length||offset7>=length||catalog_offset>=length||read32(d+64)&3||read32(d+68)&3)return false;
    if(memcmp(d+88,expected.game_code,4)||memcmp(d+92,expected.maker_code,2)||d[94]!=expected.revision||memcmp(d+96,expected.sha256,32)) {
        error="native image belongs to another ROM";return false;
    }
    uint8_t digest[32];oq_sha256(d,n,digest,128,32);
    if(memcmp(digest,d+128,32)){error="native game image SHA-256 mismatch";return false;}
    const uint8_t *relocation_data=d+256+size_t(length);uint64_t previous=0;
    for(uint64_t i=0;i<relocations;++i) {
        uint64_t target=read64(relocation_data+i*16),addend=read64(relocation_data+i*16+8);
        if(target&7||target>length||length-target<8||addend>=length||(i&&target<=previous)) {
            error="invalid native relative relocation";return false;
        }
        previous=target;
    }
#ifdef OQ_PS5
    ps5_exec_request request{};request.bytes=size_t(length);request.flags=PS5_EXEC_TOGGLED;
    if(ps5_exec_alloc(&request,&region)){error="native executable allocation failed";return false;}
    code=region.base;allocated=region.bytes;void *writable=region.write_view;
#else
    long page_size=sysconf(_SC_PAGESIZE);
    if(page_size<1){error="native page size unavailable";return false;}
    size_t page=size_t(page_size);allocated=(size_t(length)+page-1)/page*page;
    code=mmap(nullptr,allocated,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(code==MAP_FAILED){code=nullptr;allocated=0;error="native executable allocation failed";return false;}
    void *writable=code;
#endif
    image_bytes=size_t(length);memcpy(writable,d+256,image_bytes);
    for(uint64_t i=0;i<relocations;++i) {
        auto target=read64(relocation_data+i*16),addend=read64(relocation_data+i*16+8);
        uintptr_t value=reinterpret_cast<uintptr_t>(code)+size_t(addend);
        memcpy(static_cast<uint8_t*>(writable)+size_t(target),&value,sizeof(value));
    }
#ifdef OQ_PS5
    bool protected_ok=ps5_exec_protect(&region,0,image_bytes,false)==0;
#else
    bool protected_ok=mprotect(code,allocated,PROT_READ|PROT_EXEC)==0;
#endif
    if(!protected_ok){close();error="native RX protection failed";return false;}
    using Catalog=const NativeOverlayInfo *(*)(unsigned*);
    auto get_catalog=reinterpret_cast<Catalog>(static_cast<uint8_t*>(code)+size_t(catalog_offset));
    unsigned count=0;const NativeOverlayInfo *info=get_catalog(&count);
    bool valid=count<=256&&(!count?!info:inside(code,image_bytes,info,size_t(count)*sizeof(*info)))&&
               (!count||reinterpret_cast<uintptr_t>(info)%alignof(NativeOverlayInfo)==0);
    for(unsigned i=0;valid&&i<count;++i) {
        const auto &item=info[i];
        valid=(item.core==9||item.core==7)&&item.id<4096&&item.base>=0x02000000&&item.base<0x02400000&&
              item.bytes&&item.bytes<=0x02400000-item.base&&item.code_count&&item.code_count<=1048576&&
              inside(code,image_bytes,item.code,size_t(item.code_count)*sizeof(*item.code))&&
              reinterpret_cast<uintptr_t>(item.code)%alignof(NativeCodeRange)==0&&!(item.flags&~1u);
        for(unsigned r=0;valid&&r<item.code_count;++r)valid=item.code[r].begin>=item.base&&item.code[r].end>item.code[r].begin&&
            item.code[r].end-item.base<=item.bytes&&(!r||item.code[r].begin>=item.code[r-1].end);
        for(unsigned j=0;valid&&j<i;++j)valid=info[j].core!=item.core||info[j].id!=item.id;
    }
    if(!valid){close();error="invalid native overlay catalog";return false;}
    arm9=reinterpret_cast<GuestEntry>(static_cast<uint8_t*>(code)+size_t(offset9));
    arm7=reinterpret_cast<GuestEntry>(static_cast<uint8_t*>(code)+size_t(offset7));
    entry9=read32(d+64);entry7=read32(d+68);identity=expected;catalog=info;catalog_count=count;error="none";return true;
}
void NativeGameModule::close() {
#ifdef OQ_PS5
    if(region.base)ps5_exec_free(&region);region={};
#else
    if(code)munmap(code,allocated);
#endif
    code=nullptr;allocated=image_bytes=0;arm9=arm7=nullptr;entry9=entry7=0;identity={};catalog=nullptr;catalog_count=0;
}
