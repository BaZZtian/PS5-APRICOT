// Experimental console-local AOT compiler. Not a complete game preparer.
#pragma once
#include "game_module.h"
#include <string>
#include <vector>

namespace apricot::aot {
struct Instruction { G pc, word; bool thumb; };
struct Program { G core=0, entry=0; std::vector<Instruction> code; bool guarded=false; };
struct OverlayProgram {
    G core=0,id=0,base=0,bytes=0;
    uint8_t sha256[32]{};
    std::vector<Program> states;
};
struct ExtensionSite {
    G core=0,id=0,base=0,bytes=0,used=0;
    uint64_t slots=0,catalog_record=0;
    std::vector<NativeCodeRange> ranges;
};
struct Result {
    std::vector<uint8_t> module;
    std::string error;
    G fault_pc=0, fault_word=0;
    size_t instructions=0;
    std::vector<ExtensionSite> extensions;
};
// All instruction selection/decoding and x86 emission happen here, before the
// resulting immutable module is passed to the existing executable loader.
// No decoding service or source instruction stream is embedded in that module.
bool compile(const Program &arm9,const Program &arm7,const GameIdentity &,Result &);
bool compile_game(const Program &arm9,const Program &arm7,const GameIdentity &,const std::vector<OverlayProgram>&,Result &);
// Preparation-only operation. Appends a fully compiled known state; failure
// preserves the previous valid image and never installs executable pages.
bool append_guarded_state(Result&,G core,G overlay,const Program&);
// Preparation-time capability check using the same emitter as final modules.
bool supports(const Instruction &,G core,std::string &error);
}
