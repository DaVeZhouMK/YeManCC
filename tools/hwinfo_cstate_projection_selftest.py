"""Original C-state projection and group gate; cached CPU state and APIs inert.
This measures skipped per-reading work, not suppression of CPU MSR sampling.
"""
from pathlib import Path
import sys,struct,hashlib,json
ROOT=Path(__file__).resolve().parent.parent;BASE=ROOT/"../../../Build/Tasks/YMCC-Steam-HUD-Probe";OUT=(BASE/"hwinfo-cstate-projection-20261016").resolve()
sys.path.insert(0,str((BASE/"hwinfo-unused-clock-20261016/python-libs").resolve()))
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE,UC_HOOK_MEM_READ
from unicorn.x86_const import *
IMAGE=(BASE/"hwinfo-producer-budget-20261016/HWiNFO64-static-inspection.exe").resolve();assert hashlib.sha256(IMAGE.read_bytes()).hexdigest()=="5ef21625978eb2d2f6906ad31b9817ce8235be80dca556404f1d83e4674e366a"
PE=pefile.PE(str(IMAGE));BLOB=PE.get_memory_mapped_image();IB=PE.OPTIONAL_HEADER.ImageBase
assert struct.unpack_from("<Q",BLOB,0x1408ba2d0+14*8-IB)[0]==0x1402965d8
assert BLOB[0x1402966f5-IB:0x1402966fa-IB]==b"\xb8\x02\x03\x00\xf0" # F0000302 return from actual initialization.
q=lambda n:struct.pack("<Q",n)
d=lambda n:struct.pack("<I",n)
def run(enabled,instance=0):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(IB,(len(BLOB)+4095)&~4095);u.mem_write(IB,BLOB)
 heap,stack,stub=0x20000000,0x30000000,0x40000000;u.mem_map(heap,0x20000);u.mem_map(stack,0x10000);u.mem_map(stub,0x1000);u.mem_write(stub,b"\xc3"*0x1000)
 group,sensor,slot,reading,manager,package,core=[heap+n for n in [0x1000,0x2000,0x3000,0x4000,0x5000,0x7000,0x10000]];rsp,rbp,halt=stack+0x7000,stack+0x5000,stub+0xff0
 u.mem_write(group,q(sensor)+d(1)+d(0));u.mem_write(sensor,q(0x1408ba2d0));u.mem_write(sensor+0x1c,d(instance)+d(0xf0000302));u.mem_write(slot,q(group));u.mem_write(rsp+0x60,q(123));u.mem_write(rsp+0x78,q(123))
 # Original RIP-relative CPU-owner slot used by the exact C-state method.
 owner=0x14029673c+7+0x8566b5;u.mem_write(owner,q(manager))
 for index in range(32):u.mem_write(manager+index*8+8,q(package))
 u.mem_write(package+0x10,q(core))
 for index in range(12):u.mem_write(package+(index+0xca)*5*8,struct.pack("<d",43.0))
 u.mem_write(0x1407511d0,q(stub+0x10));u.mem_write(0x1407511c0,q(stub+0x20));u.mem_write(0x140751d98,q(stub+0x30))
 u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RBP,rbp);u.reg_write(UC_X86_REG_R15,group)
 result={"enabled":enabled,"instance":instance,"groupKey":None,"loadedEnabled":None,"cachedCpuLookups":0,"labelFormattingCalls":0,"projectedValues":[],"cachedResidencyBytesRead":0}
 phase="load"
 def u64(at):return struct.unpack("<Q",u.mem_read(at,8))[0]
 def text(at):
  buf=[]
  for i in range(256):
   c=bytes(u.mem_read(at+2*i,2))
   if c==b"\0\0":return b"".join(buf).decode("utf-16le")
   buf.append(c)
  raise AssertionError("Unbounded emulated string")
 def ret(value=0):
  top=u.reg_read(UC_X86_REG_RSP);target=u64(top);u.reg_write(UC_X86_REG_RAX,value);u.reg_write(UC_X86_REG_RSP,top+8);u.reg_write(UC_X86_REG_RIP,target)
 def code(uc,at,size,data):
  if phase=="load" and at==0x1403b1a98:uc.emu_stop();return
  if phase=="gate" and at in (0x14035a2b9,0x14035bd99):result["continuesToPerReadingLoop"]=at==0x14035a2b9;uc.emu_stop();return
  if at==halt:uc.emu_stop();return
  if at==0x1400211f4:
   assert text(uc.reg_read(UC_X86_REG_R8))=="%08X_%d";current=uc.reg_read(UC_X86_REG_RSP);item=u64(current+0x28)&0xffffffff;family=uc.reg_read(UC_X86_REG_R9)&0xffffffff;key=f"{family:08X}_{item}";result["groupKey"]=key;uc.mem_write(uc.reg_read(UC_X86_REG_RCX),key.encode("utf-16le")+b"\0\0");ret();return
  if at==stub+0x10:
   assert text(uc.reg_read(UC_X86_REG_RDX))==result["groupKey"];uc.mem_write(u64(uc.reg_read(UC_X86_REG_RSP)+0x28),q(456));ret();return
  if at==stub+0x20:
   assert text(uc.reg_read(UC_X86_REG_RDX))=="Enabled";current=uc.reg_read(UC_X86_REG_RSP);uc.mem_write(uc.reg_read(UC_X86_REG_R9),d(4));uc.mem_write(u64(current+0x28),d(enabled));uc.mem_write(u64(current+0x30),d(4));ret();return
  if at==stub+0x30:uc.mem_write(uc.reg_read(UC_X86_REG_RCX),q(123));ret(1);return
  if at==0x140018580:ret();return
  if at==0x14012ce74:result["cachedCpuLookups"]+=1;return
  if at==0x140018584:result["labelFormattingCalls"]+=1;ret();return
 def mem(uc,access,address,size,value,data):
  if phase=="projection" and package+0x1f90<=address<package+0x2300:result["cachedResidencyBytesRead"]+=size
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_READ,mem)
 u.emu_start(0x1403b1a04,0,count=2000);result["loadedEnabled"]=struct.unpack("<I",u.mem_read(group+8,4))[0]
 phase="gate";u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RDI,slot);u.reg_write(UC_X86_REG_R13,sensor);u.emu_start(0x14035a26b,0,count=2000)
 if result["continuesToPerReadingLoop"]:
  phase="projection"
  for index in range(12):
   u.reg_write(UC_X86_REG_RSP,rsp);u.mem_write(rsp,q(halt));u.reg_write(UC_X86_REG_RCX,sensor);u.reg_write(UC_X86_REG_RDX,index);u.reg_write(UC_X86_REG_R8,reading);u.emu_start(0x140296710,0,count=2000);assert u.reg_read(UC_X86_REG_RAX)==1;result["projectedValues"].append(struct.unpack("<d",u.mem_read(reading+8,8))[0])
 assert result["groupKey"]==f"F0000302_{instance}" and result["loadedEnabled"]==enabled
 assert result["cachedCpuLookups"]==(12 if enabled else 0);assert result["labelFormattingCalls"]==(12 if enabled else 0)
 assert result["projectedValues"]==([43.0]*12 if enabled else [])
 return result
cases=[{"instance":i,"before":run(1,i),"after":run(0,i)} for i in [0,1,7,31]]
r={"ok":True,"checks":len(cases),"cases":cases,"imageSha256":hashlib.sha256(IMAGE.read_bytes()).hexdigest(),"blocks":{n:hashlib.sha256(BLOB[a-IB:b-IB]).hexdigest() for n,a,b in [("reader",0x1403b1a04,0x1403b1a98),("groupGate",0x14035a26b,0x14035a2b9),("projection",0x140296710,0x140296b7b)]},"hardwareCalls":False,"registryWrites":False,"nativeHwinfoRan":False,"underlyingCpuMsrAcquisitionSuppressionProven":False}
(OUT/"CSTATE-EMULATION-RESULT.json").write_text(json.dumps(r,indent=2)+"\n",encoding="utf-8");print(json.dumps({"ok":True,"checks":len(cases),"beforeProjectedRows":len(cases[0]["before"]["projectedValues"]),"afterProjectedRows":len(cases[0]["after"]["projectedValues"])},indent=2))
