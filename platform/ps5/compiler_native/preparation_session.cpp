#include "preparation_session.h"
#include "initialization_trace.h"
#include "game_boot.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
namespace apricot::aot {
namespace {
struct Watch {
    const PreparedOverlay*original=nullptr;G core=0;
    std::vector<InitializationWrite>rows;std::vector<std::vector<uint8_t>>states;
    bool overflow=false;
};
struct Watches {
    std::vector<Watch>items;
    static void observe(void*context,G core,G address,G width,G writer,const uint8_t*old,G after){
        for(auto&w:static_cast<Watches*>(context)->items){auto&o=*w.original;
            if(address<o.address||uint64_t(address)-o.address+width>o.image.size()||(width!=1&&width!=2&&width!=4))continue;
            G before=0;for(G n=0;n<width;++n)before|=G(old[n])<<(n*8);if(width<4)after&=(1u<<(width*8))-1;
            if(before==after)continue;if(w.rows.size()>=1000000){w.overflow=true;continue;}
            w.rows.push_back({core,address,width,writer,before,after});
        }
    }
};
struct DiscoveryContext {PreparationCallback callback;void*context;};
bool discovery_progress(void*p,G core,G overlay,size_t){auto&c=*static_cast<DiscoveryContext*>(p);return !c.callback||c.callback(c.context,{"discover",core,overlay});}
}
bool prepare_native_module(const PreparedRom&rom,Result&out,PreparationReport&report,std::string&error,PreparationCallback callback,void*context){
    error.clear();report={};
    auto progress=[&](const char*phase,G core=0,G overlay=0xffffffffu,G state=0,uint64_t frame=0,size_t bytes=0){
        if(!callback||callback(context,{phase,core,overlay,state,frame,bytes}))return true;error="Preparation cancelled";return false;};
    DiscoveredRom discovered;DiscoveryContext dc{callback,context};
    if(!discover_rom(rom,discovered,error,discovery_progress,&dc))return false;
    std::vector<OverlayProgram>overlays;Watches watches;
    for(const auto*core:{&rom.arm9,&rom.arm7}){auto&selected=core->core==9?discovered.arm9:discovered.arm7;
        for(auto&item:selected.overlays){if(item.second.program.code.empty())continue;
            auto it=std::find_if(core->overlays.begin(),core->overlays.end(),[&](const auto&o){return o.id==item.first;});
            if(it==core->overlays.end()){error="Discovered overlay identity missing";return false;}
            const auto&o=*it;OverlayProgram translated;translated.core=core->core;translated.id=o.id;translated.base=o.address;translated.bytes=o.image.size();memcpy(translated.sha256,o.sha256,32);
            item.second.program.guarded=initialization_guard(rom.identity,core->core,o.id);
            if(item.second.program.guarded)watches.items.push_back({&o,core->core,{}, {},false});
            translated.states.push_back(std::move(item.second.program));overlays.push_back(std::move(translated));
        }
    }
    if(!progress("compile"))return false;Result candidate;
    if(!compile_game(discovered.arm9.main.program,discovered.arm7.main.program,rom.identity,overlays,candidate)){error=candidate.error;return false;}
    // No opcode decoder or compiler is reachable from a running trial. A stop
    // returns here, closes executable pages, then prepares another finite state.
    for(G trial=0;trial<33;++trial){
        ++report.trials;if(!progress("validate",0,0xffffffffu,trial,0,candidate.module.size()))return false;
        NativeGameModule module;if(!module.from_bytes(candidate.module.data(),candidate.module.size(),rom.identity)){error=module.error;return false;}
        auto machine=std::make_unique<NativeMachine>();
        if(!boot_native_game(*machine,module,rom.snapshot)){error=machine->error;return false;}
        for(auto&w:watches.items){w.rows.clear();w.overflow=false;}
        machine->bus.write_observer=Watches::observe;machine->bus.write_observer_context=&watches;
        machine->bus.rtc.snapshot=[](void*,uint8_t*v){const uint8_t fixed[7]={0x26,0x10,0x21,3,0x12,0,0};memcpy(v,fixed,7);return true;};
        bool stopped=false;uint64_t quanta=0,last=0;
        while(machine->bus.raster.frames<16000&&quanta++<100000000){auto frame=machine->bus.raster.frames;
            machine->bus.buttons=frame>=120&&frame%40<4?1:0;
            machine->bus.spi.pressed=frame>=2400&&frame%120<4;machine->bus.spi.touch_x=128;machine->bus.spi.touch_y=152;
            if(frame!=last&&frame%120==0){last=frame;if(!progress("validate",0,0xffffffffu,trial,frame,candidate.module.size()))return false;}
            if(!machine->step()){stopped=true;break;}
            for(auto&w:watches.items)if(w.overflow){error="Initialization trace exceeds preparation bound";return false;}
        }
        report.frames=machine->bus.raster.frames;
        if(!stopped){if(report.frames<16000){error="Initialization trial time bound exceeded";return false;}
            report.instructions=candidate.instructions;report.module_bytes=candidate.module.size();report.extra_states=trial;
            if(!progress("validated",0,0xffffffffu,trial,report.frames,candidate.module.size()))return false;
            out=std::move(candidate);return true;
        }
        Watch*watch=nullptr;unsigned cpu_index=0;
        for(unsigned n=0;n<2;++n)if(!machine->active[n]){cpu_index=n;break;}
        auto&cpu=machine->cpus[cpu_index];G pc=cpu.r[15];
        if(machine->stops[cpu_index]==G_UNMAPPED)for(auto&w:watches.items)if(w.core==cpu.core&&pc>=w.original->address&&uint64_t(pc)-w.original->address<w.original->image.size()){watch=&w;break;}
        if(!watch){error="Native initialization stopped outside a supported finite transformation: core="+std::to_string(cpu.core)+" pc="+std::to_string(pc)+" stop="+std::to_string(machine->stops[cpu_index]);return false;}
        auto&o=*watch->original;auto*bytes=machine->bus.map(cpu.core,o.address,o.image.size());
        if(!bytes){error="Initialization overlay memory unavailable";return false;}
        std::vector<uint8_t>image(bytes,bytes+o.image.size());
        if(!validate_initialization(o,image,watch->rows,error))return false;
        if(watch->states.size()>=32||std::find(watch->states.begin(),watch->states.end(),image)!=watch->states.end()){error="Repeated or excessive initialization state";return false;}
        auto seeds=profile_seeds(rom.identity,cpu.core,o.id),constructors=overlay_initializers(o);seeds.insert(seeds.end(),constructors.begin(),constructors.end());seeds.push_back({pc,bool(cpu.cpsr&32)});
        auto selected=discover_code({{o.address,&image,0,image.size()}},cpu.core,seeds);selected.program.guarded=true;
        if(selected.limit_reached||selected.program.code.empty()){error="Initialization-state discovery failed";return false;}
        // Executable pages are released before preparation changes any bytes.
        machine.reset();module.close();
        if(!progress("compile-state",watch->core,o.id,trial+1,report.frames,candidate.module.size()))return false;
        if(!append_guarded_state(candidate,watch->core,o.id,selected.program)){error=candidate.error;return false;}
        watch->states.push_back(std::move(image));
    }
    error="Finite initialization-state limit reached";return false;
}
}
