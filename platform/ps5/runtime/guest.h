// SPDX-License-Identifier: GPL-3.0-or-later
// Native AOT interface for real code. Separate from the stable diagnostic ABI.
#pragma once
#include <stdint.h>
using G = uint32_t;
enum GuestStop : G { G_TRANSFER=0, G_YIELD=1, G_MEMORY=2, G_UNMAPPED=3,
                      G_BAD_ABI=4, G_SYSTEM=5, G_UNSUPPORTED=6 };
struct GuestCpu {
    G r[16], cpsr, budget, fault, core;
    G bank_sp[6], bank_lr[6], spsr[6], user_high[5], fiq_high[5];
    uint64_t ticks;
};
struct GuestHost {
    G abi, bytes;
    void *context;
    bool (*read)(void *, G core, G address, G width, G *value);
    bool (*write)(void *, G core, G address, G width, G value);
    bool (*system)(void *, GuestCpu *, G operation, G argument, G *result);
};
static_assert(sizeof(GuestCpu)==200&&sizeof(GuestHost)==40,"AOT bridge ABI layout");
using GuestEntry = G (*)(GuestCpu *, const GuestHost *);
constexpr G GUEST_ABI=2;
static inline unsigned guest_bank(G mode) {
    switch(mode&31) {case 0x10:case 0x1f:return 0;case 0x11:return 1;
    case 0x12:return 2;case 0x13:return 3;case 0x17:return 4;case 0x1b:return 5;default:return 6;}
}
static inline bool guest_psr(GuestCpu &s,G value,G mask,bool saved) {
    unsigned old=guest_bank(s.cpsr);
    if(old==6)return false;
    if(saved) {if(!old)return false;s.spsr[old]=(s.spsr[old]&~mask)|(value&mask);return true;}
    if((s.cpsr&31)==0x10)mask&=0xf8000000u;
    G updated=(s.cpsr&~mask)|(value&mask);unsigned next=guest_bank(updated);
    if(next==6)return false;
    if(old!=next) {
        s.bank_sp[old]=s.r[13];s.bank_lr[old]=s.r[14];
        if(old==1||next==1)for(unsigned i=0;i<5;++i) {
            (old==1?s.fiq_high:s.user_high)[i]=s.r[8+i];
            s.r[8+i]=(next==1?s.fiq_high:s.user_high)[i];
        }
        s.r[13]=s.bank_sp[next];s.r[14]=s.bank_lr[next];
    }
    s.cpsr=updated;return true;
}
static inline G guest_user(const GuestCpu &s,unsigned number) {
    unsigned bank=guest_bank(s.cpsr);
    if(number<8||number<13&&bank!=1||number==15)return s.r[number];
    if(number<13)return s.user_high[number-8];
    return bank==0?s.r[number]:(number==13?s.bank_sp[0]:s.bank_lr[0]);
}
static inline void guest_set_user(GuestCpu &s,unsigned number,G value) {
    unsigned bank=guest_bank(s.cpsr);
    if(number<8||number<13&&bank!=1||number==15)s.r[number]=value;
    else if(number<13)s.user_high[number-8]=value;
    else if(bank==0)s.r[number]=value;
    else if(number==13)s.bank_sp[0]=value;
    else s.bank_lr[0]=value;
}
static inline void guest_nz(GuestCpu &s,G r) {s.cpsr=(s.cpsr&0x3fffffffu)|(r&0x80000000u)|(r?0:0x40000000u);}
static inline void guest_carry(GuestCpu &s,bool c) {s.cpsr=(s.cpsr&~0x20000000u)|(G(c)<<29);}
static inline G guest_add(GuestCpu &s,G a,G b,G carry,bool flags) {
    uint64_t wide=uint64_t(a)+b+carry;G r=G(wide);
    if(flags) {guest_nz(s,r);s.cpsr=(s.cpsr&~0x30000000u)|(G(wide>>32)<<29)|((~(a^b)&(a^r)&0x80000000u)>>3);}
    return r;
}
static inline bool guest_condition(const GuestCpu &s,G c) {
    bool n=s.cpsr>>31,z=(s.cpsr>>30)&1,carry=(s.cpsr>>29)&1,v=(s.cpsr>>28)&1;
    switch(c) {case 0:return z;case 1:return !z;case 2:return carry;case 3:return !carry;
    case 4:return n;case 5:return !n;case 6:return v;case 7:return !v;case 8:return carry&&!z;
    case 9:return !carry||z;case 10:return n==v;case 11:return n!=v;case 12:return !z&&n==v;
    case 13:return z||n!=v;case 14:return true;default:return false;}
}
static inline G guest_ror(G n,G amount) {amount&=31;return amount?(n>>amount)|(n<<(32-amount)):n;}
static inline G guest_shift(GuestCpu &s,G v,G type,G amount,bool by_register,bool flags) {
    bool carry=(s.cpsr>>29)&1;G r=v;
    if(by_register&&amount==0)return v;
    switch(type) {
    case 0:
        if(amount) {carry=amount<=32?bool((v>>(32-amount))&1):false;r=amount<32?v<<amount:0;}
        break;
    case 1:
        if(!by_register&&!amount)amount=32;
        carry=amount<=32?bool((v>>(amount-1))&1):false;r=amount<32?v>>amount:0;break;
    case 2:
        if(!by_register&&!amount)amount=32;
        if(amount>=32) {carry=v>>31;r=carry?0xffffffffu:0;}
        else {carry=(v>>(amount-1))&1;r=(v>>amount)|((v&0x80000000u)?~(0xffffffffu>>amount):0);}
        break;
    default:
        if(!by_register&&!amount) {r=(G(carry)<<31)|(v>>1);carry=v&1;}
        else {r=guest_ror(v,amount);carry=r>>31;}
    }
    if(flags)guest_carry(s,carry);return r;
}
static inline bool guest_load(GuestCpu &s,const GuestHost &h,G a,G width,G &r,bool signed_value=false) {
    if(width==2&&(a&1)) {s.fault=a;return false;} // CPU-specific odd halfword rules not lowered yet.
    G aligned=width==4?a&~3u:width==2?a&~1u:a;
    if(!h.read(h.context,s.core,aligned,width,&r)) {s.fault=a;return false;}
    if(width==4)r=guest_ror(r,(a&3)*8);
    if(width==2)r&=0xffffu;
    if(width==1)r&=255;
    if(signed_value) {
        if(width==1)r=(r&0x80)?r|0xffffff00u:r;
        else if(width==2)r=(r&0x8000)?r|0xffff0000u:r;
    }
    return true;
}
static inline bool guest_store(GuestCpu &s,const GuestHost &h,G a,G width,G value) {
    G aligned=width==4?a&~3u:width==2?a&~1u:a;
    if(!h.write(h.context,s.core,aligned,width,value)) {s.fault=a;return false;}
    return true;
}
static inline G guest_pc(GuestCpu &s,G target,bool exchange) {
    if(exchange)s.cpsr=(s.cpsr&~32u)|((target&1u)<<5);
    s.r[15]=target&((s.cpsr&32)?~1u:~3u);return G_TRANSFER;
}
