// Fixed, game-independent native arithmetic support copied into AOT images.
// These functions receive values/indices, never an ARM/Thumb opcode.
#include "guest.h"
extern "C" {
#define SHIFT(name,type) G name(GuestCpu*s,const GuestHost*,G value,G amount,G reg,G flags){return guest_shift(*s,value,type,amount,reg,flags);}
SHIFT(aot_lsl,0) SHIFT(aot_lsr,1) SHIFT(aot_asr,2) SHIFT(aot_ror,3)
G aot_add(GuestCpu*s,const GuestHost*,G a,G b,G carry,G flags){return guest_add(*s,a,b,carry,flags);}
G aot_psr(GuestCpu*s,const GuestHost*,G value,G mask,G saved,G){return guest_psr(*s,value,mask,saved)?G_TRANSFER:G_UNSUPPORTED;}
G aot_mrs_saved(GuestCpu*s,const GuestHost*,G dest,G,G,G){
    unsigned bank=guest_bank(s->cpsr);if(!bank||bank==6)return G_UNSUPPORTED;
    s->r[dest]=s->spsr[bank];return G_TRANSFER;
}
G aot_restore(GuestCpu*s,const GuestHost*,G target,G,G,G){
    unsigned bank=guest_bank(s->cpsr);
    if(!bank||bank==6||!guest_psr(*s,s->spsr[bank],0xffffffffu,false))return G_UNSUPPORTED;
    return guest_pc(*s,target,false);
}
G aot_get_user(GuestCpu*s,const GuestHost*,G reg,G,G,G){return guest_user(*s,reg);}
G aot_set_user(GuestCpu*s,const GuestHost*,G reg,G value,G,G){guest_set_user(*s,reg,value);return G_TRANSFER;}
#define LONG(name,signed_value,accumulate) G name(GuestCpu*s,const GuestHost*,G a,G b,G hi,G lo){ \
 uint64_t value=signed_value?uint64_t(int64_t(int32_t(a))*int64_t(int32_t(b))):uint64_t(a)*b; \
 if(accumulate)value+=(uint64_t(s->r[hi])<<32)|s->r[lo]; \
 s->r[hi]=G(value>>32);s->r[lo]=G(value);return G_TRANSFER;}
LONG(aot_umull,false,false) LONG(aot_umlal,false,true)
LONG(aot_smull,true,false) LONG(aot_smlal,true,true)
#define WORD_MUL(name,accumulate) G name(GuestCpu*s,const GuestHost*,G a,G b,G dest,G acc){ \
 int64_t value=(int64_t(int32_t(a))*int64_t(int32_t(b)))>>16; \
 if(accumulate){value+=int64_t(int32_t(s->r[acc]));if(value>2147483647ll||value<(-2147483647ll-1))s->cpsr|=0x08000000u;} \
 s->r[dest]=G(uint64_t(value));return G_TRANSFER;}
WORD_MUL(aot_smulw,false) WORD_MUL(aot_smlaw,true)
// Link container exports only; the image compiler uses individual helper offsets.
G oq_boot_arm9(GuestCpu*,const GuestHost*){return G_UNSUPPORTED;}
G oq_boot_arm7(GuestCpu*,const GuestHost*){return G_UNSUPPORTED;}
const void *oq_boot_overlay_catalog(unsigned *count){*count=0;return nullptr;}
}
