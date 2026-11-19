#pragma once
// Shared, typed document observations. Discovery, held Steam identity, transport
// serialization and bounded sockets belong to the existing Steam layer.
#include "steam_live_cdp.h"
#include "decky_sidebar_async_http.h"
#include <memory>

namespace ymcc::steamdocuments {
using Json=nlohmann::json;
enum class Kind { BigPicture, MainMenu, QuickAccess };
inline const char* name(Kind kind) {
    switch(kind){case Kind::BigPicture:return "SP";case Kind::MainMenu:return "MainMenu";case Kind::QuickAccess:return "QuickAccess";}
    throw std::runtime_error("document-kind-invalid");
}
struct Properties {std::vector<std::string> values,fileCounts;};
struct StylePlan {Properties sp,mainMenu,quickAccess;};
inline bool property(const std::string& value) {
    return value.size()>2&&value.size()<=96&&value.starts_with("--")&&
        value.find_first_not_of("-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==std::string::npos;
}
inline void validate(const Properties& fields) {
    if(fields.values.empty()||fields.values.size()>256||fields.fileCounts.size()>128)throw std::runtime_error("document-plan-invalid");
    std::set<std::string> keys,counts;
    for(const auto& key:fields.values)if(!property(key)||!keys.insert(key).second)throw std::runtime_error("document-plan-invalid");
    for(const auto& key:fields.fileCounts)if(!keys.contains(key)||!counts.insert(key).second)throw std::runtime_error("document-plan-invalid");
}
inline StylePlan parseStylePlan(const Json& value) {
    if(!value.is_object()||value.size()!=3)throw std::runtime_error("document-plan-invalid");
    StylePlan result;
    for(auto pair:{std::pair{"SP",&result.sp},std::pair{"MainMenu",&result.mainMenu},std::pair{"QuickAccess",&result.quickAccess}}){
        const auto item=value.find(pair.first);
        if(item==value.end()||!item->is_object()||item->size()!=2||!item->contains("properties")||!item->contains("fileCounts")||
           !(*item)["properties"].is_array()||!(*item)["fileCounts"].is_array())throw std::runtime_error("document-plan-invalid");
        pair.second->values=(*item)["properties"].get<std::vector<std::string>>();
        pair.second->fileCounts=(*item)["fileCounts"].get<std::vector<std::string>>();validate(*pair.second);
    }
    return result;
}
inline const Properties& fields(const StylePlan& plan,Kind kind){
    return kind==Kind::BigPicture?plan.sp:kind==Kind::MainMenu?plan.mainMenu:plan.quickAccess;
}
inline bool suffixedTitle(const std::string& title,const std::string& base){
    if(title==base)return true;
    if(!title.starts_with(base+"_uid"))return false;
    const auto suffix=title.substr(base.size()+4);
    return !suffix.empty()&&suffix.size()<=10&&suffix.find_first_not_of("0123456789")==std::string::npos;
}
struct Target {Kind kind;std::string id,path,url;bool operator==(const Target&)const=default;};
inline bool steamDocumentUrl(const std::string& url) {
    constexpr std::string_view base="https://steamloopback.host/index.html";
    if(url==base||(url.starts_with(base)&&url.size()>base.size()&&(url[base.size()]=='?'||url[base.size()]=='#')))return true;
    // Steam's SPA keeps the same trusted document after navigating from the
    // entry page to /routes/library/home (2026-11-14 field logs). Target identity,
    // held listener, origin and exact before/after href checks remain required.
    constexpr std::string_view routes="https://steamloopback.host/routes/";
    return url.starts_with(routes);
}
// Steam creates inherited-origin menu popups with this fixed blank URL.
// This only selects a candidate; receipt origin/href/document and final target
// identity checks below still establish the actual document association.
inline bool blankPopupDocumentUrl(const std::string& url) {
    if (url == "about:blank") return true;
    constexpr std::string_view prefix="about:blank?browserviewpopup=1&requestid=";
    constexpr std::string_view middle="&parentpopup=";
    if (!url.starts_with(prefix)) return false;
    const auto split=url.find(middle,prefix.size());
    if (split==std::string::npos) return false;
    const auto numeric=[](std::string_view value) {
        return !value.empty() && value.size()<=20 && value.find_first_not_of("0123456789")==std::string_view::npos;
    };
    return numeric(std::string_view(url).substr(prefix.size(),split-prefix.size())) &&
        numeric(std::string_view(url).substr(split+middle.size()));
}
// Steam can keep this fixed creation locator in /json/list after the SP
// document has navigated. It selects a candidate, never a document source:
// receipts still prove an actual Steam href/origin and stable document owner.
inline bool blankBigPictureDocumentUrl(const std::string& url) {
    constexpr std::string_view prefix="about:blank?createflags=";
    std::string_view tail=url;
    if(!tail.starts_with(prefix))return false;
    tail.remove_prefix(prefix.size());
    const auto takeNumber=[&](std::string_view separator){
        const auto end=tail.find(separator);
        if(end==std::string_view::npos||end==0||end>10||tail.substr(0,end).find_first_not_of("0123456789")!=std::string_view::npos)return false;
        tail.remove_prefix(end+separator.size());return true;
    };
    return takeNumber("&minwidth=")&&takeNumber("&minheight=")&&takeNumber("&pid=")&&
        tail=="0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad";
}
// Request-local failure facts only; never a source or action credential.
struct TargetSourceFailure {bool captured=false;Kind kind=Kind::BigPicture;const char* urlClass="other";std::size_t length=0;bool query=false,fragment=false;};
inline std::vector<Target> targets(const Json& list,const steamlive::Endpoint& endpoint,TargetSourceFailure* rejected=nullptr){
    if(!list.is_array()||list.size()>64)throw std::runtime_error("document-targets-invalid");
    const std::string prefix=std::string("ws://")+(endpoint.host==L"::1"?"[::1]":"127.0.0.1")+":"+std::to_string(endpoint.port)+"/devtools/page/";
    std::vector<Target> result;std::set<std::string> ids;unsigned main=0,auxiliary=0;
    for(const auto& tab:list){
        if(!tab.is_object())throw std::runtime_error("document-targets-invalid");
        if(tab.value("type",std::string{})!="page")continue;
        const auto title=tab.value("title",std::string{}),url=tab.value("url",std::string{});
        std::optional<Kind> kind;
        if(suffixedTitle(title,"MainMenu"))kind=Kind::MainMenu;
        else if(suffixedTitle(title,"QuickAccess"))kind=Kind::QuickAccess;
        else {auto lower=title;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(url.find("browserType=3")!=std::string::npos||lower.find("big picture")!=std::string::npos||lower.find("bigpicture")!=std::string::npos||lower.find("gamepadui")!=std::string::npos||title.find("大屏")!=std::string::npos)kind=Kind::BigPicture;}
        if(!kind)continue;
        if(!steamDocumentUrl(url)&&!(*kind==Kind::BigPicture?blankBigPictureDocumentUrl(url):blankPopupDocumentUrl(url))){
            if(rejected){
                rejected->captured=true;rejected->kind=*kind;rejected->length=(std::min<std::size_t>)(url.size(),4097);
                rejected->urlClass=url.empty()?"empty":url=="about:blank"?"blank-exact":blankPopupDocumentUrl(url)?"blank-popup-exact":
                    url.starts_with("about:blank")?"blank-variant":url.starts_with("https://steamloopback.host")?"steam-variant":"other";
                const auto bounded=std::string_view(url).substr(0,4096);rejected->query=bounded.find('?')!=bounded.npos;rejected->fragment=bounded.find('#')!=bounded.npos;
            }
            throw std::runtime_error("document-source-unconfirmed");
        }
        const auto id=tab.value("id",std::string{}),ws=tab.value("webSocketDebuggerUrl",std::string{});
        if(id.empty()||id.size()>128||id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_")!=std::string::npos||
           !ids.insert(id).second||ws!=prefix+id||url.size()>4096)throw std::runtime_error("document-targets-invalid");
        if(*kind==Kind::BigPicture)++main;else ++auxiliary;
        if(main>4||auxiliary>8)throw std::runtime_error("document-target-limit");
        result.push_back({*kind,id,"/devtools/page/"+id,url});
    }
    std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.id<b.id;});
    return result;
}
inline bool hasBigPictureTarget(const std::vector<Target>& targets){
    return std::any_of(targets.begin(),targets.end(),[](const Target& target){return target.kind==Kind::BigPicture;});
}
template<class Owned>
inline const char* finalTargetSetFailure(const std::vector<Target>& initial,const std::vector<Target>& final,Owned&& owned,ULONGLONG deadline){
    if(initial==final)return owned()&&GetTickCount64()<deadline?nullptr:"document-read-cancelled";
    if(!hasBigPictureTarget(final))return "document-targets-changed";
    // Only positively verified ownership can permit a new read after topology changes.
    try{if(owned()&&GetTickCount64()<deadline)return "document-targets-refreshed";}catch(...){}
    return "document-read-cancelled";
}
template<class Checkpoint,class ReadList,class Rejected>
inline auto selectDocumentTargetEndpoint(const std::vector<steamlive::Endpoint>& candidates,TargetSourceFailure& source,
    Checkpoint&& checkpoint,ReadList&& readList,Rejected&& rejected){
    return steamdiscovery::select<std::vector<Target>>(steamdiscovery::Kind::Documents,candidates,
        std::forward<Checkpoint>(checkpoint),[](const auto&){return true;},
        [&](const auto& candidate)->std::optional<std::vector<Target>>{
            auto found=targets(readList(candidate),candidate,&source);
            // Popups alone cannot satisfy a style read or compete with a main document.
            if(!hasBigPictureTarget(found))return {};
            return found;
        },std::forward<Rejected>(rejected));
}
inline std::string expression(const Properties& plan){
    validate(plan);
    // Only JSON arrays of validated custom-property names enter the fixed reader.
    return "(()=>{const e=document.documentElement;if(!e)throw new Error('root missing');if(globalThis.origin!=='https://steamloopback.host')throw new Error('source unknown');const s=getComputedStyle(e),nodes=document.querySelectorAll('style.css-loader-style');if(nodes.length>16384)throw new Error('style count');const keys="+
        Json(plan.fileCounts).dump()+",counts=new Map(keys.map(k=>[k,0]));let size=0,tokens=0;for(const n of nodes){const t=n.textContent||'';size+=t.length;if(t.length>262144||size>4194304)throw new Error('style size');const re=/--[A-Za-z0-9_-]+\\s*:/g;for(const match of t.matchAll(re)){if(++tokens>131072)throw new Error('style tokens');const k=match[0].replace(/\\s*:$/,'');if(counts.has(k))counts.set(k,counts.get(k)+1);}}return {documentKey:String(performance.timeOrigin),sourceOrigin:globalThis.origin,sourceHref:location.href,protocol:4,ready:document.readyState!=='loading',styles:nodes.length,values:"+
        Json(plan.values).dump()+".map(k=>s.getPropertyValue(k).trim()),fileCounts:keys.map(k=>counts.get(k))};})()";
}
inline bool receiptSourceMatches(const Target& target,const std::string& href){
    if(href.empty()||href.size()>4096)return false;
    if(target.kind==Kind::BigPicture&&blankBigPictureDocumentUrl(target.url))return steamDocumentUrl(href);
    if(target.kind!=Kind::BigPicture&&blankPopupDocumentUrl(target.url))return href==target.url||steamDocumentUrl(href);
    return href==target.url;
}
inline void validateReceipt(const Json& value,const Properties& plan,const Target& target){
    if(!value.is_object()||!value.contains("protocol")||!value["protocol"].is_number_integer()||value["protocol"]!=4||
        !value.contains("ready")||!value["ready"].is_boolean()||!value["ready"].get<bool>()||
        !value.contains("styles")||!value["styles"].is_number_integer()||value["styles"]<0||value["styles"]>16384||
        !value.contains("values")||!value["values"].is_array()||value["values"].size()!=plan.values.size()||
        !value.contains("fileCounts")||!value["fileCounts"].is_array()||value["fileCounts"].size()!=plan.fileCounts.size()||
        value.value("sourceOrigin",std::string{})!="https://steamloopback.host"||!receiptSourceMatches(target,value.value("sourceHref",std::string{})))
        throw std::runtime_error("document-receipt-invalid");
    const auto documentKey=value.value("documentKey",std::string{});
    if(documentKey.empty()||documentKey.size()>96)throw std::runtime_error("document-identity-unconfirmed");
    for(const auto& item:value["values"])if(!item.is_string()||item.get_ref<const std::string&>().size()>96)throw std::runtime_error("document-receipt-invalid");
    for(const auto& count:value["fileCounts"])if(!count.is_number_integer()||count<0||count>16384)throw std::runtime_error("document-receipt-invalid");
}
inline bool replyIdMatches(const Json& packet,unsigned id)noexcept{
    if(!packet.is_object())return false;
    const auto found=packet.find("id");
    return found!=packet.end()&&found->is_number_unsigned()&&found->get<Json::number_unsigned_t>()==id;
}
inline Json evaluate(HINTERNET socket,const std::string& code,unsigned id,ULONGLONG deadline,const std::function<bool()>& current){
    if(!current()||GetTickCount64()>=deadline)throw std::runtime_error("document-read-cancelled");
    const auto message=Json{{"id",id},{"method","Runtime.evaluate"},{"params",{{"expression",code},{"returnByValue",true}}}}.dump();
    if(WinHttpWebSocketSend(socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,const_cast<char*>(message.data()),static_cast<DWORD>(message.size()))!=NO_ERROR)throw std::runtime_error("document-transport-unavailable");
    std::string reply;
    for(unsigned fragments=0,messages=0;fragments<256;){
        if(!current()||GetTickCount64()>=deadline)throw std::runtime_error("document-read-cancelled");
        char bytes[16384];DWORD count=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
        if(WinHttpWebSocketReceive(socket,bytes,sizeof(bytes),&count,&kind)!=NO_ERROR||
           (kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE&&kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE))throw std::runtime_error("document-transport-unavailable");
        ++fragments;reply.append(bytes,count);if(reply.size()>65536)throw std::runtime_error("document-reply-invalid");
        if(kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)continue;
        const auto packet=Json::parse(reply);reply.clear();if(++messages>16)throw std::runtime_error("document-reply-invalid");
        if(!replyIdMatches(packet,id))continue;
        if(packet.contains("error")||!packet.contains("result")||packet["result"].contains("exceptionDetails"))throw std::runtime_error("document-evaluation-unconfirmed");
        const auto& remote=packet["result"]["result"];
        if(remote.value("type",std::string{})!="object"||!remote.contains("value")||!remote["value"].is_object())throw std::runtime_error("document-reply-invalid");
        if(!current())throw std::runtime_error("document-read-cancelled");return remote["value"];
    }
    throw std::runtime_error("document-reply-invalid");
}
inline Json readStyles(const steamsession::Session& supplied,const StylePlan& plan,const std::function<bool()>& ownerCurrent,DWORD budgetMs=4000){
    const auto session=supplied;
    TargetSourceFailure targetSource;const char* targetPhase="discovery";
    const auto failure=[&](const char* reason){
        Json result={{"ok",false},{"reason",reason},{"mutated",false},{"documents",Json::array()},{"complete",false}};
        if(targetSource.captured&&std::string_view(reason)=="document-source-unconfirmed")try{
            Json detail={{"schema",1},{"phase",targetPhase},{"kind",name(targetSource.kind)},{"urlClass",targetSource.urlClass},
                {"urlLength",targetSource.length},{"hasQuery",targetSource.query},{"hasFragment",targetSource.fragment}};
            result["targetSource"]=std::move(detail);
        }catch(...){} // A diagnostic allocation failure keeps the original refusal.
        return result;
    };
    try{
        validate(plan.sp);validate(plan.mainMenu);validate(plan.quickAccess);
        if(!ownerCurrent||!ownerCurrent()||!session.pid||!session.created||session.root.empty())return failure("document-read-cancelled");
        std::unique_lock lock(steamlive::transportMutex,std::try_to_lock);if(!lock.owns_lock())return failure("live-busy");
        const steamlive::TransportHoldObservation holder(lock,steamlive::TransportHolder::Documents);
        steamlive::SteamProcessLease process;
        const auto current=[&]()noexcept{try{return process.current()&&ownerCurrent();}catch(...){return false;}};
        if(!process.bind(session)||!current())return failure("document-read-cancelled");
        const auto deadline=GetTickCount64()+(std::max<DWORD>)(1,(std::min<DWORD>)(4000,budgetMs));
        const auto remaining=[&]()->DWORD{const auto now=GetTickCount64();if(!current()||now>=deadline)throw std::runtime_error("document-read-cancelled");return static_cast<DWORD>(deadline-now);};
        steamlive::HttpHandle http(WinHttpOpen(L"YMCC-SteamDocuments/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        const auto timeouts=[&](HINTERNET handle){const auto slice=(std::max<DWORD>)(1,(std::min<DWORD>)(500,remaining()/4));WinHttpSetTimeouts(handle,slice,slice,slice,slice);};
        auto discovery=selectDocumentTargetEndpoint(steamlive::listeners(session.root,session.pid),targetSource,[&]{remaining();},
            [&](const auto& candidate){
                timeouts(http.h);steamlive::HttpHandle connection(WinHttpConnect(http.h,candidate.host.c_str(),candidate.port,0));
                return Json::parse(steamlive::getTargets(connection.h,deadline));
            },[](const std::exception&,bool){});
        const auto endpoint=discovery.endpoint;
        if(!endpoint||!discovery.payload)return failure("document-main-unavailable");
        const auto selected=std::move(*discovery.payload);
        steamlive::ListenerProcessLease listener;
        if(!listener.bind(session,*endpoint,process))return failure("document-read-cancelled");
        const auto owned=[&]{return current()&&listener.current()&&steamlive::stillOwned(session.root,*endpoint,session.pid,&listener)&&current()&&listener.current();};
        if(!current()||!listener.current())return failure("document-read-cancelled");
        // Connecting allocates a local handle; the first upgrade still has a
        // fresh ownership check. Check ownership on setup failure as well.
        const auto connect=[&]{
            try{return steamlive::HttpHandle(WinHttpConnect(http.h,endpoint->host.c_str(),endpoint->port,0));}
            catch(...){if(!owned())throw std::runtime_error("document-read-cancelled");throw;}
        };
        auto connection=connect();
        struct Observation {Target target;std::unique_ptr<steamlive::BoundedSocket> socket;Json value;};
        std::vector<Observation> observations;
        for(const auto& target:selected){
            remaining();if(!owned())throw std::runtime_error("document-read-cancelled");timeouts(http.h);
            steamlive::HttpHandle upgrade(WinHttpOpenRequest(connection.h,L"GET",steamlive::widenAscii(target.path).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
            DWORD disabled=WINHTTP_DISABLE_REDIRECTS;WinHttpSetOption(upgrade.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
            if(!WinHttpSetOption(upgrade.h,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0)||!WinHttpSendRequest(upgrade.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(upgrade.h,nullptr))throw std::runtime_error("document-transport-unavailable");
            steamlive::requireStatus(upgrade.h,101);const auto socketBudget=remaining();
            steamlive::HttpHandle transferred(WinHttpWebSocketCompleteUpgrade(upgrade.h,0));
            auto socket=std::make_unique<steamlive::BoundedSocket>(transferred.h,socketBudget);transferred.h=nullptr;
            const auto value=evaluate(socket->h,expression(fields(plan,target.kind)),1,deadline,owned);
            validateReceipt(value,fields(plan,target.kind),target);
            observations.push_back({target,std::move(socket),value});
        }
        // These are sequential reads, never an atomic screenshot. Recheck each
        // document and the target set so a replaced earlier page stays Unknown.
        for(const auto& observed:observations){
            const auto final=evaluate(observed.socket->h,"(()=>({documentKey:String(performance.timeOrigin),sourceOrigin:globalThis.origin,sourceHref:location.href,ready:document.readyState!=='loading'}))()",2,deadline,owned);
            if(final.value("documentKey",std::string{})!=observed.value.value("documentKey",std::string{})||
                final.value("sourceOrigin",std::string{})!="https://steamloopback.host"||
                final.value("sourceHref",std::string{})!=observed.value.value("sourceHref",std::string{})||
                !final.contains("ready")||!final["ready"].is_boolean()||!final["ready"].get<bool>())throw std::runtime_error("document-replaced");
        }
        targetPhase="final-targets";timeouts(http.h);const auto finalTargets=targets(Json::parse(steamlive::getTargets(connection.h,deadline)),*endpoint,&targetSource);
        if(const auto reason=finalTargetSetFailure(selected,finalTargets,owned,deadline))return failure(reason);
        Json result=Json::array();std::set<Kind> present;
        for(auto& observed:observations){present.insert(observed.target.kind);result.push_back({{"kind",name(observed.target.kind)},{"id",observed.target.id},{"sourceUrl",observed.value.at("sourceHref")},{"value",std::move(observed.value)}});}
        return {{"ok",true},{"mutated",false},{"protocol",1},{"complete",present.size()==3},{"sequential",true},{"documents",std::move(result)}};
    }catch(const std::exception& e){const std::string reason=e.what();return failure(reason.starts_with("document-")?reason.c_str():"document-transport-unavailable");}
}
// Private integration candidate. The real caller must keep this exact scope
// when finish() reports debt. No global reaper, retries or detached ownership.
struct DocumentHttpParents {
    std::shared_ptr<steamlive::HttpHandle> session;
    HINTERNET connection=nullptr;
    explicit DocumentHttpParents(std::shared_ptr<steamlive::HttpHandle> value):session(std::move(value)){}
    ~DocumentHttpParents(){if(connection)WinHttpCloseHandle(connection);}
};
struct DocumentIoScope {
    ULONGLONG operationEnd=0;
    using Request=ymcc::deckymirror::AsyncRequest;
    std::shared_ptr<ymcc::deckymirror::CancelToken> cancellation;
    std::vector<std::unique_ptr<Request>> requests;
    std::vector<std::shared_ptr<DocumentHttpParents>> parents;
    std::vector<Request::Retirement> receipts;
    explicit DocumentIoScope(std::shared_ptr<ymcc::deckymirror::CancelToken> value)
        :cancellation(std::move(value)){
        // 16 discovery requests, 12 upgrades/sockets, one final list: <=41.
        // Allocate caller slots before opening any asynchronous request.
        requests.reserve(64);parents.reserve(32);receipts.reserve(64);
    }
    std::shared_ptr<DocumentHttpParents> connect(
        const std::shared_ptr<steamlive::HttpHandle>& session,const steamlive::Endpoint& endpoint){
        auto value=std::make_shared<DocumentHttpParents>(session);
        parents.push_back(value);
        value->connection=WinHttpConnect(session->h,endpoint.host.c_str(),endpoint.port,0);
        if(!value->connection)throw std::runtime_error("document-transport-unavailable");
        return value;
    }
    Request& open(const std::shared_ptr<DocumentHttpParents>& owner,const std::wstring& path,ULONGLONG end){
        if(requests.size()==requests.capacity())throw std::runtime_error("document-target-limit");
        std::unique_ptr<Request> request(new Request(Request::Prepared{},cancellation,end,false,owner));
        requests.push_back(std::move(request));
        auto& held=*requests.back();
        held.attachPrepared(WinHttpOpenRequest(owner->connection,L"GET",path.c_str(),
            nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
        return held;
    }
    Request& upgrade(Request& request,const std::shared_ptr<DocumentHttpParents>& owner,ULONGLONG end){
        if(requests.size()==requests.capacity())throw std::runtime_error("document-target-limit");
        std::unique_ptr<Request> socket(new Request(Request::Prepared{},cancellation,end,true,owner));
        requests.push_back(std::move(socket));
        auto& held=*requests.back();
        const auto context=held.preparedUpgradeContext();
        held.attachPrepared(WinHttpWebSocketCompleteUpgrade(request.handle(),context),true);
        return held;
    }
    bool finish(ULONGLONG end){
        receipts.clear();bool returned=true;
        for(auto i=requests.rbegin();i!=requests.rend();++i){
            const auto receipt=(*i)->retire(end);receipts.push_back(receipt);
            returned=returned&&receipt.returned();
        }
        return returned;
    }
};
inline Json evaluate(ymcc::deckymirror::AsyncRequest& socket,const std::string& code,
    unsigned id,ULONGLONG deadline,const std::function<bool()>& current){
    if(!current()||GetTickCount64()>=deadline)throw std::runtime_error("document-read-cancelled");
    const auto message=Json{{"id",id},{"method","Runtime.evaluate"},{"params",{{"expression",code},{"returnByValue",true}}}}.dump();
    socket.sendText(message);
    std::string reply;
    for(unsigned fragments=0,messages=0;fragments<256;){
        if(!current()||GetTickCount64()>=deadline)throw std::runtime_error("document-read-cancelled");
        const auto part=socket.documentFragment();
        const auto kind=part.kind;
        if(kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE&&kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
            throw std::runtime_error("document-transport-unavailable");
        ++fragments;reply.append(part.text);if(reply.size()>65536)throw std::runtime_error("document-reply-invalid");
        if(kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)continue;
        const auto packet=Json::parse(reply);reply.clear();if(++messages>16)throw std::runtime_error("document-reply-invalid");
        if(!replyIdMatches(packet,id))continue;
        if(packet.contains("error")||!packet.contains("result")||packet["result"].contains("exceptionDetails"))
            throw std::runtime_error("document-evaluation-unconfirmed");
        const auto& remote=packet["result"]["result"];
        if(remote.value("type",std::string{})!="object"||!remote.contains("value")||!remote["value"].is_object())
            throw std::runtime_error("document-reply-invalid");
        if(!current())throw std::runtime_error("document-read-cancelled");return remote["value"];
    }
    throw std::runtime_error("document-reply-invalid");
}

inline Json readStyles(const steamsession::Session& supplied,const StylePlan& plan,const std::function<bool()>& ownerCurrent,DWORD budgetMs,DocumentIoScope& io){
    const auto session=supplied;
    TargetSourceFailure targetSource;const char* targetPhase="discovery";
    const auto failure=[&](const char* reason){
        Json result={{"ok",false},{"reason",reason},{"mutated",false},{"documents",Json::array()},{"complete",false}};
        if(targetSource.captured&&std::string_view(reason)=="document-source-unconfirmed")try{
            Json detail={{"schema",1},{"phase",targetPhase},{"kind",name(targetSource.kind)},{"urlClass",targetSource.urlClass},
                {"urlLength",targetSource.length},{"hasQuery",targetSource.query},{"hasFragment",targetSource.fragment}};
            result["targetSource"]=std::move(detail);
        }catch(...){} // A diagnostic allocation failure keeps the original refusal.
        return result;
    };
    ULONGLONG operationEnd=0;
    const auto work=[&]()->Json{
    try{
        validate(plan.sp);validate(plan.mainMenu);validate(plan.quickAccess);
        if(!ownerCurrent||!ownerCurrent()||!session.pid||!session.created||session.root.empty())return failure("document-read-cancelled");
        std::unique_lock lock(steamlive::transportMutex,std::try_to_lock);if(!lock.owns_lock())return failure("live-busy");
        const steamlive::TransportHoldObservation holder(lock,steamlive::TransportHolder::Documents);
        steamlive::SteamProcessLease process;
        const auto current=[&]()noexcept{try{return process.current()&&ownerCurrent();}catch(...){return false;}};
        if(!process.bind(session)||!current())return failure("document-read-cancelled");
        const auto deadline=io.operationEnd?io.operationEnd:GetTickCount64()+(std::max<DWORD>)(1,(std::min<DWORD>)(4000,budgetMs));
        operationEnd=deadline;
        const auto remaining=[&]()->DWORD{const auto now=GetTickCount64();if(!current()||now>=deadline)throw std::runtime_error("document-read-cancelled");return static_cast<DWORD>(deadline-now);};
        std::shared_ptr<steamlive::HttpHandle> http(new steamlive::HttpHandle(WinHttpOpen(L"YMCC-SteamDocuments/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)));
        const auto timeouts=[&](HINTERNET handle){const auto slice=(std::max<DWORD>)(1,(std::min<DWORD>)(500,remaining()/4));WinHttpSetTimeouts(handle,slice,slice,slice,slice);};
        const auto getList=[&](const std::shared_ptr<DocumentHttpParents>& parent){
            auto& request=io.open(parent,L"/json/list",deadline);request.handshake();
            if(request.status()!=200)throw std::runtime_error("document-transport-unavailable");
            auto text=request.httpBody(1024*1024);
            if(!request.retire(deadline).returned())throw std::runtime_error("document-cleanup-incomplete");
            return text;
        };
        auto discovery=selectDocumentTargetEndpoint(steamlive::listeners(session.root,session.pid),targetSource,[&]{remaining();},
            [&](const auto& candidate){
                timeouts(http->h);auto connection=io.connect(http,candidate);
                return Json::parse(getList(connection));
            },[](const std::exception& error,bool){
                if(dynamic_cast<const ymcc::deckymirror::AsyncConstructionFailure*>(&error)||std::string_view(error.what())=="document-cleanup-incomplete")throw;
            });
        const auto endpoint=discovery.endpoint;
        if(!endpoint||!discovery.payload)return failure("document-main-unavailable");
        const auto selected=std::move(*discovery.payload);
        steamlive::ListenerProcessLease listener;
        if(!listener.bind(session,*endpoint,process))return failure("document-read-cancelled");
        const auto owned=[&]{
            if(!current()||!listener.current())return false;
            if(steamlive::listenersource::delegatedRole.load(std::memory_order_acquire)){
                // Fresh parent inventory remains the sole topology producer.
                // Keep both local held-image observations in child-then-parent
                // order; never pass the child's held pointer into that producer.
                const auto fresh=steamlive::listeners(session.root,session.pid);
                return std::find(fresh.begin(),fresh.end(),*endpoint)!=fresh.end()&&
                    listener.verify(session.root,endpoint->pid,session.pid,session.pid)&&current()&&listener.current();
            }
            return steamlive::stillOwned(session.root,*endpoint,session.pid,&listener)&&current()&&listener.current();
        };
        if(!current()||!listener.current())return failure("document-read-cancelled");
        const auto connect=[&]{
            try{return io.connect(http,*endpoint);}
            catch(...){if(!owned())throw std::runtime_error("document-read-cancelled");throw;}
        };
        auto connection=connect();
        struct Observation {Target target;ymcc::deckymirror::AsyncRequest* socket;Json value;};
        std::vector<Observation> observations;
        for(const auto& target:selected){
            remaining();if(!owned())throw std::runtime_error("document-read-cancelled");timeouts(http->h);
            auto& upgrade=io.open(connection,steamlive::widenAscii(target.path),deadline);
            upgrade.handshake(true);if(upgrade.status()!=101)throw std::runtime_error("document-transport-unavailable");
            auto& socket=io.upgrade(upgrade,connection,deadline);
            if(!upgrade.retire(deadline).returned())throw std::runtime_error("document-cleanup-incomplete");
            const auto value=evaluate(socket,expression(fields(plan,target.kind)),1,deadline,owned);
            validateReceipt(value,fields(plan,target.kind),target);
            observations.push_back({target,&socket,value});
        }
        // These are sequential reads, never an atomic screenshot. Recheck each
        // document and the target set so a replaced earlier page stays Unknown.
        for(const auto& observed:observations){
            const auto final=evaluate(*observed.socket,"(()=>({documentKey:String(performance.timeOrigin),sourceOrigin:globalThis.origin,sourceHref:location.href,ready:document.readyState!=='loading'}))()",2,deadline,owned);
            if(final.value("documentKey",std::string{})!=observed.value.value("documentKey",std::string{})||
                final.value("sourceOrigin",std::string{})!="https://steamloopback.host"||
                final.value("sourceHref",std::string{})!=observed.value.value("sourceHref",std::string{})||
                !final.contains("ready")||!final["ready"].is_boolean()||!final["ready"].get<bool>())throw std::runtime_error("document-replaced");
        }
        targetPhase="final-targets";timeouts(http->h);const auto finalTargets=targets(Json::parse(getList(connection)),*endpoint,&targetSource);
        if(const auto reason=finalTargetSetFailure(selected,finalTargets,owned,deadline))return failure(reason);
        Json result=Json::array();std::set<Kind> present;
        for(auto& observed:observations){present.insert(observed.target.kind);result.push_back({{"kind",name(observed.target.kind)},{"id",observed.target.id},{"sourceUrl",observed.value.at("sourceHref")},{"value",std::move(observed.value)}});}
        return {{"ok",true},{"mutated",false},{"protocol",1},{"complete",present.size()==3},{"sequential",true},{"documents",std::move(result)}};
    }catch(const ymcc::deckymirror::AsyncConstructionFailure&){return failure("document-initialization-failed");}
    catch(const std::exception& e){const std::string reason=e.what();return failure(reason.starts_with("document-")?reason.c_str():"document-transport-unavailable");}
    };
    // The original operation end includes retirement; do not mint cleanup time.
    auto result=work();
    if(!io.finish(operationEnd))return failure("document-cleanup-incomplete");
    return result;
}

} // namespace ymcc::steamdocuments
