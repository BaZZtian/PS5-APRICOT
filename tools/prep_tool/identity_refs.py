"""Prepare private exact-image rejection references; never release output."""
import argparse,hashlib,json,struct
from pathlib import Path

def prepare(catalog_path,image_folder,destination):
    catalog=json.loads(catalog_path.read_text())
    identity=bytes.fromhex(catalog['rom_sha256'])
    entries=catalog['entries']
    if len(identity)!=32 or not 1<=len(entries)<=256:raise ValueError('invalid catalog')
    wanted={e['sha256']:e for e in entries}
    sizes={e['bytes'] for e in entries}
    found={}
    for path in image_folder.rglob('*.bin'):
        if path.is_symlink() or path.stat().st_size not in sizes:continue
        data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if digest in wanted:found[digest]=data
    selected=[]
    for e in entries:
        if e['sha256'] in found:selected.append((e['index'],found[e['sha256']]))
    if not selected:raise ValueError('no exact original images match native catalog')
    header=bytearray(64);header[:8]=b'APRF0001';struct.pack_into('<I',header,8,len(selected));header[16:48]=identity
    table=bytearray();payload=bytearray();offset=64+24*len(selected)
    for index,data in selected:
        table+=struct.pack('<IIQQ',index,0,offset,len(data));payload+=data;offset+=len(data)
    if offset>64*1024*1024:raise ValueError('references exceed limit')
    with destination.open('xb') as f:f.write(header+table+payload)
    return {'matched':len(selected),'catalog_count':len(entries),'bytes':offset,'sha256':hashlib.sha256(header+table+payload).hexdigest()}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('catalog',type=Path);p.add_argument('images',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    print(json.dumps(prepare(a.catalog,a.images,a.output),indent=2))
