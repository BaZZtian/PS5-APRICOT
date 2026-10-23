// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <cerrno>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
namespace apricot {
// No arbitrary strings, paths, memory, save contents or automatic network upload.
struct SupportReport {
    enum class Phase:unsigned {Startup,Selection,Gameplay,Stopped,Exit};
    Phase phase=Phase::Startup;
    char game_code[5]{};
    uint64_t frames=0;
    unsigned stop9=0,stop7=0;
    bool set_game(const uint8_t* code){
        if(!code)return false;
        if(std::memcmp(code,"IPGD",4)&&std::memcmp(code,"IPKD",4)&&
           std::memcmp(code,"IPGE",4)&&std::memcmp(code,"IPKE",4))return false;
        std::memcpy(game_code,code,4);game_code[4]=0;return true;
    }
    size_t render(char* out,size_t capacity)const{
        const char* phases[]={"startup","selection","gameplay","stopped","exit"};
        if(!out||!capacity||unsigned(phase)>=5||stop9>6||stop7>6)return 0;
        if(game_code[4]|| (game_code[0]&&std::memcmp(game_code,"IPGD",4)&&
           std::memcmp(game_code,"IPKD",4)&&std::memcmp(game_code,"IPGE",4)&&std::memcmp(game_code,"IPKE",4)))return 0;
        const int n=std::snprintf(out,capacity,
            "{\n  \"schema_version\": 1,\n  \"app\": \"ApricotGS\",\n  \"version\": \"0.2.23\",\n"
            "  \"game_code\": \"%.4s\",\n  \"phase\": \"%s\",\n  \"frames\": %llu,\n"
            "  \"stop_arm9\": %u,\n  \"stop_arm7\": %u,\n  \"automatic_upload\": false\n}\n",
            game_code,phases[unsigned(phase)],static_cast<unsigned long long>(frames),stop9,stop7);
        return n>0&&size_t(n)<capacity?size_t(n):0;
    }
    bool write_atomic(const char* path)const{
        char text[1024];const size_t size=render(text,sizeof(text));
        if(!path||!size)return false;
        std::string temporary=std::string(path)+".tmpXXXXXX";
        int fd=mkstemp(temporary.data());if(fd<0)return false;
        bool ok=fchmod(fd,0644)==0;size_t done=0;
        while(ok&&done<size){const ssize_t n=write(fd,text+done,size-done);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)ok=false;else done+=size_t(n);
        }
        ok=fsync(fd)==0&&ok;ok=close(fd)==0&&ok;
        if(ok)ok=std::rename(temporary.c_str(),path)==0;
        if(!ok)unlink(temporary.c_str());
        return ok;
    }
};
}
