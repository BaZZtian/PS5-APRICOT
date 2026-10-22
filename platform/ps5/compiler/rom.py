# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only identity/container inspection; no game-code mapping is claimed."""
from pathlib import Path
import hashlib,struct
KNOWN={
 'IPKE':('A','USA','4fcded0e2713dc03929845de631d0932ea2b5a37',None),
 'IPGE':('B','USA','f8dc38ea20c17541a43b58c5e6d18c1732c7e582',None),
 'IPGD':('B','DEU','277a11721d79f85d97abbf16683cf9fafef528db','e8cd5811650e9a9a1ca98a18c2b00b24583a7a770f82b4523147c4f96656a12e'),
 'IPKD':('A','DEU','eac6055e1facbb9378ffe0a83129bf275c748c07','b0746b12cafdc2a80de21d218d491cd06c567fab07d9740ae658bb548c37a339'),
}
def inspect(path):
    path=Path(path);size=path.stat().st_size
    with path.open('rb') as f:
        h=f.read(0x160)
        if len(h)!=0x160:raise ValueError('Truncated header')
        sha1=hashlib.sha1();sha256=hashlib.sha256();sha1.update(h);sha256.update(h)
        for block in iter(lambda:f.read(1024*1024),b''):sha1.update(block);sha256.update(block)
    game=h[12:16].decode('ascii');maker=h[16:18].decode('ascii');revision=h[30]
    if game not in KNOWN or maker!='01' or revision!=0 or h[18]!=0:raise ValueError('Unsupported identity/revision/unit')
    edition,region,expected1,expected256=KNOWN[game]
    if expected1 and sha1.hexdigest()!=expected1:raise ValueError('Known identity but ROM hash differs')
    if expected256 and sha256.hexdigest()!=expected256:raise ValueError('ROM SHA-256 differs')
    def span(off,length,label):
        if length and (off<0x160 or off>size or length>size-off):raise ValueError('Invalid range: '+label)
    parts={}
    for label,at in [('arm9',0x20),('arm7',0x30)]:
        off,entry,ram,length=struct.unpack_from('<4I',h,at);span(off,length,label)
        if not length or ram+length>0x100000000:raise ValueError('Invalid CPU image')
        parts[label]={'rom_offset':off,'entry':entry,'ram_address':ram,'stored_bytes':length}
    for label,at in [('fnt',0x40),('fat',0x48),('overlays9',0x50),('overlays7',0x58)]:
        off,length=struct.unpack_from('<2I',h,at);span(off,length,label)
        if label=='fat' and length%8:raise ValueError('Invalid FAT size')
        if label.startswith('overlays') and length%32:raise ValueError('Invalid overlay table size')
        parts[label]={'rom_offset':off,'stored_bytes':length}
    return {'game_code':game,'maker_code':maker,'revision':revision,'edition':edition,'region':region,
            'sha1':sha1.hexdigest(),'sha256':sha256.hexdigest(),'identity_verified':expected1 is not None,
            'translation_mapping':'not implemented','playable':False,'container':parts}
