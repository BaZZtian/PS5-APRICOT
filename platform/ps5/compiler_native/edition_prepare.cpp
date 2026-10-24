#include "edition_prepare.h"
#include "game_boot.h"
#include "sha256_stream.h"
#include "preparation_staging.h"
#include "preparation_checkpoint.h"
#include <memory>
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
namespace apricot::aot {
namespace {
void put(std::vector<uint8_t>&v,size_t at,uint64_t n,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)v[at+i]=uint8_t(n>>(8*i));}
void hash(const std::vector<uint8_t>&v,uint8_t*d){OqSha256Stream h;h.update(v.data(),v.size());h.digest(d);}
struct Capture {NativeMachine*machine;std::vector<int16_t>samples;bool overflow=false;
 static void push(void*p,int16_t l,int16_t r){auto&c=*static_cast<Capture*>(p);if(c.machine->bus.raster.frames<4200)return;
  if(c.samples.size()>=32768u*70*2){c.overflow=true;return;}c.samples.push_back(l);c.samples.push_back(r);}
};
bool write_verified(const std::string&path,const std::vector<uint8_t>&v){
 FILE*f=fopen(path.c_str(),"wbx");if(!f)return false;bool ok=fwrite(v.data(),1,v.size(),f)==v.size();ok=!fclose(f)&&ok;if(!ok)return false;
 f=fopen(path.c_str(),"rb");if(!f)return false;uint8_t block[65536],expected[32],actual[32];OqSha256Stream h;size_t total=0,n;
 while((n=fread(block,1,sizeof(block),f))){h.update(block,n);total+=n;}ok=!ferror(f);ok=!fclose(f)&&ok;h.digest(actual);hash(v,expected);return ok&&total==v.size()&&!memcmp(expected,actual,32);
}
}
bool prepare_edition(const char*input,const char*destination,NativeGameVideo&video,PreparationCallback callback,void*context,std::string&error){
 auto update=[&](const char*phase,uint64_t frame=0){if(!callback||callback(context,{phase,0,0xffffffffu,0,frame,0}))return true;error="Preparation cancelled";return false;};
 if(!input||!destination){error="Missing preparation path";return false;}
 struct stat existing{};if(!lstat(destination,&existing)||errno!=ENOENT){error="Existing edition preserved";return false;}
 if(!update("VERIFY ROM"))return false;PreparedRom rom;if(!prepare_rom_file(input,rom,error))return false;
 Result result;PreparationReport report;const std::string checkpoint=std::string(destination)+".checkpoint";bool resumed=false;
 if(update("CHECK FOR SAFE RESUME")&&load_preparation_checkpoint(checkpoint,rom.identity,result.module)){
  NativeGameModule checked;if(checked.from_bytes(result.module.data(),result.module.size(),rom.identity)){resumed=true;checked.close();}
  else result.module.clear();
 }
 if(!resumed&&!prepare_native_module(rom,result,report,error,callback,context))return false;
 if(!resumed&&!save_preparation_checkpoint(checkpoint,rom.identity,result.module)){
  error="Preparation checkpoint could not be saved";return false;
 }
 if(!finish_prepared_edition(rom,std::move(result),destination,video,callback,context,error))return false;
 remove_preparation_checkpoint(checkpoint);return true;
}
bool finish_prepared_edition(const PreparedRom&rom,Result&&result,const char*destination,NativeGameVideo&video,PreparationCallback callback,void*context,std::string&error){
 auto update=[&](const char*phase,uint64_t frame=0){if(!callback||callback(context,{phase,0,0xffffffffu,0,frame,0}))return true;error="Preparation cancelled";return false;};
 if(!destination){error="Missing edition destination";return false;}
 struct stat existing{};if(!lstat(destination,&existing)||errno!=ENOENT){error="Existing edition preserved";return false;}
 if(!update("TITLE IMAGE AND MUSIC"))return false;
 NativeGameModule module;if(!module.from_bytes(result.module.data(),result.module.size(),rom.identity)){error=module.error;return false;}
 std::vector<uint8_t>refs(64+module.catalog_count*24,0);memcpy(refs.data(),"APRF0001",8);put(refs,8,module.catalog_count);memcpy(refs.data()+16,rom.identity.sha256,32);
 for(unsigned i=0;i<module.catalog_count;++i){const auto&item=module.catalog[i];const auto&core=item.core==9?rom.arm9:rom.arm7;
  auto o=std::find_if(core.overlays.begin(),core.overlays.end(),[&](const auto&v){return v.id==item.id;});
  if(o==core.overlays.end()||memcmp(o->sha256,item.sha256,32)){error="Native overlay identity differs";return false;}
  size_t at=64+i*24;put(refs,at,i);put(refs,at+8,refs.size(),8);put(refs,at+16,o->image.size(),8);refs.insert(refs.end(),o->image.begin(),o->image.end());
 }
 auto machine=std::make_unique<NativeMachine>();if(!boot_native_game(*machine,module,rom.snapshot)){error=machine->error;return false;}
 machine->bus.rtc.snapshot=[](void*,uint8_t*v){const uint8_t t[7]={0x26,0x10,0x21,3,0x12,0,0};memcpy(v,t,7);return true;};
 Capture capture{machine.get(),{}};capture.samples.reserve(32768u*65*2);machine->bus.sound.sink=Capture::push;machine->bus.sound.sink_context=&capture;
 std::vector<uint8_t>label;uint64_t last=0,quanta=0;
 while(machine->bus.raster.frames<7800&&quanta++<100000000){
  if(!machine->step()){error="Native title preparation stopped";return false;}
  auto f=machine->bus.raster.frames;if(f==last)continue;last=f;
  if(f%120==0&&!update("TITLE IMAGE AND MUSIC",f))return false;
  // Both exact German profiles: 4200 is the logo-reveal flash; 4500 is
  // the complete title logo, verified with native frame captures for each.
  if(f==4500){if(!video.render(machine->bus)){error=video.error;return false;}
   label.resize(256*192*4);for(unsigned n=0;n<256*192;++n)put(label,n*4,video.pixels[256*192+n]|0xff000000u);
  }
 }
 if(last<7800||label.empty()||capture.overflow||capture.samples.size()<4){error="Incomplete title media";return false;}
 machine.reset();module.close();
 size_t source=capture.samples.size()/2;uint64_t count=uint64_t(source)*48000*1024/33513982;
 if(!count||count>48000*180){error="Title music outside bounds";return false;}
 std::vector<uint8_t>audio(64+size_t(count)*4,0);memcpy(audio.data(),"APAU0001",8);put(audio,8,48000);audio[12]=2;audio[14]=16;put(audio,16,count);put(audio,24,count);memcpy(audio.data()+32,rom.identity.sha256,32);
 for(uint64_t n=0;n<count;++n){uint64_t position=n*33513982,den=48000*1024,at=position/den,frac=position%den;at=std::min(uint64_t(source-1),at);auto next=std::min(uint64_t(source-1),at+1);
  for(unsigned c=0;c<2;++c){int64_t sample=(int64_t(capture.samples[at*2+c])*int64_t(den-frac)+int64_t(capture.samples[next*2+c])*int64_t(frac))/int64_t(den);put(audio,64+n*4+c*2,uint16_t(int16_t(sample)),2);}
 }
 capture.samples.clear();capture.samples.shrink_to_fit();
 std::vector<uint8_t>manifest(256,0);memcpy(manifest.data(),"APED0001",8);put(manifest,8,256);put(manifest,12,rom.snapshot.size());memcpy(manifest.data()+16,rom.identity.game_code,4);memcpy(manifest.data()+20,rom.identity.maker_code,2);manifest[22]=rom.identity.revision;memcpy(manifest.data()+24,rom.identity.sha256,32);
 hash(result.module,manifest.data()+56);hash(label,manifest.data()+88);hash(audio,manifest.data()+120);hash(refs,manifest.data()+152);put(manifest,184,1);
 if(!update("VERIFY AND COMMIT"))return false;
 std::string staging;if(!create_preparation_staging(destination,staging)){error="Preparation staging unavailable";return false;}
 const char*names[]={"game-data.bin","game-203.oqgame","title-label.rgba","title-menu.pcm","identity-refs.bin","edition.bin"};
 const std::vector<uint8_t>*data[]={&rom.snapshot,&result.module,&label,&audio,&refs,&manifest};
 bool ok=true;for(unsigned n=0;n<6&&ok;++n)ok=update("VERIFY AND COMMIT")&&write_verified(staging+"/"+names[n],*data[n]);
 if(ok)ok=update("READY",16000);
 if(ok){if(!lstat(destination,&existing)||errno!=ENOENT)ok=false;else ok=!rename(staging.c_str(),destination);}
 if(!ok){for(const char*name:names)unlink((staging+"/"+name).c_str());rmdir(staging.c_str());if(error.empty())error="Edition write/readback/commit failed";return false;}
 return true;
}
}
