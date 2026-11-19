
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>
// Private fixture API-name adapter: actual CreateMutexW still performs the
// existence decision in one owned namespace. No production-name contention.
static std::wstring testScope;
static bool denyCreate=false;
static HANDLE coldRootEntered=nullptr,coldRootRelease=nullptr;
static const DWORD sourceCallerThread=GetCurrentThreadId();
static unsigned callerRootReads=0;
static LSTATUS WINAPI fixtureRootRead(HKEY key,LPCWSTR sub,LPCWSTR value,DWORD flags,LPDWORD type,PVOID data,LPDWORD size){
 if(coldRootEntered&&key==HKEY_CURRENT_USER&&value&&std::wstring_view(value)==L"SteamPath"){
  if(GetCurrentThreadId()==sourceCallerThread)++callerRootReads;
  else{SetEvent(coldRootEntered);WaitForSingleObject(coldRootRelease,5000);}
 }
 return RegGetValueW(key,sub,value,flags,type,data,size);
}
static HANDLE WINAPI fixtureSourceMutex(LPSECURITY_ATTRIBUTES sa,BOOL owner,LPCWSTR name){
 if(!name||std::wstring_view(name).find(L"Local\\YeManCC.SteamSource.v1.")!=0||testScope.find(L"Local\\YMCCFixture.SourceOwner.")!=0){SetLastError(ERROR_INVALID_NAME);return nullptr;}
 if(denyCreate){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
 return CreateMutexW(sa,owner,testScope.c_str());
}
static HANDLE capturedRunEvents[2]{};
static unsigned capturedEventCount=0;
static bool captureRunEvents=false;
static bool failSecondRunEvent=false,protectFirstRunEvent=false;
static const DWORD captureThread=GetCurrentThreadId();
static HANDLE WINAPI fixtureRunEvent(LPSECURITY_ATTRIBUTES sa,BOOL manual,BOOL initial,LPCWSTR name){
 if(GetCurrentThreadId()==captureThread&&captureRunEvents&&capturedEventCount==1&&failSecondRunEvent){failSecondRunEvent=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
 const auto result=CreateEventW(sa,manual,initial,name);
 if(GetCurrentThreadId()==captureThread&&captureRunEvents&&!capturedEventCount&&protectFirstRunEvent&&result)SetHandleInformation(result,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE);
 if(GetCurrentThreadId()==captureThread&&captureRunEvents&&capturedEventCount<2)capturedRunEvents[capturedEventCount++]=result;
 return result;
}
#define CreateEventW fixtureRunEvent
#define CreateMutexW fixtureSourceMutex
#define RegGetValueW fixtureRootRead
#include "steam_session_observer.h"
#undef RegGetValueW
#undef CreateMutexW
#undef CreateEventW
#ifndef YMCC_SOURCE_OWNER_RETIREMENT_ONLY
#include "steam_information_producer.h"
#endif
#include "json.hpp"
using Json=nlohmann::json;
using namespace ymcc::steamsession;
static std::string selectionText(const std::wstring& mode){std::string text;for(const auto ch:mode){if(ch>127)throw std::runtime_error("non-ASCII selector");text.push_back(static_cast<char>(ch));}return text;}
static Json checks=Json::array();
static void require(bool b,const char* m){checks.push_back({{"name",m},{"pass",b}});if(!b)throw std::runtime_error(m);}
template<class F>static bool until(F f,DWORD ms=3000){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(5);}return true;}
template<class O>static bool owns(const O& o){if constexpr(requires{o.ownsSource();})return o.ownsSource();else return true;}
template<class O>static bool running(const O& o){if constexpr(requires{o.sourceActive();})return o.sourceActive();else return true;}
static std::string birth(HANDLE p){FILETIME a{},b{},c{},d{};if(!GetProcessTimes(p,&a,&b,&c,&d))throw std::runtime_error("process birth");return std::to_string((ULONGLONG(a.dwHighDateTime)<<32)|a.dwLowDateTime);}
struct Registry {
    std::wstring path;
    explicit Registry(std::wstring p) : path(std::move(p)) {}
    ~Registry() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
    void number(const wchar_t* name, DWORD value, bool active = true) {
        HKEY key = nullptr; const auto p = path + (active ? L"\\ActiveProcess" : L"");
        if (RegCreateKeyExW(HKEY_CURRENT_USER, p.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) throw std::runtime_error("test registry create");
        const auto r = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key); if (r) throw std::runtime_error("test registry value");
    }
    void root(const std::wstring& value) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) throw std::runtime_error("test registry root");
        const auto r = RegSetValueExW(key, L"SteamPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
        RegCloseKey(key); if (r) throw std::runtime_error("test root value");
    }
};


static Json child(const std::wstring& registry,const std::filesystem::path& out,bool expected){
 wchar_t image[32768]{};require(GetModuleFileNameW(nullptr,image,32768)>0,"own child executable");
 std::wstring command=L"\""+std::wstring(image)+L"\" --source-owner-child \""+registry+L"\" \""+testScope+L"\" \""+out.wstring()+L"\" "+(expected?L"yes":L"no");
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION pi{};
 require(CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi)!=FALSE,"actual owned child created");
 CloseHandle(pi.hThread);
 struct Child{HANDLE h;~Child(){if(WaitForSingleObject(h,0)!=WAIT_OBJECT_0){TerminateProcess(h,91);WaitForSingleObject(h,3000);}CloseHandle(h);}} peer{pi.hProcess};
 const auto heldBirth=birth(pi.hProcess);
 require(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0,"actual child naturally joined");
 DWORD code=99;require(GetExitCodeProcess(pi.hProcess,&code)!=FALSE&&code==0,"child original assertions passed");
 std::ifstream f(out);Json value;f>>value;
 require(value.at("pid")==pi.dwProcessId&&value.at("birth")==heldBirth,"child receipt bound to held actual PID/birth");
 return value;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc==6&&std::wstring_view(argv[1])==L"--source-owner-child"){
  testScope=argv[3];Observer observer(argv[2]);const bool started=observer.start([](bool){},[]{return false;});
  const bool expected=std::wstring_view(argv[5])==L"yes";
  require(started==expected,"child source ownership decision");
  if(expected)require(until([&]{return observer.identitySnapshot()!=nullptr;}),"child actual source publishes");
  else require(observer.steamPresence()==-1&&!observer.identitySnapshot()&&!owns(observer),"denied child remains unknown without observer");
  const auto p=GetCurrentProcessId();const auto b=birth(GetCurrentProcess());observer.finishFactReadsForExit();
  std::ofstream f{std::filesystem::path(argv[4])};f<<Json({{"status","PASS"},{"pid",p},{"birth",b},{"started",started},{"expected",expected},{"checks",checks}}).dump();return 0;
 }
 require(argc==3&&std::wstring_view(argv[1])==L"--source-owner","exact original observer parameter selector");
 const std::wstring mode=argv[2];
#ifdef YMCC_SOURCE_OWNER_RETIREMENT_ONLY
 if(mode!=L"retired-restart"&&mode!=L"retired-close-failure"&&mode!=L"stopped-close-failure"&&mode!=L"partial-start-close-failure"&&mode!=L"action-install-root")return 2;
#else
 if(mode!=L"retired-restart"&&mode!=L"retired-close-failure"&&mode!=L"stopped-close-failure"&&mode!=L"partial-start-close-failure"&&mode!=L"duplicate"&&mode!=L"cross-process"&&mode!=L"drain"&&mode!=L"type-collision"&&mode!=L"create-denied"&&mode!=L"action-install-root")return 2;
#endif
 const std::wstring suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 testScope=L"Local\\YMCCFixture.SourceOwner."+suffix;
 wchar_t image[32768]{};require(GetModuleFileNameW(nullptr,image,32768)>0,"own observer image");
 Registry registry(L"Software\\YeManCCFixture\\SourceOwner\\"+suffix);registry.root(std::filesystem::path(image).parent_path().wstring());registry.number(L"pid",GetCurrentProcessId());registry.number(L"ActiveUser",42);
 if(mode==L"action-install-root"){
  coldRootEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);coldRootRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  struct Gates{~Gates(){if(coldRootEntered)CloseHandle(coldRootEntered);if(coldRootRelease)CloseHandle(coldRootRelease);}} gates;
  require(coldRootEntered&&coldRootRelease,"fixture has two owned cold-publication gates");
  Observer first(registry.path),duplicate(registry.path);
  struct Release{~Release(){SetEvent(coldRootRelease);}} ensureRelease;
  const auto denied=[&](Observer& observer){const auto fact=observer.readInstallRootForAction();return !fact.knownForAction()&&fact.error==ERROR_NOT_READY&&fact.path.empty();};
  require(denied(first)&&callerRootReads==0,"before claim returns unknown without reading registry");
  require(first.start([](bool){},[]{return false;})&&WaitForSingleObject(coldRootEntered,2000)==WAIT_OBJECT_0&&!first.identitySnapshot(),
      "actual observer is active before its first identity publication");
  require(!duplicate.start([](bool){},[]{return false;})&&denied(duplicate)&&callerRootReads==0,"duplicate source never performs fallback root read");
  const std::wstring firstPath=L"C:/Mock Steam/";
  registry.root(firstPath);
  const auto initial=first.readInstallRootForAction();
  require(initial.knownForAction()&&initial.error==ERROR_SUCCESS&&initial.path==firstPath&&callerRootReads==1&&!first.identitySnapshot(),
      "owned cold action reads one raw root without waiting for publication");
  registry.root(L"D:/Current Steam");
  const auto updated=first.readInstallRootForAction();
  require(updated.error==ERROR_SUCCESS&&updated.path==L"D:/Current Steam"&&callerRootReads==2,"changed root is read fresh once");
  registry.root(L"");
  const auto empty=first.readInstallRootForAction();
  require(empty.error==ERROR_SUCCESS&&empty.knownForAction()&&empty.path.empty()&&callerRootReads==3,"successful empty root remains known empty");
  require(RegDeleteKeyValueW(HKEY_CURRENT_USER,registry.path.c_str(),L"SteamPath")==ERROR_SUCCESS,"fixture removes only its SteamPath value");
  const auto missing=first.readInstallRootForAction();
  require(missing.error==ERROR_FILE_NOT_FOUND&&missing.knownForAction()&&missing.path.empty()&&callerRootReads==4,"missing SteamPath remains known absent for action");
  registry.number(L"SteamPath",7,false);
  const auto malformed=first.readInstallRootForAction();
  require(malformed.error==ERROR_UNSUPPORTED_TYPE&&!malformed.knownForAction()&&malformed.path.empty()&&callerRootReads==5,
      "malformed SteamPath is unknown rather than absent");
  SetEvent(coldRootRelease);first.finishFactReadsForExit();
  require(denied(first)&&callerRootReads==5,"retired source cannot issue another root read");
 }else if(mode==L"stopped-close-failure"||mode==L"partial-start-close-failure"){
  DWORD baseline=0;require(GetProcessHandleCount(GetCurrentProcess(),&baseline)!=FALSE,"actual initial process handle count");
  Observer first(registry.path),second(registry.path);
  captureRunEvents=true;
  if(mode==L"partial-start-close-failure"){failSecondRunEvent=true;protectFirstRunEvent=true;}
  const bool started=first.start([](bool){},[]{return false;});captureRunEvents=false;protectFirstRunEvent=false;
  if(mode==L"partial-start-close-failure"){
   require(!started&&capturedEventCount==1,"second event failure reaches original partial-start cleanup");
  }else{
   require(started&&until([&]{return first.identitySnapshot()!=nullptr;}),"actual worker publishes before ordinary stop");
   require(capturedEventCount==2&&SetHandleInformation(capturedRunEvents[0],HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"actual stop event protected");
   first.stop();
  }
  const auto retained=capturedRunEvents[0];DWORD flags=0;
  require(retained&&GetHandleInformation(retained,&flags)!=FALSE&&(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE),"failed cleanup retains exact real event handle");
  require(!first.sourceActive()&&!first.identitySnapshot()&&first.steamPresence()==-1,"failed cleanup remains unknown");
  require(!first.start([](bool){},[]{return false;}),"explicit retry blocked while event refuses close");
  require(!second.start([](bool){},[]{return false;}),"failed cleanup keeps original source ownership");
  require(SetHandleInformation(retained,HANDLE_FLAG_PROTECT_FROM_CLOSE,0)!=FALSE,"fixture releases exact event close protection");
  require(first.start([](bool){},[]{return false;}),"explicit retry recovers after old event is released");
  require(until([&]{return first.identitySnapshot()!=nullptr;}),"recovered original worker publishes");
  first.finishFactReadsForExit();second.finishFactReadsForExit();
  DWORD finalCount=0;require(GetProcessHandleCount(GetCurrentProcess(),&finalCount)!=FALSE,"actual final process handle count");
  require(finalCount==baseline,"original events threads and source mutex all returned");
 }else if(mode==L"retired-restart"||mode==L"retired-close-failure"){
  HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  require(entered&&release,"own retirement gates created");
  struct Gates{HANDLE a,b;~Gates(){SetEvent(b);CloseHandle(a);CloseHandle(b);}} gates{entered,release};
  Observer first(registry.path),second(registry.path);
  struct Release{HANDLE h;~Release(){SetEvent(h);}} ensureRelease{release};
  std::atomic<HANDLE> worker{nullptr};std::atomic<unsigned> retires{0},faults{0};
  captureRunEvents=true;
  const bool started=first.start([](bool){},[&]()->bool{++faults;throw std::bad_alloc();},{},[&](auto snapshot){
   if(snapshot)return;
   worker.store(OpenThread(SYNCHRONIZE,FALSE,GetCurrentThreadId()));++retires;
   SetEvent(entered);WaitForSingleObject(release,5000);
  });captureRunEvents=false;
  require(started,"first actual worker starts before failure");
  require(WaitForSingleObject(entered,3000)==WAIT_OBJECT_0,"actual worker failure reaches original retirement callback");
  const auto oldRead=first.sourceRead();require(!oldRead&&!first.sourceActive()&&!first.identitySnapshot()&&first.steamPresence()==-1,"retiring source is unknown and cannot grant a read");
  const auto begin=GetTickCount64();require(!first.start([](bool){},[]{return false;}),"tail callback in flight cannot restart source");
  require(GetTickCount64()-begin<100,"explicit start does not wait for in-flight retirement callback");
  require(!second.start([](bool){},[]{return false;}),"original source ownership blocks parallel replacement");
  const HANDLE priorThread=worker.load();require(priorThread!=nullptr,"actual failing worker handle held");
  SetEvent(release);require(WaitForSingleObject(priorThread,3000)==WAIT_OBJECT_0,"actual old worker terminated");CloseHandle(priorThread);
  require(faults==1&&retires==1,"exact one original worker failure and retirement");
  if(mode==L"retired-close-failure"){
   require(capturedEventCount==2&&capturedRunEvents[0]&&capturedRunEvents[1],"exact original two run event handles captured");
   const auto retained=capturedRunEvents[0];require(SetHandleInformation(retained,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"real original run event refuses close");
   require(!first.start([](bool){},[]{return false;}),"retained event close failure blocks new worker");
   DWORD flags=0;require(GetHandleInformation(retained,&flags)!=FALSE&&(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE),"rejected event remains owned and readable");
   require(SetHandleInformation(retained,HANDLE_FLAG_PROTECT_FROM_CLOSE,0)!=FALSE,"fixture removes actual close protection");
  }
  require(first.start([](bool){},[]{return false;}),"same owner explicitly restarts completed failed worker");
  require(until([&]{const auto p=first.identitySnapshot();return p&&p->presence==1&&p->session.pid==GetCurrentProcessId();}),"new original worker publishes current identity");
  require(!second.start([](bool){},[]{return false;}),"recovered source remains sole owner");
  first.finishFactReadsForExit();require(!first.start([](bool){},[]{return false;}),"final exit retirement prevents resurrection");
  require(second.start([](bool){},[]{return false;}),"next owner starts only after final source retirement");second.finishFactReadsForExit();
 }else if(mode==L"type-collision"){
  HANDLE collision=CreateEventW(nullptr,TRUE,FALSE,testScope.c_str());require(collision!=nullptr,"actual incompatible kernel object created");
  struct Collision{HANDLE h;~Collision(){if(h)CloseHandle(h);}} conflict{collision};
  Observer observer(registry.path);require(!observer.start([](bool){},[]{return false;}),"existing wrong object type cannot create second source");
  require(!observer.identitySnapshot()&&observer.steamPresence()==-1&&!owns(observer),"type failure stays unknown");
  CloseHandle(collision);conflict.h=nullptr;require(observer.start([](bool){},[]{return false;}),"explicit retry after conflict removal");observer.finishFactReadsForExit();
 }else if(mode==L"create-denied"){
  denyCreate=true;Observer observer(registry.path);require(!observer.start([](bool){},[]{return false;}),"real claim leaf denial cannot start source");
  require(observer.steamPresence()==-1&&!observer.identitySnapshot()&&!owns(observer),"denied source never publishes absence");
  denyCreate=false;require(observer.start([](bool){},[]{return false;}),"explicit retry after claim denial");observer.finishFactReadsForExit();
 }else if(mode==L"duplicate"||mode==L"cross-process"||mode==L"drain"){
  Observer first(registry.path),second(registry.path);
  require(first.start([](bool){},[]{return false;}),"first original observer starts");
  require(until([&]{const auto p=first.identitySnapshot();return p&&p->presence==1&&p->session.pid==GetCurrentProcessId();}),"first actual immutable publication");
  require(owns(first)&&running(first),"first retains exact source slot");
  if(mode==L"cross-process"){
   const auto base=std::filesystem::path(image).parent_path();
   child(registry.path,base/(L"denied-"+suffix+L".json"),false);
   first.stop();child(registry.path,base/(L"stopped-"+suffix+L".json"),false);
   first.finishFactReadsForExit();child(registry.path,base/(L"reclaim-"+suffix+L".json"),true);
  }else if(mode==L"drain"){
   HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(entered&&release,"own original executor gates");
   struct Gates{HANDLE a,b;~Gates(){SetEvent(b);CloseHandle(a);CloseHandle(b);}} gates{entered,release};
   require(first.postFactRead([&]{SetEvent(entered);WaitForSingleObject(release,5000);}),"actual original source read admitted");
   struct Release{HANDLE h;~Release(){SetEvent(h);}} alwaysRelease{release};
   require(WaitForSingleObject(entered,1000)==WAIT_OBJECT_0,"real read entered");
   std::thread stop([&]{first.stop();});stop.join();
   require(!second.start([](bool){},[]{return false;}),"stop from other thread retains source slot");
   std::atomic<bool> done{false};std::thread finish([&]{first.finishFactReadsForExit();done.store(true);});
   struct Join{std::thread& t;HANDLE h;~Join(){SetEvent(h);if(t.joinable())t.join();}} join{finish,release};
   Sleep(50);require(!done.load(),"final cleanup waits for real in-flight source read");
   require(!second.start([](bool){},[]{return false;}),"drain in progress cannot release source ownership");
   SetEvent(release);finish.join();require(done.load(),"actual source read drained");
   require(second.start([](bool){},[]{return false;}),"explicit successor claim after actual drainage");second.finishFactReadsForExit();
  }else{
   require(!second.start([](bool){},[]{return false;}),"duplicate original observer cannot start a second source");
   require(second.steamPresence()==-1&&!second.identitySnapshot()&&!owns(second),"denied observer remains unknown");
   require(!second.postFactRead([]{}),"denied observer cannot acquire facts");
#ifndef YMCC_SOURCE_OWNER_RETIREMENT_ONLY
   const auto unavailable=ymcc::steaminfo::collect(ymcc::steaminfo::All,second,{});
   require(!unavailable.available&&unavailable.presence==-1&&unavailable.installations.empty(),"public collector denies private discovery for nonowner");
#endif
   first.stop();require(owns(first)&&!running(first),"ordinary stop keeps owner but not healthy source");
   require(!second.start([](bool){},[]{return false;}),"ordinary stop is not takeover permission");
   require(first.start([](bool){},[]{return false;}),"same owning observer can explicitly restart");
   first.finishFactReadsForExit();require(!first.start([](bool){},[]{return false;}),"retired original observer cannot restart");
   require(second.start([](bool){},[]{return false;}),"explicit new owner after final cleanup");second.finishFactReadsForExit();
  }
 }else return 2;
 std::cout<<Json({{"status","PASS"},{"selection",selectionText(mode)},{"checks",checks},{"actualObserver",true},{"realSteamActions",0},{"sourceReady",false}}).dump()<<std::endl;return 0;
 }catch(const std::exception& x){std::cout<<Json({{"status","FAIL"},{"error",x.what()},{"checks",checks}}).dump()<<std::endl;return 1;}}
