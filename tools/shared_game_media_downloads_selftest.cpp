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
static std::atomic<unsigned> deniedNetworkAttempts{0};
static HINTERNET WINAPI deniedNetwork(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){deniedNetworkAttempts.fetch_add(1);SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#include "media_download_transfer_fixture.h"
#define WinHttpOpen mediaTransferFixture::open
#define WinHttpConnect mediaTransferFixture::connect
#define WinHttpOpenRequest mediaTransferFixture::openRequest
#define WinHttpCloseHandle mediaTransferFixture::close
#define WinHttpSetTimeouts mediaTransferFixture::timeouts
#define WinHttpSetOption mediaTransferFixture::option
#define WinHttpAddRequestHeaders mediaTransferFixture::headers
#define WinHttpSendRequest mediaTransferFixture::send
#define WinHttpReceiveResponse mediaTransferFixture::receive
#define WinHttpQueryHeaders mediaTransferFixture::query
#define WinHttpReadData mediaTransferFixture::read
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
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpCloseHandle
#undef WinHttpSetTimeouts
#undef WinHttpSetOption
#undef WinHttpAddRequestHeaders
#undef WinHttpSendRequest
#undef WinHttpReceiveResponse
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef MoveFileExW
#undef WriteFile
#undef CreateMutexW
#undef WaitForSingleObject
#undef WaitForMultipleObjects
#include <iostream>
#include "shared_game_media_sleep_fixture.h"
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
  if(argc!=7&&argc!=13)throw std::runtime_error("media child arguments missing public power map");
  const auto root = custom_steam_library::configuredDataRoot();
  Queue queue(root);
  const auto token = utf8(argv[4]);
  const auto fixture=read(root/L"fixture.json");
  if(fixture.contains("powerAc"))batteryFixtureSource(fixture["powerAc"].get<int>());
  auto mode = fixture.value("mode", std::string{});
  if (mode == "production") {
    return backgroundEntrypoint(root).value_or(99);
  }
  const auto publicSleep=mediaSleepFixture::child(argc,argv);
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
    atomicWrite(root/L"package-manifest.json",{{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",videoQualityPolicy},{"backgroundManualProtocol",1},{"backgroundVideoSelectionProtocol",1}});atomicWrite(root/L"fixture.json",{{"mode","complete"}});SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
  }
  ~PolicyOwnedChild(){SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",previous.empty()?nullptr:previous.c_str());}
};
static bool stormPreserved(const fs::path& p,const std::string& text){
  if(fs::is_regular_file(p)&&stormBytes(p)==text)return true;
  for(const auto& e:fs::directory_iterator(p.parent_path()))
    if(e.path().filename().wstring().starts_with(p.filename().wstring()+L".corrupt.")&&stormBytes(e.path())==text)return true;
  return false;
}

// Each selected case restores the existing owner state; default storm execution
// must not inherit a synthetic settings owner from another test.
struct MediaPreferenceFixtureState {
 bool owner,pause,receipt;int ac;uint64_t rev,source;
 MediaPreferenceFixtureState(){std::lock_guard lock(powerPolicy::mutex);owner=powerPolicy::preferenceOwner;pause=powerPolicy::pauseOnBattery;receipt=powerPolicy::hasSourceReceipt;ac=powerPolicy::observedAc;rev=powerPolicy::revision;source=powerPolicy::observedSourceRevision;powerPolicy::preferenceOwner=false;powerPolicy::pauseOnBattery=false;}
 ~MediaPreferenceFixtureState(){std::lock_guard lock(powerPolicy::mutex);powerPolicy::preferenceOwner=owner;powerPolicy::pauseOnBattery=pause;powerPolicy::hasSourceReceipt=receipt;powerPolicy::observedAc=ac;powerPolicy::revision=rev;powerPolicy::observedSourceRevision=source;}
};

struct LauncherWallpaperIdentityFixture {
      fs::path root,primary,launcher;Store store;Json scan;
      explicit LauncherWallpaperIdentityFixture(const fs::path& value):root(value),primary(value/L"Game"/L"Stalker2"/L"Binaries"/L"Win64"/L"Stalker2-Win64-Shipping.exe"),launcher(value/L"Game"/L"Stalker2.exe"),store(value){
        stormFile(primary,"owned primary bytes, never executable");stormFile(launcher,"owned launcher bytes, never executable");
        Json game={{"status","ready"},{"contentType","game"},{"primaryRole","main-game"},{"scanComplete",true},{"uniquePrimaryInvariant",true},{"candidateLimitReached",false},{"unrecognizedTool",false},{"gameDirectory",utf8(launcher.parent_path().wstring())},{"primaryExecutable",utf8(primary.wstring())}};
        game["candidates"]=Json::array({Json{{"path",utf8(primary.wstring())},{"role","main-game"},{"selected",true}},Json{{"path",utf8(launcher.wstring())},{"role","launcher"},{"selected",false}}});
        scan={{"schemaVersion",1},{"games",Json::array({game})}};
        atomicWrite(root/L"state"/L"library-scan.json",scan);
      }
      void identify(const fs::path& executable,int id=1643320){
        const auto manifest=root/L"observed.json";atomicWrite(manifest,{{"exe",utf8(executable.wstring())},{"match",{{"appId",id},{"formalName","Owned Stalker"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}}}});
        check(store.rememberIdentity(utf8(executable.wstring()),manifest,now()).value("verified",false),"owned official cache setup");
      }
      Json shortcut()const{return {{"appId",0},{"verified",false},{"executable",utf8(launcher.wstring())},{"name","S.T.A.L.K.E.R. 2: Cost of Hope"},{"catalogSource","steam-shortcut"},{"steamAccount","89250578"},{"shortcutAppId","4279376404"},{"sourceTier",2}};}
      Json inventory(){for(const auto& value:store.inventory(Json::array({shortcut()})))if(stringField(value,"executable")==utf8(launcher.wstring()))return value;throw std::runtime_error("launcher missing from actual inventory");}
      void revoke(){auto value=scan;value["games"][0]["scanComplete"]=false;atomicWrite(root/L"state"/L"library-scan.json",value);}
      void stopped(){Queue queue(root);queue.enqueue(Json::array({shortcut()}));auto value=queue.document();check(value["jobs"].size()==1,"owned pending shortcut setup");auto& job=value["jobs"].begin().value();job["state"]="unavailable";job["autoStopped"]=true;job["attempts"]=3;job["lastError"]="media-identity-unconfirmed";job["stopReason"]="automatic-attempt-budget-exhausted";atomicWrite(root/L"config"/L"game-media-downloads.json",value);}
    };

static void workerTransferTimeoutObservation(const fs::path& root);
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


  run("launcher-wallpaper-admission-current",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(1);Json rows=Json::array();
    const auto probe=[&](const char* name,const std::string& change,bool stable){
      try{
        LauncherWallpaperIdentityFixture f(root/wide(name));f.identify(f.primary);
        if(change=="exact"||change=="denied")f.identify(f.launcher);
        Queue queue(f.root);queue.enqueue(Json::array({target(570)}));const auto untouched=queue.document()["jobs"]["steam-570"];
        const auto input=f.inventory();const auto file=f.root/L"config"/L"game-media-downloads.json",progress=f.root/L"config"/L"game-media-download-progress.json";
        const auto before=stormBytes(file),beforeProgress=stormBytes(progress);unsigned crossed=0;HANDLE held=INVALID_HANDLE_VALUE;
        struct Close{HANDLE& handle;~Close(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} close{held};
        admissionWaits=0;admissionInterleave=[&]{
          ++crossed;
          if(change=="revoke")f.revoke();
          else if(change=="primary")f.identify(f.primary,555);
          else if(change=="override"||change=="exact")f.identify(f.launcher,555);
          else if(change=="excluded")atomicWrite(f.root/L"config"/L"library-config.json",{{"manualBuckets",{{utf8(f.launcher.wstring()),{{"bucket","non-game"}}}}}});
          else if(change=="denied"){
            const auto cache=custom_steam_library::identity_cache::cachePath(f.root,utf8(f.launcher.wstring()));held=CreateFileW(custom_steam_library::ioPath(cache).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"owned late cache denial setup");
          }else if(change=="unrelated"){auto scan=f.scan;scan["unrelatedNote"]="new harmless library observation";atomicWrite(f.root/L"state"/L"library-scan.json",scan);}
        };
        queue.enqueue(Json::array({input}));admissionInterleave={};
        check(crossed==1,"original second mutex wait interleave reached exactly once");const auto state=queue.document();
        check(state["jobs"]["steam-570"]==untouched,"existing native work preserved");
        if(stable)check(state["jobs"].size()==2&&state["jobs"].contains("steam-1643320"),"stable confirmed identity remains admissible");
        else check(state["jobs"].size()==1&&!state["jobs"].contains("steam-1643320")&&stormBytes(file)==before&&stormBytes(progress)==beforeProgress,"STALE_IDENTITY_ADMITTED_AFTER_MUTEX_WAIT");
        rows.push_back({{"name",name},{"passed",true},{"crossed",crossed},{"changedAfterFirstIdentityRead",change},{"stableControl",stable}});
      }catch(const std::exception& error){admissionInterleave={};rows.push_back({{"name",name},{"passed",false},{"error",error.what()}});}
    };
    probe("scan-revoked","revoke",false);
    probe("primary-id-changed","primary",false);
    probe("exact-binding-added","override",false);
    probe("non-game-classification","excluded",false);
    probe("exact-id-changed","exact",false);
    probe("exact-cache-denied","denied",false);
    probe("unrelated-library-edit","unrelated",true);
    probe("stable-shortcut","stable",true);
    try{
      LauncherWallpaperIdentityFixture f(root/L"promotion-control");Queue queue(f.root);queue.enqueue(Json::array({f.shortcut()}));queue.configure(true,"");const auto token=queue.start();const auto claimed=queue.claim(token);f.identify(f.primary);
      check(!token.empty()&&claimed.value("identityPending",false)&&queue.promoteIdentity(token,claimed),"ordinary original promotion still reaches the guarded admission");check(queue.document()["jobs"].contains("steam-1643320"),"confirmed promotion canonical successor preserved");queue.end(token,"deferred");rows.push_back({{"name","promotion-control"},{"passed",true}});
    }catch(const std::exception& error){rows.push_back({{"name","promotion-control"},{"passed",false},{"error",error.what()}});}
    atomicWrite(root/L"observations.json",rows);check(rows.size()==9&&std::all_of(rows.begin(),rows.end(),[](const Json& row){return row.value("passed",false);}),"LATE_IDENTITY_ADMISSION_CURRENT_BOUNDARY_FAILED");
  });

  run("launcher-wallpaper-identity-consumers",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(1);
    using Fixture=LauncherWallpaperIdentityFixture;
    Json positive=Json::array();
#ifndef YMCC_LAUNCHER_IDENTITY_SUFFIX_ONLY
    const auto probe=[&](const char* name,const std::function<void(Fixture&)>& body){
      try{Fixture f(root/wide(name));body(f);positive.push_back({{"name",name},{"passed",true}});}
      catch(const std::exception& error){positive.push_back({{"name",name},{"passed",false},{"error",error.what()}});}
    };
    probe("enqueue",[&](Fixture& f){f.identify(f.primary);const auto inventory=f.inventory();check(inventory.value("verified",false)&&inventory.value("appId",0)==1643320,"actual inventory has the confirmed primary identity");Queue queue(f.root);queue.enqueue(Json::array({inventory}));const auto state=queue.document();check(state["jobs"].size()==1&&state["jobs"].contains("steam-1643320")&&!state["jobs"].begin().value().value("identityPending",false),"QUEUE_DISCARDED_CONFIRMED_LAUNCHER_IDENTITY");});
    probe("promote",[&](Fixture& f){Queue queue(f.root);queue.enqueue(Json::array({f.shortcut()}));queue.configure(true,"");const auto token=queue.start();check(!token.empty(),"owned placeholder session");const auto target=queue.claim(token);check(target.value("identityPending",false),"actual placeholder claimed");f.identify(f.primary);check(queue.promoteIdentity(token,target),"QUEUE_CANNOT_PROMOTE_CONFIRMED_LAUNCHER");const auto state=queue.document();check(state["jobs"].contains("steam-1643320")&&!state["jobs"].contains(stringField(target,"key")),"actual canonical handoff retires placeholder");queue.end(token,"deferred");});
    probe("lookup",[&](Fixture& f){f.identify(f.primary);IdentityPorts ports;ports.appRoot=f.root/L"no-helper";unsigned calls=0;ports.current=[&](const Json&){++calls;return true;};const auto result=identifyShortcut(f.root,f.shortcut(),ports,[]{});check(result.value("verified",false)&&result.value("appId",0)==1643320&&calls==2,"LOOKUP_IGNORED_CONFIRMED_LAUNCHER_OR_FINAL_MEMBERSHIP");check(!f.store.identity(utf8(f.launcher.wstring())).value("verified",false)&&!fs::exists(f.root/L"cache"/L"jobs"),"cache reuse launches no helper or copied launcher identity");});
    probe("recovery",[&](Fixture& f){f.stopped();f.identify(f.primary);Queue queue(f.root);check(queue.recoverIdentityPrerequisites(f.root/L"no-helper.exe",[](const Json&){return true;})==1,"STOPPED_LAUNCHER_CANNOT_RECOVER_FROM_CONFIRMED_PRIMARY");const auto job=queue.document()["jobs"].begin().value();check(!job.value("autoStopped",true)&&job.value("attempts",9)==0,"actual prerequisite recovery reuses original bounded reset");});
    atomicWrite(root/L"positive-observations.json",positive);
    check(positive.size()==4&&std::all_of(positive.begin(),positive.end(),[](const Json& row){return row.value("passed",false);}),"CONFIRMED_LAUNCHER_IDENTITY_CONSUMERS_DISCONNECTED");
    {
      Fixture f(root/L"direct-cache");f.identify(f.primary);f.identify(f.launcher,555);Queue queue(f.root);queue.enqueue(Json::array({f.inventory()}));check(queue.document()["jobs"].contains("steam-555")&&!queue.document()["jobs"].contains("steam-1643320"),"exact launcher cache keeps priority over related primary");
    }
    {
      Fixture f(root/L"revoked-before-enqueue");f.identify(f.primary);const auto old=f.inventory();f.revoke();Queue queue(f.root);queue.enqueue(Json::array({old}));check(!queue.document()["jobs"].contains("steam-1643320"),"revoked relationship cannot borrow old verified inventory");
    }
    {
      Fixture f(root/L"revoked-before-promote");Queue queue(f.root);queue.enqueue(Json::array({f.shortcut()}));queue.configure(true,"");const auto token=queue.start();const auto claimed=queue.claim(token);f.identify(f.primary);f.revoke();const auto before=stormBytes(f.root/L"config"/L"game-media-downloads.json");check(!queue.promoteIdentity(token,claimed)&&stormBytes(f.root/L"config"/L"game-media-downloads.json")==before,"revoked primary cannot mutate the active placeholder");queue.end(token,"deferred");
    }
    {
      Fixture f(root/L"revoked-recovery");f.stopped();f.identify(f.primary);f.revoke();Queue queue(f.root);const auto before=stormBytes(f.root/L"config"/L"game-media-downloads.json");check(queue.recoverIdentityPrerequisites(f.root/L"no-helper.exe",[](const Json&){return true;})==0&&stormBytes(f.root/L"config"/L"game-media-downloads.json")==before,"revoked relationship cannot refund stopped debt");
    }
    {
      Fixture f(root/L"foreign-membership");f.stopped();f.identify(f.primary);Queue queue(f.root);const auto before=stormBytes(f.root/L"config"/L"game-media-downloads.json");check(queue.recoverIdentityPrerequisites(f.root/L"no-helper.exe",[](const Json&){return false;})==0&&stormBytes(f.root/L"config"/L"game-media-downloads.json")==before,"related cache cannot bypass current account membership");
    }
    {
      Fixture f(root/L"cancelled-cached-lookup");f.identify(f.primary);IdentityPorts ports;ports.appRoot=f.root/L"no-helper";unsigned calls=0;ports.current=[&](const Json&){return ++calls==1;};bool refused=false;try{identifyShortcut(f.root,f.shortcut(),ports,[]{});}catch(const ShortcutMembershipRejected&){refused=true;}check(refused&&calls==2&&!fs::exists(f.root/L"cache"/L"jobs"),"revocation during cached lookup stops before helper or return");
    }
    {
      Fixture f(root/L"locked-exact-cache");f.identify(f.primary);const auto file=custom_steam_library::identity_cache::cachePath(f.root,utf8(f.launcher.wstring()));stormFile(file,"{broken");HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"owned exact-cache deny setup");IdentityPorts ports;ports.appRoot=f.root/L"no-helper";ports.current=[](const Json&){return true;};bool refused=false;try{identifyShortcut(f.root,f.shortcut(),ports,[]{});}catch(const std::exception& error){refused=std::string(error.what())=="media-storage-unavailable";}CloseHandle(held);check(refused&&stormBytes(file)=="{broken"&&!fs::exists(f.root/L"cache"/L"jobs"),"known exact cache IO denial stays a storage failure");
    }
#endif
    {
      Fixture f(root/L"explicit-exact-binding");f.identify(f.primary);const auto file=custom_steam_library::identity_cache::cachePath(f.root,utf8(f.launcher.wstring()));stormFile(custom_steam_library::ioPath(file),"{broken");check(fs::is_regular_file(custom_steam_library::ioPath(file)),"owned exact-cache refusal setup exists");Queue queue(f.root);queue.enqueue(Json::array({f.shortcut()}));check(!queue.document()["jobs"].contains("steam-1643320")&&stormBytes(custom_steam_library::ioPath(file))=="{broken","existing invalid exact binding cannot be bypassed by related cache");
    }
    {
      Fixture f(root/L"non-shortcut-provenance");f.identify(f.primary);auto value=f.shortcut();value["catalogSource"]="ymcc-confirmed";value["appId"]=1643320;value["verified"]=true;Queue queue(f.root);queue.enqueue(Json::array({value}));check(queue.document()["jobs"].empty(),"library-only metadata cannot borrow shortcut alias authority");
    }
    {
      Queue queue(root/L"ordinary-control");queue.enqueue(Json::array({target(570)}));check(queue.document()["jobs"].contains("steam-570"),"ordinary no-executable native admission is unchanged");
    }
    atomicWrite(root/L"executed-stage.json",{{"prefixExecuted",!positive.empty()},{"suffixExecuted",true}});
    atomicWrite(root/L"boundary-result.json",{{"ok",true},{"positiveConsumers",positive},{"realSteamOrDeviceActions",false},{"sourceMembershipProvider","explicit fixture port; not live Steam proof"}});
  });


  const auto workerLongRoot=[](fs::path root){while(root.wstring().size()<310)root/=L"owned-worker-staging-xxxxxxxxxxxxxxxx";return root;};
  const auto workerLongFile=[](const fs::path& path,const std::string& bytes){fs::create_directories(custom_steam_library::ioPath(path.parent_path()));std::ofstream out(custom_steam_library::ioPath(path),std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());out.flush();check(bool(out),"prepare owned long fixture bytes");};
  const auto workerLongBytes=[](const fs::path& path){std::ifstream in(custom_steam_library::ioPath(path),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
  const auto workerMovie=[](){const auto box=[](const std::string& kind,const std::string& body){const auto n=static_cast<uint32_t>(body.size()+8);std::string value;for(int shift:{24,16,8,0})value.push_back(static_cast<char>(n>>shift));return value+kind+body;};return box("ftyp",std::string("isom")+std::string(12,'\0'))+box("moov",std::string(16,'\0'))+box("mdat",std::string(40,'q'));};
  const std::string workerSource="https://cdn.steamstatic.com/store_trailers/570/fixture.mp4",workerIdentity="steam-570\ndynamic\n"+workerSource;
  const auto workerHex=[](std::string v){return std::string(16-v.size(),'0')+v;};
  const auto workerPartId=workerHex(digestKey(workerIdentity))+workerHex(digestKey("resume:"+workerIdentity));
  const auto workerMetadata=[&](){return Json{{"resumeProtocol",1},{"identity",workerIdentity},{"finalSource",workerSource},{"validatorType","etag"},{"validator","\"v1\""},{"validatorDate",""},{"expectedSize",workerMovie().size()},{"complete",false}};};
  run("worker-long-validation",[&](const fs::path& base){
    const auto root=workerLongRoot(base),file=root/L"movie.mp4";workerLongFile(file,workerMovie());std::string failure;
    try{validateDownloadedVideo(file);}catch(const std::exception& error){failure=error.what();}
    atomicWrite(base/L"validation-observation.json",{{"logicalChars",file.wstring().size()},{"failure",failure}});
    check(failure.empty(),"deep staged video validation cannot read its real file");
    workerLongFile(file,workerMovie().substr(0,20));bool rejected=false;try{validateDownloadedVideo(file);}catch(...){rejected=true;}check(rejected,"deep truncated video still rejected");
  });
  run("worker-long-retained-resume",[&](const fs::path& base){
    const auto root=workerLongRoot(base),part=root/L"cache"/L"jobs"/L"game-media"/wide(workerPartId+".part");workerLongFile(part,workerMovie().substr(0,16));checkpoint::write(resumeMetadata(part),workerMetadata());
    check(retainedResume(part,workerIdentity,"steam-570"),"deep valid resume was discarded by raw file IO");
    auto doc=workerMetadata();doc["resumeProtocol"]=99;checkpoint::write(resumeMetadata(part),doc);const auto saved=workerLongBytes(resumeMetadata(part));bool upgrade=false;try{retainedResume(part,workerIdentity,"steam-570");}catch(const checkpoint::UpgradeRequired&){upgrade=true;}check(upgrade&&workerLongBytes(resumeMetadata(part))==saved,"future deep resume remains upgrade-required and unchanged");
  });
  run("worker-long-budget-accounting",[&](const fs::path& base){
    const auto root=workerLongRoot(base),folder=root/L"cache"/L"jobs"/L"game-media";workerLongFile(folder/wide(workerPartId+".part"),"partial");workerLongFile(folder/L"owned.part.corrupt.saved","preserved");workerLongFile(folder/L"unrelated.txt","ignored");WorkerContext context(root);
    atomicWrite(base/L"budget-observation.json",{{"logicalChars",folder.wstring().size()},{"used",context.used},{"expected",16}});
    check(context.used==16,"deep staging budget misses retained and corrupt part bytes");
  });
  run("worker-long-orphan-collection",[&](const fs::path& base){
    const auto root=workerLongRoot(base),folder=root/L"cache"/L"jobs"/L"game-media",part=folder/wide(workerPartId+".part"),orphan=folder/wide(std::string(32,'a')+".part");workerLongFile(part,"retained");workerLongFile(resumeMetadata(part),"{broken sidecar");workerLongFile(orphan,"orphan");fs::last_write_time(custom_steam_library::ioPath(orphan),fs::file_time_type::clock::now()-std::chrono::hours(48));WorkerContext context(root);context.used=14;context.collectAbandoned(root);
    check(!fs::exists(custom_steam_library::ioPath(orphan))&&fs::exists(custom_steam_library::ioPath(part))&&context.used==8,"deep abandoned collection failed or erased checkpoint evidence");
  });
  run("worker-long-budget-trim",[&](const fs::path& base){
    const auto root=workerLongRoot(base),folder=root/L"cache"/L"jobs"/L"game-media",part=folder/wide(workerPartId+".part");workerLongFile(part,workerMovie().substr(0,16));checkpoint::write(resumeMetadata(part),workerMetadata());
    const auto protectedPart=folder/wide(std::string(32,'b')+".part");workerLongFile(protectedPart,"protected");auto future=workerMetadata();future["resumeProtocol"]=99;checkpoint::write(resumeMetadata(protectedPart),future);const auto protectedBytes=workerLongBytes(resumeMetadata(protectedPart));WorkerContext context(root);context.used=WorkerContext::stagingLimit;context.trim(root);
    check(!fs::exists(custom_steam_library::ioPath(part))&&context.used==WorkerContext::stagingLimit-16,"deep budget trim cannot reclaim validated own checkpoint");
    check(fs::exists(custom_steam_library::ioPath(protectedPart))&&workerLongBytes(resumeMetadata(protectedPart))==protectedBytes,"deep budget trim preserves future and mismatched evidence");
  });
  run("worker-long-temporary-owner",[&](const fs::path& base){
    MediaPreferenceFixtureState restore;batteryFixtureSource(1);const auto root=workerLongRoot(base);Queue q(root);q.enqueue(Json::array({target(570)}));const auto token=q.start();check(!token.empty(),"owned deep temporary session begins");const std::string name(32,'a');const auto file=root/L"cache"/L"jobs"/L"game-media"/wide(name+".part");workerLongFile(file,"temporary");q.trackTemporary(token,name+".part");q.cleanupTemporary();check(fs::exists(custom_steam_library::ioPath(file)),"live temporary writer remains protected");auto marker=checkpoint::read(q.temporaryRecordPath(),1024*1024,2);marker["files"][name+".part"]["workerPid"]=MAXDWORD;marker["files"][name+".part"]["workerBirth"]="1";checkpoint::write(q.temporaryRecordPath(),marker);q.cleanupTemporary();
    check(!fs::exists(custom_steam_library::ioPath(file))&&q.temporaryRecords().empty(),"deep exited writer temporary file and marker were not reclaimed");
  });

  run("power-preference-owned-no-read",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(1);Queue q(root);Json inventory=Json::array();for(int n=1;n<=256;++n)inventory.push_back(target(n));q.enqueue(inventory);q.configureWallpaperPower(true);
    const auto file=root/L"config"/L"game-media-downloads.json";auto replica=read(file);replica["wallpaperPauseOnBattery"]=false;atomicWrite(file,replica);const auto original=stormBytes(file);
    IO_COUNTERS before{},after{};check(GetProcessIoCounters(GetCurrentProcess(),&before)!=FALSE,"preference owner IO start");const auto started=GetTickCount64();for(int n=0;n<32;++n)refreshMediaPowerPreference(root);check(GetProcessIoCounters(GetCurrentProcess(),&after)!=FALSE,"preference owner IO finish");
    atomicWrite(root/L"preference-owner-cost.json",{{"iterations",32},{"jobs",256},{"elapsedMs",GetTickCount64()-started},{"readOperations",after.ReadOperationCount-before.ReadOperationCount},{"readBytes",after.ReadTransferCount-before.ReadTransferCount},{"scope","actual refresh helper in settings-owned process, GetProcessIoCounters; not installed CPU"}});
    check(powerPolicy::enabled(),"durable old OFF cannot replace settings-owned ON");check(stormBytes(file)==original,"preference read does not rewrite the queue");
    check(after.ReadOperationCount==before.ReadOperationCount&&after.ReadTransferCount==before.ReadTransferCount,"authoritative preference refresh still reads the full queue");
    batteryFixtureSource(0);check(powerPolicy::blocked(),"owned no-read path still observes the shared DC source at work boundary");
    powerPolicy::own(false);refreshMediaPowerPreference(root);check(!powerPolicy::enabled()&&!powerPolicy::blocked(),"authoritative OFF remains effective without a replica read");
  });
  run("power-watch-notify-through-unreadable",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(1);Queue q(root);q.enqueue(Json::array({target(570)}));q.configure(false,"");const auto file=root/L"config"/L"game-media-downloads.json";const auto original=stormBytes(file);
    struct Handles{HANDLE changed=nullptr,ack=nullptr,held=INVALID_HANDLE_VALUE;~Handles(){if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);if(changed)CloseHandle(changed);if(ack)CloseHandle(ack);}} handles;
    handles.changed=CreateEventW(nullptr,FALSE,FALSE,eventName(root,L"library").c_str());handles.ack=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(handles.changed&&handles.ack,"owned download route and acknowledgement created");ResetEvent(handles.changed);
    std::atomic<unsigned> callbacks{0};Json observed;PolicyObserveGuard watchWait;
    {ymcc::media::downloads::Watcher watcher(root,L"library",[&]{observed=q.snapshot();++callbacks;SetEvent(handles.ack);});check(waitUntil([&]{return policyWaitEntries.load()>0;},1000),"actual download watcher reached its event wait");
      handles.held=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(handles.held!=INVALID_HANDLE_VALUE,"durable queue exclusively locked");
      bool rejected=false;try{refreshMediaPowerPreference(root);}catch(const std::exception&){rejected=true;}check(rejected,"replica preference read really fails under the sharing lock");
      SetEvent(handles.changed);const auto notified=WaitForSingleObject(handles.ack,1000)==WAIT_OBJECT_0;
      atomicWrite(root/L"unreadable-notice.json",{{"callbackObserved",notified},{"callbacks",callbacks.load()},{"status",notified?observed:Json::object()},{"sharingLockHeldDuringNotification",true}});
      check(notified&&callbacks==1,"preference read failure swallowed the download change notification");
      check(observed.value("recoveryRequired",false)&&!observed.value("enabled",true),"failed queue read is reported as unavailable instead of healthy progress");
      CloseHandle(handles.held);handles.held=INVALID_HANDLE_VALUE;ResetEvent(handles.ack);SetEvent(handles.changed);check(WaitForSingleObject(handles.ack,1000)==WAIT_OBJECT_0&&callbacks==2,"same watcher continues after the sharing lock is released");check(observed.value("total",0)==1&&!observed.value("recoveryRequired",false),"recovered notice contains the real queue");
    }
    check(stormBytes(file)==original,"unreadable preference and notification never repair or rewrite the queue");
  });

  run("power-watch-unchanged-progress",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(0);Queue q(root);Json items=Json::array();for(int n=1;n<=256;++n)items.push_back(target(n));q.enqueue(items);
    const auto file=root/L"config"/L"game-media-downloads.json";auto doc=read(file);doc["wallpaperPauseOnBattery"]=true;doc["fixturePadding"]="xx";atomicWrite(file,doc);const auto initial=stormBytes(file);
    struct Handles {HANDLE changed=nullptr,ack=nullptr,held=INVALID_HANDLE_VALUE;~Handles(){if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);if(changed)CloseHandle(changed);if(ack)CloseHandle(ack);}} h;
    h.changed=CreateEventW(nullptr,FALSE,FALSE,eventName(root,L"library").c_str());h.ack=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(h.changed&&h.ack,"create owned watcher notification route");ResetEvent(h.changed);
    std::atomic<unsigned> callbacks{0};IO_COUNTERS a{},b{};DWORD handlesBefore=0,handlesAfter=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"watcher handle baseline available");
    {
      ymcc::media::downloads::Watcher watcher(root,L"library",[&]{++callbacks;SetEvent(h.ack);});
      const auto notify=[&]{ResetEvent(h.ack);SetEvent(h.changed);check(WaitForSingleObject(h.ack,1000)==WAIT_OBJECT_0,"same observer delivers queue event");};
      notify();check(powerPolicy::enabled()&&powerPolicy::blocked(),"observer cold ON applies original DC gate");
      check(GetProcessIoCounters(GetCurrentProcess(),&a)!=FALSE,"watcher event IO start");const auto start=GetTickCount64();
      for(int n=0;n<32;++n)notify();
      check(GetProcessIoCounters(GetCurrentProcess(),&b)!=FALSE,"watcher event IO finish");
      atomicWrite(root/L"watch-cost.json",{{"iterations",32},{"jobs",256},{"callbacks",callbacks.load()},{"elapsedMs",GetTickCount64()-start},{"readOperations",b.ReadOperationCount-a.ReadOperationCount},{"readBytes",b.ReadTransferCount-a.ReadTransferCount},{"otherOperations",b.OtherOperationCount-a.OtherOperationCount},{"scope","actual Watcher thread, callback only acknowledges background advisory; not whole process or installed CPU"}});
      check(callbacks==33,"unchanged queue progress still invokes every callback");
      check(stormBytes(file)==initial,"observer never rewrites durable queue");
      check(b.ReadTransferCount==a.ReadTransferCount&&b.ReadOperationCount==a.ReadOperationCount,"unchanged progress still reads complete durable queue");
      const auto setPreservedTime=[&](const Json& next,const FILETIME& stamp){atomicWrite(file,next);HANDLE f=CreateFileW(file.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);check(f!=INVALID_HANDLE_VALUE,"open owned replacement timestamp");const bool ok=SetFileTime(f,nullptr,nullptr,&stamp)!=FALSE;CloseHandle(f);check(ok,"restore fixture write timestamp");};
      WIN32_FILE_ATTRIBUTE_DATA stamp{};check(GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&stamp)!=FALSE,"capture durable file time");
      doc["wallpaperPauseOnBattery"]=false;doc["fixturePadding"]="x";setPreservedTime(doc,stamp.ftLastWriteTime);check(stormBytes(file).size()==initial.size(),"replacement keeps size and write time");notify();check(!powerPolicy::enabled()&&!powerPolicy::blocked(),"same-size same-time replacement OFF is observed by file identity");
      doc["wallpaperPauseOnBattery"]=true;doc["fixturePadding"]="xx";setPreservedTime(doc,stamp.ftLastWriteTime);notify();check(powerPolicy::enabled()&&powerPolicy::blocked(),"same-size same-time replacement ON restores original DC gate");
      h.held=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h.held!=INVALID_HANDLE_VALUE,"lock already observed queue");const auto count=callbacks.load();notify();check(callbacks==count+1,"cached observation cannot swallow unreadable notice");CloseHandle(h.held);h.held=INVALID_HANDLE_VALUE;
      doc["wallpaperPauseOnBattery"]=false;doc["fixturePadding"]="x";setPreservedTime(doc,stamp.ftLastWriteTime);notify();check(!powerPolicy::enabled(),"same observer recovers after sharing failure");
      {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<"{broken";out.flush();check(bool(out),"owned malformed fixture written");}notify();check(!powerPolicy::enabled(),"malformed observation leaves prior preference intact");atomicWrite(file,doc);notify();check(!powerPolicy::enabled(),"failed parse is not retained as successful observation");
      fs::remove(file);notify();check(!powerPolicy::enabled(),"missing queue does not invent ON");doc["wallpaperPauseOnBattery"]=true;atomicWrite(file,doc);notify();check(powerPolicy::enabled()&&powerPolicy::blocked(),"recreated queue is read after missing observation");
      {
        Lock held(root);doc["wallpaperPauseOnBattery"]=false;atomicWrite(file,doc);const auto began=GetTickCount64();notify();check(GetTickCount64()-began<1000,"contended preference refresh does not block advisory notice");check(!powerPolicy::enabled()&&!powerPolicy::blocked(),"original atomic queue read remains available while writer owns root mutex");
      }
      notify();check(!powerPolicy::enabled()&&!powerPolicy::blocked(),"same observer consumes changed preference after root mutex release");
      GetProcessIoCounters(GetCurrentProcess(),&a);const auto stoppedCount=callbacks.load();Sleep(180);GetProcessIoCounters(GetCurrentProcess(),&b);check(callbacks==stoppedCount&&b.ReadOperationCount==a.ReadOperationCount&&b.ReadTransferCount==a.ReadTransferCount,"idle observer has no file polling");
    }
    check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE&&handlesAfter==handlesBefore,"normal Watcher teardown returns all owned handles");
    const auto oldCount=callbacks.load();SetEvent(h.changed);Sleep(50);check(callbacks==oldCount,"late event cannot revive destroyed observer");
  });

  run("power-preference-replica-updates",[&](const fs::path& root){
    MediaPreferenceFixtureState restore;batteryFixtureSource(0);Queue q(root);q.enqueue(Json::array({target(571)}));const auto file=root/L"config"/L"game-media-downloads.json";auto doc=read(file);doc["wallpaperPauseOnBattery"]=true;atomicWrite(file,doc);refreshMediaPowerPreference(root);check(powerPolicy::enabled()&&powerPolicy::blocked(),"unowned library replica still consumes durable ON and DC gate");
    doc["wallpaperPauseOnBattery"]=false;atomicWrite(file,doc);refreshMediaPowerPreference(root);check(!powerPolicy::enabled()&&!powerPolicy::blocked(),"unowned replica still consumes a later durable OFF");
    doc["wallpaperPauseOnBattery"]=true;atomicWrite(file,doc);refreshMediaPowerPreference(root);check(powerPolicy::enabled()&&powerPolicy::blocked(),"unowned replica can consume ON again without a new process");
    const auto rev=powerPolicy::ticket().revision;batteryFixtureSource(1);check(!powerPolicy::blocked()&&powerPolicy::ticket().revision!=rev,"replica ON resumes at original shared AC boundary");
  });

  run("checkpoint-long-path",[&](const fs::path& root){
    auto directory=root;while(directory.wstring().size()<280)directory/=L"owned-subdirectory";
    fs::create_directories(custom_steam_library::ioPath(directory));
    const auto file=directory/L"checkpoint.json";const auto logical=file.wstring();
    check(logical.size()>260&&!logical.starts_with(L"\\\\?\\"),"fixture uses an ordinary long logical path");
    const auto bytes=[](const fs::path& p){std::ifstream in(custom_steam_library::ioPath(p),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    const Json original={{"schemaVersion",1},{"unknown",{{"keep",true}}},{"sequence",1}};
    checkpoint::write(file,original);check(checkpoint::read(file)==original,"long checkpoint roundtrip preserves the complete document");
    const std::string corrupt("bad\0checkpoint",14);{std::ofstream out(custom_steam_library::ioPath(file),std::ios::binary|std::ios::trunc);out.write(corrupt.data(),corrupt.size());check(bool(out),"owned corrupt bytes are written in the long directory");}
    bool refused=false;try{checkpoint::read(file);}catch(...){refused=true;}check(refused&&bytes(file)==corrupt,"long malformed checkpoint remains rejected and unchanged");
    checkpoint::preserve(file);bool preserved=false;
    for(const auto& entry:fs::directory_iterator(custom_steam_library::ioPath(directory)))if(entry.path().filename().wstring().starts_with(file.filename().wstring()+L".corrupt.")&&bytes(entry.path())==corrupt)preserved=true;
    check(preserved&&bytes(file)==corrupt,"long checkpoint preserves exact corrupt original bytes");
    auto repaired=original;repaired["sequence"]=2;checkpoint::write(file,repaired);check(checkpoint::read(file)==repaired,"long checkpoint can be replaced and reread after preservation");
    auto future=repaired;future["schemaVersion"]=99;checkpoint::write(file,future);const auto retained=bytes(file);bool futureRefused=false;
    try{checkpoint::read(file);}catch(const checkpoint::UpgradeRequired&){futureRefused=true;}check(futureRefused&&bytes(file)==retained,"long path retains the future-version refusal without rewriting bytes");
    for(const bool diskFull:{false,true}){
      stormDiskFull=diskFull;stormReplaceFailure=!diskFull;bool rejected=false;try{checkpoint::write(file,original);}catch(...){rejected=true;}stormDiskFull=stormReplaceFailure=false;
      check(rejected&&bytes(file)==retained,"long checkpoint write failure cannot ACK or replace the previous document");
      for(const auto& entry:fs::directory_iterator(custom_steam_library::ioPath(directory)))check(entry.path().filename().wstring().find(L".tmp.")==std::wstring::npos,"long failed publication releases its owned temporary file");
    }
    bool unsafe=false;try{checkpoint::read(directory);}catch(const std::exception& e){unsafe=std::string(e.what())=="media-cache-file-unsafe";}
    check(unsafe&&file.wstring()==logical,"long IO retains the unsafe-type guard and ordinary logical path");
  });
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
    if(name=="broken-json"){
      check(q.snapshot().value("checkpointReadable",false),"Recovered queue lacks a current read confirmation");
      const auto zeros=root/L"zero-filled",index=zeros/L"config"/L"game-media-downloads.json";const std::string retained(3384,'\0');stormFile(index,retained);Queue restored(zeros);
      const auto refused=restored.snapshot();atomicWrite(root/L"zero-filled-unreadable.json",refused);
      check(refused.contains("checkpointReadable")&&!refused.value("checkpointReadable",true)&&refused.value("recoveryRequired",false),"real zero-filled queue cannot confirm a current read");
      restored.configure(false,"");check(stormPreserved(index,retained),"exact zero-filled original survives recovery");
      const auto disabled=restored.snapshot();check(disabled.value("checkpointReadable",false)&&disabled.value("recoveryRequired",false)&&!disabled.value("enabled",true),"readable repaired OFF remains distinct from unavailable storage");
      restored.enqueue(Json::array({target(572)}));restored.configure(true,"");const auto fresh=restored.start();check(!fresh.empty(),"new confirmed work resumes beside unknown historical debt");const auto selected=restored.claim(fresh);check(selected.value("appId",0)==572,"zero-filled repair cannot invent historical game identities");restored.progress(fresh,"downloading",33,100);
      const auto active=restored.snapshot();atomicWrite(root/L"zero-filled-recovered-active.json",active);
      check(active.value("checkpointReadable",false)&&active.value("recoveryRequired",false)&&active.value("activeCount",0)==1,"current readable download is not hidden by retained history debt");
      check(!restored.document().contains("checkpointReadable")&&!read(zeros/L"config"/L"game-media-download-progress.json").contains("checkpointReadable"),"current read confirmation is observational and never persisted");
      restored.result(fresh,"steam-572",true);restored.end(fresh,"completed");const auto done=restored.snapshot();atomicWrite(root/L"zero-filled-recovered-complete.json",done);
      check(done.value("checkpointReadable",false)&&done.value("recoveryRequired",false)&&done.value("completed",0)==1&&done.value("activeCount",-1)==0,"only the newly confirmed game completes while historical debt remains");
    }
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
    atomicWrite(root/L"package-manifest.json",{{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",videoQualityPolicy},{"backgroundManualProtocol",1},{"backgroundVideoSelectionProtocol",1}});atomicWrite(root/L"fixture.json",{{"mode","complete"}});
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
    const auto claim=q.snapshot()["active"]["steam-570"];
    q.recover(token,"media-task-time-budget","steam-570",&claim);
    const auto settled=q.document()["jobs"]["steam-570"];
    const auto sample=settled.value("lastTimeoutObservation",Json::object());
    check(sample.value("source",std::string{})=="supervisor"&&!sample.contains("workerStage")&&
      sample.value("lastReportedPhase",std::string{})=="downloading"&&sample.value("lastReportedBytes",0)==4&&sample.value("lastReportedTotalBytes",0)==8,
      "supervisor settlement keeps the current claim's last report, not a presumed failing stage");
    check(Queue::terminalReceipt(q.snapshot(),token)&&settled.value("attempts",0)==1&&settled.value("retryAt",int64_t{0})>now(),
      "same retry and terminal checkpoint after retaining timeout sample");

    Queue superseded(root/L"superseded");superseded.enqueue(Json::array({target(570)}));const auto current=superseded.start();auto oldClaim=superseded.claim(current);
    oldClaim["taskStartedAt"]=oldClaim["taskStartedAt"].get<int64_t>()-1000;
    superseded.recover(current,"media-task-time-budget","steam-570",&oldClaim);
    const auto next=superseded.document()["jobs"]["steam-570"];
    check(next.value("attempts",0)==0&&!next.contains("lastTimeoutObservation")&&next.value("state",std::string{})=="pending",
      "an expired claim must not charge or label a replacement claim for the same game");

    Queue mismatched(root/L"mismatched");mismatched.enqueue(Json::array({target(570)}));const auto fresh=mismatched.start();mismatched.claim(fresh);
    check(mismatched.progress(fresh,"steaM-570","downloading",7,8)==false,"progress must reject a different key");
    check(mismatched.progress(fresh,"steam-570","downloading",5,8),"progress must accept the actual claim");
    const auto status=mismatched.root()/L"config"/L"game-media-download-progress.json";
    auto poisoned=read(status);poisoned["active"]["steam-570"]["requestBindingRevision"]=99;atomicWrite(status,poisoned);
    mismatched.result(fresh,"steam-570",false,"media-task-timeout",0,{}, {}, {},TimeoutStage::validation);
    const auto local=mismatched.document()["jobs"]["steam-570"].value("lastTimeoutObservation",Json::object());
    check(local.value("source",std::string{})=="worker"&&local.value("workerStage",std::string{})=="validation"&&!local.contains("lastReportedPhase"),
      "worker stage persists independently without progress from another binding");

    Json observations=Json::array();
    for(const auto* name:{"session-first","attempt-first","unexpired-session-first","old-overcharge","stale-binding","attempt-count-stop"}){
      const std::string variant=name;
      Queue lane(root/wide(name));lane.enqueue(Json::array({target(570)}));const auto owner=lane.start();
      const auto wall=now();const auto issued=wall-240000;
      const auto attemptEnd=variant=="attempt-first"?issued+60000:variant=="unexpired-session-first"?wall+120000:issued+180000;
      const auto sessionEnd=variant=="attempt-first"?issued+120000:variant=="unexpired-session-first"?wall+60000:issued+60000;
      const auto charged=variant=="old-overcharge"?issued+100000:issued+10000;
      const auto previous=charged-issued;
      const int priorAttempts=variant=="attempt-count-stop"?automaticAttemptLimit-1:0;
      check(lane.update(owner,[&](Json& doc){auto& row=doc["jobs"]["steam-570"];
        row["automaticElapsedMs"]=previous;row["attempts"]=priorAttempts;
        Json window={{"version",1},{"attemptIssuedAt",issued},{"attemptDeadlineAt",attemptEnd},{"chargedAt",charged},
          {"sessionIssuedAt",issued-60000},{"sessionDeadlineAt",sessionEnd}};
        for(const auto* key:{"requestBindingRevision","provenanceRevision","selectedVideo"})if(row.contains(key))window[key]=row[key];
        if(variant=="stale-binding")window["requestBindingRevision"]=9182;
        row["sharedSleepWindow"]=std::move(window);
      }),"could not seed isolated continuation");
      const auto result=lane.claim(owner);
      const auto row=lane.document()["jobs"]["steam-570"];
      const auto chargedMs=row.value("automaticElapsedMs",int64_t{0});
      const auto sample=row.value("lastTimeoutObservation",Json::object());
      const auto remaining=result.is_null()?int64_t{0}:result.value("attemptBudgetMs",int64_t{0});
      bool good=false;
      if(variant=="unexpired-session-first"){
        good=!result.is_null()&&remaining>59000&&remaining<=60000&&sample.empty()&&
          remaining==sessionEnd-result.value("taskStartedAt",int64_t{0});
      }else if(variant=="stale-binding"){
        good=!result.is_null()&&sample.empty()&&chargedMs==previous&&!row.contains("sharedSleepWindow");
      }else{
        good=result.is_null()&&lane.claim(owner).is_null()&&lane.document()["jobs"]["steam-570"]==row&&
          chargedMs==(variant=="old-overcharge"?previous:60000)&&!row.contains("sharedSleepWindow")&&
          row.value("attempts",0)==priorAttempts+1&&row.value("lastError",std::string{})=="media-task-time-budget"&&
          sample.value("source",std::string{})=="continuation"&&sample.value("elapsedMs",int64_t{0})==60000&&
          sample.value("continuationLimit",std::string{})==(variant=="attempt-first"?"attempt":"session")&&
          !sample.contains("workerStage")&&!sample.contains("lastReportedPhase")&&
          (priorAttempts==0?row.value("retryAt",int64_t{0})>wall:
             row.value("stopReason",std::string{})=="automatic-attempt-budget-exhausted"&&row.value("retryAt",int64_t{-1})==0);
      }
      observations.push_back({{"variant",variant},{"pass",good},{"chargedMs",chargedMs},{"remainingMs",remaining},
        {"observedTimeout",sample},{"stopReason",row.value("stopReason",std::string{})}});
    }
    atomicWrite(root/L"continuation-observations.json",observations);
    for(const auto& observation:observations)check(observation.value("pass",false),"wallpaper continuation must charge only admitted time and identify its expiry");
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
  if(only=="worker-transfer-timeout")run("worker-transfer-timeout",workerTransferTimeoutObservation);
  if(!only.empty()&&results.empty())throw std::runtime_error("requested storm case not found");
  const bool ok=std::all_of(results.begin(),results.end(),[](const auto& r){return r["status"]=="PASS";});
  Json report={{"ok",ok},{"cases",results},{"checks",checks},{"scope","original Queue load/consume/save/reread; isolated files; no processes or network"}};
  atomicWrite(base/L"results.json",report);return ok?0:1;
}

// Internal fixture entry only: attach to the still-live direct parent before
// constructing the real Queue/WorkerContext. It never starts a host or producer.
static int attachedParentUnknownChild(int argc,wchar_t** argv){
  if(argc!=6)throw std::runtime_error("attached-parent-unknown child requires root, parent PID, birth and session");
  const auto decimal=[](const std::wstring& value){
    if(value.empty()||value.size()>20||value.find_first_not_of(L"0123456789")!=std::wstring::npos)
      throw std::runtime_error("invalid attached-parent-unknown identity");
    const auto number=std::stoull(value);
    if(!number)throw std::runtime_error("zero attached-parent-unknown identity");
    return number;
  };
  const auto pidValue=decimal(argv[3]),birth=decimal(argv[4]);
  if(pidValue>MAXDWORD)throw std::runtime_error("invalid attached-parent-unknown PID");
  const DWORD parentPid=static_cast<DWORD>(pidValue);
  const auto token=utf8(argv[5]);const fs::path root(argv[2]);
  if(!root.is_absolute()||root.filename()!=L"attached-parent-unknown"||token.size()!=32||!safeKey(token))
    throw std::runtime_error("invalid attached-parent-unknown scope");
  const auto request=read(root/L"attached-request.json");
  if(request.value("fixture",std::string{})!="attached-parent-unknown-v1"||
     request.value("parentPid",DWORD{0})!=parentPid||request.value("parentBirth",uint64_t{0})!=birth||
     request.value("session",std::string{})!=token||request.value("root",std::string{})!=utf8(root.wstring())||
     fs::exists(root/L"attached-child-result.json"))
    throw std::runtime_error("attached-parent-unknown request mismatch");
  // An accidental fallback would return AC and increment this counter; no OS
  // sample is needed in the child, and no local producer receipt is installed.
  batteryFixtureAc.store(1);batteryPowerReads=0;deniedNetworkAttempts=0;
  ymcc::steaminfo::transfer::Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parentPid));
  Json report={{"status","FAIL"},{"parentPid",parentPid},{"parentBirth",birth},
    {"childPid",GetCurrentProcessId()},{"childBirth",processBirthToken(GetCurrentProcessId())},
    {"session",token},{"scope","actual attached Queue/Worker preflight; no active HTTP/chunk or user OFF"}};
  bool ok=false;
  try{
    check(parent.value&&processBirthToken(parentPid)==std::to_string(birth)&&WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT,
      "attached child retains the exact live parent birth");
    {
      ymcc::powerSource::channel::Consumer consumer;
      const bool attached=consumer.attach(parent.value,birth);
      const bool direct=ymcc::powerSource::channel::detail::directParent(parentPid);
      const auto identity=consumer.rootIdentity();
      report["attachment"]={{"attached",attached},{"directParent",direct},{"rootPid",identity.pid},
        {"rootBirth",identity.birth},{"windowsSession",identity.session}};
      check(attached&&direct&&identity.pid==parentPid&&identity.birth==birth,
        "real child attaches to the actual direct parent's birth-bound map");
      ymcc::powerSource::channel::Binding binding(consumer);
      const auto ac=consumer.snapshot();const auto boundAc=ymcc::powerSource::snapshot();
      report["ac"]={{"readable",ac.readable},{"known",ac.known},{"ac",ac.ac},
        {"sampleSequence",ac.sampleSequence},{"revision",ac.revision}};
      check(ac.readable&&ac.known&&ac.ac==1&&ac.sampleSequence>0&&boundAc.sampleSequence==ac.sampleSequence,
        "bound actual child observes the published known AC receipt");
      Queue queue(root);
      check(queue.allowed(token),"attached AC admits the existing actual queue session");
      auto context=std::make_shared<WorkerContext>(root);
      Worker worker(root,token,parent.value,context,{}, {},mediaSleepFixture::permit());
      const auto before=queue.document(),job=before["jobs"]["steam-713"];
      const auto part=root/L"cache"/L"jobs"/L"game-media"/L"12345678901234567890123456789012.part";
      auto sidecar=part;sidecar.replace_extension(L".resume.json");
      const auto partBytes=stormBytes(part),sidecarBytes=stormBytes(sidecar);
      const auto media=root/L"config"/L"game-media.json";
      const bool mediaExisted=fs::exists(media);const auto mediaBytes=stormBytes(media);
      report["before"]=before;
      check(before.value("enabled",false)&&before.value("wallpaperPauseOnBattery",false)&&
        before.value("session",std::string{})==token&&!before.value("sessionClaimed",true)&&
        before.value("active",Json::object()).empty()&&job.value("state",std::string{})=="pending"&&
        job.value("attempts",0)==1&&job.value("retryAt",int64_t{0})==123&&job.value("automaticElapsedMs",int64_t{0})==4321,
        "child retains ON and the existing pending job with nonzero retry and elapsed debt");
      atomicWrite(root/L"attached-child-ready.json",{{"parentPid",parentPid},{"parentBirth",birth},
        {"childPid",GetCurrentProcessId()},{"childBirth",processBirthToken(GetCurrentProcessId())},
        {"attached",attached},{"directParent",direct},{"rootPid",identity.pid},{"rootBirth",identity.birth},
        {"sampleSequence",ac.sampleSequence},{"revision",ac.revision},{"session",token}});
      check(waitUntil([&]{return WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT||fs::exists(root/L"parent-unknown.json");},3000),
        "parent publishes the ordered unknown command within the child handshake limit");
      const auto command=read(root/L"parent-unknown.json");
      check(command.value("parentPid",DWORD{0})==parentPid&&command.value("parentBirth",uint64_t{0})==birth&&
        command.value("session",std::string{})==token,"unknown command belongs to the same live source and queue");
      const auto unknown=consumer.snapshot();const auto boundUnknown=ymcc::powerSource::snapshot();
      const auto currentIdentity=consumer.rootIdentity();
      report["unknown"]={{"readable",unknown.readable},{"known",unknown.known},{"ac",unknown.ac},
        {"rawAc",unknown.status.ACLineStatus},{"sampleSequence",unknown.sampleSequence},{"revision",unknown.revision},
        {"rootPid",currentIdentity.pid},{"rootBirth",currentIdentity.birth},
        {"parentAlive",WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT}};
      check(WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT&&processBirthToken(parentPid)==std::to_string(birth)&&
        currentIdentity.pid==parentPid&&currentIdentity.birth==birth&&unknown.readable&&!unknown.known&&unknown.ac==-1&&
        unknown.status.ACLineStatus==255&&unknown.sampleSequence>ac.sampleSequence&&unknown.revision>ac.revision&&
        unknown.sampleSequence==command.value("sampleSequence",uint64_t{0})&&unknown.revision==command.value("revision",uint64_t{0})&&
        boundUnknown.sampleSequence==unknown.sampleSequence&&boundUnknown.readable&&!boundUnknown.known,
        "readable unknown comes from the valid attached live parent, not missing map or local power");
      const bool allowed=queue.allowed(token);const bool claimDenied=queue.claim(token).is_null();
      report["queueUnknown"]={{"allowed",allowed},{"claimDenied",claimDenied}};
      check(!allowed&&claimDenied&&queue.document()["jobs"]["steam-713"]==job,
        "attached unknown rejects actual queue admission and claim without consuming the job");
      const auto workerExit=worker.run();report["workerExit"]=workerExit;
      const auto paused=queue.document();report["paused"]=paused;
      check(workerExit==0&&Queue::terminalReceipt(paused,token)&&paused.value("phase",std::string{})=="paused"&&
        paused.value("reason",std::string{})=="download-paused"&&paused.value("enabled",false)&&
        paused.value("wallpaperPauseOnBattery",false)&&!paused.value("sessionClaimed",true),
        "actual attached Worker returns through its original power-pause terminal and retains ON");
      check(paused["jobs"]["steam-713"]==job&&paused.value("active",Json::object()).empty()&&
        paused.value("current",Json::object()).empty()&&paused.value("taskStartedAt",int64_t{-1})==0,
        "actual Worker neither claims nor spends or refunds attempts, retry or automatic elapsed debt");
      const bool lateClaimDenied=queue.claim(token).is_null();
      const bool lateProgressDenied=!queue.progress(token,"steam-713","late-unknown-progress",1,1);
      queue.result(token,"steam-713",true);queue.result(token,"steam-713",false,"late-unknown-result");
      const auto after=queue.document();report["afterLateCallbacks"]=after;
      report["lateCallbacks"]={{"claimDenied",lateClaimDenied},{"progressDenied",lateProgressDenied}};
      check(lateClaimDenied&&lateProgressDenied&&after["jobs"]["steam-713"]==job,
        "late old child callbacks cannot claim, publish progress or overwrite the retained job");
      const bool retained=fs::exists(part)&&fs::exists(sidecar)&&stormBytes(part)==partBytes&&stormBytes(sidecar)==sidecarBytes;
      const bool noMediaWrite=fs::exists(media)==mediaExisted&&stormBytes(media)==mediaBytes;
      report["retainedFiles"]={{"part",retained},{"sidecar",retained},{"mediaResultUnchanged",noMediaWrite}};
      check(retained&&noMediaWrite,"attached unknown preserves exact partial/sidecar bytes and cannot publish media results");
      const auto finalSource=consumer.snapshot();
      check(WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT&&finalSource.sampleSequence==unknown.sampleSequence&&
        finalSource.readable&&!finalSource.known&&deniedNetworkAttempts.load()==0&&batteryPowerReads.load()==0,
        "parent remains live and unknown throughout actual Worker with zero network attempts or fallback power reads");
      ok=true;
    } // Binding and Consumer duplicates/maps leave scope before closing the owner handle.
  }catch(const std::exception& error){report["error"]=error.what();}
  const bool parentAlive=parent.value&&WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT;
  const bool ownerClosed=parent.value&&CloseHandle(parent.value)!=FALSE;
  if(ownerClosed)parent.value=nullptr;
  report["parentAliveBeforeOwnerClose"]=parentAlive;report["ownerHandleClosed"]=ownerClosed;
  report["networkAttempts"]=deniedNetworkAttempts.load();report["fallbackPowerReads"]=batteryPowerReads.load();
  report["checks"]=checks;ok=ok&&parentAlive&&ownerClosed;
  report["status"]=ok?"PASS":"FAIL";atomicWrite(root/L"attached-child-result.json",report);
  std::cout<<report.dump()<<std::endl;return ok?0:1;
}

static void attachedParentUnknownBoundary(const fs::path& root){
  using ymcc::powerSource::channel::detail::Identity;
  check(ymcc::powerSource::channel::startPublisher(),"selected unknown case explicitly starts the real parent publisher");
  batteryFixtureSource(1);const auto ac=ymcc::powerSource::snapshot();
  check(ac.readable&&ac.known&&ac.ac==1&&ymcc::powerSource::channel::publish(ac),
    "selected parent publishes the current known AC receipt");
  Identity identity;check(ymcc::powerSource::channel::detail::identity(GetCurrentProcess(),identity),
    "selected publisher has an actual live process birth and Windows session");
  Queue queue(root);queue.enqueue(Json::array({target(713)}));queue.configure(true,"");queue.configureWallpaperPower(true);queue.resource("");
  const auto token=queue.start();check(!token.empty(),"attached boundary starts one existing queue session on known AC");
  check(queue.update(token,[](Json& state){auto& job=state["jobs"]["steam-713"];
    job["attempts"]=1;job["retryAt"]=123;job["automaticElapsedMs"]=4321;}),
    "attached boundary seeds existing nonzero task debt");
  const auto part=root/L"cache"/L"jobs"/L"game-media"/L"12345678901234567890123456789012.part";
  stormFile(part,"attached owned resumable bytes");auto sidecar=part;sidecar.replace_extension(L".resume.json");
  atomicWrite(sidecar,{{"resumeProtocol",1},{"validator","\"attached-owned-etag\""}});
  queue.trackTemporary(token,utf8(part.filename().wstring()),true);
  const auto job=queue.document()["jobs"]["steam-713"];const auto partBytes=stormBytes(part),sidecarBytes=stormBytes(sidecar);
  atomicWrite(root/L"attached-request.json",{{"fixture","attached-parent-unknown-v1"},{"root",utf8(root.wstring())},
    {"parentPid",identity.pid},{"parentBirth",identity.birth},{"session",token}});
  wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
  check(length>0&&length<32768,"attached child uses this exact fixture executable");
  Json resources={{"assigned",false},{"childExited",false},{"jobZeroBeforeClose",false},{"sourceJobReapRemaining",nullptr}};
  // Five seconds of ordered work plus SourceJob's original two-second cleanup.
  // No inherited handles, pipes, new worker host or external executable.
  std::string failure;
  try{
    SourceJob owned(nullptr);
    const auto deadline=GetTickCount64()+5000;
    const auto remaining=[&]{const auto now=GetTickCount64();return now<deadline?static_cast<DWORD>(deadline-now):DWORD{0};};
    const auto quote=[](const std::wstring &text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : text) {
      if (c == L'\\') {
        ++slashes;
        continue;
      }
      if (c == L'\"') {
        result.append(slashes * 2 + 1, L'\\');
        result += c;
        slashes = 0;
        continue;
      }
      result.append(slashes, L'\\');
      slashes = 0;
      result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
    };
    std::wstring command=quote(executable)+L" --battery-parent-unknown-child "+quote(root.wstring())+L" "+
      std::to_wstring(identity.pid)+L" "+std::to_wstring(identity.birth)+L" "+quote(wide(token));
    std::vector<wchar_t> line(command.begin(),command.end());line.push_back(0);
    STARTUPINFOW startup{sizeof startup};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION child{};
    check(CreateProcessW(executable,line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
      nullptr,nullptr,&startup,&child)!=FALSE,"create only the explicitly inert fixture child suspended");
    const bool assigned=owned.assign(child);resources["assigned"]=assigned;resources["childPid"]=child.dwProcessId;
    resources["childBirth"]=processBirthToken(child.dwProcessId);resources["parentPid"]=identity.pid;resources["parentBirth"]=identity.birth;
    check(assigned,"the real fixture child is assigned to the original SourceJob before resume");
    BOOL belongs=FALSE;check(IsProcessInJob(child.hProcess,owned.get(),&belongs)&&belongs,
      "the suspended actual child belongs to this exact owned Job");
    check(ResumeThread(child.hThread)!=DWORD(-1),"resume only the owned inert attached child");
    check(waitUntil([&]{return fs::exists(root/L"attached-child-ready.json")||WaitForSingleObject(child.hProcess,0)!=WAIT_TIMEOUT;},
      remaining()),"actual attached child reaches its ordered AC handshake");
    const auto ready=read(root/L"attached-child-ready.json");
    check(ready.value("attached",false)&&ready.value("directParent",false)&&ready.value("rootPid",DWORD{0})==identity.pid&&
      ready.value("rootBirth",uint64_t{0})==identity.birth&&ready.value("parentPid",DWORD{0})==identity.pid&&
      ready.value("parentBirth",uint64_t{0})==identity.birth&&ready.value("childPid",DWORD{0})==child.dwProcessId&&
      ready.value("childBirth",std::string{})==processBirthToken(child.dwProcessId)&&
      ready.value("session",std::string{})==token&&ready.value("sampleSequence",uint64_t{0})==ac.sampleSequence,
      "the real direct child retained this still-live parent birth and current AC-bound WorkerContext");
    batteryFixtureSource(-1);const auto unknown=ymcc::powerSource::snapshot();
    check(unknown.readable&&!unknown.known&&unknown.ac==-1&&unknown.status.ACLineStatus==255&&
      unknown.sampleSequence>ac.sampleSequence&&ymcc::powerSource::channel::publish(unknown),
      "same live parent explicitly publishes a readable unknown receipt to its existing mapping");
    atomicWrite(root/L"parent-unknown.json",{{"parentPid",identity.pid},{"parentBirth",identity.birth},{"session",token},
      {"sampleSequence",unknown.sampleSequence},{"revision",unknown.revision}});
    const auto waited=WaitForSingleObject(child.hProcess,remaining());resources["childExited"]=waited==WAIT_OBJECT_0;
    DWORD exitCode=STILL_ACTIVE;const bool exitKnown=GetExitCodeProcess(child.hProcess,&exitCode)!=FALSE;
    resources["childExitKnown"]=exitKnown;resources["childExitCode"]=exitCode;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    const bool zero=waited==WAIT_OBJECT_0&&waitUntil([&]{return QueryInformationJobObject(owned.get(),
      JobObjectBasicAccountingInformation,&accounting,sizeof accounting,nullptr)&&accounting.ActiveProcesses==0;},remaining());
    resources["jobZeroBeforeClose"]=zero;resources["activeProcessesBeforeClose"]=accounting.ActiveProcesses;
    resources["deadlineRemainingMs"]=remaining();atomicWrite(root/L"attached-resources-before-close.json",resources);
    check(waited==WAIT_OBJECT_0&&exitKnown&&exitCode==0,"attached actual Worker child exits naturally with its successful terminal receipt");
    check(zero,"assigned actual child exits and the exact Job is zero before its handles close");
  }catch(const std::exception& error){failure=error.what();resources["error"]=failure;}
  resources["sourceJobReapRemaining"]=SourceJob::reap();
  atomicWrite(root/L"attached-resources.json",resources);
  if(!failure.empty())throw std::runtime_error(failure);
  check(resources["sourceJobReapRemaining"]==0,"original SourceJob retains no unreturned owned fixture child");
  const auto result=read(root/L"attached-child-result.json");
  check(result.value("status",std::string{})=="PASS"&&result.value("workerExit",-1)==0&&
    result.value("parentAliveBeforeOwnerClose",false)&&result.value("ownerHandleClosed",false)&&
    result.value("networkAttempts",1u)==0&&result.value("fallbackPowerReads",1u)==0,
    "actual child separately confirms live parent, Worker pause, owner HANDLE close and zero network/fallback attempts");
  const auto paused=queue.document();atomicWrite(root/L"attached-parent-observation.json",
    {{"document",paused},{"childResult","attached-child-result.json"},{"resources",resources},
     {"parentPid",identity.pid},{"parentBirth",identity.birth},{"unknownSequence",ymcc::powerSource::snapshot().sampleSequence}});
  check(Queue::terminalReceipt(paused,token)&&paused.value("phase",std::string{})=="paused"&&
    paused.value("enabled",false)&&paused.value("wallpaperPauseOnBattery",false)&&paused["jobs"]["steam-713"]==job&&
    fs::exists(part)&&fs::exists(sidecar)&&stormBytes(part)==partBytes&&stormBytes(sidecar)==sidecarBytes,
    "parent rereads original paused Queue with ON, exact task debt and partial/sidecar bytes retained");
}

// Original Worker/Queue/Store paths; only WinHTTP and the source sample are ports.
static void workerTransferTimeoutObservation(const fs::path& root){
  MediaPreferenceFixtureState restore;
  batteryFixtureSource(1);powerPolicy::configure(true);
  Queue queue(root);queue.enqueue(Json::array({target(714)}));queue.configure(true,"");
  queue.configureWallpaperPower(true);queue.resource("");
  const auto token=queue.start();check(!token.empty(),"timeout Worker starts its isolated session");
  check(queue.update(token,[](Json& value){value["maxParallel"]=1;}),"select one Worker lane");
  const std::string source="https://cdn.akamai.steamstatic.com/steam/apps/714/owned.jpg";
  const auto response=Json{{"714",{{"success",true},{"data",{{"steam_appid",714},{"type","game"},
    {"name","Owned transfer timeout"},{"background_raw",source},{"movies",Json::array()}}}}}}.dump();
  Limits limits;limits.attempt=2000;
  unsigned transfers=0;
  mediaTransferFixture::begin(response,[&]{++transfers;Sleep(DWORD(limits.attempt)+40);});
  struct CloseTransport{~CloseTransport(){mediaTransferFixture::end();}} closeTransport;
  int exit=-1;{Worker worker(root,token,GetCurrentProcess(),{}, {}, limits,mediaSleepFixture::permit());exit=worker.run();}
  const auto row=queue.document()["jobs"]["steam-714"];
  const auto observation=row.value("lastTimeoutObservation",Json::object());
  check(exit==0&&transfers==1&&mediaTransferFixture::requests.load()==2&&mediaTransferFixture::unsupported.load()==0&&
    mediaTransferFixture::liveHandles()==0,"slow mocked transfer exits without more requests and releases its handles");
  check(row.value("lastError",std::string{})=="media-task-time-budget"&&row.value("attempts",0)==1&&
    observation.value("source",std::string{})=="worker"&&observation.value("workerStage",std::string{})=="transfer"&&
    observation.value("lastReportedPhase",std::string{})=="downloading", "real Worker preserves the timed-out transfer stage and its original charge");
  check(observation.dump().find("https:")==std::string::npos,"diagnostics retain no raw source");
  check(Queue::terminalReceipt(queue.snapshot(),token),"timeout Worker releases its session normally");
  mediaTransferFixture::end();

  Json cases=Json::array();
  for(const bool sessionAlsoEnds:{false,true}) {
    const auto label=sessionAlsoEnds?L"current-session-limit":L"expired-continuation";
    Queue serial(root/label);
    serial.enqueue(Json::array({target(714,2),target(715,1)}));
    serial.configure(true,"");serial.configureWallpaperPower(true);serial.resource("");
    const auto owner=serial.start();check(!owner.empty(),"serial Worker starts its isolated session");
    const auto wall=now(),earlier=wall+2500,whole=sessionAlsoEnds?earlier:wall+60000;
    check(serial.update(owner,[&](Json& state){
      // The legacy owner uses the production single-lane path. Queue load
      // normalizes maxParallel, so writing that field cannot select it.
      state["ownerProtocol"]=1;state["sessionIssuedAt"]=wall;state["sessionDeadlineAt"]=whole;
      auto& job=state["jobs"]["steam-714"];
      Json window={{"version",1},{"attemptIssuedAt",wall-1000},{"attemptDeadlineAt",wall+15000},
        {"chargedAt",wall-1000},{"sessionIssuedAt",wall-10000},{"sessionDeadlineAt",earlier}};
      for(const auto* key:{"requestBindingRevision","provenanceRevision","selectedVideo"})
        if(job.contains(key))window[key]=job[key];
      job["sharedSleepWindow"]=std::move(window);
    }),"first game resumes with an earlier session deadline");
    auto details=Json::parse(response);details["715"]={{"success",false}};
    unsigned blockedTransfers=0;
    mediaTransferFixture::begin(details.dump(),[&]{
      ++blockedTransfers;Sleep(static_cast<DWORD>((std::max)(int64_t{0},earlier-now())+40));
    });
    int serialExit=-1;
    {CloseTransport scope;Worker worker(serial.root(),owner,GetCurrentProcess(),{}, {}, {},mediaSleepFixture::permit());serialExit=worker.run();}
    const auto settled=serial.document();
    const auto first=settled["jobs"]["steam-714"],second=settled["jobs"]["steam-715"];
    const auto timeout=first.value("lastTimeoutObservation",Json::object());
    const auto requested=mediaTransferFixture::requests.load();
    const auto slots=settled.value("parallelSlots",0);
    const bool firstSettled=slots==1&&blockedTransfers==1&&first.value("attempts",0)==1&&
      first.value("lastError",std::string{})=="media-task-timeout"&&
      timeout.value("workerStage",std::string{})=="transfer"&&Queue::terminalReceipt(settled,owner)&&
      mediaTransferFixture::unsupported.load()==0&&mediaTransferFixture::liveHandles()==0;
    const bool nextSettled=serialExit==0&&requested>2&&stringField(second,"state")=="unavailable"&&
      stringField(second,"lastError")=="store-unavailable";
    const bool nextNeverClaimed=requested==2&&stringField(second,"state")=="pending"&&
      second.value("attempts",0)==0&&stringField(second,"lastError").empty();
    cases.push_back({{"wholeSessionExpired",sessionAlsoEnds},{"workerExit",serialExit},{"requests",requested},{"parallelSlots",slots},
      {"firstSettled",firstSettled},{"secondSettled",nextSettled},{"secondUntouched",nextNeverClaimed},
      {"secondState",stringField(second,"state")},{"pass",firstSettled&&(sessionAlsoEnds?nextNeverClaimed:nextSettled)}});
  }
  atomicWrite(root/L"lane-deadline-observations.json",cases);
  for(const auto& sample:cases)check(sample.value("pass",false),"a completed claim must not lend its deadline to the next game");
}

static void activeTransferPowerBoundary(const fs::path& root){
  MediaPreferenceFixtureState restore;
  batteryFixtureSource(1);powerPolicy::configure(true);
  Queue queue(root);queue.enqueue(Json::array({target(714)}));queue.configure(true,"");
  queue.configureWallpaperPower(true);queue.resource("");
  const auto token=queue.start();check(!token.empty(),"active transfer starts the actual queue session on AC");
  check(queue.update(token,[](Json& value){value["maxParallel"]=1;auto& job=value["jobs"]["steam-714"];
    job["attempts"]=1;job["retryAt"]=123;job["automaticElapsedMs"]=4321;}),
    "active transfer seeds existing debt and selects one original Worker lane");
  const auto originalJob=queue.document()["jobs"]["steam-714"];
  const std::string source="https://cdn.akamai.steamstatic.com/steam/apps/714/owned.jpg";
  const auto body=Json{{"714",{{"success",true},{"data",{{"steam_appid",714},{"type","game"},
    {"name","Owned actual transfer"},{"background_raw",source},{"movies",Json::array()}}}}}}.dump();
  auto context=std::make_shared<WorkerContext>(root);
  Json observation={{"status","FAIL"},{"scope","actual in-process Worker lane and disk transactions; controlled WinHTTP leaf and local source samples, no network or physical source"},
    {"session",token},{"originalJob",originalJob},{"assertions",Json::array()}};
  const auto expect=[&](bool condition,const char* message){
    observation["assertions"].push_back({{"passed",condition},{"assertion",message}});check(condition,message);
  };
  HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,GetCurrentProcessId());
  struct CloseParent{HANDLE& handle;~CloseParent(){if(handle)CloseHandle(handle);}}closeParent{parent};
  expect(parent!=nullptr,"active Worker retains an actual live owner handle");
  unsigned crossed=0;
  mediaTransferFixture::begin(body,[&]{
    ++crossed;const auto active=queue.document();observation["activeAtRead"]=active;
    observation["storeAtRead"]=Store(root).snapshot();
    expect(active.value("sessionClaimed",false)&&active.value("workerPid",DWORD{0})==GetCurrentProcessId()&&
      active["active"].contains("steam-714")&&active["jobs"]["steam-714"].value("state",std::string{})=="running",
      "the first asset read occurs after the real queue claim and original metadata request");
    const auto before=ymcc::powerSource::snapshot();
    batteryFixtureSource(0);batteryFixtureSource(1); // No Worker or policy consumer observes DC.
    const auto after=ymcc::powerSource::snapshot();
    observation["source"]={{"beforeSequence",before.sampleSequence},{"beforeRevision",before.revision},
      {"afterSequence",after.sampleSequence},{"afterRevision",after.revision},{"finalAc",after.ac}};
    expect(after.readable&&after.known&&after.ac==1&&after.revision>before.revision,
      "producer records missed DC and final AC during an already active asset read");
    // Keep the original one-second Worker check cadence. The in-flight read may
    // return one bounded chunk; the next original check must retire this owner.
    const auto began=GetTickCount64();Sleep(1100);
    observation["originalCheckCadenceWaitMs"]=GetTickCount64()-began;
  });
  struct TransportScope{~TransportScope(){mediaTransferFixture::end();}}transportScope;
  try{
    int workerExit=-1;{Worker worker(root,token,parent,context,{}, {},mediaSleepFixture::permit());workerExit=worker.run();}
    observation["workerExit"]=workerExit;observation["crossings"]=crossed;
    const auto paused=queue.document();observation["paused"]=paused;
    const auto store=Store(root).snapshot();observation["storeAfter"]=store;
    observation["transport"]={{"opens",mediaTransferFixture::opens.load()},{"requests",mediaTransferFixture::requests.load()},
      {"metadataReads",mediaTransferFixture::metadataReads.load()},{"assetReads",mediaTransferFixture::assetReads.load()},
      {"unsupported",mediaTransferFixture::unsupported.load()},{"liveHandles",mediaTransferFixture::liveHandles()}};
    expect(crossed==1&&mediaTransferFixture::requests.load()==2&&mediaTransferFixture::metadataReads.load()==2&&
      mediaTransferFixture::assetReads.load()==1&&mediaTransferFixture::unsupported.load()==0,
      "actual Worker opened metadata and one asset and performs no later read after missed DC");
    expect(workerExit==0&&Queue::terminalReceipt(paused,token)&&paused.value("phase",std::string{})=="paused"&&
      paused.value("reason",std::string{})=="download-paused"&&paused.value("enabled",false)&&
      paused.value("wallpaperPauseOnBattery",false)&&paused.value("workerPid",DWORD{1})==0&&
      paused.value("active",Json::object()).empty()&&paused.value("current",Json::object()).empty(),
      "active Worker retires through the existing power-pause terminal without losing ON");
    const auto job=paused["jobs"]["steam-714"];
    expect(job.value("state",std::string{})=="pending"&&paused.value("active",Json::object()).empty()&&
      job.value("attempts",0)==originalJob.value("attempts",0)&&job.value("retryAt",int64_t{0})==originalJob.value("retryAt",int64_t{0})&&
      job.value("automaticElapsedMs",int64_t{0})==originalJob.value("automaticElapsedMs",int64_t{0}),
      "active power retirement neither spends nor refunds queue retry and elapsed debt");
    const auto entry=store["entries"]["steam-714"];
    expect(entry.value("leaseUntil",int64_t{1})==0&&entry.value("assets",Json::object()).empty(),
      "original Store cancellation releases its lease without publishing the in-flight asset");
    const auto identity=std::string("steam-714\nstatic\n")+source;
    const auto hex=[](std::string value){return std::string(16-value.size(),'0')+value;};
    const auto id=hex(digestKey(identity))+hex(digestKey("resume:"+identity));
    const auto part=root/L"cache"/L"jobs"/L"game-media"/wide(id+".part");
    auto sidecar=part;sidecar.replace_extension(L".resume.json");
    const auto bytes=stormBytes(part),sidecarBytes=stormBytes(sidecar);
    const auto resume=read(sidecar);observation["retained"]={{"part",utf8(part.wstring())},
      {"partBytes",bytes.size()},{"sidecar",utf8(sidecar.wstring())},{"metadata",resume}};
    expect(bytes=="owned-inflight-chunk"&&resume.value("identity",std::string{})==identity&&
      resume.value("validator",std::string{})=="\"owned-active-transfer\""&&!resume.value("complete",true),
      "one already in-flight chunk and its valid incomplete resume metadata are retained");
    const bool claimDenied=queue.claim(token).is_null();
    const bool progressDenied=!queue.progress(token,"steam-714","late-active-transfer",1,1);
    queue.result(token,"steam-714",true);queue.result(token,"steam-714",false,"late-active-transfer");
    observation["afterLate"]=queue.document();
    expect(claimDenied&&progressDenied&&queue.document()["jobs"]["steam-714"]==job&&
      stormBytes(part)==bytes&&stormBytes(sidecar)==sidecarBytes,
      "old active work cannot claim, charge, complete or erase retained bytes on final AC");
    expect(mediaTransferFixture::liveHandles()==0&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT,
      "original Worker joined every lane and closed every controlled network handle with the owner still live");
    const bool closed=CloseHandle(parent)!=FALSE;if(closed)parent=nullptr;
    observation["ownerHandleClosed"]=closed;expect(closed,"active transfer closes its exact retained owner handle");
    observation["status"]="PASS";
    atomicWrite(root/L"active-transfer-result.json",observation);
  }catch(const std::exception& error){observation["error"]=error.what();
    observation["liveTransportHandlesAtFailure"]=mediaTransferFixture::liveHandles();
    atomicWrite(root/L"active-transfer-result.json",observation);throw;}
}

// Insert at file scope before batteryPauseProbe; invoke once in the existing
// real-dc-revokes-authority-claims-and-late-receipts case, after its ten checks.
// These are the original Queue/Store/Worker consumers. Only foreground Facts
// and pre-existing persisted retry debt are controlled; no replacement policy.
static void explicitDownloadOffOldOwnerBoundary(const fs::path& root) {
  const auto started=GetTickCount64();
  const auto initialChecks=checks;
  MediaPreferenceFixtureState restorePreference;
  ymcc::steamwallpaperForeground::Authority authority("download-off-owner");
  ymcc::steamwallpaperForeground::Facts facts;
  facts.known=facts.physical=facts.enabled=facts.ready=true;
  facts.pid=123;facts.birth=456;facts.window=789;
  facts.steamPid=123;facts.steamBirth=456;
  facts.modeEpoch="download-off-old";facts.modeRevision=1;facts.power=1;
  struct ReaderRestore {
    steamDemand::Reader previous;
    ReaderRestore(){std::lock_guard lock(steamDemand::readerMutex);previous=steamDemand::reader;}
    ~ReaderRestore(){steamDemand::setForegroundReader(std::move(previous));}
  } restoreReader;
  // Ownership is important: Store/Queue consume durable preferences. An absent
  // replica must not disable the policy between the two real Worker contexts.
  powerPolicy::own(true);
  steamDemand::setForegroundReader([&]{return authority.observe(facts,authority.ticket());});
  Queue queue(root);
  queue.configureWallpaperPower(true);
  Store store(root);
  const std::string key="steam-715";
  const auto durable=root/L"config"/L"game-media-downloads.json";
  const auto progress=root/L"config"/L"game-media-download-progress.json";
  const auto media=root/L"config"/L"game-media.json";
  const auto networkBefore=deniedNetworkAttempts.load();
  const auto powerReadsBefore=batteryPowerReads.load();
  Json observations=Json::array();
  std::string stage="setup";
  struct ParentHandle {
    HANDLE value=nullptr;
    ~ParentHandle(){if(value)CloseHandle(value);}
  } parent;
  bool parentClosed=false;
  const auto rawState=[&] {
    Json files=Json::array();
    for(const auto& path:{durable,progress,media}){
      std::error_code error;
      const bool exists=fs::exists(custom_steam_library::ioPath(path),error);
      files.push_back({{"path",utf8(path.wstring())},{"exists",exists},
        {"observationError",error.value()},{"raw",stormBytes(custom_steam_library::ioPath(path))}});
    }
    Json observed={{"files",files},{"networkAttempts",deniedNetworkAttempts.load()},
      {"fallbackPowerReads",batteryPowerReads.load()},{"elapsedMs",GetTickCount64()-started}};
    // Failed/unreadable product observations remain explicit, never a proof.
    try{observed["queue"]=queue.document();}catch(const std::exception& e){observed["queueError"]=e.what();}
    try{observed["progress"]=queue.snapshot();}catch(const std::exception& e){observed["progressError"]=e.what();}
    try{observed["store"]=store.snapshot();}catch(const std::exception& e){observed["storeError"]=e.what();}
    return observed;
  };
  const auto observe=[&](const char* operation) {
    observations.push_back({{"operation",operation},{"state",rawState()}});
  };
  try {
    const auto source=ymcc::powerSource::snapshot();
    check(source.readable&&source.known&&source.ac==1&&source.status.SystemStatusFlag==0,
      "download OFF boundary requires the original known AC producer receipt");
    const auto oldProof=steamDemand::foreground();
    Json oldDemand={{"demandConsumer","steam"},{"demandEpoch",oldProof["ownerEpoch"]},
      {"demandRevision",oldProof["revision"]}};
    check(oldProof.value("current",false)&&steamDemand::allowed(oldDemand),
      "old queued inventory has real native foreground admission on controlled facts");
    queue.enqueue(Json::array({target(715)}),{},oldDemand);
    queue.configure(true,"");queue.resource("");
    const auto oldSession=queue.start();
    check(!oldSession.empty(),"old queued session starts through the original Queue");
    const auto birth=processBirthToken(GetCurrentProcessId());
    parent.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,GetCurrentProcessId());
    check(parent.value!=nullptr&&!birth.empty()&&WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT,
      "OFF boundary retains this still-live process HANDLE and actual birth token");
    auto oldContext=std::make_shared<WorkerContext>(root);
    Worker oldWorker(root,oldSession,parent.value,oldContext,{}, {},mediaSleepFixture::permit());
    // Exact public handshake used by Worker::run; never fabricate a dead PID,
    // patch a private member, or bypass a future WorkerStart receipt requirement.
    check(queue.update(oldSession,[&](Json& state){
      state["workerPid"]=GetCurrentProcessId();state["workerBirth"]=birth;
    }),"old session accepts the original public PID/birth handshake");
    const auto oldClaim=queue.claim(oldSession);
    check(oldClaim.is_object()&&stringField(oldClaim,"key")==key&&
      stringField(oldClaim,"session")==oldSession,
      "old owner has an actual Queue claim before its legitimate terminal");
    auto oldRequest=oldClaim;oldRequest["demandConsumer"]="background";
    const auto oldLease=store.begin(oldRequest);
    check(oldLease.value("acquired",false)&&stringField(oldLease,"demandConsumer")=="background"&&
      !stringField(oldLease,"taskId").empty(),
      "old claim acquires the same background Store lease used by Worker::prepare");
    observe("old-owner-at-AC");

    stage="legitimate-old-terminal";
    store.cancel(oldLease);
    check(stringField(store.snapshot()["entries"][key],"taskId").empty(),
      "exact old Store cancellation returns its lease before replacement");
    // Controlled existing persisted debt, as in the enclosing original case.
    // Public update/save still owns queue serialization and admission checks.
    check(queue.update(oldSession,[&](Json& state){auto& job=state["jobs"][key];
      job["attempts"]=1;job["retryAt"]=123;job["automaticElapsedMs"]=3210;
    }),"seed nonzero queue debt without changing owner or real deadlines");
    queue.end(oldSession,"deferred");
    check(Queue::terminalReceipt(queue.document(),oldSession),
      "old Queue session has the original complete terminal receipt");
    auto stored=store.snapshot();stored["entries"][key]["attempts"]=2;
    stored["entries"][key]["retryAt"]=123;atomicWrite(media,stored);
    observe("legitimate-old-terminal");

    stage="current-owner-positive";
    authority.revoke();facts.modeEpoch="download-off-current";facts.modeRevision=2;
    const auto currentProof=steamDemand::foreground();
    Json currentDemand={{"demandConsumer","steam"},{"demandEpoch",currentProof["ownerEpoch"]},
      {"demandRevision",currentProof["revision"]}};
    check(currentProof.value("current",false)&&currentProof["ownerEpoch"]!=oldProof["ownerEpoch"]&&
      currentProof["revision"]!=oldProof["revision"]&&steamDemand::allowed(currentDemand)&&
      !steamDemand::allowed(oldDemand),
      "native Authority admits a fresh foreground proof and denies the old proof");
    queue.enqueue(Json::array({target(715)}),{},currentDemand);
    const auto currentSession=queue.start();
    check(!currentSession.empty()&&currentSession!=oldSession,
      "original Queue admits a distinct current session after the real terminal");
    check(queue.update(currentSession,[&](Json& state){
      state["workerPid"]=GetCurrentProcessId();state["workerBirth"]=birth;
    }),"current session satisfies the original public worker handshake");
    const auto currentClaim=queue.claim(currentSession);
    check(currentClaim.is_object()&&stringField(currentClaim,"key")==key&&
      stringField(currentClaim,"session")==currentSession&&queue.allowed(currentSession),
      "current owner really claims the same job before explicit OFF");
    auto currentRequest=currentClaim;currentRequest["demandConsumer"]="background";
    const auto currentLease=store.begin(currentRequest);
    check(currentLease.value("acquired",false)&&currentLease["taskId"]!=oldLease["taskId"]&&
      stringField(currentLease,"demandConsumer")=="background"&&
      currentLease.value("leasePid",DWORD{0})==GetCurrentProcessId()&&
      stringField(currentLease,"leaseBirth")==birth&&currentLease.value("leaseUntil",int64_t{0})>now(),
      "new Queue claim owns a real distinct live Store lease, not a foreground-only substitute");
    check(queue.progress(currentSession,key,"downloading",17,63),
      "current owner progress is accepted before OFF to reject an always-deny fixture");
    auto currentContext=std::make_shared<WorkerContext>(root);
    Worker currentWorker(root,currentSession,parent.value,currentContext,{}, {},mediaSleepFixture::permit());
    check(!oldContext->cancelled.load()&&!currentContext->cancelled.load()&&
      oldContext->failure.empty()&&currentContext->failure.empty()&&
      powerPolicy::current(oldContext->powerTicket)&&powerPolicy::current(currentContext->powerTicket),
      "both retained contexts stay healthy on AC so OFF is not hidden by stale power");
    const auto debtBefore=queue.document()["jobs"][key];
    const auto leaseBefore=store.snapshot()["entries"][key];
    check(debtBefore.value("attempts",0)==1&&debtBefore.value("retryAt",int64_t{0})==123&&
      debtBefore.value("automaticElapsedMs",int64_t{0})==3210&&
      leaseBefore.value("attempts",0)==2&&leaseBefore.value("retryAt",int64_t{0})==123,
      "the current positive owner retains all seeded retry and elapsed debt");
    atomicWrite(root/L"user-off-before.json",{{"stage",stage},{"state",rawState()},
      {"oldSession",oldSession},{"currentSession",currentSession},
      {"oldClaim",oldClaim},{"currentClaim",currentClaim},{"oldLease",oldLease},
      {"currentLease",currentLease},{"oldProof",oldProof},{"currentProof",currentProof}});

    stage="explicit-download-OFF";
    queue.configure(false,"");
    const auto offDocument=queue.document();
    const auto offStore=store.snapshot();
    const auto offQueueRaw=stormBytes(custom_steam_library::ioPath(durable));
    const auto offProgressRaw=stormBytes(custom_steam_library::ioPath(progress));
    const auto offMediaRaw=stormBytes(custom_steam_library::ioPath(media));
    check(!offDocument.value("enabled",true)&&offDocument.value("manualUntil",int64_t{0})==0&&
      stringField(offDocument,"session")==currentSession&&offDocument["active"].contains(key)&&
      offDocument["jobs"][key]==debtBefore&&offStore["entries"][key]==leaseBefore,
      "explicit product download OFF preserves the current owner and its debt");
    check(powerPolicy::current(oldContext->powerTicket)&&steamDemand::allowed(currentDemand)&&
      !queue.allowed(currentSession)&&!queue.allowed(oldSession),
      "download OFF alone closes Queue admission while AC and current foreground stay valid");
    observe("explicit-download-OFF");
    const auto unchanged=[&](const char* operation) {
      observe(operation);
      check(queue.document()==offDocument&&store.snapshot()==offStore&&
        stormBytes(custom_steam_library::ioPath(durable))==offQueueRaw&&
        stormBytes(custom_steam_library::ioPath(progress))==offProgressRaw&&
        stormBytes(custom_steam_library::ioPath(media))==offMediaRaw&&
        deniedNetworkAttempts.load()==networkBefore&&batteryPowerReads.load()==powerReadsBefore,
        operation);
    };
    stage="late-old-owner";
    const auto oldExit=oldWorker.run();observe("old-Worker-run-return");
    check(oldExit==0,"old actual Worker exits through its original graceful cancelled path after OFF");
    unchanged("old Worker cannot release, spend, refund or rewrite the current OFF owner");
    check(queue.claim(oldSession).is_null(),"OFF rejects a late actual old-session claim");
    unchanged("late old claim preserves exact OFF queue, progress and Store bytes");
    check(!queue.progress(oldSession,key,"downloading",999,1000),"OFF rejects late old-session progress");
    unchanged("late old progress cannot overwrite current bytes or restart work");
    queue.result(oldSession,key,true);
    unchanged("late old success cannot complete, refund or release the new owner");
    queue.result(oldSession,key,false,"late-old-failure");
    unchanged("late old failure cannot spend current attempts or retry debt");
    queue.end(oldSession,"completed");
    unchanged("late old terminal cannot return the new session");
    store.finish(oldLease);
    unchanged("late old Store finish cannot return or reset the new lease");
    store.fail(oldLease);
    unchanged("late old Store failure cannot charge the new lease");
    const auto publication=root/L"late-old.mp4";
    stormFile(publication,std::string("\0\0\0\x18" "ftypisom",12));
    bool publicationRejected=false;std::string publicationError;
    try{store.publish(oldLease,publication,false);}
    catch(const std::exception& e){publicationRejected=true;publicationError=e.what();}
    observations.push_back({{"operation","late-old-publish-outcome"},
      {"rejected",publicationRejected},{"error",publicationError}});
    check(publicationRejected&&publicationError=="media-stale-publication",
      "valid local media is refused by the original old-task publication fence");
    unchanged("late old Store publish cannot change assets, ON or new-owner debt");
    store.cancel(oldLease);
    unchanged("late old Store cancel cannot release the new owner");
    check(queue.claim(currentSession).is_null()&&
      !queue.progress(currentSession,key,"downloading",888,1000)&&queue.start().empty(),
      "OFF denies current claims, current progress and a new automatic session");
    unchanged("OFF admission probes do not replay or rewrite the current owner");

    // One small healthy-context control isolates OFF itself from old-token
    // rejection. Only this exact current Worker may publish its own terminal.
    stage="current-Worker-OFF-terminal";
    const auto currentExit=currentWorker.run();
    const auto terminal=queue.document();observe("current-Worker-OFF-terminal");
    auto expectedJob=debtBefore;expectedJob["state"]="pending";
    check(currentExit==0&&Queue::terminalReceipt(terminal,currentSession)&&
      stringField(terminal,"phase")=="deferred"&&stringField(terminal,"reason")=="media-download-cancelled"&&
      !terminal.value("enabled",true)&&terminal["jobs"][key]==expectedJob&&
      store.snapshot()==offStore&&stormBytes(custom_steam_library::ioPath(media))==offMediaRaw,
      "only current actual Worker settles its own OFF session without consuming or releasing Store debt");
    check(deniedNetworkAttempts.load()==networkBefore&&batteryPowerReads.load()==powerReadsBefore&&
      powerPolicy::current(currentContext->powerTicket),
      "both actual OFF Worker runs start zero network attempts and no fallback power query");
    parentClosed=CloseHandle(parent.value)!=FALSE;
    if(parentClosed)parent.value=nullptr;
    check(parentClosed,"OFF boundary returns its owned self-process HANDLE");
    atomicWrite(root/L"user-off-after.json",{{"ok",true},{"stage",stage},
      {"observations",observations},{"final",rawState()},{"checks",checks-initialChecks},
      {"networkAttemptsBefore",networkBefore},{"networkAttemptsAfter",deniedNetworkAttempts.load()},
      {"fallbackPowerReadsBefore",powerReadsBefore},{"parentHandleClosed",parentClosed},
      {"scope","actual in-process Queue/Store/Worker; controlled foreground facts and saved debt"},
      {"foregroundPhysicalObservation",false},{"activeHTTP",false},{"childOrJobStarted",false},
      {"installedOrPhysicalProof",false}});
  } catch(const std::exception& error) {
    atomicWrite(root/L"user-off-after.json",{{"ok",false},{"stage",stage},{"error",error.what()},
      {"observations",observations},{"final",rawState()},{"checks",checks-initialChecks},
      {"networkAttemptsBefore",networkBefore},{"networkAttemptsAfter",deniedNetworkAttempts.load()},
      {"parentHandleClosedBeforeCatch",parentClosed},{"remainingHandleCleanup","RAII on unwind; not observed here"},
      {"scope","failed actual-consumer boundary; no synthetic acceptance"},
      {"installedOrPhysicalProof",false}});
    throw;
  }
}

// Original Store/Queue/Authority/Server/Prewarmer and owned Supervisor process.
// Only Windows' power sample and the existing child workload are fixture IO.
static int batteryPauseProbe(const fs::path& base,const std::string& only={},const std::string& boundary={}){
  if(!boundary.empty()&&(only!="real-dc-revokes-authority-claims-and-late-receipts"||boundary!="active-transfer"))
    throw std::runtime_error("unknown battery case boundary");
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
    if(boundary=="active-transfer"){activeTransferPowerBoundary(root/L"active-transfer");return;}
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
    explicitDownloadOffOldOwnerBoundary(root/L"explicit-download-off");
    steamDemand::setForegroundReader({});
    activeTransferPowerBoundary(root/L"active-transfer");
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
    auto context=std::make_shared<WorkerContext>(queued.root());Worker retained(queued.root(),session,parent,context,{}, {},mediaSleepFixture::permit());
    batteryFixtureSource(0);batteryFixtureSource(1); // No consumer observes the intermediate DC.
    check(retained.run()==0,"old worker returns through original graceful power pause");
    const auto paused=queued.document();atomicWrite(root/L"worker-fence-observation.json",{{"document",paused},{"sourceRevision",ymcc::powerSource::snapshot().revision}});
    check(paused.value("phase",std::string{})=="paused"&&paused.value("enabled",false),"missed DC cannot resume an old worker on final AC");
    const auto originalJob=paused["jobs"]["steam-712"];
    check(originalJob.value("attempts",0)==0&&originalJob.value("state",std::string{})=="pending","stale worker must not consume or charge the queued task");
    check(!queued.allowed(session)&&queued.claim(session).is_null(),"old session cannot regain admission or claim on final AC");
    check(!queued.progress(session,"late-progress",1,1),"old session cannot republish progress after returning ownership");
    queued.result(session,"steam-712",true);check(queued.document()["jobs"]["steam-712"]==originalJob,"late stale worker result cannot complete the task after AC");
    attachedParentUnknownBoundary(root/L"attached-parent-unknown");
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
  if(!boundary.empty()){result["selectedBoundary"]=boundary;result["priorCaseBoundariesNotRun"]=Json::array({"foreground-proof-and-old-lease","explicit-download-off"});}
  atomicWrite(base/L"battery-pause-result.json",result);std::cout<<result.dump()<<std::endl;return ok?0:1;
}

static void originalChildModes(const fs::path& base,const fs::path& self,const std::string& selected={}){
    for (const auto *mode : {"stall", "crash", "preclaim-crash", "unsupported",
                             "complete", "production"}) {
      if(!selected.empty()&&selected!=mode)continue;
      const auto root=std::string_view(mode)=="production" ? self.parent_path()/L"CustomSteamLibrary" : base/wide(mode);
      if(fs::exists(root))throw std::runtime_error("fixture child root must be fresh");
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      atomicWrite(root / L"fixture.json", {{"mode", mode}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      fs::copy_file(self, root / L"CustomSteamLibrary.exe");
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol",
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
}

int wmain(int argc, wchar_t **argv) {
  try {
    mediaSleepFixture::bind();
    if(argc==4&&std::wstring_view(argv[1])==L"--sleep-child-case"){
      const auto selected=utf8(argv[3]);
      if(selected!="production"&&selected!="complete"&&selected!="unsupported")throw std::runtime_error("unknown media sleep child case");
      const auto base=fs::absolute(argv[2]);
      if(fs::exists(base))throw std::runtime_error("fixture root must be fresh");
      fs::create_directories(base);
      batteryFixtureSource(1);
      check(ymcc::powerSource::channel::startPublisher(),"owned parent power channel starts");
      check(ymcc::powerSource::channel::publish(ymcc::powerSource::snapshot()),"owned parent publishes fixed AC receipt");
      wchar_t self[32768]{};
      check(GetModuleFileNameW(nullptr,self,32768)>0,"owned test parent image known");
      originalChildModes(base,fs::path(self),selected);
      check(SourceJob::reap()==0&&mediaSleepFixture::runtime().provider().outstanding()==0&&
            mediaSleepFixture::runtime().provider().exitedConsumerDebtCount()==0,"original child and public power lease returned");
      mediaSleepFixture::runtime().finishStop();
      std::cout<<Json({{"case",selected},{"checks",checks}}).dump()<<std::endl;
      return 0;
    }
    if(argc>1&&std::wstring_view(argv[1])==L"--battery-parent-unknown-child")
      return attachedParentUnknownChild(argc,argv);
    if(argc>1&&std::wstring_view(argv[1])==L"--battery-pause"){
      if(argc!=3)throw std::runtime_error("battery-pause requires exactly one evidence root");
      return batteryPauseProbe(fs::absolute(argv[2]));
    }
    if(argc>1&&std::wstring_view(argv[1])==L"--battery-pause-case"){
      if((argc!=4&&argc!=6)||!argv[3][0])throw std::runtime_error("battery-pause-case requires an evidence root and exact nonempty case");
      if(argc==6){if(std::wstring_view(argv[4])!=L"--boundary"||!argv[5][0])throw std::runtime_error("invalid battery case boundary arguments");
        return batteryPauseProbe(fs::absolute(argv[2]),utf8(argv[3]),utf8(argv[5]));}
      return batteryPauseProbe(fs::absolute(argv[2]),utf8(argv[3]));
    }
    if(argc==4&&std::wstring_view(argv[1])==L"--storm-case"){if(!argv[3][0])throw std::runtime_error("explicit storm selection must be nonempty");return queueStorm(fs::absolute(argv[2]),utf8(argv[3]));}
    if(argc==4&&std::wstring(argv[1])==L"--resource-cost")return queueStorm(fs::absolute(argv[2]),"resource-publication-fixed-inventory-cost",(std::clamp)(_wtoi(argv[3]),6,512)&~1);
    if(argc==3&&std::wstring_view(argv[1])==L"--storm")return queueStorm(fs::absolute(argv[2]));
    if (argc > 1 && std::wstring_view(argv[1]) == L"--media-background")
      return fakeChild(argc, argv);
    if(argc!=1)throw std::runtime_error("unknown selftest arguments");
    // The production child reads its exact parent's shared source receipt.
    // Supply the root fixture producer rather than a child-only power fallback.
    batteryFixtureSource(1);
    check(ymcc::powerSource::channel::startPublisher(),"owned parent power channel starts");
    check(ymcc::powerSource::channel::publish(ymcc::powerSource::snapshot()),"owned parent publishes the fixed AC receipt");
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
    originalChildModes(base,fs::path(self));
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
      atomicWrite(root/L"package-manifest.json",{{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol",1}});
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
                  {{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol", 1}});
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
      atomicWrite(root/L"package-manifest.json",{{"backgroundSharedSleepProtocol",1},{"backgroundSteamSourceProtocol",1},{"backgroundMediaProtocol",1}});
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
