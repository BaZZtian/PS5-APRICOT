#include "rom_prepare.h"
#include "sha256_stream.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace apricot::aot {
namespace {
constexpr uint64_t code_limit=16u*1024*1024;
G r32(const uint8_t *p){return G(p[0])|G(p[1])<<8|G(p[2])<<16|G(p[3])<<24;}
bool span(size_t size,uint64_t offset,uint64_t bytes){return offset<=size&&bytes<=size-offset;}
bool fail(std::string &error,const char *s){error=s;return false;}
void digest(const std::vector<uint8_t>&data,uint8_t result[32]){OqSha256Stream h;h.update(data.data(),data.size());h.digest(result);}
bool cpu_image(const std::vector<uint8_t>&rom,unsigned header,G core,PreparedCore &out,std::string &error){
    G offset=r32(rom.data()+header),entry=r32(rom.data()+header+4),base=r32(rom.data()+header+8),stored=r32(rom.data()+header+12);
    G hook=r32(rom.data()+(core==9?0x70:0x74));
    if(!stored||stored>code_limit||offset<0x160||!span(rom.size(),offset,stored)||uint64_t(base)+stored>0x100000000ull)return fail(error,"Invalid CPU image range");
    std::vector<uint8_t> raw(rom.begin()+offset,rom.begin()+offset+stored),image;
    if(core==9){if(!unpack_blz(raw,false,image,error))return false;}else image=std::move(raw);
    if(uint64_t(base)+image.size()>0x100000000ull||entry<base||uint64_t(entry)-base>=image.size()||(entry&3))return fail(error,"CPU entry outside image");
    if(!hook||(hook&3)||uint64_t(hook)<uint64_t(base)+4||!span(image.size(),uint64_t(hook)-base-4,4))return fail(error,"Invalid CPU settings hook");
    G settings=r32(image.data()+hook-base-4);
    if(settings<base||!span(image.size(),uint64_t(settings)-base,24))return fail(error,"CPU settings outside image");
    const uint8_t *s=image.data()+settings-base;
    G table_begin=r32(s),table_end=r32(s+4),copy_begin=r32(s+8),bss_begin=r32(s+12),bss_end=r32(s+16),compressed_end=r32(s+20);
    if((table_begin&3)||table_end<table_begin||(table_end-table_begin)%12||table_begin<base||!span(image.size(),uint64_t(table_begin)-base,table_end-table_begin))return fail(error,"Invalid autoload table");
    if(copy_begin<base||uint64_t(copy_begin)-base>image.size()||bss_end<bss_begin||uint64_t(bss_end)-bss_begin>code_limit)return fail(error,"Invalid autoload source or BSS");
    if(compressed_end&&(core!=9||uint64_t(compressed_end)!=uint64_t(base)+stored))return fail(error,"Invalid compressed-end metadata");
    PreparedCore result;result.core=core;result.base=base;result.entry=entry;result.settings=settings;result.bss_begin=bss_begin;result.bss_end=bss_end;
    result.sections.push_back({base,0,copy_begin-base,0,true});uint64_t source=uint64_t(copy_begin)-base;
    for(uint64_t at=uint64_t(table_begin)-base;at<uint64_t(table_end)-base;at+=12){
        G address=r32(image.data()+at),bytes=r32(image.data()+at+4),bss=r32(image.data()+at+8);
        if(bytes>code_limit||bss>code_limit||uint64_t(address)+bytes+bss>0x100000000ull||!span(image.size(),source,bytes))return fail(error,"Invalid autoload section");
        result.sections.push_back({address,G(source),bytes,bss,false});source+=bytes;
    }
    result.image=std::move(image);out=std::move(result);return true;
}
bool overlay_images(const std::vector<uint8_t>&rom,unsigned header,PreparedCore &out,std::string &error){
    G table=r32(rom.data()+header),bytes=r32(rom.data()+header+4),fat=r32(rom.data()+0x48),fat_bytes=r32(rom.data()+0x4c);
    if(bytes%32||bytes/32>256||!span(rom.size(),table,bytes)||(bytes&&table<0x160)||fat_bytes%8||fat_bytes/8>100000||!span(rom.size(),fat,fat_bytes)||(fat_bytes&&fat<0x160))return fail(error,"Invalid overlay/FAT table");
    std::set<G> ids;
    for(uint64_t at=table;at<uint64_t(table)+bytes;at+=32){
        const auto *s=rom.data()+at;PreparedOverlay item;
        item.id=r32(s);item.address=r32(s+4);G expanded=r32(s+8);item.bss=r32(s+12);item.init_begin=r32(s+16);item.init_end=r32(s+20);item.file_id=r32(s+24);
        G packed=r32(s+28),compressed=packed&0xffffff;item.flags=packed>>24;
        if(item.id>=4096||!ids.insert(item.id).second||(item.flags&~3u)||uint64_t(expanded)+item.bss>code_limit||uint64_t(item.address)+expanded+item.bss>0x100000000ull||!span(fat_bytes,uint64_t(item.file_id)*8,8))return fail(error,"Invalid overlay metadata");
        const auto *f=rom.data()+fat+uint64_t(item.file_id)*8;G begin=r32(f),end=r32(f+4);
        if(end<begin||uint64_t(end)-begin>code_limit||!span(rom.size(),begin,uint64_t(end)-begin)||((item.flags&1)&&compressed!=end-begin))return fail(error,"Invalid overlay file range");
        std::vector<uint8_t> raw(rom.begin()+begin,rom.begin()+end);
        if(item.flags&1){if(!unpack_blz(raw,true,item.image,error))return false;}else item.image=std::move(raw);
        if(item.image.size()!=expanded)return fail(error,"Overlay expanded length mismatch");
        if(item.init_begin!=item.init_end&&((item.init_begin&3)||(item.init_end&3)||item.init_begin<item.address||item.init_end<item.init_begin||uint64_t(item.init_end)>uint64_t(item.address)+expanded))return fail(error,"Invalid overlay initializer range");
        digest(item.image,item.sha256);out.overlays.push_back(std::move(item));
    }return true;
}
}
bool known_profile(const uint8_t code[4],RomProfile &out){
    const char *hex=nullptr;out={};
    if(!memcmp(code,"IPKD",4))hex="b0746b12cafdc2a80de21d218d491cd06c567fab07d9740ae658bb548c37a339";
    if(!memcmp(code,"IPGD",4))hex="e8cd5811650e9a9a1ca98a18c2b00b24583a7a770f82b4523147c4f96656a12e";
    if(!hex)return false;
    out.bytes=128u*1024*1024;memcpy(out.identity.game_code,code,4);memcpy(out.identity.maker_code,"01",2);
    auto nibble=[](char c){return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};
    for(unsigned i=0;i<32;++i)out.identity.sha256[i]=uint8_t(nibble(hex[i*2])*16+nibble(hex[i*2+1]));
    return true;
}
bool unpack_blz(const std::vector<uint8_t>&stored,bool required,std::vector<uint8_t>&out,std::string &error,size_t limit){
    out.clear();if(stored.size()>limit)return fail(error,"Code input exceeds limit");
    bool oversized=false,candidate=false;unsigned results=0;std::vector<uint8_t> result;
    for(size_t appended=0;stored.size()>=8&&appended<std::min(size_t(32),stored.size()-7);appended+=4){
        size_t end=stored.size()-appended;G descriptor=r32(stored.data()+end-8),extra=r32(stored.data()+end-4);size_t header=descriptor>>24,compressed=descriptor&0xffffff;
        if(header<8||header>compressed||compressed>end||!extra)continue;
        if(!std::all_of(stored.begin()+end-header,stored.begin()+end-8,[](uint8_t b){return b==255;}))continue;
        if(uint64_t(end)+extra+appended>limit){oversized=true;continue;}candidate=true;
        size_t prefix=end-compressed,source=end-header;std::vector<uint8_t> image(end+extra,0);std::copy_n(stored.begin(),prefix,image.begin());size_t write=image.size();bool valid=true;
        while(valid&&write>prefix){
            if(source<=prefix){valid=false;break;}uint8_t flags=stored[--source];
            for(int bit=7;bit>=0&&write>prefix;--bit){
                if(flags&(1u<<bit)){
                    if(source-prefix<2){valid=false;break;}source-=2;G first=stored[source+1],second=stored[source];size_t count=(first>>4)+3,displacement=((first&15)<<8|second)+3;
                    if(count>write-prefix||displacement>image.size()-write){valid=false;break;}
                    while(count--){--write;image[write]=image[write+displacement];}
                }else {if(source<=prefix){valid=false;break;}image[--write]=stored[--source];}
            }
        }
        if(valid&&source==prefix){image.insert(image.end(),stored.begin()+end,stored.end());result=std::move(image);++results;}
    }
    if(!candidate){if(oversized)return fail(error,"Code expansion exceeds limit");if(required)return fail(error,"Backwards-LZ footer missing");out=stored;return true;}
    if(results!=1)return fail(error,"Invalid or ambiguous backwards-LZ stream");out=std::move(result);return true;
}
bool prepare_snapshot(std::vector<uint8_t> snapshot,const RomProfile &profile,PreparedRom &out,std::string &error){
    out={};error.clear();size_t size=snapshot.size();
    if(size<512||size>0x20000000u||size!=profile.bytes||(size&(size-1)))return fail(error,"ROM size differs from exact profile");
    if(memcmp(snapshot.data()+12,profile.identity.game_code,4)||memcmp(snapshot.data()+16,profile.identity.maker_code,2)||snapshot[30]!=profile.identity.revision||snapshot[18])return fail(error,"ROM identity/revision/unit differs from profile");
    uint8_t hash[32];digest(snapshot,hash);if(memcmp(hash,profile.identity.sha256,32))return fail(error,"ROM SHA-256 differs from exact profile");
    PreparedRom result;result.identity=profile.identity;
    for(unsigned header:{0x40u,0x48u}){G offset=r32(snapshot.data()+header),bytes=r32(snapshot.data()+header+4);
        if(!span(size,offset,bytes)||(bytes&&offset<0x160)||(header==0x48&&(bytes%8||bytes/8>100000)))return fail(error,"Invalid asset table");
    }
    G fat=r32(snapshot.data()+0x48),fat_bytes=r32(snapshot.data()+0x4c);
    for(uint64_t at=fat;at<uint64_t(fat)+fat_bytes;at+=8){G begin=r32(snapshot.data()+at),end=r32(snapshot.data()+at+4);if(end<begin||!span(size,begin,uint64_t(end)-begin))return fail(error,"Asset FAT entry outside ROM");}
    if(!cpu_image(snapshot,0x20,9,result.arm9,error)||!cpu_image(snapshot,0x30,7,result.arm7,error)||!overlay_images(snapshot,0x50,result.arm9,error)||!overlay_images(snapshot,0x58,result.arm7,error))return false;
    result.snapshot=std::move(snapshot);out=std::move(result);return true;
}
bool prepare_rom_file(const char *path,PreparedRom &out,std::string &error){
    out={};error.clear();if(!path)return fail(error,"ROM path missing");FILE*f=fopen(path,"rb");if(!f)return fail(error,"ROM input unavailable");
    uint8_t header[512];bool ok=fread(header,1,sizeof(header),f)==sizeof(header)&&!fseek(f,0,SEEK_END);long bytes=ok?ftell(f):-1;
    RomProfile profile;
    if(!ok||bytes<512||!known_profile(header+12,profile)||uint64_t(bytes)!=profile.bytes||fseek(f,0,SEEK_SET)){fclose(f);return fail(error,"Unsupported ROM identity or file size");}
    std::vector<uint8_t> snapshot(profile.bytes);ok=fread(snapshot.data(),1,snapshot.size(),f)==snapshot.size();ok=!fclose(f)&&ok;
    if(!ok)return fail(error,"ROM snapshot read failed");return prepare_snapshot(std::move(snapshot),profile,out,error);
}
}
