#pragma once
#include <set>
#include <string_view>
#include <vector>
// Final context recovery borrows the existing same-image private child + Job.
// Slow Steam, settings or disk IO never runs on the Runtime owner. The parent
// observes cancellation every 50ms only during this bounded call, then destroys
// the exact child subtree. No new executable, service, global timer or cache wipe.
#include "decky_resource_worker.h"
#include "decky_sidebar_manual_repair.h"
#include "decky_listener_proxy.h"
#include "steam_command_channel.h"
namespace ymcc::deckyrepair::cleanupworker {
using Json=nlohmann::json;
using deckybinding::worker::require;
class Active {
    inline static std::mutex mutex_;inline static deckybinding::Owner* owner_=nullptr;
public:
    explicit Active(deckybinding::Owner& owner){std::lock_guard lock(mutex_);if(owner_)throw std::runtime_error("decky-cleanup-already-active");owner_=&owner;}
    ~Active(){std::lock_guard lock(mutex_);owner_=nullptr;}
    static void cancel(){std::lock_guard lock(mutex_);if(owner_)owner_->requestCancel();}
};
// User policy 2026-11-01: recovery patience is independent of OFF/exit drain.
inline constexpr DWORD recoveryWindowMs=30000;
inline constexpr const char* schema="ymcc.decky.context-cleanup.v1";
inline constexpr const char* lifecycleSchema="ymcc.decky.managed-lifecycle.v1";
inline unsigned long long processBirth(HANDLE process){FILETIME created{},exit{},kernel{},user{};return GetProcessTimes(process,&created,&exit,&kernel,&user)?(static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime:0;}
inline std::string nativeHost(){const auto birth=processBirth(GetCurrentProcess());return birth?std::to_string(GetCurrentProcessId())+":"+std::to_string(birth):std::string{};}
// The private same-image pipe, not a frontend IPC, carries native ownership proof.
// Its two operations inspect recovery metadata or confirm an actual owned ready
// backend; neither operation calls RestartJSContext or edits business settings.
inline bool lifecycleRequest(const Json& args,const Json& state){
    try{const auto task=args.at("task").get<std::string>();
        return args.is_object()&&args.size()==6&&(task=="inspect"||task=="confirm")&&
            !deckyupdate::managed::document(args.at("modeEpoch").get<std::string>()).empty()&&
            deckyupdate::fingerprint(args.at("frontendBuildId").get<std::string>())&&
            deckyupdate::loaderVersion(args.at("loaderVersion").get<std::string>())&&
            deckyupdate::validTicket(state.at("cleanupNativeHost").get<std::string>())&&state.at("enabled")==true&&
            // Inspect may either continue a retained no-backend host or read
            // back an existing owned backend's ledger after a lost confirm ACK.
            // Both remain read-only; a nonzero backend is verified in the child.
            ((task=="inspect"&&args.at("backendPid")==0&&args.at("backendBirth")=="0"&&state.at("pid")==0)||
             (state.at("pid").get<unsigned long long>()>0&&state.at("phase")=="loader-started"&&
              deckyupdate::validTicket(std::to_string(args.at("backendPid").get<DWORD>())+":"+args.at("backendBirth").get<std::string>())));
    }catch(...){return false;}
}
struct Request {std::string id,generation;bool lifecycle=false;deckybinding::worker::Request identity;Json args,state;};
inline Request parse(const Json& raw){
    using namespace deckybinding::worker;
    const auto kind=raw.value("schema",std::string{});
    require(raw.is_object()&&raw.size()==6&&(kind==schema||kind==lifecycleSchema),"decky-cleanup-worker-invalid");
    Request value;value.lifecycle=kind==lifecycleSchema;value.id=raw.at("id").get<std::string>();require(hex(value.id,32),"decky-cleanup-worker-invalid");
    decimal(raw.at("generation"));value.generation=raw.at("generation").get<std::string>();
    const auto& steam=raw.at("steam");require(steam.is_object()&&steam.size()==6+static_cast<std::size_t>(steam.contains("identitySource"))&&steam.at("fromUi").is_boolean(),"decky-cleanup-worker-invalid");
    value.identity.steam.root=steamlive::normalizedPath(wide(steam.at("root")));
    value.identity.steam.pid=number(steam.at("pid"));value.identity.steam.created=decimal(steam.at("created"));
    value.identity.steam.account=number(steam.at("account"));value.identity.windowsSession=number(steam.at("windowsSession"),false);
    value.identity.liveUiAccount=steam.at("fromUi").get<bool>();
    value.identity.delegatedIdentity=steam.contains("identitySource");
    if(value.identity.delegatedIdentity)require(steam.at("identitySource")=="shared-steam-observer.v1","decky-cleanup-worker-invalid");
    require(std::filesystem::path(value.identity.steam.root).is_absolute(),"decky-cleanup-worker-invalid");
    value.args=raw.at("args");value.state=raw.at("state");
    require(value.state.dump().size()<=4096&&(value.lifecycle?lifecycleRequest(value.args,value.state):requestMatches(value.args,value.identity.steam,value.state,value.args.value("reopenBigPicture",false)?Origin::ReopenBigPicture:Origin::FinalCleanup)),"decky-cleanup-worker-invalid");
    return value;
}
template<class Run,class Emit>bool tryRun(const Json& raw,HANDLE cancel,Emit&& emit,int& exit,Run&& run){
    if(!raw.is_object())return false;const auto kind=raw.value("schema",std::string{});if(kind!=schema&&kind!=lifecycleSchema)return false;
    Json detail={{"ok",false},{"mutated",false},{"reason","decky-cleanup-worker-invalid"}};
    try{const auto request=parse(raw);detail=run(request,cancel);}
    catch(...){detail={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-result-unconfirmed"}};}
    exit=detail.value("ok",false)?0:2;
    emit(Json{{"schema",kind},{"id",raw.value("id",std::string{})},{"generation",raw.value("generation",std::string{})},{"detail",detail}});return true;
}
// Pure projection of the original Steam owner's publication. A UI-selected
// account may differ; only the verified process binds this Windows fact.
inline bool sharedIdentityMatches(const steamsession::Session& steam,const std::shared_ptr<const steamsession::IdentitySnapshot>& identity){
    return identity&&identity->presence==1&&steam.pid&&steam.created&&!steam.root.empty()&&
        identity->session.root==steam.root&&identity->session.pid==steam.pid&&
        identity->session.created==steam.created&&identity->windowsSessionKnown;
}
inline Json dispatch(const steamsession::Session& steam,bool fromUi,const std::shared_ptr<const steamsession::IdentitySnapshot>& identity,const Json& args,const Json& state,const std::function<bool()>& current,DWORD recoveryMs=recoveryWindowMs,const char* kind=schema,std::shared_ptr<steamsession::IdentityDelegation> delegation={},deckybinding::listenerproxy::Read listenerRead={},deckybinding::listenerproxy::Post listenerPost={},const std::function<bool()>& beforeFirstEffect={}){
    using namespace deckybinding;
    const auto requestCurrent=[&]{return current&&current()&&(!delegation||delegation->current());};
    Json result={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-result-unconfirmed"}};
    if(!recoveryMs||recoveryMs>recoveryWindowMs)return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-invalid-window"}};
    if(!current||!requestCurrent())return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-cancelled"}};
    const bool lifecycle=std::string_view(kind)==lifecycleSchema;
    if(!lifecycle&&std::string_view(kind)!=schema)return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-worker-invalid"}};
    // Unknown is a pre-child refusal, never a reason for independent discovery.
    // Original caller revocation and child document/process checks remain live.
    if(!sharedIdentityMatches(steam,identity)||(delegation&&!delegation->matches(steam.root,steam.pid,steam.created,steam.account,identity->windowsSession,fromUi)))return {{"ok",false},{"mutated",false},
        {"reason","decky-cleanup-worker-session-invalid"},{"workerStage","identity"},
        {"cleanupWorkerReplyReceived",false},{"cleanupWorkerDestroyed",true},
        {"cleanupWorkerHandles",0u},{"cleanupWorkerProcesses",0u}};
    const auto windowsSession=identity->windowsSession;
    auto privateState=state;privateState["cleanupNativeHost"]=nativeHost();
    steamcommand::ChildRetirement commandChild(steamcommand::active);
    Owner owner;clientdetail::EventReader reader;detail::Handle child;bool replyReceived=false,childExitedObserved=false;
    require(commandChild.attach(owner.snapshot().generation),"decky-cleanup-worker-command-reservation-invalid");
    const auto startedAt=GetTickCount64();const auto deadline=startedAt+recoveryMs;
    std::shared_ptr<listenerproxy::Work> listenerWork;
    struct CancelFacts {std::shared_ptr<listenerproxy::Work>& work;~CancelFacts(){if(work)work->cancel();}} cancelFacts{listenerWork};
    std::unique_ptr<Active> active;
    try{
        result["workerStage"]="register";active=std::make_unique<Active>(owner);if(!requestCurrent())owner.requestCancel();
        result["workerStage"]="start";const auto started=owner.start(delegation?delegation->cancellationEvent():nullptr);require(static_cast<bool>(started),"decky-cleanup-worker-start-failed");
        result["workerStage"]="identity";const auto generation=std::to_string(started.generation);char id[33]{};
        snprintf(id,sizeof(id),"%016llx%016llx",static_cast<unsigned long long>(GetCurrentProcessId()),static_cast<unsigned long long>(started.generation));
        Json steamIdentity={{"root",deckyresources::worker::text(steam.root)},{"pid",steam.pid},{"created",std::to_string(steam.created)},{"account",steam.account},{"windowsSession",windowsSession},{"fromUi",fromUi}};
        if(delegation)steamIdentity["identitySource"]="shared-steam-observer.v1";
        result["workerStage"]="request";const Json request={{"schema",kind},{"id",id},{"generation",generation},{"steam",steamIdentity},{"args",args},{"state",privateState}};
        result["workerStage"]="send";const auto bytes=request.dump();require(bytes.size()<=16384&&static_cast<bool>(owner.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),150)),"decky-cleanup-worker-send-failed");
        result["workerStage"]="attach";child.value=owner.copyChildProcess();require(child.value&&reader.attach(owner.copyReadPipe()),"decky-cleanup-worker-read-failed");
        const auto sequence=std::make_shared<listenerproxy::Sequence>();
        listenerproxy::Server listenerServer(id,generation,std::move(listenerRead),sequence);
        steamcommand::ChildServer commandServer(id,generation,sequence,steamcommand::active,
            args.value("reopenBigPicture",false)?steamcommand::Plan::Reopen:steamcommand::Plan::Single);
        result["workerStage"]="read";for(;;){
            if(!requestCurrent()){result["reason"]="decky-cleanup-cancelled-unconfirmed";break;}
            const auto now=GetTickCount64();if(now>=deadline){result["reason"]="decky-cleanup-worker-deadline";break;}
            if(listenerWork){
                if(!listenerproxy::left(listenerWork->end())){result["reason"]="decky-cleanup-listener-facts-unconfirmed";break;}
                if(listenerWork->ready()){
                    const auto facts=listenerWork->take();listenerWork->cancel();
                    if(!requestCurrent()||!owner.writeFrame(listenerproxy::wire(facts),listenerproxy::left(listenerWork->end(),150),started.generation)){
                        result["reason"]="decky-cleanup-listener-facts-unconfirmed";break;
                    }
                    listenerWork.reset();
                }
            }
            std::vector<std::uint8_t> payload;DWORD error=0;const auto packet=reader.advance(payload,error);
            if(packet==clientdetail::EventReader::Read::frame){
                // Match the existing typed binding client's bounded, unique-key
                // policy. Last-key-wins must not turn an uncertain/mutating
                // native receipt into apparent successful readonly completion.
                std::vector<std::set<std::string>> objectKeys;
                const auto reply=Json::parse(payload,[&](int depth,Json::parse_event_t event,Json& parsed){
                    require(depth<=32,"decky-cleanup-worker-reply-invalid");
                    if(event==Json::parse_event_t::object_start)objectKeys.emplace_back();
                    else if(event==Json::parse_event_t::object_end)objectKeys.pop_back();
                    else if(event==Json::parse_event_t::key)require(!objectKeys.empty()&&objectKeys.back().insert(parsed.get<std::string>()).second,"decky-cleanup-worker-reply-invalid");
                    return true;
                });
                if(steamcommand::ChildServer::isQuery(reply)){
                    require(!listenerWork,"decky-cleanup-worker-reply-invalid");
                    const auto answer=commandServer.serve(reply,requestCurrent,deadline);
                    // The existing server already validated this exact action,
                    // stage, child and ticket. Until the reply is sent the child
                    // cannot use the grant. A denied release stays quarantined
                    // under the original grant/child-retirement rules.
                    const auto& command=reply.at("detail");
                    auto replyDeadline=deadline;
                    if(beforeFirstEffect&&command.at("phase")=="enter"&&command.at("stage")==0&&answer.at("detail").at("ok")==true){
                        replyDeadline=(std::min)(deadline,listenerproxy::decimal(command.at("deadline")));
                        if(!listenerproxy::left(replyDeadline)||!requestCurrent()||!beforeFirstEffect()||!requestCurrent()||!listenerproxy::left(replyDeadline)){
                            result["reason"]="decky-cleanup-command-grant-unconfirmed";break;
                        }
                    }
                    if(!owner.writeFrame(listenerproxy::wire(answer),listenerproxy::left(replyDeadline,150),started.generation)){
                        result["reason"]="decky-cleanup-command-grant-unconfirmed";break;
                    }
                    continue;
                }
                if(listenerproxy::isQuery(reply)){
                    require(!listenerWork,"decky-cleanup-worker-reply-invalid");
                    // Original caller predicates may borrow their stack. Never
                    // put them in the publisher's queued callback; this loop
                    // keeps checking them on its existing cancellation slice.
                    listenerWork=listenerServer.prepare(reply,[delegation]{return !delegation||delegation->current();},deadline);
                    const auto work=listenerWork;
                    const bool posted=listenerPost&&listenerPost([work]{work->run();},[work]{work->publish();});
                    if(!posted){
                        work->cancel();
                        if(!requestCurrent()||!owner.writeFrame(listenerproxy::wire(work->busy()),listenerproxy::left(work->end(),150),started.generation)){
                            result["reason"]="decky-cleanup-listener-facts-unconfirmed";break;
                        }
                        listenerWork.reset();
                    }
                    continue;
                }
                require(reply.is_object()&&reply.size()==4&&reply.value("schema",std::string{})==kind&&reply.at("id")==id&&reply.at("generation")==generation&&reply.at("detail").is_object(),"decky-cleanup-worker-reply-invalid");
                const auto& detail=reply.at("detail");
                // Validate before publishing acceptance. A parsed but ill-typed
                // body must not throw later in the owner and leave a retry armed.
                // Success/failure flags are required; optional fields retain
                // their existing absence semantics and must be typed if present.
                require(detail.contains("ok")&&detail["ok"].is_boolean()&&
                    detail.contains("mutated")&&detail["mutated"].is_boolean()&&
                    (!detail.contains("uncertain")||detail["uncertain"].is_boolean())&&
                    (!detail.contains("reason")||detail["reason"].is_string()),"decky-cleanup-worker-reply-invalid");
                if(!requestCurrent()){result["reason"]="decky-cleanup-cancelled-unconfirmed";break;}
                result=detail;replyReceived=true;break;
            }
            if(packet!=clientdetail::EventReader::Read::pending){
                // Preserve the reader's typed outcome. A rejected length or
                // partial frame is a protocol fault, not an undelivered reply.
                result["cleanupWorkerReadKind"]=packet==clientdetail::EventReader::Read::ended?"ended":"failed";
                result["cleanupWorkerReadError"]=error;
                result["reason"]=(error==ERROR_BAD_LENGTH||error==ERROR_HANDLE_EOF)?
                    "decky-cleanup-worker-frame-invalid":"decky-cleanup-worker-read-failed";
                break;
            }
            // A terminated child remains signaled while a descendant may still
            // hold its pipe endpoint. Observe exit once, then wait on the pipe;
            // keep the existing recovery deadline and cancellation slices.
            HANDLE waiting[]{reader.event(),child.value};const auto wait=WaitForMultipleObjects(childExitedObserved?1u:2u,waiting,FALSE,static_cast<DWORD>((std::min)(50ull,deadline-now)));
            if(wait==WAIT_OBJECT_0+1)childExitedObserved=true;
            if(wait==WAIT_FAILED)break;
        }
    }catch(const nlohmann::json::exception& error){result["protocolError"]=error.id;}
    catch(const std::exception& error){const std::string reason=error.what();if(reason.starts_with("decky-cleanup-worker-")&&reason.size()<=96)result["reason"]=reason;}
    catch(...){/* Unknown dispatch state stays uncertain; never replay it. */}
    const auto workFinishedAt=lifecycle?GetTickCount64():0ull;
    if(listenerWork)listenerWork->cancel();
    owner.requestCancel();reader.requestCancel();const auto stopped=owner.stop(0,400);const auto readStopped=reader.stop(200);
    // Use only the existing query handle after teardown. Do not wait for a
    // sample or report an in-flight CPU total as the cost of the whole child.
    FILETIME created{},exited{},kernel{},user{};
    const bool childCpuKnown=lifecycle&&stopped.fullyDestroyed&&readStopped&&child.value&&
        WaitForSingleObject(child.value,0)==WAIT_OBJECT_0&&GetProcessTimes(child.value,&created,&exited,&kernel,&user);
    if(child.value)CloseHandle(child.value);child.value=nullptr;
    const auto snapshot=owner.snapshot();const bool destroyed=stopped.fullyDestroyed&&readStopped&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;
    commandChild.confirmed(destroyed);
    if(lifecycle)try{
        Json timing={{"workMs",workFinishedAt-startedAt},{"cleanupMs",GetTickCount64()-workFinishedAt}};
        if(destroyed&&childCpuKnown){
            const auto ticks=[](const FILETIME& t){return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            timing["childCpuMs"]=(ticks(kernel)+ticks(user))/10000ull;
        }
        result["cleanupWorkerTiming"]=std::move(timing);
    }catch(...){/* Optional observation cannot alter the recovery outcome. */}
    result["cleanupWorkerReplyReceived"]=replyReceived;
    result["cleanupWorkerDestroyed"]=destroyed;result["cleanupWorkerHandles"]=snapshot.handles;result["cleanupWorkerProcesses"]=snapshot.jobActiveProcesses;
    if(!destroyed){result["ok"]=false;result["reason"]="decky-cleanup-worker-cleanup-incomplete";result["uncertain"]=true;}
    return result;
}
// Normal product callers use the sole Steam publisher. The optional lease on
// dispatch above is retained for old unmarked private-protocol compatibility.
// Hold the per-request lease until the existing child/pipe cleanup has returned;
// never discover an account here or fall back to a new unleased child.
template<class SteamSource>
inline Json dispatchShared(SteamSource& source,const steamsession::Session& steam,bool fromUi,
    const std::shared_ptr<const steamsession::IdentitySnapshot>& identity,const Json& args,const Json& state,
    const std::function<bool()>& current,DWORD recoveryMs=recoveryWindowMs,const char* kind=schema,const std::function<bool()>& beforeFirstEffect={}){
    if(!current||!current())return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-cancelled"}};
    auto delegation=sharedIdentityMatches(steam,identity)?source.delegateIdentity(identity,steam.account,fromUi):nullptr;
    if(!delegation||!delegation->matches(steam.root,steam.pid,steam.created,steam.account,identity->windowsSession,fromUi)){
        source.requestIdentityRefresh();
        return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-account-unconfirmed"},
            {"awaitCurrentIdentity",true},{"cleanupNotStarted",true},{"sessionCheck","shared-process-delegation"}};
    }
    const auto read=[view=source.identityReadView(),steam,windowsSession=identity->windowsSession](ULONGLONG end,const std::function<bool()>& stillCurrent){
        return deckybinding::listenerproxy::readShared(view,steam,windowsSession,end,stillCurrent);
    };
    const auto post=[&source](std::function<void()> work,std::function<void()> done){return source.postFactRead(std::move(work),std::move(done));};
    return dispatch(steam,fromUi,identity,args,state,current,recoveryMs,kind,std::move(delegation),read,post,beforeFirstEffect);
}

}
