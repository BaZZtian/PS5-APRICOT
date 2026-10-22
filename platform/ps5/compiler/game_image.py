# SPDX-License-Identifier: GPL-3.0-or-later
"""Package locally compiled PIC code; never distribute game-derived output.

The ELF is a link-time container only. No OS ELF loader, imports, CPU decoder,
interpreter, or runtime compiler is used by the player.
"""
import hashlib,struct
from pathlib import Path

MAX_IMAGE=256*1024*1024
HEADER_BYTES=256
EXPORTS=('oq_boot_arm9','oq_boot_arm7','oq_boot_overlay_catalog')

def package_elf(elf,identity,entries):
    elf=bytes(elf)
    def span(offset,size):
        if offset<0 or size<0 or offset>len(elf) or size>len(elf)-offset:
            raise ValueError('ELF record outside file')
        return elf[offset:offset+size]
    def unpack(fmt,offset):return struct.unpack(fmt,span(offset,struct.calcsize(fmt)))
    if len(elf)<64 or elf[:7]!=b'\x7fELF\x02\x01\x01' or any(elf[9:16]):
        raise ValueError('Expected ELF64 little-endian module')
    kind,machine,version,entry,phoff,shoff,flags,ehsize,phsize,phnum,shsize,shnum,shstr=unpack('<HHIQQQIHHHHHH',16)
    if (kind,machine,version,flags,ehsize,phsize,shsize)!=(3,62,1,0,64,56,64) or not 1<=phnum<=32 or not 1<=shnum<=65535 or shstr>=shnum:
        raise ValueError('Unsupported ELF header')
    segments=[]
    for n in range(phnum):
        typ,permissions,offset,address,physical,filesize,memsize,alignment=unpack('<IIQQQQQQ',phoff+n*phsize)
        if typ in (3,7):raise ValueError('ELF interpreter/TLS forbidden')
        if typ!=1:continue
        if not memsize or filesize>memsize or address>MAX_IMAGE or memsize>MAX_IMAGE-address or permissions&~7 or not permissions&4 or permissions&3==3:
            raise ValueError('Invalid load segment')
        span(offset,filesize)
        if alignment>1 and (alignment&(alignment-1) or address%alignment!=offset%alignment):raise ValueError('Invalid segment alignment')
        if any(address<end and address+memsize>begin for begin,end,_,_,_ in segments):raise ValueError('Overlapping load segments')
        segments.append((address,address+memsize,offset,filesize,permissions))
    if not segments or min(s[0] for s in segments)!=0:raise ValueError('Module link address must be zero')
    length=max(s[1] for s in segments)
    image=bytearray(length)
    for begin,end,offset,count,permissions in segments:image[begin:begin+count]=span(offset,count)
    def mapped(address,count,executable=None):
        return any(begin<=address and count<=end-address and (executable is None or bool(permissions&1)==executable)
                   for begin,end,_,_,permissions in segments)
    sections=[unpack('<IIQQQQIIQQ',shoff+n*shsize) for n in range(shnum)]
    names_section=sections[shstr]
    if names_section[1]!=3:raise ValueError('Missing section names')
    names=span(names_section[4],names_section[5])
    def string(table,offset):
        end=table.find(b'\0',offset)
        if not 0<=offset<len(table) or end<0:raise ValueError('Invalid ELF string')
        return table[offset:end].decode('ascii')
    symbols={};relocations=[]
    for section in sections:
        name,typ,attributes,address,offset,size,link,info,align,stride=section
        label=string(names,name)
        if attributes&0x400:raise ValueError('TLS sections forbidden')
        if label=='.relro_padding' and typ!=8:raise ValueError('Invalid RELRO padding')
        if attributes&3==3 and size and label not in ('.dynamic','.got','.got.plt','.data.rel.ro','.relro_padding'):
            raise ValueError('Mutable module globals forbidden: '+label)
        if typ==9 and size:raise ValueError('REL relocations forbidden')
        if typ in (2,11):
            if stride!=24 or size%24 or link>=shnum or sections[link][1]!=3:raise ValueError('Invalid symbol table')
            table=span(sections[link][4],sections[link][5])
            for at in range(offset,offset+size,24):
                n,properties,other,which,value,symbol_size=unpack('<IBBHQQ',at)
                symbol=string(table,n)
                if which==0 and symbol:raise ValueError('Undefined native symbol: '+symbol)
                if symbol in EXPORTS and which!=0:
                    if properties&15!=2 or not mapped(value,max(1,symbol_size),True):raise ValueError('Invalid native entry symbol')
                    if symbol in symbols and symbols[symbol]!=value:raise ValueError('Duplicate native export')
                    symbols[symbol]=value
        if typ==4:
            if stride!=24 or size%24:raise ValueError('Invalid RELA records')
            for at in range(offset,offset+size,24):
                target,relocation,addend=unpack('<QQq',at)
                if relocation!=8 or target%8 or not mapped(target,8,False) or addend<0 or not mapped(addend,1):
                    raise ValueError('Only internal aligned R_X86_64_RELATIVE data relocations supported')
                relocations.append((target,addend))
    if set(symbols)!=set(EXPORTS):raise ValueError('Missing native game exports')
    relocations.sort()
    if len(relocations)>1048576 or len({r[0] for r in relocations})!=len(relocations):raise ValueError('Invalid/duplicate relocation targets')
    code=identity['game_code'].encode('ascii');maker=identity['maker_code'].encode('ascii')
    digest=bytes.fromhex(identity['sha256']);revision=identity['revision']
    if len(code)!=4 or len(maker)!=2 or len(digest)!=32 or not 0<=revision<=255:raise ValueError('Invalid private ROM identity')
    if len(entries)!=2 or any(not isinstance(x,int) or not 0<=x<=0xffffffff or x&3 for x in entries):raise ValueError('Invalid ARM entry addresses')
    header=bytearray(HEADER_BYTES)
    struct.pack_into('<8sIIIIQQQQQIIIIII',header,0,b'OQGAME03',3,62,HEADER_BYTES,1,length,len(relocations),
                     *(symbols[name] for name in EXPORTS),*entries,2,200,40,0)
    header[88:96]=code+maker+bytes([revision,0]);header[96:128]=digest
    data=header+image+b''.join(struct.pack('<QQ',*r) for r in relocations)
    data[128:160]=hashlib.sha256(data).digest()
    return bytes(data),{'format':'orqenva-native-game-3','guest_abi':2,'architecture':'x86_64',
        'image_bytes':length,'relative_relocations':len(relocations),'entry_offsets':symbols,
        'sha256':hashlib.sha256(data).hexdigest(),'contains_game_derived_code':True,
        'public_distribution_allowed':False,'translation_complete':False,'playable':False}

def write_image(elf,output,identity,entries):
    output=Path(output)
    if output.exists():raise ValueError('Refusing to overwrite native image')
    data,report=package_elf(Path(elf).read_bytes(),identity,entries)
    output.write_bytes(data)
    return report
