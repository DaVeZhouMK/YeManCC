"""Run a pinned HWiNFO averaging block in Unicorn with inert allocation only.
No HWiNFO process, sensor mapping, hardware or external API is executed.
Counts describe this original block, not whole-process CPU savings.
"""
from pathlib import Path
import argparse,sys,struct,json,hashlib,math
root=Path(__file__).resolve().parents[1]
work=(root/'../../../Build/Tasks/YMCC-Steam-HUD-Probe').resolve()
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=work/'hwinfo-average-window-20261017');args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(work/'hwinfo-unused-clock-20261016/python-libs'))
import unicorn,pefile
from unicorn.x86_const import *
prior=work/'hwinfo-producer-budget-20261016';raw=(prior/'HWiNFO64-static-inspection.exe').read_bytes();proof=json.loads((prior/'UPSTREAM-EVIDENCE.json').read_text(encoding='utf-8'))
assert hashlib.sha256(raw).hexdigest()==proof['inspectionCopySha256']=='5ef21625978eb2d2f6906ad31b9817ce8235be80dca556404f1d83e4674e366a'
assert proof['localMatchesOfficialRelease'] and not proof['ranHwinfoBinary']
pe=pefile.PE(data=raw,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase
entry,end=0x1402a1fd6,0x1402a216f;code_page=entry&~0xfff;variable=0x141135b40;allocator=0x140179b04
stack,obj,history=0x1000000,0x2000000,0x3000000
def dbl(v):return struct.pack('<d',v)
def bits(v):return struct.unpack('<Q',dbl(v))[0]
# Preserve both original code pages, including the branch crossing their boundary.
offset=pe.get_offset_from_rva(code_page-base);code=raw[offset:offset+8192]
assert code[entry-code_page:entry-code_page+8].hex()=='4439a4244c010000'
assert struct.unpack('<i',raw[pe.get_offset_from_rva(0x140a8d340-base)+24:pe.get_offset_from_rva(0x140a8d340-base)+28])[0]==0

def run(window,count,current,minv,maxv,average,cold=False):
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_64)
 u.mem_map(code_page,8192);u.mem_write(code_page,code)
 for addr,size in [(variable&~0xfff,4096),(allocator&~0xfff,4096),(stack,0x20000),(obj,4096),(history,8192)]:u.mem_map(addr,size)
 u.mem_write(allocator,b'\xc3');u.mem_write(variable,struct.pack('<i',window))
 u.mem_write(obj+0x18,dbl(current)+dbl(minv)+dbl(maxv)+dbl(average))
 u.mem_write(obj+0x44,struct.pack('<I',count));u.mem_write(obj+0x48,struct.pack('<Q',0 if cold or not window else history))
 u.mem_write(history,b''.join(dbl((i%101)*0.25+0.125) for i in range(1000)))
 u.reg_write(UC_X86_REG_RSP,stack+0x10000);u.reg_write(UC_X86_REG_RDI,obj)
 u.reg_write(UC_X86_REG_R12,0);u.reg_write(UC_X86_REG_R14,1)
 for reg,val in [(UC_X86_REG_XMM6,current),(UC_X86_REG_XMM7,0.0),(UC_X86_REG_XMM9,1.0)]:u.reg_write(reg,bits(val))
 result={'instructions':0,'historyShifts':0,'historySumReads':0,'allocationBytes':0,'allocations':0}
 def hook(machine,addr,size,data):
  result['instructions']+=1
  if addr==allocator:
   size=machine.reg_read(UC_X86_REG_RCX);assert window==1000 and size==8000
   result['allocations']+=1;result['allocationBytes']+=size;machine.reg_write(UC_X86_REG_RAX,history)
  else:assert entry<=addr<end,hex(addr)
  if addr==0x1402a2055:result['historyShifts']+=1
  if addr==0x1402a20a4:result['historySumReads']+=1
 u.hook_add(unicorn.UC_HOOK_CODE,hook);u.emu_start(entry,end,count=100000)
 assert u.reg_read(UC_X86_REG_RIP)==end
 state=bytes(u.mem_read(obj+0x18,24)).hex()+struct.pack('<Q',u.reg_read(UC_X86_REG_XMM6)&((1<<64)-1)).hex()
 result['currentMinMaxAndXmm6Bits']=state
 result['average']=struct.unpack('<d',u.mem_read(obj+0x30,8))[0]
 assert bytes(u.mem_read(obj+0x18,8))==dbl(current)
 assert bytes(u.mem_read(obj+0x20,8))==dbl(min(minv,current))
 assert bytes(u.mem_read(obj+0x28,8))==dbl(max(maxv,current))
 assert u.reg_read(UC_X86_REG_XMM6)&((1<<64)-1)==bits(current)
 return result

cases=[];different=0
for name,v in [('cpu-power',30.0),('temperature',75.0),('presented-fps',180.0),('low-fps',101.0),('frametime',1000/180),('battery-power',-42.0),('zero',0.0),('voltage',1.05)]:
 for count,cold in [(0,True),(1,False),(250,False),(1000,False)]:
  for minv,maxv in [(v-10,v+10),(v+1,v-1)]:
   before=run(1000,count,v,minv,maxv,17.25,cold);after=run(0,count,v,minv,maxv,17.25,cold)
   assert before['currentMinMaxAndXmm6Bits']==after['currentMinMaxAndXmm6Bits']
   assert after['historyShifts']==after['historySumReads']==after['allocations']==after['allocationBytes']==0
   if cold:assert before['allocations']==1 and before['historySumReads']==1
   if count==1000:assert before['historyShifts']==999 and before['historySumReads']==1000
   if before['average']!=after['average']:different+=1
   cases.append({'name':name,'count':count,'cold':cold,'before':before,'after':after})
assert different>0
result={'ok':True,'cases':cases,'caseCount':len(cases),'averageDifferencesExpected':different,'imageSha256':hashlib.sha256(raw).hexdigest(),'originalBlockSha256':hashlib.sha256(code[entry-code_page:end-code_page]).hexdigest(),'emulatorVersion':unicorn.__version__,'realHwinfoExecuted':False,'realAPICalls':False,'hardwareSampling':False,'wholeProcessCpuBenchmark':False,'scope':'Original averaging block only. Current/min/max/outgoing current unchanged; historical Average intentionally changes. Invocation frequency and total process CPU are not asserted.'}
(out/'EMULATED-AVERAGE-RESULT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='cases'},indent=2))
