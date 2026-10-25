"""Pinned original HWiNFO RTSS mirror gate/projection, all external IO inert.
This is HWiNFO's duplicate mirror, not PresentMon or YMCC's RTSS reader.
"""
from pathlib import Path
import sys,struct,hashlib,json,os
ROOT=Path(__file__).resolve().parent.parent;BASE=(ROOT/'../../../Build/Tasks/YMCC-Steam-HUD-Probe').resolve()
OUT=Path(os.environ.get('YMCC_HWINFO_RTSS_MIRROR_OUT',BASE/'hwinfo-duplicate-frame-research-20261016'));OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(BASE/'hwinfo-unused-clock-20261016/python-libs'))
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE,UC_HOOK_MEM_READ
from unicorn.x86_const import *
IMAGE=BASE/'hwinfo-producer-budget-20261016/HWiNFO64-static-inspection.exe';raw=IMAGE.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='5ef21625978eb2d2f6906ad31b9817ce8235be80dca556404f1d83e4674e366a'
PE=pefile.PE(data=raw);BLOB=PE.get_memory_mapped_image();IB=PE.OPTIONAL_HEADER.ImageBase
assert struct.unpack_from('<Q',BLOB,0x1408d1650+14*8-IB)[0]==0x14035c4ec
assert BLOB[0x14035c562-IB:0x14035c567-IB]==b'\x25\x00\x50\x0f\xf0'
# PresentMon is an independent class/initializer and is never selected here.
assert struct.unpack_from('<Q',BLOB,0x1408ce958+14*8-IB)[0]==0x140345df0
q=lambda n:struct.pack('<Q',n)
d=lambda n:struct.pack('<I',n)
def run(enabled,count=1,foreground=True,version=0x20015,valid_signature=True,instance=0):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(IB,(len(BLOB)+4095)&~4095);u.mem_write(IB,BLOB)
 heap,stack,stub=0x20000000,0x30000000,0x40000000
 u.mem_map(heap,0xc00000);u.mem_map(stack,0x10000);u.mem_map(stub,0x1000);u.mem_write(stub,b'\xc3'*0x1000)
 group,sensor,slot,reading,mapping=[heap+n for n in [0x1000,0x2000,0x3000,0x4000,0x10000]];rsp,rbp,halt=stack+0x7000,stack+0x5000,stub+0xff0
 u.mem_write(group,q(sensor)+d(1)+d(0));u.mem_write(sensor,q(0x1408d1650));u.mem_write(sensor+0x1c,d(instance)+d(0xf00f5000));u.mem_write(slot,q(group))
 stride=0x2500;mapbytes=40+stride*count
 u.mem_write(mapping,d(0x52545353 if valid_signature else 0)+d(version)+d(stride)+d(40)+d(count))
 for i in range(count):
  app=mapping+40+i*stride
  u.mem_write(app,d(i+1));u.mem_write(app+4,b'fixture.exe\x00')
  u.mem_write(app+0x10c,d(1000));u.mem_write(app+0x110,d(2000));u.mem_write(app+0x114,d(120));u.mem_write(app+0x118,d(8333))
  u.mem_write(app+0x13a0,d(1200));u.mem_write(app+0x12c,d(1));u.mem_write(app+0x23d8,d(900));u.mem_write(app+0x23dc,d(600))
 # Only Windows imports are substituted, at their real original IAT slots.
 for address,target in [(0x1407511d0,stub+0x10),(0x1407511c0,stub+0x20),(0x140751d98,stub+0x30),
                        (0x140751be0,stub+0x100),(0x140751bd8,stub+0x110),(0x140752660,stub+0x120),(0x140752688,stub+0x130)]:u.mem_write(address,q(target))
 result={'enabled':enabled,'entries':count,'foreground':foreground,'version':hex(version),'instance':instance,'validSignature':valid_signature,
         'groupKey':None,'initializedGroup':None,'loadedEnabled':None,'foregroundQueries':0,'foregroundPidQueries':0,'rtssBytesRead':0,'projectedValues':[],'labelCopies':0,'instructions':0}
 phase='init';recent=[]
 def u64(at):return struct.unpack('<Q',u.mem_read(at,8))[0]
 def wide(at):
  b=[]
  for i in range(256):
   c=bytes(u.mem_read(at+i*2,2))
   if c==b'\0\0':return b''.join(b).decode('utf-16le')
   b.append(c)
  raise AssertionError('unbounded emulated wide string')
 def ascii(at):
  b=bytes(u.mem_read(at,512));return b.split(b'\0')[0]
 def ret(value=0):
  top=u.reg_read(UC_X86_REG_RSP);u.reg_write(UC_X86_REG_RAX,value);u.reg_write(UC_X86_REG_RSP,top+8);u.reg_write(UC_X86_REG_RIP,u64(top))
 def code(uc,at,size,data):
  result['instructions']+=1;recent.append(hex(at))
  if len(recent)>15:recent.pop(0)
  if at==halt:uc.emu_stop();return
  if phase=='load' and at==0x1403b1a98:uc.emu_stop();return
  if phase=='gate' and at in (0x14035a2b9,0x14035bd99):result['continuesToPerReadingLoop']=at==0x14035a2b9;uc.emu_stop();return
  if at==stub+0x100:
   assert phase=='init' and wide(uc.reg_read(UC_X86_REG_R8))=='RTSSSharedMemoryV2';ret(456);return
  if at==stub+0x110:assert phase=='init';ret(mapping);return
  if at==stub+0x120:result['foregroundQueries']+=1;ret(123 if foreground else 0);return
  if at==stub+0x130:
   result['foregroundPidQueries']+=1;uc.mem_write(uc.reg_read(UC_X86_REG_RDX),d(count));ret(1);return
  if at==0x1400211f4:
   assert wide(uc.reg_read(UC_X86_REG_R8))=='%08X_%d';top=uc.reg_read(UC_X86_REG_RSP);item=u64(top+0x28)&0xffffffff;family=uc.reg_read(UC_X86_REG_R9)&0xffffffff
   key=f'{family:08X}_{item}';result['groupKey']=key;uc.mem_write(uc.reg_read(UC_X86_REG_RCX),key.encode('utf-16le')+b'\0\0');ret();return
  if at==stub+0x10:
   assert wide(uc.reg_read(UC_X86_REG_RDX))==result['groupKey'];uc.mem_write(u64(uc.reg_read(UC_X86_REG_RSP)+0x28),q(456));ret();return
  if at==stub+0x20:
   assert wide(uc.reg_read(UC_X86_REG_RDX))=='Enabled';top=uc.reg_read(UC_X86_REG_RSP);uc.mem_write(uc.reg_read(UC_X86_REG_R9),d(4));uc.mem_write(u64(top+0x28),d(enabled));uc.mem_write(u64(top+0x30),d(4));ret();return
  if at==stub+0x30:uc.mem_write(uc.reg_read(UC_X86_REG_RCX),q(123));ret(1);return
  if at==0x140018580:ret();return
  if at==0x1406fe8e0:ret(len(ascii(uc.reg_read(UC_X86_REG_RCX))));return
  if at==0x1406fde8c:ret(len(wide(uc.reg_read(UC_X86_REG_RCX))));return
  if at in (0x140018dec,0x1400191fc,0x14001eab8,0x14001e8c0):result['labelCopies']+=1;ret();return
 def mem(uc,access,address,size,value,data):
  if phase=='projection' and mapping<=address<mapping+mapbytes:result['rtssBytesRead']+=size
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_READ,mem)
 def call(entry):
  u.reg_write(UC_X86_REG_RSP,rsp);u.mem_write(rsp,q(halt));u.reg_write(UC_X86_REG_RCX,sensor)
  try:u.emu_start(entry,0,count=200000)
  except Exception:print({'phase':phase,'recent':recent});raise
  assert u.reg_read(UC_X86_REG_RIP)==halt
 call(0x14035c4ec);initialized=u.reg_read(UC_X86_REG_RAX)&0xffffffff;result['initializedGroup']=hex(initialized)
 if not initialized:
  assert instance>0 or not valid_signature or version<0x20000
  return result
 assert initialized==0xf00f5000 and instance==0
 phase='load';u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RBP,rbp);u.reg_write(UC_X86_REG_R15,group);u.mem_write(rsp+0x60,q(123));u.mem_write(rsp+0x78,q(123));u.emu_start(0x1403b1a04,0,count=2000)
 result['loadedEnabled']=struct.unpack('<I',u.mem_read(group+8,4))[0];assert result['groupKey']=='F00F5000_0'
 phase='gate';u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RBP,rbp);u.reg_write(UC_X86_REG_RDI,slot);u.reg_write(UC_X86_REG_R13,sensor);u.emu_start(0x14035a26b,0,count=2000)
 if result['continuesToPerReadingLoop']:
  phase='projection'
  for index in range(4):
   u.mem_write(reading,b'\0'*0x280);u.reg_write(UC_X86_REG_RDX,index);u.reg_write(UC_X86_REG_R8,reading);call(0x14035c574)
   result['projectedValues'].append({'index':index,'valid':bool(u.reg_read(UC_X86_REG_RAX)),'value':struct.unpack('<d',u.mem_read(reading+8,8))[0]})
 if enabled:
  assert result['foregroundQueries']==4 and result['rtssBytesRead']>0 and len(result['projectedValues'])==4
  assert result['projectedValues'][0]['value']==120 and result['projectedValues'][3]['value']==8.333, result
 else:
  assert result['foregroundQueries']==result['foregroundPidQueries']==result['rtssBytesRead']==result['labelCopies']==0 and not result['projectedValues']
 return result
def primary(mirror_enabled):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(IB,(len(BLOB)+4095)&~4095);u.mem_write(IB,BLOB)
 heap,stack,halt=0x20000000,0x30000000,0x40000000
 u.mem_map(heap,0x10000);u.mem_map(stack,0x10000);u.mem_map(halt,0x1000);u.mem_write(halt,b"\xc3")
 u.mem_write(heap+8,d(mirror_enabled));u.mem_write(0x1411355d8,d(1))
 calls=[]
 def code(uc,at,size,data):
  if at==halt:uc.emu_stop();return
  if at in (0x140342d90,0x14034554c):
   calls.append(hex(at));rsp=uc.reg_read(UC_X86_REG_RSP);target=struct.unpack('<Q',uc.mem_read(rsp,8))[0]
   uc.reg_write(UC_X86_REG_RAX,1);uc.reg_write(UC_X86_REG_RSP,rsp+8);uc.reg_write(UC_X86_REG_RIP,target)
 u.hook_add(UC_HOOK_CODE,code);rsp=stack+0x8000;u.mem_write(rsp,q(halt));u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RCX,heap+0x1000)
 u.emu_start(0x140345df0,0,count=1000)
 result={'mirrorEnabled':mirror_enabled,'presentMonOption':1,'group':hex(u.reg_read(UC_X86_REG_RAX)&0xffffffff),'helperBoundaries':calls}
 assert u.reg_read(UC_X86_REG_RIP)==halt and result['group']=='0xf0000ff0' and len(calls)==2
 return result
cases=[]
for count,foreground,version in [(1,True,0x20015),(64,True,0x20015),(1024,True,0x20015),(64,False,0x20015),(4,True,0x2000c)]:
 cases.append({'name':f'entries-{count}-foreground-{foreground}-version-{version:x}','before':run(1,count,foreground,version),'after':run(0,count,foreground,version)})
for instance,signature,version in [(1,True,0x20015),(31,True,0x20015),(0,False,0x20015),(0,True,0x10000)]:
 result=run(1,1,True,version,signature,instance);assert result['initializedGroup']=='0x0';cases.append({'name':'unsupported-init','result':result})
primary_cases=[primary(1),primary(0)]
report={'ok':True,'checks':len(cases)+len(primary_cases),'cases':cases,'presentMonIndependentInit':primary_cases,'imageSha256':hashlib.sha256(raw).hexdigest(),'blockSha256':{n:hashlib.sha256(BLOB[a-IB:b-IB]).hexdigest() for n,a,b in [('initialize',0x14035c4ec,0x14035c571),('projection',0x14035c574,0x14035ca02),('registryLoad',0x1403b1a04,0x1403b1a98),('gate',0x14035a26b,0x14035a2b9)]},'realHwinfoExecuted':False,'realRTSSOrForegroundQueries':False,'registryWrites':False,'cpuPercentageMeasured':False,'scope':'Original initializer, Enabled loading, gate and per-reading mirror projection. All external Windows calls and string formatting are inert; counts do not cover total HWiNFO CPU or startup costs.'}
(OUT/'RTSS-MIRROR-EMULATION-RESULT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({'ok':True,'checks':report['checks'],'largeMirrorBefore':cases[2]['before'],'largeMirrorAfter':cases[2]['after']},indent=2))
