# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare private, fingerprint-checked code images and overlay metadata.

This stage does not claim executable coverage or a working game. It never
imports an emulator or includes ROM bytes in the compiler's own sources.
"""
from pathlib import Path
import hashlib
import json
import struct
from . import blz, rom

ROOT = Path(__file__).resolve().parents[1]
MAX_CODE = 16*1024*1024

def digest(data):
    return hashlib.sha256(data).hexdigest()

def region(data, offset, size, label):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data)-offset:
        raise ValueError('Invalid ' + label + ' range')
    return data[offset:offset+size]

def main_image(stored, base, entry, hook, *, decompress):
    image = blz.unpack(stored, max_output=MAX_CODE) if decompress else bytes(stored)
    if len(image) > MAX_CODE or not base <= entry < base+len(image) or entry & 3:
        raise ValueError('Entry outside main code image')
    pointer = hook-base-4
    if hook == 0 or pointer & 3:
        raise ValueError('Explicit code-settings pointer required')
    settings_address = struct.unpack('<I', region(image, pointer, 4, 'code-settings pointer'))[0]
    settings_offset = settings_address-base
    values = struct.unpack('<6I', region(image, settings_offset, 24, 'code settings'))
    table_begin, table_end, copy_begin, bss_begin, bss_end, compressed_end = values
    if table_begin & 3 or table_end < table_begin or (table_end-table_begin) % 12:
        raise ValueError('Invalid autoload table')
    if not base <= copy_begin <= base+len(image):
        raise ValueError('Invalid autoload source')
    if bss_end < bss_begin or bss_end-bss_begin > MAX_CODE:
        raise ValueError('Invalid main BSS')
    if compressed_end and (not decompress or compressed_end != base+len(stored)):
        raise ValueError('Compressed-end metadata differs from the stored code image')
    table = region(image, table_begin-base, table_end-table_begin, 'autoload table')
    sections = [{'address': base, 'image_offset': 0, 'bytes': copy_begin-base,
                 'bss_bytes': 0, 'implicit': True}]
    source = copy_begin-base
    for pos in range(0, len(table), 12):
        address, length, bss = struct.unpack_from('<3I', table, pos)
        if length > MAX_CODE or bss > MAX_CODE or address+length+bss > 0x100000000:
            raise ValueError('Invalid autoload section bounds')
        region(image, source, length, 'autoload section')
        sections.append({'address': address, 'image_offset': source, 'bytes': length,
                         'bss_bytes': bss, 'implicit': False})
        source += length
    return image, {'ram_address': base, 'entry': entry, 'stored_bytes': len(stored),
        'unpacked_bytes': len(image), 'compressed': image != stored,
        'settings_address': settings_address, 'main_bss': [bss_begin, bss_end],
        'sections': sections, 'sha256': digest(image), 'stored_sha256': digest(stored)}

def overlays(data, table_span, fat_span):
    table = region(data, table_span['rom_offset'], table_span['stored_bytes'], 'overlay table')
    fat = region(data, fat_span['rom_offset'], fat_span['stored_bytes'], 'FAT')
    if len(table) % 32 or len(fat) % 8:
        raise ValueError('Misaligned overlay/FAT table')
    result = []
    ids = set()
    for pos in range(0, len(table), 32):
        ident, address, size, bss, init_begin, init_end, file_id, packed = struct.unpack_from('<8I', table, pos)
        compressed_size, flags = packed & 0xffffff, packed >> 24
        if ident in ids or flags & ~3 or address+size+bss > 0x100000000 or size+bss > MAX_CODE:
            raise ValueError('Invalid overlay record')
        ids.add(ident)
        start, end = struct.unpack('<2I', region(fat, file_id*8, 8, 'overlay file id'))
        if end < start:
            raise ValueError('Invalid overlay file bounds')
        stored = region(data, start, end-start, 'overlay file')
        if len(stored) > MAX_CODE or (flags & 1 and compressed_size != len(stored)):
            raise ValueError('Overlay compressed length differs from FAT')
        image = blz.unpack(stored, max_output=MAX_CODE, required=True) if flags & 1 else stored
        if len(image) != size:
            raise ValueError('Overlay expanded length differs from RAM size')
        if init_begin != init_end and (init_begin & 3 or init_end & 3 or not address <= init_begin <= init_end <= address+size):
            raise ValueError('Invalid overlay initializer range')
        result.append((image, {'id': ident, 'address': address, 'bytes': size, 'bss_bytes': bss,
            'initializer_range': [init_begin, init_end], 'file_id': file_id,
            'flags': flags, 'compressed': bool(flags & 1), 'stored_bytes': len(stored),
            'sha256': digest(image), 'stored_sha256': digest(stored)}))
    return result

def prepare(path, output):
    path, output = Path(path), Path(output).resolve()
    identity = rom.inspect(path)
    if not identity['identity_verified']:
        raise ValueError('A verified full-ROM fingerprint is required for code preparation')
    try:
        relative = output.relative_to(ROOT)
    except ValueError:
        relative = None
    if relative is not None and (not relative.parts or not relative.parts[0].startswith('private-')):
        raise ValueError('Inside the source tree, generated ROM data must use an ignored private-* folder')
    if output.exists():
        raise ValueError('Output already exists; no private build is overwritten')
    data = path.read_bytes()
    if digest(data) != identity['sha256']:
        raise ValueError('Input changed after identity verification')
    images = {}
    manifest = {'format': 'orqenva-private-input-1', 'identity': identity,
        'contains_user_rom_data': True, 'public_distribution_allowed': False,
        'translation_complete': False, 'playable': False, 'processors': {}}
    for cpu, pointer_offset in [('arm9', 0x70), ('arm7', 0x74)]:
        fields = identity['container'][cpu]
        stored = region(data, fields['rom_offset'], fields['stored_bytes'], cpu)
        hook = struct.unpack_from('<I', data, pointer_offset)[0]
        image, info = main_image(stored, fields['ram_address'], fields['entry'], hook, decompress=cpu == 'arm9')
        images[cpu+'/main-stored.bin'] = stored
        images[cpu+'/main-unpacked.bin'] = image
        info['stored_file'] = cpu+'/main-stored.bin'
        info['unpacked_file'] = cpu+'/main-unpacked.bin'
        info['overlays'] = []
        for image, overlay in overlays(data, identity['container']['overlays9' if cpu == 'arm9' else 'overlays7'], identity['container']['fat']):
            filename = f'{cpu}/overlays/{overlay["id"]:04d}.bin'
            images[filename] = image
            overlay['file'] = filename
            info['overlays'].append(overlay)
        manifest['processors'][cpu] = info
    output.mkdir(parents=True)
    for name, image in images.items():
        file = output/name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(image)
    (output/'PRIVATE-NOT-FOR-DISTRIBUTION.txt').write_text('Generated from the owner-provided ROM. Do not commit, upload or redistribute this folder.\n')
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return {'output': str(output), 'game_code': identity['game_code'], 'private_files': len(images),
            'arm9_overlays': len(manifest['processors']['arm9']['overlays']),
            'arm7_overlays': len(manifest['processors']['arm7']['overlays']), 'playable': False}
