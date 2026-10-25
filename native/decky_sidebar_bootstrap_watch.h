#pragma once
// Event-driven lifetime of ONE already verified SharedJSContext CDP connection.
// Watch worker has no timer, localhost scan, Page reload, Loader launch or settings write.
// The injected version-update script owns only bounded failed-update/receipt timers.
#include "decky_sidebar_async_http.h"
#include "steam_live_cdp.h"
#include "decky_sidebar_health.h"
#include <deque>
#include <functional>
#include <future>
#include <thread>
namespace ymcc::deckymirror {
inline std::string bootstrapSafeReason(const std::exception& error){
    const std::string value=error.what();
    if(value.size()<=96&&(value=="decky-context-already-loaded"||value=="mirror-retained-frontend-changed"||value.rfind("mirror-bootstrap-",0)==0||value.rfind("live-",0)==0)&&
       value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos)return value;
    return "mirror-bootstrap-invalid-message";
}
class BootstrapWatch {
    std::thread worker_;
    std::shared_ptr<CancelToken> cancellation_;
public:
    using Diagnostic=std::function<void(const nlohmann::json&)>;
    BootstrapWatch()=default;
    BootstrapWatch(const BootstrapWatch&)=delete;
    ~BootstrapWatch(){stop();}
    void stop() noexcept {
        if(cancellation_)cancellation_->cancel();
        if(worker_.joinable())worker_.join();
        cancellation_.reset();
    }
    std::string start(std::shared_ptr<ymcc::steamlive::HttpHandle> session,
        std::shared_ptr<ymcc::steamlive::HttpHandle> connection,
        std::shared_ptr<AsyncRequest> socket,std::shared_ptr<CancelToken> cancellation,
        std::function<bool()> owned,std::string expression,std::string runId,DWORD contextPid,
        Diagnostic diagnostic={},ULONGLONG startupDeadline=0) {
        stop();cancellation_=cancellation;
        auto readiness=std::make_shared<std::promise<std::string>>();auto ready=readiness->get_future();
        worker_=std::thread([session=std::move(session),connection=std::move(connection),socket=std::move(socket),
            cancellation=std::move(cancellation),owned=std::move(owned),expression=std::move(expression),
            runId=std::move(runId),contextPid,diagnostic=std::move(diagnostic),readiness,startupDeadline]() mutable {
            using Json=nlohmann::json;
            unsigned long long generation=0;unsigned nextId=8200;bool readyReported=false;int activeContext=0;
            std::deque<Json> events;
            const auto notify=[&](const char* stage,const std::string& reason={}){
                try{if(diagnostic)diagnostic({{"bootstrapStage",stage},{"reason",reason},
                    {"contextPid",contextPid},{"contextGeneration",generation},{"runId",runId}});}catch(...){}
            };
            const auto requireOwner=[&]{if(!owned())throw std::runtime_error("mirror-bootstrap-owner-mismatch");};
            const auto remember=[&](const Json& packet){
                const auto method=packet.value("method",std::string{});
                if(method=="Runtime.executionContextCreated"||method=="Runtime.executionContextDestroyed"||method=="Runtime.executionContextsCleared"||(activeContext&&(method=="Runtime.bindingCalled"||method=="Runtime.exceptionThrown"))){
                    if(events.size()>=32)throw std::runtime_error("mirror-bootstrap-context-event-limit");
                    events.push_back(packet);
                }
            };
            const auto command=[&](const char* method,const Json& params){
                requireOwner();const auto budget=GetTickCount64()+2500;
                socket->setDeadline(nextId==8200&&startupDeadline?(std::min)(startupDeadline,budget):budget);
                const auto id=++nextId;socket->sendText(Json{{"id",id},{"method",method},{"params",params}}.dump());
                // Runtime.enable replays existing console history. It is ignored, not logged or confused with our reply.
                // Finite command deadline + packet cap bound this drain even on a noisy page.
                for(unsigned packets=0;packets<4096;++packets){
                    const auto reply=Json::parse(socket->receiveText(65536));
                    if(reply.value("id",0u)==id)return reply;
                    remember(reply);
                }
                throw std::runtime_error("mirror-bootstrap-reply-limit");
            };
            try{
                const auto enabled=command("Runtime.enable",Json::object());
                if(enabled.contains("error"))throw std::runtime_error("mirror-bootstrap-watch-enable-failed");
                const auto binding=command("Runtime.addBinding",{{"name","__YMCC_DECKY_HEALTH_V1"}});
                if(binding.contains("error"))throw std::runtime_error("mirror-bootstrap-health-binding-failed");
                notify("watching");readiness->set_value({});readyReported=true;
                for(;;){
                    Json packet;
                    if(events.empty()){
                        // Passive wait wakes only on the existing CDP socket or cancellation.
                        socket->setDeadline(0);packet=Json::parse(socket->receiveText(65536));
                    }else{packet=std::move(events.front());events.pop_front();}
                    const auto method=packet.value("method",std::string{});
                    if(method=="Runtime.executionContextsCleared"){activeContext=0;continue;}
                    if(method=="Runtime.executionContextDestroyed"){if(packet.value("params",Json::object()).value("executionContextId",0)==activeContext)activeContext=0;continue;}
                    std::string fatal=ymcc::deckyhealth::exception(packet,activeContext);
                    if(ymcc::deckyhealth::boundarySignal(packet,activeContext,runId)){
                        // Do not trust a marker alone. Re-read the actual visible official error boundary.
                        const auto verified=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_HEALTH__?.read() || ''"},{"contextId",activeContext},{"returnByValue",true}});
                        if(verified.value("result",Json::object()).value("result",Json::object()).value("value",std::string{})=="decky-render-failed")fatal="decky-render-failed";
                    }
                    if(!fatal.empty()){
                        requireOwner();
                        // A bundled entry also handles optional features. An uncaught exception alone
                        // is insufficient: stop only if the upstream constructor never produced a loader.
                        if(fatal=="decky-frontend-incompatible"){
                            const auto boot=command("Runtime.evaluate",{{"expression","!window.DeckyPluginLoader"},{"contextId",activeContext},{"returnByValue",true}});
                            if(boot.value("result",Json::object()).value("result",Json::object()).value("value",Json())!=true)continue;
                        }
                        // An exception from a context already destroyed during navigation is stale.
                        const auto current=command("Runtime.evaluate",{{"expression","window.__YMCC_DECKY_MIRROR__?.runId === "+Json(runId).dump()},{"contextId",activeContext},{"returnByValue",true}});
                        if(current.value("result",Json::object()).value("result",Json::object()).value("value",Json())!=true)continue;
                        // Upstream's Disable Decky button restarts Steam. Use lifecycle teardown only;
                        // the YMCC owner thread then runs the normal off/settings/owned-Job path.
                        try{command("Runtime.evaluate",{{"expression","(() => { window.dispatchEvent(new Event('ymcc-decky-health-stop')); const l=window.DeckyPluginLoader; for(const method of ['dismountAll','deinit']) { try { if(typeof l?.[method]==='function')l[method](); } catch {} } return true; })()"},{"contextId",activeContext},{"returnByValue",true}});}catch(...){}
                        requireOwner();notify("terminal",fatal);break;
                    }
                    if(method!="Runtime.executionContextCreated")continue;
                    const auto value=packet.value("params",Json::object()).value("context",Json::object());
                    const auto auxiliary=value.value("auxData",Json::object());
                    if(!value.contains("id")||!value["id"].is_number_integer()||
                       value.value("origin",std::string{})!="https://steamloopback.host"||
                       !auxiliary.value("isDefault",false))continue;
                    ++generation;activeContext=value["id"].get<int>();
                    // Pinned process + listener/user/session revalidated on EACH actual context event.
                    const auto reply=command("Runtime.evaluate",{{"expression",expression},{"contextId",value["id"]},{"returnByValue",true}});
                    if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")){
                        // Contexts can disappear during navigation. Wait for the next actual event, never poll/retry a mutation.
                        notify("context-replaced","mirror-bootstrap-reload-pending");continue;
                    }
                    const auto readback=reply["result"].value("result",Json::object()).value("value",std::string{});
                    if(readback.empty())continue; // Non-top-level frame: expression's origin/top guard refuses injection.
                    if(readback=="ymcc-decky-retained-frontend-mismatch")throw std::runtime_error("mirror-retained-frontend-changed");
                    if(readback!=runId)throw std::runtime_error("mirror-bootstrap-readback-failed");
                    requireOwner();notify("context-attached");
                }
            }catch(const std::exception& error){
                if(!readyReported)readiness->set_value(bootstrapSafeReason(error));
                notify(cancellation->canceled()?"stopped":"failed",cancellation->canceled()?std::string{}:bootstrapSafeReason(error));
            }catch(...){if(!readyReported)readiness->set_value("mirror-bootstrap-watch-failed");notify("failed","mirror-bootstrap-watch-failed");}
            // Close on this owner thread, before its HTTP parent handles. No active script registration remains.
            socket.reset();connection.reset();session.reset();
        });
        const auto failure=ready.get();if(!failure.empty())stop();return failure;
    }
};
} // namespace ymcc::deckymirror
