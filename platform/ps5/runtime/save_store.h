// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated native-player saves. No automatic access to another app's saves.
#pragma once
#include "save_flash.h"
#include "game_module.h"
#include "sha256.h"
#include <string>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
struct NativeSaveStore {
    std::string path;GameIdentity identity{};bool ready=false;
    const char *error="none";
    static void put(uint8_t *p,uint32_t value) {for(unsigned n=0;n<4;++n)p[n]=uint8_t(value>>(8*n));}
    static uint32_t get(const uint8_t *p) {uint32_t value=0;for(unsigned n=0;n<4;++n)value|=uint32_t(p[n])<<(8*n);return value;}
    bool open(const char *directory,const GameIdentity &id,HostSaveFlash &flash) {
        ready=false;error="save identity invalid";
        if(flash.selected||flash.dirty||flash.bytes.size()!=512*1024)return false;
        for(auto c:id.game_code)if(!(c>='A'&&c<='Z'||c>='0'&&c<='9'))return false;
        if(mkdir(directory,0777)&&errno!=EEXIST) {error="save directory unavailable";return false;}
        path=std::string(directory)+"/"+std::string(reinterpret_cast<const char*>(id.game_code),4)+"-";
        const char hex[]="0123456789abcdef";for(auto byte:id.sha256) {path+=hex[byte>>4];path+=hex[byte&15];}path+=".oqsave";identity=id;
        FILE *file=fopen(path.c_str(),"rb");
        if(!file) {if(errno==ENOENT) {ready=true;error="none";return true;}error="save unavailable";return false;}
        uint8_t header[128]{};std::vector<uint8_t> data(flash.bytes.size());
        bool ok=fread(header,1,128,file)==128&&fread(data.data(),1,data.size(),file)==data.size()&&fgetc(file)==EOF;
        ok=fclose(file)==0&&ok;
        if(!ok||memcmp(header,"OQSAVE01",8)||get(header+8)!=128||get(header+12)!=data.size()||memcmp(header+16,id.sha256,32)||
           memcmp(header+80,id.game_code,4)||memcmp(header+84,id.maker_code,2)||header[86]!=id.revision) {error="existing native save format or identity mismatch";return false;}
        for(unsigned n=87;n<128;++n)if(header[n]) {error="existing native save header invalid";return false;}
        uint8_t digest[32];oq_sha256(data.data(),data.size(),digest);
        if(memcmp(digest,header+48,32)) {error="existing native save checksum mismatch";return false;}
        flash.bytes=std::move(data);ready=true;error="none";return true;
    }
    bool flush(HostSaveFlash &flash) {
        if(!ready) {error="native save store not initialized";return false;}
        if(!flash.dirty)return true;
        uint8_t header[128]{};memcpy(header,"OQSAVE01",8);put(header+8,128);put(header+12,uint32_t(flash.bytes.size()));
        memcpy(header+16,identity.sha256,32);oq_sha256(flash.bytes.data(),flash.bytes.size(),header+48);
        memcpy(header+80,identity.game_code,4);memcpy(header+84,identity.maker_code,2);header[86]=identity.revision;
        auto temporary=path+".tmp";FILE *file=fopen(temporary.c_str(),"wb");if(!file) {error="native save temporary file unavailable";return false;}
        bool ok=fwrite(header,1,128,file)==128&&fwrite(flash.bytes.data(),1,flash.bytes.size(),file)==flash.bytes.size();
        ok=fflush(file)==0&&ok;ok=fsync(fileno(file))==0&&ok;ok=fclose(file)==0&&ok;
        if(!ok||rename(temporary.c_str(),path.c_str())) {error="native save commit failed";return false;}
        flash.dirty=false;error="none";return true;
    }
};
