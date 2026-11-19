#pragma once
// Resource IO runs inside the existing private same-image owned child channel.
// A stalled disk/hash cannot retain the parent Runtime thread after OFF/exit.
#include "decky_binding_client.h"
#include "decky_sidebar_setup.h"
#include <bcrypt.h>
#include <memory>
#include <optional>
#include <type_traits>
#pragma comment(lib,"bcrypt.lib")
namespace ymcc::deckyresources::worker {
inline constexpr const char* schema="ymcc.decky.resources.v1";
// Same-image private diagnostics only. Fixed, ordered phase edges never grant
// readiness, refresh the preparation deadline, or authorize another operation.
inline constexpr std::array<const char*,4> progressStages={"receipt","inspect","cache","reinspect"};
inline std::string digest(const std::wstring& path,const std::function<bool()>& current){
    struct Hash{BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} state;
    if(!current()||BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    DWORD length=0,written=0;if(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&written,0)<0)return {};
    std::vector<unsigned char> object(length),buffer(65536),value(32);
    if(BCryptCreateHash(state.algorithm,&state.hash,object.data(),length,nullptr,0,0)<0)return {};
    std::ifstream file(std::filesystem::path(path),std::ios::binary);if(!file)return {};
    while(file&&current()){file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());const auto n=file.gcount();if(n>0&&BCryptHashData(state.hash,buffer.data(),static_cast<ULONG>(n),0)<0)return {};}
    if(!current()||file.bad()||BCryptFinishHash(state.hash,value.data(),32,0)<0)return {};
    const char* hex="0123456789abcdef";std::string result;for(const auto b:value){result+=hex[b>>4];result+=hex[b&15];}return result;
}
inline std::string text(const std::filesystem::path& value){const auto bytes=value.u8string();return {bytes.begin(),bytes.end()};}
// Only the authenticated inherited pipe can invoke this operation. No command
// line path or public IPC starts it, and the child never enters application init.
template<class Emit>bool tryRun(const Json& request,HANDLE cancel,Emit&& emit,int& exit){
    if(!request.is_object()||request.value("schema",std::string{})!=schema)return false;
    Json detail={{"ready",false},{"reason","resource-worker-invalid"}};
    try{
        ymcc::deckybinding::worker::require(request.size()==8&&request.value("operation",std::string{})=="prepare"&&request.at("allowRepair").is_boolean(),"resource-worker-invalid");
        const auto id=request.at("id").get<std::string>();ymcc::deckybinding::worker::require(ymcc::deckybinding::worker::hex(id,32),"resource-worker-invalid");
        ymcc::deckybinding::worker::decimal(request.at("generation"));
        const auto home=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("home")));
        const auto steam=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("steam")));
        const auto manifest=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("manifest")));
        ymcc::deckybinding::worker::require(home.is_absolute()&&steam.is_absolute()&&manifest.is_absolute(),"resource-worker-invalid");
        const auto current=[&]{return cancel&&WaitForSingleObject(cancel,0)==WAIT_TIMEOUT;};
        const auto progressStarted=GetTickCount64();
        bool progressAvailable=true; // Keep optional observations a contiguous prefix.
        const auto progress=[&](const char* stage){
            if(!current()||!progressAvailable)return;
            try{
                const Json packet={{"schema",schema},{"id",id},{"generation",request.at("generation")},
                    {"progress",{{"stage",stage},{"elapsedMs",GetTickCount64()-progressStarted}}}};
                if constexpr(std::is_void_v<std::invoke_result_t<Emit&,const Json&>>)emit(packet);
                else if(!emit(packet))progressAvailable=false;
            }catch(...){progressAvailable=false;} // No later note can skip an omitted stage.
        };
        const Digest hash=[&](const std::wstring& file){return digest(file,current);};
        Options options;options.allowRepair=request.at("allowRepair").get<bool>();options.mayWrite=current;
        // Debug-marker creation remains in the existing parent preparation.
        // This operation touches only exact resources and the bounded local cache.
        // Bounded legacy receipt migration belongs to this already owned worker,
        // never the parent/UI thread. Refuse writes while another Loader owns the port.
        std::unique_ptr<PortLease> receiptLease;
        const auto mayMigrate=[&]{if(!current()||!options.allowRepair)return false;if(!receiptLease)receiptLease=std::make_unique<PortLease>(options.backendPort);return receiptLease->acquired()&&current();};
        progress("receipt");
        const auto migration=ymcc::deckysetup::migrateLegacyManagedReceipt(home,mayMigrate);
        receiptLease.reset(); // Cache obtains its own lease after the atomic move completes.
        Json contract,description;const auto invalid=ymcc::deckysetup::readManagedContract(home,description,contract,manifest);
        if(!migration.empty())detail={{"ready",false},{"reason",migration}};
        else {progress("inspect");detail=ymcc::deckysetup::inspect(steam,home,hash,manifest);}
        // Launch preflight passes allowRepair=false: validate every installed
        // byte above, but keep optional cache hashing/copying off that deadline.
        // Cache seeding/repair remains on the original writable preparation.
        if(migration.empty()&&invalid.empty()&&!contract.is_null()&&!options.allowRepair)
            detail["resourceMaintenanceDeferred"]=true;
        if(migration.empty()&&invalid.empty()&&!contract.is_null()&&options.allowRepair){
            const SafePath safe=[](const auto& path){return ymcc::deckysetup::safeManagedPath(path);};
            progress("cache");const auto maintenance=Cache(home,contract,hash,safe,current,options).run();
            if(maintenance.value("resourcesRepaired",0u)>0){progress("reinspect");detail=ymcc::deckysetup::inspect(steam,home,hash,manifest);}
            detail.update(maintenance);
        }
        if(!current()){detail["ready"]=false;detail["reason"]="preparation-cancelled";}
    }catch(...){detail={{"ready",false},{"reason","resource-worker-invalid"}};}
    exit=detail.value("ready",false)||detail.value("reason",std::string{})=="steam-debug-missing"?0:2;
    try{return emit(Json{{"schema",schema},{"id",request.value("id",std::string{})},{"generation",request.value("generation",std::string{})},{"detail",detail}}),true;}
    catch(...){exit=3;return true;}
}
// Parent owns the existing Owner + event-driven reader. No unjoined helper
// thread, no polling service; the 50ms cancellation check exists only during
// this bounded preparation call. stop() hard-revokes the precise owned Job.
inline Json prepare(const std::filesystem::path& steam,const std::filesystem::path& home,const std::filesystem::path& manifest,const std::function<bool()>& current,bool allowRepair){
    using namespace ymcc::deckybinding;
    if(steam.empty())return {{"ready",false},{"reason","steam-not-installed"}};
    Json result;
    if(!current||!current())return {{"ready",false},{"reason","preparation-cancelled"}};
    std::optional<Owner> ownerStorage;std::optional<clientdetail::EventReader> readerStorage;
    try{
        result={{"ready",false},{"reason","resource-worker-unavailable"}};
        ownerStorage.emplace();readerStorage.emplace();
    }
    catch(const std::bad_alloc&){
        // These constructors precede start/write and create no OS handles.
        // Still confirm the completed empty Owner's original cleanup before retry.
        bool destroyed=!readerStorage;
        if(ownerStorage){
            const auto stopped=ownerStorage->stop(0,400);const auto snapshot=ownerStorage->snapshot();
            destroyed=destroyed&&stopped.fullyDestroyed&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;
        }
        return {{"ready",false},{"reason",destroyed?"resource-worker-prewrite-memory-unavailable":"resource-worker-cleanup-incomplete"},
            {"resourceWorkerStage","construct"},{"resourceWorkerReplyReceived",false},{"resourceWorkerDestroyed",destroyed}};
    }
    auto& owner=*ownerStorage;auto& reader=*readerStorage;
    // Owner is local to this call; the observation never owns or closes its handle.
    struct ChildObservation{HANDLE value=nullptr;} child;
    // Use the same bounded cleanup on normal and exceptional exits. Nothing in
    // result construction or optional diagnostics may leave it to destructors.
    bool cleanupDone=false,resourcesDestroyed=false;
    const auto cleanup=[&]() noexcept{
        if(cleanupDone)return;cleanupDone=true;
        child.value=nullptr; // End observation before Owner releases its real handle.
        owner.requestCancel();reader.requestCancel();
        const auto stopped=owner.stop(0,400);const auto readerStopped=reader.stop(200);
        const auto snapshot=owner.snapshot();resourcesDestroyed=stopped.fullyDestroyed&&readerStopped&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;
    };
    struct CleanupOnExit{decltype(cleanup)& action;~CleanupOnExit() noexcept{action();}} cleanupOnExit{cleanup};
    const auto startedAt=GetTickCount64(),deadline=startedAt+2500;bool childExitedObserved=false;
    // Failure facts only. Keep the original readiness, cancellation and cleanup
    // decisions; capture child exit BEFORE our own cancellation can cause it.
    const char* resourceStage="start";const char* resourceReadKind=nullptr;
    DWORD resourceReadError=0,resourceSendError=0,resourceWaitError=0;bool replyReceived=false;
    DWORD resourceStartError=ERROR_SUCCESS;bool prewriteMemoryFailure=false,workerStarted=false,writeAttempted=false;
    unsigned progressCount=0;ULONGLONG childElapsedMs=0;
    try{
        auto started=owner.start();workerStarted=static_cast<bool>(started);
        if(!started){
            resourceStartError=started.error;
            // This Owner failure precedes construction or writing of the request.
            // A memory label alone is insufficient: preserve its actual cleanup facts.
            prewriteMemoryFailure=started.code==Code::systemError&&started.error==ERROR_NOT_ENOUGH_MEMORY&&
                started.fullyDestroyed&&!started.ioPending;
        }
        require(static_cast<bool>(started),"resource-worker-start-failed");
        resourceStage="prepare";const auto generation=std::to_string(started.generation);
        char id[33]{};snprintf(id,sizeof(id),"%016llx%016llx",static_cast<unsigned long long>(GetCurrentProcessId()),static_cast<unsigned long long>(started.generation));
        const Json request={{"schema",schema},{"operation","prepare"},{"id",id},{"generation",generation},{"home",text(home)},{"steam",text(steam)},{"manifest",text(manifest)},{"allowRepair",allowRepair}};
        resourceStage="send";const auto bytes=request.dump();
        {
            const std::vector<std::uint8_t> requestPayload(bytes.begin(),bytes.end());
            // A failed payload allocation is still pre-write. Once the original
            // write call is entered, never infer that its outcome was untouched.
            writeAttempted=true;
            const auto sent=owner.writeFrame(requestPayload,150);
            // Preserve the typed send result before cleanup changes Win32 last-error.
            // A failed write keeps its existing uncertainty and is never replayed.
            if(!sent)resourceSendError=sent.error;
            require(static_cast<bool>(sent),"resource-worker-send-failed");
        }
        resourceStage="attach";child.value=owner.borrowChildProcessForObservation(&resourceReadError);
        bool attached=false;
        if(child.value){
            const auto pipe=owner.copyReadPipe(&resourceReadError);
            if(pipe)attached=reader.attach(pipe,&resourceReadError);
        }
        if(!attached)resourceReadKind="attach";
        require(attached,"resource-worker-read-failed");resourceStage="read";
        for(;;){
            if(!current()){result["reason"]="preparation-cancelled";break;}
            const auto now=GetTickCount64();if(now>=deadline){result["reason"]="resource-worker-deadline";break;}
            std::vector<std::uint8_t> payload;DWORD error=0;const auto packet=reader.advance(payload,error);
            if(packet==clientdetail::EventReader::Read::frame){
                std::vector<std::set<std::string>> keys;
                const auto reply=Json::parse(payload,[&](int depth,Json::parse_event_t event,Json& parsed){
                    require(depth<=32,"resource-worker-reply-invalid");
                    if(event==Json::parse_event_t::object_start)keys.emplace_back();
                    else if(event==Json::parse_event_t::object_end)keys.pop_back();
                    else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second,"resource-worker-reply-invalid");
                    return true;
                });
                require(reply.is_object()&&reply.size()==4&&reply.value("schema",std::string{})==schema&&reply.at("id")==id&&reply.at("generation")==generation,"resource-worker-reply-invalid");
                if(reply.contains("progress")){
                    const auto& note=reply.at("progress");
                    require(progressCount<progressStages.size()&&note.is_object()&&note.size()==2&&
                        note.contains("stage")&&note["stage"].is_string()&&note["stage"]==progressStages[progressCount]&&
                        note.contains("elapsedMs")&&note["elapsedMs"].is_number_unsigned()&&note["elapsedMs"]>=childElapsedMs&&note["elapsedMs"]<=3600000ull,"resource-worker-reply-invalid");
                    childElapsedMs=note["elapsedMs"].get<ULONGLONG>();++progressCount;continue;
                }
                resourceStage="reply";replyReceived=true;
                require(reply.at("detail").is_object(),"resource-worker-reply-invalid");
                // Only the parent can classify its deadline or typed pre-write
                // memory failure. A reply cannot grant its own resource retry.
                require(!reply.at("detail").contains("reason")||
                    (reply.at("detail").at("reason")!="resource-worker-prewrite-memory-unavailable"&&
                     reply.at("detail").at("reason")!="resource-worker-deadline"),"resource-worker-reply-invalid");
                result=reply.at("detail");break;
            }
            if(packet!=clientdetail::EventReader::Read::pending){
                resourceReadKind=packet==clientdetail::EventReader::Read::ended?"ended":"failed";
                resourceReadError=error;
                result["reason"]="resource-worker-read-failed";break;
            }
            // A terminated process stays signaled. Observe that edge once,
            // then wait only for pipe completion; an inherited endpoint may
            // still be open in an owned descendant. Keep cancellation/deadline.
            HANDLE waiting[]{reader.event(),child.value};const auto wait=WaitForMultipleObjects(childExitedObserved?1u:2u,waiting,FALSE,static_cast<DWORD>((std::min)(50ull,deadline-now)));
            if(wait==WAIT_OBJECT_0+1)childExitedObserved=true;
            if(wait==WAIT_FAILED){resourceWaitError=GetLastError();result["reason"]="resource-worker-wait-failed";break;}
        }
    }catch(const std::bad_alloc&){
        // Start rejection keeps its original typed error. Only an accepted
        // worker, before any write attempt, can classify preparation allocation.
        if(workerStarted&&!writeAttempted)prewriteMemoryFailure=true;
        result={{"ready",false},{"reason","resource-worker-failed"}};
    }catch(const std::exception& error){const std::string reason=error.what();result={{"ready",false},{"reason",reason.starts_with("resource-worker-")?reason:"resource-worker-failed"}};}
    catch(...){result={{"ready",false},{"reason","resource-worker-failed"}};}
    const auto elapsedBeforeCleanup=GetTickCount64()-startedAt;
    // Capture scalar facts on the already held child before our cancellation.
    FILETIME childCreated{},childExit{},childKernel{},childUser{};
    const bool childCpuKnown=child.value&&GetProcessTimes(child.value,&childCreated,&childExit,&childKernel,&childUser);
    DWORD observedExit=0;
    const bool exitKnown=child.value&&WaitForSingleObject(child.value,0)==WAIT_OBJECT_0&&GetExitCodeProcess(child.value,&observedExit);
    cleanup();
    if(!resourcesDestroyed){result["ready"]=false;result["reason"]="resource-worker-cleanup-incomplete";}
    else if(prewriteMemoryFailure){
        // Only this typed pre-request failure may reuse the original resource
        // retry window. Attach/read failures occur AFTER write and keep their
        // existing uncertainty; a memory error there grants no replay.
        result["ready"]=false;result["reason"]="resource-worker-prewrite-memory-unavailable";
    }
    try{
        // Enrichment is optional; readiness and cleanup were decided above.
        result["resourceWorkerStage"]=resourceStage;
        result["resourceWorkerChildStage"]=progressCount?progressStages[progressCount-1]:"none";
        result["resourceWorkerProgressCount"]=progressCount;
        if(progressCount)result["resourceWorkerChildElapsedMs"]=childElapsedMs;
        if(resourceReadKind){result["resourceWorkerReadKind"]=resourceReadKind;result["resourceWorkerReadError"]=resourceReadError;}
        if(resourceStartError!=ERROR_SUCCESS)result["resourceWorkerStartError"]=resourceStartError;
        if(resourceSendError!=ERROR_SUCCESS)result["resourceWorkerSendError"]=resourceSendError;
        if(resourceWaitError!=ERROR_SUCCESS)result["resourceWorkerWaitError"]=resourceWaitError;
        result["resourceWorkerReplyReceived"]=replyReceived;
        result["resourceWorkerElapsedMs"]=elapsedBeforeCleanup;
        result["resourceWorkerChildCpuKnown"]=childCpuKnown;
        if(childCpuKnown){
            const auto ticks=[](const FILETIME& value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
            result["resourceWorkerChildCpuMs"]=(ticks(childKernel)+ticks(childUser))/10000ull;
        }
        result["resourceWorkerChildExitKnown"]=exitKnown;
        if(exitKnown)result["resourceWorkerChildExitCode"]=observedExit;
        result["resourceWorkerDestroyed"]=resourcesDestroyed;
    }catch(const std::bad_alloc&){
        // Do not replace the resource outcome because a diagnostic cannot grow.
    }
    return result;
}
}
