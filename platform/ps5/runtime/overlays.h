// SPDX-License-Identifier: GPL-3.0-or-later
// Original native-overlay identity and code-write tracking. No game metadata.
#pragma once
#include "sha256.h"
#include "sha256_stream.h"
#include "overlay_types.h"
#include <vector>
#include <algorithm>
struct NativeOverlays {
    const NativeOverlayInfo *catalog=nullptr;unsigned count=0;
    std::vector<uint8_t> resident,checked;
    std::vector<unsigned> by_extent;
    std::vector<std::vector<uint8_t>> reference_images;uint64_t reference_rejections=0;
    const uint8_t *memory=nullptr;unsigned memory_size=0;
    unsigned bindings=0,invalidations=0;uint64_t identity_checks=0;
    uint64_t identity_hash_bytes=0;
    void configure(const NativeOverlayInfo *info,unsigned size) {
        catalog=info;count=size;reference_images.clear();reference_images.resize(size);resident.assign(size,0);checked.assign(size,0);memory=nullptr;memory_size=0;
        by_extent.resize(count);for(unsigned n=0;n<count;++n)by_extent[n]=n;
        std::sort(by_extent.begin(),by_extent.end(),[&](unsigned a,unsigned b){return catalog[a].base<catalog[b].base||(catalog[a].base==catalog[b].base&&catalog[a].bytes<catalog[b].bytes);});
    }
    bool attach_reference(unsigned index,const uint8_t* data,size_t bytes) {
        if(index>=count||!data||bytes!=catalog[index].bytes)return false;
        OqSha256Stream hash;hash.update(data,bytes);uint8_t digest[32];hash.digest(digest);
        if(memcmp(digest,catalog[index].sha256,32))return false;
        reference_images[index].assign(data,data+bytes);return true;
    }
    bool resolve(unsigned core,uint32_t pc,const uint8_t *ram,unsigned ram_size,uint32_t &id) {
        if(memory!=ram||memory_size!=ram_size) {
            resident.assign(count,0);checked.assign(count,0);memory=ram;memory_size=ram_size;
        }
        const NativeOverlayInfo *match=nullptr;unsigned selected=0;
        OqSha256Stream stream;uint32_t stream_base=0;unsigned consumed=0;
        for(unsigned n:by_extent) {
            const auto &item=catalog[n];
            if(item.core!=core||pc<item.base||pc-item.base>=item.bytes)continue;
            if(!resident[n]) {
                if(checked[n])continue;
                if(item.base<0x02000000||item.base-0x02000000>ram_size||item.bytes>ram_size-(item.base-0x02000000))continue;
                // A private reference is accepted only after its full SHA matches
                // the already validated native catalog. An exact byte mismatch
                // can reject a candidate; every positive still follows SHA below.
                const auto& reference=reference_images[n];
                if(!reference.empty()&&memcmp(ram+item.base-0x02000000,reference.data(),item.bytes)){
                    checked[n]=1;++reference_rejections;continue;
                }
                // Candidates at the same base share prefixes. Hash those bytes
                // once per resolve, retaining the exact full-image SHA check.
                if(stream_base!=item.base){stream=OqSha256Stream{};stream_base=item.base;consumed=0;}
                unsigned added=item.bytes-consumed;
                stream.update(ram+item.base-0x02000000+consumed,added);
                consumed=item.bytes;identity_hash_bytes+=added;
                uint8_t digest[32];stream.digest(digest);++identity_checks;checked[n]=1;
                if(memcmp(digest,item.sha256,32))continue;
                resident[n]=1;++bindings;
            }
            if(match)return false; // Never guess between overlapping identities.
            match=&item;selected=n;
        }
        if(!match)return false;
        id=catalog[selected].id;return true;
    }
    bool before_write(uint32_t address,unsigned width,uint32_t value,const uint8_t *old,uint32_t executing_pc) {
        bool changed=false;for(unsigned n=0;n<width;++n)changed|=old[n]!=uint8_t(value>>(8*n));
        if(!changed)return true;
        for(unsigned n=0;n<count;++n) {
            const auto &item=catalog[n];
            if(uint64_t(address)>=uint64_t(item.base)+item.bytes||uint64_t(address)+width<=item.base)continue;
            // A failed identity comparison stays valid only while the ENTIRE
            // image is unchanged, including writable data outside code ranges.
            checked[n]=0;if(!resident[n])continue;
            // Module validation requires sorted, disjoint code ranges.
            unsigned left=0,right=item.code_count;
            while(left<right){unsigned mid=left+(right-left)/2;if(item.code[mid].end<=address)left=mid+1;else right=mid;}
            bool hits=left<item.code_count&&uint64_t(address)+width>item.code[left].begin;
            if(!hits)continue;
            if(executing_pc>=item.base&&executing_pc-item.base<item.bytes) {
                // A guarded overlay can write guest bytes. Its immutable native
                // code verifies each instruction against a finite compiled set;
                // an unseen instruction stops, never decodes or compiles itself.
                if(item.flags&1)continue;
                return false;
            }
            resident[n]=0;++invalidations;
        }
        return true;
    }
};
