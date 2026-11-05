#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>
#include <atomic>
#include <set>
static thread_local std::set<HANDLE> admissionMutexes;
static thread_local std::function<void()> admissionInterleave;
static thread_local unsigned admissionWaits=0;
static HANDLE WINAPI admissionCreateMutex(LPSECURITY_ATTRIBUTES attributes,BOOL owner,LPCWSTR name){
  const auto handle=CreateMutexW(attributes,owner,name);
  if(name&&std::wstring_view(name).starts_with(L"Local\\YMCC-SharedMedia-"))admissionMutexes.insert(handle);else admissionMutexes.erase(handle);
  return handle;
}
static DWORD WINAPI admissionWait(HANDLE handle,DWORD timeout){
  if(admissionInterleave&&admissionMutexes.count(handle)&&++admissionWaits==2){auto action=std::move(admissionInterleave);admissionInterleave={};action();}
  return WaitForSingleObject(handle,timeout);
}
static std::atomic<bool> policyObserveWait{false};
static std::atomic<unsigned> policyWaitEntries{0};
static std::atomic<DWORD> policyLastWait{0};
static DWORD WINAPI policyObservedWait(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
  if(policyObserveWait.load()&&count==2){policyLastWait.store(timeout);policyWaitEntries.fetch_add(1);}
  return WaitForMultipleObjects(count,handles,all,timeout);
}
#define WaitForMultipleObjects policyObservedWait
#define CreateMutexW admissionCreateMutex
#define WaitForSingleObject admissionWait
static HINTERNET WINAPI deniedNetwork(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#define WinHttpOpen deniedNetwork
static bool stormReplaceFailure=false,stormDiskFull=false;
static BOOL WINAPI stormReplace(LPCWSTR from,LPCWSTR to,DWORD flags){
  if(stormReplaceFailure||stormDiskFull){SetLastError(stormDiskFull?ERROR_DISK_FULL:ERROR_ACCESS_DENIED);return FALSE;}
  return MoveFileExW(from,to,flags);
}
static BOOL WINAPI stormWrite(HANDLE h,LPCVOID data,DWORD count,LPDWORD written,LPOVERLAPPED overlap){
  if(stormDiskFull){*written=0;SetLastError(ERROR_DISK_FULL);return FALSE;}
  return WriteFile(h,data,count,written,overlap);
}
#define MoveFileExW stormReplace
#define WriteFile stormWrite
// Explicit Win32 power boundary for the selected battery consumer probe only.
static std::atomic<int> batteryFixtureAc{-2};
static std::atomic<unsigned> batteryPowerReads{0};
static BOOL WINAPI batteryPowerStatus(LPSYSTEM_POWER_STATUS status){
  const auto ac=batteryFixtureAc.load();if(ac==-2)return GetSystemPowerStatus(status);
  ++batteryPowerReads;*status={};status->ACLineStatus=ac<0?255:BYTE(ac);return ac!=-3;
}
#define GetSystemPowerStatus batteryPowerStatus
#include "../native/shared_game_media_download_supervisor.h"
#include "../native/shared_game_media_download_worker.h"
#include "../native/shared_game_media_server.h"
#include "../native/steam_wallpaper_foreground.h"
#undef GetSystemPowerStatus
#undef WinHttpOpen
#undef MoveFileExW
#undef WriteFile
#undef CreateMutexW
#undef WaitForSingleObject
#undef WaitForMultipleObjects
#include <iostream>
// The fixture drives the same shared producer receipt as native events. Consumers
// must not gain a fallback direct power query to make a test pass.
static void batteryFixtureSource(int ac){
  batteryFixtureAc.store(ac);
  ymcc::powerSource::owner().sample(batteryPowerStatus,GetTickCount64(),true);
}
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static int checks = 0;
static Json completedIdleMetrics=Json::object();
static void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
static Json target(int id, int64_t played = 0) {
  return {{"appId", id},
          {"name", "Fixture " + std::to_string(id)},
          {"verified", true},
          {"lastPlayed", played}};
}
static bool waitUntil(const std::function<bool()> &fn, DWORD timeout = 8000) {
  const auto end = GetTickCount64() + timeout;
  do {
    if (fn())
      return true;
    Sleep(20);
  } while (GetTickCount64() < end);
  return false;
}
static int fakeChild(int argc, wchar_t **argv) {
  const auto root = custom_steam_library::configuredDataRoot();
  Queue queue(root);
  const auto token = utf8(argv[4]);
  const auto fixture=read(root/L"fixture.json");
  if(fixture.contains("powerAc"))batteryFixtureSource(fixture["powerAc"].get<int>());
  auto mode = fixture.value("mode", std::string{});
  if (mode == "production") {
    return backgroundEntrypoint(root).value_or(99);
  }
  queue.update(token, [](Json &j) {
    j["workerPid"] = GetCurrentProcessId();
    j["workerBirth"] = processBirthToken(GetCurrentProcessId());
  });
  atomicWrite(root / L"started.json",
              {{"pid", GetCurrentProcessId()},
               {"birth", processBirthToken(GetCurrentProcessId())},{"wallpaperPauseOnBattery",powerPolicy::enabled()},{"powerReads",batteryPowerReads.load()}});
  if (mode == "preclaim-crash")
    return 78;
  auto claimed = queue.claim(token);
  if (mode == "stall")
    Sleep(INFINITE);
  if (mode == "crash")
    return 77;
  if(mode=="terminal-gate"){
    check(waitUntil([&]{return fs::exists(root/L"child-observed");},2000),"parent retained the exact child before terminal publication");const auto fixture=read(root/L"fixture.json");const auto phase=fixture.value("terminalPhase",std::string{"completed"});
    {Lock lock(root);queue.result(token,claimed["key"],phase=="completed",phase=="completed"?"":"fixture-upstream-detail");if(fixture.value("nextRequest",false))queue.requestSupplement(true,"steam-571");queue.end(token,phase,phase=="completed"?"":"fixture-upstream-detail");atomicWrite(root/L"terminal-published.json",{{"session",token},{"phase",phase},{"pid",GetCurrentProcessId()}});}
    if(fixture.value("hangAfterEnd",false))Sleep(INFINITE);
    if(!fixture.value("exitImmediately",false))waitUntil([&]{return fs::exists(root/L"allow-child-exit");},1500);
    return fixture.value("exitCode",0);
  }
  if (mode == "complete") {
    queue.result(token, claimed["key"], true);
    queue.end(token, "completed");
    return 0;
  }
  return 98;
}

static std::string stormBytes(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void stormFile(const fs::path& p,const std::string& text){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<text;}
static Json policyParkedSample(unsigned duration){
  const auto cpu=[](){FILETIME c{},e{},k{},u{};check(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)!=FALSE,"policy CPU sample available");ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;};
  IO_COUNTERS before{},after{};check(GetProcessIoCounters(GetCurrentProcess(),&before)!=FALSE,"policy IO start available");const auto tick=GetTickCount64(),started=cpu();const auto waits=policyWaitEntries.load();Sleep(duration);const auto elapsed=GetTickCount64()-tick,used=cpu()-started;check(GetProcessIoCounters(GetCurrentProcess(),&after)!=FALSE,"policy IO end available");
  return {{"elapsedMs",elapsed},{"cpuMs",double(used)/10000.0},{"readOperations",after.ReadOperationCount-before.ReadOperationCount},{"writeOperations",after.WriteOperationCount-before.WriteOperationCount},{"readBytes",after.ReadTransferCount-before.ReadTransferCount},{"writtenBytes",after.WriteTransferCount-before.WriteTransferCount},{"additionalOwnerWaits",policyWaitEntries.load()-waits},{"lastOwnerWaitMs",policyLastWait.load()}};
}
struct PolicyObserveGuard{PolicyObserveGuard(){policyWaitEntries=0;policyLastWait=0;policyObserveWait=true;}~PolicyObserveGuard(){policyObserveWait=false;}};
struct PolicyOwnedChild{
  std::wstring previous;
  explicit PolicyOwnedChild(const fs::path& root){
    wchar_t env[32768]{},self[32768]{};const auto n=GetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",env,32768);if(n&&n<32768)previous.assign(env,n);check(GetModuleFileNameW(nullptr,self,32768)>0,"owned policy helper source available");fs::create_directories(root);fs::copy_file(fs::path(self),root/L"CustomSteamLibrary.exe");
    atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",videoQualityPolicy},{"backgroundManualProtocol",1},{"backgroundVideoSelectionProtocol",1}});atomicWrite(root/L"fixture.json",{{"mode","complete"}});SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
  }
  ~PolicyOwnedChild(){SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",previous.empty()?nullptr:previous.c_str());}
};
static bool stormPreserved(const fs::path& p,const std::string& text){
  if(fs::is_regular_file(p)&&stormBytes(p)==text)return true;
  for(const auto& e:fs::directory_iterator(p.parent_path()))
    if(e.path().filename().wstring().starts_with(p.filename().wstring()+L".corrupt.")&&stormBytes(e.path())==text)return true;
  return false;
}
static int queueStorm(const fs::path& base,const std::string& only={},int costPublications=6){
  if(fs::exists(base))throw std::runtime_error("fresh storm evidence directory required");
  fs::create_directories(base);Json results=Json::array();
  auto run=[&](const std::string& name,const std::function<void(const fs::path&)>& fn){
    if(!only.empty()&&name!=only)return;
    const auto begin=GetTickCount64();const auto root=base/wide(name);
    try{fn(root);results.push_back({{"case",name},{"status","PASS"},{"ms",GetTickCount64()-begin}});}
    catch(const std::exception& e){stormReplaceFailure=false;stormDiskFull=false;results.push_back({{"case",name},{"status","FAIL"},{"error",e.what()},{"ms",GetTickCount64()-begin}});}
    std::cout<<results.back().dump()<<std::endl;
  };
  std::vector<std::pair<std::string,std::string>> bad={
    {"broken-json","{\"jobs\":"},{"wrong-root","[]"},{"trailing-json","{} false"},
    {"jobs-array",R"({"jobs":[]})"},{"job-null",R"({"jobs":{"steam-570":null}})"},
    {"job-attempts",R"({"jobs":{"steam-570":{"appId":570,"verified":true,"attempts":"2"}}})"},
    {"enabled-type",R"({"enabled":"false"})"},{"manual-targets-type",R"({"manualTargets":[null]})"},
    {"old-owner-type",R"({"workerPid":"123","workerBirth":false,"session":"old"})"},
    {"old-owner-range",R"({"workerPid":4294967296,"workerBirth":"1","session":"old"})"},
    {"old-owner-birth",R"({"workerPid":123,"workerBirth":"not-a-birth","session":"old"})"},
    {"identity-mismatch",R"({"jobs":{"steam-570":{"appId":730,"verified":true,"state":"pending"}}})"},
    {"oversized",std::string(16*1024*1024+1,' ')}};
  for(const auto& [name,text]:bad)run(name,[&](const fs::path& root){
    const auto p=root/L"config"/L"game-media-downloads.json";stormFile(p,text);Queue q(root);
    const auto safe=q.document();check(safe["jobs"].empty(),"invalid queue must expose safe empty work");
    check(q.start().empty(),"invalid queue cannot authorize work");
    q.configure(false,"");check(stormPreserved(p,text),"corrupt original must survive recovery persistence");
    q.enqueue(Json::array({target(571)}));q.configure(true,"");const auto token=q.start();
    check(!token.empty(),"recovery can persist a new confirmed inventory and resume");
    check(q.claim(token)["appId"]==571,"recovered consumer claims actual verified target");
    q.result(token,"steam-571",false,"fixture-transient");
    check(Queue(root).document()["jobs"]["steam-571"]["attempts"]==1,"retry debt survives reread");
    check(Queue(root).document().value("recoveryRequired",false),"unreadable historical debt remains explicitly unconfirmed");
  });
  run("corruption-keeps-known-debt",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));const auto path=root/L"config"/L"game-media-downloads.json";auto j=read(path);
    auto& job=j["jobs"]["steam-570"];job["attempts"]=2;job["automaticElapsedMs"]=1234;job["retryAt"]=now()+60000;job["unknownJob"]={{"keep",1}};j["enabled"]="damaged";j["unknownRoot"]={{"keep",true}};atomicWrite(path,j);
    q.configure(true,"");check(q.document()["unknownRoot"]==j["unknownRoot"],"unknown root fields survive partial corruption");check(q.start().empty(),"salvaged rows await real inventory confirmation");q.enqueue(Json::array({target(570)}));const auto saved=q.document()["jobs"]["steam-570"];
    for(const auto* field:{"attempts","automaticElapsedMs","retryAt","unknownJob"})check(saved[field]==job[field],"independent valid debt and unknown values survive root corruption");
    check(q.start().empty(),"confirmed inventory cannot clear salvaged backoff");
  });
  run("unknown-debt-stays-unconfirmed",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));const auto path=root/L"config"/L"game-media-downloads.json";auto j=read(path);j["jobs"]["steam-570"]["attempts"]="unknown";atomicWrite(path,j);
    q.configure(true,"");q.enqueue(Json::array({target(570),target(571)}));const auto token=q.start();check(!token.empty()&&q.claim(token)["appId"]==571,"unrelated confirmed work continues past damaged debt");
    check(!q.document()["jobs"].contains("steam-570"),"unknown debt cannot be re-created with zero attempts");
    check(q.document()["recoveryBlockedKeys"]==Json::array({"steam-570"}),"damaged debt stays explicitly pending confirmation");
  });
  run("inventory-cannot-overwrite-newer-selected-video",[&](const fs::path& root){
    Store media(root);Queue q(root);auto entry=media.registerEntry(target(570));entry["action"]="select-video";entry["slot"]="dynamic";entry["selectedVideo"]={{"appId",570},{"url","https://video.steamstatic.com/old.mp4"}};entry=media.edit(entry);q.enqueue(Json::array({target(570)}));
    std::string acceptedBytes;uint64_t acceptedRevision=0;unsigned crossed=0;admissionWaits=0;admissionInterleave=[&]{++crossed;auto next=media.resolve(entry);next["action"]="select-video";next["slot"]="dynamic";next["selectedVideo"]={{"appId",570},{"url","https://video.steamstatic.com/new.mp4"}};next=media.edit(next);acceptedRevision=next["bindingRevision"];q.enqueue(Json::array({target(570)}));acceptedBytes=stormBytes(root/L"config"/L"game-media-downloads.json");};
    q.enqueue(Json::array({target(570)}));admissionInterleave={};const auto job=q.document()["jobs"]["steam-570"];
    check(crossed==1,"actual queue admission interleave did not execute");check(job["selectedVideo"]["url"]=="https://video.steamstatic.com/new.mp4"&&job["requestBindingRevision"]==acceptedRevision,"older inventory overwrote the accepted queued video");check(stormBytes(root/L"config"/L"game-media-downloads.json")==acceptedBytes,"stale inventory rewrote a newer queue");
  });
  run("inventory-cannot-enqueue-after-manual-wallpapers-complete",[&](const fs::path& root){
    Store media(root);Queue q(root);auto entry=media.registerEntry(target(570));q.configure(true,"");const auto original=stormBytes(root/L"config"/L"game-media-downloads.json");unsigned crossed=0;
    admissionWaits=0;admissionInterleave=[&]{++crossed;for(const auto* name:{"sample.jpg","sample.mp4"}){const auto file=root/wide(name);stormFile(file,std::string_view(name).ends_with(".mp4")?std::string("\0\0\0\x18" "ftypisom",12):std::string("\xff\xd8\xff\xe0",4));entry=media.publish(entry,file,true);}};
    q.enqueue(Json::array({target(570)}));admissionInterleave={};check(crossed==1,"manual completion interleave missing");check(q.document()["jobs"].empty()&&q.delay()<0,"stale inventory queued an unnecessary worker after manual completion");check(stormBytes(root/L"config"/L"game-media-downloads.json")==original,"stale completed inventory generated a queue write");
  });
  run("unchanged-inventory-remains-eligible-with-exact-new-choice",[&](const fs::path& root){
    Store media(root);Queue q(root);auto entry=media.registerEntry(target(570));entry["action"]="select-video";entry["slot"]="dynamic";entry["selectedVideo"]={{"appId",570},{"url","https://video.steamstatic.com/current.mp4"}};entry=media.edit(entry);q.enqueue(Json::array({target(570)}));const auto original=stormBytes(root/L"config"/L"game-media-downloads.json");q.enqueue(Json::array({target(570)}));
    check(q.document()["jobs"]["steam-570"]["requestBindingRevision"]==entry["bindingRevision"]&&q.snapshot().value("remaining",0)==1,"stable inventory lost its pending choice");check(stormBytes(root/L"config"/L"game-media-downloads.json")==original,"unchanged inventory rewrote its queue");
  });
  run("unrelated-editor-change-does-not-drop-required-inventory-work",[&](const fs::path& root){
    Store media(root);Queue q(root);auto entry=media.registerEntry(target(570));unsigned crossed=0;admissionWaits=0;admissionInterleave=[&]{++crossed;auto next=media.resolve(entry);next["action"]="playback";next["start"]=5;next["end"]=35;media.edit(next);};q.enqueue(Json::array({target(570)}));admissionInterleave={};
    check(crossed==1&&q.document()["jobs"].contains("steam-570")&&q.snapshot().value("remaining",0)==1,"new playback setting dropped required automatic work");
  });
  run("new-choice-before-queue-admission-is-used-by-the-inventory",[&](const fs::path& root){
    Store media(root);Queue q(root);auto entry=media.registerEntry(target(570));entry["action"]="select-video";entry["slot"]="dynamic";entry["selectedVideo"]={{"appId",570},{"url","https://video.steamstatic.com/old.mp4"}};entry=media.edit(entry);uint64_t revision=0;
    admissionWaits=0;admissionInterleave=[&]{auto next=media.resolve(entry);next["action"]="select-video";next["slot"]="dynamic";next["selectedVideo"]={{"appId",570},{"url","https://video.steamstatic.com/new.mp4"}};revision=media.edit(next)["bindingRevision"];};q.enqueue(Json::array({target(570)}));admissionInterleave={};const auto job=q.document()["jobs"]["steam-570"];
    check(revision>entry["bindingRevision"].get<uint64_t>()&&job["requestBindingRevision"]==revision&&job["selectedVideo"]["url"]=="https://video.steamstatic.com/new.mp4","inventory admitted a superseded unqueued video choice");
  });
  run("manual-expiry-bounds-a-retry-deadline",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(620)}));q.requestSupplement(true,"steam-570");const auto token=q.start();q.claim(token);q.result(token,"steam-570",false,"cache-owned-or-deferred");q.update(token,[](Json& j){j["manualUntil"]=now()+160;});
    const auto delay=q.delay();check(delay>=0&&delay<=160,"retry sleep extended past the manual deadline");Sleep(200);
    check(q.delay()==0,"expired scoped retry hid the other ready automatic game");
  });
  run("manual-expiry-after-final-result-does-not-park-other-work",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(620)}));q.requestSupplement(true,"steam-570");const auto token=q.start();q.claim(token);q.result(token,"steam-570",true);q.update(token,[](Json& j){j["manualUntil"]=now()+160;});const auto jobs=q.document()["jobs"];
    // Simulate restart between the final result and the previous owner's end.
    Sleep(200);Queue resumed(root);check(resumed.delay()==0,"cached scoped completion parked an unrelated ready game after expiry");
    check(resumed.recheck(),"expired privilege was not durably retired");const auto doc=resumed.document();check(doc.value("manualUntil",int64_t{1})==0&&doc.value("manualState",std::string{})=="expired"&&doc["jobs"]==jobs,"expiry changed job results/debt instead of retiring manual scope");
    const auto path=root/L"config"/L"game-media-downloads.json";const auto bytes=stormBytes(path);const auto stamp=fs::last_write_time(path);check(!resumed.recheck()&&stormBytes(path)==bytes&&fs::last_write_time(path)==stamp,"expired scope kept rewriting stable state");
    const auto next=resumed.start();check(!next.empty()&&resumed.claim(next).value("appId",0)==620,"automatic backlog did not resume at its eligible next game");resumed.end(next,"completed");
  });
  run("expired-manual-privilege-never-enables-durable-automatic-off",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(620)}));q.configure(false,"game-running");q.requestSupplement(true,"steam-570");const auto token=q.start();q.claim(token);q.result(token,"steam-570",false,"cache-owned-or-deferred");q.update(token,[](Json& j){j["manualUntil"]=now()+100;});Sleep(150);q.recheck();const auto state=q.document();
    check(!state.value("enabled",true)&&state.value("pauseReason",std::string{})=="game-running"&&!manualRun(state)&&q.delay()<0&&q.start().empty(),"manual expiry enabled automatic work or changed durable game policy");
  });
  run("inventory-wait-cannot-outlive-manual-privilege",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(false,"");q.requestSupplement(true);const auto path=root/L"config"/L"game-media-downloads.json";auto doc=read(path);doc["manualUntil"]=now()+160;atomicWrite(path,doc);q.recheck();const auto delay=q.delay();
    check(delay>=0&&delay<=160,"inventory wait ignored the earlier manual deadline");Sleep(200);check(q.delay()<0&&q.start().empty(),"expired inventory wait authorized background work while automatic downloads were OFF");
  });
  run("supervisor-wakes-at-manual-expiry-and-completes-next-game",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(620)}));q.requestSupplement(true,"steam-570");const auto token=q.start();q.claim(token);q.result(token,"steam-570",true);q.update(token,[](Json& j){j["manualUntil"]=now()+350;});
    wchar_t self[32768]{};check(GetModuleFileNameW(nullptr,self,32768)>0,"owned current binary path available");fs::copy_file(fs::path(self),root/L"CustomSteamLibrary.exe");
    atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",videoQualityPolicy},{"backgroundManualProtocol",1},{"backgroundVideoSelectionProtocol",1}});atomicWrite(root/L"fixture.json",{{"mode","complete"}});
    wchar_t prior[32768]{};const auto priorCount=GetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",prior,32768);const std::wstring previous=priorCount&&priorCount<32768?std::wstring(prior,priorCount):std::wstring{};
    struct Restore{std::wstring value;~Restore(){SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",value.empty()?nullptr:value.c_str());}} restore{previous};SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
    bool completed=false;{Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};},[]{return false;}});owner.wake(true);
      completed=waitUntil([&]{return fs::exists(root/L"started.json")&&q.snapshot().value("remaining",size_t{1})==0&&q.snapshot().value("workerPid",DWORD{1})==0;},2000);
    }
    check(completed,"actual supervisor failed to wake and finish the next eligible game after manual expiry");check(q.document()["jobs"]["steam-570"]["state"]=="done"&&q.document()["jobs"]["steam-620"]["state"]=="done","expiry relaunched the completed manual item or lost the pending game");
  });
  run("clock-rollback-checkpoint-cannot-park-unrelated-automatic-work",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(620)}));q.requestSupplement(true,"steam-570");const auto token=q.start();q.claim(token);q.result(token,"steam-570",true);
    const auto queue=root/L"config"/L"game-media-downloads.json",status=root/L"config"/L"game-media-download-progress.json";auto doc=read(queue),small=read(status);const auto until=now()+int64_t(sessionMs)+60000;doc["manualUntil"]=until;small["manualUntil"]=until;
    // Both valid stored records represent a backward clock jump; the OS clock
    // is never changed. Keep the original cached scoped nextDueAt untouched.
    atomicWrite(queue,doc);atomicWrite(status,small);check(!manualRun(q.document()),"out-of-window manual deadline retained temporary privilege");check(q.delay()==0,"out-of-window scoped cache hid the automatic backlog");q.recheck();check(q.document().value("manualUntil",int64_t{1})==0&&q.document()["jobs"]==doc["jobs"],"clock-boundary repair changed job debt instead of retiring the invalid privilege");
  });
  const auto parkedPolicy=[&](const fs::path& root,const std::string& pause){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(true,pause);if(pause=="media-storage-unavailable")q.requestSupplement(true,"steam-570");const auto revision=q.snapshot().value("statusRevision",uint64_t{0});std::atomic<int> ac{0},cpu{0},focus{0};PolicyObserveGuard observation;
    Supervisor owner(root,root/L"missing.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[&]{++ac;return 1;},[&](uint64_t){++cpu;return std::optional<double>{0};},[&]{++focus;return false;}});owner.wake(true);
    check(waitUntil([]{return policyWaitEntries.load()>=2;},1500),"owner entered its post-activation wait");auto metric=policyParkedSample(700);policyObserveWait=false;metric["pauseReason"]=pause;metric["acChecks"]=ac.load();metric["cpuSamples"]=cpu.load();metric["foregroundChecks"]=focus.load();atomicWrite(root/L"policy-idle-metrics.json",metric);
    check(metric["readOperations"]==0&&metric["writeOperations"]==0&&metric["additionalOwnerWaits"]==0&&metric["lastOwnerWaitMs"]==INFINITE,"known policy pause retained a native periodic repair poll");
    check(ac==0&&cpu==0&&focus==0&&q.snapshot().value("statusRevision",uint64_t{0})==revision&&!fs::exists(root/L"started.json"),"policy pause changed admission, probed resources or launched a helper");
  };
  run("game-policy-pause-parks-native-owner",[&](const fs::path& root){parkedPolicy(root,"game-running");});
  run("storage-policy-pause-parks-native-owner",[&](const fs::path& root){parkedPolicy(root,"media-storage-unavailable");});
  run("clearing-policy-notifies-parked-owner-and-completes-work",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(true,"game-running");PolicyOwnedChild child(root);
    Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};},[]{return false;}});owner.wake(true);Sleep(100);check(!fs::exists(root/L"started.json"),"policy pause launched its helper before permission returned");q.configure(true,"");
    check(waitUntil([&]{return fs::exists(root/L"started.json")&&q.snapshot().value("remaining",size_t{1})==0&&q.snapshot().value("workerPid",DWORD{1})==0;},2000),"clearing policy did not wake the actual owner through its existing queue event");
  });
  run("allowed-orphaned-work-retains-bounded-native-repair",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto old=q.start();q.claim(old);PolicyOwnedChild child(root);
    Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};},[]{return false;}});owner.wake(true);
    check(waitUntil([&]{return fs::exists(root/L"started.json")&&q.snapshot().value("remaining",size_t{1})==0&&q.snapshot().value("workerPid",DWORD{1})==0;},2000),"parking an explicit pause disabled the original orphan recovery path");
  });
  run("manual-game-override-wakes-an-event-parked-owner",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(false,"game-running");PolicyOwnedChild child(root);std::atomic<int> cpu{0};
    Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[&](uint64_t){++cpu;return std::optional<double>{95};},[]{return false;}});owner.wake(true);Sleep(100);check(!fs::exists(root/L"started.json"),"paused automatic work launched before the manual request");q.requestSupplement(true,"steam-570");
    check(waitUntil([&]{return fs::exists(root/L"started.json")&&q.snapshot().value("remaining",size_t{1})==0&&q.snapshot().value("workerPid",DWORD{1})==0;},2000),"explicit manual game override could not wake an event-parked owner");check(cpu==0&&!q.document().value("enabled",true)&&!manualRun(q.document()),"manual request polled CPU, enabled automatic work or retained temporary privilege");
  });
  const auto terminalHandoff=[&](const fs::path& root,const std::string& phase,bool hung,bool stop,bool immediate,bool nextRequest=false){
    Queue q(root);q.enqueue(nextRequest?Json::array({target(570),target(571)}):Json::array({target(570)}));q.configure(false,"game-running");q.requestSupplement(true,"steam-570");PolicyOwnedChild child(root);atomicWrite(root/L"fixture.json",{{"mode","terminal-gate"},{"nextRequest",nextRequest},{"terminalPhase",phase},{"hangAfterEnd",hung},{"exitImmediately",immediate},{"exitCode",phase=="deferred"?14:0}});std::atomic<int> cpu{0};
    auto owner=std::make_unique<Supervisor>(root,root/L"CustomSteamLibrary.exe",Limits{500,2000,2000,20,0,50},ResourcePorts{[]{return 1;},[&](uint64_t){++cpu;return std::optional<double>{95};},[]{return false;}});owner->wake(true);
    check(waitUntil([&]{return fs::exists(root/L"started.json");},1500),"terminal fixture child started");const auto started=read(root/L"started.json");HANDLE exact=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,started.value("pid",DWORD{0}));check(exact!=nullptr,"retain exact child handle before terminal publication");struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{exact};
    stormFile(root/L"child-observed","yes");check(waitUntil([&]{return fs::exists(root/L"terminal-published.json");},1000),"child durable terminal receipt was published");Sleep(immediate?50:120);
    const auto accepted=q.document();atomicWrite(root/L"observed-before-exit.json",{{"cpuSamples",cpu.load()},{"state",accepted}});
    check(cpu==0&&accepted.value("phase",std::string{})==phase&&accepted.value("resourcePauseReason",std::string{}).empty(),"post-terminal wake rechecked CPU or replaced the durable terminal receipt");
    if(stop)owner.reset();else if(!hung)stormFile(root/L"allow-child-exit","yes");
    check(waitUntil([&]{return WaitForSingleObject(exact,0)==WAIT_OBJECT_0;},2500),"completed/failed/cleanup-hung child exceeded the original ownership bound");if(owner)check(waitUntil([&]{return q.document().contains("lastWorkerMetrics");},1500),"parent worker outcome recorded");owner.reset();
    const auto final=q.document();atomicWrite(root/L"handoff-result.json",{{"cpuSamples",cpu.load()},{"state",final}});check(final.value("phase",std::string{})==phase&&final["history"].size()==1,"parent teardown overwrote or duplicated the accepted terminal receipt");
    check(!final.value("enabled",true)&&manualRun(final)==nextRequest&&final["jobs"]["steam-570"]["state"]==(phase=="completed"?"done":"pending"),"terminal handoff changed durable OFF, manual privilege or job outcome");
    if(nextRequest)check(final["jobs"]["steam-571"]["state"]=="pending"&&final["manualTargets"]==Json::array({"steam-570","steam-571"}),"terminal drain erased newer scoped work or its privilege");
    if(phase=="deferred")check(final.value("reason",std::string{})=="fixture-upstream-detail"&&final["jobs"]["steam-570"].value("attempts",0)==1,"nonzero diagnostic exit replaced the specific failure or charged retry debt again");
  };
  run("durable-completion-survives-child-exit-gap",[&](const fs::path& root){terminalHandoff(root,"completed",false,false,false);});
  run("durable-deferred-reason-survives-nonzero-child-exit",[&](const fs::path& root){terminalHandoff(root,"deferred",false,false,false);});
  run("durable-terminal-receipt-survives-immediate-failure-exit",[&](const fs::path& root){terminalHandoff(root,"deferred",false,false,true);});
  run("hung-child-after-completion-is-bounded-without-reopening-work",[&](const fs::path& root){terminalHandoff(root,"completed",true,false,false);});
  run("owner-stop-after-completion-preserves-the-accepted-receipt",[&](const fs::path& root){terminalHandoff(root,"completed",false,true,false);});
  run("cancelled-manual-worker-stops-without-late-resource-probes",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(false,"game-running");q.requestSupplement(true,"steam-570");PolicyOwnedChild child(root);atomicWrite(root/L"fixture.json",{{"mode","stall"}});std::atomic<int> cpu{0};
    Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[&](uint64_t){++cpu;return std::optional<double>{95};},[]{return false;}});owner.wake(true);check(waitUntil([&]{return fs::exists(root/L"started.json");},1500),"cancel fixture child started");HANDLE exact=OpenProcess(SYNCHRONIZE,FALSE,read(root/L"started.json").value("pid",DWORD{0}));check(exact!=nullptr,"retain cancelled exact child");struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{exact};q.cancelManual();check(waitUntil([&]{return WaitForSingleObject(exact,0)==WAIT_OBJECT_0;},2500),"cancelled manual child did not exit");
    check(cpu==0&&q.snapshot().value("resourcePauseReason",std::string{}).empty()&&q.document().value("manualState",std::string{})=="cancelled"&&!q.document().value("enabled",true),"cancelled worker sampled CPU or replaced cancellation policy with resource failure");
  });
  run("terminal-receipt-preserves-a-newer-pending-manual-request",[&](const fs::path& root){terminalHandoff(root,"completed",false,false,false,true);});
  run("terminal-receipt-needs-matching-history-and-released-lanes",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto token=q.start();q.claim(token);
    check(!Queue::terminalReceipt(q.snapshot(),token),"active work mistaken for a terminal receipt");q.end(token,"completed");const auto accepted=q.snapshot();check(Queue::terminalReceipt(accepted,token),"actual terminal receipt was not recognized");
    for(const auto* variant:{"session","phase","history","history-session","history-phase","worker","active","current"}){auto invalid=accepted;
      const std::string v=variant;if(v=="session")invalid["session"]="another-session";else if(v=="phase")invalid["phase"]="pending";else if(v=="history")invalid["history"]=Json::array();else if(v=="history-session")invalid["history"].back()["session"]="another-session";else if(v=="history-phase")invalid["history"].back()["phase"]="deferred";else if(v=="worker")invalid["workerPid"]=GetCurrentProcessId();else if(v=="active")invalid["active"]={{"steam-570",Json::object()}};else invalid["current"]={{"key","steam-570"}};
      check(!Queue::terminalReceipt(invalid,token),"phase alone, stale history or unreleased ownership authorized terminal drain");}
  });
  run("late-resource-observation-cannot-reopen-a-terminal-receipt",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto token=q.start();q.claim(token);q.end(token,"deferred","upstream-detail");const auto path=root/L"config"/L"game-media-downloads.json",progress=root/L"config"/L"game-media-download-progress.json";const auto accepted=stormBytes(path),status=stormBytes(progress);q.resource("cpu-busy",95,0,token);
    check(stormBytes(path)==accepted&&stormBytes(progress)==status,"late resource write changed the durable receipt or status");
  });
  run("resource-publication-rejects-old-session-and-clears-current-pause",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto old=q.start();q.end(old,"deferred");const auto token=q.start();check(!token.empty()&&token!=old,"second session started");const auto path=root/L"config"/L"game-media-downloads.json",progress=root/L"config"/L"game-media-download-progress.json";const auto accepted=stormBytes(path),status=stormBytes(progress);q.resource("cpu-busy",95,0,old);
    check(stormBytes(path)==accepted&&stormBytes(progress)==status,"old resource observation affected a replacement session");q.resource("cpu-busy",95,0,token);check(!q.allowed(token),"current CPU pause did not block work");q.resource("",0,0,token);check(q.allowed(token),"resource recovery could not clear a current CPU pause");q.configure(false,"");const auto off=stormBytes(path);q.resource("cpu-busy",95,0,token);check(stormBytes(path)==off,"late resource write overwrote durable OFF");
  });
  for(const auto* interruption:{"automatic-off","storage-pause","completed-replacement-session"})run(std::string("prelaunch-resource-observation-cannot-overwrite-")+interruption,[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto path=root/L"config"/L"game-media-downloads.json",status=root/L"config"/L"game-media-download-progress.json";std::atomic<int> samples{0};std::atomic<bool> interrupted{false};std::string accepted,acceptedStatus;const std::string mode=interruption;
    {Supervisor owner(root,root/L"unlaunched.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[&](uint64_t){++samples;
       if(mode=="automatic-off")q.configure(false,"");else if(mode=="storage-pause")q.configure(true,"media-storage-unavailable");else{const auto token=q.start();const auto job=q.claim(token);check(!job.is_null(),"replacement session claims real work");q.result(token,job["key"],true);q.end(token,"completed");}
       accepted=stormBytes(path);acceptedStatus=stormBytes(status);interrupted.store(true);return std::optional<double>{95};},[]{return false;}});owner.wake(true);
      check(waitUntil([&]{return interrupted.load();},1000),"resource admission interleave executed");Sleep(120);}
    const auto final=q.document();atomicWrite(root/L"resource-interleave-result.json",{{"state",final},{"cpuSamples",samples.load()},{"acceptedDurableUnchanged",stormBytes(path)==accepted},{"acceptedStatusUnchanged",stormBytes(status)==acceptedStatus}});
    check(samples==1,"resource publication started additional CPU work after interruption");check(stormBytes(path)==accepted&&stormBytes(status)==acceptedStatus,"in-flight prelaunch resource observation overwrote a newer OFF/policy/terminal state");
  });
  run("resource-publication-fixed-inventory-cost",[&](const fs::path& root){
    Queue q(root);Json inventory=Json::array();for(int id=1;id<=256;++id)inventory.push_back(target(id,id));q.enqueue(inventory);q.configure(true,"");q.resource("");const auto revision=q.snapshot().value("statusRevision",uint64_t{0});
    const auto cpu=[](){FILETIME c{},e{},k{},u{};check(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)!=FALSE,"resource cost CPU available");ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;};
    IO_COUNTERS a{},z{};check(GetProcessIoCounters(GetCurrentProcess(),&a)!=FALSE,"resource cost IO start");const auto started=GetTickCount64(),used=cpu();for(int n=0;n<costPublications;++n)q.resource(n%2?"":"cpu-busy",n%2?0:95);const auto elapsed=GetTickCount64()-started,spent=cpu()-used;check(GetProcessIoCounters(GetCurrentProcess(),&z)!=FALSE,"resource cost IO end");
    const auto final=q.snapshot();check(final.value("remaining",size_t{0})==256&&final.value("statusRevision",uint64_t{0})==revision+costPublications&&final.value("resourcePauseReason",std::string{}).empty(),"resource cost changed work or publication count");
    atomicWrite(root/L"resource-cost.json",{{"inventory",256},{"publications",costPublications},{"elapsedMs",elapsed},{"cpuMs",double(spent)/10000.0},{"readOperations",z.ReadOperationCount-a.ReadOperationCount},{"readBytes",z.ReadTransferCount-a.ReadTransferCount},{"writeOperations",z.WriteOperationCount-a.WriteOperationCount},{"writtenBytes",z.WriteTransferCount-a.WriteTransferCount},{"scope","fixed isolated process resource publications; no installed CPU claim"}});
  });
  run("resource-publication-rejects-a-superseded-threshold-and-manual-intent",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570),target(571)}));q.configure(true,"",15);q.resource("");const auto path=root/L"config"/L"game-media-downloads.json",status=root/L"config"/L"game-media-download-progress.json";
    const auto old=q.snapshot();q.configure(true,"",90);auto bytes=stormBytes(path),progress=stormBytes(status);check(!q.resource("cpu-busy",50,0,"",&old)&&stormBytes(path)==bytes&&stormBytes(status)==progress,"old threshold publication modified the new admission");
    q.configure(false,"");q.requestSupplement(true,"steam-570");const auto first=q.snapshot();q.requestSupplement(true,"steam-571");bytes=stormBytes(path);progress=stormBytes(status);check(!q.resource("cpu-busy",95,0,"",&first)&&stormBytes(path)==bytes&&stormBytes(status)==progress,"superseded manual selection publication modified the new request");
    const auto current=q.snapshot();check(q.resource("",0,0,"",&current),"current manual request cannot publish resource readiness");check(!q.document().value("enabled",true)&&q.document()["manualTargets"].size()==2,"resource admission changed OFF or manual scope");
  });
  run("byte-progress-does-not-invalidate-current-resource-admission",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(true,"");q.resource("");const auto token=q.start();q.claim(token);const auto captured=q.snapshot();check(q.progress(token,"steam-570","downloading",123,456),"valid byte progress accepted");check(q.resource("cpu-busy",95,0,token,&captured),"byte-only progress rejected a current admission");const auto paused=q.snapshot();check(paused["resourcePauseReason"]=="cpu-busy"&&paused["active"]["steam-570"]["bytes"]==123,"resource publication lost concurrent byte progress");check(q.resource("",0,0,token,&paused)&&q.allowed(token),"current session resource pause did not recover");
  });
  run("normalized-work-cannot-reuse-pre-normalization-status",[&](const fs::path& root){
    bool allFresh=true;
    for(const auto* variant:{"retired-popular","exhausted-budget","retired-catalog"}){Queue q(root/wide(variant));q.enqueue(Json::array({target(570)}));q.configure(true,"");q.resource("");const auto path=q.root()/L"config"/L"game-media-downloads.json",progress=q.root()/L"config"/L"game-media-download-progress.json";auto raw=read(path),oldStatus=read(progress);
      const std::string kind=variant;if(kind=="retired-popular"){raw["jobs"]["steam-570"]["catalogSource"]="popular";raw["jobs"]["steam-570"]["sourceTier"]=0;}else if(kind=="exhausted-budget")raw["jobs"]["steam-570"]["attempts"]=automaticAttemptLimit;else{raw["jobs"]=Json::object();raw["catalogPending"]=true;raw["catalogStatus"]="pending";oldStatus["catalogPending"]=true;oldStatus["catalogStatus"]="pending";}
      raw["unknownMigration"]={{"keep",42}};atomicWrite(path,raw);atomicWrite(progress,oldStatus);const auto stored=stormBytes(path),cached=stormBytes(progress);const auto canonical=q.document(),snapshot=q.snapshot();atomicWrite(q.root()/L"normalization-observed.json",{{"canonical",canonical},{"snapshot",snapshot},{"delay",q.delay()}});
      check(canonical["jobs"].empty()||canonical["jobs"]["steam-570"]["state"]=="unavailable","fixture does not actually normalize retired/exhausted work");allFresh=allFresh&&snapshot.value("remaining",size_t{1})==0&&!snapshot.value("catalogPending",true)&&q.delay()==-1;check(stormBytes(path)==stored&&stormBytes(progress)==cached,"pure normalized reads rewrote checkpoints");q.recheck();check(q.document()["unknownMigration"]==raw["unknownMigration"],"normalization repair discarded unknown migration data");check(!q.recheck(),"repeated migration repair did not become a no-op");
    }
    check(allFresh,"pre-normalization cached status still schedules removed or exhausted work");
    Queue mixed(root/L"mixed");mixed.enqueue(Json::array({target(570),target(571)}));mixed.configure(true,"");mixed.resource("");const auto token=mixed.start();const auto active=mixed.claim(token);check(active.value("appId",0)==570,"mixed normalization fixture claimed expected lane");check(mixed.progress(token,"steam-570","downloading",123,456),"mixed normalization byte progress");mixed.resumed(token,"steam-570",64);const auto prior=mixed.snapshot();const auto path=mixed.root()/L"config"/L"game-media-downloads.json";auto raw=read(path);raw["jobs"]["steam-571"]["catalogSource"]="popular";raw["jobs"]["steam-571"]["sourceTier"]=0;atomicWrite(path,raw);
    const auto current=mixed.snapshot();check(current.value("remaining",size_t{0})==1&&current["active"]["steam-570"]["bytes"]==123&&current["active"]["steam-570"]["resumedBytes"]==64&&current.value("statusRevision",uint64_t{0})==prior.value("statusRevision",uint64_t{0}),"normalization discarded current owned byte/resume progress or its revision");
    check(mixed.resource("cpu-busy",95,0,token,&current),"mixed normalized resource state published");const auto saved=mixed.snapshot();check(saved["active"]["steam-570"]["bytes"]==123&&saved["active"]["steam-570"]["resumedBytes"]==64&&!mixed.document()["jobs"].contains("steam-571"),"normalization save lost current progress or retained retired work");
  });
  run("normalized-empty-backlog-parks-without-resource-polling",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(true,"");q.resource("");const auto path=root/L"config"/L"game-media-downloads.json",progress=root/L"config"/L"game-media-download-progress.json";auto raw=read(path);raw["jobs"]["steam-570"]["catalogSource"]="popular";raw["jobs"]["steam-570"]["sourceTier"]=0;atomicWrite(path,raw);const auto stored=stormBytes(path),cached=stormBytes(progress);std::atomic<int> cpu{0},focus{0};PolicyObserveGuard observe;
    Json sample;{Supervisor owner(root,root/L"unlaunched.exe",Limits{5000,10000,20000,20,0,50},ResourcePorts{[]{return 1;},[&](uint64_t){++cpu;return std::optional<double>{0};},[&]{++focus;return false;}});owner.wake(true);check(waitUntil([&]{return policyWaitEntries.load()>0;},1500),"normalization owner reached wait");sample=policyParkedSample(3300);sample["cpuSamples"]=cpu.load();sample["focusProbes"]=focus.load();atomicWrite(root/L"normalized-owner-cost.json",sample);}
    check(cpu==0&&focus==0&&sample.value("lastOwnerWaitMs",DWORD{0})==INFINITE&&sample.value("additionalOwnerWaits",unsigned{1})==0,"normalized empty work still wakes or probes resources");check(stormBytes(path)==stored&&stormBytes(progress)==cached,"normalization-only idle owner rewrote checkpoints");
  });
  run("threaded-claims-and-progress-storm",[&](const fs::path& root){Queue q(root);Json inventory=Json::array();for(int n=1;n<=12;++n)inventory.push_back(target(n,n));q.enqueue(inventory);const auto token=q.start();
    std::mutex gate;std::vector<Json> claimed;std::exception_ptr failure;std::vector<std::thread> threads;
    for(int n=0;n<12;++n)threads.emplace_back([&]{try{auto row=q.claim(token);if(!row.is_null()){std::lock_guard lock(gate);claimed.push_back(row);}}catch(...){std::lock_guard lock(gate);failure=std::current_exception();}});
    for(auto& thread:threads)thread.join();if(failure)std::rethrow_exception(failure);
    check(claimed.size()==10,"concurrent claim storm respects original ten-slot cap");std::set<std::string> keys;for(const auto& row:claimed)keys.insert(row["key"]);check(keys.size()==10,"concurrent consumers never acquire one item twice");
    const auto status=root/L"config"/L"game-media-download-progress.json";
    for(int n=0;n<32;++n){const auto corrupt=std::string("{\"active\":")+std::to_string(n);{Lock lock(root);stormFile(status,corrupt);}check(!q.progress("old-owner","downloading",99,99),"stale owner rejected throughout corruption storm");check(q.progress(token,claimed[n%10]["key"],"downloading",n+1,100),"current owner heals each damaged progress publication");}
    check(q.document()["active"].size()==10,"progress repair never steals active ownership");
    q.configure(false,"");check(!q.allowed(token)&&!q.progress(token,"downloading",100,100),"OFF remains authoritative after repeated recovery");
  });
  run("temporary-corruption-and-future",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));const auto token=q.start();const auto path=q.temporaryRecordPath();
    const std::string broken=R"({"files":[]})";stormFile(path,broken);q.trackTemporary(token,std::string(32,'a')+".part",true);check(stormPreserved(path,broken),"damaged ownership original survives a new tracked file");
    auto future=read(path);future["schemaVersion"]=99;atomicWrite(path,future);const auto before=stormBytes(path);bool rejected=false;try{q.trackTemporary(token,std::string(32,'b')+".part",true);}catch(...){rejected=true;}check(rejected&&stormBytes(path)==before,"future ownership version cannot be downgraded");
  });
  run("corrupt-progress",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));const auto token=q.start();q.claim(token);
    const auto p=root/L"config"/L"game-media-download-progress.json";const std::string bad=R"({"active":[],"heartbeatAt":"bad"})";stormFile(p,bad);
    check(!q.snapshot().value("session",std::string{}).empty(),"status corruption falls back to durable state");
    check(q.progress(token,"downloading",4,8),"valid owner rebuilds corrupt progress");
    check(stormPreserved(p,bad),"progress corrupt bytes preserved");
    check(!q.progress("old-token","downloading",9,9),"old token remains fenced");
  });
  run("future-schema",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));
    const auto p=root/L"config"/L"game-media-downloads.json";auto j=read(p);j["schemaVersion"]=99;j["future"]={{"keep",true}};atomicWrite(p,j);const auto raw=stormBytes(p);
    check(q.start().empty(),"future queue never starts a worker");bool rejected=false;try{q.configure(false,"");}catch(...){rejected=true;}
    check(rejected&&stormBytes(p)==raw,"future queue refuses downgrade and remains byte exact");
    Queue resource(root/L"resource");resource.enqueue(Json::array({target(570)}));resource.resource("");const auto progress=resource.root()/L"config"/L"game-media-download-progress.json",durable=resource.root()/L"config"/L"game-media-downloads.json";auto next=read(progress);next["schemaVersion"]=99;atomicWrite(progress,next);const auto future=stormBytes(progress),saved=stormBytes(durable);bool refused=false;try{resource.resource("cpu-busy",95);}catch(...){refused=true;}check(refused&&stormBytes(progress)==future&&stormBytes(durable)==saved,"resource reuse overwrote a future status or its durable queue");
  });
  run("legal-unknown-debt-owner",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));auto t=q.start();q.claim(t);q.result(t,"steam-570",false,"fixture-error");
    q.update(t,[](Json& j){j["unknown"]={{"deep",Json::array({1,false,"keep"})}};j["jobs"]["steam-570"]["unknownJob"]=42;j["workerPid"]=GetCurrentProcessId();j["workerBirth"]=processBirthToken(GetCurrentProcessId());});
    const auto before=q.document();check(q.start().empty(),"live PID and birth cannot be stolen");q.configure(false,"");const auto after=Queue(root).document();
    check(after["unknown"]==before["unknown"]&&after["jobs"]==before["jobs"],"legal unknown fields and exact retry debt survive");
    check(!q.update("stale",[](Json& j){j["enabled"]=true;}),"stale owner cannot mutate durable state");
  });
  for(bool full:{false,true})run(full?"disk-full":"atomic-replace-failure",[&](const fs::path& root){
    Queue q(root);q.enqueue(Json::array({target(570)}));const auto p=root/L"config"/L"game-media-downloads.json";const auto raw=stormBytes(p);
    stormDiskFull=full;stormReplaceFailure=!full;bool rejected=false;try{q.configure(false,"");}catch(...){rejected=true;}stormDiskFull=false;stormReplaceFailure=false;
    check(rejected,"failed persistence must report failure");check(stormBytes(p)==raw,"failed save retains previous durable bytes");
    q.configure(false,"");check(!Queue(root).snapshot().value("enabled",true),"same request retries after storage fault clears");
    for(const auto& e:fs::directory_iterator(p.parent_path()))check(e.path().filename().wstring().find(L".tmp.")==std::wstring::npos,"failed save leaves no owned temporary write");
  });
  run("status-cannot-override-durable-off",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(false,"");const auto path=root/L"config"/L"game-media-download-progress.json";auto status=read(path);status["enabled"]=true;atomicWrite(path,status);
    check(!q.snapshot().value("enabled",true),"same-revision progress corruption cannot enable durable OFF");q.configure(false,"");check(!read(path).value("enabled",true),"no-op configuration heals contradicted status");
  });
  run("status-save-failure-retry",[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target(570)}));const auto p=root/L"config"/L"game-media-download-progress.json";
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"lock own progress file");
    bool failed=false;try{q.configure(false,"");}catch(...){failed=true;}CloseHandle(h);check(failed,"second publication failure reported");
    check(!q.snapshot().value("enabled",true),"old progress cannot re-enable durable OFF after partial commit");
    q.configure(false,"");check(!read(p).value("enabled",true),"same configuration repairs failed progress publication");
    Queue resource(root/L"resource");resource.enqueue(Json::array({target(570)}));resource.configure(true,"");resource.resource("");const auto progress=resource.root()/L"config"/L"game-media-download-progress.json";
    HANDLE busy=CreateFileW(progress.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(busy!=INVALID_HANDLE_VALUE,"lock own resource progress file");bool partial=false;try{resource.resource("cpu-busy",95);}catch(...){partial=true;}CloseHandle(busy);check(partial,"resource second publication failure reported");
    const auto accepted=resource.document();check(accepted.value("resourcePauseReason",std::string{})=="cpu-busy"&&read(progress).value("resourcePauseReason",std::string{}).empty(),"resource partial-commit fixture represents durable success and stale status");
    resource.resource("cpu-busy",95);const auto healed=read(progress);check(healed.value("resourcePauseReason",std::string{})=="cpu-busy"&&healed.value("queueRevision",uint64_t{0})==resource.document().value("statusRevision",uint64_t{0}),"same resource request failed to heal its stale progress publication");const auto stable=stormBytes(progress);resource.resource("cpu-busy",95);check(stormBytes(progress)==stable,"repeated healed resource state rewrote progress");
  });
  if(!only.empty()&&results.empty())throw std::runtime_error("requested storm case not found");
  const bool ok=std::all_of(results.begin(),results.end(),[](const auto& r){return r["status"]=="PASS";});
  Json report={{"ok",ok},{"cases",results},{"checks",checks},{"scope","original Queue load/consume/save/reread; isolated files; no processes or network"}};
  atomicWrite(base/L"results.json",report);return ok?0:1;
}

// Original Store/Queue/Authority/Server/Prewarmer and owned Supervisor process.
// Only Windows' power sample and the existing child workload are fixture IO.
static int batteryPauseProbe(const fs::path& base,const std::string& only={}){
  check(!fs::exists(base),"battery evidence root must be fresh");
  Json reports=Json::array(),notRun=Json::array();bool created=false;
  const auto run=[&](const char* name,auto fn){
    if(!only.empty()&&only!=name){notRun.push_back(name);return;}
    if(!created){fs::create_directories(base);created=true;}
    const auto before=checks;
    batteryFixtureSource(1);powerPolicy::configure(false);steamDemand::setForegroundReader({});
    try{fn(base/wide(name));reports.push_back({{"name",name},{"ok",true},{"checks",checks-before}});}
    catch(const std::exception& error){reports.push_back({{"name",name},{"ok",false},{"checks",checks-before},{"error",error.what()}});}
    powerPolicy::configure(false);
  };
  const auto video=[](const fs::path& root){fs::create_directories(root);const auto file=root/L"owned.mp4";std::ofstream out(file,std::ios::binary);const std::string bytes("\0\0\0\x18" "ftypisom",12);out.write(bytes.data(),bytes.size());out.close();return file;};
  run("queue-write-failure-keeps-authoritative-local-gate",[&](const auto& root){
    Queue queue(root);queue.enqueue(Json::array({target(711)}));const auto index=root/L"config"/L"game-media-downloads.json";const auto before=stormBytes(index);
    batteryFixtureSource(0);stormReplaceFailure=true;bool rejected=false;
    try{queue.configureWallpaperPower(true);}catch(const std::exception&){rejected=true;}stormReplaceFailure=false;
    check(rejected&&powerPolicy::enabled()&&powerPolicy::reason()=="wallpaper-on-battery","write failure cannot roll back the authoritative local DC gate");
    check(stormBytes(index)==before,"failed propagation retains original queue bytes");queue.snapshot();
    check(powerPolicy::enabled()&&powerPolicy::blocked(),"old durable OFF cannot reopen the authoritative process");
    queue.configureWallpaperPower(true);check(queue.document().value("wallpaperPauseOnBattery",false),"later explicit propagation repairs the durable preference");queue.configureWallpaperPower(false);
  });
  run("default-off-preserves-dc-and-unknown-preparation",[&](const auto& root){
    batteryFixtureSource(0);batteryPowerReads=0;Store store(root);auto request=target(700);request["demandConsumer"]="ymcc";
    check(store.begin(request).value("acquired",false),"OFF retains YMCC preparation on DC");check(batteryPowerReads==0,"OFF consumer does not sample DC power");batteryFixtureSource(-1);const auto readsAfterProducer=batteryPowerReads.load();
    check(store.begin(target(701)).value("acquired",false),"OFF retains prior unknown-power behavior");
    check(powerPolicy::reason().empty()&&batteryPowerReads==readsAfterProducer,"OFF does not sample power for the new policy");
  });
  run("real-dc-revokes-authority-claims-and-late-receipts",[&](const auto& root){
    powerPolicy::configure(true);ymcc::steamwallpaperForeground::Authority authority("battery-owner");
    ymcc::steamwallpaperForeground::Facts facts;facts.known=facts.physical=facts.enabled=facts.ready=true;
    facts.pid=123;facts.birth=456;facts.window=789;facts.steamPid=123;facts.steamBirth=456;facts.modeEpoch="mode-A";facts.power=1;
    steamDemand::setForegroundReader([&]{auto observed=facts;observed.enabled=powerPolicy::ticket().ac==1;return authority.observe(observed,authority.ticket());});
    const auto proof=steamDemand::foreground();auto request=target(702);request["demandConsumer"]="steam";request["demandEpoch"]=proof["ownerEpoch"];request["demandRevision"]=proof["revision"];
    Store store(root);auto lease=store.begin(request);check(lease.value("acquired",false),"AC and verified native proof grant a preparation lease");
    auto disk=store.snapshot();disk["entries"]["steam-702"]["attempts"]=2;disk["entries"]["steam-702"]["retryAt"]=123;atomicWrite(root/L"config"/L"game-media.json",disk);
    const auto source=video(root);batteryFixtureSource(0);const auto paused=steamDemand::foreground();
    check(!paused.value("current",true)&&paused["revision"]!=proof["revision"],"real DC revokes the verified native ticket");
    check(powerPolicy::reason()=="wallpaper-on-battery","DC reason matches UI contract");
    check(!store.begin(request).value("acquired",false)&&!store.begin(target(703)).value("acquired",false),"DC rejects Steam and YMCC preparation");
    const auto before=store.snapshot();store.fail(lease);store.finish(lease);
    bool rejected=false;try{store.publish(lease,source,false);}catch(const std::exception&){rejected=true;}
    check(rejected&&store.snapshot()==before,"DC blocks late commit/failure/completion without spending or resetting budgets");
    const auto legacy=store.importLegacy(target(704),root/L"absent.mp4",[]{return true;});check(legacy.value("reason",std::string{})=="wallpaper-on-battery","DC denies legacy before reading source files");
    store.cancel(lease);const auto cancelled=store.snapshot()["entries"]["steam-702"];
    check(cancelled.value("leaseUntil",int64_t{1})==0&&cancelled.value("attempts",0)==2&&cancelled.value("retryAt",int64_t{0})==123,"exact cancellation releases lease and retains budgets");
    batteryFixtureSource(1);const auto freshProof=steamDemand::foreground();
    check(freshProof.value("current",false)&&freshProof["revision"]!=proof["revision"]&&!steamDemand::allowed(request),"AC reopens fresh authority while old Steam ticket remains denied");
    auto fresh=request;fresh["demandEpoch"]=freshProof["ownerEpoch"];fresh["demandRevision"]=freshProof["revision"];
    const auto next=store.begin(fresh);check(next.value("acquired",false)&&next["taskId"]!=lease["taskId"],"AC reacquires fresh work under unchanged ON preference");
    auto late=lease;late["demandEpoch"]=freshProof["ownerEpoch"];late["demandRevision"]=freshProof["revision"];store.fail(late);store.finish(late);store.cancel(late);
    const auto current=store.snapshot()["entries"]["steam-702"];check(current["taskId"]==next["taskId"]&&current.value("attempts",0)==2&&current.value("retryAt",int64_t{0})==123,"late old work cannot charge or release new AC lease");
    steamDemand::setForegroundReader({});
  });
  run("unknown-power-is-not-ac-and-old-ymcc-work-stays-revoked",[&](const auto& root){
    powerPolicy::configure(true);Store store(root);auto request=target(705);request["demandConsumer"]="ymcc";const auto lease=store.begin(request);
    batteryFixtureSource(-1);check(powerPolicy::reason()=="wallpaper-power-unknown"&&!store.begin(target(706)).value("acquired",false),"255 waits for a real power sample");
    batteryFixtureSource(-3);check(powerPolicy::reason()=="wallpaper-power-unknown","failed Win32 power read is also unknown");
    batteryFixtureSource(1);const auto preAba=powerPolicy::ticket();batteryFixtureSource(0);batteryFixtureSource(1);check(!powerPolicy::current(preAba),"shared producer AC DC AC revokes old lease even without a consumer read on DC");const auto next=store.begin(request);check(next.value("acquired",false)&&next["taskId"]!=lease["taskId"],"fresh AC work replaces an invalidated YMCC lease");
    check(!powerPolicy::allowed(lease),"old YMCC callback stays invalid on AC");
    const auto before=store.snapshot();store.fail(lease);store.finish(lease);check(store.snapshot()==before,"old YMCC callback cannot alter the new owner");
    // Exercise the actual Worker with a retained owner context, not another policy model.
    Queue queued(root/L"worker-boundary");queued.enqueue(Json::array({target(712)}));queued.configure(true,"");queued.configureWallpaperPower(true);queued.resource("");
    const auto session=queued.start();check(!session.empty(),"known AC admits the original queued session");
    batteryFixtureSource(-1);check(!queued.allowed(session),"an existing queue rejects a readable unknown source");batteryFixtureSource(1);
    const auto parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,GetCurrentProcessId());check(parent!=nullptr,"retain actual parent handle for the worker boundary");
    struct ParentClose{HANDLE value;~ParentClose(){CloseHandle(value);}}parentClose{parent};
    auto context=std::make_shared<WorkerContext>(queued.root());Worker retained(queued.root(),session,parent,context);
    batteryFixtureSource(0);batteryFixtureSource(1); // No consumer observes the intermediate DC.
    check(retained.run()==0,"old worker returns through original graceful power pause");
    const auto paused=queued.document();atomicWrite(root/L"worker-fence-observation.json",{{"document",paused},{"sourceRevision",ymcc::powerSource::snapshot().revision}});
    check(paused.value("phase",std::string{})=="paused"&&paused.value("enabled",false),"missed DC cannot resume an old worker on final AC");
    const auto originalJob=paused["jobs"]["steam-712"];
    check(originalJob.value("attempts",0)==0&&originalJob.value("state",std::string{})=="pending","stale worker must not consume or charge the queued task");
    check(!queued.allowed(session)&&queued.claim(session).is_null(),"old session cannot regain admission or claim on final AC");
    check(!queued.progress(session,"late-progress",1,1),"old session cannot republish progress after returning ownership");
    queued.result(session,"steam-712",true);check(queued.document()["jobs"]["steam-712"]==originalJob,"late stale worker result cannot complete the task after AC");
  });
  run("cached-playback-and-prewarm-stop-without-cache-deletion",[&](const auto& root){
    Store store(root);const auto source=video(root);auto lease=store.begin(target(707));const auto entry=store.publish(lease,source,true);const auto asset=store.assetPath(entry["asset"]);
    Server server;const auto url=server.expose(store,entry);
    const auto get=[&](const std::string& value){const auto split=value.find('/',7),colon=value.find(':',7);const auto port=std::stoi(value.substr(colon+1,split-colon-1));SOCKET socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(socket!=INVALID_SOCKET,"owned loopback socket");sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<u_short>(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);check(connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"connect owned media server");const auto request="GET "+value.substr(split)+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\nConnection: close\r\n\r\n";send(socket,request.data(),int(request.size()),0);char response[2048]{};const auto size=recv(socket,response,sizeof response,0);closesocket(socket);return size>0?std::string(response,size):std::string{};};
    check(get(url).starts_with("HTTP/1.1 200"),"cached video served on AC");powerPolicy::configure(true);batteryFixtureSource(0);
    check(get(url).starts_with("HTTP/1.1 403"),"existing cached playback URL denied on DC");
    CachePrewarmer denied;denied.replace({{"denied",asset}});const auto deniedStats=denied.stats();check(deniedStats.bytes==0&&deniedStats.queued==0&&!deniedStats.active,"DC prewarm creates no read work");
    check(server.prewarm(store,Json::array({target(707)}))==0,"legacy server prewarm remains disabled");
    check(fs::exists(asset)&&fs::file_size(asset)==12,"cached video retained");batteryFixtureSource(1);
    check(get(url).starts_with("HTTP/1.1 403"),"AC does not reauthorize an old player URL");
    check(get(server.expose(store,entry)).starts_with("HTTP/1.1 200"),"fresh exposure resumes cached playback on AC");
    const auto streamed=root/L"stream-owned.mp4";{std::ofstream file(streamed,std::ios::binary);const std::string signature("\0\0\0\x18" "ftypisom",12);file.write(signature.data(),signature.size());file.seekp(32*1024*1024-1);file.put('x');}
    const auto streamEntry=store.publish(store.begin(target(710)),streamed,true);const auto streamUrl=server.expose(store,streamEntry);
    const auto pathAt=streamUrl.find('/',7),portAt=streamUrl.find(':',7);const auto port=std::stoi(streamUrl.substr(portAt+1,pathAt-portAt-1));
    SOCKET stream=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(stream!=INVALID_SOCKET,"owned range socket");int small=4096;DWORD timeout=1000;setsockopt(stream,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&small),sizeof small);setsockopt(stream,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<u_short>(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);check(connect(stream,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"connect active range stream");
    const auto range="GET "+streamUrl.substr(pathAt)+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\nRange: bytes=0-33554431\r\nConnection: close\r\n\r\n";send(stream,range.data(),int(range.size()),0);char buffer[8192]{};auto received=recv(stream,buffer,sizeof buffer,0);check(received>0&&std::string(buffer,received).starts_with("HTTP/1.1 206"),"actual range stream entered the send path");
    const auto cancelAt=GetTickCount64();batteryFixtureSource(0);powerPolicy::ticket();server.powerChanged();uint64_t receivedBytes=uint64_t(received);bool closed=false;
    while((received=recv(stream,buffer,sizeof buffer,0))>0)receivedBytes+=received;closed=received==0||WSAGetLastError()==WSAECONNRESET;closesocket(stream);
    check(closed&&GetTickCount64()-cancelAt<1000&&receivedBytes<1024*1024,"DC closes the actual active stream before reading/sending the full cached video");
    check(fs::file_size(store.assetPath(streamEntry["asset"]))==32*1024*1024,"active-stream cancellation retains cached bytes");
    atomicWrite(root/L"range-cancellation.json",{{"receivedBytes",receivedBytes},{"cachedBytes",32*1024*1024},{"cancelToCloseMs",GetTickCount64()-cancelAt},{"closed",closed}});batteryFixtureSource(1);powerPolicy::ticket();
    const auto large=root/L"prewarm-owned.bin";{std::ofstream file(large,std::ios::binary);file.seekp(1024*1024-1);file.put('x');}
    std::atomic<unsigned> reads{0};CachePrewarmer active([&]{if(++reads==2)batteryFixtureSource(0);return false;});active.replace({{"active",large}});
    check(waitUntil([&]{return active.stats().cancelled>0;},1000),"active prewarm observes DC and cancels");
    const auto stats=active.stats();check(!stats.active&&stats.completed==0&&stats.bytes<=65536,"active prewarm stops by next chunk and never records completion");
  });
  run("live-child-power",[&](const auto& root){
    Queue queue(root);queue.enqueue(Json::array({target(708)}));queue.configure(true,"");queue.configureWallpaperPower(true);PolicyOwnedChild child(root);atomicWrite(root/L"fixture.json",{{"mode","stall"},{"powerAc",1}});
    Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};},[]{return false;}});owner.wake(true);
    check(waitUntil([&]{return !queue.document().value("active",Json::object()).empty();},1500),"owned child claimed a live task");const auto started=read(root/L"started.json");check(started.value("wallpaperPauseOnBattery",false)&&started.value("powerReads",0u)>0,"separate child consumes durable ON and its own Windows power boundary");HANDLE exact=OpenProcess(SYNCHRONIZE,FALSE,started.value("pid",DWORD{0}));check(exact!=nullptr,"retain exact owned child handle");struct Close{HANDLE handle;~Close(){CloseHandle(handle);}}close{exact};
    const auto token=queue.document().value("session",std::string{});queue.update(token,[](Json& state){state["jobs"]["steam-708"]["attempts"]=1;state["jobs"]["steam-708"]["retryAt"]=123;state["jobs"]["steam-708"]["automaticElapsedMs"]=4321;});
    const auto part=root/L"cache"/L"jobs"/L"game-media"/L"12345678901234567890123456789012.part";stormFile(part,"owned resumable bytes");auto metadata=part;metadata.replace_extension(L".resume.json");atomicWrite(metadata,{{"resumeProtocol",1},{"validator","\"owned-etag\""}});queue.trackTemporary(token,utf8(part.filename().wstring()));
    batteryFixtureSource(0);powerPolicy::ticket();notifyOwner(root);
    check(waitUntil([&]{return WaitForSingleObject(exact,0)==WAIT_OBJECT_0;},2500),"DC actually terminates the owned active process");
    check(waitUntil([&]{return queue.document().value("phase",std::string{})=="paused";},1000),"native owner records a policy pause");
    const auto paused=queue.document();const auto job=paused["jobs"]["steam-708"];
    check(paused.value("enabled",false)&&paused.value("active",Json::object()).empty()&&job.value("state",std::string{})=="pending","pause retains ON and retires live lanes");
    check(job.value("attempts",0)==1&&job.value("retryAt",int64_t{0})==123&&job.value("automaticElapsedMs",int64_t{0})==4321,"DC kill neither spends nor refunds retry/time budget");
    check(fs::exists(part)&&stormBytes(part)=="owned resumable bytes"&&fs::exists(metadata),"DC cancellation retains partial bytes and resume sidecar");
    check(queue.claim(token).is_null(),"DC cannot claim another queued task");queue.result(token,"steam-708",false,"late-callback");
    check(queue.document()["jobs"]["steam-708"]==job,"late DC worker result cannot change retained job");
    batteryFixtureSource(1);powerPolicy::ticket();queue.result(token,"steam-708",false,"late-after-ac");check(queue.document()["jobs"]["steam-708"]==job,"terminal old callback remains denied after AC");
    atomicWrite(root/L"fixture.json",{{"mode","complete"},{"powerAc",1}});notifyOwner(root);
    check(waitUntil([&]{return queue.document()["jobs"]["steam-708"].value("state",std::string{})=="done";},5000),"AC resumes original ON with a fresh worker");
    check(fs::exists(part)&&stormBytes(part)=="owned resumable bytes"&&fs::exists(metadata),"AC restart keeps the existing resumable partial");
    atomicWrite(root/L"paused-state.json",paused);
  });
  if(!only.empty()&&reports.empty())throw std::runtime_error("requested battery case not found");
  batteryFixtureSource(-2);powerPolicy::configure(false);steamDemand::setForegroundReader({});
  const bool ok=std::all_of(reports.begin(),reports.end(),[](const Json& row){return row.value("ok",false);});
  Json result={{"status",ok?"PASS":"FAIL"},{"checks",checks},{"cases",reports.size()},{"results",reports},{"scope","Actual native media consumers, explicit Win32 power IO, owned files/loopback and owned child process; installed apps and hardware untouched"}};
  if(!only.empty()){result["selectedCase"]=only;result["completeRegression"]=false;result["notRun"]=notRun;}
  atomicWrite(base/L"battery-pause-result.json",result);std::cout<<result.dump()<<std::endl;return ok?0:1;
}

int wmain(int argc, wchar_t **argv) {
  try {
    if(argc>1&&std::wstring_view(argv[1])==L"--battery-pause"){
      if(argc!=3)throw std::runtime_error("battery-pause requires exactly one evidence root");
      return batteryPauseProbe(fs::absolute(argv[2]));
    }
    if(argc>1&&std::wstring_view(argv[1])==L"--battery-pause-case"){
      if(argc!=4||!argv[3][0])throw std::runtime_error("battery-pause-case requires an evidence root and exact nonempty case");
      return batteryPauseProbe(fs::absolute(argv[2]),utf8(argv[3]));
    }
    if(argc==4&&std::wstring_view(argv[1])==L"--storm-case")return queueStorm(fs::absolute(argv[2]),utf8(argv[3]));
    if(argc==4&&std::wstring(argv[1])==L"--resource-cost")return queueStorm(fs::absolute(argv[2]),"resource-publication-fixed-inventory-cost",(std::clamp)(_wtoi(argv[3]),6,512)&~1);
    if(argc==3&&std::wstring_view(argv[1])==L"--storm")return queueStorm(fs::absolute(argv[2]));
    if (argc > 1 && std::wstring_view(argv[1]) == L"--media-background")
      return fakeChild(argc, argv);
    const auto base = fs::temp_directory_path() /
                      wide("YMCC-MediaDownloads-Selftest-" + randomId());
    fs::create_directories(base);
    struct Cleanup {
      fs::path root;
      ~Cleanup() {
        std::error_code ec;
        if (fs::absolute(root).parent_path() ==
                fs::absolute(fs::temp_directory_path()) &&
            root.filename().wstring().starts_with(
                L"YMCC-MediaDownloads-Selftest-"))
          fs::remove_all(root, ec);
      }
    } cleanup{base};
    Queue q(base / L"queue");
    check(q.snapshot().value("enabled", false),
          "background download defaults on");
    q.enqueue(Json::array(
        {target(1, 100), target(2, 500), target(3, 200), target(4)}));
    check(q.snapshot()["total"] == 4, "queue persists inventory");
    q.enqueue(Json::array({target(2, 700), target(1, 50)}));
    check(q.snapshot()["total"] == 4, "dedup keeps one job per game");
    auto token = q.start();
    auto first = q.claim(token);
    check(first["appId"] == 2, "newest played missing game is first");
    const auto diskStamp = fs::last_write_time(q.root() / L"config" /
                                               L"game-media-downloads.json");
    check(q.progress(token, "downloading", 100, 200),
          "active owner reports bytes");
    check(fs::last_write_time(q.root() / L"config" /
                              L"game-media-downloads.json") == diskStamp,
          "byte progress does not rewrite durable queue");
    check(q.snapshot()["bytes"] == 100 && q.snapshot()["totalBytes"] == 200,
          "small status visible to both UIs");
    check(!q.progress("old-owner", "downloading", 999, 999),
          "stale owner cannot write progress");
    q.configure(false, "");
    check(!q.allowed(token) && !q.progress(token, "downloading", 150, 200),
          "switch off cancels live work");
    check(q.delay() < 0, "disabled owner has no wake timer");
    q.configure(true, "");
    q.result(token, first["key"], true);
    auto second = q.claim(token);
    check(second["appId"] == 3, "next recent game before unplayed");
    q.recover(token, "worker-stalled");
    auto document = q.document();
    check(document["jobs"]["steam-3"]["attempts"] == 1,
          "crash recovery records attempt");
    check(document["jobs"]["steam-3"]["retryAt"].get<int64_t>() > now() + 19000,
          "crash uses a backoff not immediate respawn");
    q.result(token, "steam-3", false, "again");
    q.result(token, "steam-3", false, "again");
    check(q.document()["jobs"]["steam-3"].value("autoStopped",false),
          "repeated failure stops that item after its finite budget");
    q.configure(true, "game-running");
    check(q.start().empty() && q.delay() < 0,
          "game policy pause retains jobs and exits");
    q.configure(true, "");
    check(Queue(q.root()).snapshot()["total"] == 4,
          "new process sees same durable queue");
    check(std::string(expiry(89999, 0, 0, 0)).empty(), "stall not early");
    check(std::string(expiry(90000, 0, 0, 0)) == "worker-stalled",
          "90 second actual stall boundary");
    check(std::string(expiry(240001, 0, 239999, 1)) == "task-timeout",
          "4 minute task boundary despite progress");
    check(std::string(expiry(900000, 0, 899999, 899999)) == "session-timeout",
          "15 minute session boundary");
    check(sourceGameMatches(L"/store_trailers/730/master.m3u8","steam-730"),"matching modern trailer owner accepted");
    check(!sourceGameMatches(L"/store_trailers/570/master.m3u8","steam-730"),"different game trailer owner rejected");
    check(!sourceGameMatches(L"/%73tore_trailers/570/master.m3u8","steam-730"),"encoded path cannot hide another game owner");
    check(!sourceGameMatches(L"/store_trailers/%35%37%30/master.m3u8","steam-730"),"encoded AppID cannot hide another game owner");
    check(!sourceGameMatches(L"/store_trailers/730/../../store_trailers/570/master.m3u8","steam-730"),"parent segments cannot escape to another game");
    check(sourceGameMatches(L"/store_trailers/730/480/../init.mp4","steam-730"),"same-game relative path remains valid");
    check(sourceGameMatches(L"/steam/apps/256972298/movie480.webm","steam-730"),"legacy movie ID path keeps metadata-based ownership");
    check(sourceGameMatches(L"/api/appdetails","steam-730"),"metadata endpoint remains valid");
    check(!sourceGameMatches(L"/store_trailers/570/master.m3u8","steam-578080"),"similar numeric game IDs never match by prefix");
    auto plan = hlsPlan("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=800000,"
                        "RESOLUTION=1280x720\n720.m3u8\n#EXT-X-STREAM-INF:"
                        "BANDWIDTH=400000,RESOLUTION=854x480\n480.m3u8\n",
                        "https://video.steamstatic.com/app/master.m3u8");
    check(plan.variant.ends_with("720.m3u8"), "HLS selects the highest available variant up to 1080p");
    plan = hlsPlan("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\npart."
                   "m4s\n#EXT-X-ENDLIST\n",
                   "https://video.steamstatic.com/app/index.m3u8");
    check(plan.segments.size() == 2 && plan.segments[0].ends_with("init.mp4"),
          "HLS stores init and media bytes offline");
    for (const auto &text :
         {std::string{"#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\nseg.m4s\n"},
          std::string{
              "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n#EXT-X-ENDLIST\n"}}) {
      bool denied = false;
      try {
        hlsPlan(text, "https://video.steamstatic.com/a.m3u8");
      } catch (...) {
        denied = true;
      }
      check(denied, "live or encrypted stream rejected");
    }
    for (const auto *url : {"https://evil.invalid/a.mp4",
                            "https://steamstatic.com.evil.invalid/a.mp4",
                            "https://user:pass@video.steamstatic.com/a.mp4",
                            "https://video.steamstatic.com:444/a.mp4",
                            "http://video.steamstatic.com/a.mp4"}) {
      bool denied = false;
      try {
        sourceUrl(url);
      } catch (...) {
        denied = true;
      }
      check(denied, "untrusted media source denied");
    }
    CpuGate gate;
    check(std::string(gate.observe(std::nullopt, 15, 1000)) == "cpu-unknown",
          "unknown CPU never allows a download");
    check(std::string(gate.observe(5.0, 15, 2000)).empty(),
          "normal load permits downloads");
    check(std::string(gate.observe(16.0, 15, 3000)) == "cpu-busy",
          "default 15 percent boundary pauses");
    check(std::string(gate.observe(14.5, 15, 4000)) == "cpu-busy",
          "hysteresis prevents threshold flapping");
    check(std::string(gate.observe(13.0, 15, 5000)) == "cpu-recovering",
          "calm period begins below the proportional recovery margin");
    check(std::string(gate.observe(13.0, 15, 14999)) == "cpu-recovering",
          "cannot resume before ten calm seconds");
    check(std::string(gate.observe(13.0, 15, 15000)).empty(),
          "resumes after ten calm seconds");
    check(std::string(gate.observe(25.0, 30, 21000)).empty(),
          "user threshold is configurable");
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    const auto original = custom_steam_library::configuredDataRoot();
    for (const auto *mode : {"stall", "crash", "preclaim-crash", "unsupported",
                             "complete", "production"}) {
      const auto root = base / wide(mode);
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      atomicWrite(root / L"fixture.json", {{"mode", mode}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      fs::copy_file(self, root / L"CustomSteamLibrary.exe");
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundMediaProtocol",
                    std::string(mode) == "unsupported" ? 0 : 1}});
      if (std::string(mode) == "production") {
        Store store(root);
        auto lease = store.begin(target(570));
        auto video = root / L"fixture.mp4", poster = root / L"fixture.jpg";
        {
          std::ofstream out(video, std::ios::binary);
          out.write("\0\0\0\x18"
                    "ftypisom-fixture",
                    20);
        }
        {
          std::ofstream out(poster, std::ios::binary);
          out.write("\xff\xd8\xff"
                    "fixture",
                    11);
        }
        // This case tests an already-current complete cache; legacy quality
        // migration is covered by shared_game_media_quality_selftest.cpp.
        lease["videoQualityPolicy"] = videoQualityPolicy;
        lease = store.publish(lease, video, false);
        lease = store.publish(lease, poster, false);
        store.finish(lease);
      }
      {
        Supervisor owner(
            root, root / L"CustomSteamLibrary.exe",
            Limits{250, 1200, 2000, 50, 0},
            ResourcePorts{[] { return 1; },
                          [](uint64_t) { return std::optional<double>{0.0}; }});
        owner.wake(false);
        Sleep(50);
        check(!fs::exists(root / L"started.json"),
              "unarmed inventory event cannot start a helper before settings "
              "load");
        owner.queue().configure(true, "");
        owner.wake(true);
        if (std::string(mode) == "production" ||
            std::string(mode) == "complete") {
          check(waitUntil([&] {
                  return queue.snapshot().value("phase", std::string{}) ==
                         "completed";
                }),
                "completed worker self exits");
          check(queue.snapshot()["workerPid"] == 0 &&
                    queue.snapshot()["remaining"] == 0,
                "no process parked after completion");
        } else {
          check(waitUntil([&] {
                  return queue.document()["jobs"]["steam-570"].value("attempts",0)>0 || queue.snapshot().value("reason",std::string{})=="download-worker-upgrade-required";
                }),
                "crashed or stalled process is recovered");
          if (std::string(mode) == "unsupported") {
            check(!fs::exists(root / L"started.json"),
                  "old library cannot open a window for an unknown flag");
            continue;
          }
          auto started = read(root / L"started.json");
          const auto pid = started.value("pid", DWORD{0});
          HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
          check(!process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0,
                "only owned stalled child terminated");
          if (process)
            CloseHandle(process);
          check(queue.delay() > 19000,
                "recovery leaves process exited during backoff");
        }
      }
    }
    // The startup gate is independent of user settings and prevents even
    // CPU reads or process startup during its mandatory interval.
    {
      const auto root = base / L"startup";
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      std::atomic<int> reads{0};
      Supervisor owner(root, root / L"missing.exe",
                       Limits{5000, 10000, 20000, 50, 600000},
                       ResourcePorts{[] { return 1; },
                                     [&](uint64_t) {
                                       ++reads;
                                       return std::optional<double>{0.0};
                                     }});
      owner.queue().configure(true, "");
      owner.wake(true);
      check(waitUntil([&] {
              return queue.snapshot().value("resourcePauseReason",
                                            std::string{}) ==
                     "startup-protection";
            }),
            "mandatory startup protection precedes work");
      check(reads.load() == 0 &&
                queue.snapshot().value("workerPid", DWORD{0}) == 0,
            "boot protection does not sample CPU or launch a helper");
    }
    // Expiry must publish independently of queued jobs or a second command.
    // Accelerate only the injected test limit; production remains 30000 ms.
    {
      const auto root=base/L"startup-expiry-empty";
      std::atomic<int> reads{0};std::atomic<bool> protectedEvent{false},releasedEvent{false};
      const auto startup=GetTickCount64()-processStartedTick+350;
      Controller controller(root,root/L"missing.exe",[&](const Json& state){
        if(state.value("resourcePauseReason",std::string{})=="startup-protection")protectedEvent=true;
        else if(protectedEvent.load())releasedEvent=true;
      },Limits{5000,10000,20000,50,startup},ResourcePorts{[]{return 1;},[&](uint64_t){++reads;return std::optional<double>{0.0};}});
      controller.background({{"enabled",true},{"pauseReason","game-running"}});
      check(waitUntil([&]{return protectedEvent.load();}),"empty queue publishes startup protection");
      auto state=controller.snapshot();
      check(state.value("startupProtectionUntil",int64_t{0})>now(),"native startup deadline is exported");
      check(waitUntil([&]{return releasedEvent.load();}),"deadline publishes release without queued work or a command");
      state=controller.snapshot();
      check(state.value("resourcePauseReason",std::string{}).empty()&&state.value("startupProtectionUntil",int64_t{-1})==0,"deadline clears native protection and its metadata");
      check(state.value("pauseReason",std::string{})=="game-running"&&reads.load()==0&&state.value("workerPid",DWORD{0})==0,"empty startup recovery preserves game admission and performs no CPU sampling or launch");
      check(state["admissionHistory"].size()>=2,"export retains startup transition evidence");
      const auto revision=state.value("statusRevision",uint64_t{0});Sleep(80);
      check(controller.snapshot().value("statusRevision",uint64_t{0})==revision,"idle protection release does not rewrite snapshots repeatedly");
      Queue queue(root);
      for(int n=0;n<30;++n)queue.configure(true,n%2?"game-running":"");
      check(queue.snapshot()["admissionHistory"].size()==12,"admission evidence stays bounded");
      const auto before=queue.snapshot().value("statusRevision",uint64_t{0});queue.configure(true,"game-running");queue.resource("");
      check(queue.snapshot().value("statusRevision",uint64_t{0})==before,"duplicate policy and resource checks remain no-ops");
    }
    for(const bool disabled:{false,true}) {
      const auto root=base/(disabled?L"startup-expiry-disabled":L"startup-expiry-queued");
      Queue queue(root);queue.enqueue(Json::array({target(570)}));
      atomicWrite(root/L"fixture.json",{{"mode","complete"}});
      fs::copy_file(self,root/L"CustomSteamLibrary.exe");
      atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
      std::atomic<int> reads{0};const auto startup=GetTickCount64()-processStartedTick+350;
      Controller controller(root,root/L"CustomSteamLibrary.exe",[](const Json&){},
        Limits{5000,10000,20000,50,startup},ResourcePorts{[]{return 1;},[&](uint64_t){++reads;return std::optional<double>{0.0};}});
      controller.background({{"enabled",true},{"pauseReason",""}});
      check(waitUntil([&]{return controller.snapshot().value("resourcePauseReason",std::string{})=="startup-protection";}),"retained queue publishes protection");
      check(!fs::exists(root/L"started.json")&&reads.load()==0,"retained jobs cannot bypass protection");
      if(disabled){
        controller.background({{"enabled",false},{"pauseReason",""}});
        check(waitUntil([&]{return controller.snapshot().value("resourcePauseReason",std::string{})!="startup-protection";}),"disabled status also releases expired startup label");
        check(!fs::exists(root/L"started.json")&&reads.load()==0&&!controller.snapshot().value("enabled",true),"deadline never overrides saved OFF or launches a worker");
      }else{
        check(waitUntil([&]{return controller.snapshot().value("phase",std::string{})=="completed";}),"retained queue starts automatically after deadline without another UI command");
        check(controller.snapshot().value("remaining",size_t{1})==0&&controller.snapshot().value("workerPid",DWORD{0})==0,"deadline-triggered worker completes and exits");
      }
    }
    // Independent process handles verify a CPU/battery pause actually ends
    // the running helper and leaves no helper while blocked.
    for (const auto *mode : {"battery-pause", "cpu-pause"}) {
      const auto root = base / wide(mode);
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      atomicWrite(root / L"fixture.json", {{"mode", "stall"}});
      fs::copy_file(self, root / L"CustomSteamLibrary.exe");
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundMediaProtocol", 1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      std::atomic<int> ac{1};
      std::atomic<double> cpu{0};
      Supervisor owner(root, root / L"CustomSteamLibrary.exe",
                       Limits{5000, 10000, 20000, 50, 0},
                       ResourcePorts{[&] { return ac.load(); },
                                     [&](uint64_t) {
                                       return std::optional<double>{cpu.load()};
                                     }});
      owner.queue().configure(true, "");
      owner.wake(true);
      check(waitUntil([&] { return fs::exists(root / L"started.json"); }),
            "resource fixture starts an owned helper");
      auto info = read(root / L"started.json");
      HANDLE process =
          OpenProcess(SYNCHRONIZE, FALSE, info.value("pid", DWORD{0}));
      check(process != nullptr, "retain exact running helper identity");
      if (std::string(mode) == "battery-pause")
        ac.store(0);
      else
        cpu.store(25);
      owner.wake();
      check(waitUntil([&] {
              return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
            }),
            "power or CPU pressure ends running helper");
      CloseHandle(process);
      check(queue.snapshot().value("resourcePauseReason", std::string{}) ==
                (std::string(mode) == "battery-pause" ? "on-battery"
                                                      : "cpu-busy"),
            "shared progress explains resource pause");
      check(queue.snapshot().value("workerPid", DWORD{0}) == 0,
            "no worker parked during resource pause");
    }
    for(const bool battery:{false,true}){
      const auto root=base/(battery?L"foreground-battery":L"foreground-cpu");
      Queue queue(root);queue.enqueue(Json::array({target(570)}));
      atomicWrite(root/L"fixture.json",{{"mode","stall"}});fs::copy_file(self,root/L"CustomSteamLibrary.exe");
      atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
      std::atomic<bool> foreground{false};std::atomic<int> ac{1},reads{0};
      Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{5000,10000,20000,50,0,50},
          ResourcePorts{[&]{return ac.load();},[&](uint64_t){++reads;return std::optional<double>{75.0};},[&]{return foreground.load();}});
      owner.wake(true);
      check(waitUntil([&]{return queue.snapshot().value("resourcePauseReason",std::string{})=="cpu-busy";}),"background CPU gate still pauses above fifteen percent");
      check(!fs::exists(root/L"started.json"),"background busy CPU cannot launch a helper");
      foreground=true;owner.wake();
      check(waitUntil([&]{return fs::exists(root/L"started.json");}),"foreground resumes without the ten second CPU recovery wait");
      const auto samples=reads.load();Sleep(120);
      check(reads.load()==samples&&queue.snapshot().value("resourcePauseReason",std::string{}).empty(),"foreground bypass does not keep sampling CPU");
      const auto pid=read(root/L"started.json").value("pid",DWORD{0});HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);check(process!=nullptr,"retain exact foreground child handle");
      if(battery)ac=0;else foreground=false;owner.wake();
      check(waitUntil([&]{return WaitForSingleObject(process,0)==WAIT_OBJECT_0;}),"losing focus or AC stops the exact running helper");CloseHandle(process);
      check(queue.snapshot().value("resourcePauseReason",std::string{})==(battery?"on-battery":"cpu-busy"),"foreground never overrides AC and background restores CPU admission");
    }
    {
      const auto root=base/L"completed-idle";Queue queue(root);queue.enqueue(Json::array({target(570)}));
      const auto token=queue.start();auto job=queue.claim(token);queue.result(token,job["key"],true);queue.end(token,"completed");
      std::atomic<int> reads{0},foregroundChecks{0};
      Supervisor owner(root,root/L"missing.exe",Limits{5000,10000,20000,20,0,20},ResourcePorts{[]{return 1;},[&](uint64_t){++reads;return std::optional<double>{75.0};},[&]{++foregroundChecks;return false;}});
      const auto revision=queue.snapshot().value("statusRevision",uint64_t{0});owner.wake(true);Sleep(180);
      check(reads==0&&foregroundChecks==0&&queue.snapshot().value("statusRevision",uint64_t{0})==revision,"completed wallpaper queue sleeps without CPU checks, focus probes or rewrites");
      check(queue.snapshot().value("workerPid",DWORD{0})==0,"completed wallpaper queue never launches a worker");
      const auto ticks=[](){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("idle-cpu-metrics-unavailable");ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;};
      IO_COUNTERS before{},after{};check(GetProcessIoCounters(GetCurrentProcess(),&before)!=FALSE,"idle IO metrics available");
      const auto started=GetTickCount64(),cpu=ticks();Sleep(1000);const auto elapsed=GetTickCount64()-started,used=ticks()-cpu;
      check(GetProcessIoCounters(GetCurrentProcess(),&after)!=FALSE,"idle IO metrics complete");
      const Json metric={{"elapsedMs",elapsed},{"cpuMs",double(used)/10000.0},{"readOperations",after.ReadOperationCount-before.ReadOperationCount},{"writeOperations",after.WriteOperationCount-before.WriteOperationCount},{"readBytes",after.ReadTransferCount-before.ReadTransferCount},{"writtenBytes",after.WriteTransferCount-before.WriteTransferCount},{"cpuSamples",reads.load()},{"foregroundChecks",foregroundChecks.load()},{"scope","one second of the original completed-queue parked state; isolated process only"}};
      completedIdleMetrics=metric;
      check(after.ReadOperationCount==before.ReadOperationCount&&after.WriteOperationCount==before.WriteOperationCount&&reads==0&&foregroundChecks==0,"completed wallpaper queue performed resident file or resource polling");
    }
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                            original.empty() ? nullptr : original.c_str());
    std::cout << Json({{"ok", true},
                       {"checks", checks},
                       {"completedIdle", completedIdleMetrics},
                       {"scope",
                        "production durable queue/HLS/source rules, real "
                        "isolated child completion/crash/stall processes; no "
                        "Steam, devices, input or external network"}})
                     .dump()
              << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
