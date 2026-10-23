#pragma once
#include "rom_prepare.h"
#include "sha256.h"
namespace apricot::aot {
struct InitializationWrite {G core,address,width,writer,before,after;};
// Validates the entire observed transformation against the ROM's original
// bytes. No executable or arbitrary memory dump is accepted as a substitute.
bool validate_initialization(const PreparedOverlay&,const std::vector<uint8_t>&final,
    const std::vector<InitializationWrite>&,std::string&error);
}
