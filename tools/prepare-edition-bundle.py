"""Prepare private, exact-identity cartridge media; never infer identity from a filename."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image

def sha(data):
    return hashlib.sha256(data).digest()

def prepare(rom_path,module_path,label_path,output,audio_path=None,refs_path=None,metadata_only=False):
    if output.exists():
        raise ValueError('Refusing to overwrite a preparation')
    if not 1024*1024 <= rom_path.stat().st_size <= 0x20000000:
        raise ValueError('ROM size outside limits')
    rom=rom_path.read_bytes()
    if len(rom)&(len(rom)-1) or rom[12:16] not in (b'IPKD',b'IPGD',b'IPKE',b'IPGE') or rom[16:18]!=b'01' or rom[30]:
        raise ValueError('Unsupported exact edition/revision')
    identity=rom[12:16]+rom[16:18]+rom[30:31]+b'\0'+sha(rom)
    if not 256 <= module_path.stat().st_size <= 256+256*1024*1024+1048576*16:
        raise ValueError('Module size outside limits')
    module=module_path.read_bytes()
    length,relocs=struct.unpack_from('<QQ',module,24)
    if module[:8]!=b'OQGAME03' or struct.unpack_from('<4I',module,8)!=(3,62,256,1) or len(module)!=256+length+relocs*16:
        raise ValueError('Invalid native module envelope')
    if module[88:128]!=identity:
        raise ValueError('Native module belongs to another ROM')
    if sha(module[:128]+bytes(32)+module[160:])!=module[128:160]:
        raise ValueError('Native module integrity failure')
    with Image.open(label_path) as im:
        # Use the actual title framebuffer; preserve the original source separately.
        if im.size!=(256,192):
            raise ValueError('Expected an original 256x192 title framebuffer')
        label=im.convert('RGBA').tobytes()
    optional={}
    for name,path,magic,limit in [('title-menu.pcm',audio_path,b'APAU0001',64*1024*1024),('identity-refs.bin',refs_path,b'APRF0001',64*1024*1024)]:
        if path is None:continue
        if path.stat().st_size>limit:raise ValueError('Private media exceeds limit')
        data=path.read_bytes()
        if data[:8]!=magic:raise ValueError('Private media header missing')
        at=32 if name=='title-menu.pcm' else 16
        if len(data)<at+32 or data[at:at+32]!=identity[8:]:raise ValueError('Private media belongs to another ROM')
        optional[name]=data
    manifest=struct.pack('<8sII',b'APED0001',256,len(rom))+identity+sha(module)+sha(label)
    manifest+=sha(optional['title-menu.pcm']) if 'title-menu.pcm' in optional else bytes(32)
    manifest+=sha(optional['identity-refs.bin']) if 'identity-refs.bin' in optional else bytes(32)
    manifest+=bytes(256-len(manifest))
    output.mkdir(parents=True,exist_ok=False)
    (output/'edition.bin').write_bytes(manifest)
    (output/'title-label.rgba').write_bytes(label)
    if not metadata_only:
        (output/'game-data.bin').write_bytes(rom)
        (output/'game-203.oqgame').write_bytes(module)
        for name,data in optional.items():(output/name).write_bytes(data)
    record={'game_code':identity[:4].decode(),'rom_sha256':identity[8:].hex(),'module_sha256':sha(module).hex(),'label_sha256':sha(label).hex(),'metadata_only':metadata_only,'private':True,'title_source':str(label_path)}
    (output/'PRIVATE-PROVENANCE.json').write_text(json.dumps(record,indent=2)+'\n')
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','module','label','output'):p.add_argument(name,type=Path)
    p.add_argument('--audio',type=Path);p.add_argument('--refs',type=Path);p.add_argument('--metadata-only',action='store_true')
    a=p.parse_args();print(json.dumps(prepare(a.rom,a.module,a.label,a.output,a.audio,a.refs,a.metadata_only),indent=2))
