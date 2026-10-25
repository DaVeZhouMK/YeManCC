// FAN1006: production selection and exact native probe/wrapper extracts.
// Win32 process/port discovery is a deterministic fixture; one separate case
// uses a short-lived inert child to verify real kernel-handle lifetime.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "fan_keepalive_process_selection.h"
using json=nlohmann::json;
struct Process { DWORD pid; std::wstring name=L"YeManFanHost.exe", image; bool live=true, query=true, waitFailed=false; };
struct Handle { std::shared_ptr<Process> process; };
struct Snapshot { std::vector<std::shared_ptr<Process>> rows; std::size_t index=0; };
std::map<DWORD,std::shared_ptr<Process>> processes;
std::wstring POWER_CONTROL_DIR=L"C:\\fixture";
bool listener=true,allowOpen=true,pathAllowed=true,faultOverride=false,throwPath=false,exitDuringPort=false;
DWORD portOwner=4; int inventories=0,snapshotCloses=0,opens=0,closes=0,imageChecks=0,pathChecks=0,portChecks=0,notes=0;
struct FixtureApi {
 using Handle=HANDLE;
 static HANDLE open(DWORD pid) noexcept {auto it=processes.find(pid);if(!allowOpen||it==processes.end())return nullptr;++opens;return static_cast<HANDLE>(new ::Handle{it->second});}
 static bool alive(HANDLE h) noexcept {const auto& p=static_cast<::Handle*>(h)->process;return p->live&&!p->waitFailed;}
 static void close(HANDLE h) noexcept {++closes;delete static_cast<::Handle*>(h);}
};
struct MockSession {bool enabled=false;}g_aiFanMockSession;
struct MockHost {DWORD pid=0;bool owns(DWORD value){return value==pid;}}g_aiFanMockHost;
struct FanHostIdentityProbe {bool processFound=false,imageMatched=false,portFound=false;DWORD matchedPid=0;};
ymcc::fan_process::KeepaliveSelection<FixtureApi>& fanKeepaliveProcessSelection(){static thread_local ymcc::fan_process::KeepaliveSelection<FixtureApi> selected;return selected;}
void reset(){fanKeepaliveProcessSelection().reset();processes.clear();listener=allowOpen=pathAllowed=true;faultOverride=throwPath=exitDuringPort=false;portOwner=4;inventories=snapshotCloses=opens=closes=imageChecks=pathChecks=portChecks=notes=0;g_aiFanMockSession.enabled=false;g_aiFanMockHost.pid=0;POWER_CONTROL_DIR=L"C:\\fixture";}
std::shared_ptr<Process> add(DWORD pid,const std::wstring& image=L"C:\\fixture\\fan-host-v2\\YeManFanHost.exe"){auto p=std::make_shared<Process>();p->pid=pid;p->image=image;processes[pid]=p;return p;}
HANDLE fakeSnapshot(DWORD,DWORD){++inventories;auto* s=new Snapshot;for(const auto& [_,p]:processes)if(p->live)s->rows.push_back(p);return s;}
bool fakeEntry(HANDLE h,PROCESSENTRY32W* e,bool next){auto* s=static_cast<Snapshot*>(h);if(next)++s->index;if(s->index>=s->rows.size())return false;const auto& p=s->rows[s->index];e->th32ProcessID=p->pid;wcsncpy_s(e->szExeFile,p->name.c_str(),_TRUNCATE);return true;}
BOOL fakeFirst(HANDLE h,PROCESSENTRY32W* e){return fakeEntry(h,e,false);}
BOOL fakeNext(HANDLE h,PROCESSENTRY32W* e){return fakeEntry(h,e,true);}
BOOL fakeCloseSnapshot(HANDLE h){++snapshotCloses;delete static_cast<Snapshot*>(h);return TRUE;}
std::wstring processImagePath(DWORD pid){++imageChecks;auto it=processes.find(pid);return it!=processes.end()&&it->second->query?it->second->image:std::wstring{};}
bool sameFinalPath(const std::wstring& a,const std::wstring& b){++pathChecks;if(throwPath)throw std::runtime_error("fixture path failure");return pathAllowed&&_wcsicmp(a.c_str(),b.c_str())==0;}
bool fanHostOwnsLoopbackPort(DWORD pid,unsigned short=8765){++portChecks;if(exitDuringPort){auto it=processes.find(pid);if(it!=processes.end())it->second->live=false;}return listener&&(portOwner==pid||portOwner==4);}
BOOL fakeQueryImage(HANDLE h,DWORD,wchar_t* buffer,DWORD* size){++imageChecks;const auto& p=static_cast<::Handle*>(h)->process;if(!p->query||p->image.size()>=*size)return FALSE;std::copy(p->image.begin(),p->image.end(),buffer);*size=static_cast<DWORD>(p->image.size());return TRUE;}
void fanHostNoteInstanceObserved(const FanHostIdentityProbe&){++notes;}
void fiExitHandoffApplyGateOverride(FanHostIdentityProbe* probe){if(faultOverride)*probe={};}
#define CreateToolhelp32Snapshot fakeSnapshot
#define Process32FirstW fakeFirst
#define Process32NextW fakeNext
#define CloseHandle fakeCloseSnapshot
#define QueryFullProcessImageNameW fakeQueryImage
#include "ProbeExtract.inc"
#include "LegacyExactExtract.inc"
#include "KeepaliveExactExtract.inc"
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef QueryFullProcessImageNameW

static int checks=0;static std::vector<std::string> failures;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void check(const char* name,F action){++checks;try{reset();action();fanKeepaliveProcessSelection().reset();require(closes==opens,"retained fixture handles not released");require(snapshotCloses==inventories,"snapshot handles not released");}catch(const std::exception& e){failures.push_back(std::string(name)+": "+e.what());fanKeepaliveProcessSelection().reset();}}
int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="--child"){Sleep(1500);return 0;}
#ifdef YMCC_FAULT_INJECT
 check("fault-injected keepalive continues full probe on every call",[]{add(71);require(fanHostExactProcessRunningForKeepalive()&&fanHostExactProcessRunningForKeepalive(),"full probe rejected");require(inventories==2&&opens==0,"fault injector incorrectly bypassed fresh gate");faultOverride=true;require(!fanHostExactProcessRunningForKeepalive(),"fault override ignored");});
#else
 check("stable same process omits repeated inventory but rechecks image/path/port",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial full probe failed");int paths=pathChecks,ports=portChecks,images=imageChecks;require(fanHostExactProcessRunningForKeepalive(),"warm check failed");require(inventories==1,"stable request repeated global process inventory");require(pathChecks>paths&&portChecks>ports&&imageChecks>images,"warm path skipped a current gate");});
 check("legacy exact check still inventories every invocation",[]{add(71);require(fanHostExactProcessRunning()&&fanHostExactProcessRunning(),"legacy failed");require(inventories==2,"exit/legacy probe gained a cached shortcut");});
 check("retired process cannot use retained pid",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");p->live=false;require(!fanHostExactProcessRunningForKeepalive(),"exited process still accepted");require(inventories==2,"exit did not trigger fresh discovery");});
 check("new exact successor after old exit is discovered immediately",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");p->live=false;add(72);require(fanHostExactProcessRunningForKeepalive(),"successor blocked");require(inventories==2,"successor not freshly discovered");require(fanHostExactProcessRunningForKeepalive()&&inventories==2,"successor selection not reused");});
 check("pid reuse cannot validate a replacement through old process object",[]{auto old=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");old->live=false;add(71,L"C:\\foreign\\YeManFanHost.exe");require(!fanHostExactProcessRunningForKeepalive(),"foreign recycled pid accepted");require(inventories==2,"pid reuse failed to re-enumerate");});
 check("same-pid exact replacement still requires fresh probe",[]{auto old=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");old->live=false;add(71);require(fanHostExactProcessRunningForKeepalive(),"exact replacement blocked");require(inventories==2,"old object reused for new process");});
 check("changed expected path is checked instead of trusting old success",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");POWER_CONTROL_DIR=L"D:\\changed";require(!fanHostExactProcessRunningForKeepalive(),"old expected path retained");});
 check("changed image is refused and drops selection",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");p->image=L"C:\\foreign\\YeManFanHost.exe";require(!fanHostExactProcessRunningForKeepalive(),"image mismatch accepted");require(inventories==2,"image failure not freshly probed");});
 check("unreadable image cannot pass a warm request",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");p->query=false;require(!fanHostExactProcessRunningForKeepalive(),"unreadable image trusted");});
 check("changed final path refuses both warm and full probes",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");pathAllowed=false;require(!fanHostExactProcessRunningForKeepalive(),"final path skipped");});
 check("listener loss cannot pass from cached identity",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");listener=false;require(!fanHostExactProcessRunningForKeepalive(),"missing listener trusted");});
 check("foreign port owner remains refused",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");portOwner=999;require(!fanHostExactProcessRunningForKeepalive(),"foreign listener accepted");});
 check("HTTP.sys and exact pid listener ownership retain existing policy",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"HTTP.sys rejected");portOwner=71;require(fanHostExactProcessRunningForKeepalive(),"exact process listener rejected");});
 check("capture-handle access denial falls back without blocking current control",[]{add(71);allowOpen=false;require(fanHostExactProcessRunningForKeepalive()&&fanHostExactProcessRunningForKeepalive(),"new synchronize right changed legacy result");require(inventories==2,"denied capture became trusted cache");});
 check("unusable retained handle falls back rather than authorizes or blocks",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");p->waitFailed=true;require(fanHostExactProcessRunningForKeepalive(),"handle check failure blocked full valid probe");require(inventories==2,"failed wait did not fall back");});
 check("mock session own-pid restriction remains live",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");g_aiFanMockSession.enabled=true;g_aiFanMockHost.pid=72;require(!fanHostExactProcessRunningForKeepalive(),"cache bypassed mock owner");});
 check("cold inventory still rejects differently named image",[]{auto p=add(71);p->name=L"Other.exe";require(!fanHostExactProcessRunningForKeepalive(),"cold name gate removed");});
 check("failed discovery is never negatively cached",[]{require(!fanHostExactProcessRunningForKeepalive(),"empty inventory accepted");add(71);require(fanHostExactProcessRunningForKeepalive(),"new reachable host blocked by old missing state");require(inventories==2,"missing result retained");});
 check("warm validation exception releases selection before next manual retry",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");throwPath=true;bool threw=false;try{fanHostExactProcessRunningForKeepalive();}catch(...){threw=true;}require(threw,"unexpected path error hidden as success");throwPath=false;require(fanHostExactProcessRunningForKeepalive(),"next current retry blocked");require(inventories==2,"exception left selection authoritative");});
 check("process exit during validation cannot use positive port evidence",[]{auto p=add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");exitDuringPort=true;require(!fanHostExactProcessRunningForKeepalive(),"exit after validation not detected");});
 check("idle reset releases selection and next arm discovers current host",[]{add(71);require(fanHostExactProcessRunningForKeepalive(),"initial failed");fanKeepaliveProcessSelection().reset();require(closes==1,"idle retained handle");require(fanHostExactProcessRunningForKeepalive()&&inventories==2,"rearm not freshly discovered");});
 check("one retained handle is replaced and closed exactly once",[]{auto& c=fanKeepaliveProcessSelection();add(71);add(72);c.remember(71);c.remember(72);require(closes==1&&opens==2,"replacement handle leak");c.reset();require(closes==2,"last handle leak");});
 // This case uses the real kernel adapter with an inert same-test executable.
 ++checks;
 try{
  wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);require(n>0&&n<32768,"test executable path unavailable");
  std::wstring cmd=L"\""+std::wstring(exe,n)+L"\" --child";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
  require(CreateProcessW(exe,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"inert child start failed");CloseHandle(pi.hThread);
  ymcc::fan_process::KeepaliveSelection<> actual;actual.remember(pi.dwProcessId);bool matched=actual.matches([&](DWORD pid,HANDLE h){wchar_t image[32768];DWORD length=32768;return pid==pi.dwProcessId&&QueryFullProcessImageNameW(h,0,image,&length)&&_wcsicmp(std::wstring(image,length).c_str(),exe)==0;});
  const DWORD ended=WaitForSingleObject(pi.hProcess,5000);if(ended!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,9);WaitForSingleObject(pi.hProcess,1000);}CloseHandle(pi.hProcess);
  require(matched,"real process handle/query not admitted");require(ended==WAIT_OBJECT_0,"inert child did not exit normally");require(!actual.matches([](DWORD,HANDLE){return true;}),"real exited process remained selected");
 }catch(const std::exception& e){failures.push_back(std::string("real kernel process-handle lifetime: ")+e.what());}
#endif
 std::cout<<json{{"ok",failures.empty()},{"checks",checks},{"failures",failures},{"scope","production selection header and native probe/wrapper extracts; fixture discovery + inert child kernel lifetime; no FanHost/HC/device/CPU benchmark"}}.dump()<<"\n";return failures.empty()?0:1;
}