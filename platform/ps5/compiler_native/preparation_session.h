#pragma once
#include "code_discovery.h"
namespace apricot::aot {
struct PreparationProgress {const char*phase;G core=0,overlay=0xffffffffu,state=0;uint64_t frame=0;size_t module_bytes=0;};
using PreparationCallback=bool(*)(void*,const PreparationProgress&);
struct PreparationReport {G trials=0,extra_states=0;uint64_t frames=0;size_t instructions=0,module_bytes=0;};
// Synchronous preparation worker; UI dispatches progress/cancellation through
// callback. Trials use fresh volatile flash only, never existing save files.
// Result is assigned only after a complete bounded native initialization run.
bool prepare_native_module(const PreparedRom&,Result&,PreparationReport&,std::string&error,
    PreparationCallback=nullptr,void*context=nullptr);
}
