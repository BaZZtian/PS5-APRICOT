"""Developer-only conversion of published numeric profiles; no ROM input."""
from pathlib import Path
import json
root=Path(__file__).resolve().parent
out='// Generated numeric addresses from exact regional profiles; no game bytes.\n'
for code in ('IPKD','IPGD'):
 data=json.loads((root.parent/'compiler/profiles'/(code+'.json')).read_text())
 rows=[]
 for name,core in (('arm9',9),('arm7',7)):
  rows.extend((core,0xffffffff,pc,int(mode)) for pc,mode in data['seeds']['boot-seeds.json'].get(name,[]))
  for ident,seeds in data['seeds']['overlay-seeds.json'].get(name,{}).items():rows.extend((core,int(ident),pc,int(mode)) for pc,mode in seeds)
 out+='static constexpr G seeds_'+code+'[][4]={\n'+''.join('{'+','.join(str(n)+'u' for n in row)+'},\n' for row in rows)+'};\n'
 out+='static constexpr G guards_'+code+'[][2]={'+','.join('{'+str(core)+'u,'+str(ident)+'u}' for core,ident in data['initialization_guards'])+'};\n'
 print(code,len(rows),'numeric seeds')
(root/'profile_seeds.inc').write_text(out,encoding='ascii')
