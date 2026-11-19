#pragma once
// Private candidate: reuse the existing same-image, one-shot Owner channel.
// Only this child performs document WinHTTP IO. Parent retains current identity
// and the existing listener source; no new EXE, resident worker or Steam action.
#include "steam_document_read.h"
#include "decky_binding_client.h"
#include "steam_document_parent.h"

namespace ymcc::steamdocuments::finite {
using Json=nlohmann::json;
using namespace ymcc::deckybinding;
inline constexpr const char* schema="ymcc.steam.documents-worker.v1";
struct Request {
    std::string id,generation;
    steamsession::Session steam;
    ULONGLONG end=0;
    StylePlan plan;
};
inline Json planJson(const StylePlan& plan){
    Json value=Json::object();
    for(auto item:{std::pair{"SP",&plan.sp},std::pair{"MainMenu",&plan.mainMenu},std::pair{"QuickAccess",&plan.quickAccess}})
        value[item.first]={{"properties",item.second->values},{"fileCounts",item.second->fileCounts}};
    return value;
}
inline Request parse(std::string_view bytes){
    worker::require(!bytes.empty()&&bytes.size()<=maxFrameBytes,"document-worker-request-invalid");
    std::vector<std::set<std::string>> keys;
    const auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& parsed){
        worker::require(depth<=16,"document-worker-request-invalid");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key)worker::require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second,"document-worker-request-invalid");
        return true;
    });
    worker::require(value.is_object()&&value.size()==6&&value.value("schema",std::string{})==schema,"document-worker-request-invalid");
    Request result;result.id=value.at("id").get<std::string>();
    worker::require(worker::hex(result.id,32),"document-worker-request-invalid");
    result.generation=value.at("generation").get<std::string>();worker::decimal(value.at("generation"));
    const auto& identity=value.at("identity");worker::require(identity.is_object()&&identity.size()==3,"document-worker-request-invalid");
    result.steam.pid=worker::number(identity.at("pid"));result.steam.created=worker::decimal(identity.at("birth"));
    result.steam.root=steamlive::normalizedPath(worker::wide(identity.at("root")));
    worker::require(std::filesystem::path(result.steam.root).is_absolute(),"document-worker-request-invalid");
    result.end=worker::decimal(value.at("deadline"));result.plan=parseStylePlan(value.at("plans"));
    const auto now=GetTickCount64();
    worker::require(result.end<=now+4000,"document-worker-request-invalid");
    return result;
}
inline Json unavailable(const char* reason){
    return {{"ok",false},{"reason",reason},{"mutated",false},{"complete",false},{"documents",Json::array()}};
}
template<class Emit>inline int run(std::string_view bytes,HANDLE cancel,Emit&& emit){
    Request request;Json result;
    try{
        request=parse(bytes);
        auto token=std::make_shared<deckymirror::CancelToken>(cancel);
        DocumentIoScope io(token);io.operationEnd=request.end;
        steamlive::listenersource::Deadline listenerEnd(request.end);
        const auto current=[&]{return !token->canceled()&&GetTickCount64()<request.end;};
        result=current()?readStyles(request.steam,request.plan,current,4000,io):unavailable("document-read-cancelled");
        // A missing final or refused close terminates THIS finite child. Do not
        // transfer a WinHTTP pointer/self lease into the long-lived parent.
        const bool okay=result.value("ok",false);
        emit(Json{{"schema",schema},{"id",request.id},{"generation",request.generation},{"detail",std::move(result)}});
        return okay?0:2;
    }catch(...){
        if(!request.id.empty())try{emit(Json{{"schema",schema},{"id",request.id},{"generation",request.generation},
            {"detail",unavailable("document-worker-failed")}});}catch(...){}
        return 2;
    }
}
inline void validateResult(const Json& value,const StylePlan& plan){
    const auto invalid="document-worker-reply-invalid";
    worker::require(value.is_object()&&value.contains("ok")&&value["ok"].is_boolean()&&
        value.contains("mutated")&&value["mutated"]==false&&value.contains("complete")&&value["complete"].is_boolean()&&
        value.contains("documents")&&value["documents"].is_array()&&value["documents"].size()<=12,invalid);
    const auto& documents=value["documents"];
    if(!value["ok"].get<bool>()){
        worker::require(!value["complete"].get<bool>()&&documents.empty()&&value.contains("reason")&&value["reason"].is_string()&&
            !value["reason"].get_ref<const std::string&>().empty()&&value["reason"].get_ref<const std::string&>().size()<=96,invalid);
        return;
    }
    worker::require(value.contains("protocol")&&value["protocol"].is_number_integer()&&value["protocol"]==1&&
        value.contains("sequential")&&value["sequential"].is_boolean()&&value["sequential"].get<bool>()&&!documents.empty(),invalid);
    std::set<std::string> ids;std::set<Kind> present;unsigned main=0,auxiliary=0;
    for(const auto& row:documents){
        worker::require(row.is_object()&&row.size()==4&&row.contains("kind")&&row["kind"].is_string()&&row.contains("id")&&row["id"].is_string()&&
            row.contains("sourceUrl")&&row["sourceUrl"].is_string()&&row.contains("value")&&row["value"].is_object(),invalid);
        const auto& label=row["kind"].get_ref<const std::string&>();
        worker::require(label=="SP"||label=="MainMenu"||label=="QuickAccess",invalid);
        const auto kind=label=="SP"?Kind::BigPicture:label=="MainMenu"?Kind::MainMenu:Kind::QuickAccess;
        const auto& id=row["id"].get_ref<const std::string&>();const auto& url=row["sourceUrl"].get_ref<const std::string&>();
        worker::require(!id.empty()&&id.size()<=128&&id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_")==std::string::npos&&
            ids.insert(id).second&&url.size()<=4096&&(steamDocumentUrl(url)||(kind!=Kind::BigPicture&&blankPopupDocumentUrl(url))),invalid);
        if(kind==Kind::BigPicture)++main;else ++auxiliary;
        worker::require(main<=4&&auxiliary<=8,invalid);present.insert(kind);
        try{validateReceipt(row["value"],fields(plan,kind),Target{kind,id,"/devtools/page/"+id,url});}
        catch(const std::bad_alloc&){throw;}
        catch(...){throw std::runtime_error(invalid);}
    }
    worker::require(main>0&&value["complete"].get<bool>()==(present.size()==3),invalid);
}
struct Observation {
    Snapshot owner;
    clientdetail::EventReader::Status reader;
    bool started=false,writeAttempted=false,replySeen=false,destroyed=false;
    unsigned listenerQueries=0;
    DWORD sendError=0,readError=0,waitError=0;
    DWORD readyHandles=0,readyError=0;
    ULONGLONG businessMs=0,cleanupMs=0;
    ULONGLONG transportHeldTick=0;
};
inline void appendTransportObservation(Json& detail,const Observation& observation,bool confirmed){
    if(confirmed&&observation.transportHeldTick)detail["readTransportHeldTick"]=observation.transportHeldTick;
}
struct ListenerSource {
    // Both callables must own all captured identity/power values; they must not
    // borrow this request stack. Existing Native ListenerExecutor owns Work.
    listenerproxy::Read read;
    listenerproxy::Post post;
};
inline Json read(const steamsession::Session& supplied,const StylePlan& plan,
    const std::function<bool()>& ownerCurrent,const ListenerSource& source,
    DWORD budgetMs=4000,Observation* observation=nullptr){
    if(observation)observation->transportHeldTick=0;
    const auto began=GetTickCount64();const auto session=supplied;
    Json result=unavailable("document-worker-unavailable");
    if(!ownerCurrent||!ownerCurrent())return unavailable("document-read-cancelled");
    validate(plan.sp);validate(plan.mainMenu);validate(plan.quickAccess);
    std::unique_lock serialized(steamlive::transportMutex,std::try_to_lock);
    if(!serialized.owns_lock())return unavailable("live-busy");
    const steamlive::TransportHoldObservation holder(serialized,steamlive::TransportHolder::DocumentWorker);
    auto& lifetime=documentParent();
    auto ownership=lifetime.acquire();
    if(!ownership)return unavailable("document-worker-cleanup-incomplete");
    steamlive::SteamProcessLease process;
    if(!process.bind(session)||!ownerCurrent())return unavailable("document-read-cancelled");
    const auto current=[&]()noexcept{try{return lifetime.current()&&process.current()&&ownerCurrent();}catch(...){return false;}};
    const auto end=GetTickCount64()+(std::max<DWORD>)(1,(std::min<DWORD>)(4000,budgetMs));
    auto ready=ownership.ready();
    auto& owner=ownership.owner();auto& reader=ownership.reader();Observation state;
    HANDLE child=nullptr;bool cleanupDone=false;
    auto revoked=std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<listenerproxy::Work> work;
    const auto cleanup=[&]()noexcept{
        if(cleanupDone)return;cleanupDone=true;const auto at=GetTickCount64();
        revoked->store(true,std::memory_order_release);if(work)work->cancel();
        child=nullptr;owner.requestCancel();reader.requestCancel();
        const auto stopped=owner.stop();const auto readerStopped=reader.stop(200);
        const bool readyClosed=ownership.closeReady();
        state.readyHandles=ownership.readyHandles();state.readyError=ownership.readyError();
        state.owner=owner.snapshot();state.reader=reader.snapshot();
        state.destroyed=stopped.fullyDestroyed&&readerStopped&&state.owner.handles==0&&state.owner.pendingIo==0&&
            state.owner.jobActiveProcesses==0&&state.reader.fullyDestroyed&&readyClosed&&state.readyHandles==0;
        ownership.completed(state.destroyed);
        state.cleanupMs=GetTickCount64()-at;
    };
    struct Finish {decltype(cleanup)& action;~Finish(){action();}} finish{cleanup};
    try{
        const auto started=ownership.start();state.started=bool(started);
        if(!started)throw std::runtime_error("document-worker-start-failed");
        char id[33]{};snprintf(id,sizeof(id),"%016llx%016llx",static_cast<unsigned long long>(GetCurrentProcessId()),
            static_cast<unsigned long long>(started.generation));
        const auto generation=std::to_string(started.generation);
        const Json request={{"schema",schema},{"id",id},{"generation",generation},{"deadline",std::to_string(end)},
            {"identity",{{"pid",session.pid},{"birth",std::to_string(session.created)},
             {"root",std::string{}}}},{"plans",planJson(plan)}};
        // UTF-8 root conversion uses the same strict Windows conversion as the
        // existing shared worker; ASCII fixtures do not authorize other paths.
        auto wireRequest=request;
        const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,session.root.data(),static_cast<int>(session.root.size()),nullptr,0,nullptr,nullptr);
        if(count<=0)throw std::runtime_error("document-worker-request-invalid");
        std::string root(static_cast<std::size_t>(count),'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,session.root.data(),static_cast<int>(session.root.size()),root.data(),count,nullptr,nullptr)!=count)
            throw std::runtime_error("document-worker-request-invalid");
        wireRequest["identity"]["root"]=std::move(root);
        const auto text=wireRequest.dump();const std::vector<std::uint8_t> payload(text.begin(),text.end());
        if(!current()||!listenerproxy::left(end))throw std::runtime_error("document-read-cancelled");
        state.writeAttempted=true;const auto sent=owner.writeFrame(payload,listenerproxy::left(end,150),started.generation);
        if(!sent){state.sendError=sent.error;throw std::runtime_error("document-worker-send-failed");}
        child=owner.borrowChildProcessForObservation(&state.readError);
        if(!child)throw std::runtime_error("document-worker-read-failed");
        const auto pipe=owner.copyReadPipe(&state.readError);
        if(!pipe||!reader.attach(pipe,&state.readError))throw std::runtime_error("document-worker-read-failed");
        listenerproxy::Server server(id,generation,source.read);bool childExited=false;
        for(;;){
            if(!current()){result=unavailable("document-read-cancelled");break;}
            const auto now=GetTickCount64();if(now>=end){result=unavailable("document-worker-deadline");break;}
            if(work){
                if(!listenerproxy::left(work->end()))throw std::runtime_error("document-listener-unavailable");
                if(work->ready()){
                    const auto response=work->take();work->cancel();work.reset();
                    if(!owner.writeFrame(listenerproxy::wire(response),listenerproxy::left(end,150),started.generation))
                        throw std::runtime_error("document-listener-unavailable");
                }
            }
            std::vector<std::uint8_t> bytes;DWORD error=0;const auto received=reader.advance(bytes,error);
            if(received==clientdetail::EventReader::Read::frame){
                std::vector<std::set<std::string>> keys;
                const auto reply=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
                    worker::require(depth<=32,"document-worker-reply-invalid");
                    if(event==Json::parse_event_t::object_start)keys.emplace_back();
                    else if(event==Json::parse_event_t::object_end)keys.pop_back();
                    else if(event==Json::parse_event_t::key)worker::require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"document-worker-reply-invalid");
                    return true;
                });
                if(listenerproxy::isQuery(reply)){
                    if(work)throw std::runtime_error("document-worker-reply-invalid");
                    work=server.prepare(reply,[revoked]{return !revoked->load(std::memory_order_acquire);},end);++state.listenerQueries;
                    auto held=work;
                    const std::weak_ptr<ListenerReady> readyWeak=ready;
                    if(!source.post||!source.post([held]{held->run();},[held,readyWeak]{
                        held->publish();if(const auto wake=readyWeak.lock())(void)wake->signal();
                    })){
                        const auto busy=work->busy();work->cancel();work.reset();
                        if(!owner.writeFrame(listenerproxy::wire(busy),listenerproxy::left(end,150),started.generation))
                            throw std::runtime_error("document-listener-unavailable");
                    }
                    continue;
                }
                worker::require(reply.is_object()&&reply.size()==4&&reply.value("schema",std::string{})==schema&&
                    reply.at("id")==id&&reply.at("generation")==generation&&reply.at("detail").is_object()&&!work,
                    "document-worker-reply-invalid");
                auto value=reply.at("detail");
                validateResult(value,plan);
                result=!current()?unavailable("document-read-cancelled"):
                    GetTickCount64()>=end?unavailable("document-worker-deadline"):std::move(value);
                state.replySeen=true;break;
            }
            if(received!=clientdetail::EventReader::Read::pending){state.readError=error;result=unavailable("document-worker-read-failed");break;}
            HANDLE handles[]{reader.event(),ready->handle(),child};
            const auto wait=WaitForMultipleObjects(childExited?2u:3u,handles,FALSE,static_cast<DWORD>((std::min)(50ull,end-now)));
            if(wait==WAIT_OBJECT_0+1)ResetEvent(ready->handle());
            if(wait==WAIT_OBJECT_0+2)childExited=true;
            if(wait==WAIT_FAILED){state.waitError=GetLastError();result=unavailable("document-worker-wait-failed");break;}
        }
    }catch(const std::exception& error){
        const std::string reason=error.what();result=unavailable(reason.starts_with("document-")?reason.c_str():"document-worker-failed");
    }catch(...){result=unavailable("document-worker-failed");}
    state.businessMs=GetTickCount64()-began;cleanup();
    if(!state.destroyed)result=unavailable("document-worker-cleanup-incomplete");
    if(observation){
        state.transportHeldTick=holder.heldTick();
        *observation=state;
    }
    return result;
}
} // namespace ymcc::steamdocuments::finite
