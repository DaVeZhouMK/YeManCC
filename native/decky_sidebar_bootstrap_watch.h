#pragma once
// Event-driven lifetime of ONE already verified SharedJSContext CDP connection.
// Watch worker has no timer, localhost scan, Page reload, Loader launch or settings write.
// The injected version-update script owns only bounded failed-update/receipt timers.
#include "decky_sidebar_async_http.h"
#include "steam_live_cdp.h"
#include "steam_failure_diagnostics.h"
#include "decky_sidebar_health.h"
#include <deque>
#include <functional>
#include <future>
#include <set>
#include <thread>
namespace ymcc::deckymirror {
inline std::string bootstrapSafeReason(const std::exception& error){
    const std::string value=error.what();
    if(value.size()<=96&&(value=="decky-context-already-loaded"||value=="mirror-retained-frontend-changed"||value.rfind("mirror-bootstrap-",0)==0||value.rfind("live-",0)==0)&&
       value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos)return value;
    return "mirror-bootstrap-invalid-message";
}
// Member destruction is reverse declaration order: close connection before
// session after the exact request/callback state has returned.
struct BootstrapHttpParents {
    const std::shared_ptr<ymcc::steamlive::HttpHandle> session;
    const std::shared_ptr<ymcc::steamlive::HttpHandle> connection;
    BootstrapHttpParents(std::shared_ptr<ymcc::steamlive::HttpHandle> s,std::shared_ptr<ymcc::steamlive::HttpHandle> c) noexcept
        :session(std::move(s)),connection(std::move(c)){}
};
class BootstrapWatch {
    std::thread worker_;
    std::shared_ptr<CancelToken> cancellation_;
    // One finite, original watch owner. Never start a replacement over debt.
    std::shared_ptr<AsyncRequest> retired_;
    AsyncRequest::Retirement retirement_;
    // Accepted close has no reusable numeric handle. Its existing callback
    // already owns state/parents; observe it without prolonging their lifetime.
    std::weak_ptr<AsyncState> closing_;
public:
    using Diagnostic=std::function<void(const nlohmann::json&)>;
    BootstrapWatch()=default;
    BootstrapWatch(const BootstrapWatch&)=delete;
    ~BootstrapWatch(){stop();}
    bool stop() noexcept {
        if(cancellation_)cancellation_->cancel();
        if(worker_.joinable())worker_.join();
        cancellation_.reset();
        if(retired_){
            retirement_=retired_->retire(GetTickCount64());
            if(retirement_.returned())retired_.reset();
        }
        if(const auto closing=closing_.lock()){
            std::lock_guard lock(closing->mutex);
            retirement_.finalNotification=closing->finalNotification;
            if(retirement_.finalNotification)closing_.reset();
        }else if(retirement_.attempted&&retirement_.closeAccepted){
            // Successful registration self-retains through its real final
            // callback. Expiry therefore proves this exact state returned.
            retirement_.finalNotification=true;
        }
        return !retired_&&closing_.expired();
    }
    AsyncRequest::Retirement retirement() const noexcept {return retirement_;}
    std::string start(std::shared_ptr<ymcc::steamlive::HttpHandle> session,
        std::shared_ptr<ymcc::steamlive::HttpHandle> connection,
        std::shared_ptr<AsyncRequest> socket,std::shared_ptr<CancelToken> cancellation,
        std::function<bool()> owned,std::string expression,std::string runId,DWORD contextPid,
        Diagnostic diagnostic={},ULONGLONG startupDeadline=0,std::shared_ptr<BootstrapHttpParents> parentScope={}) {
        if(!stop())return "mirror-bootstrap-cleanup-incomplete";
        cancellation_=cancellation;
        // Retain actual HTTP parents with this exact async callback state, so
        // watcher stack release cannot close them before the final callback.
        if(parentScope&&(parentScope->session!=session||parentScope->connection!=connection))return "mirror-bootstrap-parents-changed";
        auto parents=parentScope?std::move(parentScope):std::make_shared<BootstrapHttpParents>(session,connection);
        socket->retainParents(parents);
        auto readiness=std::make_shared<std::promise<std::string>>();auto ready=readiness->get_future();
        worker_=std::thread([this,session=std::move(session),connection=std::move(connection),socket=std::move(socket),
            cancellation=std::move(cancellation),owned=std::move(owned),expression=std::move(expression),
            runId=std::move(runId),contextPid,diagnostic=std::move(diagnostic),readiness,startupDeadline]() mutable {
            using Json=nlohmann::json;
            unsigned long long generation=0;unsigned nextId=8200;bool readyReported=false;int activeContext=0;
            std::deque<Json> events;
            int queuedConnectionContext=0;
            unsigned queuedConnectionSources=0;
            Json commandFailure; // Same command only; never retained across another request.
            const auto notify=[&](const char* stage,const std::string& reason={},const Json* sourceFailure=nullptr){
                try{if(diagnostic){
                    Json row={{"bootstrapStage",stage},{"reason",reason},{"contextPid",contextPid},{"contextGeneration",generation},{"runId",runId}};
                    if(generation>1&&std::string_view(stage)=="context-attached")row["contextAttachedTick"]=GetTickCount64();
                    if(!reason.empty()){
                        if(sourceFailure&&sourceFailure->is_object())row["steamFailure"]=*sourceFailure;
                        else if(commandFailure.is_object())row["steamFailure"]=commandFailure;
                    }
                    diagnostic(row);
                }}catch(...){}
                commandFailure=nullptr; // Never attach the preceding command error to a later socket/event failure.
            };
            // Observe only events already consumed for our active context.
            // Optional facts have a per-watch ceiling and never consume or
            // clear commandFailure, read Steam again, or authorize recovery.
            unsigned contextInvalidations=0;
            const auto observeContextInvalidation=[&](const char* reason){
                if(!diagnostic||contextInvalidations>=16)return;
                ++contextInvalidations;
                try{diagnostic(Json{{"bootstrapStage","context-invalidated"},{"reason",reason},
                    {"contextPid",contextPid},{"contextGeneration",generation},{"runId",runId}});}catch(...){}
            };
            const auto requireOwner=[&]{
                ymcc::steamlive::listenersource::Deadline listenerDeadline(GetTickCount64()+2500);
                if(!owned())throw std::runtime_error("mirror-bootstrap-owner-mismatch");
            };
            // Use the existing accepted-context policy before queue accounting.
            // Other pages/isolated worlds are ignored by the consumer too; they
            // cannot spend its bounded context replay capacity while a reply waits.
            const auto usableContext=[](const Json& value){
                const auto auxiliary=value.value("auxData",Json::object());
                return value.contains("id")&&value["id"].is_number_integer()&&
                    value.value("origin",std::string{})=="https://steamloopback.host"&&auxiliary.value("isDefault",false);
            };
            // Account and fatal signals keep their original replay admission.
            // Connection hints must also fit the existing consumer's limit.
            enum class ReplayBinding:unsigned { ignore=0, opening=1, completion=2, other=4 };
            const auto replayBinding=[&](const Json& packet){
                try{const auto& params=packet.at("params");
                    if(params.value("name",std::string{})!="__YMCC_DECKY_HEALTH_V1")return ReplayBinding::ignore;
                    const auto raw=params.value("payload",std::string{});if(raw.size()>256)return ReplayBinding::ignore;
                    const auto bindingValue=Json::parse(raw,nullptr,false);
                    if(!bindingValue.is_object()||bindingValue.size()!=2||bindingValue.value("runId",std::string{})!=runId)return ReplayBinding::ignore;
                    const auto code=bindingValue.value("code",std::string{});
                    if(code=="decky-connection-check"||code=="decky-connection-result"){
                        if(raw.size()>ymcc::deckyhealth::scopedSignalPayloadLimit)return ReplayBinding::ignore;
                        return code=="decky-connection-check"?ReplayBinding::opening:ReplayBinding::completion;
                    }
                    return code=="decky-account-changed"||code=="decky-render-failed"?ReplayBinding::other:ReplayBinding::ignore;
                }catch(...){return ReplayBinding::ignore;}
            };
            // Relevant callbacks belong to the active context or a creation
            // already waiting in this queue. Other pages can report through the
            // same Runtime agent, but the consumer cannot act on their signals.
            const auto relevantContext=[&](int id){
                if(id<=0)return false;
                if(id==activeContext)return true;
                for(const auto& queued:events)if(queued.value("method",std::string{})=="Runtime.executionContextCreated"&&
                    queued.at("params").at("context").value("id",0)==id)return true;
                return false;
            };
            // Same original socket/binding; console history remains ignored.
            // No readback, queue entry, recovery signal or commandFailure change.
            std::set<std::string> officialSocketObservations;
            const auto observeOfficialSocket=[&](const Json& packet){
                try{
                    const auto facts=ymcc::deckyhealth::officialSocketSignal(packet,activeContext,runId);
                    if(facts.is_null())return false;
                    if(!diagnostic||officialSocketObservations.size()>=16)return true;
                    const auto signature=facts.dump();
                    if(!officialSocketObservations.insert(signature).second)return true;
                    try{diagnostic(Json{{"bootstrapStage","official-socket-close"},{"contextPid",contextPid},
                        {"contextGeneration",generation},{"runId",runId},{"officialSocketClose",facts}});}catch(...){}
                    return true;
                }catch(...){return false;}
            };
            const auto remember=[&](const Json& packet){
                if(observeOfficialSocket(packet))return;
                const auto method=packet.value("method",std::string{});
                if(method=="Runtime.executionContextCreated"&&!usableContext(packet.value("params",Json::object()).value("context",Json::object())))return;
                // Drop only events the existing consumer cannot act on. A
                // destruction for a queued future context remains relevant.
                if(method=="Runtime.executionContextDestroyed"){
                    const auto id=packet.value("params",Json::object()).value("executionContextId",0);
                    if(!relevantContext(id))return;
                }
                int connectionContext=0;unsigned connectionSource=0;
                if(activeContext&&method=="Runtime.bindingCalled"){
                    const auto kind=replayBinding(packet);
                    if(kind==ReplayBinding::ignore)return;
                    const auto id=packet.value("params",Json::object()).value("executionContextId",0);
                    if(!relevantContext(id))return;
                    if(kind==ReplayBinding::opening||kind==ReplayBinding::completion){
                        connectionContext=id;connectionSource=static_cast<unsigned>(kind);
                        if(id==queuedConnectionContext&&(queuedConnectionSources&connectionSource))return;
                    }
                }
                if(activeContext&&method=="Runtime.exceptionThrown"){
                    int eventContext=0;try{eventContext=packet.at("params").at("exceptionDetails").value("executionContextId",0);}catch(...){}
                    if(!relevantContext(eventContext)||ymcc::deckyhealth::exception(packet,eventContext).empty())return;
                }
                if(method=="Runtime.executionContextCreated"||method=="Runtime.executionContextDestroyed"||method=="Runtime.executionContextsCleared"||(activeContext&&(method=="Runtime.bindingCalled"||method=="Runtime.exceptionThrown"))){
                    if(events.size()>=32)throw std::runtime_error("mirror-bootstrap-context-event-limit");
                    events.push_back(packet);
                    // Only hints still waiting in the current replay tail can merge.
                    // A lifecycle or account/fatal event starts a new group.
                    if(!connectionSource||queuedConnectionContext!=connectionContext)queuedConnectionSources=0;
                    queuedConnectionContext=connectionContext;
                    queuedConnectionSources|=connectionSource;
                }
            };
            const auto command=[&](const char* method,const Json& params){
                commandFailure=nullptr;const auto budget=GetTickCount64()+2500;
                const auto end=nextId==8200&&startupDeadline?(std::min)(startupDeadline,budget):budget;
                ymcc::steamlive::listenersource::Deadline listenerDeadline(end);
                requireOwner();socket->setDeadline(end);
                const auto id=++nextId;socket->sendText(Json{{"id",id},{"method",method},{"params",params}}.dump());
                // Runtime.enable replays existing console history. It is ignored, not logged or confused with our reply.
                // Finite command deadline + packet cap bound this drain even on a noisy page.
                for(unsigned packets=0;packets<4096;++packets){
                    const auto reply=Json::parse(socket->receiveText(65536));
                    if(reply.value("id",0u)==id){
                        if(reply.contains("error")||(reply.contains("result")&&reply["result"].contains("exceptionDetails"))){
                            try{commandFailure=ymcc::steamfailure::cdp(reply);}catch(...){}
                        }
                        return reply;
                    }
                    remember(reply);
                }
                throw std::runtime_error("mirror-bootstrap-reply-limit");
            };
            try{
                const auto enabled=command("Runtime.enable",Json::object());
                if(enabled.contains("error"))throw std::runtime_error("mirror-bootstrap-watch-enable-failed");
                const auto binding=command("Runtime.addBinding",{{"name","__YMCC_DECKY_HEALTH_V1"}});
                if(binding.contains("error"))throw std::runtime_error("mirror-bootstrap-health-binding-failed");
                const auto stopForFatal=[&](const std::string& fatal,const Json* sourceFailure=nullptr) -> bool {
                    if(fatal.empty())return false;
                    requireOwner();
                    // A bundled entry also handles optional features. An uncaught exception alone
                    // is insufficient: stop only if the upstream constructor never produced a loader.
                    if(fatal=="decky-frontend-incompatible"){
                        const auto boot=command("Runtime.evaluate",{{"expression","!window.DeckyPluginLoader"},{"contextId",activeContext},{"returnByValue",true}});
                        if(boot.value("result",Json::object()).value("result",Json::object()).value("value",Json())!=true)return false;
                    }
                    // An exception from a context already destroyed during navigation is stale.
                    const auto current=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_MIRROR__?.runId === "+Json(runId).dump()+" && window.__YMCC_DECKY_MIRROR__?.revoked !== true"},{"contextId",activeContext},{"returnByValue",true}});
                    if(current.value("result",Json::object()).value("result",Json::object()).value("value",Json())!=true)return false;
                    // Upstream's Disable Decky button restarts Steam. Use lifecycle teardown only;
                    // the YMCC owner thread then runs the normal off/settings/owned-Job path.
                    // Recheck inside the mutation too: logout/new bootstrap can
                    // land after the preceding CDP read. A proven refusal never
                    // authorizes the parent's normal OFF notification.
                    // Recheck the SAME fault inside this existing mutation: a recovered Loader
                    // or boundary must survive a late verdict. No additional CDP round trip.
                    const std::string teardownExpression="(() => { const fatal="+Json(fatal).dump()+"; try { if (window.__YMCC_DECKY_MIRROR__?.runId !== "+Json(runId).dump()+" || window.__YMCC_DECKY_MIRROR__?.revoked === true || !['decky-frontend-incompatible','decky-render-failed'].includes(fatal) || (fatal === 'decky-frontend-incompatible' && window.DeckyPluginLoader) || (fatal === 'decky-render-failed' && window.__YMCC_DECKY_HEALTH__?.read() !== 'decky-render-failed')) return false; } catch { return false; } window.dispatchEvent(new Event('ymcc-decky-health-stop')); const l=window.DeckyPluginLoader; for(const method of ['dismountAll','deinit']) { try { if(typeof l?.[method]==='function')l[method](); } catch {} } return true; })()";
                    try{
                        const auto stopped=command("Runtime.evaluate",{{"expression",teardownExpression},{"contextId",activeContext},{"returnByValue",true}});
                        if(stopped.value("result",Json::object()).value("result",Json::object()).value("value",Json())==false)return false;
                    }catch(...){};
                    requireOwner();notify("terminal",fatal,sourceFailure);return true;
                };
                notify("watching");readiness->set_value({});readyReported=true;
                for(;;){
                    Json packet;
                    if(events.empty()){
                        // Passive wait wakes only on the existing CDP socket or cancellation.
                        socket->setDeadline(0);packet=Json::parse(socket->receiveText(65536));
                    }else{packet=std::move(events.front());events.pop_front();if(events.empty()){queuedConnectionContext=0;queuedConnectionSources=0;}}
                    if(observeOfficialSocket(packet))continue;
                    const auto method=packet.value("method",std::string{});
                    if(method=="Runtime.executionContextsCleared"){if(activeContext)observeContextInvalidation("execution-contexts-cleared");activeContext=0;continue;}
                    if(method=="Runtime.executionContextDestroyed"){if(packet.value("params",Json::object()).value("executionContextId",0)==activeContext){if(activeContext)observeContextInvalidation("execution-context-destroyed");activeContext=0;}continue;}
                    // Account change is a transport revocation, never a fatal
                    // frontend verdict. Re-read the exact current document guard
                    // before accepting the marker, just like the render boundary.
                    if(activeContext&&method=="Runtime.bindingCalled"){
                        const auto params=packet.value("params",Json::object());
                        if(params.value("name",std::string{})=="__YMCC_DECKY_HEALTH_V1"&&params.value("executionContextId",0)==activeContext){
                            const auto payload=params.value("payload",std::string{});
                            const auto signal=payload.size()<=256?Json::parse(payload,nullptr,false):Json();
                            if(signal.is_object()&&signal.size()==2&&signal.contains("runId")&&signal["runId"].is_string()&&signal["runId"]==runId&&signal.contains("code")&&signal["code"].is_string()&&signal["code"]=="decky-account-changed"){
                                const auto verified=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_HEALTH__?.accountCurrent() === false"},{"contextId",activeContext},{"returnByValue",true}});
                                if(verified.value("result",Json::object()).value("result",Json::object()).value("value",Json())==true)
                                    throw std::runtime_error("mirror-bootstrap-account-changed");
                            }
                        }
                    }
                    if(const auto source=ymcc::deckyhealth::connectionSignal(packet,activeContext,runId)){
                        requireOwner();notify("connection-check",source);continue;
                    }
                    std::string fatal=ymcc::deckyhealth::exception(packet,activeContext);
                    Json eventFailure;
                    // Preserve only the accepted source event's bounded facts.
                    // The existing readonly verification commands can clear their
                    // own error slot; they must not erase this distinct cause.
                    if(!fatal.empty())try{
                        eventFailure=ymcc::steamfailure::cdp({{"result",{{"exceptionDetails",packet.at("params").at("exceptionDetails")}}}});
                    }catch(...){} // A diagnostic projection never changes the terminal policy.
                    if(ymcc::deckyhealth::boundarySignal(packet,activeContext,runId)){
                        // Do not trust a marker alone. Re-read the actual visible official error boundary.
                        const auto verified=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_HEALTH__?.read() || ''"},{"contextId",activeContext},{"returnByValue",true}});
                        if(verified.value("result",Json::object()).value("result",Json::object()).value("value",std::string{})=="decky-render-failed")fatal="decky-render-failed";
                    }
                    if(stopForFatal(fatal,eventFailure.is_object()?&eventFailure:nullptr))break;
                    if(method!="Runtime.executionContextCreated")continue;
                    const auto value=packet.value("params",Json::object()).value("context",Json::object());
                    if(!usableContext(value))continue;
                    ++generation;activeContext=value["id"].get<int>();
                    // Pinned process + listener/user/session revalidated on EACH actual context event.
                    const auto reply=command("Runtime.evaluate",{{"expression",expression},{"contextId",value["id"]},{"returnByValue",true}});
                    if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")){
                        // Contexts can disappear during navigation. Wait for the next actual event, never poll/retry a mutation.
                        notify("context-replaced","mirror-bootstrap-reload-pending");continue;
                    }
                    const auto readback=reply["result"].value("result",Json::object()).value("value",std::string{});
                    if(readback.empty())continue; // Non-top-level frame: expression's origin/top guard refuses injection.
                    if(readback=="ymcc-decky-account-not-current"||readback=="ymcc-decky-account-watch-unavailable")throw std::runtime_error("mirror-bootstrap-account-changed");
                    if(readback=="ymcc-decky-retained-frontend-mismatch")throw std::runtime_error("mirror-retained-frontend-changed");
                    if(readback!=runId)throw std::runtime_error("mirror-bootstrap-readback-failed");
                    // A retained same-run health observer reports a fatal page
                    // only once. That marker can belong to the old CDP channel.
                    // Read current state once per verified attachment; reuse the
                    // same ownership/run checks and normal terminal stop below.
                    const auto health=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_HEALTH__?.read() || ''"},{"contextId",activeContext},{"returnByValue",true}});
                    if(health.contains("error")||!health.contains("result")||health["result"].contains("exceptionDetails")){
                        notify("context-replaced","mirror-bootstrap-reload-pending");continue;
                    }
                    if(health["result"].value("result",Json::object()).value("value",std::string{})=="decky-render-failed"&&stopForFatal("decky-render-failed"))break;
                    requireOwner();notify("context-attached");
                }
            }catch(const std::exception& error){
                if(!readyReported)readiness->set_value(bootstrapSafeReason(error));
                Json transport;try{transport=bootstrapTransportFailure(error);}catch(...){}
                notify(cancellation->canceled()?"stopped":"failed",cancellation->canceled()?std::string{}:bootstrapSafeReason(error),transport.is_object()?&transport:nullptr);
            }catch(...){if(!readyReported)readiness->set_value("mirror-bootstrap-watch-failed");notify("failed","mirror-bootstrap-watch-failed");}
            // Close on this owner thread, before its HTTP parent handles. No active script registration remains.
            // Close once after the last API initiation has returned. Keep this
            // exact request and its parent scope if close/final return is unknown.
            // The original owning child/Job supplies the final process boundary.
            retirement_=socket->retire(GetTickCount64());
            if(!retirement_.returned()){
                if(!retirement_.closeAccepted)retired_=std::move(socket);
                else closing_=socket->callbackState();
            }
            socket.reset();connection.reset();session.reset();
        });
        const auto failure=ready.get();if(!failure.empty())stop();return failure;
    }
};
} // namespace ymcc::deckymirror
