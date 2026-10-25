"""Emulate only the original optional NVIDIA clock instruction block.
All external calls are inert stubs; HWiNFO and GPU/Windows APIs never execute.
This proves branch operation counts for the pinned image, not real CPU usage.
"""
from pathlib import Path
import argparse,sys,struct,json,hashlib

root=Path(__file__).resolve().parents[1]
default_out=(root/'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-unused-clock-20261016').resolve()
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=default_out);args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(out/'python-libs'))
import unicorn
from unicorn.x86_const import *
import pefile
prior=out.parent/'hwinfo-producer-budget-20261016';image=prior/'HWiNFO64-static-inspection.exe';raw=image.read_bytes();proof=json.loads((prior/'UPSTREAM-EVIDENCE.json').read_text(encoding='utf-8'))
assert hashlib.sha256(raw).hexdigest()==proof['inspectionCopySha256']
assert proof['localMatchesOfficialRelease'] and not proof['ranHwinfoBinary']
pe=pefile.PE(data=raw,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase
entry,end=0x1403e41dd,0x1403e42ff
code_page=entry&~0xfff;flag_address=0x141135a90;api_pointer=0x140aec6f0
memset_stub,wait_stub,api_stub=0x140736b70,0x1402167e0,0x7fe00000
stack,device=0x1000000,0x2000000
code_offset=pe.get_offset_from_rva(code_page-base);page=raw[code_offset:code_offset+4096]
assert page[entry-code_page:entry-code_page+7].hex()=='443935ac18d500'
# Assert the original branch still reaches the common normal-clock continuation.
assert page[entry-code_page+7:entry-code_page+13].hex()=='0f8415010000'

def run(flag,available=True,first_status=(0,0,0),read_status=0):
    uc=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_64)
    for page_address in {code_page,flag_address&~0xfff,api_pointer&~0xfff,memset_stub&~0xfff,wait_stub&~0xfff,api_stub&~0xfff}:
        uc.mem_map(page_address,4096)
    uc.mem_map(stack,0x20000);uc.mem_map(device,0x2000)
    uc.mem_write(code_page,page);uc.mem_write(flag_address,struct.pack('<I',flag));uc.mem_write(api_pointer,struct.pack('<Q',api_stub if available else 0))
    for stub in [memset_stub,wait_stub,api_stub]:uc.mem_write(stub,b'\xc3')
    uc.reg_write(UC_X86_REG_RSP,stack+0x10000);uc.reg_write(UC_X86_REG_RBP,stack+0x8000);uc.reg_write(UC_X86_REG_RSI,device);uc.reg_write(UC_X86_REG_R14,0)
    uc.reg_write(UC_X86_REG_XMM6,struct.unpack('<Q',struct.pack('<d',1000.0))[0])
    counts={'flag':flag,'apiAvailable':available,'counterInitializations':0,'counterReads':0,'scratchInitializations':0,'waitHelperCalls':0,'waitArguments':[],'instructions':0,'normalClockFieldWrites':0,'measuredFieldWrites':[]}
    normal_offsets={0xd10,0xd48,0xd50,0xd68}
    for n in normal_offsets:uc.mem_write(device+n,struct.pack('<d',1500.0+n))
    def hook_code(u,address,size,data):
        counts['instructions']+=1
        if address==memset_stub:
            count=u.reg_read(UC_X86_REG_R8);assert count==0x468
            u.mem_write(u.reg_read(UC_X86_REG_RCX),b'\0'*count);counts['scratchInitializations']+=1
        elif address==wait_stub:
            counts['waitHelperCalls']+=1;counts['waitArguments'].append(u.reg_read(UC_X86_REG_RCX))
        elif address==api_stub:
            index=counts['counterInitializations']
            if index<3:
                counts['counterInitializations']+=1;status=first_status[index]
            else:
                counts['counterReads']+=1;status=read_status
                u.mem_write(u.reg_read(UC_X86_REG_RDX)+20,struct.pack('<I',2400000+counts['counterReads']*1000))
            u.reg_write(UC_X86_REG_RAX,status&0xffffffff)
        else:
            assert entry<=address<end or 0x1403e4427<=address<0x1403e444b,hex(address)
    def hook_write(u,access,address,size,value,data):
        if device<=address<device+0x2000:
            offset=address-device
            if offset in normal_offsets:counts['normalClockFieldWrites']+=1
            counts['measuredFieldWrites'].append(hex(offset))
    uc.hook_add(unicorn.UC_HOOK_CODE,hook_code);uc.hook_add(unicorn.UC_HOOK_MEM_WRITE,hook_write)
    uc.emu_start(entry,end,count=1000)
    assert uc.reg_read(UC_X86_REG_RIP)==end
    assert counts['normalClockFieldWrites']==0
    counts['reachedCommonNormalClockContinuation']=True
    return counts

cases=[]
for label,available,statuses,read_status in [
    ('available-success',True,(0,0,0),0),
    ('API-unavailable',False,(0,0,0),0),
    ('counter-init-failed',True,(1,1,1),0),
    ('one-counter-init-failed',True,(0,1,0),0),
    ('counter-read-failed',True,(0,0,0),1),
]:
    before=run(1,available,statuses,read_status);after=run(0,available,statuses,read_status)
    assert after['counterInitializations']==after['counterReads']==after['waitHelperCalls']==after['scratchInitializations']==0
    assert not after['measuredFieldWrites']
    if available:
        assert before['counterInitializations']==3
        assert before['counterReads']==sum(v==0 for v in statuses)
        assert before['waitHelperCalls']==1 and before['waitArguments']==[30]
    else:assert before['counterInitializations']==before['counterReads']==before['waitHelperCalls']==0
    cases.append({'name':label,'before':before,'after':after})
result={'ok':True,'checks':len(cases),'imageSha256':hashlib.sha256(raw).hexdigest(),'blockSha256':hashlib.sha256(page).hexdigest(),'emulatorVersion':unicorn.__version__,'cases':cases,'realHwinfoExecuted':False,'realGPUOrWindowsAPICalls':False,'hardwareSampling':False,'cpuPercentageBenchmark':False,'scope':'Pinned original x64 instruction block in an emulator, all external calls stubbed; stops at common continuation before normal clocks. Wait argument is 30, not a measured CPU time or a 30ms assertion.'}
(out/'EMULATED-BRANCH-RESULT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'ok':True,'cases':len(cases),'successBefore':cases[0]['before'],'successAfter':cases[0]['after']},indent=2))
