// SPDX-License-Identifier: GPL-3.0-or-later
// Unreleased native player. All game code/assets are loaded from private user data.
#include "game_boot.h"
#include "game_video.h"
#include "audio_bridge.h"
#include "save_store.h"
#include "playstation_input.h"
#include "../src/display.h"
#include "menu.h"
#include "controller_legend.h"
#include "apricot/support_report.hpp"
#include "game_shaders.h"
#include "presentation.h"
#include "identity_reference.h"
#include "prepared_menu_audio.h"
#include "apricot/cartridge_view.hpp"
#include "apricot/edition_bundle.hpp"
#include "apricot/launcher_glass.hpp"
#include "status_screen.h"
#include "message_box.h"
#include "preparation_timer.h"
#include "app_version.h"
#include <ps5platform/libc.h>
#include <time.h>
#include <unistd.h>
#include "../compiler_native/edition_prepare.h"
#include "../compiler_native/rom_filename.h"
#include <sys/stat.h>
#include <dirent.h>
#include <thread>
#include <atomic>
#include <mutex>
#define APP_DATA "/data/orqenva"
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance,const char*);
namespace {
apricot::SupportReport support_report;
void write_support_report(){if(!support_report.write_atomic(APP_DATA "/debug-report.json"))say("support_report_write_failed=1");}
bool read_data(const char *path,std::vector<uint8_t> &out,size_t maximum) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    bool ok=fseek(f,0,SEEK_END)==0;long size=ok?ftell(f):-1;
    if(size<0||uint64_t(size)>maximum||fseek(f,0,SEEK_SET)) {fclose(f);return false;}
    out.assign(size_t(size),0);ok=fread(out.data(),1,out.size(),f)==out.size();return fclose(f)==0&&ok;
}
void screen_copy(std::vector<uint32_t> &canvas,const std::vector<uint32_t> &pixels,unsigned screen,unsigned left,unsigned top,unsigned width,unsigned height) {
    static std::vector<uint32_t> previous[2],scaled[2];static unsigned widths[2]{},heights[2]{};
    const auto* source=pixels.data()+screen*256*192;
    if(widths[screen]!=width||heights[screen]!=height||previous[screen].size()!=256*192||memcmp(previous[screen].data(),source,256*192*4)) {
        scaled[screen]=apricot::scale_screen_nearest(source,width,height);previous[screen].assign(source,source+256*192);
        widths[screen]=width;heights[screen]=height;
    }
    for(unsigned y=0;y<height;++y)std::copy_n(scaled[screen].data()+y*width,width,canvas.data()+(top+y)*1280+left);
}
void app_heading(std::vector<uint32_t>&canvas,bool bgra=false){
    static std::vector<uint8_t>logo;static bool loaded=false;if(!loaded){loaded=true;read_data("/app0/userdata/header-logo.bgra",logo,80*80*4);}
    apricot::status_logo(canvas,logo,12,2,80,bgra,80);
    menuProportionalText(canvas,108,0,"APRICOTGS",0xffe5eeed,7,1280);
    menuProportionalText(canvas,110,48,"v" APRICOT_VERSION,0xffe5eeed,4,1280);
}
void information(std::vector<uint32_t> &canvas,const char *message,bool progress=false) {
    static std::vector<uint32_t> base,composed;static std::vector<uint8_t> background;static std::string previous;static bool previous_progress=false;
    if(base.empty()){if(read_data("/app0/userdata/launcher-uhd.rgba",background,3840u*2160*4)&&background.size()==3840u*2160*4)base=apricot::status_background(background,3840,2160,false);else {background.clear();base=apricot::status_background({},0,0,false);}}
    if(composed.empty()||previous!=message||previous_progress!=progress){
        previous=message;previous_progress=progress;composed=base;
        if(progress){apricot::launcher_glass(composed,background,3840,2160,20,180,1260,590,false);menuProportionalText(composed,48,210,message,0xff382c11,3);}
        else {auto box=apricot::message_box(message);apricot::launcher_glass(composed,background,3840,2160,box.left,box.top,box.left+box.width,box.top+box.height,false);
            int y=box.top+(box.height-int(box.lines.size())*36)/2;
            for(auto&line:box.lines){menuProportionalText(composed,640-menuTextWidth(line)/2,y,line,background.empty()?0xffe5eeed:0xff382c11,3);y+=36;}
        }
    }
    canvas=composed;app_heading(canvas);
    apricot::controller_icon(canvas,1280,720,32,626,apricot::ControlIcon::Options);
    menuText(canvas,84,640,"BEENDEN",0xffc0d6d0,2,1280);
}
int stopped(Ps5GpuDisplay &display,std::vector<uint32_t> &canvas,const char *message) {
    support_report.phase=apricot::SupportReport::Phase::Stopped;write_support_report();
    say("native_player_stop: %s",message);information(canvas,message);
    for(;;) {pad p{};pad_poll(&p);if(p.pressed&PAD_OPTIONS)break;if(display.present(canvas.data(),1280,720,1280*4,stderr,nullptr))break;usleep(16000);}
    return 1;
}
}
int main() {
    platform_init("ApricotGS");write_support_report();pad_open();Ps5GpuDisplay display;std::vector<uint32_t> canvas(1280*720);
    bool control_enabled=false;
    say("ui_font=%s baseline=%u",apricotFontName,apricotFontBaseline);
    information(canvas,"APRICOTGS v" APRICOT_VERSION " - WIRD GELADEN");
    display.present(canvas.data(),1280,720,1280*4,stderr,nullptr);
    struct Progress {
        Ps5GpuDisplay*display;std::vector<uint32_t>*canvas;
        std::mutex lock;apricot::aot::PreparationProgress snapshot{"VORBEREITUNG"};std::atomic<bool>cancelled{false};apricot::PreparationTimer timer;
        static bool publish(void*context,const apricot::aot::PreparationProgress&p){auto&s=*static_cast<Progress*>(context);{std::lock_guard<std::mutex> guard(s.lock);s.snapshot=p;}return !s.cancelled.load();}
        static bool update(void*context,const apricot::aot::PreparationProgress&p){
            auto&s=*static_cast<Progress*>(context);const char*message=p.phase;
            if(!strcmp(message,"discover"))message="SPIELDATEN WERDEN ANALYSIERT";
            else if(!strcmp(message,"compile")||!strcmp(message,"compile-state"))message="NATIVES SPIEL WIRD ERSTELLT";
            else if(!strcmp(message,"validate")||!strcmp(message,"validated"))message="SPIELSTART WIRD GEPRUEFT";
            else if(!strcmp(message,"VERIFY ROM"))message="EIGENE ROM WIRD GEPRUEFT";
            else if(!strcmp(message,"TITLE IMAGE AND MUSIC"))message="TITELBILD UND MUSIK WERDEN ERSTELLT";
            else if(!strcmp(message,"VERIFY AND COMMIT"))message="VORBEREITUNG WIRD GESICHERT";
            else if(!strcmp(message,"READY"))message="SPIEL IST BEREIT";
            information(*s.canvas,message,true);
            char line[96];snprintf(line,sizeof(line),"v%s - STATE %u - FRAME %llu",APRICOT_VERSION,p.state,(unsigned long long)p.frame);
            menuText(*s.canvas,32,280,line,0xff382c11,2,1280);
            menuText(*s.canvas,32,450,"EINMALIGE EINRICHTUNG - BITTE WARTEN",0xff382c11,2,1280);
            auto timing=s.timer.update(now_seconds(),p.phase,p.state,p.frame);char time_text[112];
            if(timing.ready)snprintf(time_text,sizeof(time_text),"FERTIG - DAUER %u S",timing.elapsed);
            else if(timing.estimated)snprintf(time_text,sizeof(time_text),"VERGANGEN %u S - RESTZEIT EDITION CA. %u S",timing.elapsed,timing.remaining);
            else snprintf(time_text,sizeof(time_text),"VERGANGEN %u S - RESTZEIT WIRD BERECHNET",timing.elapsed);
            menuText(*s.canvas,32,490,time_text,0xff382c11,2,1280);
            for(unsigned y=340;y<356;++y)std::fill_n(s.canvas->data()+y*1280+32,1216u*std::min(uint64_t(16000),p.frame)/16000,0xff65b8ec);
            unsigned marker=unsigned(std::fmod(now_seconds()*180,1120.0));
            for(unsigned y=370;y<376;++y)std::fill_n(s.canvas->data()+y*1280+32+marker,96,0xff65b8ec);
            pad input{};pad_poll(&input);return !(input.pressed&PAD_OPTIONS)&&!s.display->present(s.canvas->data(),1280,720,1280*4,stderr,nullptr);
        }
    }progress{&display,&canvas};
    auto directory=[](const char*path){struct stat s{};if(!mkdir(path,0700))return true;return errno==EEXIST&&!lstat(path,&s)&&S_ISDIR(s.st_mode);};
    if(!directory(APP_DATA)||!directory(APP_DATA "/games")||!directory(APP_DATA "/roms")||!directory(APP_DATA "/saves"))return stopped(display,canvas,"DATENORDNER NICHT VERFUEGBAR");
    if(!mkdir("/data/apricotgs-roms",0777)){
        if(chmod("/data/apricotgs-roms",0777))return stopped(display,canvas,"ROM EINGABEORDNER NICHT VERFUEGBAR");
    }else {struct stat input_dir{};if(errno!=EEXIST||lstat("/data/apricotgs-roms",&input_dir)||!S_ISDIR(input_dir.st_mode))return stopped(display,canvas,"ROM EINGABEORDNER NICHT VERFUEGBAR");}
    std::vector<std::string> inputs;
    // Keep user ROM inputs in a persistent transfer-visible inbox; generated
    // editions and identity-based saves remain in the production data namespace.
    for(const char*folder:{"/data/apricotgs-roms",APP_DATA "/roms","/app0/roms"}) {
        if(DIR*dir=opendir(folder)){while(auto*entry=readdir(dir)){std::string name=entry->d_name;if(apricot::aot::is_rom_filename(name))inputs.push_back(std::string(folder)+"/"+name);}closedir(dir);}
    }
    // Existing owner dumps are read-only migration INPUTS, never save/module sources.
    // Merge per-edition legacy inputs even when only the other edition is
    // supplied in the inbox; otherwise adding SoulSilver would hide HeartGold.
    for(const char*path:{APP_DATA "/games/heartgold/game-data.bin",APP_DATA "/games/soulsilver/game-data.bin","/data/orqenva/games/heartgold/game-data.bin","/data/orqenva/games/soulsilver/game-data.bin"})if(!access(path,R_OK))inputs.push_back(path);
    for(const auto&input:inputs){
        FILE*f=fopen(input.c_str(),"rb");uint8_t header[32]{};bool header_ok=f&&fread(header,1,32,f)==32;if(f)fclose(f);
        if(!header_ok||(memcmp(header+12,"IPKD",4)&&memcmp(header+12,"IPGD",4)))return stopped(display,canvas,"ROM IDENTITAET NICHT UNTERSTUETZT");
        // New HG profile generation preserves the previous edition and saves.
        // Existing r1 modules cannot acquire newly compiled targets by an app update.
        std::string destination=std::string(APP_DATA "/games/")+(header[14]=='K'?"heartgold-r7":"soulsilver-r5");
        if(!access((destination+"/edition.bin").c_str(),F_OK))continue;
        NativeGameVideo capture;auto loader=reinterpret_cast<PFN_vkGetInstanceProcAddr>(vk_icdGetInstanceProcAddr);
        if(!capture.initialize(loader,oq_tiles,sizeof(oq_tiles),oq_geometry,sizeof(oq_geometry),oq_geometry_resolve,sizeof(oq_geometry_resolve)))return stopped(display,canvas,capture.error.c_str());
        std::string error;bool ready=false;std::atomic<bool>done{false};progress.timer.reset(now_seconds());
        std::thread worker([&]{ready=apricot::aot::prepare_edition(input.c_str(),destination.c_str(),capture,Progress::publish,&progress,error);done.store(true);});
        while(!done.load()){
            apricot::aot::PreparationProgress current;{std::lock_guard<std::mutex> guard(progress.lock);current=progress.snapshot;}
            if(!Progress::update(&progress,current))progress.cancelled.store(true);
            usleep(16000);
        }worker.join();
        capture.geometry.close();capture.tiles.close();if(!ready)return stopped(display,canvas,error.c_str());
    }
    std::vector<uint8_t> artwork;
    bool native_art=read_data("/app0/userdata/launcher-uhd.rgba",artwork,3840u*2160u*4u)&&artwork.size()==3840u*2160u*4u;
    if(native_art){display.set_background(std::move(artwork));std::fill(canvas.begin(),canvas.end(),0);}
    bool art=native_art||(read_data("/app0/userdata/background.rgba",artwork,1280*720*4)&&artwork.size()==1280*720*4);
    if(art&&!native_art)memcpy(canvas.data(),artwork.data(),artwork.size());
    else if(!art)information(canvas,"BACKGROUND MISSING");
    struct Edition {std::string folder;apricot::EditionBundle bundle;std::vector<uint32_t> label;};
    std::vector<Edition> editions;
    for(const char* folder:{APP_DATA "/games/active",
        APP_DATA "/games/heartgold-r7",
        APP_DATA "/games/soulsilver-r5"
        }) {
        std::vector<uint8_t> meta,label,code;apricot::EditionBundle bundle;
        std::string base=folder;
        if(!read_data((base+"/edition.bin").c_str(),meta,256))continue;
        if(!bundle.load(meta)){say("edition_rejected folder=%s reason=manifest",folder);continue;}
        uint8_t digest[32];
        if(!read_data((base+"/title-label.rgba").c_str(),label,256*192*4)||label.size()!=256*192*4){say("edition_rejected folder=%s reason=label_missing",folder);continue;}
        oq_sha256(label.data(),label.size(),digest);if(memcmp(digest,bundle.label_sha,32)){say("edition_rejected folder=%s reason=label_identity",folder);continue;}
        // The complete candidate module is hash-checked without executing it.
        if(!read_data((base+"/game-203.oqgame").c_str(),code,256+256u*1024*1024+1048576*16)||code.size()<256||memcmp(code.data(),"OQGAME03",8)||memcmp(code.data()+88,bundle.identity,40)){say("edition_rejected folder=%s reason=module_identity",folder);continue;}
        oq_sha256(code.data(),code.size(),digest);if(memcmp(digest,bundle.module_sha,32)){say("edition_rejected folder=%s reason=module_hash",folder);continue;}
        std::vector<uint8_t> candidate_rom;GameIdentity candidate_id{};
        if(!read_data((base+"/game-data.bin").c_str(),candidate_rom,0x20000000)||candidate_rom.size()!=bundle.rom_bytes||!identify_owned_data(candidate_rom,candidate_id)||
           memcmp(candidate_id.game_code,bundle.identity,4)||memcmp(candidate_id.maker_code,bundle.identity+4,2)||candidate_id.revision!=bundle.identity[6]||memcmp(candidate_id.sha256,bundle.identity+8,32)){
            say("edition_rejected folder=%s reason=rom_identity",folder);continue;}
        for(const auto& e:editions)if(e.bundle.gold()==bundle.gold())return stopped(display,canvas,"MEHRERE DATENSAETZE DERSELBEN EDITION");
        Edition e;e.folder=base;e.bundle=bundle;e.label.resize(256*192);memcpy(e.label.data(),label.data(),label.size());editions.push_back(std::move(e));
    }
    if(editions.empty())return stopped(display,canvas,"KEINE EDITION - EIGENE NDS IN ROMS ABLEGEN");
    std::sort(editions.begin(),editions.end(),[](const Edition&a,const Edition&b){return a.bundle.gold()>b.bundle.gold();});
    apricot::CartridgeView cartridge;std::vector<uint8_t> mesh;
    if(!read_data("/app0/userdata/cartridge.mesh",mesh,2*1024*1024)||!cartridge.load(mesh))return stopped(display,canvas,"CARTRIDGE MODELL FEHLT ODER IST UNGUELTIG");
    PreparedMenuMixer menu_audio;unsigned selected=0;bool any_music=false;
    for(unsigned n=0;n<editions.size();++n){
        auto& e=editions[n];std::vector<uint8_t> bytes;GameIdentity id{};
        memcpy(id.game_code,e.bundle.identity,4);memcpy(id.maker_code,e.bundle.identity+4,2);id.revision=e.bundle.identity[6];memcpy(id.sha256,e.bundle.identity+8,32);
        if(read_data((e.folder+"/title-menu.pcm").c_str(),bytes,64*1024*1024)){
            uint8_t digest[32];oq_sha256(bytes.data(),bytes.size(),digest);
            if(!memcmp(digest,e.bundle.audio_sha,32)&&menu_audio.tracks[n].load(bytes,id))any_music=true;
        }
    }
    bool menu_started=any_music&&audio_start(PreparedMenuMixer::fill,&menu_audio);
    std::vector<uint32_t> launcher_base(1280*720,0);
    if(!native_art&&art)for(size_t n=0;n<launcher_base.size();++n){auto p=artwork.data()+n*4;launcher_base[n]=0xff000000u|uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];}
    else if(!art)std::fill(launcher_base.begin(),launcher_base.end(),0xff101820);
    apricot::launcher_glass(launcher_base,native_art?display.native_background_rgba:artwork,native_art?3840:1280,native_art?2160:720);
    menuText(launcher_base,620,76,"EDITION AUSWAEHLEN",0xffe5eeed,3,1280);
    // Dark ink on the light, background-derived glass; no opaque full-width bar.
    if(editions.size()>1){
        apricot::controller_icon(launcher_base,1280,720,602,660,apricot::ControlIcon::Dpad,true);
        menuText(launcher_base,650,674,"WAHL",0xff172d36,2,1280);
    }else menuText(launcher_base,602,674,"EDITION GEWAEHLT",0xff172d36,2,1280);
    apricot::controller_icon(launcher_base,1280,720,850,660,apricot::ControlIcon::Cross,true);
    menuText(launcher_base,896,674,"START",0xff172d36,2,1280);
    apricot::controller_icon(launcher_base,1280,720,1040,660,apricot::ControlIcon::Options,true);
    menuText(launcher_base,1086,674,"ENDE",0xff172d36,2,1280);
    say("cartridge_launcher editions=%zu selected=%.4s automatic_selection=%u native_mesh=1",editions.size(),editions[selected].bundle.identity,editions.size()==1);
    say("launcher_background=%s native_background=%u music=%s",art?"loaded":"missing",native_art,menu_started?"private-title-pcm":"not_prepared");
    support_report.phase=apricot::SupportReport::Phase::Selection;write_support_report();
    app_heading(launcher_base,true);
    double menu_begin=now_seconds(),navigation_after=0;bool navigation_armed=true;float emphasis[2]={1,0};bool confirming=false;double confirm_begin=0;double menu_perf_begin=menu_begin,menu_max=0;uint64_t menu_frames=0;
    for(;;){
        double frame_begin=now_seconds(),now=frame_begin,elapsed=now-menu_begin;pad p{};pad_poll(&p);
        if(p.pressed&PAD_OPTIONS){support_report.phase=apricot::SupportReport::Phase::Exit;write_support_report();if(menu_started)audio_stop();display.close(stderr);return 0;}
        int direction=0;
        bool left=(p.held&PAD_LEFT)||p.left_x< -0.6f,right=(p.held&PAD_RIGHT)||p.left_x>0.6f;
        if(!left&&!right)navigation_armed=true;
        if(navigation_armed&&now>=navigation_after&&left!=right){direction=left?-1:1;navigation_armed=false;}
        if(!confirming&&direction&&editions.size()>1){unsigned target=direction<0?0:unsigned(editions.size()-1);
            if(target!=selected){double begin=now_seconds();selected=target;navigation_after=now+0.3;menu_audio.select(selected);say("cartridge_switch selected=%.4s main_thread_us=%.1f cached_audio=1",editions[selected].bundle.identity,(now_seconds()-begin)*1000000);}}
        bool import_pending=false;
        if(!confirming&&((p.pressed&PAD_CROSS)||control_enabled||import_pending)){confirming=true;confirm_begin=now;}
        if(confirming&&now-confirm_begin>=0.5)break;
        canvas=launcher_base;
        size_t pixels=0;
        for(unsigned n=0;n<editions.size();++n){
            emphasis[n]+=(float(n==selected)-emphasis[n])*0.14f;
            float cx=editions.size()==1?920.f:(n==0?730.f:1080.f),size=editions.size()==1?199.5f:154.f+17.5f*emphasis[n];
            float lift=confirming&&n==selected?float((now-confirm_begin)/0.5):0.f;
            double angle=elapsed*0.38+0.18*std::sin(elapsed*0.7);if(confirming&&n==selected)angle=apricot::confirmation_spin(angle,lift);
            pixels+=cartridge.draw(canvas,1280,720,cx,535.f-lift*90,size*(1+lift*0.18f),angle,editions[n].bundle.gold(),editions[n].label,true);
            uint32_t color=n==selected?0xff112c38:0xff394c53;
            const char* name=editions[n].bundle.gold()?"HEARTGOLD":"SOULSILVER";
            const char* subtitle=editions[n].bundle.gold()?"GOLDENE EDITION":"SILBERNE EDITION";
            menuText(canvas,apricot::centered_text_x(cx,strlen(name),3),584,name,color,3,1280);
            menuText(canvas,apricot::centered_text_x(cx,strlen(subtitle),2),620,subtitle,color,2,1280);
            if(n==selected)for(unsigned y=648;y<651;++y)std::fill_n(canvas.data()+y*1280+unsigned(cx)-48,96,0xff27596d);
        }
        if(elapsed<0.03)say("cartridge_render pixels=%zu",pixels);
        if(display.present(canvas.data(),1280,720,1280*4,stderr,nullptr,true)){if(menu_started)audio_stop();return 1;}
        ++menu_frames;double finish=now_seconds();menu_max=std::max(menu_max,finish-frame_begin);
        if(finish-menu_perf_begin>=2){say("cartridge_perf fps=%.1f max_frame_ms=%.2f cached_audio=1",menu_frames/(finish-menu_perf_begin),menu_max*1000);menu_perf_begin=finish;menu_frames=0;menu_max=0;}
        usleep(1000);
    }
    if(menu_started)audio_stop();
    support_report.set_game(editions[selected].bundle.identity);
    std::string selected_folder=editions[selected].folder;auto selected_bundle=editions[selected].bundle;
    std::vector<uint8_t> rom;GameIdentity identity{};
    if(!read_data((selected_folder+"/game-data.bin").c_str(),rom,0x20000000)||!identify_owned_data(rom,identity)||
       memcmp(identity.game_code,selected_bundle.identity,4)||memcmp(identity.maker_code,selected_bundle.identity+4,2)||identity.revision!=selected_bundle.identity[6]||memcmp(identity.sha256,selected_bundle.identity+8,32))return stopped(display,canvas,"SPIELDATEN WURDEN VERAENDERT");
    editions.clear();editions.shrink_to_fit();
    say("native_player_version=%s native_image_format=3; gameplay compatibility incomplete",APRICOT_VERSION);
    information(canvas,"SPIEL WIRD VORBEREITET");display.present(canvas.data(),1280,720,1280*4,stderr,nullptr);
    NativeGameModule module;NativeMachine machine;
    std::vector<uint8_t> selected_code;uint8_t code_digest[32];
    if(!read_data((selected_folder+"/game-203.oqgame").c_str(),selected_code,256+256u*1024*1024+1048576*16))return stopped(display,canvas,"NATIVES MODUL FEHLT");
    oq_sha256(selected_code.data(),selected_code.size(),code_digest);
    if(memcmp(code_digest,selected_bundle.module_sha,32))return stopped(display,canvas,"NATIVES MODUL WURDE VERAENDERT");
    if(!module.from_bytes(selected_code.data(),selected_code.size(),identity))return stopped(display,canvas,module.error);
    selected_code.clear();selected_code.shrink_to_fit();
    say("native_module bytes=%zu id=%.4s abi=2",module.image_bytes,identity.game_code);
    if(!boot_native_game(machine,module,std::move(rom)))return stopped(display,canvas,machine.error);
    std::vector<uint8_t> identity_refs;
    bool refs_ready=read_data((selected_folder+"/identity-refs.bin").c_str(),identity_refs,64*1024*1024);
    if(refs_ready){uint8_t digest[32];oq_sha256(identity_refs.data(),identity_refs.size(),digest);refs_ready=!memcmp(digest,selected_bundle.refs_sha,32)&&load_identity_references(identity_refs,identity,machine.bus.overlays);}
    say("identity_references=%s",refs_ready?"verified":"unavailable-fallback");
    identity_refs.clear();identity_refs.shrink_to_fit();
    NativeSaveStore saves;if(!saves.open(APP_DATA "/saves",identity,machine.bus.card.save))return stopped(display,canvas,saves.error);
    machine.bus.rtc.snapshot=[](void*,uint8_t *values)->bool {
        time_t now=time(nullptr);tm local{};if(!ps5_localtime_r(&now,&local))return false;
        auto bcd=[](unsigned n) {return uint8_t((n/10)*16+n%10);};
        values[0]=bcd(unsigned(local.tm_year+1900)%100);values[1]=bcd(unsigned(local.tm_mon+1));values[2]=bcd(unsigned(local.tm_mday));values[3]=uint8_t(local.tm_wday);
        values[4]=bcd(unsigned(local.tm_hour));values[5]=bcd(unsigned(local.tm_min));values[6]=bcd(unsigned(local.tm_sec));return true;
    };
    NativeGameVideo video;
    auto loader=reinterpret_cast<PFN_vkGetInstanceProcAddr>(vk_icdGetInstanceProcAddr);
    if(!video.initialize(loader,oq_tiles,sizeof(oq_tiles),oq_geometry,sizeof(oq_geometry),oq_geometry_resolve,sizeof(oq_geometry_resolve))) {
        say("Vulkan init: %s",video.error.c_str());return stopped(display,canvas,"GRAFIKAUSGABE KONNTE NICHT STARTEN");
    }
    NativeAudioBridge audio;machine.bus.sound.sink=NativeAudioBridge::push;machine.bus.sound.sink_context=&audio;
    bool audio_started=false,audio_attempted=false;NativePadInput input;
    auto &bus=machine.bus;uint64_t shown=0,last_save_bytes=0;double deadline=now_seconds(),last_input=deadline,last_save_write=deadline;
    std::vector<uint8_t> field_art;
    bool field_native=read_data("/app0/userdata/field-uhd.rgba",field_art,3840u*2160u*4u)&&field_art.size()==3840u*2160u*4u;
    if(field_native)display.set_background(std::move(field_art));
    bool field_ok=field_native||(read_data("/app0/userdata/field-background.rgba",field_art,1280*720*4)&&field_art.size()==1280*720*4);
    if(!field_native)display.set_background({});
    say("field_background=%s presentation=sharp main=800x600 touch=432x324 native_background=%u",field_ok?"loaded":"missing",field_native);
    std::vector<uint32_t> field_base(1280*720,0);
    if(!field_native&&field_ok)memcpy(field_base.data(),field_art.data(),field_art.size());
    else if(!field_ok)std::fill(field_base.begin(),field_base.end(),0xff101820);
    apricot::launcher_glass(field_base,field_native?display.native_background_rgba:field_art,field_native?3840:1280,field_native?2160:720,826,400,1272,704,false);
    gameplay_controls(field_base);
    uint64_t save_commits=0;double last_step_warning=0;
    unsigned consecutive_audio_skips=0;uint64_t audio_render_skips=0;
    double perf_begin=now_seconds(),perf_sim=0,perf_sim_max=0,perf_render=0,perf_compose=0,perf_present=0;
    uint64_t perf_frames=0,perf_presents=0,perf_skips=0,perf_geometry=bus.geometry.swaps;
    say("audio_policy=start3072 resume1024 low1536 fade128 max_render_skips3");
    say("overlay_identity_hash=prefix-shared accelerated_sha256=%u",oq_sha_accelerated_available());
    const char *failure=nullptr;bool exit_requested=false;
    support_report.phase=apricot::SupportReport::Phase::Gameplay;write_support_report();
    while(!exit_requested&&!failure) {
        auto update_input=[&]() {
            double now=now_seconds();pad p{};pad_poll(&p);input.update(p,now-last_input);last_input=now;
            bus.buttons=input.keys;bus.spi.touch_x=unsigned(input.touch_x);bus.spi.touch_y=unsigned(input.touch_y);bus.spi.pressed=input.touch;
            exit_requested=input.exit;
        };
        update_input();uint64_t previous=bus.raster.frames;unsigned quanta=0;double step_begin=now_seconds();uint64_t hash_begin=bus.overlays.identity_hash_bytes,checks_begin=bus.overlays.identity_checks;
        while(!exit_requested&&bus.raster.frames==previous&&quanta++<1000000) {
            if(!machine.step()) {failure=machine.error;break;}
            if((quanta&4095)==0)update_input();
        }
        if(exit_requested||failure)break;
        if(bus.raster.frames==previous) {failure="LAUFZEIT ZEITLIMIT";break;}
        double step_seconds=now_seconds()-step_begin;
        ++perf_frames;perf_sim+=step_seconds;perf_sim_max=std::max(perf_sim_max,step_seconds);
        if(step_seconds>0.1&&now_seconds()-last_step_warning>2) {last_step_warning=now_seconds();say("slow_native_frame frame=%llu ms=%.1f quanta=%u save_write_bytes=%llu identity_checks=%llu hash_bytes=%llu",(unsigned long long)bus.raster.frames,step_seconds*1000,quanta,(unsigned long long)bus.card.save.write_bytes,(unsigned long long)(bus.overlays.identity_checks-checks_begin),(unsigned long long)(bus.overlays.identity_hash_bytes-hash_begin));}
        if(!control_enabled&&!audio_attempted&&audio.queued()>=NativeAudioBridge::start_samples) {audio_attempted=true;audio_started=audio_start(NativeAudioBridge::fill,&audio);}
        double save_time=now_seconds();
        if(bus.card.save.write_bytes!=last_save_bytes) {last_save_bytes=bus.card.save.write_bytes;last_save_write=save_time;}
        if(bus.card.save.dirty&&!bus.card.save.selected&&save_time-last_save_write>=0.5) {
            double begin=now_seconds();bool ok=saves.flush(bus.card.save);++save_commits;
            say("save_commit count=%llu ms=%.1f write_bytes=%llu ok=%u",(unsigned long long)save_commits,(now_seconds()-begin)*1000,(unsigned long long)bus.card.save.write_bytes,ok);
            if(!ok){failure="SPIELSTAND KONNTE NICHT GESICHERT WERDEN";break;}
        }
        // Recover audio headroom without dropping simulation or input work.
        // Bound visual decimation so a slow producer never hides the game.
        if(audio_started&&audio.queued()<NativeAudioBridge::low_samples&&consecutive_audio_skips<3){
            ++consecutive_audio_skips;++audio_render_skips;++perf_skips;deadline=now_seconds();continue;
        }
        consecutive_audio_skips=0;
        double render_begin=now_seconds();
        if(!video.render(bus)) {say("video fail: %s",video.error.c_str());failure="BILDDARSTELLUNG NOCH UNVOLLSTAENDIG";break;}
        double render_seconds=now_seconds()-render_begin;
        perf_render+=render_seconds;double compose_begin=now_seconds();
        if(render_seconds>0.1&&now_seconds()-last_step_warning>2) {last_step_warning=now_seconds();say("slow_render frame=%llu ms=%.1f geometry_frame=%llu",(unsigned long long)bus.raster.frames,render_seconds*1000,(unsigned long long)bus.raster_geometry_frame);}
        canvas=field_base;
        app_heading(canvas);
        unsigned top=(bus.power_control[0]&0x8000)?0:1;screen_copy(canvas,video.pixels,top,16,88,800,600);screen_copy(canvas,video.pixels,1-top,832,64,432,324);
        int cursor_x=int((832+input.touch_x*432/256)*3),cursor_y=int((64+input.touch_y*324/192)*3);
        double present_begin=now_seconds();perf_compose+=present_begin-compose_begin;
        if(display.present(canvas.data(),1280,720,1280*4,stderr,nullptr,false,cursor_x,cursor_y)) {failure="BILDAUSGABE ANGEHALTEN";break;}
        perf_present+=now_seconds()-present_begin;++perf_presents;
        double perf_now=now_seconds(),perf_window=perf_now-perf_begin;
        if(perf_window>=2){
            say("frame_cost simulation_fps=%.1f presentation_fps=%.1f geometry_fps=%.1f skipped=%llu sim_ms=%.2f sim_max_ms=%.2f render_ms=%.2f compose_ms=%.2f present_ms=%.2f audio_queued=%u",
                perf_frames/perf_window,perf_presents/perf_window,(bus.geometry.swaps-perf_geometry)/perf_window,(unsigned long long)perf_skips,
                perf_sim*1000/perf_frames,perf_sim_max*1000,perf_render*1000/perf_presents,perf_compose*1000/perf_presents,perf_present*1000/perf_presents,audio.queued());
            perf_begin=perf_now;perf_sim=perf_sim_max=perf_render=perf_compose=perf_present=0;
            perf_frames=perf_presents=perf_skips=0;perf_geometry=bus.geometry.swaps;
        }
        deadline+=double(HostVideoClock::frame_cycles)/33513982.0;double now=now_seconds();
        if(audio_started&&audio.queued()<NativeAudioBridge::low_samples)deadline=now;
        else if(deadline>now)usleep(unsigned((deadline-now)*1000000));else if(now-deadline>0.25)deadline=now;
    }
    if(audio_started)audio_stop();if(!saves.flush(bus.card.save))failure="SPIELSTAND KONNTE NICHT GESICHERT WERDEN";
    say("native_player_end frames=%llu reason=%s",static_cast<unsigned long long>(bus.raster.frames),failure?failure:"controller exit");
    support_report.frames=bus.raster.frames;support_report.stop9=machine.stops[0];support_report.stop7=machine.stops[1];
    support_report.phase=failure?apricot::SupportReport::Phase::Stopped:apricot::SupportReport::Phase::Exit;write_support_report();
    if(failure)return stopped(display,canvas,failure);display.close(stderr);return 0;
}
