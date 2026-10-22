// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest.h"
#include "overlay_types.h"
#include <stddef.h>
#ifdef OQ_PS5
#include <ps5platform/exec.h>
#endif
struct GameIdentity {uint8_t game_code[4],maker_code[2],revision,sha256[32];};
struct NativeGameModule {
    void *code=nullptr;size_t allocated=0,image_bytes=0;
    GuestEntry arm9=nullptr,arm7=nullptr;
    G entry9=0,entry7=0;
    GameIdentity identity{};
    const NativeOverlayInfo *catalog=nullptr;unsigned catalog_count=0;
    const char *error="none";
#ifdef OQ_PS5
    ps5_exec_region region{};
#endif
    bool load(const char *path,const GameIdentity &expected);
    bool from_bytes(const uint8_t *data,size_t size,const GameIdentity &expected);
    void close();
    ~NativeGameModule(){close();}
    NativeGameModule()=default;
    NativeGameModule(const NativeGameModule&)=delete;
    NativeGameModule& operator=(const NativeGameModule&)=delete;
};
