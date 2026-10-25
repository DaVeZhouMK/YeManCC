"""Execute pinned HWiNFO group-load and polling instructions in isolation.
All registry/string/device/Windows boundaries are inert stubs. No HWiNFO runs.
"""
from pathlib import Path
import sys, struct, hashlib, json
ROOT=Path(__file__).resolve().parent.parent
BASE=ROOT/"../../../Build/Tasks/YMCC-Steam-HUD-Probe"
OUT=(BASE/"hwinfo-group-plan-20261016").resolve()
sys.path.insert(0,str((BASE/"hwinfo-unused-clock-20261016/python-libs").resolve()))
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
image=(BASE/"hwinfo-producer-budget-20261016/HWiNFO64-static-inspection.exe").resolve()
assert hashlib.sha256(image.read_bytes()).hexdigest()=="5ef21625978eb2d2f6906ad31b9817ce8235be80dca556404f1d83e4674e366a"
pe=pefile.PE(str(image)); blob=pe.get_memory_mapped_image(); IB=pe.OPTIONAL_HEADER.ImageBase
assert IB==0x140000000
q=lambda n:struct.pack("<Q",n)
d=lambda n:struct.pack("<I",n)
def run(name,value=0,status=0,instance=0):
    u=Uc(UC_ARCH_X86,UC_MODE_64)
    u.mem_map(IB,(len(blob)+4095)&~4095);u.mem_write(IB,blob)
    heap=0x20000000;stack=0x30000000;stub=0x40000000
    u.mem_map(heap,0x60000);u.mem_map(stack,0x10000);u.mem_map(stub,0x1000)
    u.mem_write(stub,b"\xc3"*0x1000)
    rsp=stack+0x7000;rbp=stack+0x5000
    group=heap+0x1000;sensor=heap+0x2000;slot=heap+0x3000
    u.mem_write(group,q(sensor)+d(1)+d(0));u.mem_write(sensor,q(0x1408bb000))
    u.mem_write(sensor+0x1c,d(instance)+d(0xf0000101));u.mem_write(slot,q(group))
    u.mem_write(rsp+0x60,q(heap+0x4000));u.mem_write(rsp+0x78,q(123))
    u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RBP,rbp);u.reg_write(UC_X86_REG_R15,group)
    u.mem_write(0x1407511d0,q(stub+0x10));u.mem_write(0x1407511c0,q(stub+0x20))
    u.mem_write(0x140751d98,q(stub+0x30))
    result={"name":name,"instance":instance,"registryStatus":status,"groupKey":None,"loadedEnabled":None,"diskSourceQueries":0,"diskQueryControls":[],"groupDisabledCleanupCalls":0}
    phase="load"
    trace=[]
    def u64(at):return struct.unpack("<Q",u.mem_read(at,8))[0]
    def text(at):
        chars=[]
        for i in range(256):
            c=bytes(u.mem_read(at+2*i,2))
            if c==b"\0\0":return b"".join(chars).decode("utf-16le")
            chars.append(c)
        raise AssertionError("Unbounded emulated string")
    def ret(value=0):
        top=u.reg_read(UC_X86_REG_RSP);target=u64(top)
        u.reg_write(UC_X86_REG_RAX,value);u.reg_write(UC_X86_REG_RSP,top+8);u.reg_write(UC_X86_REG_RIP,target)
    def hook(uc,at,size,data):
        trace.append(hex(at))
        if len(trace)>16:trace.pop(0)
        if phase=="load" and at==0x1403b1a98:uc.emu_stop();return
        if phase=="poll" and at in (0x14035a2b9,0x14035bd99):result["pollContinuation"]=hex(at);uc.emu_stop();return
        if at==0x1400211f4:
            assert phase=="load"
            fmt=text(u.reg_read(UC_X86_REG_R8));assert fmt=="%08X_%d"
            current=u.reg_read(UC_X86_REG_RSP)
            item=u64(current+0x28)&0xffffffff
            group_id=u.reg_read(UC_X86_REG_R9)&0xffffffff
            key=f"{group_id:08X}_{item}";result["groupKey"]=key
            uc.mem_write(uc.reg_read(UC_X86_REG_RCX),key.encode("utf-16le")+b"\0\0");ret();return
        if at==stub+0x10:
            assert text(uc.reg_read(UC_X86_REG_RDX))==result["groupKey"]
            uc.mem_write(u64(uc.reg_read(UC_X86_REG_RSP)+0x28),q(456));ret();return
        if at==stub+0x20:
            assert text(uc.reg_read(UC_X86_REG_RDX))=="Enabled"
            if status==0:
                current=uc.reg_read(UC_X86_REG_RSP)
                uc.mem_write(uc.reg_read(UC_X86_REG_R9),d(4))
                uc.mem_write(u64(current+0x28),d(value))
                uc.mem_write(u64(current+0x30),d(4))
            ret(status);return
        if at==stub+0x30:uc.mem_write(uc.reg_read(UC_X86_REG_RCX),q(123));ret(1);return
        if at==0x140018580:result["groupDisabledCleanupCalls"]+=1;ret();return
        if at==0x14062da80:ret(uc.reg_read(UC_X86_REG_RAX));return
        if at in (0x1400214e4,0x140736f30):ret();return
        if at==0x1401cd9f4:
            control=uc.reg_read(UC_X86_REG_R8)&0xffffffff
            assert control==0x70020
            result["diskSourceQueries"]+=1;result["diskQueryControls"].append(hex(control));ret();return
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(0x1403b1a04,0,count=2000)
    result["loadedEnabled"]=struct.unpack("<I",u.mem_read(group+8,4))[0]
    phase="poll"
    u.reg_write(UC_X86_REG_RSP,rsp);u.reg_write(UC_X86_REG_RBP,rbp)
    u.reg_write(UC_X86_REG_RDI,slot);u.reg_write(UC_X86_REG_R13,sensor)
    try:u.emu_start(0x14035a26b,0,count=2000)
    except Exception:
        print({"recentInstructions":trace,"rsp":hex(u.reg_read(UC_X86_REG_RSP)),"rcx":hex(u.reg_read(UC_X86_REG_RCX))});raise
    assert result["groupKey"]==f"F0000101_{instance}"
    expected=value if status==0 else 1
    assert result["loadedEnabled"]==expected
    assert result["diskSourceQueries"]==(1 if expected else 0)
    assert result["groupDisabledCleanupCalls"]==(0 if expected else 1)
    return result
cases=[run("enabled-source",1),run("disabled-source",0),run("legacy-nonzero-remains-enabled",2),run("absent-entry-retains-enabled",0,2),run("query-error-retains-enabled",0,5)]
for instance in [1,7,31]:cases.append(run(f"disabled-instance-{instance}",0,0,instance))
receipt={"ok":True,"checks":len(cases),"cases":cases,"imageSha256":hashlib.sha256(image.read_bytes()).hexdigest(),"blockHashes":{name:hashlib.sha256(blob[start-IB:end-IB]).hexdigest() for name,start,end in [("loader",0x1403b1a04,0x1403b1a98),("pollGate",0x14035a26b,0x14035a2b9),("diskPoll",0x1402998d8,0x140299977)]},"nativeHwinfoRan":False,"registryWrites":False,"hardwareCalls":False,"cpuPercentageMeasured":False}
(OUT/"DISK-GROUP-EMULATION-RESULT.json").write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
print(json.dumps({"ok":True,"checks":len(cases),"beforeQueries":cases[0]["diskSourceQueries"],"afterQueries":cases[1]["diskSourceQueries"]},indent=2))
