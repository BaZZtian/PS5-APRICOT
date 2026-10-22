# SPDX-License-Identifier: GPL-3.0-or-later
"""Ahead-of-time lowering of ARMv4T/ARMv5 Thumb instructions."""
from .arm import Lowered
from .aot import Unsupported

def reg(number, pc):
    return f'0x{pc+4:08x}u' if number == 15 else f's.r[{number}]'

def lower(word, pc, core):
    rd, rs = word & 7, (word >> 3) & 7
    r = lambda n: reg(n, pc)
    if word & 0xf800 in (0x0000, 0x0800, 0x1000):
        return Lowered(f's.r[{rd}]=guest_shift(s,{r(rs)},{(word>>11)&3},{(word>>6)&31},false,true);guest_nz(s,s.r[{rd}]);')
    if word & 0xf800 == 0x1800:
        right = str((word >> 6) & 7) if word & 0x400 else r((word >> 6) & 7)
        return Lowered(f's.r[{rd}]=guest_add(s,{r(rs)},'+('~G('+right+'),1' if word & 0x200 else right+',0')+',true);')
    if word & 0xe000 == 0x2000:
        dest, opcode, imm = (word >> 8) & 7, (word >> 11) & 3, word & 255
        if opcode == 0:return Lowered(f's.r[{dest}]={imm};guest_nz(s,s.r[{dest}]);')
        expression = f'guest_add(s,s.r[{dest}],'+(f'~G({imm}),1' if opcode in (1,3) else f'{imm},0')+',true)'
        return Lowered(f'(void){expression};' if opcode == 1 else f's.r[{dest}]={expression};')
    if word & 0xfc00 == 0x4000:
        opcode = (word >> 6) & 15
        a, b = r(rd), r(rs)
        expressions = {0:f'{a}&{b}',1:f'{a}^{b}',2:f'guest_shift(s,{a},0,{b}&255u,true,true)',
            3:f'guest_shift(s,{a},1,{b}&255u,true,true)',4:f'guest_shift(s,{a},2,{b}&255u,true,true)',
            5:f'guest_add(s,{a},{b},(s.cpsr>>29)&1,true)',6:f'guest_add(s,{a},~{b},(s.cpsr>>29)&1,true)',
            7:f'guest_shift(s,{a},3,{b}&255u,true,true)',8:f'{a}&{b}',9:f'guest_add(s,0,~{b},1,true)',
            10:f'guest_add(s,{a},~{b},1,true)',11:f'guest_add(s,{a},{b},0,true)',12:f'{a}|{b}',
            13:f'{a}*{b}',14:f'{a}&~{b}',15:f'~{b}'}
        body = 'G result='+expressions[opcode]+';'
        if opcode not in (5,6,9,10,11):body += 'guest_nz(s,result);'
        if opcode not in (8,10,11):body += f's.r[{rd}]=result;'
        return Lowered(body)
    if word & 0xfc00 == 0x4400:
        opcode = (word >> 8) & 3
        dest, source = rd | ((word >> 4) & 8), (word >> 3) & 15
        if opcode == 3:
            if word & 7 or word & 0x80 and core != 9:raise Unsupported('Invalid Thumb BX/BLX')
            body = f'G target={r(source)};'
            if word & 0x80:body += f's.r[14]=0x{(pc+2)|1:08x}u;'
            return Lowered(body+'guest_pc(s,target,true);', (), True, False, True)
        if opcode == 1:return Lowered(f'(void)guest_add(s,{r(dest)},~{r(source)},1,true);')
        value = f'{r(dest)}+{r(source)}' if opcode == 0 else r(source)
        if dest == 15:return Lowered(f'guest_pc(s,{value},false);', (), True, False, True)
        return Lowered(f's.r[{dest}]={value};')
    address, width, load, signed = None, 4, False, False
    if word & 0xf800 == 0x4800:
        rd = (word >> 8) & 7;load = True
        address = f'0x{((pc+4)&~3)+((word&255)*4):08x}u'
    elif word & 0xf000 == 0x5000:
        opcode = (word >> 9) & 7
        width = (4,2,1,1,4,2,1,2)[opcode]
        signed = opcode in (3,7);load = opcode >= 3
        address = f'{r(rs)}+{r((word>>6)&7)}'
    elif word & 0xe000 == 0x6000:
        load = bool(word & 0x800);width = 1 if word & 0x1000 else 4
        address = f'{r(rs)}+{((word>>6)&31)*width}u'
    elif word & 0xf000 == 0x8000:
        load = bool(word & 0x800);width = 2
        address = f'{r(rs)}+{((word>>6)&31)*2}u'
    elif word & 0xf000 == 0x9000:
        load = bool(word & 0x800);rd = (word >> 8) & 7
        address = f's.r[13]+{(word&255)*4}u'
    if address is not None:
        if load:return Lowered(f'G value=0;if(!guest_load(s,h,{address},{width},value,{str(signed).lower()}))return G_MEMORY;s.r[{rd}]=value;')
        return Lowered(f'if(!guest_store(s,h,{address},{width},{r(rd)}))return G_MEMORY;')
    if word & 0xf000 == 0xa000:
        base = 's.r[13]' if word & 0x800 else f'0x{(pc+4)&~3:08x}u'
        return Lowered(f's.r[{(word>>8)&7}]={base}+{(word&255)*4}u;')
    if word & 0xff00 == 0xb000:
        return Lowered('s.r[13]'+('-=' if word & 0x80 else '+=')+f'{(word&127)*4}u;')
    if word & 0xf600 == 0xb400:
        load = bool(word & 0x800)
        registers = [n for n in range(8) if word & (1 << n)]
        if word & 0x100:registers.append(15 if load else 14)
        if not registers:raise Unsupported('Empty Thumb stack transfer')
        body = 'G base=s.r[13];G value=0;'
        if not load:body += f'base-={len(registers)*4}u;'
        for index, number in enumerate(registers):
            if load:
                body += f'if(!guest_load(s,h,base+{index*4}u,4,value))return G_MEMORY;'
                body += f's.r[{number}]=value;' if number != 15 else f'guest_pc(s,value,{str(core==9).lower()});'
            else:body += f'if(!guest_store(s,h,base+{index*4}u,4,{r(number)}))return G_MEMORY;'
        body += f's.r[13]=base+{len(registers)*4}u;' if load else 's.r[13]=base;'
        return Lowered(body, (), load and 15 in registers, False, load and 15 in registers)
    if word & 0xf000 == 0xc000:
        base, load = (word >> 8) & 7, bool(word & 0x800)
        registers = [n for n in range(8) if word & (1 << n)]
        if not registers or not load and base in registers and base != registers[0]:raise Unsupported('Unsupported Thumb base-in-list transfer')
        body = f'G base=s.r[{base}];G value=0;'
        for index, number in enumerate(registers):
            if load:body += f'if(!guest_load(s,h,base+{index*4}u,4,value))return G_MEMORY;s.r[{number}]=value;'
            else:body += f'if(!guest_store(s,h,base+{index*4}u,4,{r(number)}))return G_MEMORY;'
        if not load or base not in registers:body += f's.r[{base}]=base+{len(registers)*4}u;'
        return Lowered(body)
    if word & 0xf000 == 0xd000:
        condition = (word >> 8) & 15
        if condition == 14:raise Unsupported('Undefined Thumb condition')
        if condition == 15:return Lowered(f'G result=0;if(!h.system(h.context,&s,0x{0x3000000|(word&255):08x}u,0,&result))return G_SYSTEM;if(result==G_YIELD){{s.r[15]=0x{pc+2:08x}u;return G_YIELD;}}')
        offset = word & 255
        if offset & 128:offset -= 256
        target = (pc+4+offset*2) & 0xffffffff
        return Lowered(f'if(guest_condition(s,{condition}))s.r[15]=0x{target:08x}u;else s.r[15]=0x{pc+2:08x}u;', ((target,True),), True, True)
    if word & 0xf800 == 0xe000:
        offset = word & 2047
        if offset & 1024:offset -= 2048
        target = (pc+4+offset*2) & 0xffffffff
        return Lowered(f's.r[15]=0x{target:08x}u;', ((target,True),), True)
    if word & 0xf800 == 0xf000:
        offset = word & 2047
        if offset & 1024:offset -= 2048
        return Lowered(f's.r[14]=0x{(pc+4+offset*4096)&0xffffffff:08x}u;')
    if word & 0xf800 in (0xf800,0xe800):
        exchange = word & 0xf800 == 0xe800
        if exchange and (core != 9 or word & 1):raise Unsupported('Invalid Thumb BLX suffix')
        body = f'G target=s.r[14]+{(word&2047)*2}u;s.r[14]=0x{(pc+2)|1:08x}u;'
        body += 'guest_pc(s,target&~3u,true);' if exchange else 'guest_pc(s,target|1u,true);'
        return Lowered(body, (), True, False, True)
    raise Unsupported(f'Unsupported Thumb encoding {word:04x}')

def emit(instructions, core, name, *, guarded=False):
    if core not in (7,9) or not name.replace('_','').isalnum():raise ValueError('Invalid compilation target')
    out = [f'extern "C" G {name}(GuestCpu *cpu,const GuestHost *host) {{',
        'if(!cpu||!host||host->abi!=GUEST_ABI||host->bytes!=sizeof(GuestHost)||!host->read||!host->write||!host->system)return G_BAD_ABI;',
        f'auto &s=*cpu;const auto &h=*host;if(s.core!={core}u||!(s.cpsr&32u))return G_UNMAPPED;',
        'dispatch:switch(s.r[15]) {']
    for pc in sorted(instructions):
        if pc & 1:raise ValueError('Unaligned Thumb code address')
        out.append(f'case 0x{pc:08x}u:goto L{pc:08x};')
    out += ['default:return G_UNMAPPED;}']
    def continuation(pc):return f'goto L{pc:08x};' if pc in instructions else f's.r[15]=0x{pc:08x}u;return G_UNMAPPED;'
    for pc, word in sorted(instructions.items()):
        instruction = lower(word, pc, core)
        guard=f'G guard=0;if(!h.read(h.context,s.core,0x{pc:08x}u,2u,&guard)||guard!=0x{word:04x}u)return G_UNMAPPED;' if guarded else ''
        out.append(f'L{pc:08x}:{{s.r[15]=0x{pc:08x}u;if(!s.budget)return G_YIELD;'+guard+'--s.budget;')
        factor=1 if core==9 else 2
        cost=3 if instruction.terminates else 1
        if word&0xf000 in (0x5000,0x6000,0x7000,0x8000,0x9000) or word&0xf800==0x4800:cost=3
        if word&0xf600==0xb400:cost=(word&255).bit_count()+int(bool(word&0x100))+2
        if word&0xf000==0xc000:cost=(word&255).bit_count()+2
        out.append(f's.ticks+={cost*factor}u;')
        out.append(instruction.body)
        out.append('if(!(s.cpsr&32u))return G_TRANSFER;goto dispatch;' if instruction.terminates else continuation(pc+2))
        out.append('}')
    return '\n'.join(out+['}'])+'\n'
