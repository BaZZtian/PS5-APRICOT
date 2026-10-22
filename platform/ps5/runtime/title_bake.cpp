// SPDX-License-Identifier: GPL-3.0-or-later
// Private native title-scene capture. No saves are opened or written.
#include "game_boot.h"
#include "game_video.h"
#include "game_shaders.h"
#include "identity_reference.h"
#include <fstream>
#include <memory>
#include <dlfcn.h>
struct AudioCapture{NativeMachine*machine;uint64_t start;std::vector<int16_t> samples;
 static void push(void*ctx,int16_t l,int16_t r){auto&c=*static_cast<AudioCapture*>(ctx);if(c.machine->bus.raster.frames>=c.start){c.samples.push_back(l);c.samples.push_back(r);}}
};
std::vector<uint8_t> read(const char*path){std::ifstream f(path,std::ios::binary);return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});}
int main(int argc,char**argv){
 if(argc!=5){fprintf(stderr,"usage: title-bake ROM MODULE REFS OUTPUT-DIR\n");return 2;}
 auto rom=read(argv[1]);GameIdentity id{};if(!identify_owned_data(rom,id))return 3;
 auto machine=std::make_unique<NativeMachine>();NativeGameModule module;if(!module.load(argv[2],id)||!boot_native_game(*machine,module,std::move(rom)))return 4;
 if(!load_identity_references(read(argv[3]),id,machine->bus.overlays))return 5;
 machine->bus.rtc.snapshot=[](void*,uint8_t*v){const uint8_t t[7]={0x26,0x10,0x21,3,0x12,0,0};memcpy(v,t,7);return true;};
 AudioCapture capture{machine.get(),4200,{}};capture.samples.reserve(32729*60*2);
 machine->bus.sound.sink=AudioCapture::push;machine->bus.sound.sink_context=&capture;
 void* lib=dlopen("libvulkan.so.1",RTLD_NOW);if(!lib){fprintf(stderr,"no host Vulkan: %s\n",dlerror());return 6;}
 auto loader=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib,"vkGetInstanceProcAddr"));NativeGameVideo video;
 if(!video.initialize(loader,oq_tiles,sizeof(oq_tiles),oq_geometry,sizeof(oq_geometry),oq_geometry_resolve,sizeof(oq_geometry_resolve))){fprintf(stderr,"%s\n",video.error.c_str());return 7;}
 uint64_t last=0;bool image=false;
 while(machine->bus.raster.frames<7800){
  if(!machine->step()){fprintf(stderr,"native stop %s frame=%llu pc9=%08x cpsr9=%08x stop9=%u pc7=%08x cpsr7=%08x stop7=%u\n",machine->error,(unsigned long long)machine->bus.raster.frames,machine->cpus[0].r[15],machine->cpus[0].cpsr,machine->stops[0],machine->cpus[1].r[15],machine->cpus[1].cpsr,machine->stops[1]);return 8;}
  if(machine->bus.raster.frames==last)continue;last=machine->bus.raster.frames;
  if(last==4200){
   if(!video.render(machine->bus)){fprintf(stderr,"%s\n",video.error.c_str());return 9;}
   for(unsigned screen=0;screen<2;++screen){std::ofstream f(std::string(argv[4])+"/title-"+std::to_string(screen)+".ppm",std::ios::binary);f<<"P6\n256 192\n255\n";for(unsigned n=0;n<256*192;++n){uint32_t p=video.pixels[screen*256*192+n];char rgb[3]={char(p),char(p>>8),char(p>>16)};f.write(rgb,3);}if(!f)return 10;}
   image=true;
  }
 }
 std::ofstream out(std::string(argv[4])+"/title-source.s16le",std::ios::binary);
 for(auto sample:capture.samples){char b[2]={char(sample),char(uint16_t(sample)>>8)};out.write(b,2);}if(!out||!image)return 11;
 video.geometry.close();video.tiles.close();dlclose(lib);
 printf("TITLE_CAPTURE id=%.4s frame=4200 end=7800 source_stereo_frames=%zu clock=33513982/1024 no_save_access=true\n",id.game_code,capture.samples.size()/2);fflush(stdout);
}
