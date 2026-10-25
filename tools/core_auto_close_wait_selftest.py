"""Freeze actual autoCloseThread/acNameMatch/acCloseEnum bodies and test without product/hardware."""
import argparse,hashlib,json,os,re,shutil,subprocess
from datetime import datetime
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def extract(text,marker):
    start=text.index(marker);opened=text.index('{',start);depth=0
    for at in range(opened,len(text)):
        if text[at]=='{':depth+=1
        elif text[at]=='}':
            depth-=1
            if depth==0:return text[start:at+1]
    raise ValueError('unclosed function '+marker)
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
before=a.baseline.read_text(encoding='utf-8-sig');current=(root/'native/main.cpp').read_text(encoding='utf-8-sig')
markers=['static bool acNameMatch(','static BOOL CALLBACK acCloseEnum(','static DWORD WINAPI autoCloseThread(']
functions={side:{m:extract(text,m) for m in markers} for side,text in [('before',before),('after',current)]}
# Unmodified process/window logic is checked byte-for-byte after normalizing the sole wait hunk.
old='// 5 秒轮询，分片 sleep 便于退出时快速响应\n        for (int i = 0; i < 50 && !g_summonQuit; i++) Sleep(100);'
new='// One interruptible wait preserves the 5s scan interval without 100ms wakeups.\n        if (!g_autoCloseWait.waitForNextScan(std::chrono::milliseconds(5000))) break;'
assert functions['before'][markers[0]]==functions['after'][markers[0]]
assert functions['before'][markers[1]]==functions['after'][markers[1]]
assert functions['before'][markers[2]].replace(old,new)==functions['after'][markers[2]],'Worker changed outside wait hunk'
assert not re.search(r'g_summonQuit\s*=\s*true',current)
assert current.count('g_autoCloseWait.requestStop();')==4,'all four shutdown writers must notify'
assert current.count('static ymcc::autoclose::ScanWait g_autoCloseWait{g_summonQuit};')==1
shims=r"""
static Scenario scenario;
static Observation observed;
static std::atomic<bool> g_summonQuit{false},g_autoCloseEnabled{false};
static std::mutex g_autoCloseMx;
static std::vector<std::string> g_autoCloseProcs;
static unsigned processIndex=0;
struct FakeProcess{DWORD pid;const wchar_t* exe;};
static const FakeProcess processes[]={{41,L"Other.exe"},{42,L"ONEXCONSOLE.EXE"},{43,L"AYASpace.exe"},{99,L"AYASpace.exe"}};
static std::wstring U2W(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
static void advance(unsigned amount){
    ++observed.waits;
    if(observed.now+amount>=scenario.stopAt){observed.now=scenario.stopAt;g_summonQuit=true;return;}
    observed.now+=amount;
    if(scenario.mode=="mixed"){
        g_autoCloseEnabled=observed.now>=7500&&observed.now<42500;
        g_autoCloseProcs=observed.now>=23000?std::vector<std::string>{"aya*.exe"}:std::vector<std::string>{"OneXConsole"};
    }
}
static void Sleep(DWORD ms){advance(ms);}
struct FakeWait{bool waitForNextScan(std::chrono::milliseconds interval){if(g_summonQuit)return false;advance(unsigned(interval.count()));return !g_summonQuit;}};
static FakeWait g_autoCloseWait;
static DWORD GetCurrentProcessId(){return 99;}
static HANDLE CreateToolhelp32Snapshot(DWORD,DWORD){observed.scans.push_back(observed.now);return scenario.mode=="snapshot-failure"?INVALID_HANDLE_VALUE:reinterpret_cast<HANDLE>(1);}
static BOOL Process32FirstW(HANDLE,PROCESSENTRY32W* entry){processIndex=0;entry->th32ProcessID=processes[0].pid;wcscpy_s(entry->szExeFile,processes[0].exe);return TRUE;}
static BOOL Process32NextW(HANDLE,PROCESSENTRY32W* entry){if(++processIndex>=std::size(processes))return FALSE;entry->th32ProcessID=processes[processIndex].pid;wcscpy_s(entry->szExeFile,processes[processIndex].exe);return TRUE;}
static BOOL CloseHandle(HANDLE){++observed.snapshotCloses;return TRUE;}
static DWORD GetWindowThreadProcessId(HWND h,DWORD* pid){*pid=DWORD(reinterpret_cast<ULONG_PTR>(h));return 1;}
static BOOL PostMessageW(HWND h,UINT message,WPARAM,LPARAM){if(message==WM_CLOSE)observed.closed.emplace_back(observed.now,DWORD(reinterpret_cast<ULONG_PTR>(h)));return TRUE;}
static BOOL EnumWindows(WNDENUMPROC callback,LPARAM lp){for(auto& process:processes)callback(reinterpret_cast<HWND>(ULONG_PTR(process.pid)),lp);return TRUE;}
static Observation run(const Scenario& input);
"""
(out/'auto-close-shims.inc').write_text(shims,encoding='utf-8')
for side in ('before','after'):
    tail='\nstatic Observation run(const Scenario& input){scenario=input;observed={};g_summonQuit=input.stopAt==0;g_autoCloseEnabled=input.mode!="disabled"&&input.mode!="mixed";g_autoCloseProcs=input.mode=="empty"?std::vector<std::string>{}:std::vector<std::string>{"OneXConsole.exe","aya*.exe"};autoCloseThread(nullptr);return observed;}\n'
    (out/f'auto-close-{side}.inc').write_text(functions[side][markers[0]]+'\nstruct AcCloseCtx { const std::unordered_set<DWORD>* pids; DWORD selfPid; };\n'+functions[side][markers[1]]+'\n'+functions[side][markers[2]]+tail,encoding='utf-8')
shutil.copy2(root/'native/auto_close_wait.h',out/'auto_close_wait.h')
shutil.copy2(root/'tools/core_auto_close_wait_selftest.cpp',out/'selftest.cpp')
vcvars=os.environ.get('YMCC_VCVARS64',r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
cmd=out/'build.cmd';exe=out/'selftest.exe'
cmd.write_text('@echo off\ncall "'+vcvars+'" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"'+str(out)+'" "'+str(out/'selftest.cpp')+'" /Fo"'+str(out/'selftest.obj')+'" /Fe"'+str(exe)+'"\nif errorlevel 1 exit /b 1\n"'+str(exe)+'"\nexit /b %errorlevel%\n',encoding='utf-8')
run=subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=out,capture_output=True,timeout=60)
log=run.stdout.decode('utf-8',errors='replace')+run.stderr.decode('utf-8',errors='replace')
(out/'test.log').write_text(log,encoding='utf-8')
receipt={'at':datetime.now().astimezone().isoformat(),'exitCode':run.returncode,'mainSha256':sha(root/'native/main.cpp'),'headerSha256':sha(root/'native/auto_close_wait.h'),'cppSha256':sha(root/'tools/core_auto_close_wait_selftest.cpp'),'runnerSha256':sha(Path(__file__)),'baselineSha256':sha(a.baseline),'matchedProcessAndCloseCodeUnchanged':True,'shutdownSignalPaths':4,'functionHashes':{side:{m:hashlib.sha256(s.encode()).hexdigest() for m,s in f.items()} for side,f in functions.items()}}
for line in log.splitlines():
    if line.startswith('{'):receipt['test']=json.loads(line)
(out/'RESULT.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt.get('test',{'exitCode':run.returncode,'log':str(out/'test.log')})))
raise SystemExit(run.returncode)
