"""Developer build step for game-independent AOT support, never a user-ROM step."""
from pathlib import Path
import argparse,hashlib,struct,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--llvm',type=Path,default=Path('/usr/lib/llvm-18/bin'));p.add_argument('--work',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent;runtime=root.parent/'runtime';a.work.mkdir(parents=True,exist_ok=True)
obj=a.work/'kernel.o';elf=a.work/'kernel.elf'
subprocess.run([str(a.llvm/'clang++'),'--target=x86_64-unknown-freebsd','-std=c++17','-O2','-fPIC','-ffreestanding','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-red-zone','-mno-avx','-I',str(runtime),'-c',str(root/'kernel.cpp'),'-o',str(obj)],check=True)
subprocess.run([str(a.llvm/'ld.lld'),'-shared','-Bsymbolic','--no-undefined','--build-id=none','-z','max-page-size=16384','-o',str(elf),str(obj)],check=True)
sys.path.insert(0,str(root.parent));from compiler.game_image import package_elf
data,report=package_elf(elf.read_bytes(),{'game_code':'TEST','maker_code':'00','revision':0,'sha256':'00'*32},[0,0])
image=data[256:256+report['image_bytes']]
if report['relative_relocations']:raise ValueError('Generic helper image must be relocation-free')
raw=elf.read_bytes();shoff=struct.unpack_from('<Q',raw,40)[0];shsize,shnum=struct.unpack_from('<HH',raw,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',raw,shoff+i*shsize) for i in range(shnum)]
symbols={}
for sec in sections:
 if sec[1]!=11:continue
 names=sections[sec[6]];strings=raw[names[4]:names[4]+names[5]]
 for at in range(sec[4],sec[4]+sec[5],24):
  n,info,other,which,value,size=struct.unpack_from('<IBBHQQ',raw,at)
  end=strings.find(b'\0',n);name=strings[n:end].decode('ascii')
  if name.startswith('aot_') and which:
   if info&15!=2 or not 0<=value<len(image):raise ValueError('Invalid helper function')
   symbols[name]=value
expected={'aot_'+n for n in ('lsl','lsr','asr','ror','add','psr','mrs_saved','restore','get_user','set_user','umull','umlal','smull','smlal','smulw','smlaw')}
if set(symbols)!=expected:raise ValueError('Unexpected native helper exports')
out='// Generated only from kernel.cpp/guest.h; no game input. Do not edit.\n'
out+='static constexpr uint8_t aot_kernel_image[]={\n'
out+='\n'.join(','.join(str(x) for x in image[i:i+32])+',' for i in range(0,len(image),32))+'\n};\n'
out+=''.join(f'static constexpr size_t {name}_offset={value};\n' for name,value in sorted(symbols.items()))
(root/'kernel_image.inc').write_text(out,encoding='ascii')
print('AOT_KERNEL',len(image),'bytes',len(symbols),'helpers',hashlib.sha256(image).hexdigest(),'no imports/relocations')
