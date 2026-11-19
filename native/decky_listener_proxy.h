#pragma once
// Read-only facts on the original authenticated same-image channel. No endpoint
// or identity supplied by a child can select another process/root in the parent.
#include "steam_live_cdp.h"
#include "decky_binding_process_owner.h"
#include <charconv>
#include <limits>
#include <mutex>
namespace ymcc::deckybinding::listenerproxy {
using Json=nlohmann::json;
using Read=std::function<std::vector<steamlive::Endpoint>(ULONGLONG,const std::function<bool()>&)>;
inline constexpr const char* schema="ymcc.decky.listener-facts.v1";
inline void require(bool ok){if(!ok)throw std::runtime_error("live-listener-proxy-invalid");}
inline std::uint64_t decimal(const Json& value){
    require(value.is_string());const auto& s=value.get_ref<const std::string&>();std::uint64_t n=0;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    require(!s.empty()&&s.size()<=20&&s[0]!='0'&&r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&n);return n;
}
inline Json parse(const std::vector<std::uint8_t>& bytes){
    require(!bytes.empty()&&bytes.size()<=16384);std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
        require(depth<=12);
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second);
        return true;
    });
}
inline bool isQuery(const Json& value){return value.is_object()&&value.value("schema",std::string{})==schema;}
inline void envelope(const Json& v,const std::string& id,const std::string& generation,const char* kind){
    require(v.is_object()&&v.size()==5&&v.at("schema")==schema&&v.at("id")==id&&v.at("generation")==generation&&v.at("kind")==kind&&v.at("detail").is_object());
}
inline bool failure(const std::string& reason){
    return reason=="live-debug-unavailable"||reason=="live-debug-not-loopback"||reason=="live-context-ambiguous"||
        reason=="live-action-cancelled"||reason=="live-busy"||reason=="live-listener-proxy-unavailable";
}
inline bool boundarySourceFailure(const std::string& reason){
    return reason=="live-busy"||reason=="live-action-cancelled"||reason=="live-listener-proxy-unavailable";
}
inline void endpoints(const std::vector<steamlive::Endpoint>& rows){
    require(!rows.empty()&&rows.size()<=16);
    for(std::size_t i=0;i<rows.size();++i){const auto& e=rows[i];require(e.pid&&e.port&&(e.host==L"127.0.0.1"||e.host==L"::1"));
        for(std::size_t j=0;j<i;++j)require(!(rows[j]==e));}
}
inline DWORD left(ULONGLONG end,DWORD cap=2500){const auto now=GetTickCount64();return now<end?static_cast<DWORD>((std::min)(ULONGLONG{cap},end-now)):0;}
inline Json packet(const std::string& id,const std::string& generation,const char* kind,Json detail){
    return {{"schema",schema},{"id",id},{"generation",generation},{"kind",kind},{"detail",std::move(detail)}};
}
inline std::vector<std::uint8_t> wire(const Json& v){const auto s=v.dump();require(s.size()<=16384);return {s.begin(),s.end()};}
using Post=std::function<bool(std::function<void()>,std::function<void()>)>;
using Wake=std::function<void()>;
struct Query {
    std::string id,generation;
    std::uint64_t sequence=0;
    ULONGLONG end=0;
};
inline Json resolve(const Query& query,const Read& read,const std::function<bool()>& current){
    Json result={{"sequence",std::to_string(query.sequence)},{"ok",false},{"reason","live-listener-proxy-unavailable"}};
    try{
        if(!current||!current())throw std::runtime_error("live-action-cancelled");
        if(!left(query.end)||!read)return packet(query.id,query.generation,"reply",std::move(result));
        auto rows=read(query.end,current);endpoints(rows);
        if(!current())throw std::runtime_error("live-action-cancelled");
        if(!left(query.end))return packet(query.id,query.generation,"reply",std::move(result));
        Json values=Json::array();for(const auto& e:rows)values.push_back({{"pid",e.pid},{"port",e.port},{"host",e.host==L"::1"?"::1":"127.0.0.1"}});
        result={{"sequence",std::to_string(query.sequence)},{"ok",true},{"endpoints",std::move(values)}};
    }catch(const std::exception& error){const std::string reason=error.what();if(failure(reason))result["reason"]=reason;}
    catch(...){}
    return packet(query.id,query.generation,"reply",std::move(result));
}
// No Session, pipe, event or Runtime is retained by a queued acquisition.
// The main-process source executor owns a callback through its real return.
class Work {
    Query query_;Read read_;std::function<bool()> current_;
    std::mutex mutex_;Json reply_;bool failed_=false;
    std::atomic<bool> revoked_{false},prepared_{false},ready_{false};
public:
    Work(Query query,Read read,std::function<bool()> current):query_(std::move(query)),read_(std::move(read)),current_(std::move(current)){}
    void cancel()noexcept{revoked_.store(true,std::memory_order_release);}
    bool ready()const noexcept{return ready_.load(std::memory_order_acquire)&&current();}
    ULONGLONG end()const noexcept{return query_.end;}
    bool current()const noexcept {try{return !revoked_.load(std::memory_order_acquire)&&left(query_.end)&&current_&&current_();}catch(...){return false;}}
    void run()noexcept{
        if(!current())return;
        try{auto reply=resolve(query_,read_,[this]{return current();});std::lock_guard lock(mutex_);reply_=std::move(reply);}
        catch(...){std::lock_guard lock(mutex_);failed_=true;}
        prepared_.store(true,std::memory_order_release);
    }
    // Only the executor's post-slot completion may make a result consumable.
    void publish()noexcept{if(prepared_.load(std::memory_order_acquire)&&current())ready_.store(true,std::memory_order_release);}
    Json take(){std::lock_guard lock(mutex_);if(failed_)throw std::runtime_error("live-listener-proxy-unavailable");return std::move(reply_);}
    Json busy()const{return packet(query_.id,query_.generation,"reply",{{"sequence",std::to_string(query_.sequence)},{"ok",false},{"reason","live-busy"}});}
};
struct Sequence {
    std::uint64_t value=0;
    void take(std::uint64_t next){require(value!=UINT64_MAX&&next==value+1);value=next;}
};
inline constexpr const char* commandSchema="ymcc.steam.command-stage.v1";
inline Json commandPacket(const std::string& id,const std::string& generation,const char* kind,Json detail){
    return {{"schema",commandSchema},{"id",id},{"generation",generation},{"kind",kind},{"detail",std::move(detail)}};
}
inline void commandEnvelope(const Json& v,const std::string& id,const std::string& generation,const char* kind){
    require(v.is_object()&&v.size()==5&&v.at("schema")==commandSchema&&v.at("id")==id&&v.at("generation")==generation&&v.at("kind")==kind&&v.at("detail").is_object());
}
class Server {
    std::string id_,generation_;std::uint64_t last_=0;Read read_;std::shared_ptr<Sequence> sequence_;
public:
    Server(std::string id,std::string generation,Read read,std::shared_ptr<Sequence> sequence={})
        :id_(std::move(id)),generation_(std::move(generation)),read_(std::move(read)),sequence_(std::move(sequence)){}
    Query admit(const Json& query,ULONGLONG callerEnd=0){
        envelope(query,id_,generation_,"query");const auto& d=query.at("detail");require(d.size()==2);
        const auto sequence=decimal(d.at("sequence")),askedEnd=decimal(d.at("deadline"));
        if(sequence_)sequence_->take(sequence);
        else {require(sequence==last_+1&&last_!=UINT64_MAX);last_=sequence;}
        const auto now=GetTickCount64();
        return {id_,generation_,sequence,(std::min)(askedEnd,(std::min)(callerEnd?callerEnd:now+2500,now+2500))};
    }
    std::shared_ptr<Work> prepare(const Json& query,std::function<bool()> current,ULONGLONG callerEnd=0){
        return std::make_shared<Work>(admit(query,callerEnd),read_,std::move(current));
    }
    Json serve(const Json& query,const std::function<bool()>& current,ULONGLONG callerEnd=0){
        return resolve(admit(query,callerEnd),read_,current);
    }
};
// Same single pipe, same cancellation and one reader. The lock spans query and
// reply so a watcher cannot consume the preceding bootstrap check's response.
class Child {
    std::shared_ptr<ChildChannel> channel_;std::shared_ptr<std::timed_mutex> writes_;
    std::timed_mutex query_;std::string id_,generation_;std::wstring root_;DWORD pid_;
    std::uint64_t sequence_=0;bool poisoned_=false;
    unsigned commandStage_=0;bool commandInFlight_=false;steamcommand::Effect commandEffect_=steamcommand::Effect::Mutation;
    void lock(std::unique_lock<std::timed_mutex>& guard,ULONGLONG end){
        while(!guard.try_lock_for(std::chrono::milliseconds((std::min)(DWORD{25},left(end))))){
            if(channel_->cancelled()||!left(end))throw std::runtime_error("live-listener-proxy-unavailable");
        }
        if(channel_->cancelled()||!left(end))throw std::runtime_error("live-listener-proxy-unavailable");
    }
public:
    Child(std::shared_ptr<ChildChannel> channel,std::shared_ptr<std::timed_mutex> writes,std::string id,std::string generation,std::wstring root,DWORD pid)
        :channel_(std::move(channel)),writes_(std::move(writes)),id_(std::move(id)),generation_(std::move(generation)),root_(std::move(root)),pid_(pid){}
    bool command(steamcommand::Effect effect,bool completed,bool accepted,ULONGLONG operationEnd){
        const auto end=(std::min)(operationEnd,GetTickCount64()+2500);
        std::unique_lock<std::timed_mutex> query(query_,std::defer_lock);lock(query,end);
        require(!poisoned_&&sequence_!=UINT64_MAX&&commandStage_<=1&&
            (completed?commandInFlight_&&effect==commandEffect_:!commandInFlight_));
        poisoned_=true;
        const auto sequence=++sequence_;
        Json detail={{"sequence",std::to_string(sequence)},{"deadline",std::to_string(end)},
            {"stage",commandStage_},{"effect",static_cast<unsigned>(effect)},{"phase",completed?"returned":"enter"}};
        if(completed)detail["accepted"]=accepted;
        const auto request=commandPacket(id_,generation_,"query",std::move(detail));
        {std::unique_lock<std::timed_mutex> writing(*writes_,std::defer_lock);lock(writing,end);
            if(!channel_->writeFrame(wire(request),left(end)))return false;}
        const auto received=channel_->readFrame(left(end));if(!received)return false;
        const auto reply=parse(received.payload);commandEnvelope(reply,id_,generation_,"reply");const auto& d=reply.at("detail");
        require(d.size()==3&&decimal(d.at("sequence"))==sequence&&d.at("stage").is_number_unsigned()&&
            d.at("stage")==commandStage_&&d.at("ok").is_boolean());
        if(!left(end)||channel_->cancelled()||!d.at("ok").get<bool>())return false;
        poisoned_=false;
        if(completed){commandInFlight_=false;++commandStage_;}
        else {commandInFlight_=true;commandEffect_=effect;}
        return true;
    }
    std::vector<steamlive::Endpoint> read(const std::wstring& root,DWORD pid,ULONGLONG operationEnd){
        const auto end=(std::min)(operationEnd,GetTickCount64()+2500);
        std::unique_lock<std::timed_mutex> query(query_,std::defer_lock);lock(query,end);
        require(!poisoned_&&root==root_&&pid&&pid==pid_&&sequence_!=UINT64_MAX);
        // Any transport/protocol failure seals this port; no stale FIFO reply
        // can satisfy another request. A typed parent failure is consumed first.
        poisoned_=true;
        const auto sequence=++sequence_;const auto request=packet(id_,generation_,"query",{{"sequence",std::to_string(sequence)},{"deadline",std::to_string(end)}});
        {std::unique_lock<std::timed_mutex> writing(*writes_,std::defer_lock);lock(writing,end);
            if(!channel_->writeFrame(wire(request),left(end)))throw std::runtime_error("live-listener-proxy-unavailable");}
        if(!left(end))throw std::runtime_error("live-listener-proxy-unavailable");
        const auto received=channel_->readFrame(left(end));if(!received)throw std::runtime_error("live-listener-proxy-unavailable");
        const auto reply=parse(received.payload);envelope(reply,id_,generation_,"reply");const auto& d=reply.at("detail");
        require(d.size()==3&&decimal(d.at("sequence"))==sequence&&d.at("ok").is_boolean());
        if(!left(end)||channel_->cancelled())throw std::runtime_error("live-listener-proxy-unavailable");
        if(!d.at("ok").get<bool>()){const auto reason=d.at("reason").get<std::string>();require(failure(reason));poisoned_=false;throw std::runtime_error(reason);}
        const auto& values=d.at("endpoints");require(values.is_array()&&values.size()<=16);std::vector<steamlive::Endpoint> rows;
        for(const auto& v:values){require(v.is_object()&&v.size()==3&&v.at("pid").is_number_unsigned()&&v.at("port").is_number_unsigned());
            const auto p=v.at("pid").get<std::uint64_t>(),port=v.at("port").get<std::uint64_t>();const auto host=v.at("host").get<std::string>();
            require(p&&p<=MAXDWORD&&port&&port<=65535&&(host=="::1"||host=="127.0.0.1"));
            rows.push_back({static_cast<DWORD>(p),static_cast<unsigned short>(port),host=="::1"?L"::1":L"127.0.0.1"});}
        endpoints(rows);poisoned_=false;return rows;
    }
};
template<class Source>
inline std::vector<steamlive::Endpoint> readShared(Source& source,const steamsession::Session& expected,DWORD windowsSession,
    ULONGLONG end,const std::function<bool()>& current){
    const auto first=source.identitySnapshot();
    const auto matches=[&](const auto& identity){return identity&&identity->presence==1&&identity->windowsSessionKnown&&
        identity->windowsSession==windowsSession&&identity->session.pid==expected.pid&&identity->session.created==expected.created&&
        identity->session.root==expected.root&&first&&identity->processEpoch==first->processEpoch;};
    if(!current||!current()||!matches(first)||!left(end))throw std::runtime_error("live-action-cancelled");
    steamlive::SteamProcessLease held;if(!held.bind(expected))throw std::runtime_error("live-action-cancelled");
    steamlive::listenersource::Deadline acquisitionEnd(end);
    steamlive::listenersource::Current acquisitionCurrent([&]{return current&&current()&&matches(source.identitySnapshot());});
    auto rows=steamlive::listeners(expected.root,expected.pid);
    if(!held.current()||!current()||!matches(source.identitySnapshot())||!left(end))throw std::runtime_error("live-action-cancelled");
    return rows;
}
} // namespace ymcc::deckybinding::listenerproxy
