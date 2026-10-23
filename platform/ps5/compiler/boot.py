# SPDX-License-Identifier: GPL-3.0-or-later
"""Private A32 boot-code discovery and native C++ generation.

This is a development stage, not a playable-game compiler. Unsupported code and
indirect targets remain explicit stopping points; CPU fallback is never used.
"""
from collections import deque
from pathlib import Path
import json
import struct
from . import arm, thumb
from .aot import Unsupported

def initializer_targets(image, record):
    """Explicit constructor pointers from verified container metadata, not guesses."""
    base=record['address'];begin,end=record.get('initializer_range',(base,base))
    if begin==end:return []
    if begin&3 or end&3 or not base<=begin<=end<=base+len(image):
        raise ValueError('Invalid private overlay initializer table')
    targets=[]
    for at in range(begin-base,end-base,4):
        value=struct.unpack_from('<I',image,at)[0]
        if value in (0,0xffffffff):continue
        mode=bool(value&1);pc=value&~1
        if not mode and pc&3:raise ValueError('Unaligned private overlay initializer')
        targets.append((pc,mode))
    return targets

def image_ranges(image,description):
    ranges=[(description['ram_address'],image)]
    for section in description['sections']:
        if not section['implicit']:
            start=section['image_offset'];ranges.insert(0,(section['address'],image[start:start+section['bytes']]))
    return ranges

def external_entry_candidates(data,image,description):
    """Conservative literal callbacks into an independently verified main image."""
    ranges=image_ranges(image,description);found=set();core=description['core']
    for at in range(0,len(data)-3,4):
        pointer=struct.unpack_from('<I',data,at)[0];mode=bool(pointer&1);pc=pointer&~1;width=2 if mode else 4
        if not mode and pc&3:continue
        for begin,buffer in ranges:
            if not begin<=pc or pc+width>begin+len(buffer):continue
            first=struct.unpack_from('<H' if mode else '<I',buffer,pc-begin)[0]
            if (mode and first in (0,0xffff)) or (not mode and first>>28!=14):break
            try:(thumb if mode else arm).lower(first,pc,core)
            except Unsupported:break
            found.add((pc,mode));break
    return sorted(found)

def discover(image, description, *, limit=12000, seeds=(), pointer_candidates=False,prologue_candidates=False):
    base = description['ram_address']
    ranges = image_ranges(image,description)
    def read(address, width=4):
        for begin, data in ranges:
            if begin <= address and address+width <= begin+len(data):
                return struct.unpack_from('<H' if width==2 else '<I', data, address-begin)[0]
        return None
    core = description['core']
    candidates=set()
    if pointer_candidates:
        # Optional static over-approximation of literal function pointers. A candidate
        # is not proof of code/data classification; missing runtime targets still stop.
        for _, data in ranges:
            for at in range(0,len(data)-3,4):
                value=struct.unpack_from('<I',data,at)[0]
                mode=bool(value&1);target=value&~1
                if not mode and target&3:continue
                first=read(target,2 if mode else 4)
                if first is None:continue
                if mode:
                    if first in (0,0xffff):continue
                elif first>>28!=14:continue
                try:(thumb if mode else arm).lower(first,target,core)
                except Unsupported:continue
                candidates.add((target,mode))
    pointer_count=len(candidates)
    if prologue_candidates:
        # Optional function-entry hypotheses, not proof of code/data
        # classification or complete reachability.
        for begin,data in ranges:
            for at in range(0,len(data)-1,2):
                first=struct.unpack_from('<H',data,at)[0]
                if first&0xfe00==0xb400 and first&0x1ff:candidates.add((begin+at,True))
                if at+4<=len(data):
                    second=struct.unpack_from('<H',data,at+2)[0]
                    literal=first&0xf800==0x4800
                    veneer=literal and second&0xff87 in (0x4700,0x4780) and (second>>3)&15==(first>>8)&7
                    leaf=second==0x4770 and (literal or first&0xf800==0x2000)
                    if veneer or leaf:candidates.add((begin+at,True))
                    if veneer:
                        for distance in range(2,min(16,at)+1,2):
                            prefix=struct.unpack_from('<H',data,at-distance)[0]
                            if prefix&0xfe00!=0x1c00 and prefix&0xf800 not in (0x6800,0x7800,0x8800):break
                            candidates.add((begin+at-distance,True))
                    if literal:
                        # Tail wrappers can load their destination, set up a few
                        # immediate arguments, then branch through that register.
                        # These remain optional entry hypotheses, not a claim
                        # that every matching byte sequence is executable code.
                        destination=(first>>8)&7
                        for distance in range(2,18,2):
                            if at+distance+2>len(data):break
                            following=struct.unpack_from('<H',data,at+distance)[0]
                            if following&0xff87 in (0x4700,0x4780):
                                if (following>>3)&15==destination:
                                    candidates.add((begin+at,True))
                                break
                            immediate=following&0xf800==0x2000 and (following>>8)&7!=destination
                            kind=following&0xf800
                            argument_load=(kind in (0x6800,0x7800,0x8800) and following&7!=destination) or (
                                kind==0x4800 and (following>>8)&7!=destination)
                            argument_math=(following&0xf800==0x0000 or following&0xfe00==0x1c00) and following&7!=destination
                            if not immediate and not argument_load and not argument_math:break
                if first==0x4770:
                    # Small leaf callbacks can begin with shifts or loads and
                    # have no stack prologue or literal function pointer.
                    for previous in range(at-2,max(-1,at-66),-2):
                        word=struct.unpack_from('<H',data,previous)[0]
                        if word in (0,0xffff):break
                        try:decoded=thumb.lower(word,begin+previous,core)
                        except Unsupported:break
                        if decoded.terminates and not decoded.conditional:break
                        if (begin+previous)&3==0:candidates.add((begin+previous,True))
            for at in range(0,len(data)-3,4):
                first=struct.unpack_from('<I',data,at)[0]
                if (first&0xffff0000==0xe92d0000 and first&65535) or first==0xe52de004:candidates.add((begin+at,False))
                if first in (0xe12fff1e,0xe1a0f00e):
                    for previous in range(at-4,max(-1,at-68),-4):
                        word=struct.unpack_from('<I',data,previous)[0]
                        if word in (0,0xffffffff):break
                        try:decoded=arm.lower(word,begin+previous,core)
                        except Unsupported:break
                        if decoded.terminates and not decoded.conditional:break
                        candidates.add((begin+previous,False))
    prologue_count=len(candidates)-pointer_count
    entry=description.get('entry')
    queue = deque(([(entry,description.get('entry_thumb',False))] if entry is not None else [])+list(seeds))
    optional_roots=deque(sorted(candidates,key=lambda item:(item[1],item[0])))
    seen, instructions, unsupported, indirect = set(), {False:{},True:{}}, {}, []
    external=set()
    while (queue or optional_roots) and len(seen) < limit:
        if not queue:queue.append(optional_roots.popleft())
        pc, mode = queue.popleft()
        if (pc,mode) in seen or pc & (1 if mode else 3):
            continue
        seen.add((pc,mode))
        word = read(pc, 2 if mode else 4)
        if word is None:
            external.add((pc,mode))
            continue
        try:
            instruction = (thumb if mode else arm).lower(word, pc, core)
        except Unsupported as error:
            unsupported[(pc,mode)] = str(error)
            continue
        instructions[mode][pc] = word
        for target, target_mode in instruction.targets:
            queue.append((target,target_mode))
        if instruction.dynamic:
            indirect.append((pc,mode))
            if mode and word&0xff87==0x4487:
                # Bounded Thumb switch: double an index, add PC, fetch a signed
                # relative halfword, then ADD pc,offset. Decode only on the PC.
                register=(word>>3)&15
                previous=[read(pc-offset,2) for offset in (2,4,6,8,10,12,14,16)]
                if register<8 and all(value is not None for value in previous[:7]):
                    asr,lsl,load,base,index,default,check,compare=previous
                    source=(index>>3)&7
                    signed_bounds=False
                    guarded=(compare is not None and default&0xf800==0xe000 and check==0xd900 and compare&0xff00==(0x2800|(source<<8)))
                    if default&0xff00==0xd800 and check&0xff00==(0x2800|(source<<8)):
                        guarded=True;compare=check
                    # A compiler can call its out-of-range assertion instead of
                    # using a plain default branch. Enumerate the explicitly
                    # compared domain; this does not assume the call cannot return.
                    bound=read(pc-18,2)
                    if (check&0xf800==0xf000 and default&0xf800 in (0xf800,0xe800) and
                        compare is not None and compare==0xd901 and bound is not None and
                        bound&0xff00==(0x2800|(source<<8))):
                        guarded=True;compare=bound
                    # Signed upper/lower guards, including a minus-one sentinel
                    # biased to table slot zero. Both branches must skip the table.
                    if (bound is not None and default&0xff00==0xdb00 and
                        check==(0x2800|(source<<8)) and compare is not None and
                        compare&0xff00==0xdc00 and bound&0xff00==(0x2800|(source<<8))):
                        guarded=signed_bounds=True;compare=bound
                    original=(check>>3)&7
                    if (bound is not None and default&0xff00==0xd400 and
                        check&0xfe00==0x1c00 and (check>>6)&7==1 and check&7==source and
                        compare is not None and compare&0xff00==0xdc00 and
                        bound&0xff00==(0x2800|(original<<8)) and bound&255<255):
                        guarded=signed_bounds=True;compare=(bound&255)+1
                    if signed_bounds:
                        end=pc-4+((load>>6)&31)*2+((compare&255)+1)*2
                        displacement=lambda value: (value&255)-256 if value&128 else value&255
                        guarded=(pc-12+2*displacement(previous[7])>=end and
                                 pc-8+2*displacement(default)>=end)
                    valid=(asr==(0x1400|(register<<3)|register) and lsl==(0x0400|(register<<3)|register) and
                        load&0xf83f==(0x8800|(register<<3)|register) and base==(0x4478|register) and
                        index&0xfe07==(0x1800|register) and (index>>6)&7==source and guarded)
                    if valid:
                        table=pc-4+((load>>6)&31)*2
                        cases=[]
                        for slot in range((compare&255)+1):
                            relative=read(table+slot*2,2)
                            if relative is None or relative&1:break
                            if relative&0x8000:relative-=0x10000
                            target=(pc+4+relative)&0xffffffff
                            if read(target,2) is None:break
                            cases.append((target,True))
                        else:
                            queue.extend(cases)
            if not mode and word & 0x0ffffff0 == 0x008ff100:
                # Native switch: ADD pc,pc,Rm,LSL #2, then a table of A32 B slots.
                for offset in range(0,256*4,4):
                    slot = pc+8+offset
                    branch = read(slot)
                    if branch is None or branch & 0xff000000 != 0xea000000:
                        break
                    queue.append((slot,False))
                # An unrolled arithmetic loop can use the same computed-PC form
                # without B slots. Recognize repeated ADC/SUBCC/ADC groups, then
                # follow their ordinary instruction fallthrough statically.
                group=[read(pc+8+offset) for offset in range(0,12,4)]
                second=[read(pc+20+offset) for offset in range(0,12,4)]
                if all(value is not None for value in group) and group==second and (
                    group[0]&0xfff00000==0xe0b00000 and group[1]&0xfff00000==0x30400000 and group[2]&0xfff00000==0xe0b00000):
                    queue.append((pc+8,False))
            if not mode and word & 0x0ffffff0 in (0x012fff10,0x012fff30):
                # An adjacent literal-load veneer supplies an additional static target.
                previous = read(pc-4)
                if previous is not None and previous & 0x0f7f0000 == 0x051f0000 and (previous>>12)&15 == word&15:
                    literal = pc+4+(previous&4095)*(1 if previous&0x800000 else -1)
                    target = read(literal)
                    if target is not None and read(target&~3) is not None:
                        queue.append((target&(~1 if target&1 else ~3),bool(target&1)))
        # BL has a statically known continuation even though execution transfers.
        call = (word & 0xf800 in (0xf800,0xe800) or word & 0xff87==0x4780) if mode else (
            word & 0x0f000000 == 0x0b000000 or word & 0x0ffffff0 == 0x012fff30 or word & 0xfe000000==0xfa000000)
        if not mode and word&0x0ffffff0==0x012fff10:
            previous=read(pc-4)
            if previous is not None and previous&0x0fffffff==0x01a0e00f:call=True
        if mode and call:
            previous = read(pc-2,2)
            if previous is not None and previous & 0xf800 == 0xf000:
                displacement = previous & 2047
                if displacement & 1024:displacement -= 2048
                target = (pc+2+displacement*4096+(word&2047)*2)&0xffffffff
                exchange = word & 0xf800 == 0xe800
                queue.append((target&~3 if exchange else target,not exchange))
        if not instruction.terminates or instruction.conditional or call:
            queue.append((pc+(2 if mode else 4),mode))
    return instructions, {'core': core, 'a32_instructions': len(instructions[False]), 'thumb_instructions':len(instructions[True]),
        'unsupported': {f'{p:08x}/{"T" if t else "A"}':v for (p,t),v in sorted(unsupported.items())},
        'indirect_sites': sorted(indirect), 'pending_addresses': list(queue)+list(optional_roots),
        'external_targets':sorted(external),
        'static_pointer_candidates':pointer_count,
        'static_prologue_candidates':prologue_count,
        'instruction_discovery_limit':limit,
        'complete_translation': False}

def cross_image_entries(images, *, limit=12000, pointer_candidates=False, prologue_candidates=False):
    """Close proven external branch targets across independently mapped images."""
    roots=[set(map(tuple,entries)) for image,description,entries in images]
    results=[discover(image,description,limit=limit,seeds=sorted(entries),pointer_candidates=pointer_candidates,prologue_candidates=prologue_candidates or description.get('cross_prologue_candidates',False))[1]
             for (image,description,_),entries in zip(images,roots)]
    for iteration in range(32):
        if any(info['pending_addresses'] for info in results):raise ValueError('Cross-image instruction discovery bound reached')
        targets={tuple(target) for info in results for target in info['external_targets']}
        changed=False
        for n,(image,description,_) in enumerate(images):
            ranges=image_ranges(image,description)
            added={t for t in targets if any(begin<=t[0] and t[0]+(2 if t[1] else 4)<=begin+len(data) for begin,data in ranges)}-roots[n]
            if not added:continue
            changed=True;roots[n].update(added)
            results[n]=discover(image,description,limit=limit,seeds=sorted(roots[n]),pointer_candidates=pointer_candidates,prologue_candidates=prologue_candidates or description.get('cross_prologue_candidates',False))[1]
        if not changed:return roots
    raise ValueError('Cross-image discovery convergence bound reached')

def generate(folder, *, limit=12000, seeds=None, output_name='native-boot', pointer_candidates=False,all_overlays=False, bootstrap_guards=()):
    folder = Path(folder).resolve()
    manifest = json.loads((folder/'manifest.json').read_text())
    if manifest.get('public_distribution_allowed') is not False or not manifest.get('contains_user_rom_data'):
        raise ValueError('Expected marked private ROM preparation')
    if seeds is None and (folder/'boot-seeds.json').is_file():
        seeds=json.loads((folder/'boot-seeds.json').read_text())
    overlay_seeds={}
    if (folder/'overlay-seeds.json').is_file():
        overlay_seeds=json.loads((folder/'overlay-seeds.json').read_text())
        if not isinstance(overlay_seeds,dict) or any(k not in ('arm9','arm7') or not isinstance(v,dict) for k,v in overlay_seeds.items()):
            raise ValueError('Invalid private overlay seed catalog')
    for cpu, entries in (seeds or {}).items():
        if cpu not in ('arm9','arm7') or any(not isinstance(p,int) or not 0<=p<=0xffffffff or not isinstance(t,bool) for p,t in entries):
            raise ValueError('Invalid private native-branch seeds')
    if not output_name.startswith('native-boot') or not output_name.replace('-','').isalnum():
        raise ValueError('Invalid private output name')
    output = folder/output_name
    if output.exists():
        raise ValueError('Native boot output already exists')
    bootstrap_guards=set(bootstrap_guards)
    if any(core not in (7,9) or not isinstance(overlay,int) or overlay<0 for core,overlay in bootstrap_guards):
        raise ValueError('Invalid static initialization guard profile')
    sources, summaries = {}, {}
    catalog_sources=['#include "overlay_types.h"\n'];catalog_entries=[]
    def emit_pages(name,core,instructions,guarded=False):
        pages={}
        for mode,words in instructions.items():
            for address,word in words.items():pages.setdefault((mode,address&~4095),{})[address]=word
        prototypes,routes=[],[]
        for (mode,page),words in sorted(pages.items()):
            function=f'oq_{name}_{"t" if mode else "a"}{page:08x}'
            sources[f'{function}.cpp']='#include "guest.h"\n'+(thumb if mode else arm).emit(words,core,function,guarded=guarded)
            prototypes.append(f'extern "C" G {function}(GuestCpu*,const GuestHost*);')
            routes.append(f'case 0x{page|int(mode):08x}u:return {function}(cpu,host);')
        router='switch((cpu->r[15]&~4095u)|((cpu->cpsr>>5)&1)){'+''.join(routes)+'default:break;}\n'
        return prototypes,router,len(pages)
    for name, core in [('arm9',9), ('arm7',7)]:
        description = dict(manifest['processors'][name], core=core)
        source = (folder/description['unpacked_file']).resolve()
        if folder not in source.parents:
            raise ValueError('Private input path escapes preparation folder')
        image = source.read_bytes()
        from .prepare import digest
        if digest(image) != description['sha256']:
            raise ValueError('Prepared image changed')
        prefix = '#include "guest.h"\n'
        overlay_prototypes=[];main_seeds=list((seeds or {}).get(name,()))
        overlay_routes=[];overlay_summaries={}
        selected=dict(overlay_seeds.get(name,{}))
        if all_overlays:
            for record in description['overlays']:selected.setdefault(str(record['id']),[])
        # Propagate direct calls before emitting any image. This also covers
        # single-return functions which have no prologue or literal pointer.
        cross_images=[];cross_ids=[];main_cross_seeds=list(main_seeds)
        from .static_variants import load as load_cross_variant
        for text_id,entries in selected.items():
            if not text_id.isdecimal() or str(int(text_id))!=text_id or not isinstance(entries,list) or (not entries and not all_overlays):raise ValueError('Invalid private overlay seeds')
            record=next((o for o in description['overlays'] if o['id']==int(text_id)),None)
            if record is None:raise ValueError('Unknown private overlay ID')
            path=(folder/record['file']).resolve()
            if folder not in path.parents:raise ValueError('Private overlay path escapes preparation')
            data=path.read_bytes()
            if len(data)!=record['bytes'] or digest(data)!=record['sha256']:raise ValueError('Prepared overlay changed')
            initializers=initializer_targets(data,record)
            local=[*entries,*[(pc,mode) for pc,mode in initializers if record['address']<=pc and pc+(2 if mode else 4)<=record['address']+len(data)]]
            main_ranges=image_ranges(image,description)
            main_cross_seeds.extend(t for t in initializers if any(a<=t[0] and t[0]+(2 if t[1] else 4)<=a+len(b) for a,b in main_ranges))
            variant=load_cross_variant(folder,core,record)
            for view in [data]+(variant[2]['loaded_states'] if variant else []):
                desc={'ram_address':record['address'],'entry':None,'sections':[],'core':core,'cross_prologue_candidates':bool(variant)}
                cross_images.append((view,desc,local));cross_ids.append(text_id)
                if pointer_candidates or all_overlays:main_cross_seeds.extend(external_entry_candidates(view,image,description))
        closure=cross_image_entries([(image,description,main_cross_seeds),*cross_images],limit=limit,pointer_candidates=pointer_candidates or all_overlays,prologue_candidates=pointer_candidates or all_overlays)
        main_seeds=list(closure[0]);cross_roots={key:set() for key in selected}
        for key,roots in zip(cross_ids,closure[1:]):cross_roots[key].update(roots)
        for text_id,entries in selected.items():
            if not text_id.isdecimal() or str(int(text_id))!=text_id or not isinstance(entries,list) or (not entries and not all_overlays):raise ValueError('Invalid private overlay seeds')
            record=next((o for o in description['overlays'] if o['id']==int(text_id)),None)
            if record is None:raise ValueError('Unknown private overlay ID')
            for pc,mode in entries:
                if not isinstance(pc,int) or not isinstance(mode,bool) or not record['address']<=pc<record['address']+record['bytes']:
                    raise ValueError('Private overlay seed is outside its image')
            path=(folder/record['file']).resolve()
            if folder not in path.parents:raise ValueError('Private overlay path escapes preparation')
            data=path.read_bytes()
            if len(data)!=record['bytes'] or digest(data)!=record['sha256']:raise ValueError('Prepared overlay changed')
            from .static_variants import load as load_variant
            variant=load_variant(folder,core,record)
            guarded_initialization=bool(variant) or (core,record['id']) in bootstrap_guards
            initializers=initializer_targets(data,record)
            entries=[*entries,*[(pc,mode) for pc,mode in initializers if record['address']<=pc and pc+(2 if mode else 4)<=record['address']+len(data)]]
            entries=sorted(set(map(tuple,entries))|cross_roots[text_id])
            desc={'ram_address':record['address'],'entry':entries[0][0] if entries else None,'entry_thumb':entries[0][1] if entries else False,'sections':[],'core':core}
            main_ranges=[(description['ram_address'],len(image))]+[(s['address'],s['bytes']) for s in description['sections']]
            label=f'{name}_overlay{record["id"]}'
            views=[data]+(variant[2]['loaded_states'] if variant else [])
            spans=[];view_info=[];variant_functions=[];pages=0
            for number,view in enumerate(views):
                words,info=discover(view,desc,limit=limit,seeds=entries,pointer_candidates=pointer_candidates or all_overlays,prologue_candidates=pointer_candidates or all_overlays or bool(variant))
                external_literals=external_entry_candidates(view,image,description) if pointer_candidates or all_overlays else []
                main_seeds.extend(external_literals);info['external_literal_candidates']=external_literals
                main_seeds.extend((pc,mode) for pc,mode in [*info['external_targets'],*initializers] if any(a<=pc and pc+(2 if mode else 4)<=a+n for a,n in main_ranges))
                tag=f'{label}_state{number}' if variant else label
                declarations,body,page_count=emit_pages(tag,core,words,guarded=guarded_initialization);pages+=page_count
                sources[f'{tag}.cpp']=prefix+'\n'.join(declarations)+f'\nextern "C" G oq_boot_{tag}(GuestCpu *cpu,const GuestHost *host){{'+body+'return G_UNMAPPED;}\n'
                variant_functions.append(f'oq_boot_{tag}')
                for mode,instructions_for_mode in words.items():
                    for pc in instructions_for_mode:spans.append((pc,pc+(2 if mode else 4)))
                view_info.append(dict(info,image_sha256=digest(view)))
            if variant:
                wrappers='\n'.join(f'extern "C" G {fn}(GuestCpu*,const GuestHost*);' for fn in variant_functions)
                wrappers+=f'\nextern "C" G oq_boot_{label}(GuestCpu *cpu,const GuestHost *host){{G before=cpu->budget,status;'
                for fn in reversed(variant_functions):wrappers+=f'status={fn}(cpu,host);if(status!=G_UNMAPPED||cpu->budget!=before)return status;'
                sources[f'{label}.cpp']=prefix+wrappers+'return G_UNMAPPED;}\n'
            info=dict(view_info[-1]);info['explicit_initializer_targets']=initializers
            if variant:info['static_states']=view_info
            overlay_prototypes.append(f'extern "C" G oq_boot_{label}(GuestCpu*,const GuestHost*);')
            overlay_routes.append(f'case {record["id"]}u:return oq_boot_{label}(cpu,host);')
            merged=[]
            for begin,end in sorted(spans):
                if merged and begin<=merged[-1][1]:merged[-1]=(merged[-1][0],max(end,merged[-1][1]))
                else:merged.append((begin,end))
            if not merged:
                if not all_overlays:raise ValueError('No supported overlay code discovered')
                # A data-only overlay needs no native entry. Never invent an
                # entry at its first byte simply to populate the catalog.
                overlay_prototypes.pop();overlay_routes.pop()
                overlay_summaries[text_id]=dict(info,pages=pages,address=record['address'],bytes=record['bytes'],native_entry=False)
                continue
            catalog_sources.append(f'static const NativeCodeRange ranges_{label}[]={{'+','.join(f'{{{a}u,{b}u}}' for a,b in merged)+'};\n')
            signature=','.join(str(b) for b in bytes.fromhex(record['sha256']))
            catalog_entries.append(f'{{{core}u,{record["id"]}u,{record["address"]}u,{record["bytes"]}u,{{{signature}}},ranges_{label},{len(merged)}u,{int(guarded_initialization)}u}}')
            overlay_summaries[text_id]=dict(info,pages=pages,address=record['address'],bytes=record['bytes'])
        instructions, summary = discover(image, description, limit=limit, seeds=main_seeds,pointer_candidates=pointer_candidates or all_overlays,prologue_candidates=pointer_candidates or all_overlays)
        prototypes,router,page_count=emit_pages(name,core,instructions)
        prototypes.extend(overlay_prototypes)
        summaries[name] = dict(summary, pages=page_count,overlays=overlay_summaries,all_overlays=all_overlays)
        sources[f'{name}.cpp'] = prefix+'\n'.join(prototypes)+f'\nextern "C" G oq_boot_{name}(GuestCpu *cpu,const GuestHost *host) {{\n'
        sources[f'{name}.cpp']+=f'if(!cpu||!host||host->abi!=GUEST_ABI||host->bytes!=sizeof(GuestHost)||!host->system||cpu->core!={core}u)return G_BAD_ABI;\n'+router
        if overlay_routes:
            sources[f'{name}.cpp']+='G id=0;if(!host->system(host->context,cpu,0x04000000u,cpu->r[15],&id))return G_UNMAPPED;switch(id){'+''.join(overlay_routes)+'default:break;}\n'
        sources[f'{name}.cpp']+='return G_UNMAPPED;}\n'
    if catalog_entries:
        catalog_sources.append('static const NativeOverlayInfo info[]={'+','.join(catalog_entries)+'};\n')
        catalog_sources.append(f'extern "C" const NativeOverlayInfo *oq_boot_overlay_catalog(unsigned *count){{*count={len(catalog_entries)};return info;}}\n')
    else:catalog_sources.append('extern "C" const NativeOverlayInfo *oq_boot_overlay_catalog(unsigned *count){*count=0;return nullptr;}\n')
    sources['overlay_catalog.cpp']=''.join(catalog_sources)
    output.mkdir()
    for name, source in sources.items():
        (output/name).write_text(source)
    (output/'analysis.json').write_text(json.dumps(summaries, indent=2)+'\n')
    return {'output':str(output), 'sources':len(sources), 'processors':summaries,
            'contains_game_derived_code':True,'public_distribution_allowed':False,'playable':False}
