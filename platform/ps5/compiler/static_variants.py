# SPDX-License-Identifier: GPL-3.0-or-later
"""Private, precompiled variants of reproducible load-time code transforms.

No runtime instruction compiler is involved. Inputs must come from the owner's
verified ROM and an original native-PC initialization trace. Observing one
variant does not prove that all possible transformations have been discovered.
"""
from pathlib import Path
import hashlib,json

def sha(data):return hashlib.sha256(data).hexdigest()

def load(folder,core,record):
    folder=Path(folder).resolve();index=folder/'static-variants.json'
    if not index.exists():return None
    item=json.loads(index.read_text()).get('arm'+str(core),{}).get(str(record['id']))
    if item is None:return None
    if item['source_sha256']!=record['sha256']:raise ValueError('Static variant belongs to another original overlay')
    path=(folder/item['file']).resolve()
    if folder not in path.parents:raise ValueError('Static variant escapes private preparation')
    data=path.read_bytes()
    if len(data)!=record['bytes'] or sha(data)!=item['sha256']:raise ValueError('Private static variant changed')
    source=(folder/record['file']).read_bytes()
    if sha(source)!=record['sha256']:raise ValueError('Private original overlay changed')
    changes=[];rebuilt=bytearray(source)
    for entry in item['transforms']:
        begin,count,writer=entry['begin'],entry['bytes'],entry['writer']
        if not all(isinstance(v,int) for v in (begin,count,writer)) or begin<record['address'] or not count or count>record['address']+len(data)-begin or writer&1 or not 0<=writer<=0xffffffff:
            raise ValueError('Invalid private static transform bounds')
        at=begin-record['address'];before=bytes.fromhex(entry['before']);after=bytes.fromhex(entry['after'])
        if len(before)!=count or len(after)!=count or rebuilt[at:at+count]!=before:raise ValueError('Non-reproducible private transform sequence')
        rebuilt[at:at+count]=after
        changes.append((begin,count,writer,before,after,data[at:at+count]))
    if not changes:raise ValueError('Empty private static transform')
    if rebuilt!=data:raise ValueError('Unexplained private variant bytes')
    if any(begin<=writer<begin+count for begin,count,writer,_,_,_ in changes):raise ValueError('Transform rewrites its executing instruction')
    states=[]
    for state in [*item.get('states',[]),{'file':item['file'],'sha256':item['sha256']}]:
        path=(folder/state['file']).resolve()
        if folder not in path.parents:raise ValueError('Static state escapes private preparation')
        buffer=path.read_bytes()
        if len(buffer)!=record['bytes'] or sha(buffer)!=state['sha256']:raise ValueError('Private static state changed')
        if buffer not in states:states.append(buffer)
    item['loaded_states']=states
    return data,changes,item

def record_native_trace(folder,build,core,overlay_id):
    folder=Path(folder).resolve();build=Path(build).resolve()
    if build.parent!=folder or not build.name.startswith('native-boot'):raise ValueError('Expected private native initialization trace')
    manifest=json.loads((folder/'manifest.json').read_text())
    if manifest.get('public_distribution_allowed') is not False:raise ValueError('Expected explicitly private preparation')
    description=next(o for o in manifest['processors']['arm'+str(core)]['overlays'] if o['id']==overlay_id)
    source=(folder/description['file']).read_bytes()
    if sha(source)!=description['sha256']:raise ValueError('Original private overlay changed')
    trace=(build/'private-write-trace.csv').read_bytes();rows=[]
    for line in trace.decode('ascii').splitlines():
        row=tuple(int(s,16) for s in line.split(','))
        if len(row)!=6:raise ValueError('Malformed private write trace')
        cpu,address,width,writer,before,after=row
        if cpu in (7,9) and width in (1,2,4) and description['address']<=address and address+width<=description['address']+len(source):
            mask=(1<<(width*8))-1;rows.append((cpu,address,width,writer,before&mask,after&mask))
    if not rows:raise ValueError('No native load-time transform observed')
    memory=(build/f'after{core}-ram.bin').read_bytes();offset=description['address']-0x02000000
    final=memory[offset:offset+len(source)];reverse=bytearray(final);cut=None
    for n in range(len(rows)-1,-1,-1):
        _,address,width,_,before,after=rows[n];at=address-description['address']
        if int.from_bytes(reverse[at:at+width],'little')!=after:raise ValueError('Native replay differs from observed resulting memory')
        reverse[at:at+width]=before.to_bytes(width,'little')
        if reverse==source:cut=n;break
    if cut is None:raise ValueError('Native replay has no verified original-overlay checkpoint')
    rows=rows[cut:];image=bytearray(source);spans=[]
    for _,address,width,pc,before,after in rows:
        at=address-description['address']
        if int.from_bytes(image[at:at+width],'little')!=before:raise ValueError('Native transform differs from its preceding verified state')
        old=before.to_bytes(width,'little');new=after.to_bytes(width,'little');image[at:at+width]=new
        if spans and pc==spans[-1]['writer'] and address==spans[-1]['begin']+spans[-1]['bytes']:
            spans[-1]['bytes']+=width;spans[-1]['before']+=old.hex();spans[-1]['after']+=new.hex()
        else:spans.append({'begin':address,'bytes':width,'writer':pc,'before':old.hex(),'after':new.hex()})
    if final!=image:raise ValueError('Native initialization replay does not reproduce the resulting image')
    if any(s['begin']<=s['writer']<s['begin']+s['bytes'] for s in spans):raise ValueError('Transform rewrites an executing initializer')
    index=folder/'static-variants.json';catalog=json.loads(index.read_text()) if index.exists() else {}
    old=catalog.get('arm'+str(core),{}).get(str(overlay_id));states=[]
    if old:
        load(folder,core,description)
        for s in [*old.get('states',[]),{'file':old['file'],'sha256':old['sha256']}]:
            if s['sha256']!=sha(image) and not any(s['sha256']==v['sha256'] for v in states):states.append(s)
    if len(states)>=64:raise ValueError('Static variant limit reached; inspect the code transform')
    target=folder/f'arm{core}/overlay-{overlay_id}-static-{sha(image)[:20]}.bin';target.parent.mkdir(exist_ok=True);target.write_bytes(image)
    item={'source_sha256':description['sha256'],'sha256':sha(image),'file':target.relative_to(folder).as_posix(),
          'transforms':spans,'states':states,'provenance':{'method':'original native-PC initialization write trace','trace_sha256':sha(trace),
          'rom_sha256':manifest['identity']['sha256'],'native_analysis_sha256':sha((build/'analysis.json').read_bytes()),
          'observed_writes':len(rows),'runtime_compilation':False},'public_distribution_allowed':False}
    catalog.setdefault('arm'+str(core),{})[str(overlay_id)]=item;index.write_text(json.dumps(catalog,indent=2)+'\n')
    load(folder,core,description)
    return {'overlay':overlay_id,'native_transform_writes':len(rows),'static_transitions':len(spans),'sha256':sha(image),'public_distribution_allowed':False}
