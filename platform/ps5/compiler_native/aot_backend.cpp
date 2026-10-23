// Experimental console-local AOT compiler. No runtime ARM decoder or JIT.
#include "aot_backend.h"
#include "sha256.h"
#include "sha256_stream.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>

namespace apricot::aot {
namespace {
#include "kernel_image.inc"
constexpr size_t max_instructions=524288,max_image=256u*1024*1024;
constexpr uint32_t cpsr=offsetof(GuestCpu,cpsr),budget=offsetof(GuestCpu,budget),
                   ticks=offsetof(GuestCpu,ticks),fault=offsetof(GuestCpu,fault);
struct Fixup {size_t at; std::string label;};
struct Emitter {
    std::vector<uint8_t> bytes;
    std::map<std::string,size_t> labels;
    std::vector<Fixup> patches;
    void b(std::initializer_list<uint8_t> v){bytes.insert(bytes.end(),v);}
    void u32(uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(v>>(i*8)));}
    void mark(const std::string &s){labels.emplace(s,bytes.size());}
    void jump(const std::string &s,int condition=-1){
        if(condition<0)b({0xe9});else b({0x0f,uint8_t(0x80+condition)});
        patches.push_back({bytes.size(),s});u32(0);
    }
    void load(unsigned reg,unsigned guest){if(reg>=8)b({0x44});b({0x8b,uint8_t(0x83+((reg&7)<<3))});u32(guest*4);}
    void store(unsigned reg,unsigned guest){if(reg>=8)b({0x44});b({0x89,uint8_t(0x83+((reg&7)<<3))});u32(guest*4);}
    void imm(unsigned reg,G value){if(reg>=8)b({0x41});b({uint8_t(0xb8+(reg&7))});u32(value);}
    void local_store(unsigned reg,uint8_t offset){if(reg>=8)b({0x44});b({0x89,uint8_t(0x44+((reg&7)<<3)),0x24,offset});}
    void local_load(unsigned reg,uint8_t offset){if(reg>=8)b({0x44});b({0x8b,uint8_t(0x44+((reg&7)<<3)),0x24,offset});}
    void helper(size_t offset){
        b({0x48,0x89,0xdf,0x4c,0x89,0xe6,0xe8});
        u32(G(int64_t(8+offset)-int64_t(bytes.size()+4)));
    }
    void entry_call(size_t offset){b({0x48,0x89,0xdf,0x4c,0x89,0xe6,0xe8});u32(G(int64_t(offset)-int64_t(bytes.size()+4)));}
    void slot_call(size_t offset){b({0x48,0x89,0xdf,0x4c,0x89,0xe6,0xff,0x15});u32(G(int64_t(offset)-int64_t(bytes.size()+4)));}
    void clear_metadata(){labels.clear();patches.clear();patches.shrink_to_fit();}
    void set(unsigned guest,G value){b({0xc7,0x83});u32(guest*4);u32(value);}
    void memimm(uint32_t offset,G value){b({0xc7,0x83});u32(offset);u32(value);}
    void result(G status){imm(0,status);jump("exit");}
    bool finish(){
        if(bytes.size()>max_image)return false;
        for(const auto &p:patches){auto it=labels.find(p.label);if(it==labels.end())return false;
            int64_t distance=int64_t(it->second)-int64_t(p.at+4);
            if(distance<std::numeric_limits<int32_t>::min()||distance>std::numeric_limits<int32_t>::max())return false;
            G value=G(distance);for(unsigned i=0;i<4;++i)bytes[p.at+i]=uint8_t(value>>(i*8));
        }return true;
    }
};
std::string label(G pc,bool thumb){return std::string(thumb?"t":"a")+std::to_string(pc);}
void flags(Emitter &e,bool arithmetic,bool subtract=false){
    // Capture host arithmetic flags before any instruction which changes them.
    if(arithmetic){e.b({0x0f,uint8_t(subtract?0x93:0x92),0xc2,0x40,0x0f,0x90,0xc6});}
    e.b({0x41,0x0f,0x94,0xc0,0x41,0x0f,0x98,0xc1});
    if(arithmetic){e.b({0x0f,0xb6,0xd2,0xc1,0xe2,29,0x40,0x0f,0xb6,0xf6,0xc1,0xe6,28,0x09,0xf2});}
    else e.b({0x31,0xd2});
    e.b({0x41,0x0f,0xb6,0xf0,0xc1,0xe6,30,0x09,0xf2,0x41,0x0f,0xb6,0xf1,0xc1,0xe6,31,0x09,0xf2});
    e.b({0x81,0xa3});e.u32(cpsr);e.u32(arithmetic?0x0fffffff:0x3fffffff);
    e.b({0x09,0x93});e.u32(cpsr);
}
void bit(Emitter &e,unsigned reg,unsigned position){
    e.b({0x8b,uint8_t(0x83+(reg<<3))});e.u32(cpsr);
    e.b({0xc1,uint8_t(0xe8+reg),uint8_t(position),0x83,uint8_t(0xe0+reg),1});
}
void condition(Emitter &e,unsigned c,const std::string &skip){
    if(c==14)return;
    if(c<8){bit(e,0,(c<2)?30:(c<4)?29:(c<6)?31:28);if(c&1)e.b({0x83,0xf0,1});}
    else if(c<10){bit(e,0,29);bit(e,1,30);e.b({0x83,0xf1,1,0x21,0xc8});if(c==9)e.b({0x83,0xf0,1});}
    else {bit(e,0,31);bit(e,1,28);e.b({0x31,0xc8});
        if(c!=11)e.b({0x83,0xf0,1});
        if(c>=12){bit(e,1,30);e.b({0x83,0xf1,1,0x21,0xc8});if(c==13)e.b({0x83,0xf0,1});}
    }
    e.b({0x85,0xc0});e.jump(skip,4);
}
void tick(Emitter &e,G amount){e.b({0x48,0x83,0x83});e.u32(ticks);e.b({uint8_t(amount)});}
void source(Emitter &e,unsigned native,unsigned guest,G pc,bool thumb){
    if(guest==15)e.imm(native,pc+(thumb?4:8));else e.load(native,guest);
}
// op is selected at preparation time. Generated code has no operation switch.
bool alu(Emitter &e,unsigned op,unsigned rd,bool set_flags,bool store_result){
    bool arithmetic=false,subtract=false;
    switch(op){
    case 0:e.b({0x21,0xc8});break; // AND eax,ecx
    case 1:e.b({0x31,0xc8});break;
    case 2:case 10:e.b({0x29,0xc8});arithmetic=true;subtract=true;break;
    case 3:e.b({0x91,0x29,0xc8});arithmetic=true;subtract=true;break;
    case 4:case 11:e.b({0x01,0xc8});arithmetic=true;break;
    case 5:case 6:case 7:
        if(op==7)e.b({0x91});
        if(op!=5)e.b({0xf7,0xd1});
        e.b({0x89,0xc2});e.load(8,cpsr/4);e.b({0x41,0xc1,0xe8,29,0x41,0x83,0xe0,1});
        e.imm(9,set_flags);e.helper(aot_add_offset);
        if(store_result)e.store(0,rd);return true;
    case 8:e.b({0x21,0xc8});break;
    case 9:e.b({0x31,0xc8});break;
    case 12:e.b({0x09,0xc8});break;
    case 13:e.b({0x89,0xc8});break;
    case 14:e.b({0xf7,0xd1,0x21,0xc8});break;
    case 15:e.b({0x89,0xc8,0xf7,0xd0});break;
    default:return false;
    }
    if(set_flags){if(!arithmetic)e.b({0x85,0xc0});flags(e,arithmetic,subtract);}
    if(store_result)e.store(0,rd);
    return true;
}
void access(Emitter &e,unsigned dest,bool load,unsigned width,bool signed_value=false){
    e.b({0x89,0x44,0x24,8,0x89,0xc2}); // preserve original address; edx=address
    if(width==4)e.b({0x83,0xe2,0xfc});
    if(width==2){
        if(load){e.b({0xa8,1});e.jump("memory_error",5);}
        e.b({0x83,0xe2,0xfe});
    }
    e.b({0x49,0x8b,0x7c,0x24,8,0x8b,0xb3});e.u32(offsetof(GuestCpu,core));e.imm(1,width);
    if(load)e.b({0x4c,0x8d,0x04,0x24});
    // Store source is supplied in r8d by the instruction-specific emitter.
    // Only existing hardware service callbacks. No ARM instruction is passed.
    e.b({0x41,0xff,0x54,0x24,uint8_t(load?offsetof(GuestHost,read):offsetof(GuestHost,write)),0x84,0xc0});
    e.jump("memory_error",4);
    if(load){e.b({0x8b,0x04,0x24});
        if(width==4)e.b({0x8b,0x4c,0x24,8,0x83,0xe1,3,0xc1,0xe1,3,0xd3,0xc8});
        else if(signed_value)e.b({0x0f,uint8_t(width==1?0xbe:0xbf),0xc0});
        else {e.b({0x25});e.u32(width==1?255:65535);}
        if(dest<16)e.store(0,dest);
    }
}
void memory(Emitter &e,unsigned base,unsigned dest,G offset,bool up,bool load,unsigned width,G pc,bool thumb){
    source(e,0,base,pc,thumb);e.b({up?uint8_t(0x05):uint8_t(0x2d)});e.u32(offset);
    if(!load)e.load(8,dest);
    access(e,dest,load,width);
}
void shift(Emitter &e,unsigned type,bool reg,bool sf){
    static constexpr size_t offsets[]={aot_lsl_offset,aot_lsr_offset,aot_asr_offset,aot_ror_offset};
    e.imm(8,reg);e.imm(9,sf);e.helper(offsets[type]);
}
void exchange(Emitter &e){
    e.b({0x89,0xc1,0x83,0xe1,1,0xc1,0xe1,5,0x83,0xa3});e.u32(cpsr);e.b({0xdf});
    e.b({0x09,0x8b});e.u32(cpsr);e.b({0x83,0xe0,0xfe,0x85,0xc9});
    auto aligned="exchange"+std::to_string(e.bytes.size());e.jump(aligned,5);e.b({0x83,0xe0,0xfc});e.mark(aligned);e.store(0,15);e.jump("dispatch");
}
void service(Emitter &e,G operation,unsigned reg,G pc,unsigned step,bool result_read=false){
    // reg==16 supplies immediate zero; otherwise ECX receives a selected source.
    if(reg<16)source(e,1,reg,pc,step==2);else e.imm(1,0);
    e.b({0x49,0x8b,0x7c,0x24,8,0x48,0x89,0xde});e.imm(2,operation);
    e.b({0xc7,0x04,0x24,0,0,0,0,0x4c,0x8d,0x04,0x24,0x41,0xff,0x54,0x24,uint8_t(offsetof(GuestHost,system)),0x84,0xc0});
    e.jump("system_error",4);
    if(result_read)return;
    auto done="service"+std::to_string(e.bytes.size());e.b({0x83,0x3c,0x24,G_YIELD});e.jump(done,5);
    e.set(15,pc+step);e.result(G_YIELD);e.mark(done);
}
void route_tree(Emitter &e,const std::vector<const Instruction*>&code,size_t begin,size_t end){
    if(begin==end){e.result(G_UNMAPPED);return;}
    size_t mid=begin+(end-begin)/2;const auto &i=*code[mid];
    e.b({0x81,0x7b,60});e.u32(i.pc);e.jump(label(i.pc,i.thumb),4);
    const auto left="tree"+std::to_string(e.bytes.size());e.jump(left,2);
    route_tree(e,code,mid+1,end);e.mark(left);route_tree(e,code,begin,mid);
}
bool emit_program(Emitter &e,const Program &p,Result &out){
    const size_t begin=e.bytes.size();
    // Names are local to each core; finish/clear fixups before appending a core.
    e.labels.clear();e.patches.clear();
    e.b({0x48,0x85,0xff});e.jump("bad_abi",4);e.b({0x48,0x85,0xf6});e.jump("bad_abi",4);
    e.b({0x83,0x3e,GUEST_ABI});e.jump("bad_abi",5);
    e.b({0x83,0x7e,4,uint8_t(sizeof(GuestHost))});e.jump("bad_abi",5);
    for(auto offset:{offsetof(GuestHost,read),offsetof(GuestHost,write),offsetof(GuestHost,system)}){
        e.b({0x48,0x83,0x7e,uint8_t(offset),0});e.jump("bad_abi",4);
    }
    e.b({0x83,0x7f,uint8_t(offsetof(GuestCpu,core)),uint8_t(p.core)});e.jump("bad_abi",5);
    e.b({0x53,0x41,0x54,0x48,0x83,0xec,72,0x48,0x89,0xfb,0x49,0x89,0xf4});
    e.mark("dispatch");
    std::vector<const Instruction*> routes[2];for(const auto&i:p.code)routes[i.thumb].push_back(&i);
    for(auto &r:routes)std::sort(r.begin(),r.end(),[](auto a,auto b){return a->pc<b->pc;});
    e.b({0xf6,0x83});e.u32(cpsr);e.b({32});e.jump("thumb_dispatch",5);
    route_tree(e,routes[0],0,routes[0].size());e.mark("thumb_dispatch");route_tree(e,routes[1],0,routes[1].size());
    std::set<std::pair<G,bool>> addresses;
    for(const auto &i:p.code)addresses.emplace(i.pc,i.thumb);
    auto route=[&](G pc,bool thumb){if(addresses.count({pc,thumb}))e.jump(label(pc,thumb));else {e.set(15,pc);e.result(G_UNMAPPED);}};
    for(size_t instruction_index=0;instruction_index<p.code.size();++instruction_index){
        const auto &i=p.code[instruction_index];
        out.fault_pc=i.pc;out.fault_word=i.word;
        const G w=i.word,pc=i.pc,factor=p.core==9?1:2;
        e.mark(label(pc,i.thumb));e.set(15,pc);
        e.b({0x83,0xbb});e.u32(budget);e.b({0});e.jump("yield",4);
        if(p.guarded){
            e.b({0x49,0x8b,0x7c,0x24,8,0x8b,0xb3});e.u32(offsetof(GuestCpu,core));e.imm(2,pc);e.imm(1,i.thumb?2:4);
            e.b({0xc7,0x04,0x24,0,0,0,0,0x4c,0x8d,0x04,0x24,0x41,0xff,0x54,0x24,uint8_t(offsetof(GuestHost,read)),0x84,0xc0});e.jump("unmapped",4);
            e.b({0x81,0x3c,0x24});e.u32(w);e.jump("unmapped",5);
        }
        e.b({0xff,0x8b});e.u32(budget);tick(e,factor);
        const std::string next="next"+label(pc,i.thumb);
        bool terminal=false;
        if(!i.thumb){
            unsigned c=w>>28;if(c>14){
                if(p.core!=9||(w&0x0e000000)!=0x0a000000){out.error="A32 extension unsupported";return false;}
                int64_t d=int64_t((w&0xffffff)*4)|((w>>23)&2);if(d&0x2000000)d-=0x4000000;
                e.set(14,pc+4);e.b({0x83,0x8b});e.u32(cpsr);e.b({32});tick(e,2*factor);route(pc+8+G(d),true);continue;
            }
            condition(e,c,next);
            unsigned rn=(w>>16)&15,rd=(w>>12)&15;
            if((w&0x0f000000)==0x0f000000){
                service(e,0x03000000u|(w&0xffffff),16,pc,4);
            }else if((w&0x0f000010)==0x0e000010){
                if(p.core!=9||((w>>8)&15)!=15){out.error="Unsupported coprocessor";return false;}
                G key=((w>>16)&15)<<12|(w&15)<<8|((w>>21)&7)<<4|((w>>5)&7);bool read=w&0x100000;
                service(e,(read?0x10000u:0x20000u)|key,read?16:rd,pc,4,read);
                if(read){e.local_load(0,0);if(rd!=15)e.store(0,rd);
                    else {e.b({0x25});e.u32(0xf0000000);e.b({0x81,0xa3});e.u32(cpsr);e.u32(0x0fffffff);e.b({0x09,0x83});e.u32(cpsr);}
                }
            }else if((w&0x0fbf0fff)==0x010f0000){
                if(rd==15){out.error="MRS pc unsupported";return false;}
                if(w&0x400000){e.imm(2,rd);e.helper(aot_mrs_saved_offset);e.b({0x85,0xc0});e.jump("unsupported",5);}
                else {e.load(0,cpsr/4);e.store(0,rd);}
            }else if((w&0x0fb0fff0)==0x0120f000||(w&0x0fb0f000)==0x0320f000){
                if(w&0x02000000){G rot=((w>>8)&15)*2,value=w&255;if(rot)value=(value>>rot)|(value<<(32-rot));e.imm(2,value);}
                else source(e,2,w&15,pc,false);
                G mask=0;for(unsigned byte=0;byte<4;++byte)if(w&(1u<<(16+byte)))mask|=255u<<(byte*8);
                e.imm(1,mask);e.imm(8,bool(w&0x400000));e.helper(aot_psr_offset);e.b({0x85,0xc0});e.jump("unsupported",5);
            }else if((w&0x0fff0ff0)==0x016f0f10){
                if(p.core!=9||rd==15||(w&15)==15){out.error="Invalid CLZ";return false;}
                source(e,1,w&15,pc,false);e.imm(0,32);e.b({0x85,0xc9});auto done="clz"+next;e.jump(done,4);
                e.b({0x0f,0xbd,0xc1,0x83,0xf0,31});e.mark(done);e.store(0,rd);
            }else if((w&0x0ff00090)==0x01000080||(w&0x0ff00090)==0x01200080||(w&0x0ff00090)==0x01400080||(w&0x0ff00090)==0x01600080){
                unsigned kind=w&0x0ff00090,dest=(w>>16)&15,acc=(w>>12)&15,rs=(w>>8)&15,rm=w&15;
                bool word_mul=kind==0x01200080,mul_only=kind==0x01600080||(word_mul&&(w&32));
                if(p.core!=9||dest==15||acc==15||rs==15||rm==15||(mul_only&&acc)||(kind==0x01400080&&dest==acc)){
                    out.error="Invalid signed DSP multiply";return false;
                }
                e.load(0,rm);e.load(1,rs);
                if(w&64)e.b({0xc1,0xe9,16});e.b({0x0f,0xbf,0xc9});
                if(word_mul){e.b({0x89,0xc2});e.imm(8,dest);e.imm(9,acc);e.helper(mul_only?aot_smulw_offset:aot_smlaw_offset);}
                else {
                    if(w&32)e.b({0xc1,0xe8,16});e.b({0x0f,0xbf,0xc0});
                    if(kind==0x01400080){e.b({0x89,0xc2});e.imm(8,dest);e.imm(9,acc);e.helper(aot_smlal_offset);}
                    else {
                        e.b({0x0f,0xaf,0xc1});
                        if(!mul_only){e.load(1,acc);e.b({0x01,0xc8});auto done="dsp"+next;e.jump(done,1);
                            e.b({0x81,0x8b});e.u32(cpsr);e.u32(0x08000000);e.mark(done);
                        }e.store(0,dest);
                    }
                }
            }else if((w&0x0fc000f0)==0x00000090){
                unsigned dest=(w>>16)&15,acc=(w>>12)&15,rs=(w>>8)&15,rm=w&15;
                if(dest==15||acc==15||rs==15||rm==15){out.error="Invalid multiply registers";return false;}
                e.load(0,rm);e.load(1,rs);e.b({0x0f,0xaf,0xc1});
                if(w&0x200000){e.load(1,acc);e.b({0x01,0xc8});}
                if(w&0x100000){e.b({0x85,0xc0});flags(e,false);}e.store(0,dest);tick(e,(w&0x100000?2:1)*factor);
            }else if((w&0x0f8000f0)==0x00800090){
                unsigned hi=(w>>16)&15,lo=(w>>12)&15,rs=(w>>8)&15,rm=w&15;
                if(hi==15||lo==15||rs==15||rm==15||hi==lo){out.error="Invalid long multiply registers";return false;}
                e.load(2,rm);e.load(1,rs);e.imm(8,hi);e.imm(9,lo);
                e.helper(w&0x400000?(w&0x200000?aot_smlal_offset:aot_smull_offset):(w&0x200000?aot_umlal_offset:aot_umull_offset));
                if(w&0x100000){e.load(0,hi);e.b({0x48,0xc1,0xe0,32});e.load(1,lo);e.b({0x48,0x09,0xc8,0x48,0x85,0xc0});flags(e,false);}
                tick(e,(w&0x100000?2:1)*factor);
            }else if((w&0x0fb00ff0)==0x01000090){
                unsigned rm=w&15;if(rn==15||rd==15||rm==15){out.error="Invalid SWP registers";return false;}
                unsigned width=w&0x400000?1:4;e.load(0,rn);e.local_store(0,48);e.load(0,rm);e.local_store(0,56);
                e.local_load(0,48);access(e,16,true,width);e.local_store(0,32);
                e.local_load(0,48);e.local_load(8,56);access(e,16,false,width);e.local_load(0,32);e.store(0,rd);tick(e,factor);
            }else if((w&0x0e000000)==0x08000000){
                bool load=w&0x100000,up=w&0x800000,before=w&0x1000000,wb=w&0x200000;
                std::vector<unsigned> regs;for(unsigned reg=0;reg<16;++reg)if(w&(1u<<reg))regs.push_back(reg);
                if(rn==15||regs.empty()||(wb&&(w&(1u<<rn)))){out.error="Unsupported A32 block transfer list";return false;}
                bool restore=(w&0x400000)&&load&&regs.back()==15,user=(w&0x400000)&&!restore;
                G offset=up?G(before?4:0):G(-4*int(regs.size()-unsigned(!before)));
                e.load(0,rn);e.local_store(0,16);e.b({uint8_t(up?0x05:0x2d)});e.u32(regs.size()*4);e.local_store(0,24);
                tick(e,(regs.size()+1)*factor);
                for(unsigned at=0;at<regs.size();++at){unsigned dest=regs[at];e.local_load(0,16);e.b({0x05});e.u32(offset+at*4);
                    if(!load){
                        if(user&&dest!=15){e.local_store(0,48);e.imm(2,dest);e.helper(aot_get_user_offset);e.b({0x41,0x89,0xc0});e.local_load(0,48);}
                        else if(dest==15)e.imm(8,pc+12);else e.load(8,dest);
                    }
                    access(e,load&&(user||dest==15)?16:dest,load,4);
                    if(load){
                        if(dest==15)e.local_store(0,32);
                        else if(user){e.b({0x89,0xc1});e.imm(2,dest);e.helper(aot_set_user_offset);}
                    }
                }
                if(wb){e.local_load(0,24);e.store(0,rn);}
                if(load&&regs.back()==15){
                    e.local_load(0,32);
                    if(restore){e.b({0x89,0xc2});e.helper(aot_restore_offset);e.b({0x85,0xc0});e.jump("unsupported",5);e.jump("dispatch");}
                    else if(p.core==9)exchange(e);else {e.b({0x83,0xe0,0xfc});e.store(0,15);e.jump("dispatch");}
                    terminal=true;
                }
            }else if((w&0x0e000000)==0x0a000000){
                int64_t d=w&0xffffff;if(d&0x800000)d-=0x1000000;
                if(w&0x1000000)e.set(14,pc+4);
                tick(e,2*factor);route(pc+8+G(d*4),false);terminal=true;
            }else if((w&0x0ffffff0)==0x012fff10||(w&0x0ffffff0)==0x012fff30){
                unsigned rm=w&15;if(rm==15){out.error="BX pc unsupported";return false;}
                if((w&32)&&p.core!=9){out.error="ARM7 BLX unsupported";return false;}
                if(w&32)e.set(14,pc+4);
                e.load(0,rm);e.b({0x89,0xc1,0x83,0xe1,1,0xc1,0xe1,5,0x83,0xa3});e.u32(cpsr);e.b({0xdf});
                e.b({0x09,0x8b});e.u32(cpsr);
                // ARM alignment: clear bit1 only when target mode is A32.
                e.b({0x83,0xe0,0xfe,0x85,0xc9});std::string aligned="bx"+next;e.jump(aligned,5);
                e.b({0x83,0xe0,0xfc});e.mark(aligned);e.store(0,15);tick(e,2*factor);e.jump("dispatch");terminal=true;
            }else if((w&0x0c000000)==0x04000000||((w&0x0e000090)==0x00000090&&((w>>5)&3))){
                bool single=(w&0x0c000000)==0x04000000,load=w&0x100000,up=w&0x800000,before=w&0x1000000,wb=!before||w&0x200000;
                if((rn==15&&wb)||(load&&rn==rd&&wb)){out.error="Invalid transfer writeback";return false;}
                bool sign=false;unsigned width=4;
                source(e,0,rn,pc,false);e.local_store(0,16);
                if(single){width=w&0x400000?1:4;
                    if(w&0x2000000){if(w&16){out.error="Invalid shifted memory offset";return false;}
                        source(e,2,w&15,pc,false);e.imm(1,(w>>7)&31);shift(e,(w>>5)&3,false,false);e.b({0x89,0xc1});
                    }else e.imm(1,w&4095);
                }else {unsigned kind=(w>>5)&3;
                    if(!load&&kind!=1){out.error="Doubleword transfer unsupported";return false;}
                    sign=kind!=1;width=kind==2?1:2;
                    if(w&0x400000)e.imm(1,((w>>4)&0xf0)|(w&15));else source(e,1,w&15,pc,false);
                }
                if(rd==15&&width!=4){out.error="Invalid narrow PC transfer";return false;}
                e.local_load(0,16);e.b({uint8_t(up?0x01:0x29),0xc8});e.local_store(0,24);
                if(!before)e.local_load(0,16);
                if(!load){if(rd==15)e.imm(8,pc+12);else e.load(8,rd);}
                tick(e,(load?2:1)*factor);access(e,rd==15?16:rd,load,width,sign);
                if(load&&rd==15)e.local_store(0,32);
                if(wb){e.local_load(0,24);e.store(0,rn);}
                if(load&&rd==15){e.local_load(0,32);if(p.core==9)exchange(e);else {e.b({0x83,0xe0,0xfc});e.store(0,15);e.jump("dispatch");}terminal=true;}
            }else if((w&0x0c000000)==0 && ((w&0x02000000)||(w&0x90)!=0x90)){
                unsigned op=(w>>21)&15;bool sf=w&0x100000,write=op<8||op>11;
                if(op>=8&&op<=11&&!sf){out.error="A32 flag form unsupported";return false;}
                bool regshift=!(w&0x02000000)&&(w&16);
                if(regshift&&((w>>8)&15)==15){out.error="Invalid shift count register";return false;}
                if(rn==15&&regshift)e.imm(0,pc+12);else source(e,0,rn,pc,false);
                G value=0,rotation=0;
                if(w&0x02000000){rotation=((w>>8)&15)*2;value=w&255;
                    if(rotation)value=(value>>rotation)|(value<<(32-rotation));e.imm(1,value);
                }else if(w&0xff0){
                    e.local_store(0,16);
                    if((w&15)==15&&regshift)e.imm(2,pc+12);else source(e,2,w&15,pc,false);
                    if(regshift){e.load(1,(w>>8)&15);e.b({0x81,0xe1});e.u32(255);if(!write||rd!=15)tick(e,factor);}
                    else e.imm(1,(w>>7)&31);
                    bool logical=op==0||op==1||op==8||op==9||op>=12;
                    shift(e,(w>>5)&3,regshift,sf&&logical);e.b({0x89,0xc1});e.local_load(0,16);
                }else source(e,1,w&15,pc,false);
                if(!alu(e,op,rd,sf,write)){out.error="A32 arithmetic operation unsupported";return false;}
                if(sf&&rotation&&(op==0||op==1||op==8||op==9||op>=12)){
                    e.b({0x81,0xa3});e.u32(cpsr);e.u32(~G(1u<<29));
                    if(value&0x80000000){e.b({0x81,0x8b});e.u32(cpsr);e.u32(1u<<29);}
                }
                if(write&&rd==15){
                    tick(e,2*factor);
                    if(sf){e.b({0x89,0xc2});e.helper(aot_restore_offset);e.b({0x85,0xc0});e.jump("unsupported",5);}
                    else {e.b({0x83,0xe0,0xfc});e.store(0,15);}
                    e.jump("dispatch");terminal=true;
                }
            }else {out.error="A32 instruction unsupported";return false;}
        }else {
            unsigned rd=(w>>8)&7;
            if((w&0xf800)<=0x1000){
                unsigned dest=w&7,rs=(w>>3)&7;e.load(2,rs);e.imm(1,(w>>6)&31);shift(e,(w>>11)&3,false,true);
                e.b({0x85,0xc0});flags(e,false);e.store(0,dest);
            }else if((w&0xe000)==0x2000){
                unsigned op=(w>>11)&3;e.load(0,rd);e.imm(1,w&255);
                if(!alu(e,op==0?13:op==2?4:2,rd,true,op!=1))return false;
            }else if((w&0xf800)==0x1800){
                unsigned dest=w&7,rs=(w>>3)&7;e.load(0,rs);
                if(w&0x400)e.imm(1,(w>>6)&7);else e.load(1,(w>>6)&7);
                alu(e,w&0x200?2:4,dest,true,true);
            }else if((w&0xfc00)==0x4000){
                unsigned op=(w>>6)&15,dest=w&7,rs=(w>>3)&7;
                if(op==2||op==3||op==4||op==7){
                    e.load(2,dest);e.load(1,rs);e.b({0x81,0xe1});e.u32(255);shift(e,op==2?0:op==3?1:op==4?2:3,true,true);
                    e.b({0x85,0xc0});flags(e,false);e.store(0,dest);
                }else {
                    e.load(0,dest);e.load(1,rs);
                    if(op==9)e.imm(0,0);
                    if(op==13){e.b({0x0f,0xaf,0xc1,0x85,0xc0});flags(e,false);e.store(0,dest);}
                    else {
                        static constexpr unsigned mapping[]={0,1,0,0,0,5,6,0,8,2,10,11,12,0,14,15};
                        if(!alu(e,mapping[op],dest,true,op!=8&&op!=10&&op!=11))return false;
                    }
                }
            }else if((w&0xfc00)==0x4400){
                unsigned op=(w>>8)&3,dest=(w&7)|((w>>4)&8),rs=(w>>3)&15;
                if(op==3){
                    if((w&7)||((w&128)&&p.core!=9)){out.error="Invalid Thumb BX/BLX";return false;}
                    source(e,0,rs,pc,true);if(w&128)e.set(14,(pc+2)|1);
                    tick(e,2*factor);exchange(e);terminal=true;
                }else {
                    source(e,0,dest,pc,true);source(e,1,rs,pc,true);
                    alu(e,op==0?4:op==1?10:13,dest,op==1,op!=1);
                    if(dest==15&&op!=1){e.b({0x83,0xe0,0xfe});e.store(0,15);tick(e,2*factor);e.jump("dispatch");terminal=true;}
                }
            }else if((w&0xf800)==0x4800){
                e.imm(0,((pc+4)&~3u)+(w&255)*4);tick(e,2*factor);access(e,rd,true,4);
            }else if((w&0xf000)==0x5000){
                unsigned op=(w>>9)&7,dest=w&7,rs=(w>>3)&7,offset=(w>>6)&7;
                static constexpr unsigned widths[]={4,2,1,1,4,2,1,2};
                e.load(0,rs);e.load(1,offset);e.b({0x01,0xc8});bool load=op>=3;if(!load)e.load(8,dest);
                tick(e,2*factor);access(e,dest,load,widths[op],op==3||op==7);
            }else if((w&0xe000)==0x6000){
                unsigned dest=w&7,rs=(w>>3)&7,width=w&0x1000?1:4;
                tick(e,2*factor);memory(e,rs,dest,((w>>6)&31)*width,true,w&0x800,width,pc,true);
            }else if((w&0xf000)==0x8000){
                tick(e,2*factor);memory(e,(w>>3)&7,w&7,((w>>6)&31)*2,true,w&0x800,2,pc,true);
            }else if((w&0xf000)==0x9000){
                tick(e,2*factor);memory(e,13,rd,(w&255)*4,true,w&0x800,4,pc,true);
            }else if((w&0xf000)==0xa000){
                if(w&0x800)e.load(0,13);else e.imm(0,(pc+4)&~3u);
                e.b({0x05});e.u32((w&255)*4);e.store(0,rd);
            }else if((w&0xff00)==0xb000){
                e.load(0,13);e.b({uint8_t(w&0x80?0x2d:0x05)});e.u32((w&127)*4);e.store(0,13);
            }else if((w&0xf600)==0xb400 || (w&0xf000)==0xc000){
                bool stack=(w&0xf600)==0xb400,load=w&0x800;unsigned base=stack?13:(w>>8)&7;
                std::vector<unsigned> regs;for(unsigned n=0;n<8;++n)if(w&(1u<<n))regs.push_back(n);
                if(stack&&(w&0x100))regs.push_back(load?15:14);
                if(regs.empty()||(!stack&&!load&&(w&(1u<<base))&&base!=regs.front())){out.error="Unsupported Thumb block list";return false;}
                tick(e,(regs.size()+1)*factor);e.load(0,base);
                if(stack&&!load){e.b({0x2d});e.u32(regs.size()*4);}
                e.local_store(0,16);
                for(unsigned at=0;at<regs.size();++at){unsigned dest=regs[at];e.local_load(0,16);e.b({0x05});e.u32(at*4);
                    if(!load)e.load(8,dest);access(e,dest==15?16:dest,load,4);
                    if(load&&dest==15)e.local_store(0,32);
                }
                if(stack||!load||!(w&(1u<<base))){
                    e.local_load(0,16);if(load||!stack){e.b({0x05});e.u32(regs.size()*4);}e.store(0,base);
                }
                if(load&&regs.back()==15){e.local_load(0,32);if(p.core==9)exchange(e);else {e.b({0x83,0xe0,0xfe});e.store(0,15);e.jump("dispatch");}terminal=true;}
            }else if((w&0xf800)==0xe000){
                int32_t d=w&2047;if(d&1024)d-=2048;
                tick(e,2*factor);route(pc+4+G(d*2),true);terminal=true;
            }else if((w&0xf000)==0xd000 && ((w>>8)&15)<14){
                int32_t d=int8_t(w&255);tick(e,2*factor);condition(e,(w>>8)&15,next);
                route(pc+4+G(d*2),true);terminal=true;
            }else if((w&0xff00)==0xdf00){
                service(e,0x03000000u|(w&255),16,pc,2);
            }else if((w&0xf800)==0xf000){
                int32_t d=w&2047;if(d&1024)d-=2048;e.set(14,pc+4+G(d*4096));
            }else if((w&0xf800)==0xf800||(w&0xf800)==0xe800){
                bool x=(w&0xf800)==0xe800;
                if(x&&(p.core!=9||(w&1))){out.error="Invalid Thumb BLX suffix";return false;}
                e.load(0,14);e.b({0x05});e.u32((w&2047)*2);e.set(14,(pc+2)|1);tick(e,2*factor);
                if(x){e.b({0x83,0xe0,0xfc});exchange(e);}else {e.b({0x83,0xe0,0xfe});e.store(0,15);e.jump("dispatch");}terminal=true;
            }else {out.error="Thumb instruction unsupported";return false;}
        }
        e.mark(next);
        if(!terminal||(!i.thumb&&(w>>28)!=14)||(i.thumb&&(w&0xf000)==0xd000)){
            G next_pc=pc+(i.thumb?2:4);
            bool falls=instruction_index+1<p.code.size()&&p.code[instruction_index+1].pc==next_pc&&p.code[instruction_index+1].thumb==i.thumb;
            if(!falls)route(next_pc,i.thumb);
        }
        if(e.bytes.size()-begin>max_image){out.error="Native image exceeds bound";return false;}
    }
    e.mark("memory_error");e.b({0x8b,0x44,0x24,8});e.store(0,18); // fault is byte offset72
    e.result(G_MEMORY);
    e.mark("yield");e.result(G_YIELD);
    e.mark("unmapped");e.result(G_UNMAPPED);
    e.mark("unsupported");e.result(G_UNSUPPORTED);
    e.mark("system_error");e.result(G_SYSTEM);
    e.mark("exit");e.b({0x48,0x83,0xc4,72,0x41,0x5c,0x5b,0xc3});
    e.mark("bad_abi");e.imm(0,G_BAD_ABI);e.b({0xc3});
    if(!e.finish()){out.error="Native branch fixup failed";return false;}return true;
}
void put32(std::vector<uint8_t>&v,size_t at,G n){for(unsigned i=0;i<4;++i)v[at+i]=uint8_t(n>>(i*8));}
void put64(std::vector<uint8_t>&v,size_t at,uint64_t n){for(unsigned i=0;i<8;++i)v[at+i]=uint8_t(n>>(i*8));}
bool validate(const Program &p,G core,Result &out,bool require_entry=true){
    if(p.core!=core||p.entry&3||p.code.empty()||p.code.size()>max_instructions){out.error="Invalid core/entry/instruction count";return false;}
    std::set<std::pair<G,bool>> seen;bool entry=false;
    for(const auto &i:p.code){out.fault_pc=i.pc;out.fault_word=i.word;
        if((i.pc&(i.thumb?1:3))||i.pc>0xfffffffbu||i.thumb&&i.word>65535||!seen.emplace(i.pc,i.thumb).second){out.error="Invalid/duplicate instruction address";return false;}
        entry|=i.pc==p.entry;
    }
    if(!entry&&require_entry){out.error="Entry not present in selected code";return false;}return true;
}
void frame(Emitter&e){e.b({0x53,0x41,0x54,0x48,0x83,0xec,72,0x48,0x89,0xfb,0x49,0x89,0xf4});}
void leave(Emitter&e){e.b({0x48,0x83,0xc4,72,0x41,0x5c,0x5b,0xc3});}
bool emit_states(Emitter&e,size_t slots){
    e.clear_metadata();frame(e);e.load(0,budget/4);e.local_store(0,16);
    for(unsigned n=33;n>0;--n){e.slot_call(slots+(n-1)*8);e.b({0x83,0xf8,G_UNMAPPED});e.jump("exit",5);
        e.load(1,budget/4);e.b({0x3b,0x4c,0x24,16});e.jump("exit",5);
    }
    e.imm(0,G_UNMAPPED);e.mark("exit");leave(e);return e.finish();
}
bool emit_router(Emitter&e,G core,size_t main,const std::vector<std::pair<std::pair<G,G>,size_t>>&overlays){
    e.clear_metadata();e.b({0x48,0x85,0xff});e.jump("bad",4);e.b({0x48,0x85,0xf6});e.jump("bad",4);
    frame(e);e.load(0,budget/4);e.local_store(0,16);e.entry_call(main);e.b({0x83,0xf8,G_UNMAPPED});e.jump("exit",5);
    e.load(1,budget/4);e.b({0x3b,0x4c,0x24,16});e.jump("exit",5);
    e.b({0x49,0x8b,0x7c,0x24,8,0x48,0x89,0xde});e.imm(2,0x04000000);e.load(1,15);
    e.b({0x4c,0x8d,0x04,0x24,0x41,0xff,0x54,0x24,uint8_t(offsetof(GuestHost,system)),0x84,0xc0});e.jump("absent",4);
    for(const auto&record:overlays)if(record.first.first==core){e.b({0x81,0x3c,0x24});e.u32(record.first.second);auto next="ov"+std::to_string(e.bytes.size());e.jump(next,5);e.entry_call(record.second);e.jump("exit");e.mark(next);}
    e.mark("absent");e.imm(0,G_UNMAPPED);e.mark("exit");leave(e);e.mark("bad");e.imm(0,G_BAD_ABI);e.b({0xc3});return e.finish();
}
void module_digest(const std::vector<uint8_t>&data,uint8_t digest[32]){
    uint8_t zero[32]{};OqSha256Stream h;h.update(data.data(),128);h.update(zero,32);h.update(data.data()+160,data.size()-160);h.digest(digest);
}
uint64_t get64(const std::vector<uint8_t>&v,size_t at){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(v[at+i])<<(i*8);return n;}
}
bool compile_game(const Program &a9,const Program &a7,const GameIdentity &identity,const std::vector<OverlayProgram>&overlays,Result &out){
    out={};if(!validate(a9,9,out)||!validate(a7,7,out))return false;
    std::set<std::pair<G,G>> ids;
    if(overlays.size()>256){out.error="Overlay catalog exceeds bound";return false;}
    for(const auto&o:overlays){
        if(!ids.emplace(o.core,o.id).second||o.id>=4096||o.base<0x02000000||o.base>=0x02400000||!o.bytes||o.bytes>0x02400000-o.base||o.states.empty()||o.states.size()>33){out.error="Invalid overlay catalog entry";return false;}
        for(const auto&p:o.states){
            if(p.core!=o.core){out.error="Invalid overlay state: core differs, overlay="+std::to_string(o.id);return false;}
            if(!validate(p,o.core,out,false)){out.error="Invalid overlay state: overlay="+std::to_string(o.id)+": "+out.error;return false;}
            if(o.states.size()>1&&!p.guarded){out.error="Invalid overlay state: missing guard, overlay="+std::to_string(o.id);return false;}
            for(const auto&i:p.code)if(i.pc<o.base||uint64_t(i.pc)-o.base+(i.thumb?2:4)>o.bytes){out.error="Overlay code outside identity extent";return false;}
        }
    }
    // Keep the8bytes before the first entry readable. Clang's indirect-function
    // sanitizer probes that prefix even for externally generated machine code.
    Emitter e;e.bytes.resize(8,0);e.bytes.insert(e.bytes.end(),std::begin(aot_kernel_image),std::end(aot_kernel_image));
    size_t entry9=e.bytes.size();if(!emit_program(e,a9,out))return false;e.clear_metadata();
    size_t entry7=e.bytes.size();if(!emit_program(e,a7,out))return false;e.clear_metadata();
    out.instructions=a9.code.size()+a7.code.size();
    std::vector<std::pair<std::pair<G,G>,size_t>> routes;
    std::vector<std::vector<std::pair<G,G>>> ranges;
    std::vector<std::pair<uint64_t,uint64_t>> relocations;
    for(const auto&o:overlays){std::vector<size_t> states;std::vector<std::pair<G,G>> spans;
        for(const auto&p:o.states){states.push_back(e.bytes.size());if(!emit_program(e,p,out))return false;e.clear_metadata();out.instructions+=p.code.size();
            for(const auto&i:p.code)spans.push_back({i.pc,i.pc+(i.thumb?2:4)});
        }
        size_t route=states.front();
        if(o.states.front().guarded){
            size_t absent=e.bytes.size();e.imm(0,G_UNMAPPED);e.b({0xc3});e.bytes.resize((e.bytes.size()+7)&~size_t(7),0);
            size_t slots=e.bytes.size();e.bytes.resize(e.bytes.size()+33*8,0);
            for(unsigned slot=0;slot<33;++slot)relocations.push_back({slots+slot*8,slot<states.size()?states[slot]:absent});
            route=e.bytes.size();if(!emit_states(e,slots)){out.error="Overlay state fixup failed";return false;}e.clear_metadata();
            out.extensions.push_back({o.core,o.id,o.base,o.bytes,G(states.size()),slots,0,{}});
        }
        routes.push_back({{o.core,o.id},route});std::sort(spans.begin(),spans.end());std::vector<std::pair<G,G>> merged;
        for(auto span:spans){if(!merged.empty()&&span.first<=merged.back().second)merged.back().second=std::max(merged.back().second,span.second);else merged.push_back(span);}
        ranges.push_back(std::move(merged));
    }
    if(!overlays.empty()){
        size_t root9=e.bytes.size();if(!emit_router(e,9,entry9,routes)){out.error="ARM9 overlay router fixup failed";return false;}e.clear_metadata();entry9=root9;
        size_t root7=e.bytes.size();if(!emit_router(e,7,entry7,routes)){out.error="ARM7 overlay router fixup failed";return false;}e.clear_metadata();entry7=root7;
    }
    e.bytes.resize((e.bytes.size()+7)&~size_t(7),0);size_t catalog_table=e.bytes.size();e.bytes.resize(e.bytes.size()+overlays.size()*64,0);
    for(size_t n=0;n<overlays.size();++n){const auto&o=overlays[n];size_t at=catalog_table+n*64;
        put32(e.bytes,at,o.core);put32(e.bytes,at+4,o.id);put32(e.bytes,at+8,o.base);put32(e.bytes,at+12,o.bytes);memcpy(e.bytes.data()+at+16,o.sha256,32);
        relocations.push_back({at+48,e.bytes.size()});put32(e.bytes,at+56,ranges[n].size());put32(e.bytes,at+60,o.states.front().guarded?1:0);
        for(auto r:ranges[n]){e.u32(r.first);e.u32(r.second);}
        for(auto &extension:out.extensions)if(extension.core==o.core&&extension.id==o.id){extension.catalog_record=at;for(auto r:ranges[n])extension.ranges.push_back({r.first,r.second});}
    }
    size_t catalog=e.bytes.size();e.b({0xc7,0x07});e.u32(overlays.size());
    if(overlays.empty())e.b({0x31,0xc0});else {e.b({0x48,0x8d,0x05});e.u32(G(int64_t(catalog_table)-int64_t(e.bytes.size()+4)));}e.b({0xc3});
    if(e.bytes.size()>max_image){out.error="Native image exceeds bound";return false;}
    std::vector<uint8_t> data(256+e.bytes.size()+relocations.size()*16,0);memcpy(data.data(),"OQGAME03",8);
    put32(data,8,3);put32(data,12,62);put32(data,16,256);put32(data,20,1);
    put64(data,24,e.bytes.size());put64(data,32,relocations.size());put64(data,40,entry9);put64(data,48,entry7);put64(data,56,catalog);
    put32(data,64,a9.entry);put32(data,68,a7.entry);put32(data,72,GUEST_ABI);
    put32(data,76,sizeof(GuestCpu));put32(data,80,sizeof(GuestHost));
    memcpy(data.data()+88,identity.game_code,4);memcpy(data.data()+92,identity.maker_code,2);
    data[94]=identity.revision;memcpy(data.data()+96,identity.sha256,32);
    std::sort(relocations.begin(),relocations.end());
    memcpy(data.data()+256,e.bytes.data(),e.bytes.size());size_t at=256+e.bytes.size();for(auto r:relocations){put64(data,at,r.first);put64(data,at+8,r.second);at+=16;}
    module_digest(data,data.data()+128);
    out.module=std::move(data);out.fault_pc=out.fault_word=0;return true;
}
bool compile(const Program&a9,const Program&a7,const GameIdentity&id,Result&out){return compile_game(a9,a7,id,{},out);}
bool append_guarded_state(Result&out,G core,G overlay,const Program&p){
    auto site=std::find_if(out.extensions.begin(),out.extensions.end(),[&](const auto&e){return e.core==core&&e.id==overlay;});
    if(site==out.extensions.end()||site->used>=33||p.core!=core||!p.guarded){out.error="Unsupported preparation extension";return false;}
    Result check;if(!validate(p,core,check,false)){out.error=check.error;out.fault_pc=check.fault_pc;out.fault_word=check.fault_word;return false;}
    for(const auto&i:p.code)if(i.pc<site->base||uint64_t(i.pc)-site->base+(i.thumb?2:4)>site->bytes){out.error="New state outside verified overlay extent";return false;}
    if(out.module.size()<256||memcmp(out.module.data(),"OQGAME03",8)){out.error="Missing compiled preparation image";return false;}
    uint64_t length=get64(out.module,24),count=get64(out.module,32);
    if(length>max_image||count>1048576||out.module.size()!=256+length+count*16||site->slots+33*8>length||site->catalog_record+64>length){out.error="Invalid preparation extension envelope";return false;}
    uint8_t digest[32];module_digest(out.module,digest);if(memcmp(digest,out.module.data()+128,32)){out.error="Preparation image changed before extension";return false;}
    Emitter e;e.bytes.assign(out.module.begin()+256,out.module.begin()+256+length);size_t entry=e.bytes.size();
    if(!emit_program(e,p,check)){out.error=check.error;out.fault_pc=check.fault_pc;out.fault_word=check.fault_word;return false;}e.clear_metadata();
    std::vector<NativeCodeRange> spans=site->ranges;for(const auto&i:p.code)spans.push_back({i.pc,i.pc+(i.thumb?2:4)});
    std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.begin<b.begin;});std::vector<NativeCodeRange> merged;
    for(auto span:spans){if(!merged.empty()&&span.begin<=merged.back().end)merged.back().end=std::max(merged.back().end,span.end);else merged.push_back(span);}
    e.bytes.resize((e.bytes.size()+7)&~size_t(7),0);size_t new_ranges=e.bytes.size();for(auto r:merged){e.u32(r.begin);e.u32(r.end);}put32(e.bytes,site->catalog_record+56,merged.size());
    if(e.bytes.size()>max_image){out.error="Extended native image exceeds bound";return false;}
    std::vector<std::pair<uint64_t,uint64_t>> relocs;bool changed_slot=false,changed_ranges=false;
    for(uint64_t n=0;n<count;++n){size_t at=256+size_t(length)+size_t(n)*16;uint64_t target=get64(out.module,at),addend=get64(out.module,at+8);
        if(target==site->slots+site->used*8){addend=entry;changed_slot=true;}if(target==site->catalog_record+48){addend=new_ranges;changed_ranges=true;}relocs.push_back({target,addend});
    }
    if(!changed_slot||!changed_ranges){out.error="Preparation relocation site missing";return false;}
    std::vector<uint8_t> data(out.module.begin(),out.module.begin()+256);put64(data,24,e.bytes.size());data.insert(data.end(),e.bytes.begin(),e.bytes.end());
    size_t footer=data.size();data.resize(footer+count*16);for(auto r:relocs){put64(data,footer,r.first);put64(data,footer+8,r.second);footer+=16;}
    module_digest(data,data.data()+128);out.module.swap(data);++site->used;site->ranges=std::move(merged);out.instructions+=p.code.size();out.error.clear();out.fault_pc=out.fault_word=0;return true;
}
bool supports(const Instruction &i,G core,std::string &error){
    Result result;
    if((core!=7&&core!=9)||(i.pc&(i.thumb?1:3))||i.pc>0xfffffffbu||(i.thumb&&i.word>65535)){
        error="Invalid instruction selection";return false;
    }
    Emitter e;bool ok=emit_program(e,Program{core,i.pc,{i}},result);error=result.error;return ok;
}
}
