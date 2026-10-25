#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "steam_original_recovery_engine.h"
#include "steam_original_recovery_wake_watch.h"
#include "steam_original_recovery_file_hash.h"
#include "steam_original_recovery_source_diagnostic.h"
#include "steam_original_recovery_semistatic_source.h"
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include <array>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include "steam_original_recovery_engine/vendor/steam_native_app_state_win32.h"
#include "steam_original_recovery_engine/running-publish-trial.h"
#include "steam_original_recovery_engine/embedded_trial.h"
static_assert(ymcc::steamOriginalRecovery::embedded::trialAbi==running_trial::abi,"embedded trial Packet ABI must match host");
#include "steam_original_recovery_engine/threadname-policy.h"
#pragma warning(push,0)
#include "steam_original_recovery_engine/query-sdk/isteamclient.h"
#include "steam_original_recovery_engine/query-sdk/isteamuser.h"
#pragma warning(pop)
namespace ymcc::steamOriginalRecovery {
namespace {
using J=Json;using U64=std::uint64_t;namespace rw=ymcc::steamrecordwin;namespace app=ymcc::steamappstate;namespace rc=ymcc::steamrecord;namespace fp=std::filesystem;namespace tr=running_trial;namespace compat=ymcc::steamcompat;
// Present even on pre-snapshot refusals so an ordinary exported log identifies
// the executing engine, independently of the host's display version.
// The revision includes the host identity lease and process-lifetime tracker fixes.
// Ordinary build/package guards bind this marker to those actual host sources.
constexpr const char* kEngineRevision="dual-route-precision-window-20261024-r11";
constexpr const char* kModuleQualification="content-contract-and-loaded-image";
enum class RecoveryRoute { LegacyDynamic, SemiStatic };
thread_local RecoveryRoute gRecoveryRoute=RecoveryRoute::SemiStatic;
bool legacyRoute(){return gRecoveryRoute==RecoveryRoute::LegacyDynamic;}
const char* routeName(){return legacyRoute()?"legacy-dynamic":"semi-static";}
const char* routeQualification(){return legacyRoute()?"known-hash-pair-and-original-loaded-guards":kModuleQualification;}
std::atomic<bool> gActive{false};std::mutex gStateMutex;J gState={{"status","idle"}};
thread_local Emit gEmit;thread_local Current gCurrent,gObservationCurrent;thread_local WakeSignal gWakeSignal=0;thread_local std::wstring gTrialDirectory;
thread_local bool gCleanupMode=false;thread_local std::function<bool()> gOriginalSteam;
#ifdef YMCC_STEAM_ORIGINAL_ENGINE_TESTING
// Faults run before scope/process opening, only in this offline build.
thread_local std::function<void(J&)> gOfflineTypedReadFault;
#endif
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct DiagnosticFailure:std::runtime_error {
  J detail;
  DiagnosticFailure(const char* reason,J value):std::runtime_error(reason),detail(std::move(value)){}
};
// The typed diagnostics intentionally use small non-std rejection objects.
// Route them through the engine/driver's existing std failure and cleanup
// paths; a rejected read never grants retry or mutation authority.
template<class Function>
decltype(auto) nativeFailureBoundary(Function&& operation,const char* unknownReason){
  try{return std::forward<Function>(operation)();}
  catch(const app::detail::Rejected& error){throw DiagnosticFailure(error.reason?error.reason:"native-ui-state-rejected",
      {{"stage","typed-native-state-read"},{"component","SteamUI"},{"exceptionClass","typed-rejection"}});}
  catch(const rc::detail::Rejected& error){throw DiagnosticFailure(error.reason?error.reason:"native-record-state-rejected",
      {{"stage","typed-native-state-read"},{"component","steamclient"},{"exceptionClass","typed-rejection"}});}
  catch(const std::exception&){throw;}
  catch(...){throw DiagnosticFailure(unknownReason,
      {{"stage","native-operation"},{"exceptionClass","unknown-cpp"}});}
}

// Independent legacy selector. Unknown or mixed pairs never gain offsets.
tr::NativeProfile qualifiedProfile(std::string_view client,std::string_view ui){
  const auto profile=tr::profileForHashes(client,ui);
  const auto record=rc::profileIdForSha256(client);const auto state=app::profileIdForSha256(ui);
  if(!profile||!record||!state)throw DiagnosticFailure("fixed-profile-hash",{
      {"stage","module-profile-before-memory-read"},{"clientSha256",client},{"uiSha256",ui},
      {"clientKnown",bool(record)},{"uiKnown",bool(state)},{"pairQualified",bool(profile)},
      {"beforeMemoryRead",true},{"automaticFallback",false}});
  return *profile;
}
rc::ProfileId recordProfileForLayout(compat::Layout layout){return layout==compat::Layout::Public ? rc::ProfileId::Public1792618285 : rc::ProfileId::Legacy1791918634;}
app::ProfileId uiProfileForLayout(compat::Layout layout){return layout==compat::Layout::Public ? app::ProfileId::Public1792618285 : app::ProfileId::Legacy1791918634;}
J compatibilityEvidence(const compat::Evidence& proof){return {{"matched",proof.matched},{"rangesChecked",proof.rangesChecked},
    {"bytesChecked",proof.bytesChecked},{"mutableRangesIgnored",proof.mutableRangesIgnored},{"failedRva",proof.failedRva},{"reason",proof.reason}};}
J compatibilityEvidence(const compat::Evidence& proof,const compat::LoadedDiagnostic& detail){
  auto value=compatibilityEvidence(proof);
  value["importDiagnostic"]={{"reason",detail.reason},{"stage",detail.stage},{"library",detail.library},{"symbol",detail.symbol},
      {"slotRva",detail.slotRva},{"target",detail.target},{"allocationBase",detail.allocationBase},{"exportRva",detail.exportRva},
      {"state",detail.state},{"type",detail.type},{"protect",detail.protect},{"win32Error",detail.win32Error},
      {"failedAddress",detail.failedAddress},{"bytesRequested",detail.bytesRequested},{"bytesRead",detail.bytesRead}};
  return value;
}
struct CompatibleFiles{compat::Qualification client,ui;tr::NativeProfile native;};
CompatibleFiles compatibleFiles(compat::Qualification client,compat::Qualification ui){
  if(!client.layout||!ui.layout)throw DiagnosticFailure("native-compatibility-contract",{
      {"stage","disk-code-and-data-contract-before-target-read"},{"clientSha256",client.sha256},{"uiSha256",ui.sha256},
      {"clientReason",client.reason},{"uiReason",ui.reason},{"clientWin32Error",client.win32Error},{"uiWin32Error",ui.win32Error},
      {"record",compatibilityEvidence(client.record)},{"publisher",compatibilityEvidence(client.publisher)},
      {"ui",compatibilityEvidence(ui.ui)},{"beforeMemoryRead",true},{"wholeImageHashIsVersionGate",false}});
  auto profile=tr::profileForCompatibility(*client.layout,*ui.layout);
  need(bool(profile),"compatibility-layout-selection");
  profile->getterRva=compat::resolvedRva(client,profile->getterRva);profile->publisherRva=compat::resolvedRva(client,profile->publisherRva);
  need(profile->getterRva&&profile->publisherRva,"compatibility-entry-location-unresolved");
  return {std::move(client),std::move(ui),*profile};
}
void requireReadProcess(bool opened,DWORD error,DWORD pid,const char* reason){
  if(!opened)throw DiagnosticFailure(reason,{{"stage","open-steam-process-for-read"},
      {"win32Error",error},{"accessDenied",error==ERROR_ACCESS_DENIED},{"steamPid",pid},
      {"requestedAccess",PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE},
      {"beforeMemoryRead",true},{"automaticElevation",false}});
}
// Execution-only timing: no probe exists during idle or wake observation.
// Counts and clocks never participate in Current or mutation admission.
struct ExecutionProbe {
  U64 calls=0,accepted=0,rejected=0,exceptions=0,clockFailures=0,ticks=0;
  U64 armAttempts=0,censusThreads=0,getterEvents=0,matchingGetterEvents=0,flagEvents=0;
  std::array<U64,10> debugEvents{};U64 otherDebugEvents=0;
};
thread_local ExecutionProbe* gExecutionProbe=nullptr;
bool current(){
  auto* const probe=gExecutionProbe;
  if(!probe){try{return !gCurrent||gCurrent();}catch(...){return false;}}
  LARGE_INTEGER begin{},end{};const bool clock=QueryPerformanceCounter(&begin)&&begin.QuadPart>=0;
  bool accepted=false,threw=false;
  try{accepted=!gCurrent||gCurrent();}catch(...){threw=true;}
  ++probe->calls;if(accepted)++probe->accepted;else ++probe->rejected;if(threw)++probe->exceptions;
  if(clock&&QueryPerformanceCounter(&end)&&end.QuadPart>=begin.QuadPart)
    probe->ticks+=static_cast<U64>(end.QuadPart-begin.QuadPart);
  else ++probe->clockFailures;
  return accepted;
}
struct ExecutionProbeScope {
  J& summary;const char* name;ExecutionProbe value;ExecutionProbe* prior;
  ExecutionProbeScope(J& out,const char* phase):summary(out),name(phase),prior(gExecutionProbe){gExecutionProbe=&value;}
  ~ExecutionProbeScope() noexcept {
    gExecutionProbe=prior;
    try{LARGE_INTEGER frequency{};const bool clock=QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0;
      J events=J::object();static constexpr std::array<const char*,10> names{
        "unused","exception","createThread","createProcess","exitThread","exitProcess","loadDll","unloadDll","outputDebugString","rip"};
      for(std::size_t i=1;i<names.size();++i)events[names[i]]=value.debugEvents[i];
      summary["executionDiagnostics"][name]={{"currentGuard",{{"calls",value.calls},{"accepted",value.accepted},
        {"rejected",value.rejected},{"exceptions",value.exceptions},{"clockFailures",value.clockFailures+(clock?0:1)},
        {"wallMs",clock?J(double(value.ticks)*1000.0/double(frequency.QuadPart)):J(nullptr)},
        {"scope","calling worker Current callback only; excludes QueryActor and exactProcess after Current"}}},
        {"debugEvents",events},{"otherDebugEvents",value.otherDebugEvents},{"armAttempts",value.armAttempts},
        {"censusThreads",value.censusThreads},{"getterEvents",value.getterEvents},{"matchingGetterEvents",value.matchingGetterEvents},
        {"flagEvents",value.flagEvents},{"eventCountersScope","captureGetterContext only; driver debugEvents already recorded separately"},
        {"diagnosticOnly",true},{"participatesInAdmission",false},{"instrumentationOverheadIncluded",true}};
    }catch(...){}
  }
};
void noteContextDebugEvent(DWORD code) noexcept {if(auto* p=gExecutionProbe){if(code<p->debugEvents.size())++p->debugEvents[code];else ++p->otherDebugEvents;}}

void requireCurrent(){need(current(),"native-current-guard-revoked");}
bool observationCurrent(){try{return gObservationCurrent?gObservationCurrent():current();}catch(...){return false;}}
void requireCleanupOwnership(){need(gOriginalSteam&&gOriginalSteam(),"original-steam-cleanup-identity-unavailable");}
void enterCleanup(){requireCleanupOwnership();gCleanupMode=true;}
void requireTargetMutation(){if(gCleanupMode)requireCleanupOwnership();else requireCurrent();}
void publish(const J& row) noexcept {try{{std::lock_guard<std::mutex> lock(gStateMutex);gState=row;if(row.contains("result")&&row.at("result").is_object()&&row.at("result").contains("status"))gState["status"]=row.at("result").at("status");else if(row.contains("event"))gState["status"]=row.at("event");gState["active"]=gActive.load();gState["ownerThreadId"]=GetCurrentThreadId();}if(gEmit)gEmit(row);}catch(...) {}}
struct H{HANDLE h=nullptr;H()=default;explicit H(HANDLE p):h(p){}~H(){if(good())CloseHandle(h);}H(const H&)=delete;H&operator=(const H&)=delete;H(H&& p)noexcept:h(std::exchange(p.h,nullptr)){}H&operator=(H&& p)noexcept{if(this!=&p){if(good())CloseHandle(h);h=std::exchange(p.h,nullptr);}return *this;}bool good()const{return h&&h!=INVALID_HANDLE_VALUE;}};
H duplicate(HANDLE h){HANDLE p=nullptr;need(DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&p,0,FALSE,DUPLICATE_SAME_ACCESS),"duplicate-handle");return H(p);}
std::wstring utf16(const std::string& s){need(!s.empty()&&s.size()<32768&&s.find('\0')==std::string::npos,"utf8-text");const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);need(n>0,"utf8");std::wstring w(static_cast<std::size_t>(n),L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n)==n,"utf8");return w;}
std::string utf8(const std::wstring& s){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);need(n>=0,"utf16");std::string out(static_cast<std::size_t>(n),'\0');if(n)need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr)==n,"utf16");return out;}
U64 decimal(const J& value){need(value.is_string(),"decimal-string-required");const auto s=value.get<std::string>();need(!s.empty()&&s.size()<=20,"invalid-decimal");U64 n=0;for(const char c:s){need(c>='0'&&c<='9',"invalid-decimal");const auto d=static_cast<U64>(c-'0');need(n<=(std::numeric_limits<U64>::max()-d)/10,"decimal-overflow");n=n*10+d;}need(n!=0,"zero-identity");return n;}
DWORD dword(const J& j,bool zero=false){need(j.is_number_integer(),"integer-required");if(!j.is_number_unsigned())need(j.get<std::int64_t>()>=0,"negative-integer");const auto n=j.get<U64>();need(n<=MAXDWORD&&(zero||n),"invalid-dword");return static_cast<DWORD>(n);}
std::string digest(const void* p,std::size_t count){BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;need(count<=std::numeric_limits<ULONG>::max()&&BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash-provider");if(BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(a,0);throw std::runtime_error("hash-create");}unsigned char bytes[32]{};const auto ok=BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<void*>(p)),static_cast<ULONG>(count),0)>=0&&BCryptFinishHash(h,bytes,32,0)>=0;BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);need(ok,"hash-result");std::string out;for(const auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::string textHash(const std::string& s){return digest(s.data(),s.size());}
std::string fileHash(const std::wstring& file){return ymcc::steamOriginalHash::hashFileFully(file).sha256;}
std::string freshUuid(){std::array<unsigned char,16> b{};need(BCryptGenRandom(nullptr,b.data(),static_cast<ULONG>(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"nonce-rng");b[6]=static_cast<unsigned char>((b[6]&15)|64);b[8]=static_cast<unsigned char>((b[8]&63)|128);std::string out;for(std::size_t i=0;i<b.size();++i){if(i==4||i==6||i==8||i==10)out+='-';out+="0123456789abcdef"[b[i]>>4];out+="0123456789abcdef"[b[i]&15];}return out;}
fp::path absolute(const J& j){const auto p=utf16(j.get<std::string>());need(p.size()>3&&p[1]==L':'&&(p[2]==L'\\'||p[2]==L'/'),"absolute-path-required");return fp::absolute(p).lexically_normal();}
J readJson(const fp::path& file){std::ifstream f(file,std::ios::binary);need(f.good(),"reference-open");std::string s((std::istreambuf_iterator<char>(f)),{});need(s.size()<8*1024*1024,"reference-limit");if(s.starts_with("\xef\xbb\xbf"))s.erase(0,3);return J::parse(s);}
void durableCreate(const fp::path& file,const std::string& text){fp::create_directories(file.parent_path());H h(CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr));need(h.good(),GetLastError()==ERROR_FILE_EXISTS?"event-already-consumed":"durable-create");DWORD written=0;need(text.size()<=MAXDWORD&&WriteFile(h.h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(h.h),"durable-flush-no-refund");}
void atomicJson(const fp::path& file,const J& value){fp::create_directories(file.parent_path());auto temp=file;temp+=L".next-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetCurrentThreadId());{std::ofstream f(temp,std::ios::binary|std::ios::trunc);need(f.good(),"status-open");f<<value.dump(2)<<'\n';need(f.good(),"status-write");}need(MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"status-atomic-replace");}

struct LegacyFile {std::string sha256;};
LegacyFile legacyFile(const std::wstring& path){return {fileHash(path)};}
compat::Evidence legacyLoadedGuard(const LegacyFile& file,compat::ModuleKind kind,const compat::ReadRva& read,U64 base,U64 size){
  need(read&&base>=0x10000&&base<=0x7fffffffffffULL&&size&&size-1<=0x7fffffffffffULL-base,"legacy-loaded-image-bounds");
  const rc::Read raw=[&](U64 address,void* out,std::size_t count){return address>=base&&address-base<size&&count<=size-(address-base)&&read(address-base,out,count);};
  if(kind==compat::ModuleKind::Client){
    const auto profile=rc::profileIdForSha256(file.sha256);need(bool(profile),"legacy-client-profile-unknown");
    const auto& layout=rc::detail::profileLayout(*profile);std::array<unsigned char,14> code{};
    need(raw(base+layout.getterRva,code.data(),code.size())&&code==layout.getterBytes,"legacy-loaded-getter-mismatch");
    // Full original record getter/vtable/account/primary checks follow via
    // rc::observe and selectUser on raw addresses, before capture or dispatch.
    return {true,1,code.size(),0,"legacy-original-getter-instruction-guard"};
  }
  const auto profile=app::profileIdForSha256(file.sha256);need(bool(profile),"legacy-ui-profile-unknown");
  app::Query query{base,size,0,true,*profile};app::detail::Reader reader(raw);app::detail::checkAnchors(reader,query);
  return {true,reader.calls,reader.bytes,0,"legacy-original-typed-ui-code-and-vtable-guards"};
}

void readExact(HANDLE p,U64 address,void* out,std::size_t n){SIZE_T got=0;need(ReadProcessMemory(p,reinterpret_cast<const void*>(address),out,n,&got)&&got==n,"remote-read-exact");}
// Every target write and every helper-owned control operation checks Current.
// Mark an attempted entry write only after its business/cleanup guard passed.
// A rejected guard has not dispatched any target write.
void writeExact(HANDLE p,U64 address,const void* in,std::size_t n,bool* writeDispatched=nullptr){requireTargetMutation();if(writeDispatched)*writeDispatched=true;SIZE_T got=0;need(WriteProcessMemory(p,reinterpret_cast<void*>(address),in,n,&got)&&got==n,"remote-write-exact");}
// Diagnostic clocks never decide admission, cancellation or a mutation deadline.
struct WorkerStamp {U64 wall=0,cpu100ns=0,cycles=0;unsigned available=0;};
WorkerStamp workerStamp() noexcept {
  WorkerStamp stamp;LARGE_INTEGER wall{};FILETIME create{},exit{},kernel{},user{};ULONG64 cycles=0;
  if(QueryPerformanceCounter(&wall)&&wall.QuadPart>=0){stamp.wall=static_cast<U64>(wall.QuadPart);stamp.available|=1;}
  if(GetThreadTimes(GetCurrentThread(),&create,&exit,&kernel,&user)){
    stamp.cpu100ns=((U64(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((U64(user.dwHighDateTime)<<32)|user.dwLowDateTime);stamp.available|=2;}
  if(QueryThreadCycleTime(GetCurrentThread(),&cycles)){stamp.cycles=cycles;stamp.available|=4;}return stamp;
}
struct WorkerCost {U64 calls=0,cpu100ns=0,cycles=0,clockFailures=0;double wallMs=0;
  void add(const WorkerStamp& begin,const WorkerStamp& end) noexcept {
    ++calls;const auto available=begin.available&end.available;
    if(available!=7)++clockFailures;
    if((available&1)&&end.wall>=begin.wall){LARGE_INTEGER frequency{};
      if(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0)
        wallMs+=1000.0*static_cast<double>(end.wall-begin.wall)/static_cast<double>(frequency.QuadPart);
      else ++clockFailures;}
    if((available&2)&&end.cpu100ns>=begin.cpu100ns)cpu100ns+=end.cpu100ns-begin.cpu100ns;
    if((available&4)&&end.cycles>=begin.cycles)cycles+=end.cycles-begin.cycles;
  }
};
struct WorkerTimer {WorkerCost& cost;WorkerStamp begin=workerStamp();
  explicit WorkerTimer(WorkerCost& target):cost(target){}
  ~WorkerTimer() noexcept {cost.add(begin,workerStamp());}
};
J workerCostJson(const WorkerCost& cost){return {{"calls",cost.calls},{"wallMs",cost.wallMs},
    {"workerCpu100ns",cost.cpu100ns},{"workerCycles",cost.cycles},{"clockFailures",cost.clockFailures}};}
void noteWorkerPhase(J& summary,const char* name,const WorkerStamp& begin) noexcept {
  try{WorkerCost cost;cost.add(begin,workerStamp());summary["phaseCost"][name]=workerCostJson(cost);
    summary["phaseCostScope"]="recovery-worker-only; excludes QueryActor and Steam remote threads; clock and nested-diagnostic overhead included";
  }catch(...){} // Missing diagnostics cannot authorize or refuse a recovery.
}
void noteSnapshotCost(J& summary,const char* name,const J& profile) noexcept {
  try{summary["snapshotCosts"][name]=profile;}catch(...){}
}
struct BoundLive{DWORD pid=0,session=0;U64 born=0;std::wstring path;H handle;};
BoundLive bindLive(const J& j,DWORD access){BoundLive p;p.pid=dword(j.at("pid"));p.session=dword(j.at("session"),true);p.born=decimal(j.at("birthFileTime"));p.path=utf16(j.at("path").get<std::string>());p.handle=H(OpenProcess(access,FALSE,p.pid));need(p.handle.good()&&rw::exactProcess(p.handle.h,p.pid,p.born,p.path,p.session),"exact-process-binding");return p;}
J normalizedIdentity(const J& p){const auto birth=p.contains("birth")?p.at("birth"):p.at("birthFileTime");const auto birthValue=decimal(birth);const auto path=absolute(p.at("path"));return{{"pid",dword(p.at("pid"))},{"birthFileTime",std::to_string(birthValue)},{"path",utf8(rw::pathKey(path.wstring()))},{"session",dword(p.at("session"),true)}};}
struct Scope{std::array<BoundLive,4> roles;J identities;U64 gameId=0;DWORD appid=0;bool owned()const{if(!current())return false;for(const auto& p:roles)if(!rw::exactProcess(p.handle.h,p.pid,p.born,p.path,p.session))return false;return true;}};
J resolveCef(const J& steam){H snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));need(snap.good(),"cef-census");PROCESSENTRY32W p{};p.dwSize=sizeof p;need(Process32FirstW(snap.h,&p),"cef-census-first");std::vector<J> candidates;do{if(p.th32ParentProcessID==dword(steam.at("pid"))&&rw::pathKey(p.szExeFile)==L"steamwebhelper.exe"){H h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.th32ProcessID));need(h.good(),"cef-query");FILETIME b{},e{},k{},u{};DWORD session=0;need(GetProcessTimes(h.h,&b,&e,&k,&u)&&ProcessIdToSessionId(p.th32ProcessID,&session),"cef-identity");const auto birth=(U64(b.dwHighDateTime)<<32)|b.dwLowDateTime;std::wstring path(32768,L'\0');DWORD n=static_cast<DWORD>(path.size());need(QueryFullProcessImageNameW(h.h,0,path.data(),&n),"cef-path");path.resize(n);if(session==dword(steam.at("session"),true)&&rw::pathKey(path)==rw::pathKey((fp::path(utf16(steam.at("path").get<std::string>())).parent_path()/L"bin"/L"cef"/L"cef.win64"/L"steamwebhelper.exe").wstring()))candidates.push_back({{"pid",p.th32ProcessID},{"birth",std::to_string(birth)},{"path",utf8(path)},{"session",session}});}}while(Process32NextW(snap.h,&p));need(GetLastError()==ERROR_NO_MORE_FILES&&candidates.size()==1,"cef-ambiguous-or-unavailable");return candidates[0];}
std::pair<U64,DWORD> targetIdentity(const J& game){const auto id=decimal(game.at("gameId"));const auto appid=dword(game.at("appid"));need(((id>>24)&255)==2&&ymcc::steamappstatewin::gameIdMatches(id,appid),"exact-type2-gameid-required");return {id,appid};}
Scope openScope(const J& event){Scope s;const auto& game=event.contains("game")?event.at("game"):event.at("target");const auto target=targetIdentity(game);s.gameId=target.first;s.appid=target.second;const auto cef=event.contains("cef")?event.at("cef"):resolveCef(event.at("steam"));const std::array<J,4> raw{event.at("steam"),game,event.at("parent"),cef};const std::array<const char*,4> names{"steam","game","ymcc","cef"};std::set<DWORD> ids;for(std::size_t i=0;i<raw.size();++i){auto n=normalizedIdentity(raw[i]);need(ids.insert(dword(n.at("pid"))).second,"distinct-binding-pids");s.identities[names[i]]=n;s.roles[i]=bindLive(n,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE);need(s.roles[i].session==s.roles[0].session,"binding-session");}need(s.roles[2].pid==GetCurrentProcessId(),"parent-must-be-current-ymcc-process");need(rw::pathKey(fp::path(s.roles[2].path).filename().wstring())==L"yemancc.exe","parent-image-must-be-ymcc");need(rw::pathKey(fp::path(s.roles[0].path).filename().wstring())==L"steam.exe","steam-image-required");need(rw::pathKey(s.roles[3].path)==rw::pathKey((fp::path(s.roles[0].path).parent_path()/L"bin"/L"cef"/L"cef.win64"/L"steamwebhelper.exe").wstring()),"cef-path-not-in-bound-steam-root");need(s.owned(),"native-current-or-binding-changed");return s;}
unsigned moduleCapacity(DWORD steam){H snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,steam));need(snap.good(),"global-module-census");MODULEENTRY32W p{};p.dwSize=sizeof p;need(Module32FirstW(snap.h,&p),"global-module-first");unsigned count=0;do{const auto name=rw::pathKey(p.szModule);if(name.starts_with(L"wuxia-publish-")&&name.ends_with(L".dll"))++count;}while(Module32NextW(snap.h,&p));need(GetLastError()==ERROR_NO_MORE_FILES,"global-module-census-incomplete");return count;}
// A snapshot requires both images from this one complete module census. No
// process/module identity or hash acceptance is retained between invocations.
bool originalImagePair(DWORD pid,const std::wstring& clientPath,const std::wstring& uiPath,
    rw::Module& client,rw::Module& ui){
  if(!pid||clientPath.empty()||uiPath.empty())return false;
  const auto clientKey=rw::pathKey(clientPath),uiKey=rw::pathKey(uiPath);
  if(clientKey==uiKey)return false;
  H census(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
  if(!census.good())return false;
  MODULEENTRY32W row{};row.dwSize=sizeof row;
  if(!Module32FirstW(census.h,&row))return false;
  rw::Module foundClient,foundUi;unsigned clientMatches=0,uiMatches=0;
  do{const auto observed=rw::pathKey(row.szExePath);
    if(observed==clientKey){++clientMatches;foundClient={reinterpret_cast<U64>(row.modBaseAddr),row.modBaseSize,row.szExePath};}
    if(observed==uiKey){++uiMatches;foundUi={reinterpret_cast<U64>(row.modBaseAddr),row.modBaseSize,row.szExePath};}
  }while(Module32NextW(census.h,&row));
  if(GetLastError()!=ERROR_NO_MORE_FILES||clientMatches!=1||uiMatches!=1||
      foundClient.base<0x10000||!foundClient.size||foundUi.base<0x10000||!foundUi.size)return false;
  client=std::move(foundClient);ui=std::move(foundUi);return true;
}
// This raw field is a diagnostic of Steam's own resume predicate only. It
// never authorizes recovery, proves OS suspension, or writes a game record.
J observeSuspendDiagnostic(const rc::Read& read,U64 record,U64 full){
  try{
    J value={{"source","cuser-suspend-field-diagnostic"},{"profileVersion","steamclient64-e9045710-record-2cc-v1"},
        {"available",false},{"raw",nullptr},{"resumePredicateNonzero",nullptr},
        {"participatesInRecoveryAdmission",false},{"actualOsSuspensionVerified",false},{"readIsAtomic",false}};
    U64 before=0,after=0;std::uint32_t raw=0;
    if(!read(rc::detail::offset(record,8),&before,sizeof before)||
        !read(rc::detail::offset(record,0x2cc),&raw,sizeof raw)||
        !read(rc::detail::offset(record,8),&after,sizeof after)){
      value["reason"]="suspend-diagnostic-read-unavailable";return value;}
    if(before!=full||after!=full){value["reason"]="suspend-diagnostic-full-gameid-changed";return value;}
    value["available"]=true;value["raw"]=raw;value["resumePredicateNonzero"]=raw!=0;
    value["reason"]="suspend-diagnostic-bracketed-field-read";return value;
  }catch(...){return nullptr;} // Optional diagnostic failure never changes a gate.
}
// Optional source context sampled only by existing full snapshots. A failed
// diagnostic never changes Running, binding admission or recovery authority.
J observePostLogonSourceDiagnostic(const rc::Read& read,U64 base,U64 size,U64 user,bool qualified=true){
  try{
    const auto value=ymcc::steamOriginalSourceDiagnostic::observe(read,base,size,user,qualified);
    J out={{"source","cuser-postlogon-owner-progress-diagnostic"},{"profileVersion","steamclient64-e9045710-postlogon-source-v1"},
        {"available",value.available},{"reason",ymcc::steamOriginalSourceDiagnostic::reasonName(value.reason)},
        {"ownerMatched",value.ownerMatched},{"getterBytesVerified",value.getterVerified},
        {"readCalls",value.readCalls},{"requestedReadBytes",value.requestedBytes},
        {"userStateRaw",nullptr},{"postLogonProgressRaw",nullptr},{"logonWaitingRaw",nullptr},
        {"participatesInRecoveryAdmission",false},{"atomicSnapshot",false},{"abaExcluded",false},
        {"actualTaskExecutionObserved",false},{"callbackPayloadObserved",false},{"docExecutionObserved",false},
        {"initialRunningLossWriterObserved",false},{"fullLoginHeapGenerationLeaseProven",false}};
    if(value.available){out["userStateRaw"]=value.userStateRaw;out["postLogonProgressRaw"]=value.postLogonProgressRaw;
      out["logonWaitingRaw"]=value.logonWaitingRaw;}
    return out;
  }catch(...){return nullptr;}
}
// Known-version source fields, sampled on the existing snapshot worker only.
// The helper reads fixed verified locations and never grants recovery authority.
J observeSemiStaticSource(const rc::Read& read,U64 clientBase,U64 clientSize,
                         U64 uiBase,U64 uiSize,U64 user,bool qualified=true){
  try{
    const auto value=ymcc::steamOriginalSemiStaticSource::observe(
        read,clientBase,clientSize,uiBase,uiSize,user,qualified);
    J out={{"source","steam-semi-static-source"},{"profileVersion","client-e9045710-ui-1b945794-source-v1"},
        {"available",value.available},{"reason",ymcc::steamOriginalSemiStaticSource::reasonName(value.reason)},
        {"userMapMatched",value.userMapMatched},{"userInterfaceMatched",value.userInterfaceMatched},
        {"uiPipeMatched",value.uiPipeMatched},{"readCalls",value.readCalls},{"requestedReadBytes",value.requestedBytes},
        {"cmStateRaw",nullptr},{"cmConnectionPresent",nullptr},{"reconnectFlagRaw",nullptr},{"uiLocalPipe",nullptr},
        {"participatesInRecoveryAdmission",false},{"sampledByExistingSnapshotsOnly",true},
        {"privateMethodCalls",0},{"callbackQueueConsumed",false},{"atomicSnapshot",false},{"abaExcluded",false},
        {"fullLoginHeapGenerationLeaseProven",false},{"actualReconnectExecuted",false},
        {"uninterruptedRunningProven",false}};
    if(value.available){out["cmStateRaw"]=value.cmStateRaw;out["cmConnectionPresent"]=value.cmConnectionPresent;
      out["reconnectFlagRaw"]=value.reconnectFlagRaw;out["uiLocalPipe"]=value.uiLocalPipe;}
    return out;
  }catch(...){return nullptr;}
}
struct Snapshot{U64 gameId=0;DWORD flags=0;U64 flagsAddress=0,self=0;rw::Module client,ui;rc::detail::Selection selected{};rc::detail::Header records{};rc::detail::Target target{};J nativeUi,record,executionProfile;
  std::string clientSha256=rc::kClientSha256,uiSha256=app::kUiSha256;
  rc::ProfileId clientProfile=rc::ProfileId::Legacy1791918634;
  app::ProfileId uiProfile=app::ProfileId::Legacy1791918634;
  bool legacyDynamic=false;U64 getterRva=0xcfdc0,publisherRva=0x4e27e0;J compatibility;compat::Qualification clientCompatibility,uiCompatibility;
};
Snapshot snapshot(Scope& s,U64 account){
  Snapshot result;const auto began=workerStamp();
  enum Cost : std::size_t {ModulePair,ClientDisk,UiDisk,ClientLoaded,UiLoaded,TypedUi,TypedRecord,UiFlags,RecordBinding,FullGuard,CostCount};
  std::array<WorkerCost,CostCount> costs{};U64 readCalls=0,readBytes=0;
  const auto owned=[&]{WorkerTimer timer(costs[FullGuard]);return s.owned();};
  need(owned(),"binding-before-native-read");result.gameId=s.gameId;
  {WorkerTimer timer(costs[ModulePair]);
    need(originalImagePair(s.roles[0].pid,(fp::path(s.roles[0].path).parent_path()/L"steamclient64.dll").wstring(),
        (fp::path(s.roles[0].path).parent_path()/L"SteamUI.dll").wstring(),result.client,result.ui),"profile-module");}
  result.legacyDynamic=legacyRoute();std::optional<LegacyFile> legacyClient,legacyUi;std::optional<CompatibleFiles> compatibility;
  if(result.legacyDynamic){
    {WorkerTimer timer(costs[ClientDisk]);legacyClient=legacyFile(result.client.path);}
    {WorkerTimer timer(costs[UiDisk]);legacyUi=legacyFile(result.ui.path);}
    const auto profile=qualifiedProfile(legacyClient->sha256,legacyUi->sha256);
    result.clientSha256=legacyClient->sha256;result.uiSha256=legacyUi->sha256;
    result.clientProfile=*rc::profileIdForSha256(result.clientSha256);result.uiProfile=*app::profileIdForSha256(result.uiSha256);
    result.getterRva=profile.getterRva;result.publisherRva=profile.publisherRva;
  }else{
  const auto qualify=[&](const rw::Module& module,compat::ModuleKind kind,Cost cost){
    WorkerTimer timer(costs[cost]);return compat::qualifyFile(module.path,kind);};
  auto qualifiedClient=qualify(result.client,compat::ModuleKind::Client,ClientDisk);
  auto qualifiedUi=qualify(result.ui,compat::ModuleKind::Ui,UiDisk);
  compatibility=compatibleFiles(std::move(qualifiedClient),std::move(qualifiedUi));
  result.clientSha256=compatibility->client.sha256;result.uiSha256=compatibility->ui.sha256;
  result.clientProfile=recordProfileForLayout(*compatibility->client.layout);
  result.uiProfile=uiProfileForLayout(*compatibility->ui.layout);
  result.getterRva=compatibility->native.getterRva;result.publisherRva=compatibility->native.publisherRva;
  result.clientCompatibility=compatibility->client;result.uiCompatibility=compatibility->ui;
  }
  H process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,s.roles[0].pid));
  const DWORD openError=process.good()?ERROR_SUCCESS:GetLastError();
  requireReadProcess(process.good(),openError,s.roles[0].pid,"native-read-process");
  const rc::Read read=[&](U64 a,void* p,std::size_t n){++readCalls;readBytes+=n;SIZE_T got=0;
    return ymcc::steamappstatewin::readableRegion(process.h,a,n)&&
        ReadProcessMemory(process.h,reinterpret_cast<void*>(a),p,n,&got)&&got==n;};
  const auto loaded=[&](const rw::Module& module){return compat::ReadRva([&,module](U64 rva,void* target,std::size_t bytes){
    return rva<module.size&&bytes<=module.size-rva&&read(module.base+rva,target,bytes);});};
  rc::Read uiRead=read,recordRead=read;U64 uiSize=result.ui.size,clientSize=result.client.size;
  if(result.legacyDynamic){
    compat::Evidence loadedClient,loadedUi;
    {WorkerTimer timer(costs[ClientLoaded]);loadedClient=legacyLoadedGuard(*legacyClient,compat::ModuleKind::Client,loaded(result.client),result.client.base,result.client.size);}
    {WorkerTimer timer(costs[UiLoaded]);loadedUi=legacyLoadedGuard(*legacyUi,compat::ModuleKind::Ui,loaded(result.ui),result.ui.base,result.ui.size);}
    result.compatibility={{"admission","known-hash-pair-and-original-loaded-guards"},{"wholeImageHashIsVersionGate",true},
        {"compatibilityContractUsed",false},{"rawTypedReads",true},{"client",compatibilityEvidence(loadedClient)},{"ui",compatibilityEvidence(loadedUi)}};
  }else{
  compat::Evidence loadedClient,loadedUi;compat::LoadedDiagnostic clientDiagnostic,uiDiagnostic;
  {WorkerTimer timer(costs[ClientLoaded]);loadedClient=compat::qualifyLoaded(compatibility->client,compat::ModuleKind::Client,loaded(result.client),result.client.base,result.client.size,{},
      [&](const compat::ImportSlot& slot,U64 target){return compat::resolvedImportTarget(process.h,slot,target,&clientDiagnostic);},&clientDiagnostic);}
  {WorkerTimer timer(costs[UiLoaded]);loadedUi=compat::qualifyLoaded(compatibility->ui,compat::ModuleKind::Ui,loaded(result.ui),result.ui.base,result.ui.size,{},
      [&](const compat::ImportSlot& slot,U64 target){return compat::resolvedImportTarget(process.h,slot,target,&uiDiagnostic);},&uiDiagnostic);}
  if(!loadedClient.matched||!loadedUi.matched)throw DiagnosticFailure("native-loaded-compatibility-contract",{
      {"stage","loaded-code-and-data-contract"},{"client",compatibilityEvidence(loadedClient,clientDiagnostic)},
      {"ui",compatibilityEvidence(loadedUi,uiDiagnostic)},{"clientSha256",result.clientSha256},{"uiSha256",result.uiSha256},
      {"wholeImageHashIsVersionGate",false}});
  result.compatibility={{"admission","verified-disk-and-loaded-code-data-contract"},{"wholeImageHashIsVersionGate",false},
      {"clientCacheHit",compatibility->client.cacheHit},{"uiCacheHit",compatibility->ui.cacheHit},
      {"clientDynamicallyLocated",compatibility->client.dynamicallyLocated},{"uiDynamicallyLocated",compatibility->ui.dynamicallyLocated},
      {"client",compatibilityEvidence(loadedClient)},{"ui",compatibilityEvidence(loadedUi)},
      {"diagnosticDigestSource","same-qualified-file-buffer"},
      {"diskWork",{{"client",{{"fileBytesRead",compatibility->client.fileBytesRead},{"digestBytesHashed",compatibility->client.digestBytesHashed}}},
        {"ui",{{"fileBytesRead",compatibility->ui.fileBytesRead},{"digestBytesHashed",compatibility->ui.digestBytesHashed}}}}}};
  uiRead=compat::adaptProcessRead(compatibility->ui,result.ui.base,result.ui.size,read);
  recordRead=compat::adaptProcessRead(compatibility->client,result.client.base,result.client.size,read);
    uiSize=compat::referenceImageSize(compatibility->ui,result.ui.size);clientSize=compat::referenceImageSize(compatibility->client,result.client.size);
  }
  app::Query aq{result.ui.base,uiSize,s.appid,true,result.uiProfile};
  rc::Query rq{result.client.base,clientSize,account,s.gameId,s.roles[1].pid,true,result.clientProfile};
  {WorkerTimer timer(costs[TypedUi]);result.nativeUi=app::observe(aq,uiRead,owned);}
  {WorkerTimer timer(costs[TypedRecord]);result.record=rc::observe(rq,recordRead,owned);}
  need(result.nativeUi.value("available",false)&&result.record.value("originalPrimaryMatched",false),"original-native-association-unavailable");
  {WorkerTimer timer(costs[UiFlags]);app::detail::Reader ar(uiRead);result.flags=app::detail::readFlags(ar,aq);
    need(!ar.fields.empty()&&(result.flags==4||result.flags==8196),"native-flags-not-qualified");result.flagsAddress=ar.fields.back().address;}
  {WorkerTimer timer(costs[RecordBinding]);rc::detail::Reader rr(recordRead);result.selected=rc::detail::selectUser(rr,rq);
    result.records=rc::detail::readHeader(rr,result.selected.user,result.clientProfile);const auto targets=rc::detail::readTargets(rr,rq,result.records);
    need(targets.size()==1&&targets[0].primary==s.roles[1].pid&&targets[0].foreground==s.roles[1].pid,"original-record-primary");result.target=targets[0];
    try{result.record["suspendStateDiagnostic"]=observeSuspendDiagnostic(read,
        rc::detail::offset(result.records.records,0x330ULL*result.target.index),s.gameId);}catch(...){}
    try{result.record["postLogonSourceDiagnostic"]=observePostLogonSourceDiagnostic(read,
        result.client.base,result.client.size,result.selected.user,result.clientSha256==rc::kClientSha256);}catch(...){}
    try{result.record["semiStaticSource"]=observeSemiStaticSource(read,result.client.base,
        result.client.size,result.ui.base,result.ui.size,result.selected.user,result.clientSha256==rc::kClientSha256&&result.uiSha256==app::kUiSha256);}catch(...){}
  }
  result.self=result.selected.user+0x1e70;need(owned(),"binding-after-native-read");
  try{WorkerCost total;total.add(began,workerStamp());const std::array<const char*,CostCount> names{
        "modulePair","clientDiskQualification","uiDiskQualification","clientLoadedQualification","uiLoadedQualification","typedUiObserve","typedRecordObserve","uiFlagsRead","recordBindingRead","fullGuard"};
    result.executionProfile={{"schema","ymcc.original-snapshot-cost.v2"},{"separateModuleFullFileHashes",result.legacyDynamic?2:0},{"total",workerCostJson(total)},{"readCalls",readCalls},{"requestedReadBytes",readBytes},
        {"fullGuardIsNestedInTypedReads",true},{"instrumentationOverheadIncludedInEnclosingPhases",true},
        {"workerScope","calling snapshot worker only; excludes QueryActor and Steam remote threads"}};
    for(std::size_t i=0;i<CostCount;++i)result.executionProfile["stages"][names[i]]=workerCostJson(costs[i]);
  }catch(...){}return result;
}
U64 nowFileTime(){FILETIME now{};GetSystemTimeAsFileTime(&now);return (U64(now.dwHighDateTime)<<32)|now.dwLowDateTime;}
U64 wakeDeadlineTick(U64 eventFileTime,U64 utc,U64 tick){
  constexpr U64 budget=ymcc::steamOriginal::WakeWatch::budgetMs*10000ULL;
  need(eventFileTime&&utc>=eventFileTime&&utc-eventFileTime<budget,"wake-deadline-unavailable");
  const auto remaining=(budget-(utc-eventFileTime))/10000ULL;
  need(remaining&&tick<=(std::numeric_limits<U64>::max)()-remaining,"wake-deadline-expired-or-overflow");
  return tick+remaining;
}
struct WakeProbeReader {
  Scope& scope;H process;U64 calls=0,bytes=0;
  explicit WakeProbeReader(Scope& value):scope(value),process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,value.roles[0].pid)){
    const DWORD openError=process.good()?ERROR_SUCCESS:GetLastError();
    requireReadProcess(process.good(),openError,scope.roles[0].pid,"wake-probe-process");
    need(scope.owned()&&rw::exactProcess(process.h,scope.roles[0].pid,scope.roles[0].born,scope.roles[0].path,scope.roles[0].session),"wake-probe-process");
  }
  bool owned()const{
    if(!observationCurrent())return false;
    HANDLE handles[4]{};for(std::size_t i=0;i<scope.roles.size();++i)handles[i]=scope.roles[i].handle.h;
    return WaitForMultipleObjects(4,handles,FALSE,0)==WAIT_TIMEOUT;
  }
  DWORD flags(const Snapshot& previous){
    need(owned(),"wake-probe-scope-changed");
    const app::Read read=[&](U64 address,void* target,std::size_t count){SIZE_T got=0;++calls;
      const bool ok=ReadProcessMemory(process.h,reinterpret_cast<void*>(address),target,count,&got)&&got==count;
      bytes+=got;return ok;};
    const auto compatibleRead=previous.legacyDynamic?read:compat::adaptProcessRead(previous.uiCompatibility,previous.ui.base,previous.ui.size,read);
    const auto uiSize=previous.legacyDynamic?previous.ui.size:compat::referenceImageSize(previous.uiCompatibility,previous.ui.size);
    app::Query query{previous.ui.base,uiSize,scope.appid,true,previous.uiProfile};app::detail::Reader reader(compatibleRead);
    app::detail::checkAnchors(reader,query);const auto value=app::detail::readFlags(reader,query);
    need(!reader.fields.empty()&&reader.fields.back().address==previous.flagsAddress&&
        (value==4||value==8196)&&owned(),"wake-probe-object-or-flags-changed");
    return value; // Diagnostic detection only; full hashes, rules, registry and record pins precede every mutation.
  }
};
U64 threadCpuTicks(){FILETIME create{},exit{},kernel{},user{};
  need(GetThreadTimes(GetCurrentThread(),&create,&exit,&kernel,&user),"wake-cpu-sample");
  return ((U64(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((U64(user.dwHighDateTime)<<32)|user.dwLowDateTime);
}

std::string pin(const std::string& nonce,std::initializer_list<U64> values){std::string bytes=nonce;for(const auto value:values)bytes.append(reinterpret_cast<const char*>(&value),sizeof value);return digest(bytes.data(),bytes.size());}
J pins(const Snapshot& s,U64 birth,U64 account,const std::string& nonce){need(nonce.size()==36&&s.flagsAddress>=0x20,"baseline-pin-input");const auto record=s.records.records+0x330ULL*s.target.index;return{{"owner",pin(nonce,{s.client.base,birth,s.selected.engine,s.selected.user,account})},{"record",pin(nonce,{birth,s.selected.user,record,s.gameId})},{"uiObject",pin(nonce,{birth,s.ui.base,s.flagsAddress-0x20,s.gameId})}};}
J life(const Scope& s){return{{"steam",s.identities.at("steam")},{"game",s.identities.at("game")},{"gameId",std::to_string(s.gameId)}};}
J referenceScope(const Scope& s){return {{"steam",s.identities.at("steam")},{"game",s.identities.at("game")},{"cef",s.identities.at("cef")},{"gameId",std::to_string(s.gameId)},{"appid",s.appid}};}
std::string scopeKey(const Scope& s,U64 account){return textHash(J{{"scope",referenceScope(s)},{"accountFingerprint",textHash(std::to_string(account))}}.dump());}
// One key derivation for read-only preflight and the durable reservation.
// Source discovery can fail cleanly without spending a publication right.
// Explicit operator audit for the one historical v1 bug that consumed a
// dispatch before the driver's loaded-module read-only gate. Nothing is erased
// or refunded. Old bytes stay authoritative unless every pinned no-write fact
// below matches; a new, non-resettable mutation key records the actual attempt.
std::optional<std::string> auditedPrepublicationSource(const Scope& s,const fp::path& data){
  try{
    if(fp::exists(data/L"DISABLED.json"))return {};
    const auto identity=life(s);const auto key=textHash(identity.dump());
    const auto path=data/L"audited-prepublication-transfer"/utf16(key+".json");if(!fp::exists(path))return {};
    const auto audit=readJson(path);
    need(audit.at("schema")=="ymcc.steam-original-audited-prepublication-transfer.v1"&&
        audit.at("authorizedBy")=="explicit-operator-no-write-audit-20261023"&&
        audit.at("targetRoute")=="legacy-dynamic"&&audit.at("life")==identity&&
        audit.at("sourceEngineRevision")=="dual-route-original-recovery-20261023-r7"&&
        audit.at("sourceNativeSha256")=="66d147da3511e33c0c349244cd81c158d7547489ce1a082865a8711cbe5e260e",
        "audit-transfer-contract");
    const auto eventId=audit.at("sourceEventId").get<std::string>();need(!eventId.empty(),"audit-source-event");
    const auto operation=textHash(J{{"life",identity},{"operation","startup-mutation-once"}}.dump());
    const auto sourceEvent=textHash(J{{"life",identity},{"eventId",eventId}}.dump());
    const auto verify=[&](const fp::path& file,const J& expected){need(expected.is_string()&&
        expected.get<std::string>().size()==64&&fileHash(file.wstring())==expected.get<std::string>(),"audit-source-bytes-changed");};
    const auto disabled=data/L"disabled"/utf16(key+".json");verify(disabled,audit.at("disabledSha256"));
    const auto blocked=readJson(disabled);need(blocked.at("consumed")==true&&blocked.at("noAutomaticRetry")==true&&
        blocked.at("reason")=="recovery-cleanup-not-confirmed","audit-disabled-reason");
    for(const auto& pair:std::array<std::pair<std::string,const char*>,2>{{{operation,"operationSha256"},{sourceEvent,"eventSha256"}}}){
      const auto file=data/L"consumed"/utf16(pair.first+".json");verify(file,audit.at(pair.second));const auto old=readJson(file);
      need(old.at("schema")=="ymcc.steam-original-recovery-consumption.v1"&&old.at("life")==identity&&
          old.at("refunded")==false&&old.at("trigger").at("kind")=="startup"&&
          old.at("trigger").at("id")==eventId,"audit-source-ledger");
    }
    const auto proofFile=data/L"audited-prepublication-transfer"/utf16(key+".source.json");verify(proofFile,audit.at("sourceSummarySha256"));
    const auto proof=readJson(proofFile);const auto& driver=proof.at("driverResult");
    need(proof.at("engineRevision")==audit.at("sourceEngineRevision")&&proof.at("lifeKey")==key&&
        proof.at("trigger").at("kind")=="startup"&&proof.at("trigger").at("id")==eventId&&
        proof.at("status")=="refused"&&proof.at("consumed")==true&&
        proof.at("reason")=="recovery-cleanup-not-confirmed"&&proof.at("recoveryRoute")=="semi-static"&&
        proof.at("before").at("gameId")==std::to_string(s.gameId)&&
        proof.at("before").at("gamePid")==s.roles[1].pid&&proof.at("before").at("originalPrimaryMatched")==true&&
        driver.at("status")=="failed"&&driver.at("reason")=="loaded-client-ui-compatibility-contract"&&
        driver.at("openProcessAttempted")==true&&driver.at("writes")==J::array()&&
        driver.at("sourceEntries")==J::array()&&driver.at("publisherCalls")==0&&
        driver.at("entryEverExposed")==false&&driver.at("armAttempted")==false&&
        driver.at("controlOutcomeUnknown")==false&&driver.at("managementCallUnresolved")==false&&
        driver.at("trialDllMayRemainLoaded")==false&&driver.at("entryMayRemainPatched")==false&&
        driver.at("pathMemoryReleased")==true&&driver.at("packetMemoryReleased")==true,
        "audit-source-not-proved-readonly");
    return textHash(J{{"life",identity},{"operation","audited-legacy-startup-mutation-once"}}.dump());
  }catch(...){return {};}
}
std::optional<std::string> auditedLegacyTransfer(const J& event,const Scope& s,const fp::path& data){
  if(event.value("recoveryRoute",std::string{})!="legacy-dynamic"||event.at("trigger").at("kind")!="startup")return {};
  return auditedPrepublicationSource(s,data);
}
void consume(const J& event,const Scope& s,const fp::path& data,bool reserve=true){
  const auto& trigger=event.at("trigger");const auto kind=trigger.at("kind").get<std::string>();if(kind!="wake"&&kind!="startup")return;
  need(trigger.contains("generation")&&trigger.contains("id"),"event-ticket-required");const auto identity=life(s);
  const auto eventId=trigger.at("id").get<std::string>();need(!eventId.empty()&&eventId.size()<160,"event-id");
  const auto generation=trigger.at("generation");need(generation.is_number_integer()||generation.is_string(),"event-generation");
  if(generation.is_number_integer()&&!generation.is_number_unsigned())need(generation.get<std::int64_t>()>=0,"event-generation-negative");
  if(generation.is_string()){const auto token=generation.get<std::string>();need(!token.empty()&&token.size()<=20&&token.find_first_not_of("0123456789")==std::string::npos,"event-generation-decimal");}
  std::string episode;if(kind=="wake"){need(trigger.contains("powerEventFileTime"),"confirmed-wake-filetime-required");episode=std::to_string(decimal(trigger.at("powerEventFileTime")));}
  const auto base=data/L"consumed";const J record={{"schema","ymcc.steam-original-recovery-consumption.v2"},{"life",identity},
      {"engineRevision",kEngineRevision},{"recoveryRoute",event.value("recoveryRoute",std::string("semi-static"))},
      {"mutationBoundary","after-driver-readonly-qualification-before-first-remote-allocation"},
      {"trigger",{{"kind",kind},{"generation",generation},{"id",eventId},{"powerEventFileTime",episode}}},{"refunded",false}};
  const auto eventKey=textHash(J{{"life",identity},{"eventId",eventId}}.dump());
  const auto operationKey=kind=="wake"?textHash(J{{"life",identity},{"operation","wake"},{"powerEventFileTime",episode}}.dump()):
      textHash(J{{"life",identity},{"operation","startup-mutation-once"}}.dump());
  const std::array<fp::path,2> keys{base/utf16(operationKey+".json"),base/utf16(eventKey+".json")};
  if(fp::exists(keys[0])){
    const auto transfer=auditedLegacyTransfer(event,s,data);
    need(bool(transfer),"event-already-consumed");
    const auto transferKey=base/utf16(*transfer+".json");
    need(!fp::exists(transferKey)&&!fp::exists(keys[1]),"event-already-consumed");
    if(reserve){auto transferred=record;transferred["auditTransfer"]=true;
      transferred["oldKeysPreserved"]=true;transferred["refunded"]=false;
      durableCreate(transferKey,transferred.dump(2));durableCreate(keys[1],transferred.dump(2));}
    return;
  }
  for(const auto& key:keys)need(!fp::exists(key),"event-already-consumed");
  if(reserve)for(const auto& key:keys)durableCreate(key,record.dump(2));
}
// QueryActor already publishes completion with this manual-reset event.
// This wait is execution-only and grants no Current or recovery authority.
void waitQueryReturn(const std::atomic<bool>& completed,HANDLE event,DWORD budgetMs=5000){
  if(!completed.load())need(WaitForSingleObject(event,budgetMs)==WAIT_OBJECT_0&&completed.load(),"context-query-return-budget");
}
class QueryActor {
  H ready{CreateEventW(nullptr,TRUE,FALSE,nullptr)},command{CreateEventW(nullptr,FALSE,FALSE,nullptr)},doneEvent{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
  std::thread thread;std::atomic<bool> quit{false},readyOk{false},completedFlag{false};std::atomic<U64> result{0};std::mutex errorMutex;std::string error;U64 expected=0;std::wstring clientPath;Current currentFn;const RecoveryRoute queryRoute;const std::string qualifiedClientSha256;
  void run(){ISteamClient* client=nullptr;HSteamPipe pipe=0;HSteamUser user=0;try{
    // Capture route/hash on the caller before starting this new OS thread.
    // gRecoveryRoute is TLS and must never choose this worker's qualification.
    if(queryRoute==RecoveryRoute::LegacyDynamic){
      need(bool(rc::profileIdForSha256(qualifiedClientSha256)),"normal-query-legacy-client-profile");
      need(fileHash(clientPath)==qualifiedClientSha256,"normal-query-legacy-client-changed");
    }else{
      need(queryRoute==RecoveryRoute::SemiStatic,"normal-query-route-unsupported");
      need(bool(compat::qualifyFile(clientPath,compat::ModuleKind::Client).layout),"normal-query-client-compatibility");
    }
    HMODULE module=LoadLibraryExW(clientPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);need(module!=nullptr,"normal-query-load-client");using Factory=void*(__cdecl*)(const char*,int*);auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"CreateInterface"));need(factory!=nullptr,"normal-query-factory");need(!currentFn||currentFn(),"normal-query-current-revoked");client=static_cast<ISteamClient*>(factory(STEAMCLIENT_INTERFACE_VERSION,nullptr));need(client!=nullptr,"normal-query-client");need(!currentFn||currentFn(),"normal-query-current-revoked");pipe=client->CreateSteamPipe();need(pipe!=0,"normal-query-owned-pipe");need(!currentFn||currentFn(),"normal-query-current-revoked");user=client->ConnectToGlobalUser(pipe);need(user!=0,"normal-query-global-user");auto* api=client->GetISteamUser(user,pipe,STEAMUSER_INTERFACE_VERSION);need(api!=nullptr,"normal-query-user-interface");readyOk=true;SetEvent(ready.h);for(;;){need(WaitForSingleObject(command.h,INFINITE)==WAIT_OBJECT_0,"normal-query-command");if(quit)break;need(!currentFn||currentFn(),"normal-query-current-revoked");result=api->GetSteamID().ConvertToUint64();need(result==expected,"normal-query-account-mismatch");completedFlag=true;SetEvent(doneEvent.h);}}catch(const std::exception& e){{std::lock_guard<std::mutex> lock(errorMutex);error=e.what();}SetEvent(ready.h);completedFlag=true;SetEvent(doneEvent.h);}catch(...){{std::lock_guard<std::mutex> lock(errorMutex);error="normal-query-unknown-exception";}SetEvent(ready.h);completedFlag=true;SetEvent(doneEvent.h);}if(client&&pipe){if(user)client->ReleaseUser(pipe,user);client->BReleaseSteamPipe(pipe);}}
 public:
  explicit QueryActor(U64 account,std::wstring path,RecoveryRoute route=RecoveryRoute::SemiStatic,std::string clientSha256={}):
      expected(account),clientPath(std::move(path)),currentFn(gCurrent),queryRoute(route),qualifiedClientSha256(std::move(clientSha256)){requireCurrent();need(ready.good()&&command.good()&&doneEvent.good(),"normal-query-events");thread=std::thread([this]{run();});}
  ~QueryActor(){quit=true;SetEvent(command.h);if(thread.joinable())thread.join();}
  void waitReady(){const auto waited=WaitForSingleObject(ready.h,5000);
    {std::lock_guard<std::mutex> lock(errorMutex);need(error.empty(),error.empty()?"normal-query":error.c_str());}
    need(waited==WAIT_OBJECT_0&&readyOk,"normal-query-readiness");}
  void fire(){requireCurrent();completedFlag=false;result=0;ResetEvent(doneEvent.h);need(SetEvent(command.h),"normal-query-dispatch");}
  bool completed()const{return completedFlag.load();}
  void waitComplete(){waitQueryReturn(completedFlag,doneEvent.h);check();}

  void check(){std::lock_guard<std::mutex> lock(errorMutex);need(error.empty(),error.empty()?"normal-query":error.c_str());need(result==expected,"normal-query-result");}
};
struct SavedThread{DWORD tid=0;H handle;CONTEXT old{};bool armed=false,restored=false;};
U64 sourceEntryAddress=0;constexpr U64 debugMask=0xffff00ffULL;
void arm(SavedThread& t,U64 address){requireCurrent();CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(t.handle.h,&c)&&c.Dr0==0&&c.Dr1==0&&c.Dr2==0&&c.Dr3==0&&(c.Dr7&0xff)==0,"existing-debug-register-refused");t.old=c;t.armed=true;c.Dr0=address;c.Dr1=sourceEntryAddress;c.Dr6&=~0xfULL;c.Dr7=(c.Dr7&~debugMask)|0xd0005ULL;need(SetThreadContext(t.handle.h,&c),"arm-debug-registers");CONTEXT v{};v.ContextFlags=CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(t.handle.h,&v)&&v.Dr0==c.Dr0&&v.Dr1==c.Dr1&&(v.Dr7&debugMask)==(c.Dr7&debugMask),"arm-debug-readback");}
void reviewedSupportRestoreStopped(std::vector<SavedThread>& threads){requireCleanupOwnership();for(auto& t:threads){if(!t.armed||t.restored)continue;if(WaitForSingleObject(t.handle.h,0)==WAIT_OBJECT_0){t.restored=true;continue;}CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(t.handle.h,&c),"restore-dr-get");c.Dr0=t.old.Dr0;c.Dr1=t.old.Dr1;c.Dr2=t.old.Dr2;c.Dr3=t.old.Dr3;c.Dr6=t.old.Dr6;c.Dr7=t.old.Dr7;need(SetThreadContext(t.handle.h,&c),"restore-dr-set");CONTEXT v{};v.ContextFlags=CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(t.handle.h,&v)&&v.Dr0==t.old.Dr0&&v.Dr1==t.old.Dr1&&v.Dr2==t.old.Dr2&&v.Dr3==t.old.Dr3&&(v.Dr7&debugMask)==(t.old.Dr7&debugMask),"restore-dr-readback");t.restored=true;}}
void closeEventFile(const DEBUG_EVENT& e){if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT&&e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT&&e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);}
void continuation(const DEBUG_EVENT& e,DWORD status=DBG_CONTINUE){requireTargetMutation();need(ContinueDebugEvent(e.dwProcessId,e.dwThreadId,status),"continue-owned-event");}
struct ImageLive{U64 base=0,size=0;std::string name;};
std::vector<ImageLive> imagesLive(DWORD pid){H snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));need(snap.good(),"image-census");MODULEENTRY32W e{};e.dwSize=sizeof e;need(Module32FirstW(snap.h,&e),"image-first");std::vector<ImageLive> rows;do{rows.push_back({reinterpret_cast<U64>(e.modBaseAddr),e.modBaseSize,utf8(e.szModule)});}while(Module32NextW(snap.h,&e));need(GetLastError()==ERROR_NO_MORE_FILES,"image-census-complete");return rows;}
J locationLive(U64 address,const std::vector<ImageLive>& images){for(const auto& m:images)if(address>=m.base&&address-m.base<m.size)return{{"module",m.name},{"rva",address-m.base}};return{{"module","unmapped-or-heap"}};}
void ownerHeld(HANDLE process,const std::string& nonce,J& out,bool pending){out["recoveryRoute"]=routeName();out["fallbackAllowed"]=false;out["status"]="unsupported-owner-held";out["pendingDebugEventUnresolved"]=pending;out["automaticContinuationSuppressed"]=true;out["dllUnloadAttempted"]=false;publish({{"event","unsupported-owner-held"},{"nonce",nonce},{"result",out}});while(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)Sleep(50);out["status"]="unsupported-target-exited";out["targetExitObserved"]=true;}
// Bind only the driver's qualification/read dependencies. Its publication,
// original-instance guards, cleanup and asynchronous result logic stay shared.
// The explicit namespace binding avoids duplicating the reviewed driver body.
namespace routeDriver {
namespace qualification {
using ::ymcc::steamcompat::Layout;using ::ymcc::steamcompat::ModuleKind;
using ::ymcc::steamcompat::ReadRva;using ::ymcc::steamcompat::executableImportTarget;
using ::ymcc::steamcompat::ImportSlot;using ::ymcc::steamcompat::LoadedDiagnostic;
using ::ymcc::steamcompat::NamedImportValidator;using ::ymcc::steamcompat::resolvedImportTarget;
struct Qualification {
  std::optional<Layout> layout;compat::Qualification content;std::optional<LegacyFile> fixed;
};
thread_local std::string clientHash,uiHash;
Qualification qualifyFile(const std::wstring& path,ModuleKind kind,bool cache){
  if(!legacyRoute()){auto content=compat::qualifyFile(path,kind,cache);return {content.layout,std::move(content),{}};}
  auto file=legacyFile(path);std::optional<Layout> layout;
  if(kind==ModuleKind::Client){const auto profile=rc::profileIdForSha256(file.sha256);need(bool(profile),"legacy-driver-client-profile");
    layout=*profile==rc::ProfileId::Public1792618285?Layout::Public:Layout::Legacy;clientHash=file.sha256;uiHash.clear();}
  else{const auto profile=app::profileIdForSha256(file.sha256);need(bool(profile),"legacy-driver-ui-profile");
    layout=*profile==app::ProfileId::Public1792618285?Layout::Public:Layout::Legacy;uiHash=file.sha256;}
  return {layout,{},std::move(file)};
}
compat::Evidence qualifyLoaded(const Qualification& value,ModuleKind kind,const ReadRva& read,U64 base,U64 size,
    const std::function<bool(U64)>& importTarget={},const compat::NamedImportValidator& namedImportTarget={},compat::LoadedDiagnostic* diagnostic=nullptr){
  if(legacyRoute()){
    if(diagnostic)*diagnostic={};need(bool(value.fixed),"legacy-driver-fixed-profile-missing");
    return legacyLoadedGuard(*value.fixed,kind,read,base,size);
  }
  return compat::qualifyLoaded(value.content,kind,read,base,size,importTarget,namedImportTarget,diagnostic);
}
U64 resolvedRva(const Qualification& value,U64 reference){
  if(legacyRoute()){need(bool(value.fixed),"legacy-driver-fixed-entry-missing");return reference;}
  return compat::resolvedRva(value.content,reference);
}
U64 referenceImageSize(const Qualification& value,U64 size){return legacyRoute()?size:compat::referenceImageSize(value.content,size);}
ReadRva adaptProcessRead(const Qualification& value,U64 base,U64 size,const ReadRva& raw){
  return legacyRoute()?raw:compat::adaptProcessRead(value.content,base,size,raw);
}
}
namespace trial {
using ::running_trial::QualificationRoute;using ::running_trial::validQualificationRoute;using ::running_trial::qualificationRouteName;
using ::running_trial::abi;using ::running_trial::AuxiliaryIdentity;using ::running_trial::Op;using ::running_trial::Packet;
using ::running_trial::shortcutAppId;using ::running_trial::steamModulePath;using ::running_trial::validSteamPath;
std::optional<::running_trial::NativeProfile> profileForCompatibility(compat::Layout client,compat::Layout ui){
  if(!legacyRoute())return ::running_trial::profileForCompatibility(client,ui);
  const auto profile=qualifiedProfile(qualification::clientHash,qualification::uiHash);
  need(recordProfileForLayout(client)==*rc::profileIdForSha256(qualification::clientHash)&&
      uiProfileForLayout(ui)==*app::profileIdForSha256(qualification::uiHash),"legacy-driver-known-pair-layout");return profile;
}
}
namespace ymcc {
namespace steamcompat=qualification;
namespace steamrecord=::ymcc::steamrecord;
namespace steamappstatewin=::ymcc::steamappstatewin;
}
namespace tr=trial;
#include "steam_original_recovery_engine/driver_port.inc"
}
using routeDriver::runTrial;using routeDriver::LifetimeMutex;using routeDriver::armChecked;
#ifdef YMCC_STEAM_ORIGINAL_ENGINE_TESTING
using routeDriver::remoteCall;using routeDriver::decimalInput;using routeDriver::dwordInput;using routeDriver::mutationDeadlineForRequest;
using routeDriver::requireMutationDeadline;using routeDriver::validateRequest;using routeDriver::bindExternalHealthyProof;
#endif
// This is a fact about this OS worker's completed prior capture, not a
// cached process/thread/account identity or a recovery ticket. TLS prevents
// another worker from inheriting a previous debugger connection's policy.
struct SourceContextInit {
  DWORD steamPid=0;U64 steamBirth=0;std::wstring steamPath;
  bool matches(const BoundLive& steam)const {
    return steamPid&&steamBirth&&steamPid==steam.pid&&steamBirth==steam.born&&steamPath==steam.path;
  }
};
thread_local SourceContextInit gSourceContextInit;
struct SourceThreadCandidate {DWORD tid=0;U64 birth=0;H handle;};
bool currentSourceThread(const SourceThreadCandidate& thread,DWORD steam) {
  if(!thread.tid||!thread.birth||!thread.handle.good()||WaitForSingleObject(thread.handle.h,0)!=WAIT_TIMEOUT||
      GetProcessIdOfThread(thread.handle.h)!=steam)return false;
  FILETIME born{},exit{},kernel{},user{};if(!GetThreadTimes(thread.handle.h,&born,&exit,&kernel,&user)||
      ((U64(born.dwHighDateTime)<<32)|born.dwLowDateTime)!=thread.birth)return false;
  PWSTR raw=nullptr;const auto hr=GetThreadDescription(thread.handle.h,&raw);
  const bool named=hr>=0&&raw&&std::wstring(raw)==L"IPC:CSteamEngine";if(raw)LocalFree(raw);return named;
}
std::optional<SourceThreadCandidate> findSourceThreadCandidate(DWORD steam,J& detail) {
  H census(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));THREADENTRY32 row{};row.dwSize=sizeof row;
  if(!census.good()||!Thread32First(census.h,&row)){detail["fallbackReason"]="source-thread-census-unavailable";return {};}
  std::optional<SourceThreadCandidate> candidate;unsigned count=0,matches=0;
  do{if(row.th32OwnerProcessID!=steam)continue;
    if(++count>4096){detail["fallbackReason"]="source-thread-census-limit";return {};}
    SourceThreadCandidate next;next.tid=row.th32ThreadID;next.handle=H(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,next.tid));
    if(!next.handle.good()||GetProcessIdOfThread(next.handle.h)!=steam||WaitForSingleObject(next.handle.h,0)!=WAIT_TIMEOUT){detail["fallbackReason"]="source-thread-query-incomplete";return {};}
    FILETIME born{},exit{},kernel{},user{};
    if(!GetThreadTimes(next.handle.h,&born,&exit,&kernel,&user)){detail["fallbackReason"]="source-thread-birth-unavailable";return {};}
    next.birth=(U64(born.dwHighDateTime)<<32)|born.dwLowDateTime;PWSTR raw=nullptr;const auto hr=GetThreadDescription(next.handle.h,&raw);
    if(hr<0||!raw||!next.birth){if(raw)LocalFree(raw);detail["fallbackReason"]="source-thread-description-unavailable";return {};}
    const bool named=std::wstring(raw)==L"IPC:CSteamEngine";LocalFree(raw);
    if(named){++matches;if(matches==1)candidate=std::move(next);}
  }while(Thread32Next(census.h,&row));
  const auto error=GetLastError();detail["queriedThreads"]=count;detail["matchingNames"]=matches;
  if(error!=ERROR_NO_MORE_FILES||!count||matches!=1){detail["fallbackReason"]="source-thread-census-incomplete-or-not-unique";return {};}
  if(!currentSourceThread(*candidate,steam)){detail["fallbackReason"]="source-thread-changed-after-census";return {};}
  return candidate;
}
J captureGetterContext(Scope& scope,const Snapshot& snap,U64 account,QueryActor& query){
  H process(OpenProcess(PROCESS_ALL_ACCESS,FALSE,scope.roles[0].pid));need(process.good(),"context-steam-process");
  BOOL debugger=TRUE;need(CheckRemoteDebuggerPresent(process.h,&debugger)&&!debugger,"context-existing-debugger-refused");
  const DWORD steam=scope.roles[0].pid;std::vector<SavedThread> threads;DEBUG_EVENT pending{};bool pendingNow=false,attached=false,started=false,cleaning=false,initialBreakpointSeen=false;DWORD pendingStatus=DBG_CONTINUE;J result;
  sourceEntryAddress=snap.client.base+snap.getterRva;const auto began=GetTickCount64();bool cancelling=false;U64 cancelBegan=0;
  const auto detach=[&]{enterCleanup();reviewedSupportRestoreStopped(threads);continuation(pending,pendingStatus);pendingNow=false;need(DebugActiveProcessStop(steam),"context-detach");attached=false;BOOL after=TRUE;need(CheckRemoteDebuggerPresent(process.h,&after)&&!after,"context-detach-readback");gCleanupMode=false;};
  try{
    requireCurrent();need(DebugActiveProcess(steam),"context-attach");attached=true;
    // Public Win32 API requires an existing debug object before this call.
    // The first attach -> FALSE interval is a documented remaining boundary.
    need(DebugSetProcessKillOnExit(FALSE),"context-debugger-kill-policy");
    for(;;){
      if(started&&!cancelling&&(!current()||GetTickCount64()-began>6000)){enterCleanup();need(DebugBreakProcess(process.h),"context-cancel-stop");cancelling=true;cancelBegan=GetTickCount64();}
      need(GetTickCount64()-began<12000&&(!cancelling||GetTickCount64()-cancelBegan<4000),"context-budget");
      if(!WaitForDebugEvent(&pending,25)){need(GetLastError()==ERROR_SEM_TIMEOUT,"context-wait");continue;}
      noteContextDebugEvent(pending.dwDebugEventCode);pendingNow=true;pendingStatus=pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;closeEventFile(pending);need(pending.dwProcessId==steam,"context-single-debuggee");
      if(cancelling){
        if(pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){const auto code=pending.u.Exception.ExceptionRecord.ExceptionCode;
          if(code==EXCEPTION_BREAKPOINT&&pending.u.Exception.dwFirstChance){pendingStatus=DBG_CONTINUE;cleaning=true;detach();throw std::runtime_error("context-cancelled-or-expired-cleaned");}
          if(code==EXCEPTION_SINGLE_STEP){const auto it=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.tid==pending.dwThreadId&&t.armed;});
            if(it!=threads.end()){CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS|CONTEXT_CONTROL;if(GetThreadContext(it->handle.h,&c)&&c.Dr0==snap.flagsAddress&&c.Dr1==sourceEntryAddress&&((c.Dr6&0xf)==1||((c.Dr6&0xf)==2&&c.Rip==sourceEntryAddress)))pendingStatus=DBG_CONTINUE;}
          }
        }
        reviewedSupportRestoreStopped(threads);continuation(pending,pendingStatus);pendingNow=false;continue;
      }
      if(pending.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT||pending.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT){SavedThread t;t.tid=pending.dwThreadId;t.handle=duplicate(pending.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT?pending.u.CreateProcessInfo.hThread:pending.u.CreateThread.hThread);threads.push_back(std::move(t));if(gExecutionProbe)++gExecutionProbe->armAttempts;armChecked(threads.back(),snap.flagsAddress);}
      if(pending.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT)for(auto& t:threads)if(t.tid==pending.dwThreadId)t.restored=true;
      need(pending.dwDebugEventCode!=EXIT_PROCESS_DEBUG_EVENT,"context-steam-ended");
      if(pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){const auto& ex=pending.u.Exception;const auto code=ex.ExceptionRecord.ExceptionCode;
        if(code==EXCEPTION_BREAKPOINT&&ex.dwFirstChance&&!started){
          initialBreakpointSeen=true;
          need(scope.owned(),"context-ownership-at-stop");H census(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));THREADENTRY32 row{};row.dwSize=sizeof row;need(census.good()&&Thread32First(census.h,&row),"context-thread-census");unsigned count=0;do{if(row.th32OwnerProcessID==steam){++count;if(gExecutionProbe)++gExecutionProbe->censusThreads;need(std::any_of(threads.begin(),threads.end(),[&](const auto& t){return t.tid==row.th32ThreadID&&t.armed&&!t.restored;}),"context-all-threads-covered");}}while(Thread32Next(census.h,&row));need(GetLastError()==ERROR_NO_MORE_FILES&&count>0,"context-thread-census-complete");started=true;pendingStatus=DBG_CONTINUE;query.fire();
        }else if(code==EXCEPTION_SINGLE_STEP&&started){
          const auto it=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.tid==pending.dwThreadId&&t.armed&&!t.restored;});need(it!=threads.end(),"context-watch-thread");CONTEXT c{};c.ContextFlags=CONTEXT_FULL|CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(it->handle.h,&c)&&c.Dr0==snap.flagsAddress&&c.Dr1==sourceEntryAddress,"context-owned-debug-registers");const auto causes=c.Dr6&0xf;
          if(causes==2){if(gExecutionProbe)++gExecutionProbe->getterEvents;need(c.Rip==sourceEntryAddress,"context-getter-entry");if(c.Rcx==snap.self){if(gExecutionProbe)++gExecutionProbe->matchingGetterEvents;U64 observed=0;need(c.Rcx>=0x1c66,"context-self-bound");readExact(process.h,c.Rcx-0x1c66,&observed,sizeof observed);need(observed==account,"context-account");FILETIME b{},e{},k{},u{};need(GetThreadTimes(it->handle.h,&b,&e,&k,&u),"context-thread-birth");PWSTR name=nullptr;const auto hr=GetThreadDescription(it->handle.h,&name);const bool named=hr>=0&&name&&std::wstring(name)==L"IPC:CSteamEngine";if(name)LocalFree(name);need(named,"context-ipc-thread-name");result={{"tid",pending.dwThreadId},{"threadBirthFileTime",std::to_string((U64(b.dwHighDateTime)<<32)|b.dwLowDateTime)},{"threadDescription","IPC:CSteamEngine"},{"expectedAccountMatched",true},{"originalInterfaceOwnerMatched",true}};pendingStatus=DBG_CONTINUE;cleaning=true;detach();break;}c.EFlags|=0x10000u;
          }else {need(causes==1,"context-owned-watch-event");if(gExecutionProbe)++gExecutionProbe->flagEvents;}
          c.Dr6&=~0xfULL;requireCurrent();need(SetThreadContext(it->handle.h,&c),"context-resume-owned-instruction");pendingStatus=DBG_CONTINUE;
        }else if(threadname_policy::classify(threadname_policy::Architecture::x64,code,ex.dwFirstChance,ex.ExceptionRecord.ExceptionFlags,ex.ExceptionRecord.NumberParameters,ex.ExceptionRecord.ExceptionInformation).recognized())pendingStatus=DBG_EXCEPTION_NOT_HANDLED;
        else throw std::runtime_error("context-unexpected-exception");
      }
      continuation(pending,pendingStatus);pendingNow=false;
    }
    query.waitComplete();requireCurrent();return result;
  }catch(const std::exception& e){
    if(attached){J held={{"reason",e.what()},{"phase","normal-getter-context"}};
      if(pendingNow&&!cleaning){try{cleaning=true;enterCleanup();if(pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT&&pending.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT&&pending.u.Exception.dwFirstChance)pendingStatus=DBG_CONTINUE;
          if(pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT&&pending.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP){const auto it=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.tid==pending.dwThreadId&&t.armed;});if(it!=threads.end()){CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS|CONTEXT_CONTROL;if(GetThreadContext(it->handle.h,&c)&&c.Dr0==snap.flagsAddress&&c.Dr1==sourceEntryAddress&&((c.Dr6&0xf)==1||((c.Dr6&0xf)==2&&c.Rip==sourceEntryAddress)))pendingStatus=DBG_CONTINUE;}}
          if(!initialBreakpointSeen){
            reviewedSupportRestoreStopped(threads);continuation(pending,pendingStatus);pendingNow=false;const auto waitBegan=GetTickCount64();
            while(!initialBreakpointSeen){need(GetTickCount64()-waitBegan<4000,"context-initial-break-cleanup-budget");
              if(!WaitForDebugEvent(&pending,25)){need(GetLastError()==ERROR_SEM_TIMEOUT,"context-initial-break-cleanup-wait");continue;}
              pendingNow=true;closeEventFile(pending);need(pending.dwProcessId==steam,"context-cleanup-single-debuggee");pendingStatus=pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;
              initialBreakpointSeen=pending.dwDebugEventCode==EXCEPTION_DEBUG_EVENT&&pending.u.Exception.dwFirstChance&&pending.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT;
              if(initialBreakpointSeen){pendingStatus=DBG_CONTINUE;break;}continuation(pending,pendingStatus);pendingNow=false;
            }
          }
          detach();}catch(...){ownerHeld(process.h,"",held,pendingNow);}}
      else ownerHeld(process.h,"",held,pendingNow);
    }
    gCleanupMode=false;throw;
  }
}
J sourceGetterContext(Scope& scope,const Snapshot& snap,U64 account,QueryActor& query,J& summary){
  J& detail=summary["sourceContextSelection"];detail={{"mode","full-capture"},{"candidateGrantsPublication",false},
    {"witnessScope","same OS worker and original Steam PID/birth/path only"},{"preAttachPolicyVerified",false}};
  const auto& steam=scope.roles[0];
  detail["recoveryRoute"]=routeName();
  if(legacyRoute()){
    detail["mode"]="legacy-fresh-capture";detail["warmCandidateAllowed"]=false;summary["source"]="legacy-fresh-capture";
    gSourceContextInit={};const auto context=captureGetterContext(scope,snap,account,query);
    requireCurrent();detail["freshCaptureCompleted"]=true;return context;
  }
  if(gSourceContextInit.matches(steam)){
    need(scope.owned(),"warm-source-scope-not-current");
    // This existing policy must succeed before the formal driver attaches.
    // Failure selects the original capture; it never becomes a bypass flag.
    if(DebugSetProcessKillOnExit(FALSE)){
      detail["preAttachPolicyVerified"]=true;summary["firstAttachExitPolicyPreinitialized"]=true;
      auto candidate=findSourceThreadCandidate(steam.pid,detail);
      need(scope.owned(),"warm-source-scope-changed-after-census");
      if(candidate){
        // Keep the original pre-injection normal account-result barrier.
        query.fire();query.waitComplete();need(scope.owned(),"warm-source-scope-changed-after-query");
        if(currentSourceThread(*candidate,steam.pid)){
          detail["mode"]="same-worker-initialized-thread-candidate";summary["source"]="same-worker-initialized-thread-candidate";
          detail["priorCaptureObservedOwnerIsNotReused"]=true;detail["normalAccountRoundtripCompleted"]=true;
          return {{"tid",candidate->tid},{"threadBirthFileTime",std::to_string(candidate->birth)},
            {"threadDescription","IPC:CSteamEngine"},{"provenance","fresh read-only candidate; actual hook validates owner/account before publisher"},
            {"expectedAccountMatched",false},{"originalInterfaceOwnerMatched",false}};
        }
        detail["fallbackReason"]="source-thread-changed-after-query";
      }
    }else detail["fallbackReason"]="same-worker-debug-policy-verification-failed";
  }else detail["fallbackReason"]="same-worker-prior-capture-witness-unavailable";
  // Only a complete original capture can establish this worker's witness.
  // No event field, config, fixture receipt or source-thread name can set it.
  summary["source"]="full-capture";gSourceContextInit={};const auto context=captureGetterContext(scope,snap,account,query);
  requireCurrent();gSourceContextInit={steam.pid,steam.born,steam.path};return context;
}
J importReference(const J& seed,const Scope& s,U64 account){
  const auto& b=seed.contains("baseline")?seed.at("baseline"):seed;
  need(b.value("healthyOriginalObserved",false)&&b.value("sameCohort",false)&&b.value("sameAccountAtEnd",false)&&b.value("currentGameProcessCount",0)==1,"seed-not-healthy-original");
  need(b.at("errors").is_array()&&b.at("errors").empty(),"seed-capture-errors");
  need(b.at("accountFingerprint")==textHash(std::to_string(account)),"seed-account-mismatch");
  for(const auto* end:{"before","after"})for(const auto* role:{"steam","game","cef"}){const auto& p=b.at(end).at("roles").at(role);need(p.value("alive",false)&&p.value("available",false)&&normalizedIdentity(p)==s.identities.at(role),"seed-cohort-mismatch");}
  need(b.at("target").at("gameid")==std::to_string(s.gameId)&&b.at("target").at("appid")==s.appid,"seed-target");
  const auto& n=b.at("native");need(n.at("nativeUi").at("nativeAppFlags")==8196&&n.at("nativeUi").at("runningBitSet")==true&&n.at("record").at("originalPrimaryMatched")==true&&n.at("record").at("primaryLivenessPid")==s.roles[1].pid,"seed-native-health");
  const auto clientSha=n.at("record").at("diskSha256").get<std::string>();
  const auto uiSha=n.at("nativeUi").at("diskSha256").get<std::string>();
  const auto digestShape=[](const std::string& value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;};
  need(digestShape(clientSha)&&digestShape(uiSha),"seed-profile-shape"); // matchReference binds these actual hashes to the qualified current snapshot.
  const auto& binding=b.at("binding");need(binding.at("status")=="observed"&&binding.at("mode")=="read-only"&&binding.at("sameCohortAfter")==true&&binding.at("samples").size()>=3,"seed-binding-capture");
  const auto nonce=b.at("bindingNonce").get<std::string>();need(nonce.size()==36&&binding.at("nonce")==nonce,"seed-pin-nonce");const auto p=binding.at("samples").at(0).at("pins");
  for(const auto& sample:binding.at("samples")){need(sample.at("flags")==8196&&sample.at("running")==true&&sample.at("primary")==s.roles[1].pid&&sample.at("foregroundComparisonPid")==s.roles[1].pid&&sample.at("sameFingerprintsAsFirst")==true&&sample.at("pins")==p,"seed-pins-not-healthy-immutable");}
  return{{"schema","ymcc.steam-native-original-reference.v1"},{"scope",referenceScope(s)},{"captureParent",s.identities.at("ymcc")},{"accountFingerprint",textHash(std::to_string(account))},{"bindingNonce",nonce},{"pins",p},{"healthyFlags",8196},{"compatibilityContract","original-running-code-data-v2"},{"clientSha256",clientSha},{"uiSha256",uiSha},{"importedSeedSha256",textHash(b.dump())}};
}
void matchReference(const J& ref,const Scope& scope,const Snapshot& sample,U64 account){
  need(ref.at("schema")=="ymcc.steam-native-original-reference.v1"&&ref.at("healthyFlags")==8196&&
      ref.at("scope")==referenceScope(scope),"healthy-reference-scope");
  // The current snapshot independently qualified disk+loaded contracts. File
  // digests remain diagnostic history; the same original instance is bound by
  // its Steam/game/CEF birth, complete GameID, account and exact heap pins.
  const auto priorClient=ref.at("clientSha256").get<std::string>(),priorUi=ref.at("uiSha256").get<std::string>();
  const auto digestShape=[](const std::string& value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;};
  const bool modern=ref.value("compatibilityContract",std::string())=="original-running-code-data-v2";
  const bool historical=bool(tr::profileForHashes(priorClient,priorUi));
  need(digestShape(priorClient)&&digestShape(priorUi)&&(modern||historical)&&
      ref.at("accountFingerprint")==textHash(std::to_string(account)),"healthy-reference-profile-or-account");
  need(ref.at("pins")==pins(sample,scope.roles[0].born,account,ref.at("bindingNonce").get<std::string>()),"saved-healthy-pins-changed");
}

// Legacy references predate full GameID/appid in referenceScope. Only this
// exact prior key is eligible; a present current reference always takes priority.
J legacyReferenceScope(const Scope& scope){return {{"steam",scope.identities.at("steam")},
    {"game",scope.identities.at("game")},{"cef",scope.identities.at("cef")}};}
std::string legacyReferenceKey(const Scope& scope,U64 account){return textHash(J{
    {"scope",legacyReferenceScope(scope)},{"accountFingerprint",textHash(std::to_string(account))}}.dump());}
std::string readLegacyReferenceBytes(const fp::path& file){
  H input(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT,nullptr));need(input.good(),"legacy-reference-open");
  BY_HANDLE_FILE_INFORMATION info{};
  need(GetFileInformationByHandle(input.h,&info)&&
      !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
      info.nNumberOfLinks==1&&info.nFileSizeHigh==0&&info.nFileSizeLow>0&&
      info.nFileSizeLow<=2u*1024*1024,"legacy-reference-file-refused");
  std::string bytes(static_cast<std::size_t>(info.nFileSizeLow),'\0');DWORD got=0,extra=0;char tail=0;
  need(ReadFile(input.h,bytes.data(),info.nFileSizeLow,&got,nullptr)&&got==info.nFileSizeLow&&
      ReadFile(input.h,&tail,1,&extra,nullptr)&&extra==0,"legacy-reference-read");return bytes;
}
bool referenceNonceValid(const std::string& nonce){
  if(nonce.size()!=36)return false;
  for(std::size_t i=0;i<nonce.size();++i){const char c=nonce[i];
    if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
    else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
  }return true;
}
J loadCompatibleReference(const fp::path& data,const Scope& scope,const Snapshot& sample,U64 account){
  const auto referencePath=data/L"references"/utf16(scopeKey(scope,account)+".json");
  if(fp::exists(referencePath)){auto reference=readJson(referencePath);
    matchReference(reference,scope,sample,account);return reference;}
  const auto legacyKey=legacyReferenceKey(scope,account);
  const auto legacyPath=data/L"references"/utf16(legacyKey+".json");
  if(!fp::exists(legacyPath))return {};
  requireCurrent();const auto bytes=readLegacyReferenceBytes(legacyPath);auto text=bytes;
  if(text.starts_with("\xef\xbb\xbf"))text.erase(0,3);
  const auto legacy=J::parse(text);
  need(legacy.at("schema")=="ymcc.steam-native-original-reference.v1"&&
      legacy.at("healthyFlags").is_number_integer()&&legacy.at("healthyFlags")==8196&&
      legacy.at("scope")==legacyReferenceScope(scope),"legacy-healthy-reference-scope");
  need(legacy.at("clientSha256")==rc::kClientSha256&&legacy.at("uiSha256")==app::kUiSha256&&
      legacy.at("accountFingerprint")==textHash(std::to_string(account)),"legacy-healthy-reference-profile-or-account");
  const auto target=targetIdentity(J{{"gameId",std::to_string(scope.gameId)},{"appid",scope.appid}});
  need(target.first==sample.gameId&&(sample.flags==4||sample.flags==8196)&&
      sample.target.primary==scope.roles[1].pid&&sample.target.foreground==scope.roles[1].pid,
      "legacy-reference-current-original-target");
  const auto nonce=legacy.at("bindingNonce").get<std::string>();
  need(referenceNonceValid(nonce),"legacy-reference-nonce");
  need(legacy.at("pins")==pins(sample,scope.roles[0].born,account,nonce),"legacy-saved-healthy-pins-changed");
  // Preserve the actual healthy sample, nonce, pins and capture parent. The
  // current snapshot may be lost; it never becomes a synthetic healthy sample.
  auto reference=legacy;reference["scope"]=referenceScope(scope);
  reference["compatibilityContract"]="original-running-code-data-v2";
  reference["importedLegacyReferenceSha256"]=textHash(bytes);
  reference["importedLegacyReferenceKey"]=legacyKey;
  matchReference(reference,scope,sample,account);requireCurrent();
  durableCreate(referencePath,reference.dump(2));return reference;
}
J nativeState(const Scope& scope,const Snapshot& sample){return{{"flags",sample.flags},{"running",sample.flags==8196},{"originalPrimaryMatched",true},{"gamePid",scope.roles[1].pid},{"steamPid",scope.roles[0].pid},{"scope",scope.identities},{"profile",{{"clientSha256",sample.clientSha256},{"uiSha256",sample.uiSha256},{"recordProfileVersion",rc::profileVersion(sample.clientProfile)},{"uiProfileVersion",app::profileVersion(sample.uiProfile)},{"wholeImageHashIsVersionGate",sample.legacyDynamic}}},{"compatibility",sample.compatibility},{"gameDiscovery",false},{"gameId",std::to_string(scope.gameId)},{"appid",scope.appid},{"uiVerified",false},{"inputVerified",false},{"loginOrHeapLeaseProven",false},{"suspendStateDiagnostic",sample.record.value("suspendStateDiagnostic",J())},{"postLogonSourceDiagnostic",sample.record.value("postLogonSourceDiagnostic",J())},{"semiStaticSource",sample.record.value("semiStaticSource",J())}};}
}
Json current_status(){std::lock_guard<std::mutex> lock(gStateMutex);auto value=gState;value["active"]=gActive.load();value["engineRevision"]=kEngineRevision;const auto& result=value.contains("result")&&value.at("result").is_object()?value.at("result"):value;value["moduleQualification"]=result.value("moduleQualification",std::string(kModuleQualification));value["wholeImageHashIsVersionGate"]=result.value("wholeImageHashIsVersionGate",false);value["recoveryRoute"]=result.value("recoveryRoute",std::string("semi-static"));return value;}
bool active() noexcept{return gActive.load();}
Json handle(const Json& event,Emit emit,Current guard,Current observationGuard,WakeSignal wakeSignal){
  bool inactive=false;if(!gActive.compare_exchange_strong(inactive,true))return{{"status","refused"},{"reason","engine-job-already-active"},{"recoveryRoute",event.value("recoveryRoute",J("semi-static"))},{"publisherCalls",0},{"engineRevision",kEngineRevision},{"moduleQualification",kModuleQualification},{"wholeImageHashIsVersionGate",false}};
  struct Finish{~Finish(){gActive=false;gEmit={};gCurrent={};gObservationCurrent={};gWakeSignal=0;gOriginalSteam={};gCleanupMode=false;gRecoveryRoute=RecoveryRoute::SemiStatic;}}finish;
  gEmit=std::move(emit);gCurrent=std::move(guard);gObservationCurrent=std::move(observationGuard);gWakeSignal=wakeSignal;gCleanupMode=false;J summary={{"schema","ymcc.steam-original-recovery-summary.v1"},{"engineRevision",kEngineRevision},{"moduleQualification",kModuleQualification},{"wholeImageHashIsVersionGate",false},{"uiVerified",false},{"inputVerified",false},{"repairAccepted",false},{"spawnedHelperProcesses",0},{"firstAttachExitPolicyPreinitialized",false}};
  summary["recoveryRoute"]=event.value("recoveryRoute",J("semi-static"));summary["route"]=summary["recoveryRoute"];
  summary["source"]="not-entered";summary["publisherCalls"]=0;summary["publisherCallsKnown"]=true;summary["consumed"]=false;summary["driverEntered"]=false;summary["mutationCommitAttempted"]=false;
  std::optional<fp::path> data,logs,evidence;
  try{
    nativeFailureBoundary([&]{
    const auto route=event.value("recoveryRoute",std::string("semi-static"));
    need(route=="legacy-dynamic"||route=="semi-static","recovery-route-unsupported");
    gRecoveryRoute=route=="legacy-dynamic"?RecoveryRoute::LegacyDynamic:RecoveryRoute::SemiStatic;
    summary["moduleQualification"]=routeQualification();summary["wholeImageHashIsVersionGate"]=legacyRoute();
    summary["qualification"]={{"mode",routeQualification()},{"passed",false},{"compatibilityContractRequired",!legacyRoute()},{"rawTypedReads",legacyRoute()}};
    data=absolute(event.at("dataRoot"));logs=absolute(event.at("logRoot"));
    evidence=event.contains("evidenceRoot")?absolute(event.at("evidenceRoot")):*data;
    summary["storage"]={{"persistentDataRoot",utf8(data->wstring())},{"evidenceRoot",utf8(evidence->wstring())},
        {"explicitEvidenceRoot",event.contains("evidenceRoot")},{"logClearRefundsConsumption",false}};
#ifdef YMCC_STEAM_ORIGINAL_ENGINE_TESTING
    if(gOfflineTypedReadFault)gOfflineTypedReadFault(summary);
#endif
    const auto& trigger=event.at("trigger");const auto kind=trigger.at("kind").get<std::string>();need(kind=="startup"||kind=="wake"||kind=="suspend-capture"||kind=="target-changed"||kind=="steam-changed","trigger-kind");
    summary["trigger"]={{"kind",kind},{"generation",trigger.at("generation")},{"id",trigger.at("id")},{"rulesEpoch",trigger.at("rulesEpoch")}};
    const auto account=decimal(event.at("account64"));
    if(kind=="wake"){
      const auto episode=decimal(trigger.at("powerEventFileTime"));const auto prior=gCurrent;
      gCurrent=[prior,episode]{const auto now=nowFileTime();return (!prior||prior())&&now>=episode&&
          now-episode<ymcc::steamOriginal::WakeWatch::budgetMs*10000ULL;};
    }
    requireCurrent();auto scope=openScope(event);gOriginalSteam=[&scope]{const auto& p=scope.roles[0];return rw::exactProcess(p.handle.h,p.pid,p.born,p.path,p.session);};
    summary["targetGameId"]=std::to_string(scope.gameId);summary["targetAppId"]=scope.appid;
    LifetimeMutex lifetime;lifetime.acquire(scope.roles[0].pid,scope.roles[0].born);
    const auto jobStarted=GetTickCount64();summary["consumed"]=false; // Healthy observations never consume mutation budget.
    const auto sourceAudit=auditedPrepublicationSource(scope,*data);
    const auto disabledKey=textHash(life(scope).dump());
    need(!fp::exists(*data/L"DISABLED.json")&&!fp::exists(*data/L"disabled"/utf16(disabledKey+".v2.json"))&&
        (!fp::exists(*data/L"disabled"/utf16(disabledKey+".json"))||sourceAudit.has_value()),"engine-disabled-after-prior-failure");
    if(sourceAudit)summary["auditedPrepublicationSource"]={{"qualified",true},{"oldKeysPreserved",true},{"refunded",false},
        {"historicalReadOnlyFailureClassified",true},{"newMutationDisableStillAuthoritative",true}};
    summary["lifeKey"]=textHash(life(scope).dump());
    const auto initialSnapshotStarted=GetTickCount64();const auto initialSnapshotCpu=workerStamp();auto sample=snapshot(scope,account);
    summary["timingsMs"]["initialSnapshot"]=GetTickCount64()-initialSnapshotStarted;
    noteWorkerPhase(summary,"initialSnapshot",initialSnapshotCpu);noteSnapshotCost(summary,"initialSnapshot",sample.executionProfile);
    summary["qualification"]["passed"]=true;summary["before"]=nativeState(scope,sample);publish({{"event","observed"},{"result",summary.at("before")}});
    const auto referencePath=*data/L"references"/utf16(scopeKey(scope,account)+".json");
    J reference=loadCompatibleReference(*data,scope,sample,account);
    if(reference.is_null()&&(event.contains("seed")||fp::exists(*data/L"seed.json"))){
      const auto seed=event.contains("seed")?event.at("seed"):readJson(*data/L"seed.json");const auto& baseline=seed.contains("baseline")?seed.at("baseline"):seed;
      bool sameSeedScope=true;for(const auto* role:{"steam","game","cef"})sameSeedScope=sameSeedScope&&normalizedIdentity(baseline.at("before").at("roles").at(role))==scope.identities.at(role);
      if(sameSeedScope){reference=importReference(seed,scope,account);matchReference(reference,scope,sample,account);requireCurrent();durableCreate(referencePath,reference.dump(2));}
      else{need(sample.flags==8196,"seed-prior-steam-game-cef-lifetime-not-reusable-while-lost");publish({{"event","seed-not-applicable"},{"reason","prior-steam-game-cef-lifetime"}});}
    }
    if(sample.flags==8196){
      if(reference.is_null()){requireCurrent();const auto nonce=freshUuid();reference={{"schema","ymcc.steam-native-original-reference.v1"},{"scope",referenceScope(scope)},{"captureParent",scope.identities.at("ymcc")},{"accountFingerprint",textHash(std::to_string(account))},{"bindingNonce",nonce},{"pins",pins(sample,scope.roles[0].born,account,nonce)},{"healthyFlags",8196},{"compatibilityContract","original-running-code-data-v2"},{"clientSha256",sample.clientSha256},{"uiSha256",sample.uiSha256}};const auto after=snapshot(scope,account);need(after.flags==8196,"healthy-reference-changed-during-capture");matchReference(reference,scope,after,account);requireCurrent();durableCreate(referencePath,reference.dump(2));summary["referenceCaptured"]=true;}
      else matchReference(reference,scope,sample,account);
      summary["status"]="healthy";summary["nativeRecovered"]=false;
    }
    if(sample.flags==8196&&kind=="wake"&&event.value("recoveryAllowed",false)){
      ymcc::steamOriginal::WakeWatch watch;watch.eventFileTime=decimal(trigger.at("powerEventFileTime"));
      const auto started=GetTickCount64(),cpuStarted=threadCpuTicks();WakeProbeReader probe(scope);
      const auto observationNow=nowFileTime();
      const bool observationScopeCurrent=probe.owned();
      summary["wakeObservationAdmission"]={{"eventFileTime",std::to_string(watch.eventFileTime)},
          {"checkedFileTime",std::to_string(observationNow)},
          {"ageMs",observationNow>=watch.eventFileTime?J((observationNow-watch.eventFileTime)/10000ULL):J(nullptr)},
          {"scopeCurrent",observationScopeCurrent},{"observationWindowMs",watch.observationMs},
          {"mutationBudgetMs",watch.budgetMs}};
      need(observationScopeCurrent,"wake-observation-scope-changed-before-start");
      need(watch.beginObservation(observationNow,true),"wake-observation-expired-before-start");
      U64 hintSignals=0,processSignals=0,waits=0,lastHealthyProbeFileTime=0,firstHintFileTime=0,lastHintFileTime=0;
      publish({{"event","wake-observation-started"},{"budgetMs",watch.budgetMs},{"pollMs",watch.pollMs},
          {"fastPollMs",watch.fastPollMs},{"fastWindowMs",watch.fastWindowMs},{"maximumProbes",watch.maximumProbes},
          {"precisionAnchor","physical-wake-witness"},{"precisionStartMs",watch.precisionStartMs},
          {"precisionEndMs",watch.precisionEndMs},{"precisionPollMs",watch.precisionPollMs},
          {"precisionProbes",watch.precisionProbes},
          {"hintWindowMs",watch.hintWindowMs},{"maximumFastProbes",watch.maximumFastProbes},
          {"observationStartFileTime",std::to_string(watch.observationStartFileTime)},
          {"settleWindowMs",watch.settleWindowMs},{"settlePollMs",watch.settlePollMs},
          {"targetGameId",std::to_string(scope.gameId)},{"mutationDispatched",false}});
      while(watch.observes(nowFileTime(),probe.owned())){
        HANDLE handles[5]{};DWORD count=0;
        for(const auto& role:scope.roles)handles[count++]=role.handle.h;
        if(gWakeSignal)handles[count++]=reinterpret_cast<HANDLE>(gWakeSignal);
        ++waits;const auto waited=WaitForMultipleObjects(count,handles,FALSE,static_cast<DWORD>(watch.delayMs(nowFileTime())));
        need(waited!=WAIT_FAILED,"wake-event-wait-failed");
        if(waited>=WAIT_OBJECT_0&&waited<WAIT_OBJECT_0+4){++processSignals;break;}
        if(gWakeSignal&&waited==WAIT_OBJECT_0+4){++hintSignals;const auto hinted=nowFileTime();
          if(!firstHintFileTime)firstHintFileTime=hinted;lastHintFileTime=hinted;
          watch.noteHint(hinted,probe.owned());}
        if(!watch.observes(nowFileTime(),probe.owned()))break;
        if(!watch.probeDue(nowFileTime()))continue;
        if(!watch.reserveProbe(nowFileTime(),probe.owned()))break;
        if(probe.flags(sample)==8196){lastHealthyProbeFileTime=nowFileTime();continue;}
        const auto lossProbeFileTime=nowFileTime(),lossQualificationStarted=GetTickCount64();
        sample=snapshot(scope,account);matchReference(reference,scope,sample,account);
        summary["timingsMs"]["lossQualification"]=GetTickCount64()-lossQualificationStarted;
        summary["lastHealthyProbeFileTime"]=std::to_string(lastHealthyProbeFileTime);
        summary["lossProbeFileTime"]=std::to_string(lossProbeFileTime);
        summary["lossConfirmedFileTime"]=std::to_string(nowFileTime());
        summary["lossNative"]=nativeState(scope,sample);
        summary["lateLossObserved"]=sample.flags==4;
        summary["lossDetectedAfterMs"]=GetTickCount64()-started;
        publish({{"event","late-running-loss-observed"},{"result",nativeState(scope,sample)},
            {"elapsedMs",summary["lossDetectedAfterMs"]},{"mutationDispatched",false}});break;
      }
      summary["observationCost"]={{"workerCpu100ns",threadCpuTicks()-cpuStarted},{"processOpens",1},
          {"processLivenessWaitCallsPerGuard",1},
          {"readCalls",probe.calls},{"readBytes",probe.bytes},{"hintSignals",hintSignals},{"processSignals",processSignals},
          {"kernelWaits",waits},{"hintEventAvailable",gWakeSignal!=0},{"fullGuardPerProbe",!static_cast<bool>(gObservationCurrent)},{"cachedReadOnlyGuardNeverAuthorizesMutation",true}};
      summary["wakeObservation"]={{"probes",watch.probes},{"pollMs",watch.pollMs},
          {"observationStartFileTime",std::to_string(watch.observationStartFileTime)},
          {"firstHintFileTime",std::to_string(firstHintFileTime)},{"lastHintFileTime",std::to_string(lastHintFileTime)},
          {"lastHealthyProbeFileTime",std::to_string(lastHealthyProbeFileTime)},
          {"fastProbes",watch.fastProbes},{"settleWindowMs",watch.settleWindowMs},{"settlePollMs",watch.settlePollMs},
          {"settleFastWindowMs",watch.settleFastWindowMs},{"settleFastPollMs",watch.settleFastPollMs},
          {"settleFastProbes",watch.settleFastProbes},{"maximumSubsecondProbes",watch.maximumFastProbes},
          {"elapsedMs",GetTickCount64()-started},{"budgetMs",watch.budgetMs},{"observationWindowMs",watch.observationMs},{"idlePolling",false},
          {"fastPollMs",watch.fastPollMs},{"fastWindowMs",watch.fastWindowMs},{"maximumProbes",watch.maximumProbes},
          {"precisionAnchor","physical-wake-witness"},{"precisionStartMs",watch.precisionStartMs},
          {"precisionEndMs",watch.precisionEndMs},{"precisionPollMs",watch.precisionPollMs},
          {"precisionProbes",watch.precisionProbes},
          {"hintWindowMs",watch.hintWindowMs},{"maximumFastProbes",watch.maximumFastProbes}};
      requireCurrent();
      if(sample.flags==8196){sample=snapshot(scope,account);matchReference(reference,scope,sample,account);
          summary["after"]=nativeState(scope,sample);}
      need(watch.allows(nowFileTime(),true)||sample.flags==8196,"wake-expired-before-recovery");
    }
    if(sample.flags==4){
      const auto recoveryCpuStarted=threadCpuTicks();const auto recoveryPhaseStarted=workerStamp();
      if(!summary.contains("lossConfirmedFileTime")){summary["lossConfirmedFileTime"]=std::to_string(nowFileTime());summary["lossNative"]=nativeState(scope,sample);}
      need(!reference.is_null(),"lost-without-matching-healthy-reference");matchReference(reference,scope,sample,account);
      need((kind=="wake"||kind=="startup")&&event.value("recoveryAllowed",false),"event-observation-only");requireCurrent();need(moduleCapacity(scope.roles[0].pid)<16,"global-steam-lifetime-image-capacity");
      summary["recoveryDispatchAfterMs"]=GetTickCount64()-jobStarted;
      if(kind=="wake"){const auto episode=decimal(trigger.at("powerEventFileTime"));need(nowFileTime()>=episode&&nowFileTime()-episode<ymcc::steamOriginal::WakeWatch::budgetMs*10000ULL,"wake-expired-before-consume");}
      consume(event,scope,*data,false);summary["consumptionPreflightPassed"]=true; // Read-only; duplicate rights rejected before source attach.
      const auto queryStarted=GetTickCount64();const auto queryCpu=workerStamp();
      QueryActor query(account,(fp::path(scope.roles[0].path).parent_path()/L"steamclient64.dll").wstring(),gRecoveryRoute,sample.clientSha256);query.waitReady();
      summary["timingsMs"]["normalQuerySetup"]=GetTickCount64()-queryStarted;
      noteWorkerPhase(summary,"normalQuerySetup",queryCpu);
      const auto contextStarted=GetTickCount64();const auto contextCpu=workerStamp();J context;{ExecutionProbeScope probe(summary,"sourceContext");context=sourceGetterContext(scope,sample,account,query,summary);}
      summary["timingsMs"]["sourceContext"]=GetTickCount64()-contextStarted;
      noteWorkerPhase(summary,"sourceContext",contextCpu);
      requireCurrent();const auto revalidationStarted=GetTickCount64();const auto revalidationCpu=workerStamp();const auto fresh=snapshot(scope,account);need(fresh.flags==4,"recovery-no-longer-lost");matchReference(reference,scope,fresh,account);
      summary["timingsMs"]["preDispatchSnapshot"]=GetTickCount64()-revalidationStarted;
      noteWorkerPhase(summary,"preDispatchSnapshot",revalidationCpu);noteSnapshotCost(summary,"preDispatchSnapshot",fresh.executionProfile);
      gTrialDirectory=(*data/L"work"/L"wuxia-single-recovery"/L"build").wstring();fp::create_directories(gTrialDirectory);const auto nonce=freshUuid();const auto image=fp::path(gTrialDirectory)/utf16("wuxia-publish-"+nonce+".dll");
      need(digest(embedded::image,sizeof embedded::image)==embedded::sha256,"embedded-trial-hash");requireCurrent();durableCreate(image,std::string(reinterpret_cast<const char*>(embedded::image),sizeof embedded::image));need(fileHash(image.wstring())==embedded::sha256,"extracted-trial-hash");
      const auto& p=reference.at("pins");J proof={{"pinNonce",reference.at("bindingNonce")},{"ownerPin",p.at("owner")},{"recordPin",p.at("record")},{"uiObjectPin",p.at("uiObject")},{"healthyFlags",8196}};
      J request={{"processes",scope.identities},{"account64",std::to_string(account)},{"gameId",std::to_string(scope.gameId)},{"appid",scope.appid},{"nonce",nonce},{"noPublish",false},{"sourceThreadId",context.at("tid")},{"sourceThreadBirth",context.at("threadBirthFileTime")},{"healthyProof",proof},{"trialDllPath",utf8(image.wstring())}};
      request["recoveryRoute"]=routeName();
      if(kind=="wake"){
        // Read uptime first so scheduling between the two clock reads cannot
        // extend the injected callback beyond the remaining physical budget.
        const auto mutationTick=GetTickCount64();const auto mutationUtc=nowFileTime();
        request["mutationDeadlineTick"]=std::to_string(wakeDeadlineTick(
            decimal(trigger.at("powerEventFileTime")),mutationUtc,mutationTick));
      }
      // Host snapshots do not complete the driver's independent qualification.
      // Keep the right unspent until the real driver reaches its first remote
      // allocation, after its own raw/content contracts and original pins.
      Current beforeMutation=[&]{
        requireCurrent();need(!summary.value("consumed",false),"publication-commit-already-used");
        summary["mutationCommitAttempted"]=true;
        consume(event,scope,*data);summary["consumed"]=true;
        summary["mutationLedgerSchema"]="ymcc.steam-original-recovery-consumption.v2";
        summary["mutationBoundary"]="after-driver-readonly-qualification-before-first-remote-allocation";
        summary["publisherCallsKnown"]=false;return true;
      };
      summary["driverEntered"]=true;
      const auto driverStarted=GetTickCount64();const auto driverCpu=workerStamp();J result;{ExecutionProbeScope probe(summary,"driverAndCleanup");result=runTrial(request,query,beforeMutation);}
      summary["timingsMs"]["driverAndCleanup"]=GetTickCount64()-driverStarted;
      noteWorkerPhase(summary,"driverAndCleanup",driverCpu);
      request["account64"]=nullptr;summary["driverResult"]=result;
      if(!summary.value("consumed",false)){
        summary["publisherCalls"]=0;summary["publisherCallsKnown"]=true;
        need(!result.value("mutationCommitAccepted",false)&&!result.value("remoteAllocationAttempted",false),"driver-mutation-without-durable-commit");
        throw std::runtime_error(result.value("reason",std::string("driver-prepublication-refused")));
      }
      if(result.contains("mutationResult")&&result.at("mutationResult").is_object()){summary["publisherCalls"]=result.at("mutationResult").value("publisherCalls",0);summary["publisherCallsKnown"]=result.at("mutationResult").contains("publisherCalls");}
      need(result.value("entryRestored",false)&&result.value("debugRegistersRestored",false)&&result.value("debuggerDetached",false)&&result.value("disarmConfirmed",false),"recovery-cleanup-not-confirmed");
      need(result.at("mutationResult").at("publisherCalls")==1&&result.at("mutationResult").at("publisherReturned")==true&&result.at("mutationResult").at("guardBefore")==true&&result.at("mutationResult").at("guardAfter")==true,"native-callback-not-confirmed");
      requireCurrent();const auto readbackStarted=GetTickCount64();const auto readbackCpu=workerStamp();const auto after=snapshot(scope,account);need(after.flags==8196,"post-native-running-not-restored");matchReference(reference,scope,after,account);
      summary["timingsMs"]["postRecoverySnapshot"]=GetTickCount64()-readbackStarted;
      noteWorkerPhase(summary,"postRecoverySnapshot",readbackCpu);noteSnapshotCost(summary,"postRecoverySnapshot",after.executionProfile);
      summary["executionCost"]={{"recoveryWorkerCpu100ns",threadCpuTicks()-recoveryCpuStarted},
          {"scope","recovery-worker-only; excludes QueryActor and Steam remote threads"}};
      try{WorkerCost total;total.add(recoveryPhaseStarted,workerStamp());summary["executionCost"]["recoveryWorkerCycles"]=total.cycles;
        summary["executionCost"]["cycleClockFailures"]=total.clockFailures;}catch(...){}
      summary["nativeRecoveredFileTime"]=std::to_string(nowFileTime());
      summary["after"]=nativeState(scope,after);summary["status"]="recovered-native";summary["nativeRecovered"]=true;summary["jobElapsedMs"]=GetTickCount64()-jobStarted;
    }
    },"native-engine-exception");
  }catch(const std::exception& e){gSourceContextInit={};summary["status"]="refused";summary["reason"]=e.what();summary["nativeRecovered"]=false;
    if(const auto* diagnostic=dynamic_cast<const DiagnosticFailure*>(&e))summary["failureDiagnostic"]=diagnostic->detail;
    if(data&&summary.value("consumed",false)&&summary.contains("lifeKey")){try{const auto disabled=*data/L"disabled"/utf16(summary.at("lifeKey").get<std::string>()+".v2.json");if(!fp::exists(disabled))durableCreate(disabled,J{{"schema","ymcc.steam-original-mutation-disable.v2"},{"reason",e.what()},{"consumed",true},{"noAutomaticRetry",true}}.dump(2));}catch(...) {}}
  }
  // Only sanitized summary leaves the worker. Raw account/seed/request never do.
  if(evidence){try{atomicJson(*evidence/L"last-recovery-summary.json",summary);atomicJson(*evidence/L"monitor"/L"status.json",summary);}catch(const std::exception& e){summary["persistenceError"]=e.what();}}
  if(logs){try{fp::create_directories(*logs);const auto file=*logs/L"steam-original-recovery.jsonl";if(fp::exists(file)&&fp::file_size(file)>4*1024*1024){auto previous=file;previous+=L".1";if(fp::exists(previous))fp::remove(previous);fp::rename(file,previous);}std::ofstream log(file,std::ios::binary|std::ios::app);log<<summary.dump()<<'\n';}catch(...) {}}
  publish({{"event","job-finished"},{"result",summary}});return summary;
}
}
#ifdef YMCC_STEAM_ORIGINAL_ENGINE_TESTING
namespace ymcc::steamOriginalRecovery {
// These entrypoints run only inside this offline fixture's own process.
static DWORD WINAPI offlineCompletedControl(void*) { return 0; }
static DWORD WINAPI offlineWaitingControl(void* signal) {
  return WaitForSingleObject(static_cast<HANDLE>(signal), INFINITE) == WAIT_OBJECT_0 ? 0 : 1;
}
Json offlineModulePairMicro(){
  const DWORD pid=GetCurrentProcessId();std::array<wchar_t,32768> first{},second{};
  need(GetModuleFileNameW(nullptr,first.data(),static_cast<DWORD>(first.size()))>0&&
      GetModuleFileNameW(GetModuleHandleW(L"kernel32.dll"),second.data(),static_cast<DWORD>(second.size()))>0,
      "owned-fixture-module-paths");
  const std::wstring clientPath=first.data(),uiPath=second.data();
  const auto equal=[](const rw::Module& a,const rw::Module& b){return a.base==b.base&&a.size==b.size&&a.path==b.path;};
  rw::Module oldClient,oldUi,newClient,newUi;
  need(rw::exactModule(pid,clientPath,oldClient)&&rw::exactModule(pid,uiPath,oldUi)&&
      originalImagePair(pid,clientPath,uiPath,newClient,newUi)&&equal(oldClient,newClient)&&equal(oldUi,newUi),
      "pair-source-consumers-disagree");
  std::wstring variantClient=clientPath,variantUi=uiPath;
  std::transform(variantClient.begin(),variantClient.end(),variantClient.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});
  std::replace(variantUi.begin(),variantUi.end(),L'\\',L'/');
  need(originalImagePair(pid,variantClient,variantUi,newClient,newUi)&&equal(oldClient,newClient)&&equal(oldUi,newUi),
      "pair-normalized-paths-changed");
  const auto rejectedPair=[&](DWORD target,const std::wstring& a,const std::wstring& b){
    rw::Module c{0x11000,1,L"sentinel-client"},u{0x12000,2,L"sentinel-ui"};
    need(!originalImagePair(target,a,b,c,u)&&c.base==0x11000&&c.path==L"sentinel-client"&&
        u.base==0x12000&&u.path==L"sentinel-ui","pair-failure-partial-or-accepted");};
  rejectedPair(0,clientPath,uiPath);rejectedPair(MAXDWORD,clientPath,uiPath);
  rejectedPair(pid,clientPath,L"");rejectedPair(pid,L"",uiPath);
  rejectedPair(pid,clientPath,clientPath);rejectedPair(pid,clientPath,uiPath+L".absent");
  rejectedPair(pid,clientPath+L".absent",uiPath);
  const auto cycles=[](){ULONG64 value=0;need(QueryThreadCycleTime(GetCurrentThread(),&value),"pair-cycle-clock");return value;};
  LARGE_INTEGER frequency{};need(QueryPerformanceFrequency(&frequency),"pair-wall-frequency");
  constexpr unsigned batches=9,iterations=128;J rounds=J::array();U64 checksum=0;
  const auto measure=[&](bool candidate){
    LARGE_INTEGER begin{},end{};need(QueryPerformanceCounter(&begin),"pair-wall-start");
    const auto cpu=threadCpuTicks(),cycle=cycles();
    for(unsigned i=0;i<iterations;++i){rw::Module c,u;
      const bool ok=candidate?originalImagePair(pid,clientPath,uiPath,c,u):
          (rw::exactModule(pid,clientPath,c)&&rw::exactModule(pid,uiPath,u));
      need(ok&&equal(c,oldClient)&&equal(u,oldUi),"pair-measurement-identity-changed");checksum^=c.base+u.base+c.size+u.size+i;
    }
    const auto elapsedCycles=cycles()-cycle,elapsedCpu=threadCpuTicks()-cpu;
    need(QueryPerformanceCounter(&end),"pair-wall-end");
    return J{{"wallMs",1000.0*static_cast<double>(end.QuadPart-begin.QuadPart)/static_cast<double>(frequency.QuadPart)},
        {"workerCpu100ns",elapsedCpu},{"workerCycles",elapsedCycles}};
  };
  for(unsigned i=0;i<8;++i){need(originalImagePair(pid,clientPath,uiPath,newClient,newUi)&&
      rw::exactModule(pid,clientPath,oldClient)&&rw::exactModule(pid,uiPath,oldUi),"pair-warmup");}
  for(unsigned round=0;round<batches;++round){J row;
    if(round%2==0){row["baseline"]=measure(false);row["candidate"]=measure(true);}
    else{row["candidate"]=measure(true);row["baseline"]=measure(false);}
    row["candidateFirst"]=round%2!=0;rounds.push_back(std::move(row));
  }
  const auto median=[&](const char* variant,const char* metric){std::array<double,batches> values{};
    for(unsigned i=0;i<batches;++i)values[i]=rounds.at(i).at(variant).at(metric).get<double>();
    std::sort(values.begin(),values.end());return values[batches/2];};
  J medians;for(const auto* variant:{"baseline","candidate"})for(const auto* metric:{"wallMs","workerCpu100ns","workerCycles"})
      medians[variant][metric]=median(variant,metric);
  const double wallSaved=1.0-median("candidate","wallMs")/median("baseline","wallMs");
  const double cyclesSaved=1.0-median("candidate","workerCycles")/median("baseline","workerCycles");
  return {{"schema","ymcc.original-module-pair-real-consumer-micro.v1"},{"success",true},{"rounds",rounds},
      {"iterationsPerRound",iterations},{"roundCount",batches},{"medians",medians},{"wallReductionFraction",wallSaved},
      {"workerCycleReductionFraction",cyclesSaved},{"candidateHasMeasuredBenefit",wallSaved>0.05&&cyclesSaved>0.05},
      {"identityComparisonsPerVariant",batches*iterations},{"baselineModuleCensusesPerPair",2},{"candidateModuleCensusesPerPair",1},
      {"functionalBoundaries",8},{"checksum",checksum},{"scope","actual compiled module-pair consumer; fixture's own process only"},
      {"sceneProcessOpens",0},{"steamGameMemoryReads",0},{"fieldLatencyClaim",false},{"fullImageHashCallsRemoved",0}};
}
Json offline_self_test(const std::string& directory){
  Json cases=Json::array();const auto test=[&](const char* name,const auto& body){try{body();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"reason",e.what()}});}};
  const auto rejected=[](const auto& body){bool refused=false;try{body();}catch(...){refused=true;}need(refused,"expected-refusal");};
  test("profile-old-pair-keeps-original-getter",[&]{const auto v=qualifiedProfile(rc::kClientSha256,app::kUiSha256);
    need(v.getterRva==0xcfdc0&&v.publisherRva==0x4e27e0,"legacy-profile-drift");});
  const auto newClient=rc::profileSha256(rc::ProfileId::Public1792618285);
  const auto newUi=app::profileSha256(app::ProfileId::Public1792618285);
  test("profile-new-pair-selects-qualified-getter",[&]{const auto v=qualifiedProfile(newClient,newUi);
    need(std::string_view(v.clientSha256)==newClient&&std::string_view(v.uiSha256)==newUi&&
      v.getterRva!=0&&v.publisherRva!=0,"new-profile-missing");});
  const auto refusedProfile=[&](const char* name,std::string client,std::string ui){test(name,[&]{
    bool refused=false;try{(void)qualifiedProfile(client,ui);}catch(const DiagnosticFailure& e){
      refused=true;need(std::string(e.what())=="fixed-profile-hash"&&
        e.detail.at("clientSha256")==client&&e.detail.at("uiSha256")==ui&&
        e.detail.at("beforeMemoryRead")==true&&e.detail.at("automaticFallback")==false,"profile-failure-detail");}
    need(refused,"unsupported-pair-accepted");});};
  refusedProfile("profile-old-client-new-ui-refused",rc::kClientSha256,newUi);
  refusedProfile("profile-new-client-old-ui-refused",newClient,app::kUiSha256);
  refusedProfile("profile-unknown-client-refused",std::string(64,'0'),newUi);
  refusedProfile("profile-unknown-ui-refused",newClient,std::string(64,'0'));
  refusedProfile("profile-short-client-digest-refused",std::string(newClient).substr(0,8),newUi);
  test("permission-access-denied-reported-separately",[&]{bool refused=false;
    try{requireReadProcess(false,ERROR_ACCESS_DENIED,8112,"native-read-process");}
    catch(const DiagnosticFailure& e){refused=true;need(std::string(e.what())=="native-read-process"&&
      e.detail.at("win32Error")==5&&e.detail.at("accessDenied")==true&&
      e.detail.at("steamPid")==8112&&e.detail.at("beforeMemoryRead")==true&&
      e.detail.at("automaticElevation")==false,"permission-detail");}need(refused,"permission-denial-hidden");});
  test("permission-other-error-not-labelled-access-denied",[&]{bool refused=false;
    try{requireReadProcess(false,ERROR_INVALID_PARAMETER,8112,"wake-probe-process");}
    catch(const DiagnosticFailure& e){refused=true;need(e.detail.at("win32Error")==87&&
      e.detail.at("accessDenied")==false,"permission-error-conflated");}need(refused,"error-hidden");});
  test("permission-success-keeps-existing-read-path",[&]{requireReadProcess(true,ERROR_SUCCESS,8112,"native-read-process");});
  test("new-profile-skips-unqualified-legacy-diagnostics-before-reading",[&]{unsigned calls=0;
    const rc::Read read=[&](U64,void*,std::size_t){++calls;return false;};
    const auto post=observePostLogonSourceDiagnostic(read,0x138000000ULL,0x2000000,0x400000,false);
    const auto semi=observeSemiStaticSource(read,0x138000000ULL,0x2000000,0x13a000000ULL,0x2000000,0x400000,false);
    need(calls==0&&!post.at("available").get<bool>()&&!semi.at("available").get<bool>()&&
      post.at("reason")=="fixed-profile-unqualified"&&semi.at("reason")=="fixed-profile-unqualified"&&
      post.at("participatesInRecoveryAdmission")==false&&semi.at("participatesInRecoveryAdmission")==false,"legacy-diagnostic-read-new-image");});
  test("compatibility-layout-selection-does-not-take-version-or-hash",[&]{for(const auto client:{compat::Layout::Legacy,compat::Layout::Public})
    for(const auto ui:{compat::Layout::Legacy,compat::Layout::Public}){const auto value=tr::profileForCompatibility(client,ui);
      need(value&&value->getterRva==0xcfdc0&&value->publisherRva==(client==compat::Layout::Legacy?0x4e27e0ULL:0x4e27f0ULL),"layout-selector-needs-hash");}});
  test("compatibility-layout-selection-refuses-invalid-contract",[&]{need(!tr::profileForCompatibility(static_cast<compat::Layout>(99),compat::Layout::Legacy)&&
      !tr::profileForCompatibility(compat::Layout::Public,static_cast<compat::Layout>(99)),"invalid-layout-fallback");});
  const auto root=absolute(directory)/utf16("offline-"+freshUuid());fp::create_directories(root);
  Scope scope;scope.gameId=10307630032478011392ULL;scope.appid=2399932135u;scope.identities=J::object();const std::array<const char*,4> names{"steam","game","ymcc","cef"};
  for(std::size_t i=0;i<names.size();++i){scope.roles[i].pid=4000000000u+static_cast<DWORD>(i);scope.roles[i].born=100000000000000000ULL+i;scope.roles[i].session=1;scope.roles[i].path=L"D:\\offline-fixture\\"+std::to_wstring(i)+L".exe";scope.identities[names[i]]={{"pid",scope.roles[i].pid},{"birthFileTime",std::to_string(scope.roles[i].born)},{"session",1},{"path",utf8(scope.roles[i].path)}};}
  const U64 account=76561198000000001ULL;const auto originalKey=scopeKey(scope,account);
  test("parent-restart-preserves-reference-key",[&]{scope.identities["ymcc"]["pid"]=3999999999u;scope.identities["ymcc"]["birthFileTime"]="100000000999999999";need(scopeKey(scope,account)==originalKey,"parent-leaked-into-reference");});
  test("steam-birth-changes-reference",[&]{auto old=scope.identities["steam"];scope.identities["steam"]["birthFileTime"]="100000000999999999";need(scopeKey(scope,account)!=originalKey,"steam-not-bound");scope.identities["steam"]=old;});
  test("game-birth-changes-reference",[&]{auto old=scope.identities["game"];scope.identities["game"]["birthFileTime"]="100000000999999999";need(scopeKey(scope,account)!=originalKey,"game-not-bound");scope.identities["game"]=old;});
  test("cef-birth-changes-reference",[&]{auto old=scope.identities["cef"];scope.identities["cef"]["birthFileTime"]="100000000999999999";need(scopeKey(scope,account)!=originalKey,"cef-not-bound");scope.identities["cef"]=old;});
  test("account-changes-reference",[&]{need(scopeKey(scope,account+1)!=originalKey,"account-not-bound");});
  test("generic-game-id-qualifies-tmnt-and-wuxia",[&]{
    need(ymcc::steamappstatewin::gameIdMatches(10307630032478011392ULL,2399932135u)&&
      ymcc::steamappstatewin::gameIdMatches(10260559608980439040ULL,2388972698u),"generic-type2-map");
    need(!ymcc::steamappstatewin::gameIdMatches(10307630032478011392ULL,2388972698u),"mixed-type2-map");
  });
  test("generic-target-parser-exact-string-and-appid",[&]{
    const auto target=targetIdentity(J{{"gameId","10307630032478011392"},{"appid",2399932135u}});
    need(target.first==10307630032478011392ULL&&target.second==2399932135u,"generic-target-rounding");
    rejected([&]{targetIdentity(J{{"gameId","10307630032478011392"},{"appid",2388972698u}});});
    rejected([&]{targetIdentity(J{{"gameId",10307630032478011392ULL},{"appid",2399932135u}});});
    rejected([&]{targetIdentity(J{{"gameId","945360"},{"appid",945360u}});});
    rejected([&]{targetIdentity(J{{"gameId","10307630032478011393"},{"appid",2399932135u}});});
    rejected([&]{targetIdentity(J{{"gameId","18446744073709551616"},{"appid",2399932135u}});});
    rejected([&]{targetIdentity(J{{"gameId","10307630032478011392"},{"appid",-1895035161}});});
  });
  test("generic-game-id-is-part-of-reference-key",[&]{const auto old=scope.gameId;scope.gameId=10260559608980439040ULL;
    need(scopeKey(scope,account)!=originalKey,"full-game-id-not-bound");scope.gameId=old;});
  test("generic-record-pin-binds-full-id",[&]{Snapshot sample;sample.gameId=scope.gameId;sample.flagsAddress=0x2000;
    const auto one=pins(sample,100,account,"00000000-0000-4000-8000-000000000001");sample.gameId=10260559608980439040ULL;
    const auto two=pins(sample,100,account,"00000000-0000-4000-8000-000000000001");
    need(one.at("owner")==two.at("owner")&&one.at("record")!=two.at("record")&&one.at("uiObject")!=two.at("uiObject"),"id-not-bound-to-record-and-ui");});
  test("new-profile-native-state-reports-actual-hashes",[&]{Snapshot sample;sample.record=J::object();sample.clientSha256=newClient;sample.uiSha256=newUi;
    sample.clientProfile=rc::ProfileId::Public1792618285;sample.uiProfile=app::ProfileId::Public1792618285;
    const auto state=nativeState(scope,sample);need(state.at("profile").at("clientSha256")==newClient&&
      state.at("profile").at("uiSha256")==newUi,"state-lied-about-profile");});
  // Execute the same reference loader used by handle, with a saved healthy
  // sample and an already-lost current snapshot. No scene process is opened.
  const auto legacySample=[&]{Snapshot sample;sample.gameId=scope.gameId;sample.flags=4;
    sample.flagsAddress=0x220020;sample.client.base=0x180000000ULL;sample.ui.base=0x190000000ULL;
    sample.selected.engine=0x310000;sample.selected.user=0x410000;sample.records.records=0x510000;
    sample.target.index=2;sample.target.primary=scope.roles[1].pid;sample.target.foreground=scope.roles[1].pid;
    return sample;};
  const auto legacyValue=[&](const Snapshot& sample){const std::string nonce="a7e60aaf-583a-486c-b709-808e25166756";
    return J{{"schema","ymcc.steam-native-original-reference.v1"},{"scope",legacyReferenceScope(scope)},
      {"captureParent",scope.identities.at("ymcc")},{"healthyFlags",8196},{"bindingNonce",nonce},
      {"pins",pins(sample,scope.roles[0].born,account,nonce)},{"accountFingerprint",textHash(std::to_string(account))},
      {"clientSha256",rc::kClientSha256},{"uiSha256",app::kUiSha256}};};
  test("reference-file-hash-change-does-not-break-original-qualified-instance",[&]{auto sample=legacySample();
    sample.clientSha256=newClient;sample.uiSha256=newUi;sample.clientProfile=rc::ProfileId::Public1792618285;
    sample.uiProfile=app::ProfileId::Public1792618285;auto ref=legacyValue(sample);ref["scope"]=referenceScope(scope);
    ref["clientSha256"]=newClient;ref["uiSha256"]=newUi;matchReference(ref,scope,sample,account);
    auto old=sample;old.clientSha256=rc::kClientSha256;old.uiSha256=app::kUiSha256;
    matchReference(ref,scope,old,account);
    ref["clientSha256"]=rc::kClientSha256;ref["uiSha256"]=app::kUiSha256;
    matchReference(ref,scope,sample,account);
    ref["compatibilityContract"]="original-running-code-data-v2";
    ref["clientSha256"]=std::string(64,'a');ref["uiSha256"]=std::string(64,'b');matchReference(ref,scope,sample,account);
    const auto originalPins=ref["pins"];ref["pins"]["owner"]=std::string(64,'0');rejected([&]{matchReference(ref,scope,sample,account);});
    ref["pins"]=originalPins;rejected([&]{matchReference(ref,scope,sample,account+1);});
    ref["scope"]["game"]["birthFileTime"]="100000000999999999";rejected([&]{matchReference(ref,scope,sample,account);});});
  test("legacy-reference-lost-snapshot-upgrades-real-loader",[&]{
    const auto data=root/L"legacy-loss";const auto sample=legacySample();const auto legacy=legacyValue(sample);
    const auto oldPath=data/L"references"/utf16(legacyReferenceKey(scope,account)+".json");
    const auto newPath=data/L"references"/utf16(scopeKey(scope,account)+".json");
    const auto bytes=legacy.dump(2)+"\n";durableCreate(oldPath,bytes);
    need(oldPath!=newPath&&!fp::exists(newPath)&&sample.flags==4,"original-loss-precondition");
    const auto reference=loadCompatibleReference(data,scope,sample,account);
    matchReference(reference,scope,sample,account);
    need(readJson(newPath)==reference&&readLegacyReferenceBytes(oldPath)==bytes,"legacy-copy-or-create");
    need(reference.at("scope")==referenceScope(scope)&&reference.at("captureParent")==legacy.at("captureParent")&&
        reference.at("pins")==legacy.at("pins")&&reference.at("bindingNonce")==legacy.at("bindingNonce")&&
        reference.at("healthyFlags")==8196&&sample.flags==4,"healthy-evidence-rewritten");
    need(reference.at("importedLegacyReferenceSha256")==textHash(bytes)&&
        reference.at("importedLegacyReferenceKey")==legacyReferenceKey(scope,account),"legacy-source-provenance");
    need(!fp::exists(data/L"consumed")&&!fp::exists(data/L"disabled"),"reference-import-changed-budget");
  });
  const auto rejectLegacy=[&](const char* name,const auto& corrupt){test(name,[&]{
    const auto data=root/utf16(name);auto sample=legacySample();auto legacy=legacyValue(sample);corrupt(legacy,sample);
    const auto oldPath=data/L"references"/utf16(legacyReferenceKey(scope,account)+".json");
    const auto bytes=legacy.dump(2);durableCreate(oldPath,bytes);
    rejected([&]{loadCompatibleReference(data,scope,sample,account);});
    need(!fp::exists(data/L"references"/utf16(scopeKey(scope,account)+".json"))&&
        readLegacyReferenceBytes(oldPath)==bytes&&!fp::exists(data/L"consumed"),"rejected-import-created-reference-or-budget");
  });};
  rejectLegacy("legacy-reference-wrong-schema",[](J& r,Snapshot&){r["schema"]="untrusted";});
  rejectLegacy("legacy-reference-unhealthy-history",[](J& r,Snapshot&){r["healthyFlags"]=4;});
  rejectLegacy("legacy-reference-noninteger-healthy-flags",[](J& r,Snapshot&){r["healthyFlags"]=8196.0;});
  rejectLegacy("legacy-reference-wrong-client-profile",[](J& r,Snapshot&){r["clientSha256"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-ui-profile",[](J& r,Snapshot&){r["uiSha256"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-account",[](J& r,Snapshot&){r["accountFingerprint"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-game-pid",[](J& r,Snapshot&){r["scope"]["game"]["pid"]=1;});
  rejectLegacy("legacy-reference-wrong-steam-birth",[](J& r,Snapshot&){r["scope"]["steam"]["birthFileTime"]="1";});
  rejectLegacy("legacy-reference-wrong-cef-scope",[](J& r,Snapshot&){r["scope"]["cef"]["session"]=9;});
  rejectLegacy("legacy-reference-extra-scope-field",[](J& r,Snapshot&){r["scope"]["appid"]=1;});
  rejectLegacy("legacy-reference-malformed-nonce",[](J& r,Snapshot&){r["bindingNonce"]="g7e60aaf-583a-486c-b709-808e25166756";});
  rejectLegacy("legacy-reference-nonce-pins-mismatch",[](J& r,Snapshot&){r["bindingNonce"]="00000000-0000-4000-8000-000000000001";});
  rejectLegacy("legacy-reference-wrong-owner-pin",[](J& r,Snapshot&){r["pins"]["owner"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-record-pin",[](J& r,Snapshot&){r["pins"]["record"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-ui-object-pin",[](J& r,Snapshot&){r["pins"]["uiObject"]=std::string(64,'0');});
  rejectLegacy("legacy-reference-wrong-current-gameid",[](J&,Snapshot& s){s.gameId=10260559608980439040ULL;});
  rejectLegacy("legacy-reference-wrong-current-primary",[](J&,Snapshot& s){s.target.primary=1;});
  rejectLegacy("legacy-reference-wrong-current-foreground",[](J&,Snapshot& s){s.target.foreground=1;});
  rejectLegacy("legacy-reference-unqualified-current-flags",[](J&,Snapshot& s){s.flags=0;});
  test("legacy-reference-full-other-type2-id-not-inferred-from-old-scope",[&]{
    const auto data=root/L"legacy-other-type2";const auto original=legacySample();const auto legacy=legacyValue(original);
    durableCreate(data/L"references"/utf16(legacyReferenceKey(scope,account)+".json"),legacy.dump(2));
    Scope other;other.identities=scope.identities;other.gameId=10260559608980439040ULL;other.appid=2388972698u;
    other.roles[0].born=scope.roles[0].born;other.roles[1].pid=scope.roles[1].pid;
    auto changed=original;changed.gameId=other.gameId;
    need(legacyReferenceKey(other,account)==legacyReferenceKey(scope,account),"old-key-unexpectedly-binds-full-id");
    rejected([&]{loadCompatibleReference(data,other,changed,account);});
    need(!fp::exists(data/L"references"/utf16(scopeKey(other,account)+".json")),"wrong-full-id-imported");
  });
  test("legacy-reference-account-change-does-not-search-old-files",[&]{
    const auto data=root/L"legacy-account-change";const auto sample=legacySample();
    durableCreate(data/L"references"/utf16(legacyReferenceKey(scope,account)+".json"),legacyValue(sample).dump(2));
    need(loadCompatibleReference(data,scope,sample,account+1).is_null(),"old-account-was-guessed");
  });
  test("legacy-reference-existing-current-ref-is-authoritative",[&]{
    const auto data=root/L"legacy-existing-current";const auto sample=legacySample();auto reference=legacyValue(sample);
    reference["scope"]=referenceScope(scope);const auto newPath=data/L"references"/utf16(scopeKey(scope,account)+".json");
    const auto bytes=reference.dump(2);durableCreate(newPath,bytes);
    durableCreate(data/L"references"/utf16(legacyReferenceKey(scope,account)+".json"),"invalid legacy JSON");
    need(loadCompatibleReference(data,scope,sample,account)==reference&&
        readLegacyReferenceBytes(newPath)==bytes&&!reference.contains("importedLegacyReferenceSha256"),"existing-current-ref-replaced");
  });
  test("legacy-reference-valid-old-cannot-replace-invalid-current-ref",[&]{
    const auto data=root/L"legacy-invalid-current";const auto sample=legacySample();const auto legacy=legacyValue(sample);
    durableCreate(data/L"references"/utf16(legacyReferenceKey(scope,account)+".json"),legacy.dump(2));
    auto invalid=legacy;invalid["scope"]=referenceScope(scope);invalid["pins"]["record"]=std::string(64,'0');
    const auto newPath=data/L"references"/utf16(scopeKey(scope,account)+".json");const auto bytes=invalid.dump(2);durableCreate(newPath,bytes);
    rejected([&]{loadCompatibleReference(data,scope,sample,account);});
    need(readLegacyReferenceBytes(newPath)==bytes,"invalid-current-ref-overwritten");
  });
  test("legacy-reference-oversized-source-refused-before-parse",[&]{
    const auto data=root/L"legacy-oversized";const auto sample=legacySample();
    durableCreate(data/L"references"/utf16(legacyReferenceKey(scope,account)+".json"),std::string(2u*1024*1024+1,' '));
    rejected([&]{loadCompatibleReference(data,scope,sample,account);});
    need(!fp::exists(data/L"references"/utf16(scopeKey(scope,account)+".json")),"oversized-source-imported");
  });
  test("wake-watch-boundary-and-cancel",[&]{ymcc::steamOriginal::WakeWatch watch;watch.eventFileTime=1000000;
    need(!watch.allows(999999,true)&&!watch.allows(1000000,false)&&watch.allows(1000000,true),"wake-proof-admission");
    need(watch.allows(1000000+59999*10000ULL,true)&&!watch.allows(1000000+60000*10000ULL,true),"wake-deadline");
    need(watch.observes(1000000+54999*10000ULL,true)&&!watch.observes(1000000+55000*10000ULL,true),"wake-readback-margin");});
  test("wake-dll-deadline-binds-physical-budget",[&]{
    const U64 event=1000000000ULL;
    need(wakeDeadlineTick(event,event+50000*10000ULL,900000)==910000,"dll-deadline-not-capped");
    rejected([&]{wakeDeadlineTick(event,event-1,900000);});
    rejected([&]{wakeDeadlineTick(event,event+60000*10000ULL,900000);});
    rejected([&]{wakeDeadlineTick(event,event,(std::numeric_limits<U64>::max)()-1);});
  });
  test("wake-watch-bounded-reservations",[&]{ymcc::steamOriginal::WakeWatch watch;watch.eventFileTime=1000000;
    for(unsigned i=0;i<watch.maximumProbes;++i)need(watch.reserveProbe(1000000,true),"probe-budget-too-small");
    need(!watch.reserveProbe(1000000,true)&&watch.probes==watch.maximumProbes,"probe-budget-refunded");});
  test("observation-current-cannot-authorize-mutation",[&]{
    gObservationCurrent=[] {return true;};gCurrent=[] {return false;};gCleanupMode=false;
    need(observationCurrent(),"observation-port-not-used");rejected([]{requireTargetMutation();});gObservationCurrent={};
  });
  test("revoked-observation-is-not-full-current",[&]{
    gObservationCurrent=[] {return false;};gCurrent=[] {return true;};gCleanupMode=false;
    need(!observationCurrent(),"observation-revocation-ignored");requireCurrent();gObservationCurrent={};
  });
  test("missing-observation-port-falls-back-to-full-guard",[&]{
    gObservationCurrent={};gCurrent=[] {return false;};need(!observationCurrent(),"fallback-revocation-ignored");
  });
  test("cancel-blocks-new-mutation",[&]{gCurrent=[] {return false;};gCleanupMode=false;gOriginalSteam=[] {return true;};rejected([]{requireTargetMutation();});});
  test("cancel-permits-owned-cleanup",[&]{gCurrent=[] {return false;};gOriginalSteam=[] {return true;};enterCleanup();requireTargetMutation();requireCleanupOwnership();});
  test("cleanup-rejects-original-steam-mismatch",[&]{gOriginalSteam=[] {return false;};rejected([]{requireTargetMutation();});});
  test("cleanup-does-not-reenable-business-current",[&]{gOriginalSteam=[] {return true;};gCleanupMode=true;rejected([]{requireCurrent();});});
  const auto rejectReason=[](const auto& body,const char* reason){
    std::string actual;try{body();}catch(const std::exception& e){actual=e.what();}
    need(actual==reason,"unexpected-state-machine-rejection");
  };
  const auto guards=[](bool business,bool original){
    gCleanupMode=false;gCurrent=[business]{return business;};gOriginalSteam=[original]{return original;};
  };
  test("p1-entry-cancel-before-write-no-dispatch",[&]{
    guards(false,true);bool entryEverExposed=false;unsigned char value=0x11,replacement=0x22;
    rejectReason([&]{writeExact(nullptr,reinterpret_cast<U64>(&value),&replacement,1,&entryEverExposed);},"native-current-guard-revoked");
    need(!entryEverExposed&&value==0x11,"cancel-dispatched-entry-write");
  });
  test("p1-entry-throwing-current-no-dispatch",[&]{
    guards(false,true);gCurrent=[]()->bool{throw std::runtime_error("fixture-current-error");};bool dispatched=false;
    rejectReason([&]{writeExact(nullptr,0,nullptr,0,&dispatched);},"native-current-guard-revoked");
    need(!dispatched,"throwing-guard-dispatched-write");
  });
  test("p1-entry-prior-dispatch-not-refunded-on-cancel",[&]{
    guards(false,true);bool dispatched=true;
    rejectReason([&]{writeExact(nullptr,0,nullptr,0,&dispatched);},"native-current-guard-revoked");
    need(dispatched,"cancel-cleared-existing-write-obligation");
  });
  test("p1-control-cancel-before-create-known-not-dispatched",[&]{
    guards(false,true);H activeCall;bool unknown=false;
    rejectReason([&]{remoteCall(nullptr,0,0,0,activeCall,&unknown);},"native-current-guard-revoked");
    need(!unknown&&!activeCall.good(),"cancel-created-control-thread-or-unknown-result");
  });
  test("p1-load-cancel-before-create-not-attempted",[&]{
    guards(false,true);H activeCall;bool loadAttempted=false;
    rejectReason([&]{remoteCall(nullptr,0,0,0,activeCall,&loadAttempted);},"native-current-guard-revoked");
    need(!loadAttempted&&!activeCall.good(),"cancel-created-loader-thread-or-retained-image-obligation");
  });
  test("p1-control-throwing-current-no-dispatch",[&]{
    guards(false,true);gCurrent=[]()->bool{throw std::runtime_error("fixture-current-error");};H activeCall;bool unknown=false;
    rejectReason([&]{remoteCall(nullptr,0,0,0,activeCall,&unknown);},"native-current-guard-revoked");
    need(!unknown&&!activeCall.good(),"throwing-guard-dispatched-control");
  });
  test("p1-create-failure-still-known-not-dispatched",[&]{
    guards(true,true);H activeCall;bool unknown=false;
    rejectReason([&]{remoteCall(nullptr,0,0,0,activeCall,&unknown);},"own-trial-management-thread");
    need(!unknown&&!activeCall.good(),"failed-create-became-unknown");
  });
  test("p1-post-dispatch-timeout-retains-handle-and-unknown",[&]{
    guards(true,true);H signal(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(signal.good(),"offline-local-event");
    H activeCall;bool unknown=false;std::string reason;
    try{remoteCall(GetCurrentProcess(),reinterpret_cast<U64>(&offlineWaitingControl),reinterpret_cast<U64>(signal.h),0,activeCall,&unknown);}catch(const std::exception& e){reason=e.what();}
    const bool retained=activeCall.good();
    need(SetEvent(signal.h)!=FALSE,"offline-release-local-thread");
    if(activeCall.good())need(WaitForSingleObject(activeCall.h,2000)==WAIT_OBJECT_0,"offline-local-thread-join");
    need(reason=="management-thread-timeout-retain-input-and-dll"&&unknown&&retained,"timeout-lost-dispatch-obligation");
  });
  test("p1-pre-attach-cancel-allows-original-owned-disarm-io",[&]{
    guards(false,true);enterCleanup();
    unsigned char packetByte=0x11,replacement=0x22;bool written=false,dispatched=false;H activeCall;
    writeExact(GetCurrentProcess(),reinterpret_cast<U64>(&packetByte),&replacement,1,&written);
    const auto code=remoteCall(GetCurrentProcess(),reinterpret_cast<U64>(&offlineCompletedControl),0,2000,activeCall,&dispatched);
    need(packetByte==0x22&&written&&dispatched&&!activeCall.good()&&code==0&&!current(),"cancel-blocked-owned-disarm-or-reenabled-business");
  });
  test("p1-pre-attach-cleanup-wrong-original-steam-refused",[&]{
    guards(false,false);
    rejectReason([]{enterCleanup();},"original-steam-cleanup-identity-unavailable");
    need(!gCleanupMode,"wrong-owner-entered-cleanup");
  });
  test("p1-cleanup-write-lost-ownership-no-dispatch",[&]{
    guards(false,true);enterCleanup();gOriginalSteam=[]{return false;};bool written=false;
    rejectReason([&]{writeExact(nullptr,0,nullptr,0,&written);},"original-steam-cleanup-identity-unavailable");
    need(!written,"cleanup-write-after-lost-original-ownership");
  });
  test("p1-cleanup-control-lost-ownership-no-dispatch",[&]{
    guards(false,true);enterCleanup();gOriginalSteam=[]{return false;};bool dispatched=false;H activeCall;
    rejectReason([&]{remoteCall(nullptr,0,0,0,activeCall,&dispatched);},"original-steam-cleanup-identity-unavailable");
    need(!dispatched&&!activeCall.good(),"cleanup-control-after-lost-original-ownership");
  });
  gCurrent={};gOriginalSteam={};gCleanupMode=false;
  test("wake-needs-confirmed-absolute-filetime",[&]{J e={{"trigger",{{"kind","wake"},{"generation",1},{"id","missing-episode"}}}};rejected([&]{consume(e,scope,root);});});
  J wake={{"trigger",{{"kind","wake"},{"generation",1},{"id","episode-a"},{"powerEventFileTime","134370000000000001"}}}};
  test("first-real-wake-durably-consumed",[&]{consume(wake,scope,root);});
  test("same-physical-wake-no-gen-reset-replay",[&]{wake["trigger"]["generation"]=999;wake["trigger"]["id"]="episode-a-after-ymcc-restart";rejected([&]{consume(wake,scope,root);});});
  test("new-physical-wake-allows-reset-process-counter",[&]{wake["trigger"]["generation"]=1;wake["trigger"]["id"]="episode-b";wake["trigger"]["powerEventFileTime"]="134370000000000002";consume(wake,scope,root);});
  J startup={{"trigger",{{"kind","startup"},{"generation",0},{"id","startup-a"}}}};
  test("startup-actual-attempt-consumed-once",[&]{consume(startup,scope,root);});
  test("startup-no-new-event-id-refund",[&]{startup["trigger"]["id"]="startup-after-restart";rejected([&]{consume(startup,scope,root);});});
  test("durable-files-retained-after-refusal",[&]{std::size_t count=0;for(const auto& row:fp::directory_iterator(root/L"consumed")){++count;const auto value=readJson(row.path());need(value.at("refunded")==false,"unexpected-refund");}need(count==6,"ledger-unexpected-count");});
  test("embedded-target-dll-original-sha",[&]{need(digest(embedded::image,sizeof embedded::image)==embedded::sha256,"embedded-drift");});
  test("decimal-overflow-refused",[&]{rejected([]{decimal(J("18446744073709551616"));});});
  test("identity-birth-path-normalized-for-dedupe",[&]{const auto normalized=normalizedIdentity(J{{"pid",1},{"birth","000123"},{"path","D:\\OFFLINE-Fixture\\WUXIA.exe"},{"session",1}});need(normalized.at("birthFileTime")=="123"&&normalized.at("path")=="d:\\offline-fixture\\wuxia.exe","identity-canonicalization");});
  test("private-account-not-exported-on-cancel",[&]{J e={{"dataRoot",utf8((root/L"private-reject").wstring())},{"logRoot",utf8((root/L"private-log").wstring())},{"account64","1234567890987654321"},{"trigger",{{"kind","startup"},{"generation",0},{"id","private-reject"},{"rulesEpoch",1}}}};const auto value=handle(e,{},[]{return false;});need(value.at("status")=="refused"&&value.dump().find("1234567890987654321")==std::string::npos,"private-account-exported");need(!active(),"active-not-released-after-safe-refusal");});
  test("clearable-evidence-does-not-reset-persistent-consumption",[&]{
    const auto persistent=root/L"Runtime"/L"SteamOriginalRecovery",receipts=root/L"SteamOriginalRecovery";
    J ticket={{"trigger",{{"kind","startup"},{"generation",0},{"id","layout-original"}}}};
    consume(ticket,scope,persistent);
    J event={{"dataRoot",utf8(persistent.wstring())},{"evidenceRoot",utf8(receipts.wstring())},
      {"logRoot",utf8((root/L"layout-log").wstring())},{"account64",std::to_string(account)},
      {"trigger",{{"kind","startup"},{"generation",0},{"id","layout-read-only"},{"rulesEpoch",1}}}};
    const auto first=handle(event,{},[]{return false;});
    need(first.at("status")=="refused"&&fp::exists(receipts/L"last-recovery-summary.json"),"evidence-root-not-used");
    need(!fp::exists(persistent/L"last-recovery-summary.json")&&!fp::exists(persistent/L"monitor"),"receipt-leaked-to-runtime");
    need(readJson(receipts/L"last-recovery-summary.json").at("storage").at("explicitEvidenceRoot")==true,"layout-receipt-missing");
    need(fp::remove(receipts/L"last-recovery-summary.json")&&fp::remove(receipts/L"monitor"/L"status.json")&&
      fp::remove(receipts/L"monitor")&&fp::remove(receipts),"clearable-receipt-directory-locked");
    ticket["trigger"]["id"]="layout-after-log-clear";rejected([&]{consume(ticket,scope,persistent);});
    const auto second=handle(event,{},[]{return false;});
    need(second.at("status")=="refused"&&fp::exists(receipts/L"last-recovery-summary.json"),"cleared-evidence-not-recreated");
    std::size_t retained=0;for(const auto& file:fp::directory_iterator(persistent/L"consumed"))
      if(file.is_regular_file())++retained;
    need(retained==2,"log-clear-refunded-or-added-consumption");
  });
  gCurrent={};gOriginalSteam={};gCleanupMode=false;
  const U64 suspendRecord=0x10000,suspendFull=10260559608980439040ULL;
  const auto suspendRead=[&](std::uint32_t raw,bool secondChanged=false,int failAt=0,bool throws=false){
    return rc::Read([&,raw,secondChanged,failAt,throws,calls=0](U64 at,void* out,std::size_t n)mutable{
      ++calls;if(throws)throw std::runtime_error("suspend-diagnostic-fixture");if(failAt&&calls==failAt)return false;
      if(at==suspendRecord+8&&n==sizeof(U64)){const auto id=suspendFull+((secondChanged&&calls==3)?1:0);std::memcpy(out,&id,n);return true;}
      if(at==suspendRecord+0x2cc&&n==sizeof raw){std::memcpy(out,&raw,n);return true;}return false;});
  };
  test("suspend-diagnostic-zero-is-not-a-resume-ticket",[&]{const auto v=observeSuspendDiagnostic(suspendRead(0),suspendRecord,suspendFull);
    need(v.at("available")==true&&v.at("raw")==0&&v.at("resumePredicateNonzero")==false&&v.at("participatesInRecoveryAdmission")==false,"suspend-zero-diagnostic");});
  test("suspend-diagnostic-nonzero-stays-diagnostic",[&]{const auto v=observeSuspendDiagnostic(suspendRead(2),suspendRecord,suspendFull);
    need(v.at("available")==true&&v.at("raw")==2&&v.at("resumePredicateNonzero")==true&&v.at("actualOsSuspensionVerified")==false,"suspend-nonzero-diagnostic");});
  test("suspend-diagnostic-unknown-raw-not-normalized",[&]{const auto v=observeSuspendDiagnostic(suspendRead(99),suspendRecord,suspendFull);
    need(v.at("available")==true&&v.at("raw")==99&&v.at("readIsAtomic")==false,"suspend-unknown-raw");});
  test("suspend-diagnostic-failed-read-is-unavailable",[&]{const auto v=observeSuspendDiagnostic(suspendRead(2,false,2),suspendRecord,suspendFull);
    need(v.at("available")==false&&v.at("raw").is_null(),"suspend-failure-authorized");});
  test("suspend-diagnostic-full-id-change-is-unavailable",[&]{const auto v=observeSuspendDiagnostic(suspendRead(2,true),suspendRecord,suspendFull);
    need(v.at("available")==false&&v.at("raw").is_null(),"suspend-raced-id-authorized");});
  test("suspend-diagnostic-exception-and-overflow-stay-optional",[&]{need(observeSuspendDiagnostic(suspendRead(2,false,0,true),suspendRecord,suspendFull).is_null(),"suspend-read-exception");
    need(observeSuspendDiagnostic(suspendRead(2),std::numeric_limits<U64>::max(),suspendFull).is_null(),"suspend-offset-overflow");});
  test("suspend-diagnostic-never-changes-native-flags",[&]{Scope scope;Snapshot sample;sample.record={{"suspendStateDiagnostic",observeSuspendDiagnostic(suspendRead(2),suspendRecord,suspendFull)}};
    for(const auto flags:{4u,8196u}){sample.flags=flags;const auto v=nativeState(scope,sample);need(v.at("flags")==flags&&v.at("running")==bool(flags==8196)&&v.at("suspendStateDiagnostic").at("raw")==2,"suspend-field-changed-running");}});
  test("postlogon-source-diagnostic-failed-read-remains-optional",[&]{
    const rc::Read read=[](U64,void*,std::size_t){return false;};
    const auto v=observePostLogonSourceDiagnostic(read,0x138000000ULL,0x2000000,0x400000);
    need(v.at("available")==false&&v.at("reason")=="read-failed"&&v.at("userStateRaw").is_null()&&
        v.at("postLogonProgressRaw").is_null()&&v.at("participatesInRecoveryAdmission")==false,"postlogon-diagnostic-failure-gated");
    Scope scope;Snapshot sample;sample.record={{"postLogonSourceDiagnostic",v}};
    for(const auto flags:{4u,8196u}){sample.flags=flags;const auto state=nativeState(scope,sample);
      need(state.at("flags")==flags&&state.at("running")==bool(flags==8196)&&
          state.at("postLogonSourceDiagnostic")==v,"postlogon-diagnostic-changed-running");}});
  test("postlogon-source-diagnostic-exception-and-overflow-remain-optional",[&]{
    const rc::Read throwing=[](U64,void*,std::size_t)->bool{throw std::runtime_error("owned-fixture-read-failure");};
    const auto failed=observePostLogonSourceDiagnostic(throwing,0x138000000ULL,0x2000000,0x400000);
    need(failed.at("available")==false&&failed.at("reason")=="read-exception"&&
        failed.at("getterBytesVerified")==false&&failed.at("callbackPayloadObserved")==false,"postlogon-read-exception-authorized");
    unsigned reads=0;const rc::Read counted=[&](U64,void*,std::size_t){++reads;return false;};
    const auto overflow=observePostLogonSourceDiagnostic(counted,0x138000000ULL,0x2000000,std::numeric_limits<U64>::max());
    need(overflow.at("available")==false&&reads==0&&overflow.at("fullLoginHeapGenerationLeaseProven")==false,
        "postlogon-overflow-read-or-authority");});
  test("semi-static-source-failure-is-optional-and-does-not-change-running",[&]{
    const rc::Read read=[](U64,void*,std::size_t){return false;};
    const auto v=observeSemiStaticSource(read,0x138000000ULL,0x2000000,0x13a000000ULL,0x2000000,0x400000);
    need(v.at("available")==false&&v.at("cmStateRaw").is_null()&&v.at("uiLocalPipe").is_null()&&
        v.at("participatesInRecoveryAdmission")==false&&v.at("actualReconnectExecuted")==false,
        "semi-static-failure-authorized");
    Scope scope;Snapshot sample;sample.record={{"semiStaticSource",v}};
    for(const auto flags:{4u,8196u}){sample.flags=flags;const auto state=nativeState(scope,sample);
      need(state.at("flags")==flags&&state.at("running")==bool(flags==8196)&&state.at("semiStaticSource")==v,
          "semi-static-source-changed-running");}});
  test("semi-static-source-exception-and-overflow-remain-optional",[&]{
    const rc::Read throwing=[](U64,void*,std::size_t)->bool{throw std::runtime_error("fixture-read");};
    const auto failed=observeSemiStaticSource(throwing,0x138000000ULL,0x2000000,0x13a000000ULL,0x2000000,0x400000);
    need(failed.at("available")==false&&failed.at("callbackQueueConsumed")==false,"semi-static-exception-authorized");
    unsigned reads=0;const rc::Read counted=[&](U64,void*,std::size_t){++reads;return false;};
    const auto overflow=observeSemiStaticSource(counted,0x138000000ULL,0x2000000,0x13a000000ULL,0x2000000,
        std::numeric_limits<U64>::max());
    need(overflow.at("available")==false&&reads==0&&overflow.at("fullLoginHeapGenerationLeaseProven")==false,
        "semi-static-overflow-read-or-authority");});
  test("source-context-witness-default-unavailable",[&]{SourceContextInit witness;BoundLive steam;steam.pid=11;steam.born=22;steam.path=L"C:\\owned\\steam.exe";
    need(!witness.matches(steam),"empty-witness-matched");});
  test("source-context-witness-binds-full-steam-lifetime",[&]{BoundLive steam;steam.pid=11;steam.born=22;steam.path=L"C:\\owned\\steam.exe";
    SourceContextInit witness{11,22,steam.path};need(witness.matches(steam),"exact-witness-rejected");++steam.pid;need(!witness.matches(steam),"changed-pid-accepted");--steam.pid;
    ++steam.born;need(!witness.matches(steam),"changed-birth-accepted");--steam.born;steam.path=L"C:\\different\\steam.exe";need(!witness.matches(steam),"changed-path-accepted");});
  test("source-context-witness-cannot-cross-os-worker",[&]{const auto before=gSourceContextInit;gSourceContextInit={11,22,L"C:\\owned\\steam.exe"};
    bool empty=false;std::thread worker([&]{empty=!gSourceContextInit.steamPid&&!gSourceContextInit.steamBirth&&gSourceContextInit.steamPath.empty();});worker.join();gSourceContextInit=before;need(empty,"tls-witness-crossed-worker");});
  test("source-context-real-owned-thread-selected-and-revalidated",[&]{
    H ready(CreateEventW(nullptr,TRUE,FALSE,nullptr)),stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    need(ready.good()&&stop.good(),"owned-thread-test-events");std::atomic<bool> named{false};
    std::thread worker([&]{named=SetThreadDescription(GetCurrentThread(),L"IPC:CSteamEngine")>=0;SetEvent(ready.h);WaitForSingleObject(stop.h,5000);});
    bool success=false;std::string error;std::optional<SourceThreadCandidate> candidate;
    try{need(WaitForSingleObject(ready.h,2000)==WAIT_OBJECT_0&&named,"owned-thread-test-ready");J detail;
      candidate=findSourceThreadCandidate(GetCurrentProcessId(),detail);need(bool(candidate),"owned-thread-test-select");
      need(currentSourceThread(*candidate,GetCurrentProcessId()),"owned-thread-test-revalidate");
      need(!currentSourceThread(*candidate,GetCurrentProcessId()+1),"owned-thread-test-foreign-owner");
      ++candidate->birth;need(!currentSourceThread(*candidate,GetCurrentProcessId()),"owned-thread-test-birth-change");--candidate->birth;
      need(detail.at("matchingNames")==1&&detail.at("queriedThreads").get<unsigned>()>0,"owned-thread-test-detail");success=true;
    }catch(const std::exception& e){error=e.what();}SetEvent(stop.h);worker.join();
    need(success,error.empty()?"owned-thread-selection-failed":error.c_str());
    need(candidate&&!currentSourceThread(*candidate,GetCurrentProcessId()),"owned-thread-test-exit-rejected");
  });
  test("source-context-candidate-rejects-invalid-thread",[&]{SourceThreadCandidate c;c.tid=1;c.birth=1;need(!currentSourceThread(c,GetCurrentProcessId()),"invalid-thread-accepted");});
  test("query-completion-already-done-skips-event",[&]{std::atomic<bool> done{true};waitQueryReturn(done,nullptr,0);});
  test("query-completion-signaled-without-result-refused",[&]{std::atomic<bool> done{false};H event(CreateEventW(nullptr,TRUE,TRUE,nullptr));need(event.good(),"wait-fixture-event");
    rejected([&]{waitQueryReturn(done,event.h,0);});});
  test("query-completion-timeout-refused",[&]{std::atomic<bool> done{false};H event(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(event.good(),"wait-fixture-event");
    rejected([&]{waitQueryReturn(done,event.h,0);});});
  test("query-completion-invalid-event-refused",[&]{std::atomic<bool> done{false};rejected([&]{waitQueryReturn(done,nullptr,0);});});
  test("query-completion-real-event-wakes-for-owned-fixture",[&]{std::atomic<bool> done{false};H event(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(event.good(),"wait-fixture-event");
    std::thread worker([&]{done=true;SetEvent(event.h);});bool waited=false;
    try{waitQueryReturn(done,event.h,1000);waited=true;}catch(...){}worker.join();need(waited&&done,"event-did-not-wake");});
  test("query-completion-does-not-bypass-revoked-current",[&]{const auto previous=gCurrent;gCurrent=[]{return false;};std::atomic<bool> done{true};bool wrote=false,refused=false;
    try{waitQueryReturn(done,nullptr,0);requireCurrent();wrote=true;}catch(const std::runtime_error&){refused=true;}gCurrent=previous;need(refused&&!wrote,"event-granted-current");});
  test("execution-probe-keeps-allow-reject-exception-semantics",[&]{
    const auto previous=gCurrent;J out;{ExecutionProbeScope probe(out,"test");
      gCurrent=[] {return true;};need(current(),"probe-allow");
      gCurrent=[] {return false;};need(!current(),"probe-reject");
      gCurrent=[]()->bool {throw std::runtime_error("guard-failure");};need(!current(),"probe-exception");}
    gCurrent=previous;const auto& c=out.at("executionDiagnostics").at("test").at("currentGuard");
    need(c.at("calls")==3&&c.at("accepted")==1&&c.at("rejected")==2&&c.at("exceptions")==1,"probe-return-counts");
  });
  test("execution-probe-restores-nested-and-unwind-scope",[&]{J out;
    {ExecutionProbeScope outer(out,"outer");need(gExecutionProbe==&outer.value,"probe-outer");
      try{ExecutionProbeScope inner(out,"inner");need(gExecutionProbe==&inner.value,"probe-inner");throw std::runtime_error("unwind");}
      catch(const std::runtime_error&){}need(gExecutionProbe==&outer.value,"probe-outer-restored");}
    need(!gExecutionProbe&&out.at("executionDiagnostics").size()==2,"probe-disabled-after-scope");
  });
  test("execution-probe-counts-debug-event-kinds-without-admission",[&]{J out;
    {ExecutionProbeScope probe(out,"context");noteContextDebugEvent(CREATE_PROCESS_DEBUG_EVENT);noteContextDebugEvent(CREATE_THREAD_DEBUG_EVENT);
      noteContextDebugEvent(LOAD_DLL_DEBUG_EVENT);noteContextDebugEvent(EXCEPTION_DEBUG_EVENT);noteContextDebugEvent(42);}
    const auto& d=out.at("executionDiagnostics").at("context");need(d.at("debugEvents").at("createProcess")==1&&
      d.at("debugEvents").at("createThread")==1&&d.at("debugEvents").at("loadDll")==1&&d.at("debugEvents").at("exception")==1&&
      d.at("otherDebugEvents")==1&&d.at("participatesInAdmission")==false,"probe-event-counts");
  });
  test("execution-probe-rejected-guard-still-prevents-write",[&]{
    const auto previous=gCurrent;gCurrent=[]{return false;};J out;bool wrote=false,refused=false;
    {ExecutionProbeScope probe(out,"revoked");try{requireCurrent();wrote=true;}catch(const std::runtime_error&){refused=true;}}
    gCurrent=previous;need(refused&&!wrote&&out.at("executionDiagnostics").at("revoked").at("currentGuard").at("rejected")==1,"probe-authority-changed");
  });
  test("execution-probe-disabled-keeps-no-output",[&]{need(!gExecutionProbe,"probe-default-active");const auto previous=gCurrent;
    gCurrent=[] {return true;};need(current(),"probe-disabled-allow");gCurrent=[] {return false;};need(!current(),"probe-disabled-reject");
    gCurrent=[]()->bool {throw std::runtime_error("guard-failure");};need(!current(),"probe-disabled-throw");gCurrent=previous;noteContextDebugEvent(1);
    need(!gExecutionProbe,"probe-disabled-published");
  });
  test("worker-phase-cost-observes-success-and-unwind",[&]{
    WorkerCost cost;{WorkerTimer timer(cost);need(current(),"clock-fixture-current");}
    try{WorkerTimer timer(cost);throw std::runtime_error("clock-fixture-unwind");}catch(const std::runtime_error&){}
    const auto value=workerCostJson(cost);need(value.at("calls")==2&&value.at("clockFailures")==0&&
        value.at("workerCycles").get<U64>()>0&&value.at("wallMs").get<double>()>=0,"worker-clock-collection");
  });
  test("worker-phase-cost-six-existing-timing-keys",[&]{J summary;
    for(const auto* name:{"initialSnapshot","normalQuerySetup","sourceContext","preDispatchSnapshot","driverAndCleanup","postRecoverySnapshot"}){
      const auto began=workerStamp();noteWorkerPhase(summary,name,began);
      need(summary.at("phaseCost").at(name).contains("workerCpu100ns")&&
          summary.at("phaseCost").at(name).contains("workerCycles"),"phase-clock-key-missing");}
    need(summary.at("phaseCost").size()==6&&!summary.contains("timingsMs"),"phase-diagnostics-changed-existing-wall-metrics");
  });
  test("worker-phase-cost-does-not-authorize-revoked-guard",[&]{
    const auto previous=gCurrent;gCurrent=[]{return false;};WorkerCost cost;bool refused=false;
    try{WorkerTimer timer(cost);requireCurrent();}catch(const std::runtime_error& e){refused=std::string(e.what())=="native-current-guard-revoked";}
    gCurrent=previous;need(refused&&cost.calls==1,"clock-skipped-revoked-guard");
  });
  // These faults use the actual typed readers, real handle(), and real own-file
  // persistence. No scene process is opened: the hook precedes openScope().
  const auto injectedReadFailure=[&](const char* label,std::function<void(J&)> fault){
    struct Restore {
      Current current,observation;SourceContextInit witness;
      ~Restore(){gOfflineTypedReadFault={};gCurrent=std::move(current);gObservationCurrent=std::move(observation);gSourceContextInit=std::move(witness);}
    } restore{gCurrent,gObservationCurrent,gSourceContextInit};
    const auto runtime=root/utf16(label)/L"runtime",evidence=root/utf16(label)/L"evidence";
    J events=J::array(),receipt;bool escaped=false;
    gSourceContextInit={11,22,L"D:/offline-fixture/steam.exe"};
    gOfflineTypedReadFault=std::move(fault);
    try{receipt=handle(J{{"dataRoot",utf8(runtime.wstring())},{"logRoot",utf8(evidence.wstring())},{"evidenceRoot",utf8(evidence.wstring())}},
        [&](const J& row){events.push_back(row);},[]{return true;},{},0);}catch(...){escaped=true;}
    const bool witnessCleared=!gSourceContextInit.steamPid&&!gSourceContextInit.steamBirth&&gSourceContextInit.steamPath.empty();
    J result={{"escaped",escaped},{"receipt",receipt},{"events",events},{"activeAfter",active()},
        {"witnessCleared",witnessCleared},{"runtime",utf8(runtime.wstring())},{"evidence",utf8(evidence.wstring())}};
    if(fp::exists(evidence/L"last-recovery-summary.json"))result["durableSummary"]=readJson(evidence/L"last-recovery-summary.json");
    return result;
  };
  const auto requireFailureReceipt=[&](const J& result,const char* reason){
    need(result.at("escaped")==false,"typed-rejection-escaped-engine-handler");
    const auto& receipt=result.at("receipt");
    need(receipt.at("status")=="refused"&&receipt.at("reason")==reason&&receipt.at("nativeRecovered")==false&&
        receipt.at("repairAccepted")==false,"typed-rejection-reported-as-recovery");
    need(result.at("activeAfter")==false&&result.at("witnessCleared")==true,"failed-job-retained-active-or-source-witness");
    need(result.contains("durableSummary")&&result.at("durableSummary")==receipt,"failure-summary-not-durable");
    bool finished=false;for(const auto& row:result.at("events"))if(row.value("event",std::string{})=="job-finished"){
        need(row.at("result")==receipt,"finished-event-differs-from-failure");finished=true;}
    need(finished,"failure-job-finished-not-emitted");
  };
  test("typed-ui-reader-rejection-keeps-engine-summary",[&]{
    unsigned calls=0;
    const auto result=injectedReadFailure("typed-ui",[&](J&){
      const app::Read unavailable=[&](U64,void*,std::size_t){++calls;return false;};
      app::detail::Reader reader(unavailable);(void)reader.u64(0x10000);
    });
    need(calls==1,"actual-ui-reader-not-exercised");requireFailureReceipt(result,"memory-read-failed");
    need(result.at("receipt").at("failureDiagnostic").at("component")=="SteamUI","ui-rejection-component-lost");
    need(!fp::exists(absolute(result.at("runtime"))/L"consumed"),"reader-only-failure-consumed-right");
  });
  test("typed-record-reader-rejection-keeps-engine-summary",[&]{
    unsigned calls=0;
    const auto result=injectedReadFailure("typed-record",[&](J&){
      const rc::Read unavailable=[&](U64,void*,std::size_t){++calls;return false;};
      rc::detail::Reader reader(unavailable);(void)reader.u64(0x10000);
    });
    need(calls==1,"actual-record-reader-not-exercised");requireFailureReceipt(result,"memory-read-failed");
    need(result.at("receipt").at("failureDiagnostic").at("component")=="steamclient","record-rejection-component-lost");
  });
  test("typed-rejection-preserves-real-consumption-and-disable",[&]{
    const auto persistent=root/L"typed-consumed"/L"runtime";
    J ticket={{"trigger",{{"kind","startup"},{"generation",1},{"id","typed-consume-original"}}}};
    const auto key=textHash(life(scope).dump());
    const auto result=injectedReadFailure("typed-consumed",[&](J& summary){
      consume(ticket,scope,persistent);summary["consumed"]=true;summary["lifeKey"]=key;
      const app::Read unavailable=[](U64,void*,std::size_t){return false;};
      app::detail::Reader reader(unavailable);(void)reader.u64(0x10000);
    });
    requireFailureReceipt(result,"memory-read-failed");
    need(result.at("receipt").at("consumed")==true,"failure-refunded-consumption");
    const auto disabled=readJson(persistent/L"disabled"/utf16(key+".v2.json"));
    need(disabled.at("consumed")==true&&disabled.at("noAutomaticRetry")==true,"typed-failure-disable-not-durable");
    const auto previous=gCurrent;gCurrent=[]{return true;};bool duplicate=false;
    ticket["trigger"]["id"]="typed-consume-different-event";
    try{consume(ticket,scope,persistent);}catch(const std::runtime_error& error){duplicate=std::string(error.what())=="event-already-consumed";}
    gCurrent=previous;need(duplicate,"new-event-refunded-startup-after-typed-rejection");
    unsigned count=0;for(const auto& row:fp::directory_iterator(persistent/L"consumed")){
      ++count;need(readJson(row.path()).at("refunded")==false,"typed-failure-ledger-refunded");}
    need(count==2,"typed-failure-ledger-changed");
  });
  test("std-diagnostic-details-survive-native-failure-boundary",[&]{
    const J detail={{"stage","kept-original-diagnostic"},{"code",7}};
    const auto result=injectedReadFailure("typed-std",[&](J&){throw DiagnosticFailure("original-diagnostic",detail);});
    requireFailureReceipt(result,"original-diagnostic");
    need(result.at("receipt").at("failureDiagnostic")==detail,"standard-diagnostic-double-wrapped");
  });
  test("unknown-cpp-exception-keeps-durable-engine-summary",[&]{
    const auto result=injectedReadFailure("typed-unknown",[](J&){throw unsigned{41};});
    requireFailureReceipt(result,"native-engine-exception");
    need(result.at("receipt").at("failureDiagnostic").at("exceptionClass")=="unknown-cpp","unknown-cpp-not-classified");
  });
  J executionMicro;test("execution-module-pair-real-consumer-micro",[&]{executionMicro=offlineModulePairMicro();need(executionMicro.at("success")==true,"module-pair-micro-failed");});
  const auto passed=std::count_if(cases.begin(),cases.end(),[](const auto& c){return c.at("passed")==true;});
  return{{"schema","ymcc.steam-original-recovery-engine-offline.v1"},{"executionModulePairMicro",executionMicro},{"tests",cases.size()},{"passed",passed},{"success",static_cast<std::size_t>(passed)==cases.size()},{"cases",cases},{"sceneProcessOpens",0},{"debugAttaches",0},{"extraProductProcesses",0},{"ledgerEvidence",utf8(root.wstring())},{"liveRecoveryVerified",false}};
}
Json readonly_file_hash_diagnostic(){
  J result={{"schema","ymcc.formal-file-hash-worker-cpu-diagnostic.v1"},{"authority","diagnostic-only-no-admission"},
      {"memoryWrites",0},{"steamGameMemoryReads",0},{"debugAttaches",0},{"recoveryAttempts",0},{"snapshotCalls",0},
      {"fileHashConsumer","baseline full-file helper; module snapshots reuse qualified-buffer digest"},{"representsCurrentModuleSnapshotDigestConsumer",false},{"rounds",J::array()}};
  try{
    const std::array<std::wstring,2> paths{L"D:\\Game\\Steam\\steamclient64.dll",L"D:\\Game\\Steam\\SteamUI.dll"};
    const std::array<const char*,2> names{"clientFullFileHash","uiFullFileHash"};
    const std::array<std::string,2> expected{rc::kClientSha256,app::kUiSha256};
    std::array<std::uintmax_t,2> sizes{};std::array<fp::file_time_type,2> stamps{};
    for(std::size_t i=0;i<paths.size();++i){sizes[i]=fp::file_size(paths[i]);stamps[i]=fp::last_write_time(paths[i]);}
    const auto event=readJson(L"C:\\Users\\DaVe\\AppData\\Local\\YeManCC\\SteamOriginalRecovery\\last-event.json");
    auto steam=bindLive(normalizedIdentity(event.at("steam")),PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE);
    need(rw::pathKey(steam.path)==L"d:\\game\\steam\\steam.exe","fixed-steam-module-root");
    for(unsigned round=0;round<3;++round){J row={{"index",round}};
      need(rw::exactProcess(steam.handle.h,steam.pid,steam.born,steam.path,steam.session),"module-pair-steam-changed-before");
      WorkerCost moduleCost;rw::Module client,ui;
      {WorkerTimer timer(moduleCost);need(originalImagePair(steam.pid,paths[0],paths[1],client,ui),"fixed-live-module-pair");}
      row["modulePair"]=workerCostJson(moduleCost);row["hashOrder"]=round%2==0?"client,ui":"ui,client";
      for(unsigned order=0;order<2;++order){const std::size_t i=round%2==0?order:1-order;
        WorkerCost cost;std::string observed;{WorkerTimer timer(cost);observed=fileHash(paths[i]);}
        need(observed==expected[i]&&sizes[i]==fp::file_size(paths[i])&&stamps[i]==fp::last_write_time(paths[i]),
            "full-fixed-image-hash-or-stamp-changed");
        row[names[i]]=workerCostJson(cost);row[names[i]]["bytes"]=sizes[i];row[names[i]]["sha256"]=observed;
      }
      need(rw::exactProcess(steam.handle.h,steam.pid,steam.born,steam.path,steam.session)&&
          normalizedIdentity(readJson(L"C:\\Users\\DaVe\\AppData\\Local\\YeManCC\\SteamOriginalRecovery\\last-event.json").at("steam"))==normalizedIdentity(event.at("steam")),
          "module-pair-steam-changed-after");
      row["sameOriginalSteamBeforeAndAfter"]=true;result["rounds"].push_back(std::move(row));
    }
    result["success"]=true;result["steam"]=event.at("steam");
    result["measurementScope"]="baseline full-file helper on fixed client/ui files; current module snapshots use qualified-buffer digest; original Steam module pair metadata only; no typed memory snapshot or host business Current measurement";
    result["osFileCacheControlled"]=false;result["fieldRecoveryCpuClaim"]=false;
  }catch(const std::exception& e){result["success"]=false;result["reason"]=e.what();}return result;
}
}
#endif
