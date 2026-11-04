#if defined(YMCC_MOUSE_INVENTORY_FRESHNESS_FIXTURE)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "fixtures/process_inventory_ports.h"
#include "mouse_backend_process.h"
// Optional original mouse-native selection. All OS facts use the existing owner ports.
static unsigned freshnessChecks=0,freshnessFailures=0;
static void expectFresh(bool value,const char* label){++freshnessChecks;if(!value)++freshnessFailures;std::cerr<<(value?"PASS ":"FAIL ")<<label<<'\n';}
static osfacts::Row unrelated(){osfacts::Row row{77};row.name=L"other.exe";row.path=L"Q:\\FixtureOnly\\other.exe";return row;}
static osfacts::Row joy(DWORD pid=101){osfacts::Row row{pid};row.name=L"JoyXoff.exe";row.path=L"Q:\\FixtureOnly\\JoyXoff.exe";return row;}
static void resetFresh(){expectFresh(osfacts::s.handles.empty(),"prior case returned all fake handles");expectFresh(osfacts::s.violations==0,"prior case used only valid owner OS ports");osfacts::s={};osfacts::s.rows={unrelated()};ymcc::processInventory::invalidate();}
static void warmTopology(){const auto receipt=ymcc::processInventory::read(&osfacts::query,ymcc::processInventory::currentTime,0);expectFresh(receipt->status==ymcc::processInventory::Status::Complete,"owner warm-up returns a complete topology");}
int main(){try{
 const std::wstring executable=joy().path;
 for(const ULONGLONG age:{0ull,249ull,250ull}){
  resetFresh();warmTopology();const auto before=osfacts::s.queryCalls;osfacts::s.tick+=age;osfacts::s.rows.push_back(joy());
  const auto result=ymcc::mouseProcess::observe(executable);
  expectFresh(result.ok&&result.known&&result.running,"external JoyXoff start is visible to fresh readback at 0/249/250ms");
  expectFresh(osfacts::s.queryCalls==before+1&&osfacts::s.snapshots==0,"readback requests one fresh receipt from the existing owner");
 }
 resetFresh();warmTopology();osfacts::s.tick+=249;osfacts::s.mode=osfacts::QueryMode::failed;
 auto result=ymcc::mouseProcess::observe(executable);expectFresh(!result.ok&&!result.known&&!result.running,"fresh census failure cannot reuse an older known OFF");
 resetFresh();warmTopology();osfacts::s.mode=osfacts::QueryMode::allocationFailure;
 result=ymcc::mouseProcess::observe(executable);expectFresh(!result.ok&&!result.known,"allocation failure remains unknown rather than cached OFF");
 resetFresh();osfacts::s.mode=osfacts::QueryMode::unsupported;warmTopology();osfacts::s.tick+=249;osfacts::s.rows.push_back(joy());const auto beforeFallback=osfacts::s.snapshots;
 result=ymcc::mouseProcess::observe(executable);expectFresh(result.ok&&result.known&&result.running&&osfacts::s.snapshots==beforeFallback+1,"old OS fallback also obtains its fresh topology from the sole owner");
 resetFresh();warmTopology();osfacts::s.rows.push_back(joy());osfacts::s.partial=true;osfacts::s.mode=osfacts::QueryMode::unsupported;
 result=ymcc::mouseProcess::observe(executable);expectFresh(!result.known&&!result.running,"partial fallback cannot authorize fresh absence or running");
 resetFresh();warmTopology();const auto offQueries=osfacts::s.queryCalls;
 result=ymcc::mouseProcess::observe(executable);expectFresh(result.ok&&result.known&&!result.running,"complete fresh no-candidate control remains known OFF");expectFresh(osfacts::s.queryCalls==offQueries+1,"known OFF control is freshly observed");
 resetFresh();warmTopology();auto foreign=joy();foreign.path=L"Q:\\Other\\JoyXoff.exe";osfacts::s.rows.push_back(foreign);
 result=ymcc::mouseProcess::observe(executable);expectFresh(result.ok&&result.known&&!result.running,"same basename from another path cannot become the selected backend");
 resetFresh();osfacts::s.rows.push_back(joy());warmTopology();osfacts::s.rows={unrelated(),joy(102)};
 result=ymcc::mouseProcess::observe(executable);expectFresh(result.ok&&result.known&&result.running,"fresh readback sees a replacement PID after exit and restart");
 resetFresh();warmTopology();const auto cacheQueries=osfacts::s.queryCalls;osfacts::s.rows.push_back(joy());std::vector<DWORD> pids;DWORD error=0;
 const auto status=ymcc::processNames::candidates(L"JoyXoff.exe",pids,error,&osfacts::query);
 expectFresh(status==ymcc::processNames::BasicResult::Complete&&pids.empty()&&osfacts::s.queryCalls==cacheQueries,"unmodified passive consumers retain their original 250ms cache contract");
 expectFresh(osfacts::s.handles.empty()&&osfacts::s.violations==0,"selection returns every owned fake resource");
 std::cout<<"{\"status\":\""<<(freshnessFailures?"FAIL":"PASS")<<"\",\"checks\":"<<freshnessChecks<<",\"failures\":"<<freshnessFailures<<",\"realProcessActions\":false,\"selection\":\"inventory-freshness-only\"}\n";return freshnessFailures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
#else
#define NOMINMAX
#include "mouse_backend_process.h"
#include "process_efficiency.h"
#include "mouse_mode_com_apartment.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
static int cases = 0;
static void need(bool value, const char* label) { if (!value) throw std::runtime_error(label); ++cases; }

// Preload SDK/STL/policy headers before intercepting only the foreground header's
// OS boundary. The backend tests below keep their real, owned-process APIs.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <map>
#include <type_traits>
#include "mouse_mode_policy.h"
#include "mouse_mode_foreground_rules.h"
namespace foregroundFake {
struct Calls {
    unsigned reads=0,foreground=0,window=0,visible=0,iconic=0,root=0,windowPid=0;
    unsigned selfPid=0,open=0,image=0,times=0,wait=0,handlePid=0,session=0,ownSession=0;
    unsigned windows=0,windowClass=0,close=0;
};
struct Process {
    DWORD pid=0;std::uint64_t birth=0;std::wstring image;bool alive=false;DWORD session=7;
};
struct Window {
    HWND handle=nullptr,root=nullptr;DWORD pid=0;std::wstring type;
    bool valid=true,visible=true,iconic=false;
};
static HWND hwnd(std::uintptr_t value){return reinterpret_cast<HWND>(value);}
enum class Race { None, Foreground, WindowPid, Death, FailedFinalPid };
struct State {
    Calls calls;std::array<Process,3> processes;std::array<Window,6> windows;
    std::map<HANDLE,Process*> handles;std::uintptr_t nextHandle=0x10000;
    HWND foreground=hwnd(10);DWORD self=900,ownSession=7;
    bool failOpen=false,failImage=false,emptyImage=false,failTimes=false,failWait=false;
    bool failSession=false,failOwnSession=false,failHandlePid=false,failWindowPid=false;
    bool failDirectory=false,failFinalPid=false;Race race=Race::None;
    unsigned violations=0;HWND lastClass=nullptr;
    State(){
        processes[0]={101,0x100000007ull,L"C:\\Windows\\Explorer.EXE",true,7};
        processes[1]={202,0x200000008ull,L"Q:\\FixtureOnly\\Editor.EXE",true,7};
        processes[2]={101,0x300000009ull,L"Q:\\FixtureOnly\\Replacement.EXE",false,7};
        windows[0]={hwnd(10),hwnd(20),101,L"Child"};
        windows[1]={hwnd(11),hwnd(21),101,L"OtherChild"};
        windows[2]={hwnd(12),hwnd(22),202,L"EditorChild"};
        windows[3]={hwnd(20),hwnd(20),101,L"Progman"};
        windows[4]={hwnd(21),hwnd(21),101,L"#32768"};
        windows[5]={hwnd(22),hwnd(22),202,L"EditorMain"};
    }
};
static State s;
static void count(unsigned& value){++value;++s.calls.reads;}
static Window* window(HWND h){for(auto& w:s.windows)if(w.handle==h)return &w;return nullptr;}
static Process* process(HANDLE handle){
    const auto found=s.handles.find(handle);
    if(found==s.handles.end()){++s.violations;return nullptr;}return found->second;
}
static HWND WINAPI getForegroundWindow(){count(s.calls.foreground);return s.foreground;}
static BOOL WINAPI isWindow(HWND h){count(s.calls.window);const auto w=window(h);return w&&w->valid;}
static BOOL WINAPI isWindowVisible(HWND h){count(s.calls.visible);const auto w=window(h);return w&&w->visible;}
static BOOL WINAPI isIconic(HWND h){count(s.calls.iconic);const auto w=window(h);return w&&w->iconic;}
static HWND WINAPI getAncestor(HWND h,UINT flags){
    count(s.calls.root);if(flags!=GA_ROOT)++s.violations;const auto w=window(h);return w?w->root:nullptr;
}
static DWORD WINAPI getWindowThreadProcessId(HWND h,LPDWORD pid){
    count(s.calls.windowPid);const auto w=window(h);if(pid)*pid=w?w->pid:0;
    // A failing API may leave a nonzero output behind: its return value matters.
    if(s.failWindowPid||s.failFinalPid){s.failFinalPid=false;return 0;}return w?42:0;
}
static DWORD WINAPI getCurrentProcessId(){count(s.calls.selfPid);return s.self;}
static HANDLE WINAPI openProcess(DWORD access,BOOL inherit,DWORD pid){
    count(s.calls.open);
    if(access!=(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)||inherit)++s.violations;
    if(!s.failOpen)for(auto& p:s.processes)if(p.pid==pid&&p.alive){
        const auto handle=reinterpret_cast<HANDLE>(s.nextHandle++);s.handles.emplace(handle,&p);
        if(s.handles.size()!=1)++s.violations;return handle;
    }
    SetLastError(ERROR_ACCESS_DENIED);return nullptr;
}
static BOOL WINAPI queryFullProcessImageNameW(HANDLE handle,DWORD flags,LPWSTR path,PDWORD size){
    count(s.calls.image);const auto p=process(handle);if(flags)++s.violations;
    if(!p||s.failImage){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const auto& image=p->image;
    if(!size||!path||*size<=image.size()){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    if(s.emptyImage){*size=0;path[0]=L'\0';return TRUE;}
    std::wmemcpy(path,image.c_str(),image.size()+1);*size=static_cast<DWORD>(image.size());return TRUE;
}
static BOOL WINAPI getProcessTimes(HANDLE handle,LPFILETIME birth,LPFILETIME end,LPFILETIME kernel,LPFILETIME user){
    count(s.calls.times);const auto p=process(handle);
    if(!p||s.failTimes){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    *birth={static_cast<DWORD>(p->birth),static_cast<DWORD>(p->birth>>32)};
    *end={};*kernel={};*user={};return TRUE;
}
static DWORD WINAPI waitForSingleObject(HANDLE handle,DWORD timeout){
    count(s.calls.wait);const auto p=process(handle);if(timeout)++s.violations;
    return !p||s.failWait?WAIT_FAILED:p->alive?WAIT_TIMEOUT:WAIT_OBJECT_0;
}
static DWORD WINAPI getProcessId(HANDLE handle){
    count(s.calls.handlePid);const auto p=process(handle);return !p||s.failHandlePid?0:p->pid;
}
static BOOL WINAPI processIdToSessionId(DWORD pid,DWORD* session){
    if(pid==s.self){count(s.calls.ownSession);if(s.failOwnSession)return FALSE;*session=s.ownSession;return TRUE;}
    count(s.calls.session);if(s.failSession)return FALSE;
    for(const auto& p:s.processes)if(p.pid==pid&&p.alive){*session=p.session;return TRUE;}return FALSE;
}
static BOOL WINAPI closeHandle(HANDLE handle){
    ++s.calls.close;if(s.handles.erase(handle)!=1){++s.violations;return FALSE;}return TRUE;
}
static UINT WINAPI getWindowsDirectoryW(LPWSTR buffer,UINT size){
    count(s.calls.windows);const std::wstring directory=L"C:\\Windows";
    if(s.failDirectory)return 0;
    if(size<=directory.size())return static_cast<UINT>(directory.size()+1);
    std::wmemcpy(buffer,directory.c_str(),directory.size()+1);return static_cast<UINT>(directory.size());
}
static int WINAPI getClassNameW(HWND h,LPWSTR buffer,int size){
    count(s.calls.windowClass);s.lastClass=h;const auto w=window(h);
    if(!w||size<=0)return 0;
    const auto length=std::min(w->type.size(),static_cast<std::size_t>(size-1));
    std::wmemcpy(buffer,w->type.c_str(),length);buffer[length]=L'\0';
    switch(s.race){
    case Race::Foreground:s.foreground=hwnd(12);break;
    case Race::WindowPid:s.windows[0].pid=202;break;
    case Race::Death:s.processes[0].alive=false;break;
    case Race::FailedFinalPid:s.failFinalPid=true;break;
    default:break;
    }
    s.race=Race::None;return static_cast<int>(length);
}
}
#define GetForegroundWindow foregroundFake::getForegroundWindow
#define IsWindow foregroundFake::isWindow
#define IsWindowVisible foregroundFake::isWindowVisible
#define IsIconic foregroundFake::isIconic
#define GetAncestor foregroundFake::getAncestor
#define GetWindowThreadProcessId foregroundFake::getWindowThreadProcessId
#define GetCurrentProcessId foregroundFake::getCurrentProcessId
#define OpenProcess foregroundFake::openProcess
#define QueryFullProcessImageNameW foregroundFake::queryFullProcessImageNameW
#define GetProcessTimes foregroundFake::getProcessTimes
#define WaitForSingleObject foregroundFake::waitForSingleObject
#define GetProcessId foregroundFake::getProcessId
#define ProcessIdToSessionId foregroundFake::processIdToSessionId
#define CloseHandle foregroundFake::closeHandle
#define GetWindowsDirectoryW foregroundFake::getWindowsDirectoryW
#define GetClassNameW foregroundFake::getClassNameW
#include "mouse_mode_foreground.h"
#undef GetForegroundWindow
#undef IsWindow
#undef IsWindowVisible
#undef IsIconic
#undef GetAncestor
#undef GetWindowThreadProcessId
#undef GetCurrentProcessId
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef GetProcessTimes
#undef WaitForSingleObject
#undef GetProcessId
#undef ProcessIdToSessionId
#undef CloseHandle
#undef GetWindowsDirectoryW
#undef GetClassNameW
static_assert(!std::is_copy_constructible_v<ymcc::mousePolicy::ForegroundObserver>);
static_assert(!std::is_copy_assignable_v<ymcc::mousePolicy::ForegroundObserver>);
static ymcc::mousePolicy::Json foregroundObserverReport;
static void foregroundObserverCases(){
    using ymcc::mousePolicy::Context;using ymcc::mousePolicy::ForegroundObserver;
    using ymcc::mousePolicy::Mode;using ymcc::mousePolicy::decide;
    using namespace foregroundFake;
    const auto resetWorld=[](){
        need(s.handles.empty()&&s.violations==0,"foreground-prior-case-returned-only-owned-fake-handles");s=State{};
    };
    const auto allowed=[](const Context& c){return c.identityKnown&&!c.foregroundImage.empty()&&decide(Mode::Auto,c).on;};
    const auto denied=[](const Context& c){return !c.identityKnown&&c.foregroundImage.empty()&&!decide(Mode::Auto,c).on;};
    resetWorld();
    {
        ForegroundObserver observer;
        for(unsigned i=0;i<120;++i){
            const auto before=s.calls;const auto c=observer.observe(false);
            need(allowed(c)&&c.desktop&&c.pid==101&&c.created==s.processes[0].birth&&
                 c.foregroundImage=="c:\\windows\\explorer.exe"&&c.foregroundClass=="Progman","foreground-stable-complete-image-and-64-bit-birth");
            need(s.calls.open==1&&s.calls.image==1&&s.calls.times==1&&s.handles.size()==1,"foreground-120-observations-reuse-one-process-identity");
            need(s.calls.foreground>=before.foreground+2&&s.calls.windowPid>=before.windowPid+2&&
                 s.calls.window>before.window&&s.calls.visible>before.visible&&s.calls.iconic>before.iconic,"foreground-each-observation-rechecks-window-and-final-hwnd-pid");
            need(s.calls.wait>=before.wait+2&&s.calls.handlePid>=before.handlePid+2&&
                 s.calls.session>before.session&&s.calls.ownSession>before.ownSession,"foreground-each-observation-rechecks-held-object-life-and-session");
            need(s.calls.root>before.root&&s.calls.windowClass>before.windowClass,"foreground-each-observation-rereads-root-and-class");
        }
        foregroundObserverReport={{"measurement","fake-winapi-call-counts"},{"cpuMeasured",false},{"realForegroundActions",false},
            {"stableObservations",120},{"openProcess",s.calls.open},{"queryFullProcessImageName",s.calls.image},
            {"getProcessTimes",s.calls.times},{"getForegroundWindow",s.calls.foreground},
            {"getWindowThreadProcessId",s.calls.windowPid},{"getProcessId",s.calls.handlePid},
            {"waitForSingleObject",s.calls.wait},{"targetSessionQueries",s.calls.session},{"ownSessionQueries",s.calls.ownSession},
            {"getAncestor",s.calls.root},{"getClassName",s.calls.windowClass}};
    }
    need(s.handles.empty()&&s.calls.close==1,"foreground-destructor-closes-the-single-lease");
    resetWorld();
    {
        ForegroundObserver observer;need(allowed(observer.observe(false)),"foreground-window-change-warmup");
        s.windows[3].type=L"Shell_TrayWnd";
        need(observer.observe(false).foregroundClass=="Shell_TrayWnd","foreground-same-hwnd-class-is-not-cached");
        s.windows[0].root=hwnd(21);
        need(observer.observe(false).foregroundClass=="#32768"&&s.lastClass==hwnd(21),"foreground-same-hwnd-root-is-reread");
        s.foreground=hwnd(11);const auto otherWindow=observer.observe(false);
        need(allowed(otherWindow)&&otherWindow.window==reinterpret_cast<std::uintptr_t>(hwnd(11))&&
             otherWindow.foregroundClass=="#32768"&&s.calls.open==1&&s.calls.image==1&&s.calls.times==1,"foreground-same-live-process-other-hwnd-reuses-identity");
        s.foreground=hwnd(12);const auto otherProcess=observer.observe(false);
        need(allowed(otherProcess)&&!otherProcess.desktop&&otherProcess.pid==202&&otherProcess.created==s.processes[1].birth&&
             otherProcess.foregroundImage=="q:\\fixtureonly\\editor.exe"&&s.calls.open==2&&s.calls.close==1,"foreground-new-pid-reacquires-and-reclassifies-complete-image");
    }
    resetWorld();
    {
        ForegroundObserver observer;const auto old=observer.observe(false);need(allowed(old),"foreground-pid-reuse-warmup");
        s.processes[0].alive=false;s.processes[2].alive=true;
        const auto replacement=observer.observe(false);
        need(allowed(replacement)&&!replacement.desktop&&replacement.pid==old.pid&&replacement.created==s.processes[2].birth&&
             replacement.created!=old.created&&replacement.foregroundImage=="q:\\fixtureonly\\replacement.exe"&&
             replacement.foregroundImage!=old.foregroundImage,"foreground-dead-object-same-pid-reuse-has-new-birth-and-image");
        need(s.calls.open==2&&s.calls.image==2&&s.calls.times==2&&s.calls.close==1&&s.handles.size()==1,"foreground-dead-lease-closed-before-same-pid-reacquire");
    }
    struct Fault {const char* label;void (*apply)();};
    const Fault faults[]={
        {"foreground-null-window-drops-old-allow",+[]{s.foreground=nullptr;}},
        {"foreground-invalid-window-drops-old-allow",+[]{s.windows[0].valid=false;}},
        {"foreground-hidden-window-drops-old-allow",+[]{s.windows[0].visible=false;}},
        {"foreground-minimized-window-drops-old-allow",+[]{s.windows[0].iconic=true;}},
        {"foreground-own-ymcc-drops-old-allow",+[]{s.windows[0].pid=s.self;}},
        {"foreground-unknown-window-pid-drops-old-allow",+[]{s.windows[0].pid=0;}},
        {"foreground-failed-window-pid-return-drops-old-allow",+[]{s.failWindowPid=true;}},
        {"foreground-exit-without-replacement-drops-old-allow",+[]{s.processes[0].alive=false;}},
        {"foreground-held-process-pid-change-drops-old-allow",+[]{s.processes[0].pid=303;}},
        {"foreground-held-process-pid-query-failure-drops-old-allow",+[]{s.failHandlePid=true;}},
        {"foreground-liveness-query-failure-drops-old-allow",+[]{s.failWait=true;}},
        {"foreground-target-session-unknown-drops-old-allow",+[]{s.failSession=true;}},
        {"foreground-own-session-unknown-drops-old-allow",+[]{s.failOwnSession=true;}},
        {"foreground-session-mismatch-drops-old-allow",+[]{++s.processes[0].session;}},
        {"foreground-final-hwnd-change-drops-old-allow",+[]{s.race=Race::Foreground;}},
        {"foreground-final-window-pid-change-drops-old-allow",+[]{s.race=Race::WindowPid;}},
        {"foreground-final-process-death-drops-old-allow",+[]{s.race=Race::Death;}},
        {"foreground-final-pid-api-failure-with-nonzero-pid-drops-old-allow",+[]{s.race=Race::FailedFinalPid;}},
        {"foreground-new-process-open-failure-drops-old-allow",+[]{s.foreground=hwnd(12);s.failOpen=true;}},
        {"foreground-new-process-image-query-failure-drops-old-allow",+[]{s.foreground=hwnd(12);s.failImage=true;}},
        {"foreground-new-process-empty-image-drops-old-allow",+[]{s.foreground=hwnd(12);s.emptyImage=true;}},
        {"foreground-new-process-birth-query-failure-drops-old-allow",+[]{s.foreground=hwnd(12);s.failTimes=true;}}
    };
    for(const auto& fault:faults){
        resetWorld();ForegroundObserver observer;need(allowed(observer.observe(false)),"foreground-failure-case-starts-from-confirmed-allow");
        fault.apply();const auto rejected=observer.observe(false);
        need(denied(rejected)&&s.handles.empty(),fault.label);
        need(s.violations==0,"foreground-failure-returns-only-owned-lease");
        const auto before=s.calls;s=State{};s.calls=before;
        need(allowed(observer.observe(false))&&s.calls.open==before.open+1&&s.calls.image==before.image+1&&s.calls.times==before.times+1,
             "foreground-recovery-after-failure-cannot-reuse-old-identity");
    }
    resetWorld();
    {
        ForegroundObserver observer;s.failDirectory=true;
        need(denied(observer.observe(false))&&s.handles.empty(),"foreground-unknown-windows-directory-cannot-authorize-allow");
    }
    resetWorld();
    {
        ForegroundObserver observer;need(allowed(observer.observe(false)),"foreground-reset-warmup");
        observer.reset();need(s.handles.empty()&&s.calls.close==1,"foreground-reset-releases-lease");
        observer.reset();need(s.calls.close==1,"foreground-reset-is-idempotent");
        need(allowed(observer.observe(false))&&s.calls.open==2&&s.calls.image==2&&s.calls.times==2,"foreground-reset-forces-fresh-identity");
    }
    unsigned manualReads=0,manualForeground=0,manualCloses=0;
    for(const auto mode:{Mode::Off,Mode::On}){
        resetWorld();ForegroundObserver observer;need(allowed(observer.observe(false)),"foreground-manual-transition-warmup");
        const auto before=s.calls;const auto manual=observer.observe(false,false);
        manualReads+=s.calls.reads-before.reads;manualForeground+=s.calls.foreground-before.foreground;manualCloses+=s.calls.close-before.close;
        need(manual==Context{}&&s.calls.reads==before.reads&&s.calls.close==before.close+1&&s.handles.empty(),
             mode==Mode::Off?"foreground-manual-off-no-window-or-process-io-and-releases-lease":"foreground-manual-on-no-window-or-process-io-and-releases-lease");
        need(decide(mode,manual).on==(mode==Mode::On),"foreground-manual-policy-keeps-requested-on-off-behavior");
        const auto stopped=s.calls;observer.observe(false,false);
        need(s.calls.reads==stopped.reads&&s.calls.close==stopped.close,"foreground-repeat-manual-has-zero-io");
        need(allowed(observer.observe(false))&&s.calls.open==before.open+1&&s.calls.image==before.image+1&&s.calls.times==before.times+1,
             "foreground-auto-after-manual-reacquires-identity");
    }
    foregroundObserverReport["manualObservations"]=4;foregroundObserverReport["manualReadCalls"]=manualReads;
    foregroundObserverReport["manualForegroundCalls"]=manualForeground;foregroundObserverReport["manualLeaseCloses"]=manualCloses;
    for(const bool inspect:{false,true}){
        resetWorld();ForegroundObserver observer;need(allowed(observer.observe(false)),"foreground-steam-transition-warmup");
        const auto before=s.calls;const auto steam=observer.observe(true,inspect);Context expected{};expected.steam=true;
        need(steam==expected&&s.calls.reads==before.reads&&s.calls.close==before.close+1&&s.handles.empty(),"foreground-steam-only-context-zero-read-io-releases-lease");
        observer.observe(true,inspect);need(s.calls.reads==before.reads&&s.calls.close==before.close+1,"foreground-repeat-steam-has-zero-io");
    }
    resetWorld();
    {
        need(allowed(ymcc::mousePolicy::observeForeground(false)),"foreground-free-function-default-signature");
        need(allowed(ymcc::mousePolicy::observeForeground(false,true))&&s.calls.open==1&&s.calls.image==1&&s.calls.times==1,
             "foreground-free-function-reuses-thread-local-observer");
        const auto before=s.calls;need(ymcc::mousePolicy::observeForeground(false,false)==Context{}&&s.calls.reads==before.reads&&
             s.calls.close==before.close+1&&s.handles.empty(),"foreground-free-function-manual-suppresses-all-read-io");
        need(allowed(ymcc::mousePolicy::observeForeground(false)),"foreground-free-function-reacquires-after-manual");
        const auto reacquired=s.calls;Context steam{};steam.steam=true;
        need(ymcc::mousePolicy::observeForeground(true)==steam&&s.calls.reads==reacquired.reads&&s.handles.empty(),
             "foreground-free-function-steam-releases-thread-local-lease");
    }
    resetWorld();
    std::exception_ptr workerError;
    std::thread worker([&]{try{
        need(allowed(ymcc::mousePolicy::observeForeground(false)),"foreground-worker-local-observer-acquires");
        need(allowed(ymcc::mousePolicy::observeForeground(false))&&s.calls.open==1,"foreground-worker-local-observer-reuses-lease");
    }catch(...){workerError=std::current_exception();}});
    worker.join();if(workerError)std::rethrow_exception(workerError);
    need(s.handles.empty()&&s.calls.close==1&&s.violations==0,"foreground-worker-exit-destroys-thread-local-lease");
}

using namespace ymcc::mouseProcess;
static int queryCalls = 0;
static unsigned sessionQueries = 0;
static BOOL WINAPI changedSession(DWORD pid, DWORD* session) {
    if (!::ProcessIdToSessionId(pid,session)) return FALSE;
    if (++sessionQueries == 2) ++*session;
    return TRUE;
}
static BOOL WINAPI deniedHeldSession(DWORD pid, DWORD* session) {
    if (++sessionQueries == 2) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ::ProcessIdToSessionId(pid,session);
}
static BOOL WINAPI deniedCandidateSession(DWORD, DWORD*) {
    SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}

static NTSTATUS NTAPI unsupportedBasic(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG) {
    ++queryCalls; return static_cast<NTSTATUS>(0xC0000003L);
}
static NTSTATUS NTAPI deniedBasic(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG) {
    ++queryCalls; return static_cast<NTSTATUS>(0xC0000022L);
}
static NTSTATUS NTAPI oversizedBasic(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG returned) {
    ++queryCalls; *returned=32*1024*1024; return static_cast<NTSTATUS>(0xC0000004L);
}
static NTSTATUS NTAPI shortBasic(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG returned) {
    ++queryCalls; *returned=1; return 0;
}
static NTSTATUS NTAPI resizedBasic(SYSTEM_INFORMATION_CLASS information, PVOID buffer, ULONG length, PULONG returned) {
    need(information==SystemBasicProcessInformation,"basic-information-class");
    if(++queryCalls==1){*returned=length+64;return static_cast<NTSTATUS>(0xC0000004L);}
    SYSTEM_BASICPROCESS_INFORMATION row{};std::memcpy(buffer,&row,sizeof(row));*returned=sizeof(row);return 0;
}
static void basicParserCases() {
    const std::wstring name=L"JoyXoff.exe";
    const auto extent=(sizeof(SYSTEM_BASICPROCESS_INFORMATION)+(name.size()+1)*sizeof(wchar_t)+7)&~std::size_t{7};
    std::vector<unsigned char> bytes(extent*2);
    auto fill=[&]{
        std::fill(bytes.begin(),bytes.end(),0);
        for(unsigned i=0;i<2;++i){
            SYSTEM_BASICPROCESS_INFORMATION row{};
            row.NextEntryOffset=i?0:static_cast<ULONG>(extent);
            row.UniqueProcessId=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(100+i));
            row.SequenceNumber=900+i;
            row.ImageName.Buffer=reinterpret_cast<wchar_t*>(bytes.data()+i*extent+sizeof(row));
            row.ImageName.Length=static_cast<USHORT>(name.size()*sizeof(wchar_t));
            row.ImageName.MaximumLength=row.ImageName.Length+sizeof(wchar_t);
            std::memcpy(row.ImageName.Buffer,name.c_str(),row.ImageName.MaximumLength);
            std::memcpy(bytes.data()+i*extent,&row,sizeof(row));
        }
    };
    auto alter=[&](auto modify){SYSTEM_BASICPROCESS_INFORMATION row{};std::memcpy(&row,bytes.data(),sizeof(row));modify(row);std::memcpy(bytes.data(),&row,sizeof(row));};
    std::vector<DWORD> found;
    fill();need(parseBasicCandidates(bytes.data(),bytes.size(),L"joyxoff.EXE",found)&&found==std::vector<DWORD>({100,101}),"basic-case-insensitive-all-matching-pids");
    need(parseBasicCandidates(bytes.data(),bytes.size(),L"other.exe",found)&&found.empty(),"basic-name-shortlist-excludes-foreign-names");
    fill();alter([](auto& row){row.NextEntryOffset=1;});found={999};need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found)&&found.empty(),"basic-rejects-short-unaligned-record");
    fill();alter([&](auto& row){row.NextEntryOffset=static_cast<ULONG>(bytes.size()+8);});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-out-of-range-next");
    fill();alter([](auto& row){row.ImageName.Length=1;});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-odd-name-length");
    fill();alter([](auto& row){row.ImageName.MaximumLength=0;});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-length-above-capacity");
    fill();alter([](auto& row){row.ImageName.Buffer=nullptr;});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-null-name-with-length");
    fill();alter([&](auto& row){row.ImageName.Buffer=reinterpret_cast<wchar_t*>(bytes.data());});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-name-overlapping-record");
    fill();alter([&](auto& row){row.ImageName.Buffer=reinterpret_cast<wchar_t*>(bytes.data()+bytes.size());});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-name-outside-record");
    fill();alter([](auto& row){row.UniqueProcessId=reinterpret_cast<HANDLE>(std::uintptr_t{1}<<32);});need(!parseBasicCandidates(bytes.data(),bytes.size(),name,found),"basic-rejects-oversized-pid");
    fill();found={999};need(!parseBasicCandidates(bytes.data(),extent+1,name,found)&&found.empty(),"basic-partial-list-never-proves-absence");
    need(!parseBasicCandidates(nullptr,0,name,found),"basic-null-buffer-rejected");
    DWORD error=0;queryCalls=0;
    need(basicCandidates(name,found,error,&unsupportedBasic)==BasicResult::Unavailable&&queryCalls==1,"basic-old-os-requests-original-fallback");
    queryCalls=0;need(basicCandidates(name,found,error,&deniedBasic)==BasicResult::Failed&&error==ERROR_ACCESS_DENIED&&queryCalls==1,"basic-query-failure-remains-unknown");
    queryCalls=0;need(basicCandidates(name,found,error,&oversizedBasic)==BasicResult::Failed&&error==ERROR_INSUFFICIENT_BUFFER&&queryCalls==1,"basic-buffer-growth-bounded");
    queryCalls=0;need(basicCandidates(name,found,error,&shortBasic)==BasicResult::Failed&&error==ERROR_INVALID_DATA,"basic-success-with-malformed-data-remains-unknown");
    queryCalls=0;need(basicCandidates(name,found,error,&resizedBasic)==BasicResult::Complete&&found.empty()&&queryCalls==2,"basic-length-race-retries-fresh-query");
}
struct Cost {double wallMs=0;unsigned long long cycles=0;};
static Cost inspectCost(const std::wstring& path,BasicQuery query) {
    LARGE_INTEGER frequency{},from{},to{};ULONG64 before=0,after=0;
    QueryPerformanceFrequency(&frequency);QueryThreadCycleTime(GetCurrentThread(),&before);QueryPerformanceCounter(&from);
    for(unsigned i=0;i<32;++i){const auto result=inspect(path,query);need(result.known&&result.processes.empty(),"benchmark-missing-name-remains-known-absent");}
    QueryPerformanceCounter(&to);QueryThreadCycleTime(GetCurrentThread(),&after);
    return {static_cast<double>(to.QuadPart-from.QuadPart)*1000.0/frequency.QuadPart,after-before};
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 3;
    const std::wstring owned=argv[1], foreign=argv[2], exits=argv[3];
    struct Cleanup {
        std::wstring a,b;
        ~Cleanup(){ymcc::mouseProcess::set(a,false);ymcc::mouseProcess::set(b,false);}
    } cleanup{owned,foreign};
    try {
        foregroundObserverCases();
        APTTYPE apartmentType; APTTYPEQUALIFIER qualifier;
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"fixture-starts-without-com");
        {ymcc::mouseMode::ComApartment first;need(first.ready()&&SUCCEEDED(first.status),"worker-com-initialized");
         {ymcc::mouseMode::ComApartment second;need(second.ready()&&second.status==S_FALSE,"existing-mta-reference-balanced");}
         need(SUCCEEDED(CoGetApartmentType(&apartmentType,&qualifier))&&apartmentType==APTTYPE_MTA,"outer-mta-survives-inner-guard");}
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"worker-com-reference-released");
        need(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"fixture-sta-created");
        {ymcc::mouseMode::ComApartment existing;need(existing.ready()&&existing.status==RPC_E_CHANGED_MODE,"existing-sta-is-usable");}
        need(SUCCEEDED(CoGetApartmentType(&apartmentType,&qualifier))&&(apartmentType==APTTYPE_STA||apartmentType==APTTYPE_MAINSTA),"existing-sta-not-uninitialized");
        CoUninitialize();
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"fixture-sta-reference-released");
        basicParserCases();
        using namespace ymcc::mouseProcess;
        need(observe(owned).known && !observe(owned).running,"initially-stopped");
        auto value=set(owned,true,&ymcc::processEfficiency::createProcess); need(value.ok && value.running && value.known,"direct-start-live");
        auto first=inspect(owned);need(first.processes.size()==1,"single-instance-started");
        const DWORD pid=GetProcessId(first.processes[0].value);
        DWORD session=0;need(ProcessIdToSessionId(GetCurrentProcessId(),&session)!=FALSE,"fixture-session-known");
        Snapshot movedSession;sessionQueries=0;inspectCandidate(movedSession,pid,session,owned,&changedSession);
        need(movedSession.known&&movedSession.processes.empty()&&sessionQueries==2,"held-instance-session-reuse-is-not-admitted");
        Snapshot deniedSession;sessionQueries=0;inspectCandidate(deniedSession,pid,session,owned,&deniedHeldSession);
        need(!deniedSession.known&&deniedSession.processes.empty()&&deniedSession.error==ERROR_ACCESS_DENIED,"held-session-query-failure-remains-unknown");
        Snapshot deniedHint;inspectCandidate(deniedHint,pid,session,owned,&deniedCandidateSession);
        need(!deniedHint.known&&deniedHint.processes.empty()&&deniedHint.error==ERROR_ACCESS_DENIED,"candidate-session-query-failure-is-not-absence");
        auto legacy=inspect(owned,nullptr);need(legacy.known&&legacy.processes.size()==1&&GetProcessId(legacy.processes[0].value)==pid,"legacy-fallback-same-live-instance");
        auto unsupported=inspect(owned,&unsupportedBasic);need(unsupported.known&&unsupported.processes.size()==1&&GetProcessId(unsupported.processes[0].value)==pid,"old-os-fallback-preserves-live-path-check");
        const auto denied=inspect(owned,&deniedBasic);need(!denied.known&&denied.error==ERROR_ACCESS_DENIED,"query-error-is-not-process-absent");
        value=set(owned,true); auto again=inspect(owned);
        need(value.ok && again.processes.size()==1 && GetProcessId(again.processes[0].value)==pid,"explicit-on-is-idempotent");
        need(set(foreign,true).ok,"foreign-same-basename-started");
        need(inspect(owned).processes.size()==1 && inspect(foreign).processes.size()==1,"exact-path-isolation");
        value=set(owned,false);need(value.ok && !value.running,"confirmed-stop");
        need(observe(foreign).running,"foreign-same-name-survives-stop");
        need(set(owned,false).ok,"explicit-off-is-idempotent");
        value=set(exits,true);need(!value.ok && !value.running,"immediate-exit-is-not-success");
        value=set(owned+L".missing",true);need(!value.ok && !value.running,"missing-executable-is-not-success");
        need(set(owned,true).ok,"restart-after-stop");
        need(set(owned,false).ok && set(foreign,false).ok,"isolated-children-cleaned");
        std::vector<DWORD> pids;DWORD error=0;
        const auto supported=basicCandidates(L"YMCC-No-Such-Backend-1e283d60.exe",pids,error);
        const std::wstring missing=owned+L".cpu-query-missing";
        const auto legacyCost=inspectCost(missing,nullptr),basicCost=inspectCost(missing,resolveBasicQuery());
        std::cout << "{\"status\":\"PASS\",\"cases\":" << cases << ",\"boundary\":\"isolated-owned-win32-processes\","
            << "\"basicAvailable\":" << (supported==BasicResult::Complete?"true":"false") << ",\"queriesPerCost\":32,"
            << "\"legacyWallMs\":" << legacyCost.wallMs << ",\"basicWallMs\":" << basicCost.wallMs
            << ",\"legacyThreadCycles\":" << legacyCost.cycles << ",\"basicThreadCycles\":" << basicCost.cycles
            << ",\"foregroundObserver\":" << foregroundObserverReport.dump() << "}";
        return 0;
    } catch(const std::exception& error) {
        std::cout << "{\"status\":\"FAIL\",\"error\":\"" << error.what() << "\"}";
        return 1;
    }
}

#endif // YMCC_MOUSE_INVENTORY_FRESHNESS_FIXTURE
