#pragma once
// Event-driven lifetime of ONE already verified SharedJSContext CDP connection.
// No timer, localhost scan, Page reload, Loader launch, persisted credential or settings write.
#include "decky_sidebar_async_http.h"
#include "steam_live_cdp.h"
#include <deque>
#include <functional>
#include <future>
#include <thread>
namespace ymcc::deckymirror {
inline std::string bootstrapSafeReason(const std::exception& error){
    const std::string value=error.what();
    if(value.size()<=96&&(value.rfind("mirror-bootstrap-",0)==0||value.rfind("live-",0)==0)&&
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
            unsigned long long generation=0;unsigned nextId=8200;bool readyReported=false;
            std::deque<Json> events;
            const auto notify=[&](const char* stage,const std::string& reason={}){
                try{if(diagnostic)diagnostic({{"bootstrapStage",stage},{"reason",reason},
                    {"contextPid",contextPid},{"contextGeneration",generation}});}catch(...){}
            };
            const auto requireOwner=[&]{if(!owned())throw std::runtime_error("mirror-bootstrap-owner-mismatch");};
            const auto remember=[&](const Json& packet){
                if(packet.value("method",std::string{})=="Runtime.executionContextCreated"){
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
                notify("watching");readiness->set_value({});readyReported=true;
                for(;;){
                    Json packet;
                    if(events.empty()){
                        // Passive wait wakes only on the existing CDP socket or cancellation.
                        socket->setDeadline(0);packet=Json::parse(socket->receiveText(65536));
                    }else{packet=std::move(events.front());events.pop_front();}
                    if(packet.value("method",std::string{})!="Runtime.executionContextCreated")continue;
                    const auto value=packet.value("params",Json::object()).value("context",Json::object());
                    const auto auxiliary=value.value("auxData",Json::object());
                    if(!value.contains("id")||!value["id"].is_number_integer()||
                       value.value("origin",std::string{})!="https://steamloopback.host"||
                       !auxiliary.value("isDefault",false))continue;
                    ++generation;
                    // Pinned process + listener/user/session revalidated on EACH actual context event.
                    const auto reply=command("Runtime.evaluate",{{"expression",expression},{"contextId",value["id"]},{"returnByValue",true}});
                    if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")){
                        // Contexts can disappear during navigation. Wait for the next actual event, never poll/retry a mutation.
                        notify("context-replaced","mirror-bootstrap-reload-pending");continue;
                    }
                    const auto readback=reply["result"].value("result",Json::object()).value("value",std::string{});
                    if(readback.empty())continue; // Non-top-level frame: expression's origin/top guard refuses injection.
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
