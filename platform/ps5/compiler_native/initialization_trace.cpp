#include "initialization_trace.h"
#include <cstring>
namespace apricot::aot {
bool validate_initialization(const PreparedOverlay&o,const std::vector<uint8_t>&final,
    const std::vector<InitializationWrite>&rows,std::string&error){
    error.clear();auto fail=[&](const char*s){error=s;return false;};
    if(o.image.empty()||o.image.size()>16*1024*1024||final.size()!=o.image.size()||rows.empty()||rows.size()>1000000)
        return fail("Invalid bounded initialization trace");
    uint8_t digest[32];oq_sha256(o.image.data(),o.image.size(),digest);
    if(memcmp(digest,o.sha256,32))return fail("Original overlay identity changed");
    for(auto&r:rows){
        if((r.core!=7&&r.core!=9)||(r.width!=1&&r.width!=2&&r.width!=4)||r.address<o.address||
           uint64_t(r.address)-o.address+r.width>o.image.size()||(r.writer&1)||
           (r.width<4&&((r.before|r.after)>>(r.width*8))))return fail("Invalid initialization write");
    }
    auto value=[](const std::vector<uint8_t>&v,size_t at,G width){G n=0;for(G b=0;b<width;++b)n|=G(v[at+b])<<(b*8);return n;};
    auto store=[](std::vector<uint8_t>&v,size_t at,G width,G n){for(G b=0;b<width;++b)v[at+b]=uint8_t(n>>(b*8));};
    auto reverse=final;size_t cut=rows.size();
    for(size_t n=rows.size();n>0;--n){const auto&r=rows[n-1];size_t at=r.address-o.address;
        if(value(reverse,at,r.width)!=r.after)return fail("Initialization trace final bytes differ");
        store(reverse,at,r.width,r.before);if(reverse==o.image){cut=n-1;break;}
    }
    if(cut==rows.size())return fail("No verified original-overlay checkpoint");
    auto rebuilt=o.image;G begin=0,end=0,writer=0;bool span=false;
    for(size_t n=cut;n<rows.size();++n){auto&r=rows[n];size_t at=r.address-o.address;
        if(value(rebuilt,at,r.width)!=r.before)return fail("Initialization trace preceding bytes differ");
        store(rebuilt,at,r.width,r.after);
        if(!span||r.writer!=writer||r.address!=end){
            if(span&&writer>=begin&&writer<end)return fail("Initializer rewrites its executing instruction");
            begin=r.address;writer=r.writer;span=true;
        }end=r.address+r.width;
    }
    if(span&&writer>=begin&&writer<end)return fail("Initializer rewrites its executing instruction");
    if(rebuilt!=final)return fail("Initialization replay does not reproduce final bytes");
    if(final==o.image)return fail("Initialization produced no new state");return true;
}
}
