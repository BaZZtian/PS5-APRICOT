#include "code_discovery.h"
#include <algorithm>
#include <deque>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
namespace apricot::aot {
namespace {
#include "profile_seeds.inc"
struct Reader {
    const std::vector<CodeView>&views;
    bool read(G pc,unsigned width,G &value)const{
        for(const auto &v:views)if(v.image&&pc>=v.base&&uint64_t(pc)-v.base+width<=v.bytes&&v.offset<=v.image->size()&&v.bytes<=v.image->size()-v.offset){
            auto p=v.image->data()+v.offset+pc-v.base;value=G(p[0])|G(p[1])<<8;if(width==4)value|=G(p[2])<<16|G(p[3])<<24;return true;
        }return false;
    }
};
struct Flow {bool terminal=false,conditional=false,dynamic=false,call=false;std::vector<CodeSeed> targets;};
Flow flow(G word,G pc,bool thumb){
    Flow f;
    if(!thumb){
        f.conditional=(word>>28)<14;
        if((word>>28)==15){int64_t d=int64_t((word&0xffffff)*4)|((word>>23)&2);if(d&0x2000000)d-=0x4000000;f.targets.push_back({pc+8+G(d),true});f.terminal=f.call=true;}
        else if((word&0x0e000000)==0x0a000000){int64_t d=word&0xffffff;if(d&0x800000)d-=0x1000000;f.targets.push_back({pc+8+G(d*4),false});f.terminal=true;f.call=word&0x1000000;}
        else if((word&0x0ffffff0)==0x012fff10||(word&0x0ffffff0)==0x012fff30){f.terminal=f.dynamic=true;f.call=word&32;}
        else if((word&0x0e000000)==0x08000000){f.terminal=f.dynamic=(word&0x100000)&&(word&0x8000);}
        else if((word&0x0c000000)==0x04000000){f.terminal=f.dynamic=(word&0x100000)&&((word>>12)&15)==15;}
        else if((word&0x0c000000)==0){unsigned op=(word>>21)&15;f.terminal=f.dynamic=((word>>12)&15)==15&&(op<8||op>11);}
    }else {
        if((word&0xfc00)==0x4400){unsigned op=(word>>8)&3,dest=(word&7)|((word>>4)&8);f.terminal=f.dynamic=op==3||(op!=1&&dest==15);f.call=op==3&&(word&128);}
        else if((word&0xf600)==0xb400){f.terminal=f.dynamic=(word&0x800)&&(word&0x100);}
        else if((word&0xf000)==0xd000&&((word>>8)&15)<14){f.terminal=f.conditional=true;f.targets.push_back({pc+4+G(int32_t(int8_t(word&255))*2),true});}
        else if((word&0xf800)==0xe000){int32_t d=word&2047;if(d&1024)d-=2048;f.terminal=true;f.targets.push_back({pc+4+G(d*2),true});}
        else if((word&0xf800)==0xf800||(word&0xf800)==0xe800)f.terminal=f.dynamic=f.call=true;
    }return f;
}
using Cache=std::map<std::tuple<G,G,bool>,bool>;
bool supported(Cache &cache,G pc,G word,bool thumb,G core){auto key=std::make_tuple(core,word,thumb);auto it=cache.find(key);if(it!=cache.end())return it->second;std::string error;bool ok=supports({pc,word,thumb},core,error);cache.emplace(key,ok);return ok;}
}
std::vector<CodeView> core_views(const PreparedCore&c){
    std::vector<CodeView> views;
    for(auto it=c.sections.rbegin();it!=c.sections.rend();++it)if(!it->implicit)views.push_back({it->address,&c.image,it->offset,it->bytes});
    views.push_back({c.base,&c.image,0,c.image.size()});return views;
}
std::vector<CodeSeed> profile_seeds(const GameIdentity&id,G core,G overlay){
    const G (*seeds)[4]=nullptr;size_t count=0;
    if(!memcmp(id.game_code,"IPKD",4)){seeds=seeds_IPKD;count=sizeof(seeds_IPKD)/sizeof(seeds_IPKD[0]);}
    if(!memcmp(id.game_code,"IPGD",4)){seeds=seeds_IPGD;count=sizeof(seeds_IPGD)/sizeof(seeds_IPGD[0]);}
    std::vector<CodeSeed> out;for(size_t i=0;i<count;++i)if(seeds[i][0]==core&&seeds[i][1]==overlay)out.push_back({seeds[i][2],bool(seeds[i][3])});return out;
}
bool initialization_guard(const GameIdentity&id,G core,G overlay){
    const G(*guards)[2]=nullptr;size_t count=0;
    if(!memcmp(id.game_code,"IPKD",4)){guards=guards_IPKD;count=sizeof(guards_IPKD)/sizeof(guards_IPKD[0]);}
    if(!memcmp(id.game_code,"IPGD",4)){guards=guards_IPGD;count=sizeof(guards_IPGD)/sizeof(guards_IPGD[0]);}
    for(size_t i=0;i<count;++i)if(guards[i][0]==core&&guards[i][1]==overlay)return true;return false;
}
std::vector<CodeSeed> overlay_initializers(const PreparedOverlay&o){
    std::vector<CodeSeed> result;if(o.init_begin==o.init_end)return result;
    if((o.init_begin&3)||(o.init_end&3)||o.init_begin<o.address||o.init_end<o.init_begin||uint64_t(o.init_end)-o.address>o.image.size())return result;
    for(G pc=o.init_begin;pc<o.init_end;pc+=4){size_t at=pc-o.address;G value=G(o.image[at])|G(o.image[at+1])<<8|G(o.image[at+2])<<16|G(o.image[at+3])<<24;
        if(!value||value==0xffffffff)continue;bool mode=value&1;G address=value&~1u;if(!mode&&(address&3))continue;result.push_back({address,mode});
    }return result;
}
std::vector<CodeSeed> external_literals(const PreparedOverlay&o,const PreparedCore&c){
    auto views=core_views(c);Reader r{views};Cache cache;std::set<CodeSeed> out;
    for(size_t at=0;at+4<=o.image.size();at+=4){G value=G(o.image[at])|G(o.image[at+1])<<8|G(o.image[at+2])<<16|G(o.image[at+3])<<24;bool mode=value&1;G pc=value&~1u,word;
        if((!mode&&(pc&3))||!r.read(pc,mode?2:4,word)||(mode?(word==0||word==65535):(word>>28)!=14))continue;
        if(supported(cache,pc,word,mode,c.core))out.emplace(pc,mode);
    }return {out.begin(),out.end()};
}
CodeDiscovery discover_code(const std::vector<CodeView>&views,G core,const std::vector<CodeSeed>&seeds,size_t limit){
    CodeDiscovery out;out.program.core=core;if(!seeds.empty())out.program.entry=seeds.front().first&~3u;
    if(core!=9&&core!=7){out.limit_reached=true;return out;}
    Reader r{views};Cache cache;std::set<CodeSeed> candidates,seen;
    auto at=[&](const CodeView&v,size_t off,unsigned width,G &word){
        if(!v.image||off>v.bytes||width>v.bytes-off||v.offset>v.image->size()||v.bytes>v.image->size()-v.offset)return false;
        auto p=v.image->data()+v.offset+off;word=G(p[0])|G(p[1])<<8;if(width==4)word|=G(p[2])<<16|G(p[3])<<24;return true;
    };
    // Literal pointers and function prologues are bounded entry hypotheses,
    // not a claim that every matching byte sequence is executable.
    for(const auto &v:views)for(size_t off=0;off+4<=v.bytes;off+=4){G value,word;if(!at(v,off,4,value))continue;bool mode=value&1;G pc=value&~1u;
        if((!mode&&(pc&3))||!r.read(pc,mode?2:4,word)||(mode?(word==0||word==65535):(word>>28)!=14))continue;
        if(supported(cache,pc,word,mode,core))candidates.emplace(pc,mode);
    }
    out.pointer_candidates=candidates.size();
    for(const auto &v:views){
        for(size_t off=0;off+2<=v.bytes;off+=2){G first;if(!at(v,off,2,first))continue;G base=v.base+G(off);
            if((first&0xfe00)==0xb400&&(first&0x1ff))candidates.emplace(base,true);
            G second;if(at(v,off+2,2,second)){
                bool literal=(first&0xf800)==0x4800,veneer=literal&&((second&0xff87)==0x4700||(second&0xff87)==0x4780)&&((second>>3)&15)==((first>>8)&7);
                if(veneer||(second==0x4770&&(literal||(first&0xf800)==0x2000)))candidates.emplace(base,true);
                // Argument adaptation may precede the final literal target
                // load. Walk only bounded straight-line low-register copies
                // and loads; stop at data, stores or control flow.
                if(veneer)for(unsigned distance=2;distance<=16&&distance<=off;distance+=2){G prefix;if(!at(v,off-distance,2,prefix))break;
                    unsigned kind=prefix&0xf800;
                    if((prefix&0xfe00)!=0x1c00&&kind!=0x6800&&kind!=0x7800&&kind!=0x8800)break;
                    candidates.emplace(base-distance,true);
                }
                if(literal){unsigned dest=(first>>8)&7;for(unsigned distance=2;distance<=16;distance+=2){G following;if(!at(v,off+distance,2,following))break;
                    if((following&0xff87)==0x4700||(following&0xff87)==0x4780){if(((following>>3)&15)==dest)candidates.emplace(base,true);break;}
                    // A veneer may load, copy or shift arguments before tail-
                    // calling the literal target. Keep this scan bounded.
                    // Only permit known non-control instructions which cannot
                    // overwrite the register holding that target.
                    bool immediate=(following&0xf800)==0x2000&&((following>>8)&7)!=dest;
                    unsigned kind=following&0xf800;
                    bool load=((kind==0x6800||kind==0x7800||kind==0x8800)&&(following&7)!=dest)||
                              (kind==0x4800&&((following>>8)&7)!=dest);
                    bool argument_math=((following&0xf800)==0x0000||(following&0xfe00)==0x1c00)&&(following&7)!=dest;
                    if(!immediate&&!load&&!argument_math)break;
                }}
            }
            if(first==0x4770)for(size_t distance=2;distance<=64&&distance<=off;distance+=2){G w;if(!at(v,off-distance,2,w)||!w||w==65535||!supported(cache,base-G(distance),w,true,core))break;
                auto f=flow(w,base-G(distance),true);if(f.terminal&&!f.conditional)break;if(!((base-distance)&3))candidates.emplace(base-G(distance),true);
            }
        }
        for(size_t off=0;off+4<=v.bytes;off+=4){G first;if(!at(v,off,4,first))continue;G base=v.base+G(off);
            if(((first&0xffff0000)==0xe92d0000&&(first&65535))||first==0xe52de004)candidates.emplace(base,false);
            if(first==0xe12fff1e||first==0xe1a0f00e)for(size_t distance=4;distance<=64&&distance<=off;distance+=4){G w;if(!at(v,off-distance,4,w)||!w||w==0xffffffff||!supported(cache,base-G(distance),w,false,core))break;
                auto f=flow(w,base-G(distance),false);if(f.terminal&&!f.conditional)break;candidates.emplace(base-G(distance),false);
            }
        }
    }
    out.prologue_candidates=candidates.size()-out.pointer_candidates;
    std::vector<CodeSeed> optional(candidates.begin(),candidates.end());std::sort(optional.begin(),optional.end(),[](auto a,auto b){return std::tie(a.second,a.first)<std::tie(b.second,b.first);});size_t next_optional=0;
    std::deque<CodeSeed> queue(seeds.begin(),seeds.end());
    while((!queue.empty()||next_optional<optional.size())&&seen.size()<limit){
        if(queue.empty())queue.push_back(optional[next_optional++]);auto [pc,mode]=queue.front();queue.pop_front();
        if((pc&(mode?1:3))||!seen.emplace(pc,mode).second)continue;
        G word;if(!r.read(pc,mode?2:4,word)){++out.external;out.external_targets.push_back({pc,mode});continue;}
        if(!supported(cache,pc,word,mode,core)){++out.unsupported;continue;}
        out.program.code.push_back({pc,word,mode});auto f=flow(word,pc,mode);
        for(auto target:f.targets)queue.push_back(target);if(f.dynamic)++out.indirect;
        if(!mode&&(word&0x0ffffff0)==0x008ff100){
            for(unsigned offset=0;offset<1024;offset+=4){G branch;if(!r.read(pc+8+offset,4,branch)||(branch&0xff000000)!=0xea000000)break;queue.push_back({pc+8+offset,false});}
            G a[6];bool ok=true;for(unsigned i=0;i<6;++i)ok=ok&&r.read(pc+8+i*4,4,a[i]);
            if(ok&&a[0]==a[3]&&a[1]==a[4]&&a[2]==a[5]&&(a[0]&0xfff00000)==0xe0b00000&&(a[1]&0xfff00000)==0x30400000&&(a[2]&0xfff00000)==0xe0b00000)queue.push_back({pc+8,false});
        }
        if(!mode&&((word&0x0ffffff0)==0x012fff10||(word&0x0ffffff0)==0x012fff30)){
            G previous;if(r.read(pc-4,4,previous)){
                if((previous&0x0fffffff)==0x01a0e00f)f.call=true;
                if((previous&0x0f7f0000)==0x051f0000&&((previous>>12)&15)==(word&15)){
                    G value,dummy,address=pc+4+G(int(previous&4095)*(previous&0x800000?1:-1));
                    if(r.read(address,4,value)&&r.read(value&~3u,4,dummy))queue.push_back({value&(value&1?~1u:~3u),bool(value&1)});
                }
            }
        }
        if(mode&&f.call){G previous;if(r.read(pc-2,2,previous)&&(previous&0xf800)==0xf000){int32_t d=previous&2047;if(d&1024)d-=2048;
                G target=pc+2+G(d*4096)+(word&2047)*2;bool x=(word&0xf800)==0xe800;queue.push_back({x?target&~3u:target,!x});
            }
        }
        if(mode&&(word&0xff87)==0x4487){
            bool resolved=false;
            unsigned reg=(word>>3)&15;G prev[9]{};bool have[9]{};for(unsigned i=0;i<9;++i)have[i]=r.read(pc-2*(i+1),2,prev[i]);
            if(reg<8&&std::all_of(have,have+7,[](bool b){return b;})){
                G asr=prev[0],lsl=prev[1],load=prev[2],base=prev[3],index=prev[4],def=prev[5],check=prev[6],compare=prev[7];unsigned source=(index>>3)&7;
                bool signed_bounds=false;
                bool guarded=have[7]&&(def&0xf800)==0xe000&&check==0xd900&&(compare&0xff00)==(0x2800|(source<<8));
                if((def&0xff00)==0xd800&&(check&0xff00)==(0x2800|(source<<8))){guarded=true;compare=check;}
                if((check&0xf800)==0xf000&&((def&0xf800)==0xf800||(def&0xf800)==0xe800)&&have[7]&&compare==0xd901&&have[8]&&(prev[8]&0xff00)==(0x2800|(source<<8))){guarded=true;compare=prev[8];}
                // Signed switch: CMP index,#max; BGT default; CMP index,#0;
                // BLT default. Both guards are needed to prove the table bound.
                if(have[8]&&(def&0xff00)==0xdb00&&check==(0x2800|(source<<8))&&
                   (prev[7]&0xff00)==0xdc00&&(prev[8]&0xff00)==(0x2800|(source<<8))){guarded=signed_bounds=true;compare=prev[8];}
                // Index biased by one: CMP original,#max; BGT default;
                // ADDS index,original,#1; BMI default proves 0..max+1.
                unsigned original=(check>>3)&7;
                if(have[8]&&(def&0xff00)==0xd400&&(check&0xfe00)==0x1c00&&
                   ((check>>6)&7)==1&&(check&7)==source&&(prev[7]&0xff00)==0xdc00&&
                   (prev[8]&0xff00)==(0x2800|(original<<8))&&(prev[8]&255)<255){
                    guarded=signed_bounds=true;compare=(prev[8]&255)+1;
                }
                if(signed_bounds){G end=pc-4+((load>>6)&31)*2+((compare&255)+1)*2;
                    G upper=pc-16+4+G(int32_t(int8_t(prev[7]&255))*2),lower=pc-12+4+G(int32_t(int8_t(def&255))*2);
                    guarded=upper>=end&&lower>=end;
                }
                bool valid=asr==(0x1400|(reg<<3)|reg)&&lsl==(0x0400|(reg<<3)|reg)&&(load&0xf83f)==(0x8800|(reg<<3)|reg)&&base==(0x4478|reg)&&(index&0xfe07)==(0x1800|reg)&&((index>>6)&7)==source&&guarded;
                if(valid){G table=pc-4+((load>>6)&31)*2;std::vector<CodeSeed> cases;bool complete=true;
                    for(unsigned slot=0;slot<=(compare&255);++slot){G value,dummy;if(!r.read(table+slot*2,2,value)||(value&1)){complete=false;break;}G target=pc+4+G(int32_t(int16_t(value)));if(!r.read(target,2,dummy)){complete=false;break;}cases.push_back({target,true});}
                    if(complete){for(auto target:cases)queue.push_back(target);out.jump_tables.push_back({pc,G(cases.size())});resolved=true;}
                }
            }
            if(!resolved)out.unresolved_switches.push_back(pc);
        }
        if(!f.terminal||f.conditional||f.call)queue.push_back({pc+(mode?2:4),mode});
    }
    out.limit_reached=!queue.empty()||next_optional<optional.size();
    std::sort(out.program.code.begin(),out.program.code.end(),[](const auto&a,const auto&b){return std::tie(a.thumb,a.pc)<std::tie(b.thumb,b.pc);});return out;
}
bool discover_rom(const PreparedRom &rom,DiscoveredRom &out,std::string &error,DiscoveryProgress progress,void*context){
    out={};error.clear();DiscoveredRom result;
    auto add_main=[&](const std::vector<CodeView>&views,const std::vector<CodeSeed>&targets,std::vector<CodeSeed>&roots){
        for(auto root:targets)for(const auto&v:views)if(root.first>=v.base&&uint64_t(root.first)-v.base+(root.second?2:4)<=v.bytes){roots.push_back(root);break;}
    };
    for(const auto*core:{&rom.arm9,&rom.arm7}){
        auto &destination=core->core==9?result.arm9:result.arm7;auto views=core_views(*core);
        auto roots=profile_seeds(rom.identity,core->core);roots.insert(roots.begin(),{core->entry,false});
        for(const auto &overlay:core->overlays){
            if(progress&&!progress(context,core->core,overlay.id,0)){error="Preparation cancelled";return false;}
            auto literals=external_literals(overlay,*core);roots.insert(roots.end(),literals.begin(),literals.end());
            auto selected=profile_seeds(rom.identity,core->core,overlay.id),initializers=overlay_initializers(overlay);
            for(auto root:initializers){
                if(root.first>=overlay.address&&uint64_t(root.first)-overlay.address+(root.second?2:4)<=overlay.image.size())selected.push_back(root);
                else add_main(views,{root},roots);
            }
            auto found=discover_code({{overlay.address,&overlay.image,0,overlay.image.size()}},core->core,selected);
            if(found.limit_reached){error="Overlay discovery bound reached";return false;}
            add_main(views,found.external_targets,roots);
            if(progress&&!progress(context,core->core,overlay.id,found.program.code.size())){error="Preparation cancelled";return false;}
            destination.overlays.push_back({overlay.id,std::move(found)});
        }
        if(progress&&!progress(context,core->core,0xffffffffu,0)){error="Preparation cancelled";return false;}
        destination.main=discover_code(views,core->core,roots);destination.main.program.entry=core->entry;
        if(destination.main.limit_reached){error="Main-code discovery bound reached";return false;}
        // Direct calls can cross independently mapped images. Propagate their
        // proven targets until no destination gains an entry; function-shape
        // heuristics alone cannot find empty or unusual leaf functions.
        std::set<CodeSeed> main_roots(roots.begin(),roots.end());
        std::vector<std::set<CodeSeed>> overlay_roots;
        for(const auto&o:core->overlays){auto s=profile_seeds(rom.identity,core->core,o.id),init=overlay_initializers(o);
            std::set<CodeSeed> entries(s.begin(),s.end());for(auto t:init)if(t.first>=o.address&&uint64_t(t.first)-o.address+(t.second?2:4)<=o.image.size())entries.insert(t);
            overlay_roots.push_back(std::move(entries));}
        bool converged=false;
        for(unsigned round=0;round<32;++round){
            std::set<CodeSeed> targets(destination.main.external_targets.begin(),destination.main.external_targets.end());
            for(const auto&item:destination.overlays)targets.insert(item.second.external_targets.begin(),item.second.external_targets.end());
            bool changed=false,main_changed=false;
            for(auto t:targets)for(const auto&v:views)if(t.first>=v.base&&uint64_t(t.first)-v.base+(t.second?2:4)<=v.bytes){main_changed|=main_roots.insert(t).second;break;}
            for(size_t n=0;n<core->overlays.size();++n){const auto&o=core->overlays[n];bool added=false;
                for(auto t:targets)if(t.first>=o.address&&uint64_t(t.first)-o.address+(t.second?2:4)<=o.image.size())added|=overlay_roots[n].insert(t).second;
                if(!added)continue;changed=true;
                if(progress&&!progress(context,core->core,o.id,0)){error="Preparation cancelled";return false;}
                auto&found=destination.overlays[n].second;found=discover_code({{o.address,&o.image,0,o.image.size()}},core->core,{overlay_roots[n].begin(),overlay_roots[n].end()});
                if(found.limit_reached){error="Cross-image overlay discovery bound reached";return false;}
            }
            if(main_changed){changed=true;destination.main=discover_code(views,core->core,{main_roots.begin(),main_roots.end()});destination.main.program.entry=core->entry;
                if(destination.main.limit_reached){error="Cross-image main discovery bound reached";return false;}}
            if(!changed){converged=true;break;}
        }
        if(!converged){error="Cross-image discovery convergence bound reached";return false;}
        if(progress&&!progress(context,core->core,0xffffffffu,destination.main.program.code.size())){error="Preparation cancelled";return false;}
    }out=std::move(result);return true;
}
}
