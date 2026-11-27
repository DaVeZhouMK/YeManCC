#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <functional>
#include <string_view>
#include <tuple>
struct DigestCalls {unsigned opens=0,closes=0,hashes=0,bundledManifestProbes=0;};
static thread_local DigestCalls digestCalls;
static thread_local std::wstring_view countedManifest;
static thread_local DWORD refusedRootReopen=0;
static thread_local unsigned rootReopenAttempts=0;
static HANDLE WINAPI resourceFixtureOpenProcess(DWORD access,BOOL inherit,DWORD pid){
 if(refusedRootReopen&&pid==refusedRootReopen){++rootReopenAttempts;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
 return ::OpenProcess(access,inherit,pid);
}
static DWORD WINAPI resourceFixtureAttributes(LPCWSTR path){
 if(path&&!countedManifest.empty()&&countedManifest==path)++digestCalls.bundledManifestProbes;
 return ::GetFileAttributesW(path);
}
static NTSTATUS WINAPI resourceFixtureOpen(BCRYPT_ALG_HANDLE* algorithm,LPCWSTR name,LPCWSTR implementation,ULONG flags){
 ++digestCalls.opens;return ::BCryptOpenAlgorithmProvider(algorithm,name,implementation,flags);
}
static NTSTATUS WINAPI resourceFixtureClose(BCRYPT_ALG_HANDLE algorithm,ULONG flags){
 const auto status=::BCryptCloseAlgorithmProvider(algorithm,flags);if(status>=0)++digestCalls.closes;return status;
}
static NTSTATUS WINAPI resourceFixtureHash(BCRYPT_ALG_HANDLE algorithm,BCRYPT_HASH_HANDLE* hash,PUCHAR object,
        ULONG objectSize,PUCHAR secret,ULONG secretSize,ULONG flags){
 ++digestCalls.hashes;return ::BCryptCreateHash(algorithm,hash,object,objectSize,secret,secretSize,flags);
}
// Only the fixture's WinSock terminal selects an ephemeral loopback port.
// Keep production PortLease and all real ownership/busy checks unchanged.
static int WSAAPI resourceFixtureBind(SOCKET socket,const sockaddr* value,int length){
 if(value&&length==sizeof(sockaddr_in)&&value->sa_family==AF_INET){
  auto address=*reinterpret_cast<const sockaddr_in*>(value);
  if(ntohs(address.sin_port)==1337&&address.sin_addr.s_addr==htonl(INADDR_ANY)){
   address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
   return ::bind(socket,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
  }
 }
 return ::bind(socket,value,length);
}
#define bind resourceFixtureBind
#define BCryptOpenAlgorithmProvider resourceFixtureOpen
#define BCryptCloseAlgorithmProvider resourceFixtureClose
#define BCryptCreateHash resourceFixtureHash
#define GetFileAttributesW resourceFixtureAttributes
#define OpenProcess resourceFixtureOpenProcess
#include "../native/decky_binding_worker_entry.h"
#undef OpenProcess
#undef GetFileAttributesW
#undef BCryptOpenAlgorithmProvider
#undef BCryptCloseAlgorithmProvider
#undef BCryptCreateHash
#include "../native/decky_sidebar_runtime.h"
#include "../native/decky_sidebar_diagnostics.h"
#undef bind
#include <condition_variable>
#include <iostream>
#include <thread>
using Json=nlohmann::json;
namespace fs=std::filesystem;
using namespace ymcc::deckyresources;
static void need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class Config,class Callback>static void installPreflight(Config& config,Callback run){
    config.preflight=[run]{return run([]{return true;});};
    if constexpr(requires{config.scopedPreflight=run;})config.scopedPreflight=run;
}
static void put(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;need(out.good(),"fixture write failed");}
static std::wstring env(const wchar_t* name){wchar_t value[32768]{};const auto n=GetEnvironmentVariableW(name,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring{};}
static bool childPeer(int& exit){
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);const auto mode=ymcc::deckybinding::ChildChannel::classify(argc,argv);if(mode==ymcc::deckybinding::ChildMode::ordinary){LocalFree(argv);return false;}
    const auto behavior=env(L"YMCC_RESOURCE_WORKER_FIXTURE");if(behavior.empty()){LocalFree(argv);return ymcc::deckybinding::entry::tryRunCommandLine(GetCommandLineW(),exit);}
    ymcc::deckybinding::ChildChannel channel;const auto opened=channel.openArguments(argc,argv);LocalFree(argv);if(opened!=ymcc::deckybinding::ChildMode::worker){exit=91;return true;}
    try{
        const auto frame=channel.readFrame(3000);need(static_cast<bool>(frame),"fixture request missing");const auto request=Json::parse(frame.payload);
        const auto nonce=ymcc::deckybinding::worker::wide(request.at("home"));put(fs::path(nonce)/L"fixture-child-started",std::to_string(GetCurrentProcessId()));
        if(behavior==L"digest-count"){
          const auto bundledManifest=ymcc::deckybinding::worker::wide(request.at("manifest"));
          countedManifest=bundledManifest;
          const auto handled=worker::tryRun(request,channel.cancelEvent(),[&](const Json& packet){
            if(packet.contains("detail")){
                const Json counts={{"providerOpens",digestCalls.opens},{"providerCloses",digestCalls.closes},{"hashStates",digestCalls.hashes},
                    {"bundledManifestAttributeProbes",digestCalls.bundledManifestProbes}};
                put(fs::path(nonce)/L"fixture-digest-count.json",counts.dump());
            }
            const auto bytes=packet.dump();return bool(channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250));
          },exit);
          countedManifest={};return handled;
        }
        if(behavior==L"receipt-changed-before-package-check"){
            return worker::tryRun(request,channel.cancelEvent(),[&](const Json& packet){
                if(packet.contains("progress")&&packet["progress"].value("inspectStep",std::string{})=="plugin-package"){
                    const auto home=fs::path(nonce);
                    put(home/L".ymcc-managed.receipt","{}");
                    put(home/ymcc::deckysetup::managedFiles[2],"corrupt-during-update");
                }
                const auto bytes=packet.dump();return bool(channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250));
            },exit);
        }
        if(behavior.starts_with(L"progress-optional-memory-")||behavior.starts_with(L"progress-optional-return-")){
            const bool afterWrite=behavior==L"progress-optional-memory-after-inspect"||behavior==L"progress-optional-return-after-inspect"||behavior==L"progress-optional-memory-after-cache";
            const bool returnFailure=behavior.starts_with(L"progress-optional-return-");
            if(behavior==L"progress-optional-memory-void-inspect")return worker::tryRun(request,channel.cancelEvent(),[&](const Json& packet)->void{
                if(packet.contains("progress")&&packet["progress"].value("stage",std::string{})=="inspect")throw std::bad_alloc{};
                const auto bytes=packet.dump();need(bool(channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250)),"optional-progress-void-emit");
            },exit);
            const auto lostStage=behavior==L"progress-optional-memory-receipt"?"receipt":behavior==L"progress-optional-memory-reinspect"?"reinspect":(behavior==L"progress-optional-memory-cache"||behavior==L"progress-optional-memory-after-cache")?"cache":"inspect";
            return worker::tryRun(request,channel.cancelEvent(),[&](const Json& packet){
                const bool substep=behavior==L"progress-optional-return-package";
                const bool cacheCheck=behavior==L"progress-optional-return-cache-verify";
                const bool selected=packet.contains("progress")&&(cacheCheck?packet["progress"].value("cacheStep",std::string{})=="verify":
                    substep?packet["progress"].value("inspectStep",std::string{})=="plugin-package":packet["progress"].value("stage",std::string{})==lostStage);
                if(selected&&!afterWrite){if(returnFailure)return false;throw std::bad_alloc{};} // Before any bytes of this optional note.
                const auto bytes=packet.dump();const auto sent=channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250);
                if(selected&&afterWrite){if(returnFailure)return false;throw std::bad_alloc{};} // Already-delivered prefix is still valid.
                return static_cast<bool>(sent);
            },exit);
        }
        if(behavior==L"progress-stall"||behavior==L"inspect-stall"||behavior==L"loader-open-stall"||behavior==L"loader-read-stall"||behavior==L"cache-verify-stall"){
            const auto* inspectStep=behavior==L"loader-open-stall"?"loader-open":behavior==L"loader-read-stall"?"loader-read":"plugin-package";
            return worker::tryRun(request,channel.cancelEvent(),[&](const Json& packet){
                const auto bytes=packet.dump();const auto written=channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250);
                if(packet.contains("progress")&&(behavior==L"cache-verify-stall"?packet["progress"].value("cacheStep",std::string{})=="verify":
                    behavior==L"progress-stall"?packet["progress"].value("stage",std::string{})=="cache":packet["progress"].value("inspectStep",std::string{})==inspectStep)){
                    put(fs::path(nonce)/L"fixture-cache-stage-started","entered");
                    std::mutex mutex;std::condition_variable wait;std::unique_lock lock(mutex);wait.wait(lock,[]{return false;});
                }
                return static_cast<bool>(written);
            },exit);
        }
        if(behavior.starts_with(L"progress-invalid-")){
            Json note={{"schema",worker::schema},{"id",request.at("id")},{"generation",request.at("generation")},{"progress",{{"stage","receipt"},{"elapsedMs",0u}}}};
            if(behavior==L"progress-invalid-order")note["progress"]["stage"]="cache";
            if(behavior==L"progress-invalid-type")note["progress"]["elapsedMs"]=-1;
            if(behavior==L"progress-invalid-secret")note["progress"]["stage"]="secret-path";
            if(behavior==L"progress-invalid-extra")note["progress"]["ready"]=true;
            if(behavior==L"progress-invalid-inspect-order"){
                for(const auto* stage:{"receipt","inspect"}){
                    note["progress"]["stage"]=stage;
                    const auto prefix=note.dump();
                    need(bool(channel.writeFrame(std::vector<std::uint8_t>(prefix.begin(),prefix.end()),250)),"invalid inspect control prefix");
                }
                note["progress"]={{"inspectStep","plugin-package"},{"elapsedMs",0u}};
            }
            if(behavior==L"progress-invalid-cache-order"||behavior==L"progress-invalid-cache-repeat"){
                for(const auto* stage:{"receipt","inspect","cache"}){
                    note["progress"]["stage"]=stage;
                    const auto prefix=note.dump();
                    need(bool(channel.writeFrame(std::vector<std::uint8_t>(prefix.begin(),prefix.end()),250)),"invalid cache control prefix");
                }
                if(behavior==L"progress-invalid-cache-repeat"){
                    note["progress"]={{"cacheStep","verify"},{"elapsedMs",0u}};
                    const auto prefix=note.dump();
                    need(bool(channel.writeFrame(std::vector<std::uint8_t>(prefix.begin(),prefix.end()),250)),"invalid cache repeat prefix");
                }
                else note["progress"]={{"cacheStep","seed"},{"elapsedMs",0u}};
            }
            auto bytes=note.dump();
            if(behavior==L"progress-invalid-duplicate")bytes.insert(1,"\"progress\":{\"stage\":\"receipt\",\"elapsedMs\":0},");
            channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250);
            if(behavior==L"progress-invalid-repeat")channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),250);
            exit=0;return true;
        }
        if(behavior==L"eof-held-endpoint"){
            using ymcc::deckybinding::detail::Handle;
            Handle pipe,cancel;
            need(DuplicateHandle(GetCurrentProcess(),channel.pipeHandle(),GetCurrentProcess(),&pipe.value,0,TRUE,DUPLICATE_SAME_ACCESS)!=FALSE,"held-pipe-copy");
            need(DuplicateHandle(GetCurrentProcess(),channel.cancelEvent(),GetCurrentProcess(),&cancel.value,0,TRUE,DUPLICATE_SAME_ACCESS)!=FALSE,"held-cancel-copy");
            HANDLE inherited[]{pipe.value,cancel.value};ymcc::deckybinding::detail::Attributes attributes;need(attributes.initialize(inherited),"held-endpoint-attributes");
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;startup.lpAttributeList=attributes.list;
            const auto image=ymcc::deckybinding::detail::executable();
            std::wstring command=L"\""+image+L"\" --fixture-resource-held-endpoint "+std::to_wstring(reinterpret_cast<UINT_PTR>(pipe.value))+L" "+std::to_wstring(reinterpret_cast<UINT_PTR>(cancel.value));
            PROCESS_INFORMATION process{};need(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,nullptr,&startup.StartupInfo,&process)!=FALSE,"held-endpoint-start");
            Handle leaf{process.hProcess},thread{process.hThread};
            const Json proof={{"root",ymcc::deckybinding::worker::contextIdentity(GetCurrentProcess())},{"leaf",ymcc::deckybinding::worker::contextIdentity(leaf.value)}};
            put(fs::path(nonce)/L"fixture-held-endpoint.json",proof.dump());
            Handle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,env(L"YMCC_RESOURCE_EXIT_READY").c_str())};
            Handle acknowledge{OpenEventW(SYNCHRONIZE,FALSE,env(L"YMCC_RESOURCE_EXIT_ACK").c_str())};
            need(ready.value&&acknowledge.value&&SetEvent(ready.value),"held-endpoint-controls");
            need(WaitForSingleObject(acknowledge.value,1500)==WAIT_OBJECT_0,"held-endpoint-acknowledgement");
            exit=0;return true; // Leaf is never released; production Owner must revoke it.
        }
        if(behavior==L"eof"){exit=0;return true;} // Peer closes without a result; never retry/spin.
        if(behavior==L"bad-reply"){
            const Json reply={{"schema",worker::schema},{"id",std::string(32,'f')},{"generation",request.at("generation")},{"detail",{{"ready",true},{"reason","forged"}}}};
            const auto text=reply.dump();channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),1000);exit=0;return true;
        }
        // Exact callback does not observe cancellation; only the owned Job can
        // revoke this wait. No fixture release occurs until AFTER measurement.
        std::mutex mutex;std::condition_variable condition;bool released=false;std::unique_lock lock(mutex);condition.wait(lock,[&]{return released;});exit=0;return true;
    }catch(...){exit=92;return true;}
}
static DWORD handles(){DWORD n=0;need(GetProcessHandleCount(GetCurrentProcess(),&n)!=FALSE,"handle snapshot failed");return n;}
static Json exitedResourceWait=Json::object();
static void checkExitedResourceWait(const fs::path& steam,const fs::path& home,const fs::path& metadata){
 using ymcc::deckybinding::detail::Handle;
 struct Environment {
  std::wstring mode=env(L"YMCC_RESOURCE_WORKER_FIXTURE"),ready=env(L"YMCC_RESOURCE_EXIT_READY"),ack=env(L"YMCC_RESOURCE_EXIT_ACK");
  ~Environment(){SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",mode.empty()?nullptr:mode.c_str());SetEnvironmentVariableW(L"YMCC_RESOURCE_EXIT_READY",ready.empty()?nullptr:ready.c_str());SetEnvironmentVariableW(L"YMCC_RESOURCE_EXIT_ACK",ack.empty()?nullptr:ack.c_str());}
 } restore;
 const auto prefix=L"Local\\YMCC-ResourceExit-Fixture-"+std::to_wstring(GetCurrentProcessId());
 const auto readyName=prefix+L"-ready",ackName=prefix+L"-ack";
 Handle ready{CreateEventW(nullptr,TRUE,FALSE,readyName.c_str())},acknowledge{CreateEventW(nullptr,TRUE,FALSE,ackName.c_str())};need(ready.value&&acknowledge.value,"exit-wait-control-events");
 need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"eof-held-endpoint")&&SetEnvironmentVariableW(L"YMCC_RESOURCE_EXIT_READY",readyName.c_str())&&SetEnvironmentVariableW(L"YMCC_RESOURCE_EXIT_ACK",ackName.c_str()),"exit-wait-environment");
 const auto before=handles();const auto started=GetTickCount64();unsigned observations=0;ULONGLONG exitedAt=0;
 Handle root,leaf;bool identityVerified=false;
 FILETIME creation{},exit{},kernelBefore{},userBefore{},kernelAfter{},userAfter{};
 need(GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernelBefore,&userBefore)!=FALSE,"exit-wait-thread-counter-before");
 const auto current=[&]{
  ++observations;
  if(!root.value&&WaitForSingleObject(ready.value,0)==WAIT_OBJECT_0){
   std::ifstream file(home/L"fixture-held-endpoint.json");Json proof;file>>proof;
   root.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,proof.at("root").at("pid").get<DWORD>());
   leaf.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,proof.at("leaf").at("pid").get<DWORD>());need(root.value&&leaf.value,"exit-wait-observed-process-handles");
   identityVerified=ymcc::deckybinding::worker::contextIdentity(root.value)==proof.at("root")&&ymcc::deckybinding::worker::contextIdentity(leaf.value)==proof.at("leaf");need(identityVerified&&SetEvent(acknowledge.value),"exit-wait-observed-identities");
  }
  const auto now=GetTickCount64();if(root.value&&!exitedAt&&WaitForSingleObject(root.value,0)==WAIT_OBJECT_0)exitedAt=now;
  return exitedAt?now-exitedAt<180:now-started<1500;
 };
 const auto result=worker::prepare(steam,home,metadata,current,true);const auto elapsed=GetTickCount64()-started;
 need(GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernelAfter,&userAfter)!=FALSE,"exit-wait-thread-counter-after");
 const bool rootExited=root.value&&WaitForSingleObject(root.value,0)==WAIT_OBJECT_0,leafExited=leaf.value&&WaitForSingleObject(leaf.value,0)==WAIT_OBJECT_0;
 if(root.value){CloseHandle(root.value);root.value=nullptr;}if(leaf.value){CloseHandle(leaf.value);leaf.value=nullptr;}
 const auto ticks=[](const FILETIME& v){return (static_cast<unsigned long long>(v.dwHighDateTime)<<32)|v.dwLowDateTime;};
 exitedResourceWait={{"currentPredicateCalls",observations},{"maximumCalls",128},{"elapsedMs",elapsed},{"threadCpuMs",(ticks(kernelAfter)-ticks(kernelBefore)+ticks(userAfter)-ticks(userBefore))/10000.0},
  {"rootExitObservedBeforeCancel",exitedAt!=0},{"identityVerified",identityVerified},{"rootExited",rootExited},{"leafExited",leafExited},{"manualLeafRelease",false},{"reason",result.value("reason",std::string{})},{"destroyed",result.value("resourceWorkerDestroyed",false)},{"beforeHandles",before},{"afterHandles",handles()}};
 need(exitedAt&&identityVerified&&rootExited&&leafExited&&result.value("resourceWorkerDestroyed",false)&&result.value("reason",std::string{})=="preparation-cancelled"&&elapsed<=3200&&before==handles(),"exited resource peer lost cancellation or owned cleanup");
 need(observations<=128,"exited resource process kept waking a pending pipe in a hot loop");
}
static Json optionalProgressLoss=Json::array();
static Json launchPreflightRevocations=Json::array();
static Json optionalRepairProgressLoss=Json::array();
static Json digestWork=Json::array();
static Json cases=Json::array();template<class F>static void check(const char* name,F&& work){try{work();cases.push_back({{"name",name},{"pass",true}});}catch(const std::exception& error){cases.push_back({{"name",name},{"pass",false},{"error",error.what()}});}}
int wmain(int argc,wchar_t** argv){
 if(argc==4&&std::wstring_view(argv[1])==L"--fixture-resource-held-endpoint"){
  const auto pipe=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[2]))),cancel=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[3])));
  if(GetFileType(pipe)!=FILE_TYPE_PIPE||WaitForSingleObject(cancel,0)==WAIT_FAILED)return 93;
  Sleep(INFINITE);return 0;
 }
 int code=0;if(childPeer(code))return code;try{
    need(argc==4,"explicit fixture result and official data file required");const auto base=fs::absolute(argv[1]);need(base.filename()==L"worker-fixture","scope rejected");const auto steam=base/L"Steam",home=base/L"PowerControl"/L"decky",metadata=base/L"YeManCC"/L"decky-package.json";put(steam/L"steam.exe","inert");put(steam/L".cef-enable-remote-debugging","");
    for(const auto* name:ymcc::deckysetup::managedFiles)put(home/name,"fixture-"+ymcc::deckysetup::pathText(fs::path(name)));
    fs::copy_file(argv[3],home/ymcc::deckysetup::managedFiles[0],fs::copy_options::overwrite_existing);
    Json manifest={{"schemaVersion",1},{"owner","YMCC"},{"origin","official-release"},{"loaderVersion",ymcc::deckyidentity::version},{"loaderSha256",ymcc::deckyidentity::sha256},{"files",Json::array()}};
    for(const auto* name:ymcc::deckysetup::managedFiles)manifest["files"].push_back({{"path",ymcc::deckysetup::pathText(fs::path(name))},{"bytes",fs::file_size(home/name)},{"sha256",worker::digest((home/name).wstring(),[]{return true;})}});put(metadata,manifest.dump());
    const auto normal=[&]{return worker::prepare(steam,home,metadata,[]{return true;},true);};
    Json cachePreparationObservations=Json::array();
    const auto observeCachePreparation=[&](const char* stage){
        const auto handlesBefore=handles();
        auto result=normal();
        const auto handlesAfter=handles();
        cachePreparationObservations.push_back({{"stage",stage},{"result",result},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter}});
        return result;
    };
    check("read-only launch preparation validates all live bytes without creating or hashing the repair cache",[&]{
        const auto cache=home/L".ymcc-resource-cache";need(!fs::exists(cache),"read-only fixture cache must start absent");
        const auto r=worker::prepare(steam,home,metadata,[]{return true;},false);
        need(r.value("ready",false)&&r.value("managedPackageVerified",false)&&r.value("resourceWorkerDestroyed",false),"read-only launch lost complete live verification");
        need(!fs::exists(cache),"read-only launch performed noncritical cache IO");
        const auto logged=ymcc::deckydiag::record("prepare",r);
        need(logged.value("resourceWorkerChildStage",std::string{})=="inspect"&&logged.value("resourceWorkerProgressCount",0u)==2u&&logged.contains("resourceWorkerChildElapsedMs"),"read-only actual child progress was not retained");
        need(!logged.contains("resourceWorkerInitialInspectStep")&&!logged.contains("resourceWorkerInitialInspectElapsedMs"),"read-only preflight acquired extra progress traffic");
        need(!r.contains("resourceWorkerCacheStep")&&!logged.contains("resourceWorkerCacheStep"),"read-only preflight acquired cache progress traffic");
        const auto invalidCache=ymcc::deckydiag::record("prepare",{{"resourceWorkerCacheStep","secret-path"},{"resourceWorkerCacheStepElapsedMs",123u}});
        need(!invalidCache.contains("resourceWorkerCacheStep")&&!invalidCache.contains("resourceWorkerCacheStepElapsedMs"),"untyped cache progress reached the log");
        const auto invalidStep=ymcc::deckydiag::record("prepare",{{"resourceWorkerInitialInspectStep","secret-path"},{"resourceWorkerInitialInspectElapsedMs",123u}});
        need(!invalidStep.contains("resourceWorkerInitialInspectStep")&&!invalidStep.contains("resourceWorkerInitialInspectElapsedMs"),"untyped inspection reading reached the log");
        need(logged.value("resourceWorkerChildCpuKnown",false)&&logged.contains("resourceWorkerChildCpuMs")&&logged["resourceWorkerChildCpuMs"].is_number_unsigned(),"actual child CPU counter facts were lost");
        const auto unknownCpu=ymcc::deckydiag::record("prepare",{{"resourceWorkerChildCpuKnown",false},{"resourceWorkerChildCpuMs",12u}});
        need(unknownCpu.contains("resourceWorkerChildCpuKnown")&&!unknownCpu["resourceWorkerChildCpuKnown"].get<bool>()&&!unknownCpu.contains("resourceWorkerChildCpuMs"),"unknown child CPU became an observed zero or rate");
        need(logged.value("resourceWorkerReplyReceived",false)&&logged.value("resourceWorkerDestroyed",false)&&logged.value("resourceWorkerStage",std::string{})=="reply","successful resource framing facts were lost");
        need(r.contains("resourceWorkerCleanupMs")&&r["resourceWorkerCleanupMs"].is_number_unsigned()&&
            logged.contains("resourceWorkerCleanupMs")&&logged["resourceWorkerCleanupMs"]==r["resourceWorkerCleanupMs"],"actual resource cleanup timing did not survive diagnostic projection");
        for(const Json& invalid:{Json(-1),Json("secret-path"),Json(3600001u)}){
            const auto badCleanup=ymcc::deckydiag::record("prepare",{{"resourceWorkerCleanupMs",invalid}});
            need(!badCleanup.contains("resourceWorkerCleanupMs"),"invalid resource cleanup timing reached the log");
        }
        auto poison=Json{{"resourceWorkerChildStage","secret-path"},{"resourceWorkerProgressCount",5u},{"resourceWorkerChildElapsedMs",-1},{"resourceWorkerReadKind","secret-payload"},{"resourceWorkerReadError",-1},{"resourceWorkerChildExitCode",4294967296ull},{"resourceWorkerStage","secret-path"},{"resourceWorkerReplyReceived","true"},{"resourceWorkerElapsedMs",-1},{"resourceWorkerChildCpuKnown","true"},{"resourceWorkerChildCpuMs",-1},{"payload","private"}};
        const auto filtered=ymcc::deckydiag::record("prepare",poison);
        for(const auto* key:{"resourceWorkerChildStage","resourceWorkerProgressCount","resourceWorkerChildElapsedMs","resourceWorkerReadKind","resourceWorkerReadError","resourceWorkerChildExitCode","resourceWorkerStage","resourceWorkerReplyReceived","resourceWorkerElapsedMs","resourceWorkerChildCpuKnown","resourceWorkerChildCpuMs","payload"})need(!filtered.contains(key),"resource diagnostics leaked untyped or private content");
        need(r.value("resourceMaintenanceDeferred",false),"deferred maintenance was not reported");
        need(ymcc::deckydiag::record("prepare",r).value("resourceMaintenanceDeferred",false),"existing preparation log lost deferred-maintenance evidence");
    });
    check("read-only launch still rejects same-size plugin corruption and cannot repair or prime cache",[&]{
        const auto plugin=home/ymcc::deckysetup::managedFiles[2];const auto original=std::string("fixture-")+ymcc::deckysetup::pathText(fs::path(ymcc::deckysetup::managedFiles[2]));
        auto corrupt=original;corrupt.back()=corrupt.back()=='x'?'y':'x';put(plugin,corrupt);
        const auto r=worker::prepare(steam,home,metadata,[]{return true;},false);
        need(!r.value("ready",true)&&r.value("reason",std::string{})=="resource-package-mismatch"&&r.value("resourceWorkerDestroyed",false),"read-only launch admitted corrupt live plugin");
        need(r.value("resourceMaintenanceDeferred",false),"valid manifest was confused with a verified live plugin");
        need(!fs::exists(home/L".ymcc-resource-cache"),"read-only rejection wrote repair cache");
        need(worker::digest(plugin.wstring(),[]{return true;})!=manifest["files"][2]["sha256"].get<std::string>(),"read-only rejection silently repaired user file");put(plugin,original);
        const auto receipt=home/L".ymcc-managed.receipt";
        put(receipt,"{}");const auto invalid=worker::prepare(steam,home,metadata,[]{return true;},false);fs::remove(receipt);
        need(!invalid.value("ready",true)&&invalid.value("reason",std::string{})=="resource-package-invalid"&&
            !invalid.contains("resourceMaintenanceDeferred")&&invalid.value("resourceWorkerDestroyed",false)&&!fs::exists(home/L".ymcc-resource-cache"),
            "read-only launch fell back from an invalid installed receipt or reported it as valid");
    });
    check("actual private early-entry child validates current official bytes and primes cache",[&]{
        const auto r=observeCachePreparation("initial-seed");need(r.value("ready",false)&&r.value("resourceCacheAvailable",false)&&r.value("resourceWorkerDestroyed",false),"real resource worker did not finish with zero resources");
        const auto written=ymcc::deckydiag::record("prepare",r);
        need(written.value("resourceWorkerInitialInspectStep",std::string{})=="steam-debug-marker"&&written.contains("resourceWorkerInitialInspectElapsedMs"),"real initial inspection did not reach final observed step");
        need(written.value("resourceWorkerCacheStep",std::string{})=="seed"&&written.contains("resourceWorkerCacheStepElapsedMs"),"first seed did not report cache substep");
        for(const bool allowRepair:{false,true}){
            const auto countPath=home/L"fixture-digest-count.json";fs::remove(countPath);
            SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"digest-count");
            const auto measured=worker::prepare(steam,home,metadata,[]{return true;},allowRepair);
            SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
            const auto counts=readDocument(countPath,1024);
            const auto files=ymcc::deckysetup::managedFiles.size();
            digestWork.push_back({{"writableCacheHit",allowRepair},{"counts",counts},{"expectedHashStates",(allowRepair?3:1)*files},
                {"expectedBundledManifestAttributeProbes",2u}});
            need(measured.value("ready",false)&&measured.value("managedPackageVerified",false)&&measured.value("resourceWorkerDestroyed",false)&&
                (!allowRepair||measured.value("resourceCacheAvailable",false)),"digest measurement did not reach a verified preparation");
            need(allowRepair?measured.value("resourceWorkerCacheStep",std::string{})=="verify":!measured.contains("resourceWorkerCacheStep"),"cache hit and read-only work were not distinguished");
            need(counts.at("providerOpens")==1&&counts.at("providerCloses")==1&&counts.at("hashStates")== (allowRepair?3:1)*files,
                "resource request reopened its provider or failed to hash all live and cache files independently");
        }
        const auto before=handles();SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"progress-optional-return-package");
        const auto dropped=normal();SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
        need(dropped.value("ready",false)&&dropped.value("managedPackageVerified",false)&&dropped.value("resourceWorkerDestroyed",false)&&dropped.value("resourceWorkerReplyReceived",false)&&handles()==before,"optional inspection note changed verification or ownership");
        need(dropped.value("resourceWorkerInitialInspectStep",std::string{})=="loader-read"&&dropped.value("resourceWorkerProgressCount",0u)==2u,"lost inspection note invented a later edge");
        for(const auto* mode:{L"progress-optional-memory-receipt",L"progress-optional-memory-inspect",L"progress-optional-memory-after-inspect",L"progress-optional-memory-void-inspect",L"progress-optional-return-inspect",L"progress-optional-return-after-inspect"}){
            const auto before=handles();const auto previous=env(L"YMCC_RESOURCE_WORKER_FIXTURE");
            need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",mode)!=FALSE,"optional-progress-fixture-mode");
            const auto value=normal();
            need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",previous.empty()?nullptr:previous.c_str())!=FALSE,"optional-progress-fixture-restore");
            const auto modeName=ymcc::deckysetup::pathText(fs::path(mode));
            const unsigned expected=std::wstring_view(mode)==L"progress-optional-memory-receipt"?0u:(std::wstring_view(mode)==L"progress-optional-memory-inspect"||std::wstring_view(mode)==L"progress-optional-memory-void-inspect"||std::wstring_view(mode)==L"progress-optional-return-inspect")?1u:2u;
            optionalProgressLoss.push_back({{"mode",modeName},{"result",value},{"handlesBefore",before},{"handlesAfter",handles()},{"expectedPrefixCount",expected}});
            need(value.value("ready",false)&&value.value("managedPackageVerified",false)&&value.value("resourceCacheAvailable",false)&&value.value("resourceWorkerDestroyed",false)&&value.value("resourceWorkerReplyReceived",false),"optional-progress-memory-failure-changed-real-preparation");
            need(value.value("resourceWorkerProgressCount",99u)==expected&&value.value("resourceWorkerChildStage",std::string{})==(expected==0?"none":expected==1?"receipt":"inspect"),"optional-progress-memory-lost-or-invented-prefix");
            need(handles()==before,"optional-progress-memory-failure-leaked-handles");
        }
        // Each fresh bundled read probes the exact path twice (safe-path walk
        // and presence check). Maintenance consumes the validated contract.
        for(const auto& work:digestWork)need(work.at("counts").at("bundledManifestAttributeProbes")==work.at("expectedBundledManifestAttributeProbes"),
            "preparation repeated the bundled manifest lookup");
        const auto cacheNoteHandles=handles();
        SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"progress-optional-return-cache-verify");
        const auto missingNote=normal();
        SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
        need(missingNote.value("ready",false)&&missingNote.value("resourceCacheAvailable",false)&&
            missingNote.value("resourceWorkerReplyReceived",false)&&missingNote.value("resourceWorkerDestroyed",false)&&
            missingNote.value("resourceWorkerProgressCount",0u)==3u&&!missingNote.contains("resourceWorkerCacheStep")&&handles()==cacheNoteHandles,
            "optional cache verification note changed preparation or invented evidence");
    });
    check("actual same-image child repairs missing Loader and confirms final identity",[&]{
        if(!fs::exists(home/L".ymcc-resource-cache")){const auto seeded=observeCachePreparation("repair-prerequisite-seed");need(seeded.value("ready",false)&&seeded.value("resourceCacheAvailable",false)&&seeded.value("resourceWorkerDestroyed",false),"repair-fixture-cache-prime");}
        fs::remove(home/ymcc::deckysetup::managedFiles[0]);const auto r=observeCachePreparation("loader-repair");need(r.value("ready",false)&&r.value("resourcesRepaired",0u)==1&&r.value("resourceWorkerDestroyed",false),"real worker did not repair official bytes");
        need(r.value("resourceWorkerCacheStep",std::string{})=="repair"&&r.value("resourceWorkerChildStage",std::string{})=="reinspect","cache restore substep was lost or confused with reinspection");
        for(const auto* mode:{L"progress-optional-memory-cache",L"progress-optional-memory-after-cache",L"progress-optional-memory-reinspect"}){
            const auto before=handles();const auto previous=env(L"YMCC_RESOURCE_WORKER_FIXTURE");
            need(fs::remove(home/ymcc::deckysetup::managedFiles[0]),"repair-progress-loader-must-start-missing");
            need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",mode)!=FALSE,"repair-progress-fixture-mode");
            const auto value=normal();
            need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",previous.empty()?nullptr:previous.c_str())!=FALSE,"repair-progress-fixture-restore");
            const auto restored=worker::digest((home/ymcc::deckysetup::managedFiles[0]).wstring(),[]{return true;});
            const unsigned expected=std::wstring_view(mode)==L"progress-optional-memory-cache"?2u:3u;
            optionalRepairProgressLoss.push_back({{"mode",ymcc::deckysetup::pathText(fs::path(mode))},{"result",value},{"handlesBefore",before},{"handlesAfter",handles()},{"expectedPrefixCount",expected},{"restoredLoaderSha256",restored}});
            need(restored==ymcc::deckyidentity::sha256,"repair-progress-actual-loader-not-restored");
            need(value.value("ready",false)&&value.value("managedPackageVerified",false)&&value.value("resourcesRepaired",0u)==1&&value.value("resourceCacheAvailable",false)&&value.value("resourceWorkerDestroyed",false)&&value.value("resourceWorkerReplyReceived",false),"optional-repair-progress-failure-lost-actual-restoration-success");
            need(value.value("resourceWorkerProgressCount",99u)==expected&&value.value("resourceWorkerChildStage",std::string{})==(expected==2?"inspect":"cache"),"repair-progress-prefix-changed");
            need(handles()==before,"repair-progress-lost-handles");
        }
        const auto before=handles();
        const auto receipt=home/L".ymcc-managed.receipt";
        const auto plugin=home/ymcc::deckysetup::managedFiles[2];
        const auto previous=env(L"YMCC_RESOURCE_WORKER_FIXTURE");
        need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"receipt-changed-before-package-check")!=FALSE,"receipt-race-fixture-mode");
        const auto refused=normal();
        need(SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",previous.empty()?nullptr:previous.c_str())!=FALSE,"receipt-race-fixture-restore");
        const auto digest=worker::digest(plugin.wstring(),[]{return true;});
        const auto releaseOkay=handles()==before;
        fs::remove(receipt);
        put(plugin,std::string("fixture-")+ymcc::deckysetup::pathText(fs::path(ymcc::deckysetup::managedFiles[2])));
        need(!refused.value("ready",true)&&refused.value("reason",std::string{})=="resource-package-invalid"&&
            refused.value("resourceWorkerDestroyed",false)&&releaseOkay,"latest invalid receipt was not rejected or worker was retained");
        need(refused.value("resourcesRepaired",0u)==0&&!refused.value("resourceRepairAttempted",false)&&
            !refused.value("resourceCacheAvailable",false)&&!digest.empty()&&digest!=manifest["files"][2]["sha256"].get<std::string>(),
            "cache repaired from an obsolete contract despite current receipt rejection");
    });
    check("actual owned worker preserves a legacy receipt when write admission is refused",[&]{put(home/L".ymcc-managed.json",manifest.dump());put(home/L"settings"/L".ymcc-managed.json","old-settings-copy");const auto r=worker::prepare(steam,home,metadata,[]{return true;},false);need(!r.value("ready",true)&&r.value("resourceWorkerDestroyed",false)&&fs::exists(home/L".ymcc-managed.json")&&!fs::exists(home/L".ymcc-managed.receipt"),"non-owner resource worker moved legacy receipt");});
    check("actual owned worker migrates duplicate root JSON before two official startup scans",[&]{const auto r=normal();need(r.value("ready",false)&&r.value("resourceWorkerDestroyed",false)&&!fs::exists(home/L".ymcc-managed.json")&&fs::exists(home/L".ymcc-managed.receipt"),"production worker did not retire conflicting root JSON");for(unsigned startup=0;startup<2;startup++)for(const auto& file:fs::directory_iterator(home))if(file.path().extension()==L".json")need(MoveFileW(file.path().c_str(),(home/L"settings"/file.path().filename()).c_str())!=FALSE,"official settings scan still hits receipt collision");const auto second=normal();need(second.value("ready",false)&&second.value("resourceWorkerDestroyed",false),"repeat worker startup broke receipt ownership");std::ifstream saved(home/L"settings"/L".ymcc-managed.json");std::string value;saved>>value;need(value=="old-settings-copy","worker changed settings-side user metadata");});
    const auto count=handles();
    check("forged or stale child response cannot claim ready",[&]{
        for(const auto* mode:{L"bad-reply",L"progress-invalid-order",L"progress-invalid-type",L"progress-invalid-secret",L"progress-invalid-extra",L"progress-invalid-duplicate",L"progress-invalid-repeat",L"progress-invalid-inspect-order",L"progress-invalid-cache-order",L"progress-invalid-cache-repeat"}){
            SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",mode);const auto r=normal();SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
            need(!r.value("ready",true)&&r.value("resourceWorkerDestroyed",false)&&r.value("reason",std::string{})=="resource-worker-reply-invalid","forged resource reply accepted");
            need(handles()==count,"invalid reply leaked handles");
        }
    });
    check("child EOF before a result stops promptly without retry or leaked resources",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"eof");const auto started=GetTickCount64();const auto r=normal();const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(!r.value("ready",true)&&r.value("reason",std::string{})=="resource-worker-read-failed"&&elapsed<1000&&r.value("resourceWorkerDestroyed",false)&&handles()==count,"resource EOF spun or retained ownership");
        const auto logged=ymcc::deckydiag::record("prepare",r);
        need(logged.value("resourceWorkerReadKind",std::string{})=="ended"&&logged.contains("resourceWorkerReadError")&&logged.at("resourceWorkerReadError")==r.at("resourceWorkerReadError")&&!logged.value("resourceWorkerReplyReceived",true)&&logged.value("resourceWorkerDestroyed",false),"real EOF framing facts missing from existing preparation log");
        checkExitedResourceWait(steam,home,metadata);});
    check("noncooperative resource callback is revoked before the unchanged 3200ms deadline",[&]{
        const auto startedFile=home/L"fixture-child-started";fs::remove(startedFile);
        SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");
        ymcc::deckybinding::detail::Handle process;rootReopenAttempts=0;
        struct ResetRefusal{~ResetRefusal(){refusedRootReopen=0;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);}} reset;
        const auto started=GetTickCount64();
        const auto r=worker::prepare(steam,home,metadata,[&]{
            if(!process.value){
                std::ifstream input(startedFile);DWORD pid=0;
                if(input>>pid&&pid){
                    process.value=::OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
                    need(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT,"fixture root was not live before revocation");
                    refusedRootReopen=pid;
                }
            }
            return true;
        },true);
        const auto elapsed=GetTickCount64()-started;
        const bool exited=process.value&&WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;
        DWORD closeError=0;need(process.close(closeError),"fixture process handle close failed");
        const bool destroyed=r.value("resourceWorkerDestroyed",false);
        const bool clean=elapsed<=3200&&!r.value("ready",true)&&destroyed&&exited&&handles()==count&&
            r.value("reason",std::string{})=="resource-worker-deadline"&&refusedRootReopen&&rootReopenAttempts==0;
        cases.push_back({{"name","stalled callback actual resources at deadline"},{"pass",clean},{"elapsedMs",elapsed},{"deadlineMs",3200},
            {"destroyed",destroyed},{"rootExited",exited},{"rootReopenAttempts",rootReopenAttempts},{"beforeHandles",count},{"afterHandles",handles()},
            {"reason",r.value("reason",std::string{})}});
        need(clean,"resource worker re-opened its owned root or failed to confirm bounded cleanup");
    });
    check("OFF during a stuck child stops only its owned Job without waiting for callback release",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");const auto started=GetTickCount64();const auto r=worker::prepare(steam,home,metadata,[&]{return GetTickCount64()-started<150;},true);const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(elapsed<=1000&&r.value("reason",std::string{})=="preparation-cancelled"&&r.value("resourceWorkerDestroyed",false),"OFF blocked on resource child");
        const auto logged=ymcc::deckydiag::record("prepare",r);
        need(!logged.value("resourceWorkerChildExitKnown",true)&&!logged.contains("resourceWorkerChildExitCode"),"our own OFF termination was reported as prior child exit");
        need(handles()==count,"OFF resource child leaked handles");});
    check("explicit OFF at entry starts no child and touches no resource",[&]{const auto before=handles();const auto r=worker::prepare(steam,home,metadata,[]{return false;},true);need(!r.value("ready",true)&&r.value("reason",std::string{})=="preparation-cancelled"&&handles()==before,"OFF starts resource worker");});
    check("parent setup consumes child verification without any synchronous resource digest",[&]{
        ymcc::deckyresources::Options options;options.enabled=false;options.isolatedPreparation=normal;
        const auto noParentDigest=[](const std::wstring&)->std::string{throw std::runtime_error("parent digest must never run");};
        const auto r=ymcc::deckysetup::prepare(steam,home,noParentDigest,false,[]{return true;},metadata,options);
        need(r.value("ready",false)&&r.value("resourceWorkerDestroyed",false),"parent performed resource IO or lost child verification");
        // Inject the delegated parent's typed outcome, not a claim of an OS
        // leak. The original owned child and cancellation checks run elsewhere.
        bool allPreserved=true,cleanupVisibleWithRestart=true,prewarmCleanupVisible=true;
        for(const auto* variant:{"revoked-clean","revoked-cleanup","marker-recheck-revoked","marker-recheck-running"}){
            const std::string_view mode=variant;
            const auto scopedSteam=base/fs::path(variant);
            fs::create_directories(scopedSteam);
            const auto marker=scopedSteam/L".cef-enable-remote-debugging";
            const bool recheck=mode.starts_with("marker-recheck"),clean=mode=="revoked-clean";
            bool allowed=true;unsigned reads=0;
            options.isolatedPreparation=[&]()->Json{
                if(++reads==1&&recheck)return {{"ready",false},{"reason","steam-debug-missing"},{"resourceWorkerDestroyed",true}};
                allowed=mode=="marker-recheck-running";
                return {{"ready",false},{"reason",clean?"preparation-cancelled":"resource-worker-cleanup-incomplete"},{"resourceWorkerDestroyed",clean}};
            };
            const auto result=ymcc::deckysetup::prepare(scopedSteam,home,noParentDigest,mode=="marker-recheck-running",[&]{return allowed;},metadata,options);
            const auto expected=clean?"preparation-cancelled":"resource-worker-cleanup-incomplete";
            const auto logged=ymcc::deckydiag::record("prepare",result);
            if(!clean){
                for(const auto& [owner,power,expired]:{std::tuple{true,true,false},std::tuple{false,false,false},std::tuple{true,false,false},std::tuple{true,true,true}}){
                    const auto settled=ymcc::deckysetup::settlePrewarm(result,owner,power,expired);
                    const auto visible=ymcc::deckydiag::record("prewarm",{{"ready",settled.reason.empty()},{"reason",settled.reason}});
                    prewarmCleanupVisible=prewarmCleanupVisible&&settled.reason==expected&&!settled.timedOut&&
                        visible.value("reason",std::string{})==expected&&!visible.value("ready",true);
                }
            }
            allPreserved=allPreserved&&reads==(recheck?2u:1u)&&fs::exists(marker)==recheck&&
                result.value("createdSteamDebugMarker",false)==recheck&&!result.value("ready",true)&&
                result.value("reason",std::string{})==expected&&logged.value("reason",std::string{})==expected&&
                logged.contains("resourceWorkerDestroyed")&&logged.value("resourceWorkerDestroyed",!clean)==clean;
            if(mode=="marker-recheck-running"||mode=="revoked-cleanup"){
                // Model the existing outer restart latch over actual setup output.
                auto restartPending=result;
                ymcc::deckysetup::applySteamRestartRequirement(restartPending);
                const auto visible=ymcc::deckydiag::record("prepare",restartPending);
                cleanupVisibleWithRestart=cleanupVisibleWithRestart&&restartPending==result&&
                    visible.value("reason",std::string{})==expected;
            }
        }
        need(allPreserved,"revocation or marker restart masked the child cleanup status");
        need(cleanupVisibleWithRestart,"outer restart latch masked unconfirmed cleanup");
        const Json cancelled={{"ready",false},{"reason","preparation-cancelled"},{"resourceWorkerDestroyed",true}};
        const auto ownerRevoked=ymcc::deckysetup::settlePrewarm(r,false,false,false);
        const auto powerRevoked=ymcc::deckysetup::settlePrewarm(r,true,false,false);
        const auto expired=ymcc::deckysetup::settlePrewarm(cancelled,true,true,true);
        const auto retiredExpired=ymcc::deckysetup::settlePrewarm(cancelled,false,false,true);
        const auto normalReady=ymcc::deckysetup::settlePrewarm(r,true,true,false);
        need(ownerRevoked.reason=="preparation-cancelled"&&!ownerRevoked.timedOut&&powerRevoked.reason=="preparation-deferred"&&!powerRevoked.timedOut&&
            expired.reason=="preparation-cancelled"&&expired.timedOut&&retiredExpired.reason=="preparation-cancelled"&&!retiredExpired.timedOut&&
            normalReady.reason.empty()&&!normalReady.timedOut,"ordinary prewarm cancellation, readiness or deadline changed");
        need(prewarmCleanupVisible,"outer prewarm terminal state masked unconfirmed cleanup");
        auto ready=r;
        ymcc::deckysetup::applySteamRestartRequirement(ready);
        need(!ready.value("ready",true)&&ready.value("reason",std::string{})=="steam-restart-required"&&
            ready.value("resourceWorkerDestroyed",false),"normal restart latch lost after confirmed cleanup");
    });
    check("actual Runtime OFF drains a stuck resource preparation before its original deadline",[&]{
        fs::remove(home/L"fixture-child-started");SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");
        ymcc::deckysidebar::Runtime runtime;ymcc::deckysidebar::Config config;std::atomic<bool> destroyed=false;
        config.prepareEnvironment=[&](const std::function<bool()>& current){const auto r=worker::prepare(steam,home,metadata,current,true);destroyed=r.value("resourceWorkerDestroyed",false);return r.value("ready",false)?std::string{}:r.value("reason",std::string{});};
        runtime.initialize(std::move(config));runtime.observeSteam(0);runtime.setEnabled(true);
        const auto entered=GetTickCount64()+1500;while(!fs::exists(home/L"fixture-child-started")&&GetTickCount64()<entered)Sleep(2);
        need(fs::exists(home/L"fixture-child-started"),"actual Runtime resource child did not enter");
        const auto started=GetTickCount64();runtime.shutdown();const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
        need(elapsed<=3200&&destroyed&&!runtime.snapshot().enabled&&handles()==count,"actual Runtime OFF retained resource Job or parent worker handles");
        fs::remove(home/L"fixture-cache-stage-started");SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"progress-stall");
        const auto progressStarted=GetTickCount64();ULONGLONG stageEntered=0;
        const auto stageResult=worker::prepare(steam,home,metadata,[&]{
            if(!stageEntered&&fs::exists(home/L"fixture-cache-stage-started"))stageEntered=GetTickCount64();
            return !stageEntered||GetTickCount64()-stageEntered<100;
        },true);
        SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
        const auto stageLogged=ymcc::deckydiag::record("prepare",stageResult);
        need(GetTickCount64()-progressStarted<=3200&&!stageResult.value("ready",true)&&stageResult.value("reason",std::string{})=="preparation-cancelled"&&stageResult.value("resourceWorkerDestroyed",false)&&handles()==count,"actual phase callback stall weakened cancellation or resource return");
        need(stageLogged.value("resourceWorkerChildStage",std::string{})=="cache"&&stageLogged.value("resourceWorkerProgressCount",0u)==3u&&!stageLogged.value("resourceWorkerReplyReceived",true),"last child stage was lost or progress claimed final readiness");
        need(stageLogged.contains("resourceWorkerCleanupMs")&&stageLogged["resourceWorkerCleanupMs"].is_number_unsigned()&&
            stageLogged["resourceWorkerCleanupMs"]==stageResult["resourceWorkerCleanupMs"],"cancelled resource cleanup timing was not reported");
        const std::pair<const wchar_t*,const char*> inspectionStalls[]={{L"inspect-stall","plugin-package"},
            {L"loader-open-stall","loader-open"},{L"loader-read-stall","loader-read"},{L"cache-verify-stall","verify"}};
        for(const auto& [mode,stage]:inspectionStalls){
            fs::remove(home/L"fixture-cache-stage-started");SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",mode);
            const auto substepStarted=GetTickCount64();ULONGLONG substepEntered=0;
            const auto substep=worker::prepare(steam,home,metadata,[&]{
                if(!substepEntered&&fs::exists(home/L"fixture-cache-stage-started"))substepEntered=GetTickCount64();
                return !substepEntered||GetTickCount64()-substepEntered<100;
            },true);
            SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
            const auto substepLogged=ymcc::deckydiag::record("prepare",substep);
            need(substepEntered&&GetTickCount64()-substepStarted<=3200&&!substep.value("ready",true)&&substep.value("reason",std::string{})=="preparation-cancelled"&&substep.value("resourceWorkerDestroyed",false)&&handles()==count,"inspection callback stall defeated cancellation or retained handles");
            const bool cacheVerify=std::wstring_view(mode)==L"cache-verify-stall";
            need(substepLogged.value("resourceWorkerChildStage",std::string{})==(cacheVerify?"cache":"inspect")&&
                substepLogged.value(cacheVerify?"resourceWorkerCacheStep":"resourceWorkerInitialInspectStep",std::string{})==stage&&
                substepLogged.contains(cacheVerify?"resourceWorkerCacheStepElapsedMs":"resourceWorkerInitialInspectElapsedMs")&&
                !substepLogged.value("resourceWorkerReplyReceived",true),"substep stall was not distinguishable from coarse phase");
        }
        bool scopedOkay=true;
        for(const bool off:{true,false}){
            fs::remove(home/L"fixture-child-started");SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");
            ymcc::deckysidebar::Runtime launch;ymcc::deckysidebar::Config ports;Json evidence;std::atomic<unsigned> returned=0;
            ports.ownedBinding=std::make_shared<ymcc::deckybinding::Client>();
            installPreflight(ports,[&](const std::function<bool()>& current){
                evidence=worker::prepare(steam,home,metadata,current,false);
                ++returned;return evidence.value("reason",std::string{});
            });
            launch.initialize(std::move(ports));launch.observeSteam(1);launch.setEnabled(true);
            const auto waitChild=GetTickCount64()+1500;
            while(!fs::exists(home/L"fixture-child-started")&&GetTickCount64()<waitChild)Sleep(2);
            const bool entered=fs::exists(home/L"fixture-child-started");const auto revokedAt=GetTickCount64();
            if(off)launch.shutdown();
            else {launch.observeSteam(0,true);while(!returned.load()&&GetTickCount64()-revokedAt<3200)Sleep(2);}
            const auto waitMs=GetTickCount64()-revokedAt;
            const auto beforeShutdown=returned.load();const auto sawProcess=launch.snapshot().pid!=0;
            if(!off)launch.shutdown();
            SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
            const bool clean=handles()==count;
            const auto reason=evidence.value("reason",std::string{});
            const auto destroyed=evidence.value("resourceWorkerDestroyed",false);
            launchPreflightRevocations.push_back({{"input",off?"off":"steam-session-left"},{"entered",entered},{"readsReturned",beforeShutdown},
                {"processStarted",sawProcess},{"elapsedAfterRevocationMs",waitMs},{"clean",clean},{"reason",reason},{"workerDestroyed",destroyed}});
            scopedOkay=scopedOkay&&entered&&beforeShutdown==1&&!sawProcess&&waitMs<=3200&&clean&&destroyed&&reason=="preparation-cancelled";
        }
        need(scopedOkay,"revoked launch preflight was not cancelled through its original owner fence");
    });
    check("actual Runtime committed preparation repairs resource loss without granting Loader launch",[&]{
        ymcc::deckysidebar::Runtime runtime;ymcc::deckysidebar::Config config;std::atomic<unsigned> prepared=0;std::atomic<bool> recovered=false;
        config.prepareEnvironment=[&](const std::function<bool()>& current){const auto r=worker::prepare(steam,home,metadata,current,true);recovered=r.value("ready",false)&&r.value("resourcesRepaired",0u)>0&&r.value("resourceWorkerDestroyed",false);++prepared;return r.value("ready",false)?std::string{}:r.value("reason",std::string{});};
        runtime.initialize(std::move(config));runtime.observeSteam(0);runtime.setEnabled(true);
        auto deadline=GetTickCount64()+2500;while(prepared<1&&GetTickCount64()<deadline)Sleep(2);need(prepared==1,"Runtime resource preparation did not complete");
        fs::remove(home/ymcc::deckysetup::managedFiles[2]);need(runtime.requestPreparationRefresh(77),"committed preparation was not admitted");
        deadline=GetTickCount64()+2500;while(prepared<2&&GetTickCount64()<deadline)Sleep(2);
        need(prepared==2&&recovered&&runtime.snapshot().pid==0,"Ready did not recover resources or started a Loader");
        need(!runtime.requestPreparationRefresh(77),"duplicate Ready restarted preparation");runtime.shutdown();need(handles()==count,"committed resource recovery leaked resources");
    });
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const auto& r){return r.at("pass")==true;});const Json result={{"pass",passed==static_cast<int>(cases.size())},{"count",cases.size()},{"passed",passed},{"cases",cases},{"realSteamActions",0},{"officialLoaderExecuted",false},{"childIsOwnTestImage",true},{"exitedResourceWait",exitedResourceWait},{"optionalProgressLoss",optionalProgressLoss},{"optionalRepairProgressLoss",optionalRepairProgressLoss},{"digestWork",digestWork},{"launchPreflightRevocations",launchPreflightRevocations},{"originalStopDeadlineMs",3200},{"cachePreparationObservations",cachePreparationObservations}};put(argv[2],result.dump(2));std::cout<<result.dump()<<std::endl;return result.at("pass")==true?0:2;
 }catch(const std::exception& error){SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);std::cerr<<error.what()<<std::endl;return 3;}}
