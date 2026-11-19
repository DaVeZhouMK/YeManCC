#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <atomic>
#include <winhttp.h>
static std::atomic<bool> selectedVideoNoNetwork{false};
static std::atomic<unsigned> selectedVideoNetworkAttempts{0};
static HINTERNET WINAPI selectedVideoHttpOpen(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags){
 if(selectedVideoNoNetwork.load()){++selectedVideoNetworkAttempts;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
 return WinHttpOpen(agent,access,proxy,bypass,flags);
}
#define WinHttpOpen selectedVideoHttpOpen
static std::atomic<bool> shortcutRegistryFixture{false};
static std::atomic<DWORD> shortcutFixtureAccount{17};
static std::atomic<unsigned> shortcutAccountReads{0};
static std::atomic<DWORD> shortcutFixtureAccountAfter{0};
static std::atomic<LSTATUS> shortcutFixtureAccountError{ERROR_SUCCESS},shortcutFixtureRootError{ERROR_SUCCESS};
static std::wstring shortcutFixtureSteamRoot;
static LSTATUS WINAPI shortcutFixtureRegGetValue(HKEY key,LPCWSTR subkey,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD size){
 if(shortcutRegistryFixture.load()&&key==HKEY_CURRENT_USER&&subkey&&name){
  const bool account=std::wstring_view(subkey)==L"Software\\Valve\\Steam\\ActiveProcess"&&std::wstring_view(name)==L"ActiveUser";
  const bool root=std::wstring_view(subkey)==L"Software\\Valve\\Steam"&&std::wstring_view(name)==L"SteamPath";
  if(account||root){const auto reads=account?++shortcutAccountReads:shortcutAccountReads.load();const auto failure=account?shortcutFixtureAccountError.load():shortcutFixtureRootError.load();if(failure!=ERROR_SUCCESS)return failure;const DWORD active=account&&reads>=2&&shortcutFixtureAccountAfter.load()?shortcutFixtureAccountAfter.load():shortcutFixtureAccount.load();const DWORD bytes=account?sizeof active:DWORD((shortcutFixtureSteamRoot.size()+1)*sizeof(wchar_t));
   if(!size)return ERROR_INVALID_PARAMETER;if(type)*type=account?REG_DWORD:REG_SZ;const DWORD capacity=*size;*size=bytes;
   if(!data)return ERROR_SUCCESS;if(capacity<bytes)return ERROR_MORE_DATA;
   memcpy(data,account?static_cast<const void*>(&active):static_cast<const void*>(shortcutFixtureSteamRoot.c_str()),bytes);return ERROR_SUCCESS;
  }
 }
 return RegGetValueW(key,subkey,name,flags,type,data,size);
}
#define RegGetValueW shortcutFixtureRegGetValue
#include "../native/shared_game_media_download_worker.h"
#undef RegGetValueW
#undef WinHttpOpen
#ifndef YMCC_SOURCE_IDLE_WAIT_PORT_INCLUDED
#define YMCC_SOURCE_IDLE_WAIT_PORT_INCLUDED

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>

// Test-only observation port; inline before recoveryObservedWait and before any
// WaitForMultipleObjects macro substitution. Requires C++17 inline variables.
namespace source_idle_wait_port {
inline std::atomic<bool> enabled{false}, captured{false};
inline std::atomic<DWORD> timeout{0}, result{WAIT_FAILED};
inline std::atomic<ULONGLONG> enteredTick{0}, returnedTick{0};
inline std::atomic<unsigned> signalFailures{0};
inline HANDLE entered = nullptr, returned = nullptr;

// Both events are borrowed: the test creates/owns them. With all prior callers
// joined, initialize/reset observations and events, assign both handles, then
// publish enabled.store(true, std::memory_order_release). Enable only for the
// intended original owner: this port identifies eligibility solely by count==2.
// captured is a one-shot claim, not an entry/completion publication. Read the
// observations with acquire after their event, or after joining the owner.
// Before closing/resetting events, handles, or observations: disable with a
// release store, then join the original owner and every possible in-flight hook
// caller. Disabling does not cancel a captured wait or suppress its completion.
// No hook calls may outlive the test-owned handles/observation lifetime.
inline DWORD WINAPI wait(DWORD count, const HANDLE* handles, BOOL all, DWORD ms) {
    bool observe = false;
    if (enabled.load(std::memory_order_acquire) && count == 2) {
        bool expected = false;
        observe = captured.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    if (observe) {
        const DWORD originalError = ::GetLastError();
        timeout.store(ms, std::memory_order_release);
        enteredTick.store(::GetTickCount64(), std::memory_order_release);
        if (!::SetEvent(entered)) {
            signalFailures.fetch_add(1, std::memory_order_relaxed);
        }
        ::SetLastError(originalError);
    }

    // Always perform the original OS wait with exactly the original arguments.
    // The entry event marks the call boundary, not proof of a blocked thread.
    const DWORD actual = ::WaitForMultipleObjects(count, handles, all, ms);

    if (observe) {
        const DWORD actualError = ::GetLastError();
        result.store(actual, std::memory_order_release);
        returnedTick.store(::GetTickCount64(), std::memory_order_release);
        if (!::SetEvent(returned)) {
            signalFailures.fetch_add(1, std::memory_order_relaxed);
        }
        // Observer signalling failures never replace the real wait's outcome.
        ::SetLastError(actualError);
    }
    return actual;
}

// No wake, timeout substitution, extra wait, thread, timer, or polling is added.
// For the original idle-reap path the test must retain Limits{}.recheck (retryMs,
// currently 20000); deleting only idle reap should expose INFINITE at entry.
// INFINITE is forwarded unchanged and may need normal owner shutdown to return.
// This port alone proves neither OFF cleanup before destruction nor no wake
// after fault release; those are assertions for the parent case/runner.
} // namespace source_idle_wait_port

#endif // YMCC_SOURCE_IDLE_WAIT_PORT_INCLUDED

static std::atomic<bool> observeRecoveryWait{false};static std::atomic<unsigned> recoveryWaitEntries{0};
static DWORD WINAPI recoveryObservedWait(DWORD count,const HANDLE* events,BOOL all,DWORD timeout){
 if(observeRecoveryWait.load()&&count==2&&timeout==60000)++recoveryWaitEntries;
 return source_idle_wait_port::wait(count,events,all,timeout);
}
// Load the real producer first; its pragma once prevents macro substitution
// inside that implementation. Only this original Supervisor gets the test port.
#include "../native/steam_source_lease.h"
namespace source_job_capture_port {
inline std::atomic<bool> fail{false};inline std::atomic<HANDLE> job{nullptr};inline std::atomic<unsigned> failures{0},terminations{0};
inline BOOL WINAPI query(HANDLE handle,JOBOBJECTINFOCLASS kind,LPVOID data,DWORD bytes,LPDWORD returned){
 if(fail&&handle==job&&kind==JobObjectBasicProcessIdList){++failures;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;}
 return ::QueryInformationJobObject(handle,kind,data,bytes,returned);
}
inline BOOL WINAPI terminate(HANDLE handle,UINT code){if(handle==job)++terminations;return ::TerminateJobObject(handle,code);}
}
#include "../native/steam_source_lease_transfer.h"
#define QueryInformationJobObject source_job_capture_port::query
#define TerminateJobObject source_job_capture_port::terminate
#include "../native/shared_game_media_source_job.h"
#undef TerminateJobObject
#undef QueryInformationJobObject
namespace ymcc::steaminfo {std::shared_ptr<SourceLeaseV2> fixtureAcquireSourceV2() noexcept;}
#define WaitForMultipleObjects recoveryObservedWait
#define acquireSourceV2 fixtureAcquireSourceV2
#include "../native/shared_game_media_download_supervisor.h"
#undef acquireSourceV2
#undef WaitForMultipleObjects
#include "../native/shared_game_media_video_request.h"
#include "../native/steam_source_lease_contract.h"
#include "shared_game_media_sleep_fixture.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static int checks=0;
static void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static Json verified(int id){return {{"appId",id},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}};}
static void shortcuts(const fs::path& file,const Json& rows){std::string bytes;auto text=[&](const std::string& s){bytes+=s;bytes+=char(0);};bytes+=char(0);text("shortcuts");int n=0;for(const auto& row:rows){bytes+=char(0);text(std::to_string(n++));bytes+=char(2);text("appid");auto id=row["appid"].get<uint32_t>();for(int i=0;i<4;i++)bytes+=char(id>>(8*i));for(const auto* field:{"appname","exe"}){bytes+=char(1);text(field);text(row[field]);}bytes+=char(8);}bytes+=char(8);bytes+=char(8);fs::create_directories(file.parent_path());std::ofstream(file,std::ios::binary).write(bytes.data(),bytes.size());}
static Json row(const fs::path& exe,int n){return {{"appId",0},{"executable",utf8(exe.wstring())},{"name","candidate only"},{"steamAccount","17"},{"shortcutAppId",std::to_string(0x90000000u+n)},{"catalogSource","steam-shortcut"},{"sourceTier",2},{"verified",false}};}
// Controlled consumer input only: no factory, discovery, registry read or producer.
// Each capture is immutable; cancel is one-way and borrowed handles live with it.
static std::function<void()> afterShortcutFixtureVdfRead; // Installed before and used only by the fixture owner thread.
class ShortcutFixtureSource final : public ymcc::steaminfo::SourceLeaseV2 {
 const ymcc::steaminfo::SourceSnapshot facts_;
 std::array<void*,3> events_{};
 HANDLE provider_=nullptr;
 mutable std::atomic<bool> cancelled_{false};
 const bool revokeOnSnapshot_;
 const bool observeAfterRead_;
 unsigned currentReads_=0;
 static ymcc::steaminfo::SourceSnapshot capture(){
  ymcc::steaminfo::SourceSnapshot s;
  s.sourceRevision=s.modeRevision=s.powerGeneration=1;
  s.mode="desktop";s.modeEpoch="owned-consumer-input";
  s.selection=ymcc::steaminfo::Selection::RegistryCandidate;
  s.selectedAccount=shortcutFixtureAccount.load();
  s.facts.available=true;s.facts.requested=ymcc::steaminfo::All;s.facts.observedAt=GetTickCount64();
  s.facts.presence=1;s.facts.processPid=GetCurrentProcessId();
  s.facts.processBirth=ymcc::steaminfo::transfer::birth(GetCurrentProcess());
  s.facts.processPath=utf8(shortcutFixtureSteamRoot+L"\\steam.exe");
  s.facts.identityRevision=s.facts.processEpoch=s.facts.accountEpoch=1;
  s.facts.root=utf8(shortcutFixtureSteamRoot);
  s.facts.rootError=static_cast<uint32_t>(shortcutFixtureRootError.load());
  s.facts.registryAccount=s.selectedAccount;
  s.facts.accountError=static_cast<uint32_t>(shortcutFixtureAccountError.load());
  return s;
 }
 void close() noexcept {for(auto& event:events_)if(event){CloseHandle(static_cast<HANDLE>(event));event=nullptr;}if(provider_){CloseHandle(provider_);provider_=nullptr;}}
public:
 explicit ShortcutFixtureSource(bool revokeOnSnapshot=false,bool observeAfterRead=false):facts_(capture()),revokeOnSnapshot_(revokeOnSnapshot),observeAfterRead_(observeAfterRead){
  for(auto& event:events_){event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event){close();throw std::runtime_error("fixture-source-event-unavailable");}}
  provider_=OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId());
  if(!provider_){close();throw std::runtime_error("fixture-source-provider-unavailable");}
 }
 ~ShortcutFixtureSource() override {close();}
 void cancel() const noexcept {cancelled_.store(true,std::memory_order_release);if(events_[2])SetEvent(static_cast<HANDLE>(events_[2]));}
 const ymcc::steaminfo::SourceSnapshot& snapshot() const noexcept override {
  // Deterministic interleave after admission, before the real VDF read's final
  // current check. No second account or registry observation is fabricated.
  if(revokeOnSnapshot_)cancel();return facts_;
 }
 bool current() noexcept override {
  if(observeAfterRead_&&++currentReads_==2&&afterShortcutFixtureVdfRead){
   auto onRead=std::move(afterShortcutFixtureVdfRead);afterShortcutFixtureVdfRead={};
   try{onRead();}catch(...){cancel();}
  }
  return !cancelled_.load(std::memory_order_acquire);
 }
 const std::array<void*,3>& cancellationEvents() const noexcept override {return events_;}
 void* providerProcess() const noexcept override {return provider_;}
};
static std::shared_ptr<ShortcutFixtureSource> shortcutFixtureSource(bool revokeOnSnapshot=false){return std::make_shared<ShortcutFixtureSource>(revokeOnSnapshot);}
static std::atomic<unsigned> fixtureSourceAcquires{0};
namespace ymcc::steaminfo {
std::shared_ptr<SourceLeaseV2> fixtureAcquireSourceV2() noexcept {
 try{
  if(shortcutRegistryFixture.load()){++fixtureSourceAcquires;return std::make_shared<ShortcutFixtureSource>(false,true);}
  return acquireSourceV2();
 }catch(...){return {};}
}
}
static std::function<bool(const Json&)> shortcutFixtureCurrent(){
 IdentityPorts ports;ports.source=shortcutFixtureSource();
 // One capture per original Queue operation, including its final locked check.
 return [ports](const Json& target){try{ports.requireCurrent(target);return true;}catch(const ShortcutMembershipRejected&){return false;}};
}
template<class Q> static size_t recoverCurrentMembership(Q& queue,const fs::path& helper,const std::function<bool(const Json&)>& current){
 if constexpr(requires {queue.recoverIdentityPrerequisites(helper,current);})return queue.recoverIdentityPrerequisites(helper,current);
 else return queue.recoverIdentityPrerequisites(helper); // old source negative, no simulated recovery
}
static void sourceJobCaptureFailureProbe(const fs::path& root){
 namespace sf=ymcc::steaminfo::transfer;namespace port=source_job_capture_port;
 const auto priorRoot=shortcutFixtureSteamRoot;shortcutFixtureSteamRoot=(root/L"steam").wstring();
 sf::Handle heldProcess,heldJob;
 struct Restore{std::wstring root;~Restore(){port::fail=false;SourceJob::reap(2000);port::job=nullptr;shortcutFixtureSteamRoot=root;}}restore{priorRoot};
 auto source=shortcutFixtureSource();std::weak_ptr<ymcc::steaminfo::SourceLeaseV2> retained=source;
 port::failures=port::terminations=0;
 {
  auto exported=sf::Export::capture(source);check(bool(exported),"Source Job regression export unavailable");SourceJob owner(std::move(exported));port::job=owner.get();
  check(DuplicateHandle(GetCurrentProcess(),owner.get(),GetCurrentProcess(),&heldJob.value,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"Source Job regression query view unavailable");
  const auto self=sf::module();auto line=identityQuote(self)+L" --unresumed-source-job-fixture";
  STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION child{};
  check(CreateProcessW(self.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"Source Job regression suspended child unavailable");
  check(owner.assign(child),"Source Job regression child assignment failed");
  check(DuplicateHandle(GetCurrentProcess(),child.hProcess,GetCurrentProcess(),&heldProcess.value,SYNCHRONIZE,FALSE,0)!=FALSE,"Source Job regression held process unavailable");
  check(owner.source()->sealInheritance(),"Source Job regression inheritance seal failed");
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION active{};check(::QueryInformationJobObject(owner.get(),JobObjectBasicAccountingInformation,&active,sizeof active,nullptr)&&active.ActiveProcesses==1,"Source Job regression must own one real child");
  source.reset();port::fail=true;
 }
 check(port::failures>=1,"Source Job regression did not reach member capture failure");
 check(port::terminations==1&&WaitForSingleObject(heldProcess.value,2000)==WAIT_OBJECT_0,"Member capture failure suppressed exact owned Job cancellation");
 check(!retained.expired(),"Failed capture prematurely released original source");
 port::fail=false;check(SourceJob::reap(2000)==0,"Source Job regression retained ledger did not return");
 JOBOBJECT_BASIC_ACCOUNTING_INFORMATION returned{};
 check(::QueryInformationJobObject(heldJob.value,JobObjectBasicAccountingInformation,&returned,sizeof returned,nullptr)&&returned.ActiveProcesses==0,"Source Job regression actual Job was not zero");
 check(retained.expired(),"Source Job regression source was not released after actual return");
}
static void sourceJobOffIdleReapProbe(const fs::path& root){
 namespace sf=ymcc::steaminfo::transfer;namespace port=source_job_capture_port;namespace wait=source_idle_wait_port;
 const auto priorRoot=shortcutFixtureSteamRoot;const bool priorNetwork=selectedVideoNoNetwork.load();
 shortcutFixtureSteamRoot=(root/L"steam").wstring();selectedVideoNoNetwork=true;
 sf::Handle heldProcess,heldJob,entered(CreateEventW(nullptr,TRUE,FALSE,nullptr)),returned(CreateEventW(nullptr,TRUE,FALSE,nullptr));
 check(entered.value&&returned.value,"Idle cleanup observation events unavailable");
 std::unique_ptr<Supervisor> owner;
 struct Restore{
  std::unique_ptr<Supervisor>& owner;std::wstring root;bool network;
  ~Restore(){port::fail=false;wait::enabled=false;owner.reset();SourceJob::reap(2000);port::job=nullptr;wait::entered=wait::returned=nullptr;shortcutFixtureSteamRoot=root;selectedVideoNoNetwork=network;}
 }restore{owner,priorRoot,priorNetwork};
 auto source=shortcutFixtureSource();std::weak_ptr<ymcc::steaminfo::SourceLeaseV2> retained=source;
 port::failures=port::terminations=0;
 {
  auto exported=sf::Export::capture(source);check(bool(exported),"Idle cleanup source export unavailable");
  SourceJob debt(std::move(exported));port::job=debt.get();
  check(DuplicateHandle(GetCurrentProcess(),debt.get(),GetCurrentProcess(),&heldJob.value,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"Idle cleanup Job view unavailable");
  const auto self=sf::module();auto line=identityQuote(self)+L" --unresumed-idle-cleanup-fixture";
  STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION child{};
  check(CreateProcessW(self.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"Idle cleanup suspended child unavailable");
  check(debt.assign(child),"Idle cleanup actual child assignment failed");
  check(DuplicateHandle(GetCurrentProcess(),child.hProcess,GetCurrentProcess(),&heldProcess.value,SYNCHRONIZE,FALSE,0)!=FALSE,"Idle cleanup held child unavailable");
  check(debt.source()->sealInheritance(),"Idle cleanup inheritance seal failed");
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION active{};
  check(::QueryInformationJobObject(debt.get(),JobObjectBasicAccountingInformation,&active,sizeof active,nullptr)&&active.ActiveProcesses==1,"Idle cleanup requires a real assigned child");
  source.reset();port::fail=true;
 }
 check(port::failures.load()>=1&&port::terminations.load()>=1,"Idle cleanup did not capture failure and cancel its own child");
 check(WaitForSingleObject(heldProcess.value,2000)==WAIT_OBJECT_0&&!retained.expired(),"Idle cleanup debt must retain source after actual child exit");
 const auto data=root/L"queue";Queue queue(data);queue.configure(false,"");
 const auto document=data/L"config"/L"game-media-downloads.json";
 const auto bytes=[](const fs::path& file){std::ifstream input(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
 const auto saved=bytes(document);check(!saved.empty()&&!downloadsEnabled(queue.snapshot()),"Idle cleanup queue is not explicitly OFF");
 const auto sourceCaptures=fixtureSourceAcquires.load(),networkAttempts=selectedVideoNetworkAttempts.load();
 Limits limits; // Preserve the actual original recheck cadence and wait duration.
 const DWORD originalWait=limits.recheck;
 wait::enabled=false;wait::captured=false;wait::timeout=0;wait::result=WAIT_FAILED;wait::enteredTick=0;wait::returnedTick=0;wait::signalFailures=0;
 wait::entered=entered.value;wait::returned=returned.value;wait::enabled=true;
 ResourcePorts resources;resources.ac=[](){return 0;};resources.cpu=[](uint64_t){return std::optional<double>{0};};resources.foreground=[](){return false;};
 owner=std::make_unique<Supervisor>(data,root/L"absent-bin"/L"CustomSteamLibrary.exe",limits,resources);
 check(WaitForSingleObject(entered.value,4000)==WAIT_OBJECT_0,"Idle cleanup owner did not enter its actual wait");
 const DWORD requested=wait::timeout.load();
 const bool finite=requested==originalWait;
 // A missing idle reap must fail before the destructor can mask the result.
 // Clear the injected OS fault without sending any business/owner wake.
 port::fail=false;
 if(finite){
  check(!retained.expired(),"Idle cleanup source released before original slow wait");
  check(WaitForSingleObject(returned.value,originalWait+5000)==WAIT_OBJECT_0,"Idle cleanup actual slow wait did not return");
  check(wait::result.load()==WAIT_TIMEOUT,"Idle cleanup used a wake instead of its original timeout");
  const auto elapsed=wait::returnedTick.load()-wait::enteredTick.load();
  check(elapsed+64>=originalWait,"Idle cleanup shortened the original OS wait interval");
  const auto until=GetTickCount64()+2000;while(!retained.expired()&&GetTickCount64()<until)Sleep(1);
  check(retained.expired()&&owner!=nullptr,"Idle cleanup completed only after owner destruction");
 }
 wait::enabled=false;
 const bool beforeDestructor=retained.expired();
 owner.reset();
 check(SourceJob::reap()==0&&retained.expired(),"Idle cleanup final destructor did not return remaining exact ledger");
 JOBOBJECT_BASIC_ACCOUNTING_INFORMATION final{};
 check(::QueryInformationJobObject(heldJob.value,JobObjectBasicAccountingInformation,&final,sizeof final,nullptr)&&final.ActiveProcesses==0&&WaitForSingleObject(heldProcess.value,0)==WAIT_OBJECT_0,"Idle cleanup returned without actual child and Job zero");
 check(wait::signalFailures.load()==0,"Idle cleanup observation signal failed");
 check(bytes(document)==saved&&!downloadsEnabled(queue.snapshot()),"OFF idle cleanup changed the queue or enabled business work");
 check(fixtureSourceAcquires.load()==sourceCaptures&&selectedVideoNetworkAttempts.load()==networkAttempts,"OFF idle cleanup reacquired Steam authority or reached network");
 atomicWrite(root/L"idle-observation.json",{{"expectedWaitMs",originalWait},{"requestedWaitMs",requested},{"waitResult",wait::result.load()},
  {"actualWaitMs",wait::returnedTick.load()-wait::enteredTick.load()},{"sourceReleasedBeforeDestructor",beforeDestructor},
  {"queueOff",true},{"businessWakeAfterFaultRelease",false},{"sourceReacquisitions",fixtureSourceAcquires.load()-sourceCaptures},
  {"networkAttempts",selectedVideoNetworkAttempts.load()-networkAttempts},{"actualJobZero",true},{"heldChildSignaled",true},{"clockGranularityToleranceMs",64}});
 check(finite,"Pending cleanup must use original slow owner wait before destructor");
}

static int bindingRepairProbe(const fs::path& base){
 if(fs::exists(base))throw std::runtime_error("fresh binding probe root required");fs::create_directories(base);Json rows=Json::array();
 wchar_t filter[256]{};const auto filterSize=GetEnvironmentVariableW(L"YMCC_SHORTCUT_BINDING_CASE",filter,256);if(filterSize>=256)throw std::runtime_error("invalid binding case selector");const auto selected=filterSize?utf8(filter):std::string{};
 const auto run=[&](const char* name,auto work){if(!selected.empty()&&selected!=name)return;const auto started=checks;try{work(base/wide(name));rows.push_back({{"name",name},{"ok",true},{"checks",checks-started}});}catch(const std::exception& e){rows.push_back({{"name",name},{"ok",false},{"error",e.what()},{"checks",checks-started}});}};
 // Explicit selection only: preserve the original default case set.
 if(selected=="source-job-capture-failure")run("source-job-capture-failure",sourceJobCaptureFailureProbe);
 if(selected=="source-job-off-idle-reap")run("source-job-off-idle-reap",sourceJobOffIdleReapProbe);
 if(selected=="repaired-files-require-current-shortcut")run("repaired-files-require-current-shortcut",[&](const fs::path& root){
  const auto helper=root/L"bin"/L"SteamArtworkLab.exe",exe=root/L"game.exe",other=root/L"other.exe";
  const auto vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
  fs::create_directories(helper.parent_path());std::ofstream(helper)<<"inert helper";std::ofstream(exe)<<"inert original";std::ofstream(other)<<"inert different target";
  shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
  struct Restore {~Restore(){shortcutRegistryFixture=false;}} restore;
  const auto setTarget=[&](const fs::path& path){shortcuts(vdf,Json::array({{{"appid",0x90000006u},{"appname","owned shortcut"},{"exe",utf8(path.wstring())}}}));};
  Queue q(root/L"queue");q.enqueue(Json::array({row(exe,6)}));const auto key=q.document()["jobs"].begin().key();const auto token=q.start();
  check(!token.empty(),"Legacy prerequisite fixture did not start");
  for(int n=0;n<3;++n){q.update(token,[&](Json& state){state["jobs"][key]["retryAt"]=0;});check(stringField(q.claim(token),"key")==key,"Legacy prerequisite fixture lost its target");q.result(token,key,false,"media-identity-worker-unavailable");}
  q.end(token,"deferred");const auto stopped=q.document();
  check(itemStopped(stopped["jobs"][key])&&!stopped["jobs"][key].contains("identityPrerequisiteStamp"),"Legacy prerequisite fixture did not reach the unstamped terminal state");
  setTarget(other);check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&q.document()==stopped,"Repaired files revived a shortcut that now points to a different EXE");
  shortcuts(vdf,Json::array());check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&q.document()==stopped,"Repaired files revived a removed shortcut");
  setTarget(exe);shortcutFixtureAccount=99;check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&q.document()==stopped,"Repaired files revived another account's shortcut");
  shortcutFixtureAccount=17;check(q.recoverIdentityPrerequisites(helper)==0&&q.document()==stopped,"Missing membership provider admitted legacy recovery");
  const auto unknown=[](const Json&)->bool{throw std::runtime_error("media-storage-unavailable");};check(q.recoverIdentityPrerequisites(helper,unknown)==0&&q.document()==stopped,"Unreadable membership admitted legacy recovery");
  const auto membership=shortcutFixtureCurrent();unsigned calls=0;
  check(q.recoverIdentityPrerequisites(helper,[&](const Json& j){if(++calls==2)setTarget(other);return membership(j);})==0&&calls==2&&q.document()==stopped,"Membership changed before commit but restored the retry budget");
  setTarget(exe);calls=0;const auto held=helper.parent_path()/L"held.exe";
  const auto missing=q.recoverIdentityPrerequisites(helper,[&](const Json& j){if(++calls==2)fs::rename(helper,held);return membership(j);});
  check(missing==0&&calls==2&&q.document()==stopped,"Helper disappeared before commit but restored the retry budget");fs::rename(held,helper);
  check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==1,"Current shortcut with restored prerequisites remained stopped");
  const auto revived=q.document();check(revived["jobs"][key].value("attempts",-1)==0&&!itemStopped(revived["jobs"][key])&&
    !revived["jobs"][key].value("verified",true)&&revived["jobs"][key].value("appId",1)==0,"Valid recovery changed identity or did not restore one bounded budget");
  check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&q.document()==revived,"Repeated wake refunded the repaired task again");
 });
 if(selected=="acknowledged-empty-bulk-finishes-without-worker")run("acknowledged-empty-bulk-finishes-without-worker",[&](const fs::path& root){
   mediaSleepFixture::bind();
  const auto steam=root/L"steam",helper=root/L"bin"/L"SteamArtworkLab.exe",exe=root/L"game.exe",other=root/L"other.exe";
  const auto vdf=steam/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
  fs::create_directories(helper.parent_path());std::ofstream(helper)<<"inert helper";std::ofstream(exe)<<"inert game";std::ofstream(other)<<"inert replacement";
  shortcutFixtureSteamRoot=steam.wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
  struct Restore {~Restore(){shortcutRegistryFixture=false;}} restore;
  const auto setTarget=[&](const fs::path& path){shortcuts(vdf,Json::array({{{"appid",0x90000006u},{"appname","owned shortcut"},{"exe",utf8(path.wstring())}}}));};
  const auto waitFor=[](auto ready){const auto end=GetTickCount64()+1500;do{if(ready())return true;Sleep(5);}while(GetTickCount64()<end);return false;};
  for(const std::string variant:{"empty","stale","restored","pending","late-receipt"}){
   Queue q(root/wide(variant));setTarget(other);const auto key="file-"+digestKey(normalized(utf8(exe.wstring())));
   if(variant=="stale"||variant=="restored"||variant=="late-receipt"){
    q.enqueue(Json::array({row(exe,6)}));const auto token=q.start();check(!token.empty(),"Fixture could not start old shortcut attempts");
    IdentityPorts ports;ports.source=shortcutFixtureSource();
     {
      const auto claimed=q.claim(token);
     check(stringField(claimed,"key")==key,"Fixture did not claim original shortcut");Json detail;
     try{ports.requireCurrent(claimed);}catch(const ShortcutMembershipRejected& error){detail=error.observation;}
     check(stringField(detail,"reason")=="shortcut-target-changed","Fixture did not observe a changed EXE");
     q.result(token,key,false,"media-shortcut-no-longer-current",0,{},detail);
      check(itemStopped(q.document()["jobs"][key]),"Known stale target should be stopped before testing inventory completion");
    }q.end(token,"deferred");
   }else if(variant=="pending")q.enqueue(Json::array({{{"appId",620},{"verified",true}}}));
   q.configure(false,"");q.requestSupplement(true);
   const auto initial=q.document();const auto id=stringField(initial,"inventoryRequestId");check(!id.empty()&&manualRun(initial),"Manual request was not admitted");
   Limits limits;limits.startup=0;limits.recheck=60000;ResourcePorts resources;resources.ac=[](){return 0;};
   const auto captures=fixtureSourceAcquires.load();
   q.inventoryReady("unrelated-receipt");check(manualRun(q.document())&&stringField(q.document(),"inventoryReadyId")!=id,"A foreign receipt ended the request");
   std::atomic<bool> lateReceiptRan{false};
   struct RestoreHook{~RestoreHook(){afterShortcutFixtureVdfRead={};}} restoreHook;
   if(variant=="late-receipt")afterShortcutFixtureVdfRead=[&]{setTarget(exe);q.inventoryReady(id);lateReceiptRan=true;};
   {Supervisor owner(q.root(),helper.parent_path()/L"CustomSteamLibrary.exe",limits,resources);owner.wake(true);
    check(waitFor([&]{return fixtureSourceAcquires.load()>captures;}),"Initial owner recheck did not run");
    if(variant=="restored")setTarget(exe);
    if(variant!="late-receipt")q.inventoryReady(id);
    if(variant=="empty"||variant=="stale"){
     check(waitFor([&]{return !manualRun(q.snapshot());}),"Acknowledged empty bulk did not complete without a worker");
     const auto done=q.document();check(stringField(done,"manualState")=="completed"&&done["jobs"]==initial["jobs"],"Empty completion changed job data or did not finish");
    }else if(variant=="late-receipt"){
     check(waitFor([&]{return lateReceiptRan.load();}),"Late receipt fixture did not interleave after the first VDF read");
     check(waitFor([&]{return q.document()["jobs"][key].value("attempts",-1)==0;})&&manualRun(q.document()),"Late receipt closed before the recheck for restored membership");
    }else if(variant=="restored"){
     check(waitFor([&]{return q.document()["jobs"][key].value("attempts",-1)==0;}),"Restored shortcut was not recovered by its existing owner wake");
     check(manualRun(q.document()),"Recovered exact shortcut lost its manual admission");
    }else{
     check(waitFor([&]{return stringField(q.snapshot(),"resourcePauseReason")=="on-battery";}),"Pending work did not reach the normal resource gate");
     check(manualRun(q.document())&&stringField(q.document()["jobs"]["steam-620"],"state")=="pending","Pending work was prematurely completed");
    }
   }
   const auto final=q.document();check(!final.value("enabled",true)&&final.value("workerPid",DWORD{0})==0&&final["active"].empty(),"Owner changed OFF or left active work");
  }
 });
 const auto bytes=[](const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
 const auto cache=[](Store& store,const std::string& exe,int id){const auto file=store.root()/L"observation.json";atomicWrite(file,{{"exe",exe},{"match",verified(id)}});auto result=store.rememberIdentity(exe,file,now());check(result.value("verified",false)&&result.value("appId",0)==id,"fresh exact-file official cache is accepted");};
 if(selected=="verified-exe-finds-existing-current-game-video")run("verified-exe-finds-existing-current-game-video",[&](const fs::path& root){
  fs::create_directories(root);const auto exe=root/L"game.exe",other=root/L"launcher.exe";
  std::ofstream(exe)<<"inert game";std::ofstream(other)<<"inert launcher";
  const auto primary=utf8(exe.wstring()),launcher=utf8(other.wstring());Store store(root/L"data");
  const auto exactKey=stringField(store.registerEntry({{"executable",primary}}),"key");
  cache(store,primary,106);
  store.registerEntry({{"executable",launcher},{"appId",106},{"verified",true}});
  store.registerEntry({{"appId",777},{"verified",true}});
  const auto index=store.root()/L"config"/L"game-media.json";
  const Json image={{"id","old-image"},{"extension","jpg"},{"kind","image"}},video={{"id","current-video"},{"extension","mp4"},{"kind","video"}};
  const auto output=store.assetPath(video);fs::create_directories(output.parent_path());std::ofstream(output)<<"owned video";std::ofstream(store.assetPath(image))<<"owned static";
  auto doc=read(index);doc["entries"][exactKey]["assets"]["static"]=image;doc["entries"]["steam-106"]["assets"]["dynamic"]=video;atomicWrite(index,doc);
  const auto cacheFile=custom_steam_library::identity_cache::cachePath(store.root(),primary);
  const auto cacheDiskPath=custom_steam_library::ioPath(cacheFile);
  const auto originalIndex=bytes(index),originalCache=bytes(cacheDiskPath);
  const auto lookup=[&]{return store.resolve({{"executable",primary},{"currentGameWallpaper",true}});};
  const auto exact=store.resolve({{"executable",primary}}),canonical=store.resolve({{"appId",106}});
  check(exact.value("staticReady",false)&&!exact.value("dynamicReady",false)&&canonical.value("dynamicReady",false),"Fixture must hold split static and dynamic records without a persisted alias");
  const auto displayed=lookup();
  check(displayed.value("dynamicReady",false)&&stringField(displayed,"key")=="steam-106","Verified current EXE could not read its existing canonical video");
  check(displayed.value("currentGameWallpaperFallback",false)&&stringField(displayed["asset"],"id")=="current-video","Display lookup must identify the existing video as a read-only fallback");
  check(bytes(index)==originalIndex&&!originalCache.empty()&&bytes(cacheDiskPath)==originalCache,"Display lookup must not write the media or exact identity record");
  const auto noFallback=[&](const char* reason){const auto found=lookup();check(!found.value("currentGameWallpaperFallback",false)&&stringField(found,"key")==exactKey,reason);};
  auto manual=doc;manual["entries"][exactKey]["manualSlots"]["static"]=true;atomicWrite(index,manual);noFallback("Verified identity must not override manual static media");
  auto invalid=doc;invalid["entries"]["steam-106"]["invalidSlots"]["dynamic"]="current-video";atomicWrite(index,invalid);noFallback("Invalid canonical video must not be returned");
  auto untrusted=doc;untrusted["entries"]["steam-106"]["identityVerified"]=false;atomicWrite(index,untrusted);noFallback("Unverified canonical media must not be returned");
  auto conflict=doc;conflict["aliases"]["steam-106"]="steam-777";conflict["entries"]["steam-777"]["assets"]["dynamic"]=video;atomicWrite(index,conflict);noFallback("A canonical alias to another AppID must not authorize its video");
  atomicWrite(index,doc);
  check(stringField(store.resolve({{"key",exactKey},{"executable",primary},{"currentGameWallpaper",true}}),"key")==exactKey,"Explicit media key stays exact");
  atomicWrite(cacheFile,{{"schemaVersion",999}});noFallback("Future primary identity cannot authorize a display fallback");
 });
 if(selected=="launcher-video-missing-from-primary-exe-lookup")run("launcher-video-missing-from-primary-exe-lookup",[&](const fs::path& root){
  const auto install=root/L"install",launcher=install/L"Launcher.exe",primary=install/L"Game-Win64-Shipping.exe";
  fs::create_directories(install);std::ofstream(launcher)<<"owned launcher";std::ofstream(primary)<<"owned main executable";
  const auto launchPath=utf8(launcher.wstring()),mainPath=utf8(primary.wstring());Store store(root/L"data");
  cache(store,launchPath,106);
  atomicWrite(store.root()/L"state"/L"library-scan.json",{{"games",Json::array({{{"gameDirectory",utf8(install.wstring())},
    {"primaryExecutable",mainPath},{"primaryRole","main-game"},{"contentType","game"},{"status","ready"},
    {"scanComplete",true},{"uniquePrimaryInvariant",true},{"candidates",Json::array({{{"path",mainPath},{"role","main-game"},{"selected",true}},{{"path",launchPath},{"role","launcher"},{"selected",false}}})}}})}});
  const auto mainEntry=store.registerEntry({{"executable",mainPath}}),steam=store.registerEntry({{"executable",launchPath},{"appId",106},{"verified",true}});
  const auto fixedMain=mainEntry.value("key",std::string{});
  const Json image={{"id","fixture-image"},{"extension","jpg"},{"kind","image"}},video={{"id","fixture-video"},{"extension","mp4"},{"kind","video"}};
  const auto location=store.assetPath(image);fs::create_directories(location.parent_path());std::ofstream(location)<<"owned image";std::ofstream(store.assetPath(video))<<"owned video";
  auto index=store.snapshot();index["entries"][fixedMain]["assets"]["static"]=image;index["entries"][steam["key"].get<std::string>()]["assets"]["dynamic"]=video;
  const auto path=store.root()/L"config"/L"game-media.json";atomicWrite(path,index);
  const auto native=store.resolve({{"executable",launchPath}}),control=store.resolve({{"executable",mainPath}});
  check(native.value("dynamicReady",false)&&stringField(native,"key")=="steam-106","Owned launcher must already resolve the downloaded video");
  check(control.value("staticReady",false)&&!control.value("dynamicReady",false)&&stringField(control,"key")==fixedMain&&!store.identity(mainPath).value("verified",false),"Ordinary exact lookup must reproduce the static-only primary and no exact cache");
  atomicWrite(root/L"observed.json",{{"launcherKey",native["key"]},{"launcherVideoReady",native["dynamicReady"]},{"primaryKey",control["key"]},{"primaryVideoReady",control["dynamicReady"]},{"primaryStaticReady",control["staticReady"]}});
  const auto game=store.resolve({{"executable",mainPath},{"currentGameWallpaper",true}});
  check(game.value("dynamicReady",false)&&stringField(game,"key")=="steam-106","Current game EXE resolves only its old static record despite the same game's verified launcher video");
  const auto readCurrent=[&]{return store.resolve({{"executable",mainPath},{"currentGameWallpaper",true}});};
  const auto reportStatic=[&](const char* reason){const auto exact=readCurrent();check(!exact.value("currentGameWallpaperFallback",false)&&!exact.value("dynamicReady",false)&&stringField(exact,"key")==fixedMain,reason);};
  const auto scanPath=store.root()/L"state"/L"library-scan.json",config=store.root()/L"config"/L"library-config.json";
  const auto stableMedia=read(path),scan=read(scanPath);
  check(game.value("currentGameWallpaperFallback",false)&&stringField(game["asset"],"id")=="fixture-video"&&read(path)==index,"Fallback returns the existing asset without a durable alias or store write");
  check(!store.identity(mainPath).value("verified",false),"Display fallback must not grant the actual EXE identity authority");
  const auto local=[&](const auto& edit,const char* reason){auto d=stableMedia;edit(d["entries"][fixedMain]);atomicWrite(path,d);reportStatic(reason);atomicWrite(path,stableMedia);};
  local([](Json& e){e["manualSlots"]["static"]=true;},"Fallback bypassed a manual static image");
  local([](Json& e){e["modeExplicit"]=true;e["wallpaperMode"]="static";},"Fallback ignored explicit static mode");
  local([](Json& e){e["deletedSlots"]["dynamic"]=true;},"Fallback resurrected a deleted dynamic slot");
  local([](Json& e){e["invalidSlots"]["dynamic"]="invalid-video";},"Fallback hid an explicitly rejected video");
  local([](Json& e){e["selectedVideo"]={{"candidateId","manual"}};},"Fallback overrode an explicit pending selection");
  auto ambiguous=scan;const auto duplicate=ambiguous["games"][0];ambiguous["games"].push_back(duplicate);atomicWrite(scanPath,ambiguous);reportStatic("Two scan claims must not share a video");
  auto incomplete=scan;incomplete["games"][0]["scanComplete"]=false;atomicWrite(scanPath,incomplete);reportStatic("Partial scan supplied a launcher claim");
  auto competing=scan;competing["games"][0]["candidates"].push_back({{"path",utf8((install/L"AnotherLauncher.exe").wstring())},{"role","launcher"}});atomicWrite(scanPath,competing);reportStatic("Multiple launchers must not choose a video");
  atomicWrite(scanPath,scan);
  atomicWrite(config,{{"manualBuckets",{{launchPath,{{"bucket","non-game"}}}}}});reportStatic("An excluded launcher supplied a fallback");atomicWrite(config,Json::object());
  auto withdrawn=stableMedia;withdrawn["entries"]["steam-106"]["invalidSlots"]["dynamic"]="fixture-video";atomicWrite(path,withdrawn);reportStatic("An invalid video may not be exposed by fallback");atomicWrite(path,stableMedia);
  cache(store,mainPath,777);reportStatic("A separate primary identity must not borrow a launcher video");
 });
 run("bulk-retry-does-not-revive-changed-shortcut-target",[&](const fs::path& root){
  const auto bin=root/L"bin",helper=bin/L"SteamArtworkLab.exe",old=root/L"game-shipping.exe",launcher=root/L"launcher.exe";
  const auto vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
  fs::create_directories(bin);std::ofstream(helper)<<"inert";std::ofstream(old)<<"old";std::ofstream(launcher)<<"launcher";
  const auto original=row(old,6),replacement=row(launcher,6);
  const auto changeTarget=[&](const fs::path& path){shortcuts(vdf,Json::array({{{"appid",0x90000006u},{"appname","owned candidate"},{"exe",utf8(path.wstring())}}}));};
  shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
  struct Restore {~Restore(){shortcutRegistryFixture=false;}} restore;
  changeTarget(launcher);
  Queue q(root/L"queue");Store store(q.root());q.enqueue(Json::array({original}));
  const auto key=std::string("file-")+digestKey(normalized(utf8(old.wstring())));
  const auto token=q.start();
  IdentityPorts ports;ports.source=shortcutFixtureSource();
   {
   const auto owned=q.claim(token);check(stringField(owned,"key")==key,"Original shortcut was not claimed");
   Json detail;try{ports.requireCurrent(owned);}catch(const ShortcutMembershipRejected& error){detail=error.observation;}
   check(stringField(detail,"reason")=="shortcut-target-changed","Real shortcut reader did not distinguish the changed target");
   q.result(token,key,false,"media-shortcut-no-longer-current",0,{},detail);
  }
  q.end(token,"deferred");
   const auto stopped=q.document()["jobs"][key];
   atomicWrite(root/L"first-settlement.json",stopped);
   check(itemStopped(stopped)&&stringField(stopped,"state")=="unavailable"&&stopped.value("attempts",0)==1&&
      stopped.value("retryAt",int64_t{-1})==0&&stringField(stopped,"stopReason")=="media-shortcut-no-longer-current",
      "A proven changed target scheduled another identity attempt");
  cache(store,utf8(launcher.wstring()),106);
  q.enqueue(Json::array({replacement,{{"appId",620},{"verified",true}}}));
  const auto settle=q.start();
  for(int i=0;i<2;++i){const auto id=stringField(q.claim(settle),"key");check(id=="steam-106"||id=="steam-620","Original claim selected retired work");q.result(settle,id,true,id=="steam-620"?"store-unavailable":"");}
  q.end(settle,"completed");
  q.configure(false,"game-running");
  const auto current=q.document()["jobs"]["steam-106"];
  q.requestSupplement(true);
  auto bulk=q.document();
  check(bulk["jobs"][key]==stopped,"Bulk retry revived a proven stale target");
  check(bulk["jobs"]["steam-106"]==current&&stringField(bulk["jobs"]["steam-620"],"state")=="pending","Bulk retry touched completed media or stopped retrying ordinary unavailable jobs");
  check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&q.document()==bulk,"Different current EXE revived the stopped row");
  changeTarget(old);
  check(q.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==1,"Restored exact target was not recovered during explicit bulk retry with automatic download off");
  check(stringField(q.document()["jobs"][key],"state")=="pending"&&q.document()["jobs"][key].value("attempts",-1)==0,"Exact recovery did not restore a bounded budget");
  q.cancelManual();
  const auto stateFile=q.root()/L"config"/L"game-media-downloads.json";
  auto seed=q.document();seed["jobs"][key]=stopped;atomicWrite(stateFile,seed);
  q.requestSupplement(true,key);check(q.document()["jobs"][key].value("attempts",-1)==0,"Per-game explicit retry was accidentally fenced");q.cancelManual();
  seed=q.document();seed["jobs"][key]=stopped;seed["jobs"][key]["provenanceRevision"]=1;atomicWrite(stateFile,seed);
  q.requestSupplement(true);check(q.document()["jobs"][key].value("attempts",-1)==0,"Observation from a retired provenance blocked current work");
 });
 run("cache-correction-retires-only-old-file-binding",[&](const fs::path& root){
  Store store(root);const auto exe=utf8((root/L"corrected-game.exe").wstring());const Json old={{"appId",281610},{"executable",exe},{"verified",true},{"name","Historical different game"}};
  const auto sample=root/L"retained.mp4";fs::create_directories(root);{std::ofstream out(sample,std::ios::binary);const char media[]={0,0,0,24,'f','t','y','p','i','s','o','m',0,0,0,0};out.write(media,sizeof media);}
  auto retainedEntry=store.registerEntry(old);store.publish(retainedEntry,sample,true);auto lease=store.begin(old);check(lease.value("acquired",false),"old alias has live preparation lease");
  auto doc=read(root/L"config"/L"game-media.json");doc["entries"]["steam-281610"]["privateRetainedMetadata"]={{"keep",true}};atomicWrite(root/L"config"/L"game-media.json",doc);const auto retained=doc["entries"]["steam-281610"];
  cache(store,exe,883710);auto current=old;current["appId"]=883710;current["name"]="Current correct game";
  const auto index=root/L"config"/L"game-media.json";const auto unmodified=bytes(index);
  const auto explicitMissing=[](const Json& e,const char* key){return stringField(e,"key")==key&&stringField(e,"status")=="missing"&&!e.value("ready",true)&&!e.contains("asset");};
  const auto lookup=store.resolve(current);
  const auto reason="new AppID cannot borrow old EXE-bound wallpaper (returned "+stringField(lookup,"key")+")";
  check(explicitMissing(lookup,"steam-883710"),reason.c_str());
  check(explicitMissing(store.resolve({{"key","steam-883710"},{"executable",exe}}),"steam-883710"),"explicit missing key does not fall through to EXE lookup");
  check(store.resolve({{"executable",exe}}).value("key",std::string{})=="steam-281610","EXE-only lookup still follows the existing durable alias before repair");
  const auto batch=store.resolveMany(Json::array({current,old}));
  check(batch.size()==2&&explicitMissing(batch[0],"steam-883710")&&batch[1].value("ready",false)&&stringField(batch[1],"key")=="steam-281610","batch stays exact and still returns the explicitly requested historical AppID");
  check(bytes(index)==unmodified,"pre-repair reads leave the media index byte-identical");
  Store legacy(root/L"legacy");const auto localExe=utf8((root/L"legacy.exe").wstring());
  const auto local=legacy.publish(legacy.registerEntry({{"executable",localExe}}),sample,true);
  const Json toBind={{"appId",570},{"executable",localExe},{"verified",true}};
  check(explicitMissing(legacy.resolve(toBind),"steam-570")&&legacy.resolve({{"executable",localExe}})["asset"]==local["asset"],"unbound EXE wallpaper is available only through EXE lookup until binding");
  check(legacy.registerEntry(toBind)["asset"]==local["asset"]&&legacy.resolve({{"key",local["key"]}})["asset"]==local["asset"],"verified same-EXE binding still adopts local bytes and retains the alias");
  const auto value=store.registerEntry(current);
  check(value.value("key",std::string{})=="steam-883710","new verified identity can supersede a contradictory old file alias");
  const auto after=store.snapshot();check(store.resolve({{"executable",exe}}).value("appId",0)==883710,"EXE-only lookup follows corrected identity");
  const auto historical=after["entries"]["steam-281610"];check(historical["privateRetainedMetadata"]==retained["privateRetainedMetadata"]&&historical["assets"]==retained["assets"],"old AppID metadata and assets stay retained");
  check(historical.value("executable",std::string{}).empty()&&historical.value("taskId",std::string{}).empty()&&historical.value("leaseUntil",int64_t{1})==0,"retired alias no longer advertises EXE or keeps its stale preparation");
  check(historical.value("bindingRevision",uint64_t{0})>retained.value("bindingRevision",uint64_t{0}),"old publication revision is revoked");
  check(value.value("assets",Json::object()).empty(),"different AppID cannot inherit historical wrong-game media");
  bool latePublication=false;try{store.publish(lease,sample,false);}catch(...){latePublication=true;}check(latePublication,"old in-flight publication cannot resurrect retired file binding");
  const auto file=root/L"config"/L"game-media.json";const auto saved=bytes(file);auto rejectedFailure=lease;rejectedFailure["reason"]="media-network-request-failed";const auto settled=store.fail(rejectedFailure);check(settled.value("key",std::string{})=="steam-281610"&&bytes(file)==saved,"retired lease failure returns retained record without reseeding or writing");bool stale=false;try{store.registerEntry(old);}catch(const std::exception& e){stale=std::string(e.what())=="media-identity-binding-conflict";}
  check(stale&&bytes(file)==saved,"late old verified=true callback cannot reverse correction or write media state");
  auto wrong=current;wrong["appId"]=570;bool wrongRejected=false;try{store.registerEntry(wrong);}catch(const std::exception& e){wrongRejected=std::string(e.what())=="media-identity-binding-conflict";}
  check(wrongRejected&&bytes(file)==saved,"cache for a different AppID cannot authorize a rebind");
 });
 run("untrusted-binding-conflict-spends-only-its-own-budget",[&](const fs::path& root){
  const auto exe=utf8((root/L"unconfirmed.exe").wstring());Store store(root);store.registerEntry({{"appId",281610},{"executable",exe},{"verified",true}});
  const auto oldBytes=bytes(root/L"config"/L"game-media.json");Json target={{"appId",883710},{"executable",exe},{"verified",true},{"name","Conflict fixture"},{"lastPlayed",200}};
  // Keep an already accepted canonical item as in the field queue; no exact
  // resolver cache is available for it, so the conflict must remain refused.
  Queue q(root);q.enqueue(Json::array({{{"appId",883710},{"verified",true},{"lastPlayed",200}},{{"appId",777},{"verified",true},{"lastPlayed",100}}}));auto saved=q.document();saved["jobs"]["steam-883710"]["executable"]=exe;atomicWrite(root/L"config"/L"game-media-downloads.json",saved);
  auto e=store.edit({{"appId",777},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});
  const auto stable=bytes(root/L"config"/L"game-media.json");
  for(int n=1;n<=3;++n){const auto token=q.start();check(!token.empty(),"next original bounded retry starts");check(Worker(root,token,GetCurrentProcess()).run()==0,"one rejected binding does not abort the whole worker");const auto after=q.document();check(after["jobs"]["steam-883710"].value("attempts",0)==n,"preparation refusal is charged to that game");check(after["jobs"]["steam-777"].value("state",std::string{})=="done","independent queued game completes in the same worker");if(n<3){check(q.delay()>0,"failed binding waits at original retry deadline");q.update(token,[](Json& j){j["jobs"]["steam-883710"]["retryAt"]=0;});}}
  check(q.start().empty()&&q.delay()<0,"three original attempts stop the restart loop");const auto afterMedia=read(root/L"config"/L"game-media.json"),beforeMedia=Json::parse(stable);check(afterMedia["entries"]["steam-281610"]==beforeMedia["entries"]["steam-281610"]&&afterMedia["aliases"]==beforeMedia["aliases"]&&!afterMedia["entries"].contains("steam-883710"),"untrusted conflict cannot alter retained media or aliases");
 });
 run("accepted-rebind-completes-and-does-not-resurrect-old-publication",[&](const fs::path& root){
  const auto exe=utf8((root/L"cached-correction.exe").wstring());Store store(root);store.registerEntry({{"appId",281610},{"executable",exe},{"verified",true}});cache(store,exe,883710);
  Queue q(root);q.enqueue(Json::array({{{"appId",883710},{"verified",true},{"executable",exe}}}));
  auto e=store.edit({{"appId",883710},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});
  const auto token=q.start();
  if(!token.empty())check(Worker(root,token,GetCurrentProcess()).run()==0,"corrected cached binding completes without network or guessing");
  // Force the original Store admission as it is reached by actual preparation.
  check(store.registerEntry({{"appId",883710},{"verified",true},{"executable",exe}}).value("appId",0)==883710,"cache-proven corrected binding remains usable");
  check(store.resolve({{"executable",exe}}).value("key",std::string{})=="steam-883710","later file consumers do not return to historical wrong AppID");
 });
 run("current-shortcut-return-recovers-only-restored-membership",[&](const fs::path& root){
  fs::create_directories(root/L"bin");const auto helper=root/L"bin"/L"SteamArtworkLab.exe",exe=root/L"fixture-106.exe",vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
  std::ofstream(helper)<<"inert helper, never launched";std::ofstream(exe)<<"inert target, never launched";
  const auto target=row(exe,6);const Json record={{"appid",0x90000006u},{"appname","owned candidate"},{"exe",utf8(exe.wstring())}};
  shortcuts(vdf,Json::array({record}));shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=99;shortcutRegistryFixture=true;
  struct Reset {~Reset(){shortcutRegistryFixture=false;}} reset;
  const auto key=std::string("file-")+digestKey(normalized(utf8(exe.wstring())));
  const auto seed=[&](Queue& q,const char* reason){q.configure(true,"");q.enqueue(Json::array({target}));auto d=q.document();auto& job=d["jobs"][key];job["state"]="unavailable";job["attempts"]=3;job["autoStopped"]=true;job["automaticElapsedMs"]=1234;job["lastError"]=reason;job["stopReason"]="automatic-attempt-budget-exhausted";job["retryAt"]=0;job["identityPrerequisiteStamp"]=identityPrerequisites(helper,utf8(exe.wstring())).stamp;job["unknownDebt"]="preserved";atomicWrite(q.root()/L"config"/L"game-media-downloads.json",d);};
  Queue q(root/L"queue");seed(q,"media-shortcut-no-longer-current");const auto durable=q.root()/L"config"/L"game-media-downloads.json";const auto initial=bytes(durable),toolStamp=identityFileStamp(helper),exeStamp=identityFileStamp(exe);
  check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==initial,"Foreign current account cannot revive a stopped shortcut");
  shortcutFixtureAccount=17;shortcuts(vdf,Json::array());check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==initial,"Removed shortcut cannot be revived by an existing EXE");
  shortcuts(vdf,Json::array({record}));check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==1,"Current restored shortcut remains stopped");
  auto current=q.document()["jobs"][key];check(current.value("attempts",-1)==0&&!itemStopped(current)&&current.value("identityRecoveryReason",std::string{})=="media-shortcut-no-longer-current"&&current["unknownDebt"]=="preserved","Restored membership did not use the original bounded recovery contract");
  check(!current.value("verified",true)&&current.value("appId",1)==0,"Current shortcut membership must not invent a Steam identity");
  const auto resumed=bytes(durable);for(int i=0;i<8;++i)check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==resumed,"Repeated current membership repeatedly refunded attempts");
  seed(q,"media-identity-unconfirmed");const auto ordinary=bytes(durable);check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==ordinary,"Membership recovery refunded an unchanged genuine identity failure");
  seed(q,"media-shortcut-no-longer-current");const auto stopped=bytes(durable);
  HANDLE held=CreateFileW(vdf.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned VDF lock failed");
  const auto locked=recoverCurrentMembership(q,helper,shortcutFixtureCurrent());CloseHandle(held);check(locked==0&&bytes(durable)==stopped,"Unreadable membership proof revived the task");
  int calls=0;const auto changed=[&](const Json&){return ++calls==1;};check(recoverCurrentMembership(q,helper,changed)==0&&calls==2&&bytes(durable)==stopped,"Changed membership at final admission revived the task");
  const auto unknown=[](const Json&)->bool{throw std::runtime_error("media-storage-unavailable");};check(recoverCurrentMembership(q,helper,unknown)==0&&bytes(durable)==stopped,"Membership read error changed stopped debt");
  q.configure(false,"");const auto off=bytes(durable);check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==off,"OFF was bypassed by membership restoration");
  q.configure(true,"game-running");const auto paused=bytes(durable);check(recoverCurrentMembership(q,helper,shortcutFixtureCurrent())==0&&bytes(durable)==paused,"Policy pause was bypassed by membership restoration");
  Queue owned(root/L"owner");seed(owned,"media-shortcut-no-longer-current");shortcutFixtureAccount=99;shortcutAccountReads=0;fixtureSourceAcquires=0;
  {Limits limits;limits.startup=0;limits.recheck=60000;ResourcePorts resources;resources.ac=[](){return 0;};Supervisor owner(owned.root(),helper.parent_path()/L"CustomSteamLibrary.exe",limits,resources);owner.wake(true);
   auto until=GetTickCount64()+1500;while(GetTickCount64()<until&&fixtureSourceAcquires.load()==0)Sleep(2);check(fixtureSourceAcquires.load()>0&&shortcutAccountReads.load()==0,"Original owner wake did not validate current shortcut membership");
   check(owned.document()["jobs"][key].value("attempts",-1)==3,"Owner revived a foreign-account shortcut");
   shortcutFixtureAccount=17;owner.wake();until=GetTickCount64()+1500;while(GetTickCount64()<until&&owned.document()["jobs"][key].value("attempts",-1)!=0)Sleep(2);
   check(owned.document()["jobs"][key].value("attempts",-1)==0&&fixtureSourceAcquires.load()>0&&shortcutAccountReads.load()==0,"Original owner wake did not recover restored membership");
  }
  check(owned.document().value("workerPid",DWORD{0})==0&&owned.document()["active"].empty(),"Resource-blocked owner left a media worker or active task");
  check(toolStamp==identityFileStamp(helper)&&exeStamp==identityFileStamp(exe),"Membership restoration required helper/EXE replacement");
  // A repaired consumer is a new prerequisite even when the resolver/game
  // files have not changed. One bounded reset only; never a periodic refund.
  const auto priorStamp=digestKey("identity-network-receipt-v1\n"+toolStamp+"\n"+exeStamp);
  const auto makePrior=[&](Queue& queue,const char* reason){seed(queue,reason);auto state=queue.document();state["jobs"][key]["identityPrerequisiteStamp"]=priorStamp;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",state);};
  Queue upgraded(root/L"manifest-upgrade");makePrior(upgraded,"media-identity-unconfirmed");const auto recovered=recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent());
  Json upgradeObservation={{"recovered",recovered},{"priorStamp",priorStamp},{"currentStamp",identityPrerequisites(helper,utf8(exe.wstring())).stamp},{"helperSame",toolStamp==identityFileStamp(helper)},{"targetSame",exeStamp==identityFileStamp(exe)}};
  atomicWrite(root/L"manifest-upgrade-observation.json",upgradeObservation);
  check(recovered==1,"REPAIRED_MANIFEST_CONSUMER_CANNOT_REVIVE_OLD_STOPPED_TASK");
  auto repaired=upgraded.document()["jobs"][key];check(!itemStopped(repaired)&&repaired["attempts"]==0&&repaired["automaticElapsedMs"]==0&&repaired["unknownDebt"]=="preserved"&&repaired["identityPrerequisiteStamp"]!=priorStamp,"Manifest repair did not retain original bounded reset/data");
  const auto once=bytes(upgraded.root()/L"config"/L"game-media-downloads.json");for(int n=0;n<8;++n)check(recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent())==0&&bytes(upgraded.root()/L"config"/L"game-media-downloads.json")==once,"Repeated observation refunded repaired task");
  seed(upgraded,"media-identity-unconfirmed");const auto unchanged=bytes(upgraded.root()/L"config"/L"game-media-downloads.json");check(recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent())==0&&bytes(upgraded.root()/L"config"/L"game-media-downloads.json")==unchanged,"Same repaired consumer refunded genuine failure");
  for(const char* reason:{"media-identity-network-unavailable","media-network-request-failed"}){makePrior(upgraded,reason);const auto previous=bytes(upgraded.root()/L"config"/L"game-media-downloads.json");check(recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent())==0&&bytes(upgraded.root()/L"config"/L"game-media-downloads.json")==previous,"Manifest epoch refunded unrelated network failure");}
  makePrior(upgraded,"media-identity-unconfirmed");upgraded.configure(false,"");const auto disabled=bytes(upgraded.root()/L"config"/L"game-media-downloads.json");check(recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent())==0&&bytes(upgraded.root()/L"config"/L"game-media-downloads.json")==disabled,"Manifest repair bypassed OFF");
  upgraded.configure(true,"game-running");const auto policy=bytes(upgraded.root()/L"config"/L"game-media-downloads.json");check(recoverCurrentMembership(upgraded,helper,shortcutFixtureCurrent())==0&&bytes(upgraded.root()/L"config"/L"game-media-downloads.json")==policy,"Manifest repair bypassed game-running policy");

 });
 run("verified-shortcut-return-retains-current-identity",[&](const fs::path& root){
  const auto helper=root/L"bin"/L"SteamArtworkLab.exe",exe=root/L"game.exe",vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
  fs::create_directories(root/L"bin");std::ofstream(exe)<<"inert exact game, never launched";const auto file=utf8(exe.wstring());
  Queue queue(root/L"data");Store store(queue.root());cache(store,file,106);auto target=row(exe,6);target["appId"]=106;target["verified"]=true;
  const Json record={{"appid",0x90000006u},{"appname","owned verified shortcut"},{"exe",file}};
  shortcuts(vdf,Json::array({record}));shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=99;shortcutRegistryFixture=true;struct Reset{~Reset(){shortcutRegistryFixture=false;}}reset;
  queue.configure(true,"");queue.enqueue(Json::array({target}));const auto key=std::string("steam-106");const auto durable=queue.root()/L"config"/L"game-media-downloads.json";
  check(queue.document()["jobs"].contains(key)&&!queue.document()["jobs"][key].value("identityPending",false),"Verified fixture was not admitted as a canonical media job");
  IdentityPorts membershipPorts;membershipPorts.source=shortcutFixtureSource();
  for(int n=1;n<=3;++n){const auto token=queue.start();check(!token.empty(),"Original verified shortcut attempt did not start");check(Worker(queue.root(),token,GetCurrentProcess(),{},membershipPorts).run()==0,"Actual Worker failed to settle stale verified membership");const auto job=queue.document()["jobs"][key];check(job.value("attempts",0)==n&&job.value("lastError",std::string{})=="media-shortcut-no-longer-current","Verified stale shortcut did not spend its original bounded attempts");if(n<3)queue.update(token,[&](Json& j){j["jobs"][key]["retryAt"]=0;});}
  check(itemStopped(queue.document()["jobs"][key]),"Actual Worker did not retain exhausted verified shortcut");
  const auto original=bytes(durable);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==original,"Foreign account revived verified shortcut");
  shortcutFixtureAccount=17;check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==1,"Verified restored shortcut remains stopped");
  const auto recovered=queue.document()["jobs"][key];check(recovered.value("appId",0)==106&&recovered.value("verified",false)&&!recovered.value("identityPending",false)&&recovered.value("attempts",-1)==0&&!itemStopped(recovered),"Verified recovery changed identity or missed original bounded reset");
  check(!fs::exists(helper),"Verified recovery unexpectedly required or created an identity helper");
  const auto current=bytes(durable);for(int i=0;i<8;++i)check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==current,"Repeated verified membership refunded attempts");
  const auto stop=[&](const char* reason="media-shortcut-no-longer-current"){auto j=queue.document();auto& job=j["jobs"][key];job["state"]="unavailable";job["autoStopped"]=true;job["attempts"]=3;job["automaticElapsedMs"]=1234;job["lastError"]=reason;job["stopReason"]="automatic-attempt-budget-exhausted";job["unknownDebt"]="keep";atomicWrite(durable,j);};
  stop();const auto stopped=bytes(durable);shortcuts(vdf,Json::array());check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==stopped,"Removed shortcut revived canonical identity");shortcuts(vdf,Json::array({record}));
  HANDLE held=CreateFileW(vdf.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned verified VDF lock failed");const auto locked=queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent());CloseHandle(held);check(locked==0&&bytes(durable)==stopped,"Unreadable shortcut revived verified job");
  const auto identityPath=custom_steam_library::identity_cache::cachePath(queue.root(),file);held=CreateFileW(custom_steam_library::ioPath(identityPath).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned exact identity cache lock failed");const auto cacheLocked=queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent());CloseHandle(held);check(cacheLocked==0&&bytes(durable)==stopped,"Unreadable identity cache revived canonical job");
  cache(store,file,777);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==stopped,"Corrected different AppID revived old canonical job");cache(store,file,106);
  const auto changedMembership=shortcutFixtureCurrent();
  int calls=0;const auto changed=[&](const Json& job){++calls;if(calls==2)cache(store,file,777);return changedMembership(job);};check(queue.recoverIdentityPrerequisites(helper,changed)==0&&calls==2&&bytes(durable)==stopped,"Late identity correction revived old canonical job");cache(store,file,106);
  calls=0;const auto removed=[&](const Json&){return ++calls==1;};check(queue.recoverIdentityPrerequisites(helper,removed)==0&&calls==2&&bytes(durable)==stopped,"Changed final membership revived canonical job");
  fs::remove(exe);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==stopped,"Missing game EXE revived verified shortcut");std::ofstream(exe)<<"restored inert game";
  queue.configure(false,"");const auto off=bytes(durable);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==off,"OFF was bypassed for verified recovery");queue.configure(true,"game-running");const auto paused=bytes(durable);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==paused,"Policy pause was bypassed for verified recovery");queue.configure(true,"");
  stop("media-network-request-failed");const auto network=bytes(durable);check(queue.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&bytes(durable)==network,"Membership recovery refunded unrelated download failures");
  shortcutAccountReads=0;fixtureSourceAcquires=0;
  stop();{Limits limits;limits.startup=0;limits.recheck=60000;ResourcePorts resources;resources.ac=[](){return 0;};Supervisor owner(queue.root(),helper.parent_path()/L"CustomSteamLibrary.exe",limits,resources);owner.wake(true);const auto until=GetTickCount64()+1500;while(GetTickCount64()<until&&queue.document()["jobs"][key].value("attempts",-1)!=0)Sleep(2);check(queue.document()["jobs"][key].value("attempts",-1)==0&&fixtureSourceAcquires.load()>0&&shortcutAccountReads.load()==0,"Existing owner wake did not recover current verified shortcut");}
  const auto final=queue.document();check(final.value("workerPid",DWORD{0})==0&&final["active"].empty()&&final["jobs"][key]["unknownDebt"]=="keep","Verified recovery lost retained metadata or bypassed launch protection");
  Queue pending(root/L"cached-placeholder");Store pendingStore(pending.root());const auto placeholder=row(exe,6);pending.configure(true,"");pending.enqueue(Json::array({placeholder}));const auto fileKey=pending.document()["jobs"].begin().key();auto stoppedPlaceholder=pending.document();auto& old=stoppedPlaceholder["jobs"][fileKey];old["state"]="unavailable";old["autoStopped"]=true;old["attempts"]=3;old["automaticElapsedMs"]=1234;old["retryAt"]=0;old["lastError"]="media-identity-unconfirmed";old["stopReason"]="automatic-attempt-budget-exhausted";old["identityPrerequisiteStamp"]=identityPrerequisites(helper,file).stamp;old["unknownDebt"]="preserve-placeholder";atomicWrite(pending.root()/L"config"/L"game-media-downloads.json",stoppedPlaceholder);
  check(pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&pending.document()==stoppedPlaceholder,"Missing helper and absent verified cache refunded pending identity");cache(pendingStore,file,106);
  shortcutFixtureAccount=99;check(pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&pending.document()==stoppedPlaceholder,"Verified cache revived a foreign-account placeholder");shortcutFixtureAccount=17;
  shortcuts(vdf,Json::array());check(pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&pending.document()==stoppedPlaceholder,"Verified cache revived a removed shortcut");shortcuts(vdf,Json::array({record}));
  const auto pendingCache=custom_steam_library::identity_cache::cachePath(pending.root(),file);held=CreateFileW(custom_steam_library::ioPath(pendingCache).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned pending cache lock failed");const auto pendingLocked=pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent());CloseHandle(held);check(pendingLocked==0&&pending.document()==stoppedPlaceholder,"Unreadable cache revived stopped placeholder");
  const auto correctedMembership=shortcutFixtureCurrent();
  calls=0;const auto corrected=[&](const Json& job){if(++calls==2)cache(pendingStore,file,777);return correctedMembership(job);};check(pending.recoverIdentityPrerequisites(helper,corrected)==0&&pending.document()==stoppedPlaceholder,"Late cache identity change revived stopped placeholder");const auto lateIdentityCalls=calls;cache(pendingStore,file,106);
  calls=0;const auto retired=[&](const Json&){return ++calls==1;};check(pending.recoverIdentityPrerequisites(helper,retired)==0&&pending.document()==stoppedPlaceholder,"Late membership change revived stopped placeholder");const auto lateMembershipCalls=calls;
  check(pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==1,"Verified cached identity did not revive stopped placeholder without helper");check(lateIdentityCalls==2&&lateMembershipCalls==2,"Cached identity proof did not recheck both lock boundaries");
  const auto revived=pending.document();check(revived["jobs"][fileKey].value("attempts",-1)==0&&!itemStopped(revived["jobs"][fileKey])&&revived["jobs"][fileKey]["unknownDebt"]=="preserve-placeholder","Cached recovery changed retained metadata or skipped original bounded reset");check(pending.recoverIdentityPrerequisites(helper,shortcutFixtureCurrent())==0&&pending.document()==revived,"Repeated cache observation refunded pending identity");
  auto tomb=pendingStore.registerEntry({{"appId",106},{"verified",true}});tomb["action"]="delete";tomb["slot"]="static";tomb=pendingStore.edit(tomb);tomb["action"]="delete";tomb["slot"]="dynamic";pendingStore.edit(tomb);
  IdentityPorts cachePorts;cachePorts.appRoot=helper.parent_path();cachePorts.source=shortcutFixtureSource();selectedVideoNoNetwork=true;selectedVideoNetworkAttempts=0;struct ResetNetwork{~ResetNetwork(){selectedVideoNoNetwork=false;}}resetNetwork;
  const auto cachedToken=pending.start();check(!cachedToken.empty()&&Worker(pending.root(),cachedToken,GetCurrentProcess(),{},cachePorts).run()==0,"Cached identity did not complete through the original Worker");
  check(!pending.document()["jobs"].contains(fileKey)&&pending.document()["active"].empty()&&pending.document().value("workerPid",DWORD{0})==0,"Cached identity placeholder was not retired or retained active ownership");check(!fs::exists(helper)&&selectedVideoNetworkAttempts.load()==0&&pendingStore.identity(file).value("appId",0)==106,"Cached recovery launched/created helper, reached network or changed identity");

 });
 run("shortcut-refusal-keeps-original-observation",[&](const fs::path& base){
   mediaSleepFixture::bind();
  wchar_t suffix[256]{};const auto suffixLength=GetEnvironmentVariableW(L"YMCC_SHORTCUT_REFUSAL_SUFFIX",suffix,256);
  if(suffixLength>=256||(suffixLength&&std::wstring_view(suffix)!=L"verified-after-different-account"))throw std::runtime_error("invalid original refusal suffix selector");
  Json executed=Json::array();
  for(const bool canonical:{false,true})for(const auto* mode:{"different-account","empty-account","registry-unavailable","root-unavailable","removed","changed-exe","changed-during-read","malformed","storage","provider"}){
   if(suffixLength&&(!canonical||std::string_view(mode)=="different-account"))continue;
   executed.push_back(std::string(canonical?"verified:":"pending:")+mode);
   const std::string variant=mode;const auto root=base/(canonical?L"verified":L"pending")/wide(variant);fs::create_directories(root);const auto exe=root/L"game.exe",vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";std::ofstream(exe)<<"owned inert target";const auto file=utf8(exe.wstring());Queue q(root/L"data");Store store(q.root());if(canonical)cache(store,file,106);auto target=row(exe,6);q.configure(true,"");q.enqueue(Json::array({target}));const auto key=q.document()["jobs"].begin().key();
   Json record={{"appid",0x90000006u},{"appname","owned shortcut"},{"exe",file}};if(variant=="changed-exe")record["exe"]=utf8((root/L"other.exe").wstring());shortcuts(vdf,variant=="removed"?Json::array():Json::array({record}));if(variant=="malformed")std::ofstream(vdf,std::ios::binary|std::ios::trunc)<<"malformed";
   shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=variant=="different-account"?99:variant=="empty-account"?0:17;shortcutFixtureAccountAfter=variant=="changed-during-read"?99:0;shortcutFixtureAccountError=variant=="registry-unavailable"?ERROR_FILE_NOT_FOUND:ERROR_SUCCESS;shortcutFixtureRootError=variant=="root-unavailable"?ERROR_PATH_NOT_FOUND:ERROR_SUCCESS;shortcutAccountReads=0;shortcutRegistryFixture=true;selectedVideoNoNetwork=true;selectedVideoNetworkAttempts=0;
   struct Reset{~Reset(){shortcutRegistryFixture=false;shortcutFixtureAccountAfter=0;shortcutFixtureAccountError=ERROR_SUCCESS;shortcutFixtureRootError=ERROR_SUCCESS;selectedVideoNoNetwork=false;}}reset;
   HANDLE held=INVALID_HANDLE_VALUE;if(variant=="storage"){held=CreateFileW(vdf.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned diagnostic VDF lock failed");}struct Close{HANDLE value;~Close(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}}close{held};
   IdentityPorts ports;if(variant=="provider")ports.current=[](const Json&){return false;};
   std::shared_ptr<ShortcutFixtureSource> source;
   if(variant!="provider"){source=shortcutFixtureSource(variant=="changed-during-read");ports.source=source;}
   const bool factsUnknown=variant=="empty-account"||variant=="registry-unavailable"||variant=="root-unavailable"||variant=="changed-during-read";
   const auto before=q.document()["jobs"][key];const auto started=now();
    const auto token=q.start();check(!token.empty()&&Worker(q.root(),token,GetCurrentProcess(),{},ports,{},mediaSleepFixture::permit()).run()==0,"Original refusal Worker did not settle");const auto settled=now();const auto doc=q.document(),job=doc["jobs"][key];const auto observed=job.value("lastShortcutObservation",Json::object());
   if(variant=="storage"){check(job.value("lastError",std::string{})=="media-storage-unavailable"&&job.value("attempts",-1)==0&&observed.empty(),"Storage refusal acquired account authority or spent an identity attempt");}
   else if(factsUnknown){
    // Former registry-specific reasons are retained in original.cpp/HUNKS.
    // Unavailable account/root or a revoked source is Unknown, never mismatch.
    check(job.value("lastError",std::string{})=="media-steam-facts-unavailable"&&job.value("attempts",-1)==before.value("attempts",-2)&&job.value("automaticElapsedMs",int64_t{0})==before.value("automaticElapsedMs",int64_t{0})&&!itemStopped(job),"Unknown shared source spent or refunded original identity debt");
    check(job.value("retryAt",int64_t{0})>=started+retryMs&&job.value("retryAt",int64_t{0})<=settled+retryMs,"Unknown shared source lost the original lane20s backoff");
    check(observed.empty(),"Unknown source invented a definite shortcut or registry refusal");
    if(variant=="changed-during-read"){
     check(source->snapshot().selectedAccount==17&&source->snapshot().facts.registryAccount==17&&!source->current(),"Revoked immutable source fabricated a later account observation");
     check(WaitForSingleObject(static_cast<HANDLE>(source->cancellationEvents()[2]),0)==WAIT_OBJECT_0&&!source->current(),"Revoked source became current again or lost its cancellation event");
    }
   }else{
    const std::string reason=variant=="different-account"?"active-account-mismatch":variant=="removed"?"shortcut-removed":variant=="changed-exe"?"shortcut-target-changed":variant=="provider"?"membership-provider-refused":"shortcut-data-invalid";
    check(job.value("lastError",std::string{})=="media-shortcut-no-longer-current"&&job.value("attempts",0)==1,"Diagnostic changed original membership error or attempt budget");
     check(itemStopped(job)==(!canonical&&variant=="changed-exe"),"First stop must require an exact changed target on a pending shortcut");
    check(stringField(observed,"reason")==reason,"Shortcut refusal discarded its original read observation");
    check(observed.value("observedAt",int64_t{0})>0&&stringField(observed,"expectedAccount")=="17","Shortcut observation lacks its current result owner");
    if(variant=="different-account")check(observed.value("activeAccount",DWORD{0})==99,"Observed foreign account was guessed or lost");
   }
   // These are consumer inputs. No source call count stands in for registry IO.
   check(shortcutAccountReads.load()==0,"Explicit source consumer performed a private registry observation");check(selectedVideoNetworkAttempts.load()==0&&doc["active"].empty()&&doc.value("workerPid",DWORD{0})==0,"Refusal diagnostic reached network or retained Worker ownership");atomicWrite(root/L"observed.json",{{"variant",variant},{"canonical",canonical},{"accountReads",shortcutAccountReads.load()},{"contract",factsUnknown?"shared-source-unknown":"original-membership-or-storage"},{"job",job}});
  }
  check(executed.size()==(suffixLength?9u:20u),"Original refusal suffix selection is exact");
  atomicWrite(base/L"selection.json",{{"suffix",suffixLength?"verified-after-different-account":"all"},{"executed",executed}});
 });
 run("explicit-video-choice-keeps-its-request-owner",[&](const fs::path& base){
  for(const auto* mode:{"explicit","automatic","bulk","cancelled","expired","deleted","changed-store","changed-revision","missing-token","wrong-appid"}){
   const auto root=base/wide(mode),exe=root/L"game.exe";fs::create_directories(root);std::ofstream(exe)<<"owned inert game";const auto file=utf8(exe.wstring());
   Queue q(root/L"data");Store store(q.root());cache(store,file,106);auto target=row(exe,6);target["appId"]=106;target["verified"]=true;
   q.configure(true,"");q.enqueue(Json::array({target}));auto entry=store.registerEntry(target);
   const std::string key="steam-106",url="https://video.steamstatic.com/store_trailers/106/chosen/master.m3u8";
   const auto catalog=q.root()/L"cache"/L"jobs"/L"media-video-search"/L"steam-106.json";
   atomicWrite(catalog,{{"entryKey",key},{"appId",106},{"bindingRevision",entry["bindingRevision"]},{"searchToken","owned-catalog"},{"createdAt",now()},{"candidates",Json::array({{{"id","0"},{"url",url},{"title","owned official choice"}}})}});
   Json request={{"key",key},{"appId",106},{"executable",file},{"bindingRevision",entry["bindingRevision"]},{"searchToken","owned-catalog"},{"candidateId","0"}};
   entry=selectVideoForDownload(q.root(),request);const auto retained=q.document()["jobs"][key]["localShortcut"];
   check(stringField(q.document()["jobs"][key],"catalogSource")=="steam-shortcut","Fixture did not retain pre-existing shortcut provenance");
   const std::string variant=mode;
   if(variant=="automatic")q.cancelManual();
   if(variant=="bulk"){q.cancelManual();q.requestSupplement(true);q.inventoryReady(q.snapshot().value("inventoryRequestId",std::string{}));}
   if(variant=="cancelled"){q.cancelManual();q.configure(false,"");}
   if(variant=="deleted"){entry["action"]="delete";entry["slot"]="dynamic";store.edit(entry);}
   if(variant=="changed-store"){entry["action"]="rename";entry["name"]="newer editor decision";store.edit(entry);}
   if(variant=="expired"||variant=="changed-revision"||variant=="missing-token"||variant=="wrong-appid"){
    auto doc=q.document();if(variant=="expired")doc["manualUntil"]=now()-1;
    if(variant=="changed-revision")doc["manualTargetRevisions"][key]=entry["bindingRevision"].get<uint64_t>()+1;
    if(variant=="missing-token")doc["jobs"][key]["selectedVideo"].erase("selectionToken");
    if(variant=="wrong-appid")doc["jobs"][key]["selectedVideo"]["appId"]=777;
    atomicWrite(q.root()/L"config"/L"game-media-downloads.json",doc);
   }
   selectedVideoNetworkAttempts=0;selectedVideoNoNetwork=true;struct Reset{~Reset(){selectedVideoNoNetwork=false;}} reset;
   const auto token=q.start();if(variant=="cancelled"){check(token.empty()&&selectedVideoNetworkAttempts.load()==0,"Cancelled OFF selection started a transfer");continue;}
   check(!token.empty(),"Owned selected-video Worker did not start");IdentityPorts ports;unsigned membershipReads=0;ports.current=[&](const Json&){++membershipReads;return false;};
   check(Worker(q.root(),token,GetCurrentProcess(),{},ports).run()==0,"Controlled original Worker did not settle its request");const auto job=q.document()["jobs"][key];
   atomicWrite(root/L"observed.json",{{"variant",variant},{"networkSessionAttempts",selectedVideoNetworkAttempts.load()},{"membershipReads",membershipReads},{"lastError",job.value("lastError",std::string{})},{"job",job},{"scope","original selection/Queue/Worker; owned WinHttpOpen refusal, no HTTP session or external destination"}});
   if(variant=="explicit"){
    check(selectedVideoNetworkAttempts.load()>0,"Accepted video choice was blocked by retained shortcut owner");
    check(selectedVideoNetworkAttempts.load()==custom_steam_library::network_policy::maxAttempts&&job.value("lastError",std::string{})=="media-network-open-failed","Selected request did not retain original weak-network attempts/diagnostic");
    check(membershipReads==0&&job["selectedVideo"]==entry["selectedVideo"],"Explicit current choice borrowed a shortcut account or lost the chosen video");
   }else check(selectedVideoNetworkAttempts.load()==0&&membershipReads>0&&job.value("lastError",std::string{})=="media-shortcut-no-longer-current","Automatic or revoked choice bypassed original shortcut membership gate");
   check(job["localShortcut"]==retained&&stringField(job,"catalogSource")=="steam-shortcut","Explicit request permanently rewrote automatic shortcut provenance");
  }
 });
 run("selected-video-retry-keeps-original-budget",[&](const fs::path& base){
  const std::string key="steam-106";
  const auto setup=[&](const fs::path& root){fs::create_directories(root);const auto exe=root/L"game.exe";std::ofstream(exe)<<"owned inert game";const auto file=utf8(exe.wstring());Queue q(root/L"data");Store store(q.root());cache(store,file,106);auto target=row(exe,6);target["appId"]=106;target["verified"]=true;q.configure(false,"");q.enqueue(Json::array({target}));auto e=store.registerEntry(target);atomicWrite(q.root()/L"cache"/L"jobs"/L"media-video-search"/L"steam-106.json",{{"entryKey",key},{"appId",106},{"bindingRevision",e["bindingRevision"]},{"searchToken","retry-catalog"},{"createdAt",now()},{"candidates",Json::array({{{"id","0"},{"url","https://video.steamstatic.com/store_trailers/106/chosen/master.m3u8"}}})}});Json request={{"key",key},{"appId",106},{"executable",file},{"bindingRevision",e["bindingRevision"]},{"searchToken","retry-catalog"},{"candidateId","0"}};selectVideoForDownload(q.root(),request);return request;};
  const auto root=base/L"retry";const auto request=setup(root);Queue q(root/L"data");Store store(q.root());const auto original=q.document();const auto deadline=original["manualUntil"],requestId=original["manualRequestId"];int64_t previousSpent=0;
  selectedVideoNetworkAttempts=0;selectedVideoNoNetwork=true;struct Reset{~Reset(){selectedVideoNoNetwork=false;}} reset;
  for(int n=1;n<=3;++n){const auto token=q.start();check(!token.empty(),"Original selected-video retry did not start");IdentityPorts ports;ports.current=[](const Json&){return false;};check(Worker(q.root(),token,GetCurrentProcess(),{},ports).run()==0,"Controlled selected-video Worker did not settle");const auto state=q.document();const auto& job=state["jobs"][key];
   check(job.value("attempts",0)==n&&job.value("lastError",std::string{})=="media-network-open-failed","Retry lost original attempts or returned to shortcut account gate");const auto spent=job.value("automaticElapsedMs",int64_t{0});check(spent>previousSpent&&spent<=automaticGameMs,"Retry refunded or expanded original cumulative time");previousSpent=spent;
   if(n<3){check(manualRun(state)&&state["manualUntil"]==deadline&&state["manualRequestId"]==requestId,"Retryable selected video lost its original manual window");check(q.delay()>0&&q.start().empty(),"Selected-video retry skipped original backoff");const auto stable=q.document();selectVideoForDownload(q.root(),request);check(q.document()==stable,"Lost response retry refunded attempts or extended deadline");auto media=read(q.root()/L"config"/L"game-media.json");media["entries"][key]["retryAt"]=0;atomicWrite(q.root()/L"config"/L"game-media.json",media);q.update(token,[&](Json& j){j["jobs"][key]["retryAt"]=0;});}
   else check(itemStopped(job)&&!manualRun(state)&&q.start().empty(),"Three original attempts did not stop the manual request");
  }
  check(selectedVideoNetworkAttempts.load()==3*custom_steam_library::network_policy::maxAttempts,"Manual retries did not reuse original four-attempt weak-network policy");atomicWrite(base/L"retry-final.json",q.document());
  for(const auto* mode:{"cancelled","paused","deadline","edited","elapsed-backoff","nonretryable"}){const auto rr=base/wide(mode);setup(rr);Queue queue(rr/L"data");Store media(queue.root());const auto token=queue.start();queue.claim(token);queue.result(token,key,false,std::string_view(mode)=="nonretryable"?"media-http-403":"media-network-read-failed");const std::string variant=mode;
   if(variant=="elapsed-backoff")queue.update(token,[&](Json& j){j["jobs"][key]["retryAt"]=now()-1;});
   if(variant=="cancelled")queue.cancelManual();
   if(variant=="deadline")queue.update(token,[&](Json& j){j["manualUntil"]=now()+1000;});
   if(variant=="edited"){auto e=media.resolve({{"key",key}});e["action"]="rename";e["name"]="newer edit";media.edit(e);}
   queue.end(token,variant=="paused"?"paused":"deferred",variant=="paused"?"download-paused":"");if(variant=="elapsed-backoff")check(manualRun(queue.document())&&queue.delay()==0,"Scheduling delay past backoff ended a still-current manual request");else check(!manualRun(queue.document()),"Cancellation, power pause, deadline, superseding edit or permanent HTTP refusal revived the old manual request");
  }
  const auto pp=base/L"partial";const auto partialRequest=setup(pp);Queue partial(pp/L"data");Store media(partial.root());const auto initial=partial.document();const auto token=partial.start();partial.update(token,[](Json& j){j["workerPid"]=GetCurrentProcessId();j["workerBirth"]=processBirthToken(GetCurrentProcessId());});const auto claim=partial.claim(token);auto lease=media.begin(claim,partial.document().value("manualRequestId",std::string{}));check(lease.value("acquired",false),"Partial publication fixture did not own a real Store lease");const auto poster=pp/L"poster.jpg";{std::ofstream out(poster,std::ios::binary);out.write("\xff\xd8\xff\xe0",4);}const auto published=media.publish(lease,poster,false);check(partial.advancePublishedBinding(token,key,lease,published),"Original partial Store/Queue publication was not accepted");const auto after=partial.document();
  check(after["manualTargetRevisions"][key]==published["bindingRevision"],"Partial publication lost accepted manual selection revision");check(after["manualUntil"]==initial["manualUntil"]&&after["manualRequestId"]==initial["manualRequestId"]&&after["jobs"][key]["attempts"]==initial["jobs"][key]["attempts"],"Partial publication extended deadline, changed request owner or refunded attempts");
  check(partial.currentManualVideoSelection(token,after["active"][key]),"Owned static completion revoked pending selected video");bool acknowledged=false;Json ack;try{ack=selectVideoForDownload(partial.root(),partialRequest);acknowledged=true;}catch(...){}
  check(acknowledged,"Owned partial publication made exact selection retry look stale");check(ack["bindingRevision"]==published["bindingRevision"]&&partial.document()==after,"Partial acknowledgement changed current binding, deadline or retry debt");
  auto missing=after;missing["manualTargetRevisions"].erase(key);atomicWrite(partial.root()/L"config"/L"game-media-downloads.json",missing);bool denied=false;try{selectVideoForDownload(partial.root(),partialRequest);}catch(...){denied=true;}check(denied&&partial.document()==missing,"Missing accepted partial receipt authorized an old candidate");atomicWrite(partial.root()/L"config"/L"game-media-downloads.json",after);
  auto foreign=partialRequest;foreign["candidateId"]="other";denied=false;try{selectVideoForDownload(partial.root(),foreign);}catch(...){denied=true;}check(denied&&partial.document()==after,"Accepted partial receipt authorized a different candidate");
  auto cancelled=after;cancelled["manualUntil"]=0;cancelled["manualState"]="cancelled";atomicWrite(partial.root()/L"config"/L"game-media-downloads.json",cancelled);denied=false;try{selectVideoForDownload(partial.root(),partialRequest);}catch(...){denied=true;}check(denied&&partial.document()==cancelled,"Partial acknowledgement restarted a cancelled selection");atomicWrite(partial.root()/L"config"/L"game-media-downloads.json",after);
  auto failed=published;failed["reason"]="media-network-read-failed";media.fail(failed);partial.result(token,key,false,"media-network-read-failed");partial.end(token,"deferred");check(manualRun(partial.document()),"Static success followed by video network failure ended the original bounded selection");atomicWrite(base/L"partial-final.json",partial.document());
  const auto cp=base/L"completed";const auto completedRequest=setup(cp);Queue completed(cp/L"data");Store finishedMedia(completed.root());const auto accepted=completed.document();const auto ct=completed.start();completed.update(ct,[](Json& j){j["workerPid"]=GetCurrentProcessId();j["workerBirth"]=processBirthToken(GetCurrentProcessId());});const auto cc=completed.claim(ct);auto cl=finishedMedia.begin(cc,completed.document().value("manualRequestId",std::string{}));check(cl.value("acquired",false),"Final acknowledgement fixture did not own a Store lease");
  const auto movie=cp/L"selected.mp4";{std::ofstream out(movie,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}auto publication=finishedMedia.publish(cl,movie,false);
  check(!pendingMediaRefresh(publication,"dynamic")&&publication.value("dynamicReady",false),"Final acknowledgement fixture did not publish its selected video");
  const auto beforeFinal=completed.document();auto automatic=beforeFinal;automatic["enabled"]=true;automatic["manualUntil"]=0;automatic["manualState"]="cancelled";atomicWrite(completed.root()/L"config"/L"game-media-downloads.json",automatic);check(!completed.advancePublishedBinding(ct,key,cl,publication)&&completed.document()==automatic,"Automatic final publication borrowed an expired manual receipt");atomicWrite(completed.root()/L"config"/L"game-media-downloads.json",beforeFinal);
  auto unaccepted=beforeFinal;unaccepted["manualTargetRevisions"].erase(key);atomicWrite(completed.root()/L"config"/L"game-media-downloads.json",unaccepted);check(!completed.advancePublishedBinding(ct,key,cl,publication)&&completed.document()==unaccepted,"Final publication advanced without its accepted manual receipt");atomicWrite(completed.root()/L"config"/L"game-media-downloads.json",beforeFinal);
  check(completed.advancePublishedBinding(ct,key,cl,publication),"Owned final publication did not advance its accepted selection receipt");
  finishedMedia.finish(publication);completed.result(ct,key,true);completed.end(ct,"completed");const auto terminal=completed.document();const auto currentEntry=finishedMedia.resolve({{"key",key}});
  check(terminal["manualTargetRevisions"][key]==publication["bindingRevision"]&&terminal["jobs"][key]["requestBindingRevision"]==publication["bindingRevision"],"Final publication lost its current accepted binding");
  check(terminal["jobs"][key]["state"]=="done"&&!manualRun(terminal)&&terminal["manualRequestId"]==accepted["manualRequestId"],"Final publication did not preserve natural completion and original request owner");
  bool completionAck=false;Json completedReply;try{completedReply=selectVideoForDownload(completed.root(),completedRequest);completionAck=true;}catch(...){}
  check(completionAck&&completedReply["bindingRevision"]==publication["bindingRevision"],"Completed selected video rejected its exact lost-response acknowledgement");check(completed.document()==terminal&&finishedMedia.resolve({{"key",key}})==currentEntry,"Completed acknowledgement restarted work or changed retained media");
  auto changed=currentEntry;changed["action"]="mode";changed["mode"]="static";finishedMedia.edit(changed);const auto edited=finishedMedia.resolve({{"key",key}});denied=false;try{selectVideoForDownload(completed.root(),completedRequest);}catch(...){denied=true;}check(denied&&completed.document()==terminal&&finishedMedia.resolve({{"key",key}})==edited,"Later user edit borrowed a completed selection receipt");
 });
 bool ok=!rows.empty();for(const auto& row:rows)ok=ok&&row.value("ok",false);const Json report={{"ok",ok},{"checks",checks},{"cases",rows},{"scope","production Store/Queue/Worker; isolated metadata, synthetic exact-file cache and explicit AC port; no external HTTP, Steam or user data"}};atomicWrite(base/L"result.json",report);std::cout<<report.dump()<<std::endl;return ok?0:2;
}
static void shortcutReadIoProbe(const fs::path& root,const fs::path& self){
 const auto bin=root/L"bin",exe=root/L"fixture-106.exe",vdf=root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
 fs::create_directories(bin);fs::copy_file(self,bin/L"SteamArtworkLab.exe");std::ofstream(exe)<<"inert target, never executed";
 const auto target=row(exe,6);const Json record={{"appid",0x90000006u},{"appname","owned candidate"},{"exe",utf8(exe.wstring())}};
 shortcuts(vdf,Json::array({record}));shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
 struct Reset{~Reset(){shortcutRegistryFixture=false;}}reset;
 const auto seed=[&](Queue& queue,Store& store,const Json& item){queue.enqueue(Json::array({item,{{"appId",777},{"verified",true}}}));for(const int id:{106,777}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}};
 Queue queue(root/L"data");Store store(queue.root());seed(queue,store,target);const auto key=std::string("file-")+digestKey(normalized(stringField(target,"executable")));
 auto state=queue.document();state["jobs"][key]["attempts"]=2;state["jobs"][key]["automaticElapsedMs"]=1234;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",state);
 struct Held{HANDLE file=INVALID_HANDLE_VALUE;~Held(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}}held;
 held.file=CreateFileW(vdf.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held.file!=INVALID_HANDLE_VALUE,"owned Steam shortcut file sharing denial is active");
 IdentityPorts ports;ports.appRoot=bin;ports.timeoutMs=3000;ports.source=shortcutFixtureSource(); // default current consumes explicit source and the original real VDF reader
 const auto token=queue.start();check(!token.empty()&&Worker(queue.root(),token,GetCurrentProcess(),{},ports).run()==0,"shortcut read denial returns through the original worker");
 state=queue.document();atomicWrite(root/L"blocked-queue.json",state);
 check(state["jobs"][key].value("attempts",0)==2&&!itemStopped(state["jobs"][key]),"temporary shortcut read IO must not spend the last identity attempt");
 check(state["jobs"][key].value("automaticElapsedMs",int64_t{0})==1234&&state["jobs"][key].value("lastError",std::string{})=="media-storage-unavailable"&&state["jobs"][key].value("retryAt",int64_t{0})>now(),"shortcut read IO preserves identity debt and original storage backoff");
 check(state["jobs"]["steam-777"].value("state",std::string{})=="done","unrelated media completes during a shortcut read denial");
 check(!fs::exists(queue.root()/L"cache"/L"jobs"/L"game-media-identity"/L"helpers.jsonl"),"known shortcut read denial does not launch an identity helper");
 auto confirmed=target;confirmed["appId"]=106;confirmed["verified"]=true;Queue known(root/L"known");Store knownStore(known.root());
 const auto observation=root/L"known-observation.json";atomicWrite(observation,{{"exe",utf8(exe.wstring())},{"match",verified(106)}});
 check(knownStore.rememberIdentity(utf8(exe.wstring()),observation,now()).value("verified",false),"confirmed fixture has exact official cache authority before queue admission");seed(known,knownStore,confirmed);
 const auto knownToken=known.start();check(!knownToken.empty()&&Worker(known.root(),knownToken,GetCurrentProcess(),{},ports).run()==0,"confirmed shortcut read denial does not fail the worker");
 const auto deferred=known.document();check(deferred["jobs"].contains("steam-106"),"confirmed shortcut retains its original verified queue key");
 check(deferred["jobs"]["steam-106"].value("lastError",std::string{})=="media-storage-unavailable"&&deferred["jobs"]["steam-106"].value("retryAt",int64_t{0})>now()&&deferred["jobs"]["steam-777"].value("state",std::string{})=="done","confirmed shortcut IO is deferred per item without cancelling healthy work");
 CloseHandle(held.file);held.file=INVALID_HANDLE_VALUE;
 check(shortcutFixtureCurrent()(target),"the exact owned shortcut is current after read access recovers");
 queue.update(token,[&](Json& doc){doc["jobs"][key]["retryAt"]=0;});const auto retry=queue.start();check(!retry.empty()&&Worker(queue.root(),retry,GetCurrentProcess(),{},ports).run()==0,"same helper resumes after shortcut storage recovery");
 check(!queue.document()["jobs"].contains(key)&&store.identity(utf8(exe.wstring())).value("appId",0)==106,"recovered exact shortcut promotes only through the original official cache");
 shortcutFixtureAccount=18;check(!shortcutFixtureCurrent()(target),"another active account cannot borrow shortcut storage recovery");shortcutFixtureAccount=17;
 auto foreign=target;foreign["executable"]=utf8((root/L"other.exe").wstring());check(!shortcutFixtureCurrent()(foreign),"a mismatched executable is still refused");
 shortcuts(vdf,Json::array());check(!shortcutFixtureCurrent()(target),"a readable removed shortcut remains a real rejection");
 shortcuts(vdf,Json::array({record,record}));check(!shortcutFixtureCurrent()(target),"ambiguous shortcut authority remains refused");
 {std::ofstream out(vdf,std::ios::binary|std::ios::trunc);out<<"malformed";}check(!shortcutFixtureCurrent()(target),"malformed shortcut data does not gain storage retry authority");
 shortcuts(vdf,Json::array({record}));check(shortcutFixtureCurrent()(target),"only a restored valid exact shortcut can become current");
}
static int identityCacheIoProbe(const fs::path& root,const fs::path& self){
 if(fs::exists(root))throw std::runtime_error("fresh cache IO probe root required");fs::create_directories(root);
 ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;return s;});
 struct Reset{~Reset(){ymcc::powerSource::setExternalReader(nullptr);}}reset;
 shortcutReadIoProbe(root/L"s",self);
 const auto bin=root/L"bin",exe=root/L"fixture-106.exe";fs::create_directories(bin);fs::copy_file(self,bin/L"SteamArtworkLab.exe");std::ofstream(exe)<<"inert target, never executed";
 Queue q(root/L"data");Store store(q.root());const auto item=row(exe,6);q.enqueue(Json::array({item,{{"appId",777},{"verified",true}}}));
 const auto fileKey=std::string("file-")+digestKey(normalized(stringField(item,"executable")));
 auto doc=q.document();doc["jobs"][fileKey]["attempts"]=2;doc["jobs"][fileKey]["automaticElapsedMs"]=1234;doc["jobs"][fileKey]["lastError"]="media-identity-unconfirmed";doc["jobs"][fileKey]["lastPlayed"]=200;
 atomicWrite(q.root()/L"config"/L"game-media-downloads.json",doc);
 for(const int id:{106,777}){auto entry=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",entry["key"]},{"bindingRevision",entry["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
 const auto cache=custom_steam_library::identity_cache::cachePath(store.root(),utf8(exe.wstring()));fs::create_directories(custom_steam_library::ioPath(cache.parent_path()));
 const auto writeCache=[&](const std::string& bytes){std::ofstream out(custom_steam_library::ioPath(cache),std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());out.close();check(bool(out),"owned cache input is written completely");};
 const auto readCache=[&](){std::ifstream in(custom_steam_library::ioPath(cache),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 const std::string retained="{owned temporarily unreadable cache";writeCache(retained);
 struct Held{HANDLE file=INVALID_HANDLE_VALUE;~Held(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}}held;
 held.file=CreateFileW(custom_steam_library::ioPath(cache).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held.file!=INVALID_HANDLE_VALUE,"owned cache sharing denial is active");
 bool unavailable=false;const auto denied=store.identity(utf8(exe.wstring()),&unavailable);check(unavailable&&denied.empty(),"cache IO diagnostic keeps the original empty identity shape");
 IdentityPorts ports;ports.appRoot=bin;ports.current=[](const Json&){return true;};ports.timeoutMs=3000;
 const auto invocations=q.root()/L"cache"/L"jobs"/L"game-media-identity"/L"helpers.jsonl";
 const auto helperCalls=[&](){std::ifstream in(invocations);std::string line;unsigned count=0;while(std::getline(in,line)){check(Json::parse(line).is_object(),"helper witness is a real complete invocation row");++count;}return count;};
 const auto token=q.start();check(!token.empty(),"cache IO scenario claims the original queue");check(Worker(q.root(),token,GetCurrentProcess(),{},ports).run()==0,"one blocked identity cache does not cancel the worker");
 const auto blocked=q.document();atomicWrite(root/L"blocked-queue.json",blocked);
 check(blocked["jobs"][fileKey].value("attempts",0)==2&&!itemStopped(blocked["jobs"][fileKey]),"temporary cache IO must not spend the last identity attempt");
 check(blocked["jobs"][fileKey].value("lastError",std::string{})=="media-storage-unavailable"&&blocked["jobs"][fileKey].value("retryAt",int64_t{0})>now(),"cache IO uses the existing storage deferral and backoff");
 check(blocked["jobs"][fileKey].value("automaticElapsedMs",int64_t{0})==1234,"cache IO retains previously charged identification time");
 check(blocked["jobs"]["steam-777"].value("state",std::string{})=="done","unrelated runnable media completes despite the cache denial");
 check(!fs::exists(q.root()/L"cache"/L"jobs"/L"game-media-identity"/L"helpers.jsonl"),"known cache IO must defer before starting the identity helper");
 CloseHandle(held.file);held.file=INVALID_HANDLE_VALUE;
 check(readCache()==retained,"failed cache IO preserves exact unreadable original bytes");
 q.update(token,[&](Json& state){state["jobs"][fileKey]["retryAt"]=0;}); // accelerate the existing20s fixture backoff only
 const auto retry=q.start();check(!retry.empty(),"storage recovery can retry without changing helper or EXE bytes");check(Worker(q.root(),retry,GetCurrentProcess(),{},ports).run()==0,"same identity component persists and promotes after storage recovery");
 check(!q.document()["jobs"].contains(fileKey)&&store.identity(utf8(exe.wstring())).value("appId",0)==106,"only exact verified recovered identity retires the file task");
 check(helperCalls()==1,"storage recovery invokes the identity helper exactly once");
 bool preserved=false;for(const auto& entry:fs::directory_iterator(custom_steam_library::ioPath(cache.parent_path()))){std::ifstream in(entry.path(),std::ios::binary);if(std::string(std::istreambuf_iterator<char>(in),{})==retained)preserved=true;}
 check(preserved,"corrupt original remains retained after successful guarded cache repair");
 // Initial read can succeed and storage can become denied after helper exit.
 writeCache(retained);unsigned membershipCalls=0;IdentityPorts race=ports;
 race.current=[&](const Json&){if(++membershipCalls==3)held.file=CreateFileW(custom_steam_library::ioPath(cache).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);return true;};
 bool raceDeferred=false;try{identifyShortcut(q.root(),item,race,[]{});}catch(const std::exception& error){raceDeferred=std::string(error.what())=="media-storage-unavailable";}
 check(held.file!=INVALID_HANDLE_VALUE&&membershipCalls==3,"storage race starts only after actual helper exit and manifest membership check");
 check(raceDeferred,"post-helper storage IO still takes the original deferral");
 CloseHandle(held.file);held.file=INVALID_HANDLE_VALUE;
 check(readCache()==retained,"post-helper IO race preserves exact canonical bytes");
 check(helperCalls()==2,"post-helper race used one actual helper without retrying it");
 for(const bool future:{true,false}){
   Json guarded={{"schemaVersion",future?99:1},{"executable",future?utf8(exe.wstring()):utf8(exe.wstring())+".foreign"},{"match",verified(106)},{"updatedAt",now()}};writeCache(guarded.dump(2));
   unavailable=true;const auto unknown=store.identity(utf8(exe.wstring()),&unavailable);check(!unavailable&&unknown.empty(),"future or foreign authority clears the optional storage flag");
   bool unconfirmed=false;try{identifyShortcut(q.root(),item,ports,[]{});}catch(const std::exception& error){unconfirmed=std::string(error.what())=="media-identity-unconfirmed";}
   check(unconfirmed,"future or foreign cache authority is not misreported as transient storage");check(readCache()==guarded.dump(2),"storage recovery classification cannot overwrite future or foreign cache bytes");
 }
 Json result={{"ok",true},{"checks",checks},{"scope","Actual identifyShortcut/Worker/Queue and real owned helper; held canonical cache file only; no external HTTP or target/Steam/installed state control"}};atomicWrite(root/L"result.json",result);std::cout<<result.dump()<<std::endl;return 0;
}
static int recoveryWakeProbe(const fs::path& root){
 if(fs::exists(root))throw std::runtime_error("fresh resource-wake root required");fs::create_directories(root);
 mediaSleepFixture::bind();
 const auto helper=root/L"bin"/L"SteamArtworkLab.exe",exe=root/L"fixture-106.exe";Queue q(root/L"data");q.configure(true,"");const auto target=row(exe,6);q.enqueue(Json::array({target,{{"appId",777},{"verified",true}}}));auto d=q.document();const auto key=std::string("file-")+digestKey(normalized(stringField(target,"executable")));auto& old=d["jobs"][key];old["state"]="unavailable";old["autoStopped"]=true;old["attempts"]=3;old["lastError"]="media-identity-worker-unavailable";old["retryAt"]=0;atomicWrite(q.root()/L"config"/L"game-media-downloads.json",d);
 const auto steam=root/L"steam";
 shortcuts(steam/L"userdata"/L"17"/L"config"/L"shortcuts.vdf",Json::array({{{"appid",0x90000006u},{"appname","owned shortcut"},{"exe",utf8(exe.wstring())}}}));
 shortcutFixtureSteamRoot=steam.wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
 struct RestoreShortcut {~RestoreShortcut(){shortcutRegistryFixture=false;}} restoreShortcut;
 bool resumed=false,entered=false;recoveryWaitEntries=0;observeRecoveryWait=true;
 {Limits limits;limits.startup=0;limits.recheck=60000;ResourcePorts ports;ports.ac=[](){return 0;};Supervisor owner(q.root(),helper.parent_path()/L"CustomSteamLibrary.exe",limits,ports);owner.wake(true);
  auto deadline=GetTickCount64()+1500;while(GetTickCount64()<deadline&&recoveryWaitEntries.load()<2)Sleep(2);entered=recoveryWaitEntries.load()>=2;
  fs::create_directories(helper.parent_path());std::ofstream(helper)<<"fixture helper, never executed";std::ofstream(exe)<<"restored exact EXE, never executed";
  owner.wake(); // one existing notification, consumed by the inner resource wait
  deadline=GetTickCount64()+1200;while(GetTickCount64()<deadline&&q.document()["jobs"][key].value("attempts",-1)!=0)Sleep(2);
  resumed=q.document()["jobs"][key].value("attempts",-1)==0;
 }observeRecoveryWait=false;
 ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;return s;});
 struct RestorePowerReader{~RestorePowerReader(){ymcc::powerSource::setExternalReader(nullptr);}} restorePowerReader;
 Json prerequisites=Json::array();bool allStopped=true;
 selectedVideoNetworkAttempts=0;selectedVideoNoNetwork=true;
 struct RestoreNetwork{~RestoreNetwork(){selectedVideoNoNetwork=false;}} restoreNetwork;
 for(const bool missingHelper:{false,true}){
  const auto lane=root/(missingHelper?L"helper":L"target"),bin=lane/L"bin",game=lane/L"fixture-106.exe",tool=bin/L"SteamArtworkLab.exe";
  fs::create_directories(bin);
  if(missingHelper)std::ofstream(game)<<"owned exact target";
  else fs::copy_file(ymcc::steaminfo::transfer::module(),tool);
  Queue queue(lane/L"data");Store store(queue.root());const auto exact=row(game,6);
  queue.configure(true,"");queue.enqueue(Json::array({exact}));const auto key=queue.document()["jobs"].begin().key();
  for(const auto* slot:{"static","dynamic"}){
   const auto media=store.resolve({{"appId",106}});
   store.edit({{"appId",106},{"bindingRevision",media.value("bindingRevision",uint64_t{0})?media["bindingRevision"]:Json(1)},{"slot",slot},{"action","delete"}});
  }
  bool membership=true;IdentityPorts ports;ports.appRoot=bin;
  ports.current=[&](const Json& candidate){return membership&&stringField(candidate,"steamAccount")==stringField(exact,"steamAccount")&&stringField(candidate,"shortcutAppId")==stringField(exact,"shortcutAppId")&&stringField(candidate,"executable")==stringField(exact,"executable");};
  const auto run=[&]{const auto token=queue.start();check(!token.empty(),"owned prerequisite queue must admit its due task");return Worker(queue.root(),token,GetCurrentProcess(),{},ports,{},mediaSleepFixture::permit()).run();};
  const auto first=run();const auto settled=queue.document();const auto job=settled["jobs"][key],status=queue.snapshot();
  const auto* reason=missingHelper?"media-identity-helper-unavailable":"media-identity-target-unavailable";
  const auto calls=queue.root()/L"cache"/L"jobs"/L"game-media-identity"/L"helpers.jsonl";
  const bool firstStop=first==0&&job.value("attempts",0)==1&&job.value("autoStopped",false)&&stringField(job,"stopReason")==reason&&stringField(job,"lastError")==reason&&stringField(job,"identityPrerequisiteStamp")==reason&&job.value("retryAt",int64_t{-1})==0&&status.value("remaining",size_t{99})==0;
  const bool quiet=queue.recoverIdentityPrerequisites(tool,ports.current)==0&&queue.document()==settled&&!fs::exists(calls)&&settled.value("workerPid",DWORD{1})==0;
  if(missingHelper)fs::copy_file(ymcc::steaminfo::transfer::module(),tool);
  else std::ofstream(game)<<"restored exact target";
  membership=false;const bool foreign=queue.recoverIdentityPrerequisites(tool,ports.current)==0&&queue.document()==settled;
  membership=true;const auto reopened=queue.recoverIdentityPrerequisites(tool,ports.current);
  const bool once=reopened==1&&queue.recoverIdentityPrerequisites(tool,ports.current)==0&&queue.document()["jobs"][key].value("attempts",-1)==0;
  const bool completed=once&&run()==0&&queue.document()["jobs"].empty()&&store.identity(utf8(game.wstring())).value("appId",0)==106&&fs::is_regular_file(calls);
  allStopped=allStopped&&firstStop&&quiet&&foreign&&once&&completed;
  prerequisites.push_back({{"reason",reason},{"firstStop",firstStop},{"quietWhileMissing",quiet},{"foreignAccountRefused",foreign},{"restoredOnce",once},{"completedThroughWorker",completed},{"firstAttempts",job.value("attempts",0)},{"firstState",stringField(job,"state")},{"firstRetryAt",job.value("retryAt",int64_t{-1})}});
 }
 const auto isolated=root/L"network",game=isolated/L"network-temporary.exe";
 fs::create_directories(isolated);std::ofstream(game)<<"owned network lookup target";
 fs::copy_file(ymcc::steaminfo::transfer::module(),isolated/L"SteamArtworkLab.exe");
 Queue network(isolated/L"data");network.configure(true,"");network.enqueue(Json::array({row(game,6)}));
 auto debt=network.document();const auto id=debt["jobs"].begin().key();debt["jobs"][id]["attempts"]=2;debt["jobs"][id]["automaticElapsedMs"]=1234;
 atomicWrite(network.root()/L"config"/L"game-media-downloads.json",debt);
 IdentityPorts networkPorts;networkPorts.appRoot=isolated;networkPorts.current=[](const Json&){return true;};
 const auto token=network.start();const auto code=Worker(network.root(),token,GetCurrentProcess(),{},networkPorts,{},mediaSleepFixture::permit()).run();const auto next=network.document()["jobs"][id];
 const bool retainedRetry=code==0&&!token.empty()&&stringField(next,"lastError")=="media-identity-network-unavailable"&&next.value("attempts",0)==2&&next.value("automaticElapsedMs",int64_t{0})==1234&&next.value("retryAt",int64_t{0})>now()&&!itemStopped(next);
 Json r={{"ok",entered&&resumed&&allStopped&&retainedRetry&&selectedVideoNetworkAttempts.load()==0},{"checks",15},{"enteredOriginalResourceWait",entered},{"repairedTaskRecheckedAfterOneWake",resumed},{"workerPid",q.document().value("workerPid",DWORD{0})},{"networkAttempts",selectedVideoNetworkAttempts.load()},{"networkRetryRetained",retainedRetry},{"prerequisites",prerequisites},{"scope","original Supervisor resource wake and real Worker prerequisite settlement and exact-file restoration in owned roots"}};atomicWrite(root/L"result.json",r);std::cout<<r.dump()<<std::endl;return r.value("ok",false)?0:2;
}
struct OwnedRetryEnvironment {
    std::wstring previous;const wchar_t* variable;bool present=false;
    explicit OwnedRetryEnvironment(const wchar_t* key=L"YEMAN_MEDIA_IDENTITY_RETRY_OWNER"):variable(key){SetLastError(ERROR_SUCCESS);const auto n=GetEnvironmentVariableW(variable,nullptr,0);present=n||GetLastError()!=ERROR_ENVVAR_NOT_FOUND;if(n){previous.resize(n);const auto count=GetEnvironmentVariableW(variable,previous.data(),n);previous.resize(count);}}
    ~OwnedRetryEnvironment(){SetEnvironmentVariableW(variable,present?previous.c_str():nullptr);}
};
static std::atomic<int> identityPowerAc{1};
static std::atomic<uint64_t> identityPowerRevision{1};
static ymcc::powerSource::Snapshot identityPowerReceipt() noexcept {
 ymcc::powerSource::Snapshot s;s.readable=true;s.ac=identityPowerAc.load();s.known=s.ac>=0;s.status.ACLineStatus=s.known?BYTE(s.ac):255;s.status.SystemStatusFlag=0;s.revision=identityPowerRevision.load();return s;
}
static void identityPowerInterruption(const fs::path& base,const fs::path& bin){
 const auto reader=ymcc::powerSource::externalReader().load();const bool preference=powerPolicy::enabled();
 struct Restore{ymcc::powerSource::ExternalReader reader;bool preference;~Restore(){ymcc::powerSource::setExternalReader(reader);powerPolicy::configure(preference);}}restore{reader,preference};
 ymcc::powerSource::setExternalReader(identityPowerReceipt);
 for(const int interruptedAc:{0,-1}){
  identityPowerAc=1;++identityPowerRevision;powerPolicy::configure(true);
  const auto root=base/(interruptedAc==0?L"missed-dc":L"missed-unknown");fs::create_directories(root);
  const auto exe=root/L"fixture-106.exe";std::ofstream(exe)<<"inert exact identity target";
  Queue queue(root/L"data");Store store(queue.root());const auto target=row(exe,1);queue.enqueue(Json::array({target,{{"appId",777},{"verified",true}}}));
  const auto key=queue.document()["jobs"].begin().key();auto seeded=queue.document();seeded["wallpaperPauseOnBattery"]=true;seeded["jobs"][key]["attempts"]=2;seeded["jobs"][key]["automaticElapsedMs"]=1234;seeded["jobs"][key]["lastPlayed"]=200;
  seeded["statusRevision"]=seeded.value("statusRevision",uint64_t{0})+1;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",seeded);
  for(int id:{106,777}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
  const auto helperStamp=identityFileStamp(bin/L"SteamArtworkLab.exe"),exeStamp=identityFileStamp(exe);
  const auto token=queue.start();check(!token.empty(),"identity power fixture starts with a current AC receipt");
  auto context=std::make_shared<WorkerContext>(queue.root());IdentityPorts ports;ports.appRoot=bin;ports.timeoutMs=3000;unsigned membershipCalls=0;bool observed=false;Json interruption;
  ports.current=[&](const Json&){
   if(++membershipCalls==2){
    identityPowerAc=interruptedAc;++identityPowerRevision;const auto interrupted=powerPolicy::ticket();
    identityPowerAc=1;++identityPowerRevision;const auto restored=powerPolicy::ticket();observed=true;
    interruption={{"interruptedAc",interrupted.ac},{"restoredAc",restored.ac},{"oldTicket",context->powerTicket.revision},{"restoredTicket",restored.revision}};
    {std::lock_guard lock(context->gate);context->checkedAt=0;}
    // The existing progress heartbeat is throttled to1s. Let its next real
    // boundary run normally, after the actual owned helper has exited.
    Sleep(1100);
   }return true;
  };
  const auto code=Worker(queue.root(),token,GetCurrentProcess(),context,ports).run();const auto paused=queue.document();const auto item=paused["jobs"][key];
  atomicWrite(root/L"interrupted-queue.json",paused);atomicWrite(root/L"interruption.json",{{"observed",observed},{"membershipCalls",membershipCalls},{"receipt",interruption},{"code",code},{"identity",store.identity(utf8(exe.wstring()))}});
  check(observed&&interruption.value("interruptedAc",9)==interruptedAc&&interruption.value("restoredAc",0)==1&&interruption["restoredTicket"]!=interruption["oldTicket"],"real identity callback observes DC or unknown followed by AC under the original retained worker ticket");
  check(item.value("attempts",-1)==2&&!itemStopped(item)&&item.value("automaticElapsedMs",int64_t{-1})==1234,"power interruption must not consume the final identity attempt or its time budget");
  check(code==0&&stringField(paused,"phase")=="paused"&&stringField(paused,"reason")=="download-paused","identity power interruption follows the original worker pause result");
  check(paused.value("workerPid",DWORD{1})==0&&paused["active"].empty()&&stringField(paused,"wallpaperPowerCancelledSession")==token&&!queue.allowed(token),"interrupted identity owner returns all active work and retires its old session");
  check(store.identity(utf8(exe.wstring())).empty(),"old identity output is not published across the power interruption");
  check(paused["jobs"]["steam-777"].value("attempts",-1)==0,"power interruption does not charge unrelated task failure debt");
  const auto jobs=queue.root()/L"cache"/L"jobs"/L"game-media-identity";
  for(const auto& entry:fs::directory_iterator(jobs))check(!entry.is_directory(),"interrupted identity helper output is reclaimed");
  const auto next=queue.start();check(!next.empty()&&next!=token,"fresh AC admission creates a new bounded identity session");
  ports.current=[](const Json&){return true;};
  check(Worker(queue.root(),next,GetCurrentProcess(),{},ports).run()==0,"fresh current owner resumes the original pending identity");
  check(!queue.document()["jobs"].contains(key)&&store.identity(utf8(exe.wstring())).value("appId",0)==106,"only the fresh owner promotes the exact official identity");
  check(helperStamp==identityFileStamp(bin/L"SteamArtworkLab.exe")&&exeStamp==identityFileStamp(exe),"power recovery does not replace helper or target files");
  unsigned helpers=0;std::string line;std::ifstream witness(jobs/L"helpers.jsonl");while(std::getline(witness,line)){check(Json::parse(line).is_object(),"actual identity helper invocation is retained");++helpers;}
  check(helpers==2,"power interruption and recovery launch exactly one helper each");
  atomicWrite(root/L"recovered-queue.json",queue.document());
 }
 // A genuine identity refusal still spends the final original attempt. The
 // power fix must not turn ordinary failure into an unlimited retry exemption.
 identityPowerAc=1;++identityPowerRevision;powerPolicy::configure(false);
 const auto root=base/L"genuine-refusal",exe=root/L"network-unrecognized.exe";fs::create_directories(root);std::ofstream(exe)<<"inert genuine refusal";
 Queue queue(root/L"data");queue.enqueue(Json::array({row(exe,2)}));auto d=queue.document();const auto key=d["jobs"].begin().key();d["jobs"][key]["attempts"]=2;d["jobs"][key]["automaticElapsedMs"]=1234;d["statusRevision"]=d.value("statusRevision",uint64_t{0})+1;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",d);
 IdentityPorts ports;ports.appRoot=bin;ports.current=[](const Json&){return true;};ports.timeoutMs=3000;const auto token=queue.start();check(!token.empty(),"ordinary refusal still uses the original queue");
 check(Worker(queue.root(),token,GetCurrentProcess(),{},ports).run()==0,"ordinary identity refusal settles normally");const auto refused=queue.document();atomicWrite(root/L"refused-queue.json",refused);
 check(refused["jobs"][key].value("attempts",0)==3&&itemStopped(refused["jobs"][key])&&stringField(refused["jobs"][key],"lastError")=="media-identity-unconfirmed","genuine identity refusal retains its original bounded stop policy");
}
static void identityLongOutputRead(const fs::path& base,const fs::path& bin){
 Json observed=Json::array();
 for(const auto* mode:{"valid","foreign","malformed"}){
  const auto root=base/wide(mode);fs::create_directories(root);const auto exe=root/L"fixture-108.exe";
  std::ofstream(exe)<<"owned inert exact target";std::ofstream(exe.wstring()+L".deep-output")<<mode;
  Queue queue(root/L"data");Store store(queue.root());const auto target=row(exe,3);queue.enqueue(Json::array({target}));auto seeded=queue.document();const auto key=seeded["jobs"].begin().key();
  seeded["jobs"][key]["attempts"]=2;seeded["jobs"][key]["automaticElapsedMs"]=1234;seeded["statusRevision"]=seeded.value("statusRevision",uint64_t{0})+1;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",seeded);
  auto e=store.edit({{"appId",108},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});
  Json witness=Json::object();IdentityPorts ports;ports.appRoot=bin;ports.timeoutMs=3000;unsigned calls=0;
  ports.current=[&](const Json&){
   if(++calls==2){const auto jobs=queue.root()/L"cache"/L"jobs"/L"game-media-identity";
    for(const auto& row:fs::directory_iterator(custom_steam_library::ioPath(jobs,true))){
     if(!row.is_directory())continue;const auto output=jobs/row.path().filename();const auto manifest=output/std::wstring(80,L'g')/L"manifest.json";
     const auto snapshot=custom_steam_library::identity_cache::readSnapshot(manifest);
     witness={{"outputCharacters",output.wstring().size()},{"manifestCharacters",manifest.wstring().size()},{"directoryCharacters",manifest.parent_path().wstring().size()},
      {"exists",snapshot.exists},{"parsed",snapshot.parsed},{"value",snapshot.value}};
    }
   }return true;
  };
  const auto token=queue.start();check(!token.empty(),"long manifest fixture uses the original current queue session");const auto code=Worker(queue.root(),token,GetCurrentProcess(),{},ports).run();const auto final=queue.document();const auto identity=store.identity(utf8(exe.wstring()));
  const auto cache=custom_steam_library::identity_cache::cachePath(queue.root(),utf8(exe.wstring()));const auto cached=custom_steam_library::identity_cache::readSnapshot(cache);
  observed.push_back({{"mode",mode},{"code",code},{"witness",witness},{"queue",final},{"identity",identity},{"cache",cached.value}});atomicWrite(base/L"long-output-observations.json",observed);
  check(witness.value("exists",false)&&witness.value("outputCharacters",size_t{999})<248&&witness.value("directoryCharacters",size_t{0})>260,"real exited helper produced a long descendant beneath a short output root");
  if(std::string_view(mode)=="valid"){
   check(witness.value("parsed",false)&&witness["value"]["match"].value("appId",0)==108&&witness["value"].value("exe",std::string{})==utf8(exe.wstring()),"owned long-output observation contains the exact valid identity before consumption");
   check(code==0&&!final["jobs"].contains(key)&&identity.value("verified",false)&&identity.value("appId",0)==108,"valid long helper manifest must promote exact identity without consuming final attempt");
   const auto path=stringField(cached.value,"manifestPath");check(!path.empty()&&path.find("\\\\?\\")==std::string::npos&&path.find(std::string(80,'g'))!=std::string::npos,"extended enumeration never persists a Win32 device prefix as manifest identity");
  }else{
   const auto job=final["jobs"][key];check(code==0&&identity.empty()&&job.value("attempts",0)==3&&itemStopped(job)&&stringField(job,"lastError")=="media-identity-unconfirmed","foreign or malformed long manifest retains exact-file identity refusal and bounded stop");
  }
  check(final.value("workerPid",DWORD{1})==0&&final["active"].empty(),"long-output consumer returns its worker ownership");
  const auto jobs=queue.root()/L"cache"/L"jobs"/L"game-media-identity";for(const auto& row:fs::directory_iterator(jobs))check(!row.is_directory(),"long helper output is reclaimed on acceptance and rejection");
 }
}
static void identityOutputDirectoryIo(const fs::path& base,const fs::path& bin){
 Json observations=Json::array();
 for(const auto* mode:{"root","nested"}){
  const auto root=base/wide(mode);fs::create_directories(root);const auto exe=root/L"fixture-109.exe";std::ofstream(exe)<<"owned inert directory IO target";
  Queue queue(root/L"data");Store store(queue.root());const auto target=row(exe,4);queue.enqueue(Json::array({target,{{"appId",777},{"verified",true}}}));auto seeded=queue.document();const auto key=seeded["jobs"].begin().key();
  seeded["jobs"][key]["attempts"]=2;seeded["jobs"][key]["automaticElapsedMs"]=1234;seeded["jobs"][key]["lastPlayed"]=200;seeded["statusRevision"]=seeded.value("statusRevision",uint64_t{0})+1;atomicWrite(queue.root()/L"config"/L"game-media-downloads.json",seeded);
  for(int id:{109,777}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
  const auto jobs=queue.root()/L"cache"/L"jobs"/L"game-media-identity";const auto helperStamp=identityFileStamp(bin/L"SteamArtworkLab.exe"),exeStamp=identityFileStamp(exe);
  struct Held{HANDLE handle=INVALID_HANDLE_VALUE;fs::path output;~Held(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}}held;
  Json witness=Json::object();IdentityPorts ports;ports.appRoot=bin;ports.timeoutMs=3000;unsigned calls=0;
  ports.current=[&](const Json&){
   if(++calls==2)for(const auto& entry:fs::directory_iterator(custom_steam_library::ioPath(jobs,true))){
    if(!entry.is_directory())continue;held.output=jobs/entry.path().filename();const auto blocked=std::string_view(mode)=="root"?held.output:held.output/L"artwork"/L"fixture";
    const auto manifest=custom_steam_library::identity_cache::readSnapshot(held.output/L"artwork"/L"fixture"/L"manifest.json");
    held.handle=CreateFileW(custom_steam_library::ioPath(blocked).c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    std::error_code ec;if(held.handle!=INVALID_HANDLE_VALUE)for(fs::recursive_directory_iterator it(custom_steam_library::ioPath(held.output,true),fs::directory_options::none,ec),end;!ec&&it!=end;it.increment(ec)){}
    witness={{"lockAcquired",held.handle!=INVALID_HANDLE_VALUE},{"directoryReadError",ec.value()},{"errorCategory",ec.category().name()},{"manifestParsed",manifest.parsed},{"manifestAppId",manifest.value.value("match",Json::object()).value("appId",0)}};
   }return true;
  };
  const auto token=queue.start();check(!token.empty(),"directory IO fixture starts the exact pending identity session");const auto began=now();const auto code=Worker(queue.root(),token,GetCurrentProcess(),{},ports).run();const auto deferred=queue.document();const auto item=deferred["jobs"][key];
  observations.push_back({{"mode",mode},{"code",code},{"witness",witness},{"queue",deferred}});atomicWrite(base/L"directory-io-observations.json",observations);
  if(held.handle!=INVALID_HANDLE_VALUE){CloseHandle(held.handle);held.handle=INVALID_HANDLE_VALUE;}
  // External sharing ownership can block product cleanup. After releasing our
  // test handle, remove only this exact UUID output within this owned jobs root.
  const auto outputName=utf8(held.output.filename().wstring());check(held.output.is_absolute()&&held.output.parent_path()==jobs&&outputName.size()==32&&safeKey(outputName),"fixture cleanup is confined to its exact owned temporary output");
  std::error_code cleanup;fs::remove_all(custom_steam_library::ioPath(held.output,true),cleanup);check(!cleanup,"owned fixture directory is reclaimed after releasing external read denial");
  check(witness.value("lockAcquired",false)&&witness.value("directoryReadError",0)==ERROR_SHARING_VIOLATION&&witness.value("manifestParsed",false)&&witness.value("manifestAppId",0)==109,"actual exited helper has a valid result and an observed directory sharing denial");
  check(item.value("attempts",-1)==2&&!itemStopped(item)&&item.value("automaticElapsedMs",int64_t{-1})==1234,"temporary output directory IO must not consume the final identity attempt or charged time");
  check(code==0&&stringField(item,"lastError")=="media-storage-unavailable"&&item.value("retryAt",int64_t{0})>=began+retryMs,"directory read denial uses the original bounded storage backoff");
  check(deferred.value("workerPid",DWORD{1})==0&&deferred["active"].empty()&&store.identity(utf8(exe.wstring())).empty(),"unreadable output grants no identity and returns all worker ownership");
  check(stringField(deferred["jobs"]["steam-777"],"state")=="done","directory IO deferral does not block unrelated healthy media");
  queue.update(token,[&](Json& d){d["jobs"][key]["retryAt"]=0;}); // accelerate only the original20s fixture backoff
  const auto retry=queue.start();check(!retry.empty(),"released directory can retry with the original remaining budget");ports.current=[](const Json&){return true;};
  check(Worker(queue.root(),retry,GetCurrentProcess(),{},ports).run()==0,"same identity component recovers after directory IO release");
  check(!queue.document()["jobs"].contains(key)&&store.identity(utf8(exe.wstring())).value("appId",0)==109,"only recovered current exact identity retires the pending file");
  check(helperStamp==identityFileStamp(bin/L"SteamArtworkLab.exe")&&exeStamp==identityFileStamp(exe),"directory IO recovery does not replace helper or target files");
  unsigned helpers=0;std::string line;std::ifstream callsFile(jobs/L"helpers.jsonl");while(std::getline(callsFile,line)){check(Json::parse(line).is_object(),"directory recovery helper invocation is a complete actual witness");++helpers;}
  check(helpers==2,"directory IO and recovery each invoke the helper once");for(const auto& entry:fs::directory_iterator(jobs))check(!entry.is_directory(),"successful recovery output is cleaned by the original product owner");
  atomicWrite(root/L"recovered-queue.json",queue.document());
 }
}
static int identityNetworkProbe(const fs::path& root,const fs::path& self,const std::string& selectedCase={},bool zeroExit=false){
 if(fs::exists(root))throw std::runtime_error("fresh identity network probe root required");fs::create_directories(root);
 ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;return s;});
 struct Reset{~Reset(){ymcc::powerSource::setExternalReader(nullptr);}}reset;
 const auto bin=root/L"bin";fs::create_directories(bin);fs::copy_file(self,bin/L"SteamArtworkLab.exe");
 IdentityPorts ports;ports.appRoot=bin;ports.current=[](const Json&){return true;};ports.timeoutMs=3000;
 Json rows=Json::array();const auto run=[&](const char* name,auto body){if(!selectedCase.empty()&&selectedCase!=name)return;const auto start=checks;try{body();rows.push_back({{"name",name},{"ok",true},{"checks",checks-start}});}catch(const std::exception& e){rows.push_back({{"name",name},{"ok",false},{"error",e.what()},{"checks",checks-start}});}};
 run("network-defers-without-spending-final-attempt-and-recovers",[&]{
  identityOutputDirectoryIo(root/L"output-directory-io",bin);
  identityLongOutputRead(root/L"long-output-read",bin);
  identityPowerInterruption(root/L"identity-power-interruption",bin);
  OwnedRetryEnvironment restoreRetryEnvironment;SetEnvironmentVariableW(L"yeman_media_identity_retry_owner",L"fixture-parent-owner");
  OwnedRetryEnvironment restoreDataEnvironment(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT");
  const auto inheritedRoot=root/L"继承 旧目录";fs::create_directories(inheritedRoot);std::ofstream(inheritedRoot/L"keep.txt")<<"original unrelated data root";
  SetEnvironmentVariableW(L"yeman_steam_big_picture_data_root",inheritedRoot.c_str());
  const auto exe=root/L"network-temporary.exe";std::ofstream(exe)<<"inert target";
  Queue q(root/L"库 A");Store store(q.root());const auto target=row(exe,1);q.enqueue(Json::array({target,{{"appId",777},{"verified",true}}}));
  const auto key=std::string("file-")+digestKey(normalized(stringField(target,"executable")));
  auto doc=q.document();doc["jobs"][key]["attempts"]=2;doc["jobs"][key]["automaticElapsedMs"]=1234;doc["jobs"][key]["lastPlayed"]=200;atomicWrite(q.root()/L"config"/L"game-media-downloads.json",doc);
  for(int id:{106,777}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
  const auto toolStamp=identityFileStamp(bin/L"SteamArtworkLab.exe"),exeStamp=identityFileStamp(exe);
  // Deny only this real child's failure receipt after its process exits.
  // The original worker must defer storage IO without consuming identity debt.
  {
   struct HeldReceipt{HANDLE handle=INVALID_HANDLE_VALUE;fs::path file;~HeldReceipt(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}}held;
   auto deniedPorts=ports;
   deniedPorts.current=[&](const Json&){
    const auto jobs=q.root()/L"cache"/L"jobs"/L"game-media-identity";
    if(held.handle==INVALID_HANDLE_VALUE&&fs::is_directory(jobs))for(const auto& entry:fs::directory_iterator(jobs)){
     const auto receipt=entry.path()/L"rounds-failed.json";if(!entry.is_directory()||!fs::is_regular_file(receipt))continue;
     held.file=receipt;held.handle=CreateFileW(custom_steam_library::ioPath(receipt).c_str(),GENERIC_READ,FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);break;
    }return true;
   };
   const auto token=q.start();const auto started=now();check(!token.empty(),"receipt IO starts the original worker");
   check(Worker(q.root(),token,GetCurrentProcess(),{},deniedPorts).run()==0,"unreadable failed receipt settles through original worker");
   const auto state=q.document();atomicWrite(root/L"failed-receipt-queue.json",state);const auto job=state["jobs"][key];
   check(held.handle!=INVALID_HANDLE_VALUE,"actual helper failure receipt sharing denial was established");
   CloseHandle(held.handle);held.handle=INVALID_HANDLE_VALUE;
   // A foreign sharing owner may keep an already-deleted temporary directory
   // pending until it releases the handle. Reclaim only this owned test output.
   if(fs::exists(held.file.parent_path()))fs::remove_all(held.file.parent_path());
   check(job.value("attempts",-1)==2&&!itemStopped(job),"temporarily unreadable failed identity receipt must not spend final attempt");
   check(stringField(job,"lastError")=="media-storage-unavailable"&&job.value("retryAt",int64_t{0})>=started+retryMs,"failed receipt IO uses existing storage backoff");
   check(job.value("automaticElapsedMs",int64_t{-1})==1234,"failed receipt IO preserves charged identity time");
   check(job.value("lastIdentityObservation",Json::object()).value("helperExitCode",0)==1&&stringField(job.value("lastIdentityObservation",Json::object()),"receiptStatus")=="storage-unavailable","failed identity receipt retains same-read storage diagnosis");
   check(state["jobs"]["steam-777"].value("state",std::string{})=="done","receipt IO does not block unrelated media");
   check(state.value("workerPid",DWORD{1})==0&&state["active"].empty(),"failed receipt IO leaves no active worker or helper");
   check(store.identity(utf8(exe.wstring())).empty(),"unreadable failure receipt grants no identity");
   q.update(token,[&](Json& value){value["jobs"][key]["retryAt"]=0;});
  }
  for(int n=0;n<4;++n){
   const auto token=q.start();check(!token.empty(),"network retry starts original worker");const auto started=now();
   check(Worker(q.root(),token,GetCurrentProcess(),{},ports).run()==0,"network lookup does not kill unrelated work");
   const auto state=q.document();atomicWrite(root/(L"network-queue-"+std::to_wstring(n)+L".json"),state);const auto& job=state["jobs"][key];
   check(job.value("attempts",-1)==2&&!itemStopped(job),"transient network must not consume the final identity attempt");
   check(job.value("automaticElapsedMs",int64_t{-1})==1234,"transient network retains previous charged time");
   check(stringField(job,"lastError")=="media-identity-network-unavailable"&&job.value("retryAt",int64_t{0})>=started+retryMs,"exact network receipt has existing bounded retry delay");
   check(state["jobs"]["steam-777"].value("state",std::string{})=="done","healthy unrelated task completes");
   check(state.value("workerPid",DWORD{1})==0&&state["active"].empty(),"deferred worker exits without background activity");
   check(store.identity(utf8(exe.wstring())).empty(),"a network failure never supplies a Steam identity");
   const auto observed=job.value("lastIdentityObservation",Json::object());
   check(observed.value("helperExitCode",0)==1&&stringField(observed,"receiptStatus")=="matching-target"&&stringField(observed,"failureKind")=="network-unavailable"&&observed.value("retryableFailures",0)==1,"network identity failure retains exact helper outcome");
   check(observed.value("observedAt",int64_t{0})>0&&observed.value("provenanceRevision",uint64_t{0})==job.value("provenanceRevision",uint64_t{0})&&observed.dump().find("fixture-secret")==std::string::npos,"identity summary is dated scoped and excludes provider payload");
   check(!observed.contains("successfulResponses")&&!observed.contains("nonRetryableResponses"),"invalid unrelated counters cannot alter network deferral or enter diagnostics");
   q.update(token,[&](Json& value){value["jobs"][key]["retryAt"]=0;}); // only accelerate original20s fixture backoff
  }
  std::ofstream(exe.wstring()+L".online")<<"owned network recovery signal";
  const auto token=q.start();check(!token.empty(),"same files can retry after network recovery");
  check(Worker(q.root(),token,GetCurrentProcess(),{},ports).run()==0,"same worker resumes after valid official observation");
  check(!q.document()["jobs"].contains(key)&&store.identity(utf8(exe.wstring())).value("appId",0)==106,"fresh verified identity promotes and retires the pending file");
  check(toolStamp==identityFileStamp(bin/L"SteamArtworkLab.exe")&&exeStamp==identityFileStamp(exe),"recovery requires no helper or game file replacement");
  const auto jobs=q.root()/L"cache"/L"jobs"/L"game-media-identity";for(const auto& entry:fs::directory_iterator(jobs))check(!entry.is_directory(),"owned temporary helper output is reclaimed");
  std::ifstream helpers(jobs/L"helpers.jsonl");std::string line;unsigned observations=0;
  while(std::getline(helpers,line)){const auto observed=Json::parse(line);check(observed.value("retryOwner",std::string{})=="shared-media-v1","real helper receives a child-scoped retry owner");check(normalized(observed.value("configuredRoot",std::string{}))==normalized(utf8(q.root().wstring())),"identity helper must receive the active task data root");++observations;}
  check(observations==6,"every actual identity helper launch is observed exactly once");
  wchar_t parentOwner[64]{};GetEnvironmentVariableW(L"YEMAN_MEDIA_IDENTITY_RETRY_OWNER",parentOwner,64);
  check(std::wstring_view(parentOwner)==L"fixture-parent-owner","identity helper launch never mutates parent environment");
  check(custom_steam_library::configuredDataRoot()==inheritedRoot,"identity helper data root override never mutates parent environment");
  std::ifstream retained(inheritedRoot/L"keep.txt");check(std::string(std::istreambuf_iterator<char>(retained),{})=="original unrelated data root","unrelated inherited root retains its original data");
 });
 run("legacy-unconfirmed-policy-recovery-is-once-only",[&]{
  const auto exe=root/L"fixture-107.exe";std::ofstream(exe)<<"inert legacy target";Queue q(root/L"legacy");const auto target=row(exe,3);q.enqueue(Json::array({target}));const auto key=q.document()["jobs"].begin().key();
  auto doc=q.document();auto& job=doc["jobs"][key];job["attempts"]=3;job["state"]="unavailable";job["autoStopped"]=true;job["lastError"]="media-identity-unconfirmed";job["identityPrerequisiteStamp"]=digestKey(identityFileStamp(bin/L"SteamArtworkLab.exe")+"\n"+identityFileStamp(exe));
  atomicWrite(q.root()/L"config"/L"game-media-downloads.json",doc);
  check(q.recoverIdentityPrerequisites(bin/L"SteamArtworkLab.exe",ports.current)==1,"repaired network receipt policy revives legacy exhausted identity once");
  check(q.document()["jobs"][key].value("attempts",-1)==0&&!itemStopped(q.document()["jobs"][key]),"legacy recovery uses original bounded fresh budget");
  check(q.recoverIdentityPrerequisites(bin/L"SteamArtworkLab.exe",ports.current)==0,"unchanged repaired policy cannot repeatedly refund attempts");
  const auto token=q.start();check(!token.empty(),"legacy recovered task starts original queue");
  for(int n=0;n<3;++n){const auto claimed=q.claim(token);check(!claimed.is_null(),"fresh legacy budget remains bounded");q.result(token,key,false,"media-identity-unconfirmed",0,identityPrerequisites(bin/L"SteamArtworkLab.exe",utf8(exe.wstring())).stamp);q.update(token,[&](Json& state){state["jobs"][key]["retryAt"]=0;});}
  q.end(token,"deferred");check(itemStopped(q.document()["jobs"][key])&&q.recoverIdentityPrerequisites(bin/L"SteamArtworkLab.exe",ports.current)==0,"real unconfirmed identity still stops after the repaired bounded attempt");
  check(Queue(q.root()).recoverIdentityPrerequisites(bin/L"SteamArtworkLab.exe",ports.current)==0,"restart cannot reset the consumed policy recovery");
 });
 for(const auto* stem:{"network-foreign","network-malformed","network-no-receipt","network-unrecognized","network-unrecognized-legacy","network-unrecognized-unknown-stage","network-unrecognized-invalid-stage","network-no-evidence","network-string-count","network-flag-false"})run(stem,[&]{
  const auto exe=root/(wide(stem)+L".exe");std::ofstream(exe)<<"inert target";Queue q(root/wide(stem));const auto target=row(exe,2);q.enqueue(Json::array({target}));const auto key=q.document()["jobs"].begin().key();
  auto doc=q.document();doc["jobs"][key]["attempts"]=2;atomicWrite(q.root()/L"config"/L"game-media-downloads.json",doc);
  mediaSleepFixture::bind();
  const auto token=q.start();check(!token.empty(),"negative network input starts original worker");check(Worker(q.root(),token,GetCurrentProcess(),{},ports,{},mediaSleepFixture::permit()).run()==0,"negative receipt settles through original worker");
  const auto job=q.document()["jobs"][key];check(stringField(job,"lastError")=="media-identity-unconfirmed"&&itemStopped(job)&&job.value("attempts",0)==3,"foreign malformed or nonretryable receipt retains original bounded identity refusal");
  check(Store(q.root()).identity(utf8(exe.wstring())).empty(),"rejected receipt cannot publish identity");
  const auto observed=job.value("lastIdentityObservation",Json::object());const std::string name=stem;
  const auto expected=name=="network-foreign"?"target-mismatch":name=="network-malformed"?"invalid-json":name=="network-no-receipt"?"missing":name=="network-string-count"?"invalid-fields":"matching-target";
  check(observed.value("helperExitCode",0)==1&&stringField(observed,"receiptStatus")==expected,"unconfirmed identity retains precise helper failure diagnosis");
  check(observed.dump().find("fixture-secret")==std::string::npos&&(!observed.contains("retryableFailures")||observed["retryableFailures"].is_number_integer()),"invalid or foreign failure data is not copied into diagnostics");
  if(name=="network-unrecognized")check(stringField(observed,"failureKind")=="unrecognized","real recognition rejection stays distinct from network failure");
  if(name=="network-foreign")check(!observed.contains("failureKind")&&!observed.contains("retryableFailures"),"foreign receipt contributes no failure facts");
  if(name=="network-unrecognized")check(stringField(observed,"steamLookupStage")=="candidate-not-verified","same-target diagnosis survives queue commit");
  else check(!observed.contains("steamLookupStage"),"legacy, foreign, invalid or non-recognition receipt must not supply a diagnostic stage");
  const auto temporary=q.root()/L"cache"/L"jobs"/L"game-media-identity";
  for(const auto& entry:fs::directory_iterator(temporary))check(!entry.is_directory(),"failure summary survives original temporary output cleanup");
  if(zeroExit){
   for(const auto* mode:{"empty","malformed","foreign","unconfirmed","verified"}){
    const std::string variant=mode;const auto lane=root/(L"zero-exit-"+wide(variant));fs::create_directories(lane);const auto game=lane/(variant=="verified"?L"fixture-106.exe":L"zero-exit-"+wide(variant)+L".exe");std::ofstream(game)<<"owned inert exact game";
    Queue work(lane/L"data");Store store(work.root());const auto item=row(game,3);work.enqueue(Json::array({item,{{"appId",777},{"verified",true}}}));const auto fileKey=std::string("file-")+digestKey(normalized(utf8(game.wstring())));auto seeded=work.document();seeded["jobs"][fileKey]["attempts"]=2;seeded["jobs"][fileKey]["automaticElapsedMs"]=1234;seeded["jobs"][fileKey]["lastPlayed"]=200;atomicWrite(work.root()/L"config"/L"game-media-downloads.json",seeded);
    for(int id:{106,777}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
    const auto owner=work.start();check(!owner.empty(),"zero-exit observation uses original queue admission");const auto code=Worker(work.root(),owner,GetCurrentProcess(),{},ports).run();const auto result=work.document();const auto failed=result["jobs"].value(fileKey,Json::object());const auto detail=failed.value("lastIdentityObservation",Json::object());const auto cache=store.identity(utf8(game.wstring()));const auto jobs=work.root()/L"cache"/L"jobs"/L"game-media-identity";
    unsigned children=0;std::ifstream calls(jobs/L"helpers.jsonl");std::string line;while(std::getline(calls,line)){check(Json::parse(line).is_object(),"zero-exit helper receipt is readable");++children;}
    atomicWrite(lane/L"observed.json",{{"variant",variant},{"code",code},{"helperCalls",children},{"job",failed},{"observation",detail},{"identity",cache},{"queue",result}});
    check(code==0&&children==1&&result.value("workerPid",DWORD{1})==0&&result["active"].empty(),"zero-exit settles original owner after one actual helper");
    check(result["jobs"]["steam-777"].value("state",std::string{})=="done","zero-exit refusal does not block unrelated queued media");
    if(variant=="verified")check(!result["jobs"].contains(fileKey)&&cache.value("appId",0)==106&&cache.value("verified",false),"genuine zero-exit official result still promotes exact identity");
    else{
     check(stringField(failed,"lastError")=="media-identity-unconfirmed"&&failed.value("attempts",0)==3&&itemStopped(failed)&&failed.value("automaticElapsedMs",int64_t{0})>=1234,"zero-exit refusal retains original last-attempt and charged-time rules");
     check(cache.empty(),"zero-exit rejected output grants no identity");
     check(detail.contains("helperExitCode")&&detail.value("helperExitCode",DWORD{1})==0&&stringField(detail,"receiptStatus")==std::string(variant=="empty"?"manifest-missing":"manifest-unconfirmed"),"zero-exit refusal must retain actual successful exit and scan outcome");
     check(detail.value("manifestCandidates",uint64_t{99})==(variant=="empty"?0:1)&&detail.value("examinedEntries",uint64_t{99})==(variant=="empty"?0:1)&&detail.value("observedAt",int64_t{0})>0&&detail.value("provenanceRevision",uint64_t{0})==failed.value("provenanceRevision",uint64_t{0}),"zero-exit summary records only bounded original scan facts under current provenance");
     check(detail.dump().find("fixture-secret")==std::string::npos&&!detail.contains("appId")&&!detail.contains("executable")&&!detail.contains("manifest"),"zero-exit diagnostic excludes provider payload and candidate authority");
     const auto durable=work.root()/L"config"/L"game-media-downloads.json";std::ifstream beforeFile(durable,std::ios::binary);const std::string beforeBytes(std::istreambuf_iterator<char>(beforeFile),{});beforeFile.close();work.result(owner+"-stale",fileKey,false,"media-identity-unconfirmed",0,"",Json{},{{"helperExitCode",99},{"receiptStatus","manifest-missing"},{"manifestCandidates",64}});std::ifstream afterFile(durable,std::ios::binary);check(std::string(std::istreambuf_iterator<char>(afterFile),{})==beforeBytes,"stale result cannot replace retained zero-exit diagnosis");
    }
    for(const auto& entry:fs::directory_iterator(jobs))check(!entry.is_directory(),"zero-exit diagnosis survives product output cleanup");
   }
  }
 });
 const bool ok=std::all_of(rows.begin(),rows.end(),[](const auto& r){return r.value("ok",false);});Json result={{"ok",ok},{"checks",checks},{"selection","identity-network"},{"cases",rows},{"scope","original Worker/Queue/identifyShortcut with owned real child, no remote network or installed runtime"}};atomicWrite(root/L"result.json",result);std::cout<<result.dump()<<std::endl;return ok?0:2;
}
static int identityNetworkOwnerProbe(const fs::path& root,const fs::path& self){
 if(fs::exists(root))throw std::runtime_error("fresh network owner root required");fs::create_directories(root);
 const auto bin=self.parent_path()/L"CustomSteamLibrary",data=root/L"data",exe=root/L"network-temporary.exe";fs::create_directories(bin);fs::create_directories(data);
 fs::copy_file(self,bin/L"SteamArtworkLab.exe",fs::copy_options::overwrite_existing);fs::copy_file(self,bin/L"CustomSteamLibrary.exe",fs::copy_options::overwrite_existing);std::ofstream(exe)<<"owned inert game";std::ofstream(data/L"fixture-network-owner")<<"owned child entry only";
 atomicWrite(bin/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundVideoQualityPolicy",videoQualityPolicy}});
 const auto target=row(exe,4);shortcuts(root/L"steam"/L"userdata"/L"17"/L"config"/L"shortcuts.vdf",Json::array({{{"appid",0x90000004u},{"appname","fixture only"},{"exe",utf8(exe.wstring())}}}));
 const bool priorRegistryFixture=shortcutRegistryFixture.load();const auto priorSteamRoot=shortcutFixtureSteamRoot;
 struct ResetSourceInput{bool enabled;std::wstring root;~ResetSourceInput(){shortcutRegistryFixture=enabled;shortcutFixtureSteamRoot=root;}}resetSourceInput{priorRegistryFixture,priorSteamRoot};
 shortcutFixtureSteamRoot=(root/L"steam").wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
 Queue q(data);Store store(data);q.enqueue(Json::array({target}));for(int id:{106}){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
 const auto key=q.document()["jobs"].begin().key();const DWORD needed=GetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",nullptr,0);std::wstring old(needed,L'\0');if(needed){GetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",old.data(),needed);old.resize(needed-1);}
 struct Restore{std::wstring value;bool present;~Restore(){SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",present?value.c_str():nullptr);}}restore{old,needed!=0};
 check(SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str())!=FALSE,"owned data root passed only to fixture children");
 HANDLE notice=CreateEventW(nullptr,FALSE,FALSE,eventName(data,L"main").c_str());check(notice!=nullptr,"fixture observes original queue notifications");struct Close{HANDLE value;~Close(){CloseHandle(value);}}close{notice};
 ymcc::powerSource::channel::Publisher power;check(power.start(),"fixture owns parent power receipt publisher");ymcc::powerSource::Snapshot source;source.readable=source.known=true;source.ac=1;source.status.ACLineStatus=1;source.status.SystemStatusFlag=0;source.sampleSequence=source.revision=1;source.observedAt=GetTickCount64();check(power.publish(source),"owned AC receipt reaches the unchanged headless consumer");
 int64_t deferredAt=0,recoveredAt=0;bool deferred=false,recovered=false;Limits limits;limits.startup=0;ResourcePorts resources;resources.ac=[](){return 1;};resources.foreground=[](){return true;};
 {
  Supervisor owner(data,bin/L"CustomSteamLibrary.exe",limits,resources);owner.wake(true);const auto deadline=GetTickCount64()+4000;
  while(GetTickCount64()<deadline){const auto job=q.document()["jobs"].value(key,Json::object());if(stringField(job,"lastError")=="media-identity-network-unavailable"){deferred=true;deferredAt=now();break;}WaitForSingleObject(notice,250);}
  check(deferred,"real headless entry and worker publish the transient network reason");atomicWrite(root/L"deferred.json",q.document());
  check(q.document()["jobs"][key].value("attempts",-1)==0,"automatic owner keeps network errors outside identity debt");
  std::ofstream(exe.wstring()+L".online")<<"owned simulated network recovery";
  const auto until=GetTickCount64()+24000;
  while(GetTickCount64()<until){if(store.identity(utf8(exe.wstring())).value("appId",0)==106&&!q.document()["jobs"].contains(key)){recovered=true;recoveredAt=now();break;}WaitForSingleObject(notice,500);}
  check(recovered,"original supervisor resumes automatically without manual wake or retry edits");
  check(recoveredAt-deferredAt>=retryMs-1000,"automatic recovery respects original20s backoff");
 }
 check(q.document().value("workerPid",DWORD{1})==0&&q.document()["active"].empty(),"supervisor returns the real child and leaves no active download");
 Json result={{"ok",true},{"checks",checks},{"selection","identity-network-owner"},{"recoveryElapsedMs",recoveredAt-deferredAt},{"scope","original Supervisor20s timer, headless entry, Worker and real helper; owned registry/VDF and AC/foreground ports, no live applications"}};atomicWrite(root/L"result.json",result);std::cout<<result.dump()<<std::endl;return 0;
}
int wmain(int argc,wchar_t** argv){try{
 if((argc==5||argc==11)&&std::wstring_view(argv[1])==L"--media-background"){
  const auto data=custom_steam_library::configuredDataRoot();if(data.empty()||!fs::is_regular_file(data/L"fixture-network-owner"))return 10;
  shortcutRegistryFixture.store(true);shortcutFixtureSteamRoot=(data.parent_path()/L"steam").wstring();shortcutFixtureAccount=17;
  const auto result=backgroundEntrypoint(fs::absolute(argv[0]).parent_path());return result.value_or(10);
 }
 if(argc==4&&std::wstring_view(argv[2])==L"--steam-comprehensive-identity-only"){
  const auto exe=fs::path(argv[1]),output=fs::path(argv[3]);const auto stem=utf8(exe.stem().wstring());
  wchar_t retryOwner[64]{};GetEnvironmentVariableW(L"YEMAN_MEDIA_IDENTITY_RETRY_OWNER",retryOwner,64);
  fs::create_directories(output);std::ofstream(output.parent_path()/L"helpers.jsonl",std::ios::app)<<Json({{"pid",GetCurrentProcessId()},{"mode",utf8(argv[2])},{"exe",utf8(exe.wstring())},{"retryOwner",utf8(retryOwner)},{"configuredRoot",utf8(custom_steam_library::configuredDataRoot().wstring())}}).dump()<<"\n";
  if(stem=="hang")Sleep(30000);
  if(stem.starts_with("zero-exit-")){
   if(stem=="zero-exit-empty")return 0;
   if(stem=="zero-exit-malformed")std::ofstream(output/L"manifest.json")<<"{fixture-secret-malformed";
   else atomicWrite(output/L"manifest.json",{{"privateDetail","fixture-secret-provider-payload"},{"exe",stem=="zero-exit-foreign"?"C:/fixture-secret-other.exe":utf8(exe.wstring())},{"match",stem=="zero-exit-unconfirmed"?Json::object():verified(106)}});
   return 0;
  }
  if(stem.starts_with("network-")&&!(stem=="network-temporary"&&fs::exists(exe.wstring()+L".online"))){
   if(stem!="network-no-receipt"){
    if(stem=="network-malformed")std::ofstream(output/L"rounds-failed.json")<<"{malformed";
    else{const bool unrecognized=stem.starts_with("network-unrecognized");
     Json failure={{"kind",unrecognized?"unrecognized":"network-unavailable"},{"retryWhenNetworkReturns",!unrecognized&&stem!="network-flag-false"},{"retryableFailures",unrecognized||stem=="network-no-evidence"?0:1}};if(stem=="network-string-count")failure["retryableFailures"]="1";
     if(stem=="network-unrecognized"||stem=="network-foreign"||stem=="network-no-evidence")failure["steamLookupStage"]="candidate-not-verified";
     if(stem=="network-unrecognized-unknown-stage")failure["steamLookupStage"]="fixture-secret-candidate-title";
     if(stem=="network-unrecognized-invalid-stage")failure["steamLookupStage"]={{"name","fixture-secret-provider-payload"}};
     if(stem=="network-temporary"){failure["successfulResponses"]=UINT64_MAX;failure["nonRetryableResponses"]="fixture-secret-invalid-counter";}
     failure["privateDetail"]="fixture-secret-provider-payload";
     atomicWrite(output/L"rounds-failed.json",{{"privateDetail","fixture-secret-url"},{"executable",stem=="network-foreign"?"C:/foreign.exe":utf8(exe.wstring())},{"failure",failure}});}
   }return 1;
  }
  const auto deepFile=exe.wstring()+L".deep-output";const bool deep=fs::exists(deepFile);std::string deepMode;if(deep){std::ifstream mode(deepFile);mode>>deepMode;}
  const auto manifest=deep?output/std::wstring(80,L'g')/L"manifest.json":output/L"artwork"/L"fixture"/L"manifest.json";
  // This owned producer is observed only after its process exits. Keep its
  // temporary-path limit separate from the production queue under test.
  fs::create_directories(custom_steam_library::ioPath(manifest.parent_path()));
  std::ofstream observed(custom_steam_library::ioPath(manifest),std::ios::binary);
  if(deepMode=="malformed")observed<<"{malformed";
  else observed<<Json({{"exe",stem=="mismatch"||deepMode=="foreign"?"C:/different.exe":utf8(exe.wstring())},{"match",stem=="unconfirmed"?Json::object():verified(stem=="network-temporary"?106:stem.starts_with("fixture-")?std::stoi(stem.substr(8)):777)}}).dump(2);observed.close();
  if(!observed)throw std::runtime_error("fixture-identity-observation-write-failed");return 0;
 }
 if(argc==3&&std::wstring_view(argv[2])==L"--identity-cache-io")return identityCacheIoProbe(fs::absolute(argv[1]),fs::absolute(argv[0]));
 if(argc==5&&std::wstring_view(argv[2])==L"--identity-network"&&std::wstring_view(argv[3])==L"network-no-receipt"&&std::wstring_view(argv[4])==L"--zero-exit")return identityNetworkProbe(fs::absolute(argv[1]),fs::absolute(argv[0]),utf8(argv[3]),true);
 if(argc==4&&std::wstring_view(argv[2])==L"--identity-network"){
  const std::wstring_view selected=argv[3];
  for(const auto* name:{L"network-string-count",L"network-defers-without-spending-final-attempt-and-recovers",L"legacy-unconfirmed-policy-recovery-is-once-only",L"network-foreign",L"network-malformed",L"network-no-evidence",L"network-unrecognized",L"network-unrecognized-legacy",L"network-unrecognized-unknown-stage",L"network-unrecognized-invalid-stage"})
   if(selected==name)return identityNetworkProbe(fs::absolute(argv[1]),fs::absolute(argv[0]),utf8(argv[3]));
  throw std::runtime_error("invalid identity network case selector");
 }
 if(argc==3&&std::wstring_view(argv[2])==L"--identity-network")return identityNetworkProbe(fs::absolute(argv[1]),fs::absolute(argv[0]));
 if(argc==3&&std::wstring_view(argv[2])==L"--identity-network-owner"){
  const auto root=fs::absolute(argv[1]),self=fs::absolute(argv[0]);
  if(self.filename()!=L"YeManCC.exe"){
   // Re-enter only this original fixture in its isolated installed layout.
   const auto directory=fs::path(root.wstring()+L".source-host");fs::create_directories(directory);
   const auto parent=directory/L"YeManCC.exe";fs::copy_file(self,parent);
   auto command=identityQuote(parent.wstring())+L" "+identityQuote(root.wstring())+L" --identity-network-owner";
   std::vector<wchar_t> line(command.begin(),command.end());line.push_back(0);STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION child{};
   if(!ymcc::processEfficiency::createProcess(parent.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup,&child))throw std::runtime_error("owned-network-fixture-parent-launch-failed");
   ymcc::steaminfo::transfer::Handle process(child.hProcess),thread(child.hThread);
   if(WaitForSingleObject(child.hProcess,35000)!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,16);WaitForSingleObject(child.hProcess,2000);throw std::runtime_error("owned-network-fixture-parent-timeout");}
   DWORD code=1;if(!GetExitCodeProcess(child.hProcess,&code))throw std::runtime_error("owned-network-fixture-parent-result-missing");return static_cast<int>(code);
  }return identityNetworkOwnerProbe(root,self);
 }
 if(argc==3&&std::wstring_view(argv[2])==L"--recovery-wake")return recoveryWakeProbe(fs::absolute(argv[1]));
 if(argc==3&&std::wstring_view(argv[2])==L"--binding-repair"){
  ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;s.status.SystemStatusFlag=0;return s;});
  struct Reset{~Reset(){ymcc::powerSource::setExternalReader(nullptr);}} reset;return bindingRepairProbe(fs::absolute(argv[1]));
 }
 if(argc!=2)throw std::runtime_error("fresh fixture root required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fixture root exists");fs::create_directories(root);
 // The existing worker now consumes the shared source receipt. Supply its
 // explicit in-memory AC port; this is not physical power acceptance.
 ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;s.status.SystemStatusFlag=0;return s;});
 struct ResetPower{~ResetPower(){ymcc::powerSource::setExternalReader(nullptr);}} resetPower;
 const auto bin=root/L"bin";fs::create_directories(bin);fs::copy_file(fs::absolute(argv[0]),bin/L"SteamArtworkLab.exe");
 const auto data=root/L"data",vdf=root/L"shortcuts.vdf";Json raw=Json::array(),scan=Json::array();
 for(int n=0;n<5;n++){const auto exe=root/(L"fixture-"+std::to_wstring(100+n)+L".exe");std::ofstream(exe)<<"fixture, never executed";raw.push_back({{"appid",0x90000000u+n},{"appname","candidate "+std::to_string(n)},{"exe",utf8(exe.wstring())}});if(n<4)scan.push_back({{"primaryExecutable",utf8(exe.wstring())},{"directoryName","local duplicate"}});}
 shortcuts(vdf,raw);atomicWrite(data/L"state"/L"library-scan.json",{{"games",scan}});Store store(data);Queue q(data);
 auto inventory=store.inventory(shortcutInventoryForAccount(vdf,17,[]{return 17u;}));check(inventory.size()==5,"shortcut-only and overlapping library files join exactly once");
 for(const auto& target:inventory){check(stringField(target,"catalogSource")=="steam-shortcut","dedupe preserves added-Steam priority");check(stringField(target,"steamAccount")=="17","stable account evidence survives inventory");check(!target.value("verified",true)&&target.value("appId",-1)==0,"display names/shortcut IDs never verify store identity");}
 q.configure(true,"");q.enqueue(inventory);check(q.document()["jobs"].size()==5,"unknown added shortcuts enter production background queue");check(q.snapshot()["sourceCounts"]["identifying"]==5,"status exposes identities awaiting resolver");
 const auto before=q.snapshot().value("statusRevision",uint64_t{0});for(int n=0;n<30;n++)q.enqueue(inventory);check(q.document()["jobs"].size()==5,"repeat inventory cannot duplicate pending files");check(q.snapshot().value("statusRevision",uint64_t{0})==before,"repeat inventory causes no redundant durable write");
 auto spoof=inventory[0];spoof["appId"]=0x90000000u;spoof["verified"]=true;q.enqueue(Json::array({spoof}));check(q.document()["jobs"].size()==5,"spoofed shortcut AppID is replaced by pending exact-file identity");
 auto plain=inventory[0];plain["catalogSource"]="ymcc-confirmed";plain["executable"]=utf8((root/L"not-confirmed.exe").wstring());q.enqueue(Json::array({plain}));check(q.document()["jobs"].size()==5,"unconfirmed generic scan cannot bypass shortcut admission");
 Json order={{"native",{{"sourceTier",1},{"lastPlayed",300}}},{"shortcut",{{"sourceTier",2},{"catalogSource","steam-shortcut"},{"lastPlayed",500}}},{"confirmed",{{"sourceTier",2},{"lastPlayed",200}}},{"other",{{"sourceTier",3},{"lastPlayed",400}}}};
 for(const auto* key:{"shortcut","other","native","confirmed"}){check(nextKey(order,now())==key,"current recent-only policy preserves play order across source tiers");order.erase(key);}check(nextKey(order,now()).empty(),"empty recent queue has no eligible identity");
 const auto token=q.start();auto claimed=q.claim(token);check(!claimed.is_null()&&claimed.value("identityPending",false),"production claim selects a pending file");check(q.claim(token).is_null(),"at most one identity helper can be active");q.end(token,"deferred");
 // Complete/manual slots let the real worker exercise identification, promotion,
 // de-duplication and exit without accessing any external metadata or media.
 for(int id=100;id<105;id++){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
 IdentityPorts ports;ports.appRoot=bin;ports.current=[&](const Json& t){const auto found=shortcutIdentity(vdf,stringField(t,"shortcutAppId"));return stringField(t,"steamAccount")=="17"&&!found.empty()&&normalized(stringField(found,"executable"))==normalized(stringField(t,"executable"));};
 const auto runToken=q.start();check(!runToken.empty(),"identity queue starts despite no initial store AppIDs");check(Worker(data,runToken,GetCurrentProcess(),{},ports).run()==0,"real worker completes file identification without renderer help");
 check(q.document()["jobs"].empty(),"identity placeholders retire after protected cached output is reused");check(q.snapshot().value("workerPid",DWORD{1})==0&&q.snapshot().value("remaining",size_t{1})==0,"all identities complete and worker exits");
 for(const auto& target:inventory){const auto id=store.identity(stringField(target,"executable"));check(id.value("verified",false),"each exact-file resolver result is remembered for later media admission");check(store.resolve({{"executable",target["executable"]}}).value("key",std::string{})=="steam-"+std::to_string(id.value("appId",0)),"unknown selected file immediately reuses an existing cached canonical entry");}
 const auto count=read(data/L"config"/L"game-media.json")["entries"].size();check(count==5,"manual slots remain unchanged and no guessed media binding appears");
 auto failing=[&](const char* name,const char* expected,IdentityPorts use){const auto exe=root/(wide(name)+L".exe");std::ofstream(exe)<<"not executable";auto target=row(exe,8);const auto fresh=root/wide(name);bool rejected=false;try{identifyShortcut(fresh,target,use,[]{});}catch(const std::exception& e){rejected=std::string(e.what()).find(expected)!=std::string::npos;}check(rejected,expected);check(Store(fresh).identity(utf8(exe.wstring())).empty(),"failed/changed identity never binds the file");check(!fs::exists(fresh/L"cache"/L"jobs"/L"game-media-identity")||std::all_of(fs::directory_iterator(fresh/L"cache"/L"jobs"/L"game-media-identity"),fs::directory_iterator{},[](const auto& p){return !p.is_directory();}),"temporary resolver output is cleaned");};
 ports.current=[](const Json&){return true;};failing("mismatch","identity-unconfirmed",ports);
 const auto wrongObservation=root/L"wrong-observation.json";atomicWrite(wrongObservation,{{"exe","C:/different.exe"},{"match",verified(777)}});
 check(Store(root/L"wrong-binding").rememberIdentity(utf8((root/L"mismatch.exe").wstring()),wrongObservation,now()).value("cacheError",std::string{})=="identity-observation-target-mismatch","wrong-file resolver observation still has an explicit refusal receipt");failing("unconfirmed","identity-unconfirmed",ports);
 auto timeout=ports;timeout.timeoutMs=150;auto started=GetTickCount64();failing("hang","identity-timeout",timeout);check(GetTickCount64()-started<4000,"hung resolver is killed within bounded timeout");
 auto stale=ports;stale.current=[](const Json&){return false;};failing("stale-account","no-longer-current",stale);
 auto changing=ports;int calls=0;changing.current=[&](const Json&){return ++calls<3;};failing("changed-after-output","no-longer-current",changing);
 auto cancel=root/L"cancel.exe";std::ofstream(cancel)<<"fixture";int beats=0;bool cancelled=false;try{identifyShortcut(root/L"cancel",row(cancel,9),ports,[&]{if(++beats>1)throw std::runtime_error("media-download-cancelled");});}catch(const std::exception&e){cancelled=std::string(e.what())=="media-download-cancelled";}check(cancelled,"policy cancellation propagates before identity publication");
 // A verified exact file now becomes one canonical store task. The old
 // popular/native priority survives aliases and later generic confirmations.
 const auto queueRoot=root/L"promotion";Store promoted(queueRoot);auto target=row(root/L"fixture-100.exe",0);const auto manifest=root/L"verified.json";atomicWrite(manifest,{{"exe",target["executable"]},{"match",verified(100)}});promoted.rememberIdentity(target["executable"],manifest,now());Queue pq(queueRoot);pq.configure(true,"");pq.enqueue(Json::array({{{"appId",100},{"verified",true},{"sourceTier",2},{"catalogSource","ymcc-confirmed"}},target}));check(pq.document()["jobs"].size()==1&&stringField(pq.document()["jobs"]["steam-100"],"catalogSource")=="steam-shortcut","verified alias upgrades confirmed task to added-Steam priority exactly once");pq.enqueueCatalog(Json::array({{{"appId",100},{"verified",true},{"sourceTier",0},{"catalogSource","popular"},{"chartRank",1}}}));pq.enqueueCatalog(Json::array());check(stringField(pq.document()["jobs"]["steam-100"],"catalogSource")=="steam-shortcut","chart refresh restores remembered shortcut priority");
 // Historical unavailable rows recover on a proven file repair, not on every
 // inventory/startup. All fixtures use owned roots and the original real child.
 const auto recoveryRoot=root/L"historical-recovery",repairBin=recoveryRoot/L"bin";
 fs::create_directories(repairBin);const auto helper=repairBin/L"SteamArtworkLab.exe";
 const auto recoveryTarget=row(root/L"fixture-100.exe",0);
 Queue recover(recoveryRoot/L"data");recover.configure(true,"");recover.enqueue(Json::array({recoveryTarget}));
 const auto key=std::string("file-")+digestKey(normalized(stringField(recoveryTarget,"executable")));
 auto exhaust=[&](const std::string& reason,const std::string& stamp=std::string{}){
   const auto session=recover.start();check(!session.empty(),"recovery test starts the original queue");
   for(int n=0;n<3;++n){recover.update(session,[&](Json& j){j["jobs"][key]["retryAt"]=0;});const auto claim=recover.claim(session);check(!claim.is_null(),"original bounded attempt can be claimed");recover.result(session,key,false,reason,0,stamp);}
   recover.end(session,"deferred");check(recover.document()["jobs"][key].value("autoStopped",false),"three original failures retain the automatic stop");
 };
 exhaust("media-identity-worker-unavailable");
 const auto stopped=recover.document();check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"missing helper cannot refund historical attempts");check(recover.document()==stopped,"unrepaired history performs no durable write");
 fs::copy_file(fs::absolute(argv[0]),helper);
 check(recover.recoverIdentityPrerequisites(helper,ports.current)==1,"restored helper admits a bounded retry of legacy history");
 check(recover.document()["jobs"][key].value("attempts",-1)==0&&!recover.document()["jobs"][key].value("autoStopped",true),"legacy retry clears only that task's exhausted budget");
 const auto repaired=recover.document();for(int n=0;n<40;++n)check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"repeated inventory cannot refund the same repaired environment");
 check(recover.document()==repaired,"unchanged file environment has no queue write");
 const auto stamp=identityPrerequisites(helper,stringField(recoveryTarget,"executable")).stamp;
 exhaust("media-identity-unconfirmed",stamp);
 check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"unchanged unconfirmed identity is not retried forever");
 check(Queue(recover.root()).recoverIdentityPrerequisites(helper,ports.current)==0,"restart preserves the consumed recovery fingerprint");
 {std::ofstream change(helper,std::ios::binary|std::ios::app);change<<"new-resolver-file";}
 recover.configure(false,"");check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"OFF prevents repair-triggered retry");
 recover.configure(true,"game-running");check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"policy pause remains authoritative during repair");recover.configure(true,"");
 check(recover.recoverIdentityPrerequisites(helper,ports.current)==1,"changed resolver file permits one fresh bounded attempt");
 exhaust("media-network-request-failed",identityPrerequisites(helper,stringField(recoveryTarget,"executable")).stamp);
 {std::ofstream change(helper,std::ios::binary|std::ios::app);change<<"another-build";}
 check(recover.recoverIdentityPrerequisites(helper,ports.current)==0,"identity repair does not refund unrelated network failures");
 recover.requestSupplement(true,key);check(recover.document()["jobs"][key].value("attempts",-1)==0,"explicit manual retry remains available for unchanged files");recover.cancelManual();
 // A missing game file has its own error; restoring it is observable even
 // when the component was already installed.
 const auto absent=root/L"fixture-106.exe";auto absentTarget=row(absent,6);auto probe=ports;probe.appRoot=repairBin;probe.current=[](const Json&){return true;};
 bool missingTarget=false;try{identifyShortcut(recoveryRoot/L"missing-target",absentTarget,probe,[]{});}catch(const std::exception& e){missingTarget=std::string(e.what())=="media-identity-target-unavailable";}check(missingTarget,"missing game EXE is distinguished from missing helper");
 auto noHelper=probe;noHelper.appRoot=root/L"absent-bin";bool missingHelper=false;try{identifyShortcut(recoveryRoot/L"missing-helper",recoveryTarget,noHelper,[]{});}catch(const std::exception& e){missingHelper=std::string(e.what())=="media-identity-helper-unavailable";}check(missingHelper,"missing helper has a distinct failure reason");
 // Run the recovered historical job through the actual worker and child.
 recover.requestSupplement(true,key);recover.cancelManual();
 Store recoveryStore(recover.root());auto deleted=recoveryStore.edit({{"appId",100},{"bindingRevision",1},{"slot","static"},{"action","delete"}});recoveryStore.edit({{"key",deleted["key"]},{"bindingRevision",deleted["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});
 const auto repairedToken=recover.start();check(Worker(recover.root(),repairedToken,GetCurrentProcess(),{},probe).run()==0,"recovered historical queue completes through production worker and real child");
 check(recoveryStore.identity(stringField(recoveryTarget,"executable")).value("verified",false),"recovered task publishes a freshly verified exact-file identity");
 check(recover.document()["jobs"].empty(),"recovered identity placeholder retires instead of remaining historically blocked");
 Queue restoredTarget(recoveryRoot/L"restored-target");restoredTarget.configure(true,"");restoredTarget.enqueue(Json::array({absentTarget}));
 auto targetDoc=restoredTarget.document();const auto targetKeyValue=targetDoc["jobs"].begin().key();auto& targetJob=targetDoc["jobs"][targetKeyValue];targetJob["state"]="unavailable";targetJob["autoStopped"]=true;targetJob["attempts"]=3;targetJob["lastError"]="media-identity-worker-unavailable";targetJob["retryAt"]=0;atomicWrite(restoredTarget.root()/L"config"/L"game-media-downloads.json",targetDoc);
 check(restoredTarget.recoverIdentityPrerequisites(helper,ports.current)==0,"missing target cannot refund old failures even with a working helper");
 std::ofstream(absent)<<"restored exact file";check(restoredTarget.recoverIdentityPrerequisites(helper,ports.current)==1,"restored target file revives its historical identity task");
 // Exercise the production supervisor wake path as well. An explicit DC
 // resource port prevents any media process launch while we verify recovery.
 Queue wakeQueue(recoveryRoot/L"supervisor-wake");wakeQueue.configure(true,"");wakeQueue.enqueue(Json::array({recoveryTarget}));
 auto wakeDoc=wakeQueue.document();auto& wakeJob=wakeDoc["jobs"][key];wakeJob["state"]="unavailable";wakeJob["autoStopped"]=true;wakeJob["attempts"]=3;wakeJob["lastError"]="media-identity-worker-unavailable";wakeJob["retryAt"]=0;atomicWrite(wakeQueue.root()/L"config"/L"game-media-downloads.json",wakeDoc);
 {const auto steam=recoveryRoot/L"steam";const auto wakeVdf=steam/L"userdata"/L"17"/L"config"/L"shortcuts.vdf";
   fs::create_directories(wakeVdf.parent_path());fs::copy_file(vdf,wakeVdf);
   shortcutFixtureSteamRoot=steam.wstring();shortcutFixtureAccount=17;shortcutRegistryFixture=true;
   struct RestoreShortcut {~RestoreShortcut(){shortcutRegistryFixture=false;}} restoreShortcut;
   Limits shortLimits;shortLimits.startup=0;ResourcePorts pausedResources;pausedResources.ac=[](){return 0;};
   Supervisor owner(wakeQueue.root(),repairBin/L"CustomSteamLibrary.exe",shortLimits,pausedResources);owner.wake(true);
   const auto until=GetTickCount64()+2000;while(GetTickCount64()<until&&wakeQueue.document()["jobs"][key].value("attempts",-1)!=0)Sleep(10);
   check(wakeQueue.document()["jobs"][key].value("attempts",-1)==0,"existing supervisor wake automatically revives repaired historical identity");
   check(wakeQueue.document().value("workerPid",DWORD{0})==0,"recovery does not bypass the DC launch gate");}
 std::cout<<Json({{"ok",true},{"checks",checks},{"scope","production inventory/Queue/Worker/real identity subprocess; synthetic exact-file resolver results, no live cache/Steam writes or network"}}).dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
