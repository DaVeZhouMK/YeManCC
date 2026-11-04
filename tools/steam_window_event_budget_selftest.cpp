// Production session observer + message pump with fixed in-memory Win32 edges.
// No real Steam, windows, process enumeration or configuration is accessed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdlib>
#include <new>
#include <set>
#include <map>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.hpp>
// Only the explicitly selected exception branch arms this thread-local fault.
static thread_local int allocationCountdown = -1;
void* operator new(std::size_t bytes) {
 if(allocationCountdown>=0 && allocationCountdown--==0){allocationCountdown=-1;throw std::bad_alloc();}
 if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static std::atomic<int> failAfterImage{-1};
static std::atomic<bool> faultUsed{false},snapshotEnabled{false};
static std::atomic<unsigned> callbackEscapes{0};
static std::set<HANDLE> observerEvents,snapshotHandles;
static std::set<HKEY> observerKeys;
static constexpr UINT hintMessage = WM_APP + 80;
static constexpr DWORD fixturePid = 4242;
static std::atomic<DWORD> observerThread{0};
static WINEVENTPROC eventCallback = nullptr;
static std::atomic<unsigned> opens{0},imageReads{0},dispatches{0},notifications{0};
static std::mutex fixtureMutex;
static std::map<HANDLE,DWORD> processHandles;
static std::map<DWORD,HANDLE> lifetimes;
static std::map<DWORD,std::wstring> images;
static bool deny = false;
static HWINEVENTHOOK fixtureSetWinEventHook(DWORD,DWORD,HMODULE,WINEVENTPROC callback,DWORD,DWORD,DWORD) {
 eventCallback=callback;observerThread=GetCurrentThreadId();return reinterpret_cast<HWINEVENTHOOK>(1);
}
static BOOL unhook(HWINEVENTHOOK){return TRUE;}
static HWND ancestor(HWND window,UINT){return window;}
static DWORD windowPid(HWND hwnd,LPDWORD pid){*pid=static_cast<DWORD>(reinterpret_cast<UINT_PTR>(hwnd));return 1;}
static HANDLE fixtureEvent(LPSECURITY_ATTRIBUTES attributes,BOOL manual,BOOL initial,LPCWSTR name){
 HANDLE h=::CreateEventW(attributes,manual,initial,name);if(h){std::lock_guard lock(fixtureMutex);observerEvents.insert(h);}return h;
}
static LSTATUS fixtureRegOpen(HKEY parent,LPCWSTR subkey,DWORD options,REGSAM access,PHKEY result){
 const auto code=::RegOpenKeyExW(parent,subkey,options,access,result);if(code==ERROR_SUCCESS){std::lock_guard lock(fixtureMutex);observerKeys.insert(*result);}return code;
}
static LSTATUS fixtureRegClose(HKEY key){{std::lock_guard lock(fixtureMutex);observerKeys.erase(key);}return ::RegCloseKey(key);}
static HANDLE snapshot(DWORD,DWORD){
 if(!snapshotEnabled)return INVALID_HANDLE_VALUE;
 const auto h=::CreateEventW(nullptr,TRUE,FALSE,nullptr);if(h){std::lock_guard lock(fixtureMutex);snapshotHandles.insert(h);}return h?h:INVALID_HANDLE_VALUE;
}
static BOOL firstProcess(HANDLE h,LPPROCESSENTRY32W pe){
 std::lock_guard lock(fixtureMutex);if(!snapshotHandles.count(h)){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
 pe->th32ProcessID=fixturePid;wcscpy_s(pe->szExeFile,L"steam.exe");return TRUE;
}
static BOOL nextProcess(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
static BOOL processTimes(HANDLE h,LPFILETIME birth,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user){
 {std::lock_guard lock(fixtureMutex);if(processHandles.count(h)){*birth={123,1};*exit={};*kernel={};*user={};return TRUE;}}
 return ::GetProcessTimes(h,birth,exit,kernel,user);
}
static HANDLE openProcess(DWORD access,BOOL,DWORD pid) {
 ++opens;std::lock_guard lock(fixtureMutex);if(deny||!lifetimes.count(pid))return nullptr;
 HANDLE result=nullptr;if(!DuplicateHandle(GetCurrentProcess(),lifetimes.at(pid),GetCurrentProcess(),&result,SYNCHRONIZE,FALSE,0))return nullptr;
 processHandles[result]=pid;return result;
}
static BOOL imageName(HANDLE h,DWORD,LPWSTR path,PDWORD length) {
 ++imageReads;std::lock_guard lock(fixtureMutex);const auto it=processHandles.find(h);if(it==processHandles.end())return FALSE;
 const auto& image=images.at(it->second);if(*length<=image.size())return FALSE;wcscpy_s(path,*length,image.c_str());*length=static_cast<DWORD>(image.size());const int fault=failAfterImage.exchange(-1);if(fault>=0){allocationCountdown=fault;faultUsed=true;}return TRUE;
}
static BOOL closeHandle(HANDLE h) { {std::lock_guard lock(fixtureMutex);processHandles.erase(h);observerEvents.erase(h);snapshotHandles.erase(h);}return ::CloseHandle(h); }
static LRESULT dispatch(const MSG* message) {
 if(message->message==hintMessage){
   try{eventCallback(nullptr,static_cast<DWORD>(message->wParam),reinterpret_cast<HWND>(message->lParam),OBJID_WINDOW,CHILDID_SELF,0,0);}catch(...){++callbackEscapes;}
   ++dispatches;return 0;
 }
 return ::DispatchMessageW(message);
}
#define CreateEventW fixtureEvent
#define RegOpenKeyExW fixtureRegOpen
#define RegCloseKey fixtureRegClose
#define Process32FirstW firstProcess
#define Process32NextW nextProcess
#define GetProcessTimes processTimes
#define SetWinEventHook fixtureSetWinEventHook
#define UnhookWinEvent unhook
#define GetAncestor ancestor
#define GetWindowThreadProcessId windowPid
#define CreateToolhelp32Snapshot snapshot
#define OpenProcess openProcess
#define QueryFullProcessImageNameW imageName
#define CloseHandle closeHandle
#define DispatchMessageW dispatch
#include "steam_session_observer.h"
#undef CreateEventW
#undef RegOpenKeyExW
#undef RegCloseKey
#undef Process32FirstW
#undef Process32NextW
#undef GetProcessTimes
#undef SetWinEventHook
#undef UnhookWinEvent
#undef GetAncestor
#undef GetWindowThreadProcessId
#undef CreateToolhelp32Snapshot
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef CloseHandle
#undef DispatchMessageW
using namespace ymcc::steamsession;
static unsigned checks=0;
static void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;std::cout<<"PASS "<<label<<'\n';}
template<class F> static void until(F f,DWORD timeout=3000){const auto end=GetTickCount64()+timeout;while(!f()){if(GetTickCount64()>=end)throw std::runtime_error("fixture deadline");Sleep(2);}}
static void send(unsigned count,DWORD pid=fixturePid,DWORD event=EVENT_OBJECT_CREATE) {
 for(unsigned i=0;i<count;++i)if(!PostThreadMessageW(observerThread,hintMessage,event,pid))throw std::runtime_error("post hint failed");
}
static void lifetime(DWORD pid,const std::wstring& image) {
 std::lock_guard lock(fixtureMutex);if(lifetimes.count(pid)){SetEvent(lifetimes[pid]);CloseHandle(lifetimes[pid]);}
 lifetimes[pid]=CreateEventW(nullptr,TRUE,FALSE,nullptr);images[pid]=image;
}
static ULONGLONG cpu() {FILETIME b{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return (ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
struct Registry {std::wstring path=L"Software\\YMCCWindowBudgetFixture-"+std::to_wstring(GetCurrentProcessId());~Registry(){RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());}};

// Reuse the production observer, callback, classifier and existing fake ports.
// This optional selection performs no cost/storm benchmark or real application IO.
struct InjectedFault {};
static int exceptionCases(const std::filesystem::path& out){
 unsigned failures=0;
 const auto expect=[&](bool value,const char* label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';};
 const auto empty=[&]{std::lock_guard lock(fixtureMutex);return processHandles.empty()&&observerEvents.empty()&&snapshotHandles.empty()&&observerKeys.empty();};
 const auto reclaim=[&]{
  std::lock_guard lock(fixtureMutex);
  // Preserve failure verdict before disposing only this fixture's leaked resources.
  for(const auto& [h,pid]:processHandles)::CloseHandle(h);processHandles.clear();
  for(const auto h:observerEvents)::CloseHandle(h);observerEvents.clear();
  for(const auto h:snapshotHandles)::CloseHandle(h);snapshotHandles.clear();
  for(const auto key:observerKeys)::RegCloseKey(key);observerKeys.clear();
  for(const auto& [pid,h]:lifetimes)::CloseHandle(h);lifetimes.clear();images.clear();
  faultUsed=false;failAfterImage=-1;snapshotEnabled=false;allocationCountdown=-1;
 };
 const std::wstring root=L"x:\\inert-window-budget",steam=root+L"\\steam.exe",helper=root+L"\\bin\\cef\\cef.win64\\steamwebhelper.exe";
 for(int allocation:{0,1}){
  lifetime(fixturePid,steam);ULONGLONG birth=999;bool caught=false;failAfterImage=allocation;
  try{HANDLE h=verifiedProcess(fixturePid,root,&birth);if(h)closeHandle(h);}catch(const std::bad_alloc&){caught=true;}
  expect(caught,"verifiedProcess exercises the selected string allocation failure");
  expect(empty(),"verifiedProcess returns its held handle after allocation failure");
  expect(birth==999,"failed identity verification does not publish process birth");reclaim();
 }
 lifetime(fixturePid,helper);
 {WindowProcessCache cache;bool caught=false;
  try{cache.classify(fixturePid,100,[](const std::wstring&)->unsigned{throw InjectedFault{};});}catch(const InjectedFault&){caught=true;}
  expect(caught,"classifier fault reaches its C++ caller");expect(empty(),"failed classification immediately releases its pinned handle");
  unsigned classified=0;const auto kind=cache.classify(fixturePid,101,[&](const std::wstring&){++classified;return 2u;});
  expect(kind==2&&classified==1,"the next event retries the same live PID after classifier failure");cache.clear();expect(empty(),"classification clear releases the recovered entry");}
 reclaim();
 // Positive identity and rejected-path cases retain the existing ownership contract.
 lifetime(fixturePid,steam);ULONGLONG birth=0;HANDLE verified=verifiedProcess(fixturePid,root,&birth);
 expect(verified!=nullptr&&birth!=0,"successful verifiedProcess returns the live handle and birth");
 expect(processHandles.size()==1,"successful verification transfers handle ownership to its caller");if(verified)closeHandle(verified);
 lifetime(fixturePid,L"x:\\other\\steam.exe");verified=verifiedProcess(fixturePid,root,&birth);expect(!verified&&empty(),"same basename from another path is rejected without leaked handles");reclaim();
 Registry reg;HKEY key=nullptr;check(RegCreateKeyExW(HKEY_CURRENT_USER,reg.path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"isolated exception registry key");
 RegSetValueExW(key,L"SteamPath",0,REG_SZ,reinterpret_cast<const BYTE*>(root.c_str()),static_cast<DWORD>((root.size()+1)*sizeof(wchar_t)));RegCloseKey(key);
 lifetime(fixturePid,helper);std::atomic<bool> ready=false;notifications=0;callbackEscapes=0;
 {Observer observer(reg.path);check(observer.start([&](bool){ready=true;},[]{return false;},[]{++notifications;}),"exception callback observer starts");until([&]{return ready.load()&&observerThread.load();});
  const auto dispatched=dispatches.load();failAfterImage=0;send(1);until([&]{return dispatches>dispatched;});
  expect(faultUsed,"actual callback exercises the injected image-string allocation failure");expect(callbackEscapes==0,"C++ exceptions cannot leave the WinEvent callback boundary");
  {std::lock_guard lock(fixtureMutex);expect(processHandles.empty(),"failed callback classification retains no process handle");}
  const auto retryDispatch=dispatches.load();send(1);until([&]{return dispatches>retryDispatch;});Sleep(230);
  expect(notifications==1,"later window event recovers after the callback fault");observer.stop();expect(empty(),"normal stop returns observer subscription and event resources");}
 reclaim();
 // Exercise the real run() unwind after it owns a process, registry key and notify event.
 HKEY active=nullptr;check(RegCreateKeyExW(HKEY_CURRENT_USER,(reg.path+L"\\ActiveProcess").c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&active,nullptr)==ERROR_SUCCESS,"isolated active-process key");
 DWORD pid=fixturePid;RegSetValueExW(active,L"pid",0,REG_DWORD,reinterpret_cast<const BYTE*>(&pid),sizeof(pid));RegCloseKey(active);
 lifetime(fixturePid,steam);ready=false;std::atomic<bool> retryThrew=false;
 {Observer observer(reg.path);check(observer.start([&](bool){ready=true;},[&]()->bool{retryThrew=true;throw InjectedFault{};}),"exception unwind observer starts");
  until([&]{return ready.load()&&observer.steamPresence()==1;});until([&]{return retryThrew.load();});observer.stop();
  expect(empty(),"run exception releases live process, registry subscription and notify event");expect(observer.steamPresence()==-1,"observer failure retains UNKNOWN presence");}
 reclaim();
 // The fallback snapshot is local to inspect() and must also unwind.
 RegOpenKeyExW(HKEY_CURRENT_USER,(reg.path+L"\\ActiveProcess").c_str(),0,KEY_SET_VALUE,&active);pid=0;RegSetValueExW(active,L"pid",0,REG_DWORD,reinterpret_cast<const BYTE*>(&pid),sizeof(pid));RegCloseKey(active);
 lifetime(fixturePid,steam);snapshotEnabled=true;failAfterImage=0;
 {Observer observer(reg.path);check(observer.start([](bool){},[]{return false;}),"fallback unwind observer starts");until([&]{return faultUsed.load();});observer.stop();
  expect(empty(),"discovery allocation fault returns snapshot, candidate and observer resources");expect(observer.steamPresence()==-1,"failed discovery cannot publish stale success or absence");}
 reclaim();
 nlohmann::json report{{"ok",failures==0},{"checks",checks},{"failures",failures},{"selection","exceptions-only"},{"realSteamOperations",0},{"realCssOperations",0},{"realProcessEnumeration",false},{"ownedResourceReclaimAfterVerdict",true}};
 std::ofstream f(out/L"window-exceptions.json");f<<report.dump(2);std::cout<<report.dump(2)<<'\n';return failures?1:0;
}

int wmain(int argc,wchar_t** argv){try{
 if(argc==3&&std::wstring_view(argv[2])==L"--exceptions-only"){const std::filesystem::path selectedOut(argv[1]);std::filesystem::create_directories(selectedOut);return exceptionCases(selectedOut);}
 check(argc==2,"isolated output supplied");const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
#ifndef YMCC_BASELINE_METRICS
 WindowHintWindow hints;check(hints.timeout(1000)==INFINITE,"no event has no deadline");hints.request(1000);
 for(unsigned i=0;i<50000;++i)hints.request(1199);
 check(!hints.take(1199)&&hints.take(1200),"50000 synthetic hints retain one 200ms deadline");
 check(hints.timeout(1200)==INFINITE&&!hints.take(99999),"delivery ends the finite deadline");
 hints.request(2000);hints.clear();check(hints.timeout(99999)==INFINITE,"cancellation removes the deadline");
#endif
 Registry reg;HKEY key=nullptr;check(RegCreateKeyExW(HKEY_CURRENT_USER,reg.path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"isolated registry key");
 const std::wstring root=L"X:\\inert-window-budget";RegSetValueExW(key,L"SteamPath",0,REG_SZ,reinterpret_cast<const BYTE*>(root.c_str()),static_cast<DWORD>((root.size()+1)*sizeof(wchar_t)));RegCloseKey(key);
 const auto helper=root+L"\\bin\\cef\\cef.win64\\steamwebhelper.exe";lifetime(fixturePid,helper);
 Observer observer(reg.path);std::atomic<bool> ready=false;check(observer.start([&](bool){ready=true;},[]{return false;},[]{++notifications;}),"production observer starts");
 until([&]{return ready.load()&&observerThread.load();});Sleep(20);
 const auto c0=cpu();send(2000);until([&]{return dispatches==2000;});Sleep(250);
 const unsigned createOpens=opens.load(),createImages=imageReads.load(),createNotifies=notifications.load();const auto createCpu=cpu()-c0;
#ifndef YMCC_BASELINE_METRICS
 check(createOpens==1&&createImages==1,"2000 CREATE hints query one pinned process image");
 check(createNotifies==1,"2000 CREATE hints fan out to WebView/mode/game observers only once");
#endif
 const auto beforeQuiet=cpu();const auto beforeNotifications=notifications.load();Sleep(700);
 const auto idleCpu=cpu()-beforeQuiet;check(notifications==beforeNotifications,"quiet worker adds no UI notification");
 // Every reused PID must be reclassified, including inside the former 30s TTL.
 lifetime(fixturePid,L"X:\\other.exe");auto n=dispatches.load();send(1,fixturePid,EVENT_OBJECT_NAMECHANGE);until([&]{return dispatches>n;});Sleep(250);
#ifndef YMCC_BASELINE_METRICS
 check(notifications==beforeNotifications,"old helper PID reused by unrelated process produces no UI hint");
#endif
 lifetime(fixturePid,helper);n=dispatches.load();send(1,fixturePid,EVENT_OBJECT_CREATE);until([&]{return dispatches>n;});Sleep(250);
#ifndef YMCC_BASELINE_METRICS
 check(notifications==beforeNotifications+1,"new helper lifetime remains observable");
#endif
 // Protected/nonexistent processes are a bounded negative result, not repeated OpenProcess.
 {std::lock_guard lock(fixtureMutex);deny=true;}const auto beforeOpen=opens.load();n=dispatches.load();send(2000,9090);until([&]{return dispatches==n+2000;});
 const auto deniedOpens=opens.load()-beforeOpen;
#ifndef YMCC_BASELINE_METRICS
 check(deniedOpens==1,"2000 denied CREATE hints perform one process-open attempt");
#endif
 {std::lock_guard lock(fixtureMutex);deny=false;}
 // A later event retries negative entries without any resident retry timer.
 lifetime(9090,helper);Sleep(1100);n=dispatches.load();send(1,9090);until([&]{return dispatches>n;});Sleep(250);
#ifndef YMCC_BASELINE_METRICS
 check(notifications==beforeNotifications+2,"later event recovers a previously inaccessible process");
#endif
 // Capacity stays bounded and one eviction does not flush all cached images.
 for(DWORD pid=10000;pid<10300;++pid)lifetime(pid,L"X:\\other.exe");n=dispatches.load();
 for(DWORD pid=10000;pid<10300;++pid)send(1,pid);until([&]{return dispatches==n+300;});
 size_t retained=0;{std::lock_guard lock(fixtureMutex);retained=processHandles.size();}
#ifndef YMCC_BASELINE_METRICS
 check(retained<=WindowProcessCache::capacity,"global process-image cache stays within 256 handles");
 const auto capacityReads=imageReads.load();n=dispatches.load();send(1,10299);until([&]{return dispatches>n;});
 check(imageReads==capacityReads,"capacity eviction keeps recently classified processes cached");
#endif
 // Never drain an unbounded message queue before noticing stop.
 std::atomic<bool> flooding{true};std::thread flood([&]{const auto end=GetTickCount64()+500;while(flooding&&GetTickCount64()<end){PostThreadMessageW(observerThread,hintMessage,EVENT_OBJECT_NAMECHANGE,fixturePid);SwitchToThread();}});
 Sleep(20);const auto stopAt=GetTickCount64();observer.stop();const auto stopMs=GetTickCount64()-stopAt;flooding=false;flood.join();
#ifndef YMCC_BASELINE_METRICS
 check(stopMs<1500,"observer stops during a continuous notification stream");
#endif

#ifndef YMCC_BASELINE_METRICS
 Observer fair(reg.path);std::atomic<unsigned> retryCalls{0};ready=false;
 check(fair.start([&](bool changed){if(changed)ready=true;else ++retryCalls;},[]{return true;},[]{++notifications;}),"fairness fixture starts");
 until([&]{return ready.load();});std::atomic<bool> generating{true};
 std::thread busy([&]{const auto end=GetTickCount64()+1400;while(generating&&GetTickCount64()<end){PostThreadMessageW(observerThread,hintMessage,EVENT_OBJECT_NAMECHANGE,fixturePid);SwitchToThread();}});
 const bool retryDelivered=[&]{const auto end=GetTickCount64()+1300;while(!retryCalls&&GetTickCount64()<end)Sleep(2);return retryCalls==1;}();
 generating=false;busy.join();fair.stop();check(retryDelivered,"bounded startup retry is not starved by continuous Windows messages");
#endif
 {std::lock_guard lock(fixtureMutex);check(processHandles.empty(),"stop releases all cached process handles");for(const auto& [pid,h]:lifetimes)CloseHandle(h);lifetimes.clear();}
 nlohmann::json report{{"ok",true},{"checks",checks},{"syntheticCreateHints",2000},{"processOpens",createOpens},{"imageQueries",createImages},{"fanoutNotifications",createNotifies},{"syntheticDeniedHints",2000},{"deniedProcessOpens",deniedOpens},{"hintCpuMs",createCpu/10000.0},{"idleSampleMs",700},{"idleCpuMs",idleCpu/10000.0},{"stopDuringStormMs",stopMs},{"retainedHandlePeakSample",retained},{"realSteamOperations",0},{"realCssOperations",0}};
 std::ofstream f(out/L"window-budget.json");f<<report.dump(2);std::cout<<report.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
