// Explicit variant of the existing native transport consumer. The current and
// previous bootstrap discovery blocks are verbatim extractions; only IO leaves
// below are memory ports. The common coordinator is compiled from product code.
#include "decky_sidebar_bootstrap.h"
namespace endpointDiscoveryBootstrap {
using Endpoint=ymcc::steamlive::Endpoint;
using ymcc::deckymirror::AsyncFailure;
using ymcc::deckymirror::AsyncRetirementFailure;
using ymcc::deckymirror::bootstrapDiscoveryRejected;
using ymcc::deckymirror::bootstrapTransportFailure;
using ymcc::deckymirror::bootstrapSafeReason;
using Stage=ymcc::steamfailure::SourceStage;
struct Reply { DWORD code=0;Stage stage=Stage::Discovery;unsigned status=200;bool untyped=false; };
inline std::vector<Reply> replies;
inline unsigned fetches=0,opens=0,closes=0,checks=0,upgrades=0,upgradeStatus=101;
inline bool allowed=true,cancelled=false,failFirst=false,cancelAfterFetch=false,invalid=false;
inline bool expired=false,cancelAfterFailure=false,expireAfterFailure=false;
struct HttpHandle {
    HINTERNET h=nullptr;
    explicit HttpHandle(HINTERNET value):h(value){++opens;}
    ~HttpHandle(){++closes;}
};
// Memory HTTP parents match the current source shape; no real API callback proof.
struct BootstrapHttpParents {
    const std::shared_ptr<HttpHandle> session,connection;
    BootstrapHttpParents(std::shared_ptr<HttpHandle> s,std::shared_ptr<HttpHandle> c) noexcept:session(std::move(s)),connection(std::move(c)){}
};
static HINTERNET WinHttpConnect(HINTERNET,LPCWSTR host,INTERNET_PORT,DWORD){return reinterpret_cast<HINTERNET>(static_cast<std::uintptr_t>(std::wstring(host)==L"::1"?2u:1u));}
static HINTERNET WinHttpOpenRequest(HINTERNET value,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR*,DWORD){return value;}
struct AsyncRequest {
    HINTERNET value;
    unsigned ordinal=0;bool upgrading=false;
    bool retired=false;std::shared_ptr<void> parentLifetime;
    AsyncRequest(HINTERNET handle,int,ULONGLONG,bool=false,std::shared_ptr<void> parents={}):value(handle),parentLifetime(std::move(parents)){++opens;}
    ~AsyncRequest(){if(!retired)++closes;}
    struct Retirement {bool closeAccepted=true,finalNotification=true;DWORD error=0;bool returned()const{return finalNotification;}};
    Retirement retire(ULONGLONG){if(!retired){retired=true;++closes;parentLifetime.reset();}return {};}
    void handshake(bool upgrade=false){
        if(upgrade){upgrading=true;++upgrades;return;}
        ordinal=++fetches;
        if(ordinal<=replies.size()){
            const auto& reply=replies[ordinal-1];
            if(reply.code||reply.untyped){
                if(cancelAfterFailure)cancelled=true;
                if(expireAfterFailure)expired=true;
                if(reply.untyped)throw std::runtime_error("mirror-bootstrap-async-error");
                throw AsyncFailure("mirror-bootstrap-async-error",reply.code,reply.stage);
            }
        }
        if(failFirst&&fetches==1)throw std::runtime_error("mirror-bootstrap-async-error");
    }
    unsigned status()const{return upgrading?upgradeStatus:ordinal&&ordinal<=replies.size()?replies[ordinal-1].status:200;}
    std::string httpBody(unsigned)const{
        if(cancelAfterFetch)cancelled=true;
        const bool ipv6=reinterpret_cast<std::uintptr_t>(value)==2;
        auto tab=Json{{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},
            {"webSocketDebuggerUrl",std::string("ws://")+(ipv6?"[::1]":"127.0.0.1")+":8080/devtools/page/ABC-123"}};
        if(invalid)tab["webSocketDebuggerUrl"]="ws://foreign.invalid/secret";
        return Json::array({tab}).dump();
    }
};
template<class Work>
auto bootstrapRequestRetired(AsyncRequest& request,ULONGLONG deadline,Work&& work){
    using Value=decltype(work(request));std::optional<Value> value;std::exception_ptr failure;
    try{value.emplace(work(request));}catch(...){failure=std::current_exception();}
    const auto returned=request.retire(deadline);
    if(!returned.returned())throw AsyncRetirementFailure(returned.closeAccepted,returned.finalNotification,returned.error,failure);
    if(failure)std::rethrow_exception(failure);
    return std::move(*value);
}
static bool sameWindowsUserAndSession(DWORD){return allowed;}
using ymcc::steamlive::targetPath;
static void reset(){fetches=opens=closes=checks=upgrades=0;upgradeStatus=101;replies.clear();allowed=true;
    cancelled=failFirst=cancelAfterFetch=invalid=expired=cancelAfterFailure=expireAfterFailure=false;}
// The reference outlives discovery stack unwinding and the verbatim early return.
static std::pair<Endpoint,std::string> bootstrapDiagnostic(const std::vector<Endpoint>& candidates,bool before,Json& discoveryFailure){
    (void)discoveryFailure; // The historical block has no diagnostic sink.
    auto session=std::make_shared<HttpHandle>(reinterpret_cast<HINTERNET>(7));
    const int cancellation=0;const auto deadline=GetTickCount64()+7000;
    const auto checkpoint=[&]{++checks;if(cancelled)throw std::runtime_error("mirror-bootstrap-canceled");
        if(expired)throw std::runtime_error("mirror-bootstrap-deadline");};
    if(before){
#include "bootstrap_discovery_before.inc"
    }else{
#include "bootstrap_discovery_current.inc"
    }
}
static std::pair<Endpoint,std::string> bootstrap(const std::vector<Endpoint>& candidates,bool before){
    Json discoveryFailure;return bootstrapDiagnostic(candidates,before,discoveryFailure);
}
template<class Thrower> static Json failureResult(Thrower&& thrower,Json& discoveryFailure,Json steamFailure=Json(),bool sent=false){
    const auto operationStarted=GetTickCount64();
    try{std::forward<Thrower>(thrower)();return Json{{"ok",true},{"mutated",sent}};}
#include "bootstrap_failure_result.inc"
}
}
static int endpointDiscoveryFixture(bool beforeOnly=false){
    using Endpoint=ymcc::steamlive::Endpoint;using Kind=ymcc::steamdiscovery::Kind;
    const std::vector<Endpoint> aliases{{424243,8080,L"127.0.0.1"},{424243,8080,L"::1"}};
    std::vector<std::string> names;unsigned checks=0;
    const auto need=[&](bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;};
    const auto test=[&](const char* name,const auto& run){run();names.emplace_back(name);};
    if(beforeOnly){endpointDiscoveryBootstrap::reset();const auto value=endpointDiscoveryBootstrap::bootstrap(aliases,true);(void)value;
        need(endpointDiscoveryBootstrap::fetches==1,"shared bootstrap successful alias must use one target lookup");return 0;}
    test("bootstrap-successful-ip-alias-one-fetch",[&]{endpointDiscoveryBootstrap::reset();const auto value=endpointDiscoveryBootstrap::bootstrap(aliases,false);
        need(value.first.host==L"127.0.0.1"&&value.second=="/devtools/page/ABC-123","bootstrap keeps first successful local endpoint");
        need(endpointDiscoveryBootstrap::fetches==1,"shared bootstrap successful alias must use one target lookup");
        need(endpointDiscoveryBootstrap::opens==endpointDiscoveryBootstrap::closes,"successful bootstrap discovery closes original memory IO handles");});
    test("bootstrap-failed-first-alias-can-recover",[&]{endpointDiscoveryBootstrap::reset();endpointDiscoveryBootstrap::failFirst=true;const auto value=endpointDiscoveryBootstrap::bootstrap(aliases,false);
        need(value.first.host==L"::1"&&endpointDiscoveryBootstrap::fetches==2,"first failed address does not suppress a later live alias");
        need(endpointDiscoveryBootstrap::opens==endpointDiscoveryBootstrap::closes,"failed bootstrap alias has no IO handle leak");});
    test("bootstrap-foreign-user-no-lookup",[&]{endpointDiscoveryBootstrap::reset();endpointDiscoveryBootstrap::allowed=false;bool rejected=false;
        try{endpointDiscoveryBootstrap::bootstrap(aliases,false);}catch(const std::exception&e){rejected=std::string(e.what())=="mirror-bootstrap-context-unavailable";}
        need(rejected&&endpointDiscoveryBootstrap::fetches==0&&endpointDiscoveryBootstrap::opens==endpointDiscoveryBootstrap::closes,"bootstrap preserves user-session eligibility before target IO");});
    test("bootstrap-cancel-after-fetch-refuses",[&]{endpointDiscoveryBootstrap::reset();endpointDiscoveryBootstrap::cancelAfterFetch=true;bool rejected=false;
        try{endpointDiscoveryBootstrap::bootstrap(aliases,false);}catch(const std::exception&e){rejected=std::string(e.what())=="mirror-bootstrap-canceled";}
        need(rejected&&endpointDiscoveryBootstrap::fetches==1&&endpointDiscoveryBootstrap::opens==endpointDiscoveryBootstrap::closes,"bootstrap reply cannot become selected after cancellation");});
    test("bootstrap-invalid-target-is-terminal",[&]{endpointDiscoveryBootstrap::reset();endpointDiscoveryBootstrap::invalid=true;bool rejected=false;
        try{endpointDiscoveryBootstrap::bootstrap(aliases,false);}catch(const std::exception&e){rejected=std::string(e.what())=="live-targets-invalid";}
        need(rejected&&endpointDiscoveryBootstrap::fetches==1&&endpointDiscoveryBootstrap::opens==endpointDiscoveryBootstrap::closes,"malformed target is never masked by a second alias");});
    for(const auto kind:{Kind::Live,Kind::Documents,Kind::Bootstrap}){
        const std::string label=kind==Kind::Live?"live":kind==Kind::Documents?"documents":"bootstrap";
        test((label+"-same-socket-coalesced").c_str(),[&]{unsigned calls=0;const auto selected=ymcc::steamdiscovery::select<std::string>(kind,aliases,[]{},[](const auto&){return true;},[&](const auto&)->std::optional<std::string>{++calls;return "target";},[](const auto&,bool){});
            need(selected.endpoint&&selected.payload=="target"&&calls==1,"all typed discovery consumers share successful socket alias coalescing");});
        test((label+"-same-port-different-process-ambiguous").c_str(),[&]{auto values=aliases;values[1].pid++;unsigned calls=0;bool rejected=false;
            try{ymcc::steamdiscovery::select<std::string>(kind,values,[]{},[](const auto&){return true;},[&](const auto&)->std::optional<std::string>{++calls;return "target";},[](const auto&,bool){});}catch(const std::exception&e){rejected=std::string(e.what())==ymcc::steamdiscovery::ambiguity(kind);}
            need(rejected&&calls==2,"same port on a second process is still ambiguous");});
        test((label+"-same-process-other-port-ambiguous").c_str(),[&]{auto values=aliases;values[1].port++;bool rejected=false;
            try{ymcc::steamdiscovery::select<std::string>(kind,values,[]{},[](const auto&){return true;},[](const auto&)->std::optional<std::string>{return "target";},[](const auto&,bool){});}catch(const std::exception&e){rejected=std::string(e.what())==ymcc::steamdiscovery::ambiguity(kind);}
            need(rejected,"another port on the same process is still a second endpoint");});
    }
    test("documents-empty-prefix-does-not-hide-later-target",[&]{unsigned calls=0;const auto selected=ymcc::steamdiscovery::select<std::vector<int>>(Kind::Documents,aliases,[]{},[](const auto&){return true;},[&](const auto&)->std::optional<std::vector<int>>{if(++calls==1)return {};return std::vector<int>{1};},[](const auto&,bool){});
        need(selected.endpoint&&selected.endpoint->host==L"::1"&&calls==2&&selected.payload->size()==1,"empty document result never establishes a selected alias");});
    test("documents-parse-failure-not-hidden",[&]{unsigned calls=0;bool rejected=false;try{ymcc::steamdiscovery::select<int>(Kind::Documents,aliases,[]{},[](const auto&){return true;},[&](const auto&)->std::optional<int>{++calls;throw std::runtime_error("document-source-unconfirmed");},[](const auto&,bool){});}catch(const std::exception&e){rejected=std::string(e.what())=="document-source-unconfirmed";}
        need(rejected&&calls==1,"document source failure remains terminal");});
    test("live-failed-prefix-retains-first-error-and-recovers",[&]{unsigned calls=0,rejections=0;const auto selected=ymcc::steamdiscovery::select<std::string>(Kind::Live,aliases,[]{},[](const auto&){return true;},[&](const auto&)->std::optional<std::string>{if(++calls==1)throw std::runtime_error("live-transport-failed");return "target";},[&](const auto&e,bool selected){need(!selected&&std::string(e.what())=="live-transport-failed","original unsettled discovery failure is forwarded to its existing diagnostic sink");++rejections;});
        need(selected.endpoint&&calls==2&&rejections==1,"nonterminal live candidate can recover without replaying an action");});
    test("cancel-before-first-io",[&]{unsigned calls=0;bool rejected=false;try{ymcc::steamdiscovery::select<int>(Kind::Live,aliases,[]{throw std::runtime_error("cancelled");},[](const auto&){return true;},[&](const auto&)->std::optional<int>{++calls;return 1;},[](const auto&,bool){});}catch(...){rejected=true;}
        need(rejected&&calls==0,"cancelled request does not discover targets");});
    test("cancel-during-ignored-error",[&]{bool alive=true,rejected=false;unsigned calls=0;try{ymcc::steamdiscovery::select<int>(Kind::Live,aliases,[&]{if(!alive)throw std::runtime_error("cancelled");},[](const auto&){return true;},[&](const auto&)->std::optional<int>{++calls;alive=false;throw std::runtime_error("live-transport-failed");},[](const auto&,bool){});}catch(const std::exception&e){rejected=std::string(e.what())=="cancelled";}
        need(rejected&&calls==1,"candidate error handling cannot swallow a revoked request");});
    test("unknown-kind-before-callback",[&]{unsigned calls=0;bool rejected=false;try{ymcc::steamdiscovery::select<int>(static_cast<Kind>(99),aliases,[&]{++calls;},[](const auto&){return true;},[](const auto&)->std::optional<int>{return 1;},[](const auto&,bool){});}catch(const std::exception&e){rejected=std::string(e.what())=="invalid-steam-discovery-kind";}
        need(rejected&&calls==0,"unknown typed discovery never reaches a callback");});
    std::cout<<"ENDPOINT_DISCOVERY_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",checks},{"realSteamOperations",0},{"defaultGroupExecuted",false},{"actualBootstrapDiscoveryBlocks",true}}.dump()<<"\n";return 0;
}

// An explicit subcase of endpoint-discovery. It never enters the default 20.
static int bootstrapFailureDiagnosticsFixture(bool beforeOnly=false){
    namespace sf=ymcc::steamfailure;
    namespace port=endpointDiscoveryBootstrap;
    using Stage=sf::SourceStage;
    using ymcc::deckymirror::AsyncFailure;
    const std::vector<port::Endpoint> aliases{{424243,8080,L"127.0.0.1"},{424243,8080,L"::1"}};
    std::vector<std::string> names;unsigned checks=0;
    const auto need=[&](bool value,const char* assertion){if(!value)throw std::runtime_error(assertion);++checks;};
    const auto factIs=[](const Json& result,const char* domain,const char* stage,std::int64_t code){
        if(!result.contains("steamFailure"))return false;
        const auto& fact=result["steamFailure"];
        return fact.is_object()&&fact.value("domain",std::string{})==domain&&fact.value("sourceStage",std::string{})==stage&&
            fact.value("codeKnown",false)&&fact.contains("code")&&fact["code"].is_number_integer()&&fact["code"].get<std::int64_t>()==code;
    };
    const auto reasonIs=[&](const Json& result,const char* reason,bool sent=false){
        need(!result.value("ok",true)&&result.value("reason",std::string{})==reason,"bootstrap final catch preserves original reason");
        need(result.value("mutated",!sent)==sent&&result.value("deadlineMs",0)==7000,"bootstrap diagnostics preserve mutation and original deadline");
    };
    const auto allFail=[] {port::replies={{ERROR_WINHTTP_CANNOT_CONNECT,Stage::Discovery},{ERROR_WINHTTP_CONNECTION_ERROR,Stage::Receive}};};
    if(beforeOnly){
        port::reset();allFail();Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,true,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-context-unavailable");
        need(port::fetches==2&&port::upgrades==0&&port::opens==5&&port::closes==5,"before discovery returns all original memory handles without extra fetches");
        // This is the sole old R30 behavior: the original sink discards the code.
        need(factIs(result,"winhttp","receive",ERROR_WINHTTP_CONNECTION_ERROR),"bootstrap discovery must retain the original bounded failure");
        return 0;
    }
    const auto test=[&](const char* name,unsigned fetches,unsigned opens,unsigned upgrades,const auto& body){
        port::reset();const auto result=body();
        need(port::fetches==fetches&&port::upgrades==upgrades,"bootstrap diagnostics add no discovery fetch or upgrade");
        need(port::opens==opens&&port::closes==opens,"bootstrap diagnostics return every original memory handle");
        const auto counters=std::array<unsigned,5>{port::fetches,port::opens,port::closes,port::checks,port::upgrades};
        sf::FailureContext context;context.actionId=17;context.recoveryEpisode=3;context.steamPid=424243;context.steamBirth=19;
        const auto projection=sf::project(result,context);
        need(projection==sf::project(result,context),"bootstrap failure projection is deterministic and pure");
        need(counters==std::array<unsigned,5>{port::fetches,port::opens,port::closes,port::checks,port::upgrades},"pure projection performs no memory IO or checkpoint");
        need(projection.dump().size()<sf::recordLimit&&result.dump().size()<sf::recordLimit,"bootstrap diagnostics retain the original bounded projection size");
        if(result.contains("steamFailure")){
            need(result["steamFailure"].is_object()&&sf::normalize(result["steamFailure"])==result["steamFailure"],"bootstrap retains a single normalized failure fact");
            need(projection["steamFailure"]==result["steamFailure"],"consumer projection preserves the selected bounded fact");
        }else need(!projection["steamFailure"].value("codeKnown",true),"absence of a current failure never fabricates a code");
        for(const auto* text:{"RAW-BOOTSTRAP-SECRET","foreign.invalid","steamloopback.host","ABC-123","127.0.0.1","token="})
            need(result.dump().find(text)==std::string::npos&&projection.dump().find(text)==std::string::npos,"bootstrap result and projection contain no raw remote text or endpoint");
        names.emplace_back(name);
    };
    // These priority checks obtain their stale candidate fact from the actual
    // discovery block, then exercise only the actual final catch, with no IO.
    const auto retainedCandidate=[&](Json& discoveryFailure){
        bool unavailable=false;
        try{port::bootstrapDiagnostic(aliases,false,discoveryFailure);}
        catch(const std::exception& error){unavailable=std::string(error.what())=="mirror-bootstrap-context-unavailable";}
        need(unavailable&&discoveryFailure.is_object()&&discoveryFailure.value("code",0u)==ERROR_WINHTTP_CONNECTION_ERROR,
            "priority control starts with the last actual memory discovery failure");
    };
    test("bootstrap-all-alias-async-failures-retain-last",2,5,0,[&]{
        allFail();Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-context-unavailable");
        need(factIs(result,"winhttp","receive",ERROR_WINHTTP_CONNECTION_ERROR),"all failed aliases retain the last actual AsyncFailure code and stage");return result;
    });
    test("bootstrap-non200-retains-http-discovery",2,5,0,[&]{
        port::replies={{0,Stage::Discovery,502},{0,Stage::Discovery,503}};Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-context-unavailable");
        need(factIs(result,"http","discovery",503),"non200 discovery retains the actual last status with its original context-unavailable reason");return result;
    });
    test("bootstrap-untyped-alias-keeps-last-typed-fact",2,5,0,[&]{
        port::replies={{ERROR_WINHTTP_CANNOT_CONNECT,Stage::Discovery},{0,Stage::Discovery,200,true}};Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-context-unavailable");
        need(factIs(result,"winhttp","discovery",ERROR_WINHTTP_CANNOT_CONNECT),"an untyped alias error cannot erase the last real typed discovery fact");return result;
    });
    test("bootstrap-recovered-alias-clears-discovery-failure",2,5,0,[&]{
        port::replies={{ERROR_WINHTTP_CANNOT_CONNECT,Stage::Discovery}};Json discoveryFailure;
        const auto result=port::failureResult([&]{const auto selected=port::bootstrapDiagnostic(aliases,false,discoveryFailure);
            need(selected.first.host==L"::1"&&selected.second=="/devtools/page/ABC-123","diagnostics preserve recovery by the second alias");},discoveryFailure);
        need(result.value("ok",false)&&discoveryFailure.is_null()&&!result.contains("steamFailure"),"a recovered alias publishes no stale discovery code");return result;
    });
    test("bootstrap-cancel-does-not-attach-discovery-failure",1,3,0,[&]{
        allFail();port::cancelAfterFailure=true;Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-canceled");
        need(discoveryFailure.is_object()&&!result.contains("steamFailure"),"cancellation cannot borrow the candidate failure retained before the checkpoint");return result;
    });
    test("bootstrap-deadline-does-not-attach-discovery-failure",1,3,0,[&]{
        allFail();port::expireAfterFailure=true;Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-deadline");
        need(discoveryFailure.is_object()&&!result.contains("steamFailure"),"deadline cannot borrow the candidate failure retained before the checkpoint");return result;
    });
    test("bootstrap-later-error-after-recovery-has-no-old-code",2,5,0,[&]{
        port::replies={{ERROR_WINHTTP_CANNOT_CONNECT,Stage::Discovery}};Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);
            throw std::runtime_error("mirror-bootstrap-owner-mismatch");},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-owner-mismatch");
        need(discoveryFailure.is_null()&&!result.contains("steamFailure"),"a later phase cannot inherit a recovered alias code");return result;
    });
    test("bootstrap-non101-retains-http-upgrade",2,7,1,[&]{
        port::replies={{ERROR_WINHTTP_CANNOT_CONNECT,Stage::Discovery}};port::upgradeStatus=403;Json discoveryFailure;
        const auto result=port::failureResult([&]{port::bootstrapDiagnostic(aliases,false,discoveryFailure);
            port::HttpHandle connection(reinterpret_cast<HINTERNET>(1));
            port::AsyncRequest request(connection.h,0,GetTickCount64()+7000);
            // The product upgrade status guard; only request's IO leaf is memory.
            request.handshake(true);const auto upgradeStatus=request.status();if(upgradeStatus!=101)throw AsyncFailure("mirror-bootstrap-upgrade-status",upgradeStatus,ymcc::steamfailure::SourceStage::Upgrade,ymcc::steamfailure::Domain::Http);
        },discoveryFailure);
        reasonIs(result,"mirror-bootstrap-upgrade-status");
        need(discoveryFailure.is_null()&&factIs(result,"http","upgrade",403),"non101 upgrade retains its current HTTP status and original reason");return result;
    });
    test("bootstrap-cdp-failure-precedes-discovery-failure",2,5,0,[&]{
        allFail();Json discoveryFailure;retainedCandidate(discoveryFailure);
        const auto cdp=sf::cdp(Json{{"error",{{"code",-32000},{"message","RAW-BOOTSTRAP-SECRET token=hidden target closed"}}}});
        const auto result=port::failureResult([]{throw std::runtime_error("mirror-bootstrap-readback-failed");},discoveryFailure,cdp,true);
        reasonIs(result,"mirror-bootstrap-readback-failed",true);
        need(factIs(result,"cdp-protocol","evaluate",-32000)&&result["steamFailure"]==cdp,"current CDP failure takes precedence over the older candidate failure");return result;
    });
    test("bootstrap-current-typed-failure-precedes-cdp-and-discovery",2,5,0,[&]{
        allFail();Json discoveryFailure;retainedCandidate(discoveryFailure);
        const auto cdp=sf::cdp(Json{{"error",{{"code",-32001},{"message","RAW-BOOTSTRAP-SECRET token=hidden"}}}});
        const auto result=port::failureResult([]{throw port::AsyncFailure("mirror-bootstrap-async-error",ERROR_WINHTTP_TIMEOUT,Stage::Receive);},discoveryFailure,cdp,true);
        reasonIs(result,"mirror-bootstrap-async-error",true);
        need(factIs(result,"winhttp","receive",ERROR_WINHTTP_TIMEOUT),"current typed error preserves its dynamic type and precedes CDP and candidate failures");return result;
    });
    test("bootstrap-later-error-rejects-pending-discovery-failure",2,5,0,[&]{
        allFail();Json discoveryFailure;retainedCandidate(discoveryFailure);
        const auto result=port::failureResult([]{throw std::runtime_error("mirror-bootstrap-session-changed");},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-session-changed");
        need(discoveryFailure.is_object()&&!result.contains("steamFailure"),"only context-unavailable may project a pending discovery failure");return result;
    });
    test("bootstrap-unsafe-reason-does-not-leak-or-reuse-code",2,5,0,[&]{
        allFail();Json discoveryFailure;retainedCandidate(discoveryFailure);
        const auto result=port::failureResult([]{throw std::runtime_error("RAW-BOOTSTRAP-SECRET https://foreign.invalid/?token=hidden");},discoveryFailure);
        reasonIs(result,"mirror-bootstrap-invalid-message");
        need(!result.contains("steamFailure"),"unrecognized later error cannot reuse the older candidate failure");return result;
    });
    std::cout<<"BOOTSTRAP_DISCOVERY_DIAGNOSTICS_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",checks},
        {"defaultGroupExecuted",false},{"realSteamOperations",0}}.dump()<<"\n";return 0;
}

// Both main configurations use this gate before any business branch. Detect the
// flag anywhere so mixed/reordered selectors cannot fall through to live IO.
static std::optional<int> endpointDiscoverySelection(int argc,char** argv){
    bool requested=false;for(int i=1;i<argc;++i){const std::string_view argument=argv[i];
        if(argument=="--endpoint-discovery-only"||argument=="bootstrap-failure-diagnostics"||argument=="bootstrap-failure-before"||argument=="bootstrap-old-alias")requested=true;}
    if(!requested)return {};
    if(argc==2&&std::string_view(argv[1])=="--endpoint-discovery-only")return endpointDiscoveryFixture();
    if(argc!=4||std::string_view(argv[1])!="--endpoint-discovery-only"||std::string_view(argv[2])!="--case")return 2;
    const std::string_view selected=argv[3];
    if(selected=="bootstrap-old-alias")return endpointDiscoveryFixture(true);
    if(selected=="bootstrap-failure-diagnostics")return bootstrapFailureDiagnosticsFixture();
    if(selected=="bootstrap-failure-before")return bootstrapFailureDiagnosticsFixture(true);
    return 2;
}
