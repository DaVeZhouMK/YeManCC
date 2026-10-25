"""Pinned upstream USBC_PD init/poll instructions; all IO/heap APIs are stubs.
No research executable, HWiNFO process, port or registry API is run.
"""
from pathlib import Path
import sys,struct,hashlib,json
ROOT=Path(__file__).resolve().parent.parent
BASE=ROOT/"../../../Build/Tasks/YMCC-Steam-HUD-Probe"
OUT=(BASE/"hwinfo-pd-budget-20261016").resolve()
sys.path.insert(0,str((BASE/"hwinfo-unused-clock-20261016/python-libs").resolve()))
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE
from unicorn.x86_const import *
IMAGE=(BASE/"hwinfo-producer-budget-20261016/HWiNFO64-static-inspection.exe").resolve()
assert hashlib.sha256(IMAGE.read_bytes()).hexdigest()=="5ef21625978eb2d2f6906ad31b9817ce8235be80dca556404f1d83e4674e366a"
PE=pefile.PE(str(IMAGE));BLOB=PE.get_memory_mapped_image();IB=PE.OPTIONAL_HEADER.ImageBase
OPTION=next(r for r in json.loads((BASE/"hwinfo-producer-budget-20261016/VERIFIED-OPTION-TABLE.json").read_text())["entries"] if r["name"]=="USBC_PD")
assert OPTION=={"name":"USBC_PD","entryVA":"0x140a8cfe0","type":0,"destinationVA":"0x14113560c","default":1}
ENTRY=struct.unpack_from("<6Q",BLOB,int(OPTION["entryVA"],16)-IB)
# Validate the option descriptor itself against the original image, not just a report.
assert ENTRY[1]==0 and ENTRY[2]==int(OPTION["destinationVA"],16) and ENTRY[3]==1
assert BLOB[ENTRY[0]-IB:ENTRY[0]-IB+16]=="USBC_PD\0".encode("utf-16le")

q=lambda n:struct.pack("<Q",n)
d=lambda n:struct.pack("<I",n)
def run(flag,route,instance=0):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(IB,(len(BLOB)+4095)&~4095);u.mem_write(IB,BLOB)
 heap,stack,stub=0x20000000,0x30000000,0x40000000
 u.mem_map(heap,0x10000);u.mem_map(stack,0x10000);u.mem_map(stub,0x1000);u.mem_write(stub,b"\xc3"*0x1000)
 sensor=heap+0x1000;rsp=stack+0x8000;halt=stub+0xff0;allocated=[]
 u.mem_write(sensor+0x1c,d(instance));u.mem_write(sensor+0x10,b"\x01")
 u.mem_write(0x14113560c,d(flag))
 counts={"flag":flag,"route":route,"instance":instance,"allocations":0,"portProbeCalls":0,"wordQueries":0,"byteQueries":0,"statusQueries":0,"portCloseCalls":0,"instructions":0}
 def ret(value=0):
  top=u.reg_read(UC_X86_REG_RSP);target=struct.unpack("<Q",u.mem_read(top,8))[0]
  u.reg_write(UC_X86_REG_RAX,value);u.reg_write(UC_X86_REG_RSP,top+8);u.reg_write(UC_X86_REG_RIP,target)
 def hook(uc,at,size,data):
  counts["instructions"]+=1
  if at==halt:uc.emu_stop();return
  if at==0x140492838:
   address=heap+0x4000+len(allocated)*0x100;allocated.append(address);counts["allocations"]+=1;ret(address);return
  if at in (0x140367d90,0x140368648):
   counts["portProbeCalls"]+=1;obj=uc.reg_read(UC_X86_REG_RCX)
   success=(at==0x140367d90 and route=="native-port")or(at==0x140368648 and route=="secondary-port")
   if success:uc.mem_write(obj,q(0x1234));uc.mem_write(obj+0x30,struct.pack("<H",0x5450))
   ret(1 if success else 0);return
  if at==0x140367408:counts["wordQueries"]+=1;ret(0 if route=="unsupported-port" else 0x8853);return
  if at in (0x140368794,0x1403c3b60,0x1403673c8):counts["byteQueries"]+=1;ret(1);return
  if at==0x140368348:counts["statusQueries"]+=1;ret(0x13);return
  if at in (0x140368274,0x140368738):counts["portCloseCalls"]+=1;ret();return
  if at in (0x140179ba8,0x140130180,0x1402442f8):ret();return
  if at==0x14062da80:ret(uc.reg_read(UC_X86_REG_RAX));return
 u.hook_add(UC_HOOK_CODE,hook)
 u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RCX,sensor);u.mem_write(rsp,q(halt))
 u.emu_start(0x140367480,0,count=10000)
 group=u.reg_read(UC_X86_REG_RAX)&0xffffffff;counts["initializedGroup"]=hex(group)
 if group:
  u.mem_write(sensor+0x20,d(group));u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RCX,sensor);u.mem_write(rsp,q(halt))
  u.emu_start(0x140367774,0,count=10000)
 counts["externalQueryCount"]=sum(counts[n] for n in ["portProbeCalls","wordQueries","byteQueries","statusQueries"])
 if flag==0:
  assert group==0 and counts["allocations"]==0 and counts["externalQueryCount"]==0
 else:
  assert counts["externalQueryCount"]>0
  assert group==(0 if route=="unsupported-port" else 0xf0005452 if route=="native-port" and instance==0 else 0xf0008853)
 return counts
cases=[]
for route,instance in [("native-port",0),("secondary-port",0),("direct-bus",0),("unsupported-port",0),("direct-bus",1),("direct-bus",31)]:
 before=run(1,route,instance);after=run(0,route,instance);cases.append({"name":route+"-"+str(instance),"before":before,"after":after})
r={"ok":True,"checks":len(cases),"cases":cases,"imageSha256":hashlib.sha256(IMAGE.read_bytes()).hexdigest(),"blockHashes":{n:hashlib.sha256(BLOB[a-IB:b-IB]).hexdigest() for n,a,b in [("initialize",0x140367480,0x140367774),("poll",0x140367774,0x14036789e)]},"nativeHwinfoRan":False,"hardwareCalls":False,"registryWrites":False,"cpuPercentageMeasured":False,"pollInvocationScope":"Initialization source gate and original per-instance poll; not the entire upstream scheduler"}
(OUT/"PD-EMULATION-RESULT.json").write_text(json.dumps(r,indent=2)+"\n",encoding="utf-8")
print(json.dumps({"ok":True,"checks":len(cases),"nativeBeforeCalls":cases[0]["before"]["externalQueryCount"],"nativeAfterCalls":cases[0]["after"]["externalQueryCount"]},indent=2))
