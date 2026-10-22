# SPDX-License-Identifier: GPL-3.0-or-later
"""A32 lowering for explicitly discovered addresses; all decoding is on the PC.

Generated functions contain native control flow and constant hardware-service
calls. Missing code and instructions fail explicitly; no interpreter is emitted.
"""
from dataclasses import dataclass
from .aot import Unsupported

@dataclass
class Lowered:
    body: str
    targets: tuple = ()
    terminates: bool = False
    conditional: bool = False
    dynamic: bool = False

def reg(number, pc, *, store=False, shifted=False):
    return f'0x{pc+(12 if store or shifted else 8):08x}u' if number == 15 else f's.r[{number}]'

def operand(word, pc, flags):
    if word & (1 << 25):
        amount = ((word >> 8) & 15)*2
        raw = word & 255
        value = ((raw >> amount) | (raw << (32-amount))) & 0xffffffff if amount else raw
        prefix = f'guest_carry(s,{str(bool(value >> 31)).lower()});' if flags and amount else ''
        return prefix+f'G rhs=0x{value:08x}u;'
    if word & 16:
        if word & 128 or (word >> 8) & 15 == 15:
            raise Unsupported('Invalid register-specified shift')
        amount = f'({reg((word >> 8) & 15, pc)}&255u)'
    else:
        amount = str((word >> 7) & 31)
    return (f'G rhs=guest_shift(s,{reg(word & 15, pc, shifted=bool(word & 16))},'
            f'{(word >> 5) & 3},{amount},{str(bool(word & 16)).lower()},{str(flags).lower()});')

def lower(word, pc, core):
    cond = word >> 28
    rn, rd, rm = (word >> 16) & 15, (word >> 12) & 15, word & 15
    r = lambda n: reg(n, pc)
    if cond == 15:
        if core == 9 and word & 0x0e000000 == 0x0a000000:
            disp = (word & 0xffffff)*4 | ((word >> 23) & 2)
            if disp & 0x2000000:disp -= 0x4000000
            target = (pc+8+disp) & 0xffffffff
            return Lowered(f's.r[14]=0x{pc+4:08x}u;guest_pc(s,0x{target|1:08x}u,true);', ((target, True),), True)
        raise Unsupported('Unsupported unconditional-extension instruction')
    conditional = cond != 14
    if word & 0x0ffffff0 in (0x012fff10, 0x012fff30):
        link = bool(word & 32)
        if rm == 15 or link and core != 9:raise Unsupported('Invalid BX/BLX encoding for CPU')
        body = f'G target={r(rm)};'+(f's.r[14]=0x{pc+4:08x}u;' if link else '')
        return Lowered(body+'guest_pc(s,target,true);', (), True, conditional, True)
    if word & 0x0e000000 == 0x0a000000:
        disp = word & 0xffffff
        if disp & 0x800000:disp -= 0x1000000
        target = (pc+8+disp*4) & 0xffffffff
        body = (f's.r[14]=0x{pc+4:08x}u;' if word & 0x01000000 else '')
        return Lowered(body+f's.r[15]=0x{target:08x}u;', ((target, False),), True, conditional)
    if word & 0x0f000000 == 0x0f000000:
        return Lowered(f'G result=0;if(!h.system(h.context,&s,0x{0x3000000|(word&0xffffff):08x}u,0,&result))return G_SYSTEM;if(result==G_YIELD){{s.r[15]=0x{pc+4:08x}u;return G_YIELD;}}')
    if word & 0x0f000010 == 0x0e000010:
        if core != 9 or (word >> 8) & 15 != 15:raise Unsupported('Unsupported coprocessor')
        key = ((word >> 16) & 15) << 12 | (word & 15) << 8 | ((word >> 21) & 7) << 4 | ((word >> 5) & 7)
        read = bool(word & 0x00100000)
        body = f'G result=0;if(!h.system(h.context,&s,0x{(0x10000 if read else 0x20000)|key:08x}u,{"0" if read else r(rd)},&result))return G_SYSTEM;'
        if read:body += f's.r[{rd}]=result;' if rd != 15 else 's.cpsr=(s.cpsr&0x0fffffffu)|(result&0xf0000000u);'
        else:body += f'if(result==G_YIELD){{s.r[15]=0x{pc+4:08x}u;return G_YIELD;}}'
        return Lowered(body)
    if word & 0x0fbf0fff == 0x010f0000:
        if rd == 15:raise Unsupported('MRS pc')
        saved = bool(word & 0x400000)
        body = 'unsigned bank=guest_bank(s.cpsr);if(bank==6||bank==0)return G_UNSUPPORTED;' if saved else ''
        return Lowered(body+f's.r[{rd}]='+('s.spsr[bank];' if saved else 's.cpsr;'))
    if word & 0x0fb0fff0 == 0x0120f000 or word & 0x0fb0f000 == 0x0320f000:
        mask = sum(0xff << (byte*8) for byte in range(4) if word & (1 << (16+byte)))
        body = operand(word, pc, False)
        body += f'if(!guest_psr(s,rhs,0x{mask:08x}u,{str(bool(word&0x400000)).lower()}))return G_UNSUPPORTED;'
        return Lowered(body)
    if word & 0x0fff0ff0 == 0x016f0f10:
        if core != 9 or rd == 15 or rm == 15:raise Unsupported('Invalid CLZ')
        return Lowered(f'G n={r(rm)};s.r[{rd}]=n?G(__builtin_clz(n)):32u;')
    dsp=word&0x0ff00090
    if dsp in (0x01000080,0x01200080,0x01400080,0x01600080):
        dest,accumulate,rs=(word>>16)&15,(word>>12)&15,(word>>8)&15
        if core!=9 or 15 in (dest,accumulate,rs,rm):raise Unsupported('Invalid signed DSP multiply for CPU')
        x,y=bool(word&32),bool(word&64)
        half_s=f'int64_t(int16_t({r(rs)}>>{16 if y else 0}))'
        half_m=f'int64_t(int16_t({r(rm)}>>{16 if x else 0}))'
        if dsp==0x01400080:
            if dest==accumulate:raise Unsupported('Overlapping signed DSP long destinations')
            body=f'uint64_t product=uint64_t({half_m}*{half_s});uint64_t sum=product+((uint64_t(s.r[{dest}])<<32)|s.r[{accumulate}]);'
            return Lowered(body+f's.r[{dest}]=G(sum>>32);s.r[{accumulate}]=G(sum);')
        word_multiply=dsp==0x01200080
        multiply_only=dsp==0x01600080 or word_multiply and x
        if multiply_only and accumulate:raise Unsupported('Reserved signed DSP multiply field')
        product=f'(int64_t(int32_t({r(rm)}))*{half_s})>>16' if word_multiply else f'{half_m}*{half_s}'
        body=f'int64_t product={product};'
        if not multiply_only:
            body+=f'product+=int64_t(int32_t({r(accumulate)}));if(product>2147483647ll||product<(-2147483647ll-1))s.cpsr|=0x08000000u;'
        body+=f's.r[{dest}]=G(uint64_t(product));'
        return Lowered(body)
    if word & 0x0fc000f0 == 0x00000090:
        dest, accumulate = (word >> 16) & 15, (word >> 12) & 15
        rs = (word >> 8) & 15
        if 15 in (dest, accumulate, rs, rm):raise Unsupported('Multiply using pc')
        body = f'G result={r(rm)}*{r(rs)}'+(f'+{r(accumulate)};' if word & 0x200000 else ';')
        body += f's.r[{dest}]=result;'+('guest_nz(s,result);' if word & 0x100000 else '')
        return Lowered(body)
    if word & 0x0f8000f0 == 0x00800090:
        hi, lo, rs = (word >> 16) & 15, (word >> 12) & 15, (word >> 8) & 15
        if 15 in (hi, lo, rs, rm) or hi == lo:raise Unsupported('Invalid long multiply')
        mul = f'uint64_t(int64_t(int32_t({r(rm)}))*int64_t(int32_t({r(rs)})))' if word & 0x400000 else f'uint64_t({r(rm)})*{r(rs)}'
        body = f'uint64_t result={mul};'
        if word & 0x200000:body += f'result+=(uint64_t(s.r[{hi}])<<32)|s.r[{lo}];'
        body += f's.r[{hi}]=G(result>>32);s.r[{lo}]=G(result);'
        if word & 0x100000:body += 's.cpsr=(s.cpsr&0x3fffffffu)|(G(result>>32)&0x80000000u)|(result?0:0x40000000u);'
        return Lowered(body)
    if word & 0x0fb00ff0 == 0x01000090:
        if 15 in (rn, rd, rm):raise Unsupported('Invalid SWP')
        width = 1 if word & 0x400000 else 4
        return Lowered(f'G value=0;G address={r(rn)};G source={r(rm)};if(!guest_load(s,h,address,{width},value)||!guest_store(s,h,address,{width},source))return G_MEMORY;s.r[{rd}]=value;')
    if word & 0x0e000000 == 0x08000000:
        if rn == 15 or not word & 0xffff:raise Unsupported('Unimplemented empty block transfer')
        registers = [n for n in range(16) if word & (1 << n)]
        load, up, before, writeback = bool(word & 0x100000), bool(word & 0x800000), bool(word & 0x1000000), bool(word & 0x200000)
        restore = bool(word & 0x400000) and load and 15 in registers
        user = bool(word & 0x400000) and not restore
        if writeback and rn in registers:raise Unsupported('Block transfer with base in writeback list')
        offset = 4*int(before) if up else -4*(len(registers)-int(not before))
        body = f'G base={r(rn)};G address=base+G({offset});G value=0;'
        if restore:body += 'G pc_value=0;'
        for index, number in enumerate(registers):
            if load:
                body += f'if(!guest_load(s,h,address+{index*4}u,4,value))return G_MEMORY;'
                if number != 15:body += f'guest_set_user(s,{number},value);' if user else f's.r[{number}]=value;'
                elif restore:body += 'pc_value=value;'
                else:body += f'guest_pc(s,value,{str(core==9).lower()});'
            else:
                source = f'guest_user(s,{number})' if user and number != 15 else reg(number,pc,store=True)
                body += f'if(!guest_store(s,h,address+{index*4}u,4,{source}))return G_MEMORY;'
        if writeback:body += f's.r[{rn}]=base+G({len(registers)*4*(1 if up else -1)});'
        if restore:body += 'unsigned bank=guest_bank(s.cpsr);if(!bank||bank==6||!guest_psr(s,s.spsr[bank],0xffffffffu,false))return G_UNSUPPORTED;guest_pc(s,pc_value,false);'
        return Lowered(body, (), load and 15 in registers, conditional, load and 15 in registers)
    single = word & 0x0c000000 == 0x04000000
    half = word & 0x0e000090 == 0x00000090 and (word >> 5) & 3
    if single or half:
        load = bool(word & 0x100000)
        if rn == 15 and (not word & 0x1000000 or word & 0x200000):raise Unsupported('Writeback to pc')
        if load and rn == rd and (not word & 0x1000000 or word & 0x200000):raise Unsupported('Load overlaps writeback base')
        signed = False
        if single:
            width = 1 if word & 0x400000 else 4
            if word & 0x2000000:
                if word & 16:raise Unsupported('Invalid load/store shifted offset')
                offset = f'guest_shift(s,{r(rm)},{(word>>5)&3},{(word>>7)&31},false,false)'
            else:offset = str(word & 4095)
        else:
            kind = (word >> 5) & 3
            if not load and kind != 1:raise Unsupported('Doubleword transfer not yet lowered')
            signed = kind != 1
            width = 1 if kind == 2 else 2
            offset = str(((word >> 4) & 0xf0) | (word & 15)) if word & 0x400000 else r(rm)
        if rd == 15 and width != 4:raise Unsupported('Invalid byte/halfword pc transfer')
        body = f'G base={r(rn)};G modified=base'+('+' if word & 0x800000 else '-')+f'G({offset});G address='+('modified;' if word & 0x1000000 else 'base;')
        if load:body += f'G value=0;if(!guest_load(s,h,address,{width},value,{str(signed).lower()}))return G_MEMORY;'
        else:body += f'if(!guest_store(s,h,address,{width},{reg(rd,pc,store=True)}))return G_MEMORY;'
        if not word & 0x1000000 or word & 0x200000:body += f's.r[{rn}]=modified;'
        if load:body += f'guest_pc(s,value,{str(core==9).lower()});' if rd == 15 else f's.r[{rd}]=value;'
        return Lowered(body, (), load and rd == 15, conditional, load and rd == 15)
    if word & 0x0c000000 == 0:
        opcode, flags = (word >> 21) & 15, bool(word & 0x100000)
        test = opcode in (8, 9, 10, 11)
        if test and not flags:raise Unsupported('Reserved data-processing encoding')
        logical = opcode in (0, 1, 8, 9, 12, 13, 14, 15)
        body = operand(word, pc, flags and logical)
        body += f'G a={reg(rn,pc,shifted=not bool(word&0x02000000) and bool(word&16))};G result=0;'
        flag = str(flags).lower()
        expressions = {0:'a&rhs',1:'a^rhs',2:f'guest_add(s,a,~rhs,1,{flag})',3:f'guest_add(s,rhs,~a,1,{flag})',
            4:f'guest_add(s,a,rhs,0,{flag})',5:f'guest_add(s,a,rhs,(s.cpsr>>29)&1,{flag})',
            6:f'guest_add(s,a,~rhs,(s.cpsr>>29)&1,{flag})',7:f'guest_add(s,rhs,~a,(s.cpsr>>29)&1,{flag})',
            8:'a&rhs',9:'a^rhs',10:'guest_add(s,a,~rhs,1,true)',11:'guest_add(s,a,rhs,0,true)',
            12:'a|rhs',13:'rhs',14:'a&~rhs',15:'~rhs'}
        body += 'result='+expressions[opcode]+';'
        if logical and flags:body += 'guest_nz(s,result);'
        if not test:
            if rd != 15:body += f's.r[{rd}]=result;'
            else:
                if flags:body += 'unsigned bank=guest_bank(s.cpsr);if(!bank||bank==6||!guest_psr(s,s.spsr[bank],0xffffffffu,false))return G_UNSUPPORTED;'
                body += 'guest_pc(s,result,false);'
        return Lowered(body, (), rd == 15 and not test, conditional, rd == 15 and not test)
    raise Unsupported(f'Unsupported A32 encoding {word:08x}')

def emit(instructions, core, name, *, guarded=False):
    """instructions maps verified aligned guest addresses to A32 words."""
    if core not in (7,9) or not name.replace('_','').isalnum():raise ValueError('Invalid compilation target')
    out = [f'extern "C" G {name}(GuestCpu *cpu,const GuestHost *host) {{',
        'if(!cpu||!host||host->abi!=GUEST_ABI||host->bytes!=sizeof(GuestHost)||!host->read||!host->write||!host->system)return G_BAD_ABI;',
        f'auto &s=*cpu;const auto &h=*host;if(s.core!={core}u||(s.cpsr&32u))return G_UNMAPPED;',
        'dispatch:switch(s.r[15]) {']
    for pc in sorted(instructions):
        if pc & 3:raise ValueError('Unaligned A32 code address')
        out.append(f'case 0x{pc:08x}u:goto L{pc:08x};')
    out += ['default:return G_UNMAPPED;}']
    def continuation(pc):
        return f'goto L{pc:08x};' if pc in instructions else f's.r[15]=0x{pc:08x}u;return G_UNMAPPED;'
    for pc, word in sorted(instructions.items()):
        instruction = lower(word, pc, core)
        guard=f'G guard=0;if(!h.read(h.context,s.core,0x{pc:08x}u,4u,&guard)||guard!=0x{word:08x}u)return G_UNMAPPED;' if guarded else ''
        out.append(f'L{pc:08x}:{{s.r[15]=0x{pc:08x}u;if(!s.budget)return G_YIELD;'+guard+'--s.budget;')
        factor=1 if core==9 else 2
        out.append(f's.ticks+={factor}u;')
        if word >> 28 < 14:out.append(f'if(!guest_condition(s,{word>>28})){{{continuation(pc+4)}}}')
        cost=1
        if instruction.terminates:cost=3
        elif word&0x0e000000==0x08000000:cost=(word&0xffff).bit_count()+2
        elif word&0x0c000000==0x04000000 or word&0x0e000090==0x00000090:cost=3 if word&0x100000 else 2
        elif not word&0x02000000 and word&16:cost=2
        if cost>1:out.append(f's.ticks+={(cost-1)*factor}u;')
        out.append(instruction.body)
        if instruction.terminates:
            if instruction.dynamic or instruction.targets and instruction.targets[0][1]:out.append('if(s.cpsr&32u)return G_TRANSFER;goto dispatch;')
            elif instruction.targets:out.append(continuation(instruction.targets[0][0]))
        else:out.append(continuation(pc+4))
        out.append('}')
    return '\n'.join(out+['}'])+'\n'
