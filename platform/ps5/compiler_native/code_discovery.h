#pragma once
#include "aot_backend.h"
#include "rom_prepare.h"
#include <utility>
namespace apricot::aot {
struct CodeView {G base=0;const std::vector<uint8_t>*image=nullptr;size_t offset=0,bytes=0;};
using CodeSeed=std::pair<G,bool>;
struct CodeDiscovery {
    Program program;
    size_t pointer_candidates=0,prologue_candidates=0,unsupported=0,external=0,indirect=0;
    bool limit_reached=false;
    std::vector<CodeSeed> external_targets;
    std::vector<std::pair<G,G>> jump_tables; // dispatcher address, verified cases
    std::vector<G> unresolved_switches;
};
struct DiscoveredCore {CodeDiscovery main;std::vector<std::pair<G,CodeDiscovery>> overlays;};
struct DiscoveredRom {DiscoveredCore arm9,arm7;};
using DiscoveryProgress=bool(*)(void*,G core,G overlay,size_t instructions);
bool discover_rom(const PreparedRom&,DiscoveredRom&,std::string&error,DiscoveryProgress progress=nullptr,void*context=nullptr);
std::vector<CodeView> core_views(const PreparedCore&);
CodeDiscovery discover_code(const std::vector<CodeView>&,G core,const std::vector<CodeSeed>&seeds,size_t limit=1000000);
std::vector<CodeSeed> profile_seeds(const GameIdentity&,G core,G overlay=0xffffffffu);
bool initialization_guard(const GameIdentity&,G core,G overlay);
std::vector<CodeSeed> overlay_initializers(const PreparedOverlay&);
std::vector<CodeSeed> external_literals(const PreparedOverlay&,const PreparedCore&);
}
