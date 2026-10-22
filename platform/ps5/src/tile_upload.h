/* Copyright (C) 2026 Mihawk-99 */
/* ApricotGS adaptation of display.cpp's sequential tiled upload. */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
namespace Video {
// Destination is a 16-byte aligned, 64 KiB tile in direct display memory.
// Caller fences all streaming stores before cursor stores and presentation.
inline void uploadTile(void* destination,const uint8_t* source,unsigned pitch,
                       const uint16_t* source_x,const uint16_t* source_y,
                       const uint16_t* order,unsigned bx,unsigned by,
                       unsigned output_height,bool bgra,const void* background=nullptr){
    auto* tile=static_cast<__m128i*>(destination);
    const __m128i keep=_mm_set1_epi32(int(0xff00ff00u));
    const __m128i low=_mm_set1_epi32(255);
    for(unsigned k=0;k<16384;k+=4){
        uint32_t p[4]{};
        for(unsigned n=0;n<4;++n){unsigned at=order[k+n],x=bx*128+(at&127),y=by*128+(at>>7);
            if(y<output_height){
                std::memcpy(&p[n],source+size_t(source_y[y])*pitch+size_t(source_x[x])*4,4);
                if(background&&(p[n]>>24)==0){std::memcpy(&p[n],static_cast<const uint8_t*>(background)+(size_t(y)*3840+x)*4,4);
                    if(bgra)p[n]=(p[n]&0xff00ff00u)|((p[n]&255u)<<16)|((p[n]>>16)&255u);
                }
            }
        }
        __m128i value=_mm_set_epi32(int(p[3]),int(p[2]),int(p[1]),int(p[0]));
        if(!bgra)value=_mm_or_si128(_mm_and_si128(value,keep),_mm_or_si128(_mm_slli_epi32(_mm_and_si128(value,low),16),_mm_and_si128(_mm_srli_epi32(value,16),low)));
        _mm_stream_si128(tile+k/4,value);
    }
}
}
