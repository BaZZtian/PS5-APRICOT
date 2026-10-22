# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a private, partial native module, not a ready-to-play game."""
from pathlib import Path
import concurrent.futures,hashlib,json,subprocess
from .game_image import write_image

def link_sources(sources,output,llvm,includes,cache,jobs=4):
    output,llvm,includes,cache=map(Path,(output,llvm,includes,cache))
    output.mkdir(exist_ok=True);cache.mkdir(exist_ok=True)
    compiler=llvm/'clang++';version=subprocess.check_output([str(compiler),'--version']).splitlines()[0]
    flags=['--target=x86_64-unknown-freebsd','-std=c++17','-O1','-fPIC','-ffreestanding','-fno-builtin',
           '-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
           '-mno-red-zone','-mno-avx','-I',str(includes)]
    headers=b''.join((includes/name).read_bytes() for name in ('guest.h','irq.h','overlay_types.h'))
    def compile_source(source):
        key=hashlib.sha256(source.read_bytes()+headers+version+repr(flags).encode()).hexdigest()
        obj=cache/(key+'.o')
        if not obj.exists():
            temporary=obj.with_suffix('.partial.o')
            subprocess.run([str(compiler),*flags,'-c',str(source),'-o',str(temporary)],check=True)
            temporary.replace(obj)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:objects=list(pool.map(compile_source,sources))
    (output/'exports.map').write_text('{ global: oq_boot_arm9; oq_boot_arm7; oq_boot_overlay_catalog; local: *; };\n')
    elf=output/'game.elf'
    subprocess.run([str(llvm/'ld.lld'),'-shared','-Bsymbolic','--no-undefined','--build-id=none','-z','max-page-size=16384',
                    '--version-script='+str(output/'exports.map'),'-o',str(elf),*map(str,objects)],check=True)
    return elf

def build(preparation,native_folder,llvm='/usr/lib/llvm-18/bin',jobs=4,cache=None):
    preparation=Path(preparation).resolve();native=(preparation/native_folder).resolve()
    if not 1<=jobs<=16 or native.parent!=preparation or not native.name.startswith('native-boot'):
        raise ValueError('Expected native build inside private preparation')
    manifest=json.loads((preparation/'manifest.json').read_text())
    if manifest.get('public_distribution_allowed') is not False or not manifest.get('contains_user_rom_data'):
        raise ValueError('Expected explicitly private ROM preparation')
    analysis=json.loads((native/'analysis.json').read_text())
    if set(analysis)!=set(('arm9','arm7')):raise ValueError('Native processor analysis missing')
    destination=native/'native-image'
    if destination.exists():raise ValueError('Native image output already exists')
    sources=sorted(native.glob('*.cpp'))
    if not sources:raise ValueError('No translated native source')
    runtime=Path(__file__).resolve().parents[1]/'runtime'
    cache=Path(cache).resolve() if cache is not None else preparation/'private-image-cache'
    elf=link_sources(sources,destination,llvm,runtime,cache,jobs)
    entries=[manifest['processors'][name]['entry'] for name in ('arm9','arm7')]
    report=write_image(elf,destination/'game.oqgame',manifest['identity'],entries)
    report['identity']=manifest['identity'];report['module']='game.oqgame'
    report['scope']='Partial static translation; development trace only'
    (destination/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
