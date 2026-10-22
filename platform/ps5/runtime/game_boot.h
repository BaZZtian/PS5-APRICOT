// SPDX-License-Identifier: GPL-3.0-or-later
// Boot state for the dedicated native player. No original BIOS/firmware required.
#pragma once
#include "machine.h"
#include "game_module.h"
static inline bool identify_owned_data(const std::vector<uint8_t> &rom,GameIdentity &identity) {
    if(rom.size()<1024*1024||rom.size()>0x20000000||(rom.size()&(rom.size()-1)))return false;
    std::copy_n(rom.data()+12,4,identity.game_code);std::copy_n(rom.data()+16,2,identity.maker_code);
    identity.revision=rom[0x1e];oq_sha256(rom.data(),rom.size(),identity.sha256);return true;
}
static inline bool boot_native_game(NativeMachine &machine,const NativeGameModule &module,std::vector<uint8_t> rom) {
    if(machine.initialized||!module.arm9||!module.arm7) {machine.error="native game module missing";return false;}
    GameIdentity identity{};
    if(!identify_owned_data(rom,identity)||memcmp(identity.game_code,module.identity.game_code,4)||
       memcmp(identity.maker_code,module.identity.maker_code,2)||identity.revision!=module.identity.revision||memcmp(identity.sha256,module.identity.sha256,32)) {
        machine.error="game data and native module differ";return false;
    }
    bool profile=identity.maker_code[0]=='0'&&identity.maker_code[1]=='1'&&identity.revision==0&&
        identity.game_code[0]=='I'&&identity.game_code[1]=='P'&&(identity.game_code[2]=='K'||identity.game_code[2]=='G')&&
        (identity.game_code[3]=='D'||identity.game_code[3]=='E');
    if(!profile) {machine.error="cartridge profile unsupported";return false;}
    auto read32=[&](unsigned at) {G value=0;for(unsigned n=0;n<4;++n)value|=G(rom[at+n])<<(n*8);return value;};
    auto &bus=machine.bus;uint8_t *destinations[2]{};G offsets[2]{},sizes[2]{};
    for(unsigned n=0;n<2;++n) {
        unsigned header=n?0x30:0x20;offsets[n]=read32(header);G entry=read32(header+4),address=read32(header+8);sizes[n]=read32(header+12);
        if(!sizes[n]||sizes[n]>4*1024*1024||offsets[n]>rom.size()||sizes[n]>rom.size()-offsets[n]||entry!=(n?module.entry7:module.entry9)||entry&3||
           !(destinations[n]=bus.map(n?7:9,address,sizes[n]))) {machine.error="native input processor layout invalid";return false;}
    }
    for(unsigned n=0;n<2;++n)std::copy_n(rom.data()+offsets[n],sizes[n],destinations[n]);
    bus.owner_rom=std::move(rom);std::copy_n(bus.owner_rom.data(),0x170,bus.ram.data()+0x3ffe00);
    bus.card.rom_size=G(bus.owner_rom.size());bus.card.chip_id=0xe0010080u|((bus.card.rom_size/(1024*1024)-1)<<8);
    bus.card.sector_only=true;bus.card.context=&bus;
    bus.card.read_rom=[](void *context,uint32_t address,uint8_t *output,unsigned count)->bool {
        auto &memory=static_cast<Bus*>(context)->owner_rom;if(address>memory.size()||count>memory.size()-address)return false;
        std::copy_n(memory.data()+address,count,output);return true;
    };
    for(G base:{0x027ff800u,0x027ffc00u}) {
        Bus::write(&bus,7,base,4,bus.card.chip_id);Bus::write(&bus,7,base+4,4,bus.card.chip_id);
        Bus::write(&bus,7,base+8,2,G(bus.owner_rom[0x15e])|G(bus.owner_rom[0x15f])<<8);
        Bus::write(&bus,7,base+10,2,G(bus.owner_rom[0x6c])|G(bus.owner_rom[0x6d])<<8);
    }
    Bus::write(&bus,7,0x027ffc40,2,1);bus.spi.set_language(identity.game_code[3]=='D'?3:1);
    std::copy_n(bus.spi.personal.data(),0x70,bus.ram.data()+0x3ffc80);
    return machine.configure(module.arm9,module.arm7,module.entry9,module.entry7,module.catalog,module.catalog_count);
}
