"""Offline fixed-PE qualification; never maps executable pages or attaches to a process.
Requires pefile and capstone from the supplied --deps directory. Evidence output
contains all chained .pdata parts, code bytes, instruction boundaries and RIP sources.
"""
from pathlib import Path
import argparse,sys,hashlib,struct,bisect,json,re
ap=argparse.ArgumentParser()
ap.add_argument('--legacy',required=True,type=Path)
ap.add_argument('--public',required=True,type=Path)
ap.add_argument('--deps',required=True,type=Path)
ap.add_argument('--output',required=True,type=Path)
ap.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent)
args=ap.parse_args()
HERE=args.output.resolve();HERE.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(args.deps.resolve()))
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_REG_RIP,X86_OP_IMM
class Sample:
 def __init__(self,key,path,sha):
  self.key=key;self.path=str(path);self.raw=Path(path).read_bytes();self.sha=hashlib.sha256(self.raw).hexdigest();assert self.sha==sha
  self.pe=pefile.PE(data=self.raw);self.base=self.pe.OPTIONAL_HEADER.ImageBase
  self.entries=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in self.pe.DIRECTORY_ENTRY_EXCEPTION];self.starts=[e[0] for e in self.entries]
  self.md=Cs(CS_ARCH_X86,CS_MODE_64);self.md.detail=True
 def read(self,r,n):return self.pe.get_data(r,n)
 def owner(self,r):
  i=bisect.bisect_right(self.starts,r)-1
  return self.entries[i] if i>=0 and r<self.entries[i][1] else None
 def ultimate(self,e):
  seen=set()
  while e and e[2] not in seen:
   seen.add(e[2]);b=self.read(e[2],4)
   if not (b[0]>>3)&4:return e
   e=struct.unpack('<III',self.read(e[2]+4+((b[2]+1)&~1)*2,12))
  raise ValueError(e)
 def ins(self,r,n):return list(self.md.disasm(self.read(r,n),self.base+r))
 def full(self,r,save=True):
  owner=self.owner(r)
  if owner:
   root=self.ultimate(owner);parts=[e for e in self.entries if self.ultimate(e)==root]
  else:raise ValueError('no pdata: '+hex(r))
  rows=[];segments=[]
  for a,z,u in parts:
   code=self.read(a,z-a);ins=self.ins(a,z-a);assert sum(i.size for i in ins)==len(code)
   segments.append(dict(begin=hex(a),end=hex(z),unwind=hex(u),sha256=hashlib.sha256(code).hexdigest(),code=code.hex()))
   for i in ins:
    row=dict(rva=hex(i.address-self.base),bytes=i.bytes.hex(),op=i.mnemonic,args=i.op_str)
    for op in i.operands:
     if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:row['rip']=hex(i.address+i.size+op.mem.disp-self.base)
    if i.mnemonic in ('call','jmp') and i.operands[0].type==X86_OP_IMM:row['target']=hex(i.operands[0].imm-self.base)
    rows.append(row)
  doc=dict(sample=self.path,sha256=self.sha,root=hex(root[0]),parts=segments,instructions=rows)
  if save:
   stem=HERE/f'{self.key}-{root[0]:x}'
   stem.with_suffix('.json').write_text(json.dumps(doc,indent=2)+'\n')
   stem.with_suffix('.asm').write_text('\n'.join(f"{row['rva']:>10} {row['bytes']:<32} {row['op']} {row['args']}"+(f" ; RIP {row['rip']}" if 'rip' in row else '') for row in rows)+'\n')
  return doc
 def occurrences(self,pattern,code=True):
  out=[]
  for sec in self.pe.sections:
   if code and not sec.Characteristics&0x20000000:continue
   for m in re.finditer(re.escape(pattern),sec.get_data()):out.append(sec.VirtualAddress+m.start())
  return out
 def refs(self,targets):
  targets=set(targets);out=[]
  for sec in self.pe.sections:
   if not sec.Characteristics&0x20000000:continue
   data=sec.get_data()
   for m in re.finditer(rb'[\x48-\x4f][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',data):
    r=sec.VirtualAddress+m.start();t=r+7+struct.unpack_from('<i',data,m.start()+3)[0]
    if t in targets:out.append((r,t,self.owner(r)))
  return out

EXPECTED={
 'legacy':dict(sha='e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e',size=0x19b3000,root=0x181d008,engineVt=0x1361f38,engineSub=0x1361ff0,userVt=0x136fa80,userSub=0x136fbd0,engineTd=0x17dd3a8,userTd=0x177b328,engineCol=0x15ce5e0,userCol=0x15d5338,engineCtor=0x9859d0,userCtor=0x9cfee0,factory=0x98c3d0,insert=0x991d30,walk=0x984dc0,lookup=0x9d4b40,predicate=0x9d7a30,tick=0x9d34b0,publisher=0x4e27e0,table='legacy',predicateRvas=[0x9d7a9c,0x9d7bc9,0x9d7b2a,0x9d7a5f,0x9d7aef,0x9d7bb6,0x9d7bd9]),
 'public':dict(sha='253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc',size=0x19b4000,root=0x181e008,engineVt=0x1362478,engineSub=0x1362530,userVt=0x1370030,userSub=0x1370180,engineTd=0x17de3a8,userTd=0x177c328,engineCol=0x15cf260,userCol=0x15d5fb8,engineCtor=0x985ab0,userCtor=0x9cffc0,factory=0x98c4b0,insert=0x991e10,walk=0x984ea0,lookup=0x9d4c20,predicate=0x9d7b10,tick=0x9d3590,publisher=0x4e27f0,table='publicBuild',predicateRvas=[0x9d7b7c,0x9d7ca9,0x9d7c0a,0x9d7b3f,0x9d7bcf,0x9d7c96,0x9d7cb9])}
GETTER=bytes.fromhex('488b819ae3ffff488902488bc2c3')
PRED_BYTES=[bytes.fromhex(x) for x in ['0fb68b01020000','ff83a8010000','8b8b68020000','888300020000','888300020000','888300020000','888300020000']]
checks=[]
def check(value,label):
 if not value:raise AssertionError(label)
 checks.append(label)
def u64(s,a):return struct.unpack('<Q',s.read(a,8))[0]
def rtti(s,vtable,td,offset,name):
 col=u64(s,vtable-8)-s.base
 sig,off,cd,ty,ch,self=struct.unpack('<IIIIII',s.read(col,24))
 check(sig==1 and off==offset and cd==0 and ty==td and self==col,'RTTI complete-object locator '+name+':'+hex(offset))
 check(s.read(td+16,len(name)+1)==name.encode()+b'\0','RTTI type descriptor '+name)
 return dict(vtable=hex(vtable),col=hex(col),typeDescriptor=hex(td),subobjectOffset=hex(offset),name=name)
def source_table(e):
 values=[0xcfdc0,e['root'],e['engineVt'],e['engineSub'],e['userVt'],e['userSub']]+list(GETTER)
 values += [0xfe0,0x1080,0x1090,0x10,8,0x1e70,0x20a,0x2568,0x2578,0x330,8,0x198]+e['predicateRvas']
 values += [v for a in PRED_BYTES for v in a]+list(map(len,PRED_BYTES))+[0x200,0x1a8,0x268]
 blocks=[]
 for rel in ['native/steam_native_record_diagnostic.h','native/steam_original_recovery_engine/vendor/steam_native_record_diagnostic.h']:
  text=(args.source_root/rel).read_text(encoding='utf-8-sig')
  block=re.search(r'static constexpr ProfileLayout '+e['table']+r' \{(.*?)\n    \};',text,re.S).group(1)
  nums=[int(x,0) for x in re.findall(r'0x[0-9a-f]+|\b[0-9]+\b',block.split('"')[0])]
  check(nums==values,'production profile table exactly matches independent PE qualification: '+rel+':'+e['table'])
  check(e['sha'] in text,'complete SHA in source: '+rel+':'+e['table'])
  blocks.append(block)
 check(blocks[0]==blocks[1],'root/vendor profile tables synchronized '+e['table'])
def qualify(key,path):
 e=EXPECTED[key];s=Sample(key,path,e['sha']);startChecks=len(checks)
 check(s.pe.OPTIONAL_HEADER.SizeOfImage==e['size'],'PE image size '+key)
 source_table(e)
 types=[rtti(s,e['engineVt'],e['engineTd'],0,'.?AVCSteamEngine@@'),rtti(s,e['engineSub'],e['engineTd'],0xfe0,'.?AVCSteamEngine@@'),rtti(s,e['userVt'],e['userTd'],0,'.?AVCUser@@'),rtti(s,e['userSub'],e['userTd'],0x1e70,'.?AVCUser@@')]
 roots={'singleton':0xbedb0,**{name:e[name] for name in ['engineCtor','userCtor','factory','insert','walk','lookup','predicate','tick','publisher']}}
 docs={name:s.full(rva) for name,rva in roots.items()}
 for name,doc in docs.items():
  check(int(doc['root'],16)==roots[name],'ultimate .pdata root '+key+':'+name)
  check(all(int(b['end'],16)<=s.pe.OPTIONAL_HEADER.SizeOfImage for b in doc['parts']),'all chained parts inside image '+name)
  # Every instruction byte came from the complete owned part; full() rejects undecoded gaps.
  for part in doc['parts']:
   ins=s.ins(int(part['begin'],16),int(part['end'],16)-int(part['begin'],16))
   boundaries={i.address for i in ins}
   for i in ins:
    for op in i.operands:
     if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
      target=i.address+i.size+op.mem.disp-s.base
      check(0<=target<s.pe.OPTIONAL_HEADER.SizeOfImage,'RIP source in image '+hex(i.address-s.base))
    if i.mnemonic.startswith('j') and i.operands[0].type==X86_OP_IMM:
     target=i.operands[0].imm
     if s.base+int(part['begin'],16)<=target<s.base+int(part['end'],16):check(target in boundaries,'intra-part branch instruction boundary '+hex(i.address-s.base))
 def row(name,rva):
  rows=[i for i in docs[name]['instructions'] if int(i['rva'],16)==rva]
  check(len(rows)==1,'source at qualified full-function instruction boundary '+name+':'+hex(rva))
  return rows[0]
 def code(name,rva,expected):check(row(name,rva)['bytes']==expected,'field opcode '+name+':'+hex(rva))
 def rip(name,rva,target):check(row(name,rva).get('rip')==hex(target),'resolved RIP '+name+':'+hex(rva)+' -> '+hex(target))
 def contains(name,hexcode):check(any(i['bytes']==hexcode for i in docs[name]['instructions']),'complete root contains exact semantic instruction '+name+':'+hexcode)
 rip('singleton',0xbedfc,e['root']);check(row('singleton',0xbedf7).get('target')==hex(e['engineCtor']),'singleton constructs qualified CSteamEngine')
 a=e['engineCtor'];rip('engineCtor',a+0x23,e['engineVt']);code('engineCtor',a+0x2a,'488906');rip('engineCtor',a+0x34,e['engineSub']);code('engineCtor',a+0x3b,'488986e00f0000')
 contains('engineCtor','4c89ae80100000');contains('engineCtor','4489ae90100000')
 a=e['userCtor'];rip('userCtor',a+0x30,e['userVt']);code('userCtor',a+0x3a,'488906');rip('userCtor',a+0x3d,e['userSub']);code('userCtor',a+0x44,'488986701e0000')
 contains('userCtor','48899e68250000');contains('userCtor','899e78250000')
 check(any(i.get('target')==hex(e['userCtor']) for i in docs['factory']['instructions']),'factory constructs qualified CUser')
 contains('factory','488d8e80100000');contains('factory','48895c2430')
 check(any(i.get('target')==hex(e['insert']) for i in docs['factory']['instructions']),'factory inserts CUser pointer from local key/pointer pair')
 contains('insert','410f100424');contains('insert','0f1101');contains('insert','48c1e104');contains('insert','894610')
 contains('walk','488b8f80100000');contains('walk','399f90100000');contains('walk','4803c0');contains('walk','488b4cc108');rip('walk',e['walk']+10,e['root'])
 check(s.owner(0xcfdc0) is None,'getter has no pdata (document explicit leaf boundary)')
 check(s.read(0xcfdc0,14)==GETTER,'entire getter leaf code')
 gi=s.ins(0xcfdc0,14);check(sum(i.size for i in gi)==14 and [i.mnemonic for i in gi]==['mov','mov','mov','ret'],'entire getter is a straight-line leaf with terminal ret')
 check(u64(s,e['userSub']+0x50)==s.base+0xcfdc0,'CUser secondary vtable +50 getter slot')
 check(0x1e70+gi[0].operands[1].mem.disp==0x20a,'getter receiver adjustment resolves full 64-bit account U+20a')
 leaf=dict(begin='0xcfdc0',end='0xcfdce',code=GETTER.hex(),sha256=hashlib.sha256(GETTER).hexdigest(),scope='entire non-pdata leaf through terminal ret; vtable direct entry',instructions=[dict(rva=hex(i.address-s.base),bytes=i.bytes.hex(),op=i.mnemonic,args=i.op_str) for i in gi])
 (HERE/(key+'-getter-leaf.json')).write_text(json.dumps(leaf,indent=2)+'\n')
 for b in ['488bb968250000','48638178250000','4869c830030000','483b4708','4881c730030000']:contains('lookup',b)
 for b in ['488b960a020000','448bb678250000','4c8b9668250000','4869c030030000','4439bb9c010000','898b9c010000','898b98010000']:contains('tick',b)
 for r,b in zip(e['predicateRvas'],PRED_BYTES):code('predicate',r,b.hex())
 a=e['predicate'];code('predicate',a+0x60,'8b8b9c010000');rip('predicate',a+0x66,0x118e258);rip('predicate',a+0x90,0x118dc08);rip('predicate',a+0xa1,0x118dbe8);code('predicate',a+0xa7,'8b8398010000')
 imports={i.address-s.base:(d.dll.decode(),i.name.decode() if i.name else str(i.ordinal)) for d in s.pe.DIRECTORY_ENTRY_IMPORT for i in d.imports}
 for r,name in [(0x118e258,'ThreadIsProcessIdActive'),(0x118dc08,'GetForegroundWindow'),(0x118dbe8,'GetWindowThreadProcessId')]:check(imports[r][1]==name,'resolved semantic import '+name)
 p=docs['publisher'];check(len(p['parts'])==8 and sum(int(x['end'],16)-int(x['begin'],16) for x in p['parts'])==1185 and len(p['instructions'])==272,'publisher all eight chained pdata parts / 1185 bytes / 272 instructions')
 check(s.read(e['publisher'],10).hex()=='488bc44881ec88010000','publisher complete prologue candidate')
 return dict(profile=key,sample=str(path.resolve()),sha256=s.sha,passed=True,checks=len(checks)-startChecks,rtti=types,functions={k:dict(root=v['root'],parts=[dict(begin=p['begin'],end=p['end'],sha256=p['sha256']) for p in v['parts']],bytes=sum(int(p['end'],16)-int(p['begin'],16) for p in v['parts']),instructions=len(v['instructions'])) for k,v in docs.items()},getter=leaf,fields=dict(engineUserVector='0x1080',engineUserCount='0x1090',userEntryStride='0x10',userEntryPointer='0x8',accountFull64='0x20a',recordVector='0x2568',recordCount='0x2578',recordStride='0x330',gameIdFull64='0x8',foregroundComparisonPid='0x198',primaryLivenessPid='0x19c'),semanticImports={hex(r):imports[r] for r in [0x118e258,0x118dc08,0x118dbe8]})
try:
 results=[qualify('legacy',args.legacy),qualify('public',args.public)]
 report=dict(passed=True,profiles=results,checkCount=len(checks),checks=checks,targetCalls=0,targetMemoryWrites=0,processAttachments=0,scope='fixed PE static qualification only; synthetic heap tests are separate',unverified=['real public-build Steam/user heap read','publisher/getter runtime invocation','running reconnect/fullscreen/real sleep','ABA exclusion / atomic snapshot / login-generation lease'])
 (HERE/'pe-qualification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(passed=True,profiles=2,checkCount=len(checks),report=str(HERE/'pe-qualification.json'))))
except Exception as ex:
 (HERE/'pe-qualification-failure.json').write_text(json.dumps(dict(passed=False,error=str(ex),checks=checks),indent=2)+'\n')
 raise

