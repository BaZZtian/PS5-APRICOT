"""ApricotGS user-local ROM preparation. Generated output must remain private."""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('output', type=Path, help='New private preparation directory')
    parser.add_argument('--llvm', type=Path, default=Path('/usr/lib/llvm-18/bin'))
    parser.add_argument('--jobs', type=int, default=4, choices=range(1, 17))
    parser.add_argument('--cache', type=Path, help='Optional existing private compiler object cache')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    platform = root/'platform/ps5'
    if not platform.is_dir():
        platform = root/'local/platform'
    sys.path.insert(0, str(platform))
    from compiler.rom import inspect
    from compiler.prepare import prepare
    from compiler.boot import generate
    from compiler.build_game import build
    from compiler.static_variants import record_native_trace
    identity = inspect(args.rom)
    profile_path = platform/'compiler/profiles'/(identity['game_code']+'.json')
    if not profile_path.is_file():
        raise ValueError('No released translation profile for this exact edition/region')
    profile = json.loads(profile_path.read_text())
    if any(profile[key] != identity[key] for key in ('game_code', 'maker_code', 'revision')) or profile['rom_sha256'] != identity['sha256']:
        raise ValueError('ROM does not match the translation profile')
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output already exists; no files are overwritten')
    for command in (args.llvm/'clang++', args.llvm/'ld.lld'):
        if not command.is_file():
            raise ValueError('LLVM 18 compiler/linker missing')
    shader_tool = os.environ.get('GLSLANG') or shutil.which('glslangValidator')
    if not shader_tool:
        raise ValueError('Install glslang-tools before preparing game data')
    # Resolve dependencies before reading or creating private game outputs.
    import numpy as np
    from PIL import Image
    print('Verify ROM and extract private code images', flush=True)
    prepare(args.rom, output)
    for name, data in profile['seeds'].items():
        if name not in ('boot-seeds.json', 'overlay-seeds.json'):
            raise ValueError('Unexpected profile member')
        (output/name).write_text(json.dumps(data)+'\n')
    print('Translate ARM9/ARM7 and statically compile x86-64 module', flush=True)
    generate(output, limit=1000000, output_name='native-boot-release-0',
             pointer_candidates=True, all_overlays=True,
             bootstrap_guards=[tuple(v) for v in profile['initialization_guards']])
    built = build(output, 'native-boot-release-0', llvm=args.llvm, jobs=args.jobs, cache=args.cache)
    native = output/'native-boot-release-0'
    runtime = platform/'runtime'
    host = output/'host-tools'
    host.mkdir()
    common = [str(args.llvm/'clang++'), '-std=c++17', '-O2', '-I', str(runtime)]
    def compile_host(name, sources, extra=()):
        target = host/name
        subprocess.run([*common, *map(str, sources), *extra, '-o', str(target)], check=True)
        return target
    probe = compile_host('module-prepare', [runtime/'module_prepare.cpp', runtime/'game_module.cpp'])
    module = native/'native-image/game.oqgame'
    watched = next(e for e in json.loads((output/'manifest.json').read_text())['processors']['arm9']['overlays'] if e['id'] == 123)
    controls = output/'initialization-controls.txt'
    controls.write_text(''.join(f'{f} 4 1 -1 -1\n' for f in range(120, 16000, 40))+
                        ''.join(f'{f} 4 0 128 152\n' for f in range(2400, 16000, 120)))
    observed = set()
    for iteration in range(33):
        result = subprocess.run([str(probe), str(args.rom.resolve()), str(module), str(native),
                                 hex(watched['address']), str(watched['bytes']), '16000', str(controls)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (native/'initialization-result.txt').write_text(result.stdout)
        if result.returncode == 0:
            break
        stop = re.search(r'frames=(\d+) stopped=1 pc9=([0-9a-f]+) stop9=3\b', result.stdout)
        if result.returncode != 10 or not stop or not watched['address'] <= int(stop[2], 16) < watched['address']+watched['bytes']:
            raise ValueError('Native initialization stopped outside the supported finite-state preparation; see private result file')
        report = record_native_trace(output, native, 9, watched['id'])
        if report['sha256'] in observed or iteration == 32:
            raise ValueError('Native initialization did not converge; no incomplete edition is exported')
        observed.add(report['sha256'])
        print(f'Compile observed initialization state {iteration+1} offline', flush=True)
        name = 'native-boot-release-'+str(iteration+1)
        generate(output, limit=1000000, output_name=name, pointer_candidates=True,
                 all_overlays=True, bootstrap_guards=[tuple(v) for v in profile['initialization_guards']])
        built = build(output, name, llvm=args.llvm, jobs=args.jobs, cache=args.cache)
        native = output/name
        module = native/'native-image/game.oqgame'
    refs = output/'identity-refs.bin'
    references = load_module('apricot_identity_refs', root/'tools/prep_tool/identity_refs.py')
    references.prepare(native/'native-catalog.json', output, refs)
    shaders = host/'game_shaders.h'
    with shaders.open('wb') as merged:
        for name in ('tiles', 'geometry', 'geometry_resolve'):
            header = host/(name+'.h')
            subprocess.run([shader_tool, '-V', '--target-env', 'vulkan1.1', '--vn', 'oq_'+name,
                            str(runtime/'shaders'/(name+'.comp')), '-o', str(header)], check=True)
            merged.write(header.read_bytes())
    capture = compile_host('title-bake', [runtime/'title_bake.cpp', runtime/'game_module.cpp', runtime/'vulkan_tiles.cpp'], ['-I', str(host), '-ldl'])
    media = output/'title-media'
    media.mkdir()
    environment = dict(os.environ)
    # Select Mesa's deterministic CPU Vulkan device for offline media preparation.
    candidates = [Path('/usr/share/vulkan/icd.d')/name for name in ('lvp_icd.json', 'lvp_icd.x86_64.json')]
    icd = next((p for p in candidates if p.is_file()), None)
    if icd is None:
        raise ValueError('Install mesa-vulkan-drivers (Lavapipe) for title preparation')
    environment['VK_ICD_FILENAMES'] = str(icd)
    print('Generate local title image and music through the native module', flush=True)
    subprocess.run([str(capture), str(args.rom.resolve()), str(module), str(refs), str(media)], check=True, env=environment)
    with Image.open(media/'title-1.ppm') as image:
        image.save(media/'title.png')
    raw = np.fromfile(media/'title-source.s16le', dtype='<i2').reshape(-1, 2)
    count = int(len(raw)*48000*1024/33513982)
    positions = np.arange(count, dtype=np.float64)*33513982/(48000*1024)
    indices = np.arange(len(raw), dtype=np.float64)
    audio = np.stack([np.interp(positions, indices, raw[:, c]) for c in range(2)], axis=1).clip(-32768, 32767).round().astype('<i2')
    header = bytearray(64)
    header[:8] = b'APAU0001'
    struct.pack_into('<IHHIIII', header, 8, 48000, 2, 16, count, 0, count, 0)
    header[32:64] = bytes.fromhex(identity['sha256'])
    pcm = media/'title-menu.pcm'
    pcm.write_bytes(header+audio.tobytes())
    bundle = load_module('apricot_edition_bundle', root/'tools/prepare-edition-bundle.py')
    edition = 'heartgold' if identity['game_code'] == 'IPKD' else 'soulsilver'
    destination = output/'transfer/games'/edition
    bundle.prepare(args.rom, module, media/'title.png', destination, pcm, refs)
    print(json.dumps({'app': 'ApricotGS', 'edition': identity['game_code'], 'module_sha256': built['sha256'],
                      'transfer_folder': str(destination), 'console_destination': '/data/orqenva/games/'+edition,
                      'private_output': True, 'save_files_accessed': False,
                      'compatibility': 'Experimental; unsupported gameplay paths may stop.'}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit('Preparation stopped: '+str(error))
