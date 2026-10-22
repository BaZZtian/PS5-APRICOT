// SPDX-License-Identifier: GPL-3.0-or-later
// Native PC preparation: bounded original initialization execution, no save access.
#include "game_boot.h"
#include <fstream>
#include <memory>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <sstream>
struct PreparationInput {uint64_t frame,duration;unsigned keys;int x,y;};
struct InitTrace {
    G begin=0,bytes=0;uint64_t rows=0;std::ofstream file;
    static void observe(void* ctx,G cpu,G address,G width,G writer,const uint8_t* old,G value){
        auto& t=*static_cast<InitTrace*>(ctx);
        if(address<t.begin||address-t.begin>=t.bytes||width>t.bytes-(address-t.begin)||!width||width>4)return;
        G before=0;for(unsigned n=0;n<width;++n)before|=G(old[n])<<(8*n);
        if(width<4)value&=(1u<<(width*8))-1;
        if(before==value)return;
        if(t.rows++>=1000000)return;
        t.file<<std::hex<<cpu<<','<<address<<','<<width<<','<<writer<<','<<before<<','<<value<<'\n';
    }
};
static bool number(const char* s,unsigned long long& result){
    if(!s||!*s||*s=='-')return false;errno=0;char* end=nullptr;
    result=strtoull(s,&end,0);return !errno&&end&&!*end;
}
int main(int argc,char** argv){
    if(argc!=7&&argc!=8){fprintf(stderr,"usage: module-prepare ROM MODULE PRIVATE-OUTPUT WATCH-BASE WATCH-BYTES FRAMES [title]\n");return 2;}
    const bool title=argc==8&& !std::strcmp(argv[7],"title");
    std::vector<PreparationInput> inputs;
    if(argc==8&&!title){std::ifstream controls(argv[7]);if(!controls)return 2;
        std::string line;while(std::getline(controls,line)){if(line.empty()||line[0]=='#')continue;
            if(line.size()>128||inputs.size()>=20000)return 2;
            PreparationInput input{};std::istringstream row(line);std::string extra;
            if(!(row>>input.frame>>input.duration>>input.keys>>input.x>>input.y)||(row>>extra)||
               input.frame>20000||!input.duration||input.duration>20000-input.frame||input.keys>4095||
               input.x< -1||input.x>255||input.y< -1||input.y>191||((input.x<0)!=(input.y<0)))return 2;
            inputs.push_back(input);}
        if(!controls.eof())return 2;
    }
    unsigned long long begin=0,bytes=0,frames=0;
    if(!number(argv[4],begin)||!number(argv[5],bytes)||!number(argv[6],frames)||begin<0x02000000||begin>=0x02400000||!bytes||bytes>0x02400000-begin||!frames||frames>20000)return 2;
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate);auto length=file.tellg();
    if(length<1024*1024||length>512*1024*1024)return 3;
    std::vector<uint8_t> rom(size_t(length),0);file.seekg(0);file.read(reinterpret_cast<char*>(rom.data()),rom.size());if(!file)return 3;
    auto machine=std::make_unique<NativeMachine>();GameIdentity id{};NativeGameModule module;
    if(!identify_owned_data(rom,id)||!module.load(argv[2],id)||!boot_native_game(*machine,module,std::move(rom)))return 4;
    std::ofstream catalog(std::string(argv[3])+"/native-catalog.json",std::ios::binary);
    auto digest=[&](const uint8_t* value){static const char h[]="0123456789abcdef";for(unsigned i=0;i<32;++i)catalog<<h[value[i]>>4]<<h[value[i]&15];};
    catalog<<"{\"rom_sha256\":\"";digest(id.sha256);catalog<<"\",\"entries\":[";
    for(unsigned i=0;i<module.catalog_count;++i){const auto& e=module.catalog[i];if(i)catalog<<',';
        catalog<<"{\"index\":"<<i<<",\"core\":"<<e.core<<",\"id\":"<<e.id<<",\"bytes\":"<<e.bytes<<",\"sha256\":\"";digest(e.sha256);catalog<<"\"}";}
    catalog<<"]}\n";catalog.close();if(!catalog)return 5;
    InitTrace trace;trace.begin=G(begin);trace.bytes=G(bytes);trace.file.open(std::string(argv[3])+"/private-write-trace.csv",std::ios::binary);
    if(!trace.file)return 5;
    machine->bus.write_observer=InitTrace::observe;machine->bus.write_observer_context=&trace;
    machine->bus.rtc.snapshot=[](void*,uint8_t* v){const uint8_t t[7]={0x26,0x10,0x21,3,0x12,0,0};std::memcpy(v,t,7);return true;};
    bool stopped=false;uint64_t quanta=0;
    while(machine->bus.raster.frames<frames&&quanta++<100000000){
        auto f=machine->bus.raster.frames;machine->bus.buttons=argc==7&&f>=120&&f%40<2?1:0;
        machine->bus.spi.pressed=false;
        for(const auto& input:inputs)if(f>=input.frame&&f-input.frame<input.duration){
            machine->bus.buttons|=input.keys;
            if(input.x>=0){machine->bus.spi.pressed=true;machine->bus.spi.touch_x=input.x;machine->bus.spi.touch_y=input.y;}
        }
        if(!machine->step()){stopped=true;break;}
        if(trace.rows>=1000000)return 6;
    }
    trace.file.flush();if(!trace.file)return 7;
    std::ofstream ram(std::string(argv[3])+"/after9-ram.bin",std::ios::binary);ram.write(reinterpret_cast<const char*>(machine->bus.ram.data()),machine->bus.ram.size());if(!ram)return 7;
    printf("PREP_NATIVE id=%.4s frames=%llu stopped=%u pc9=%08x stop9=%u pc7=%08x stop7=%u writes=%llu save_read=%llu save_write=%llu no_cpu_fallback=true\n",id.game_code,(unsigned long long)machine->bus.raster.frames,stopped,machine->cpus[0].r[15],machine->stops[0],machine->cpus[1].r[15],machine->stops[1],(unsigned long long)trace.rows,(unsigned long long)machine->bus.card.save.read_bytes,(unsigned long long)machine->bus.card.save.write_bytes);
    return stopped?10:machine->bus.raster.frames>=frames?0:11;
}
