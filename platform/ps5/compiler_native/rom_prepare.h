// Native, bounded preparation of an immutable user-owned NDS snapshot.
#pragma once
#include "game_module.h"
#include <string>
#include <vector>
namespace apricot::aot {
struct RomProfile {GameIdentity identity{};uint32_t bytes=0;};
struct NativeSection {G address=0,offset=0,bytes=0,bss=0;bool implicit=false;};
struct PreparedOverlay {
    G id=0,address=0,bss=0,init_begin=0,init_end=0,file_id=0,flags=0;
    uint8_t sha256[32]{};
    std::vector<uint8_t> image;
};
struct PreparedCore {
    G core=0,base=0,entry=0,settings=0,bss_begin=0,bss_end=0;
    std::vector<uint8_t> image;
    std::vector<NativeSection> sections;
    std::vector<PreparedOverlay> overlays;
};
struct PreparedRom {
    GameIdentity identity{};
    std::vector<uint8_t> snapshot;
    PreparedCore arm9,arm7;
};
bool known_profile(const uint8_t code[4],RomProfile &);
bool unpack_blz(const std::vector<uint8_t>&stored,bool required,std::vector<uint8_t>&out,std::string&error,size_t limit=16u*1024*1024);
bool prepare_snapshot(std::vector<uint8_t> snapshot,const RomProfile &,PreparedRom &,std::string &error);
// Opens input read-only. No output/module/save is written by these functions.
bool prepare_rom_file(const char *path,PreparedRom &,std::string &error);
}
