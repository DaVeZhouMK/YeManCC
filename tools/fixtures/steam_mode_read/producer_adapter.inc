#include "steam_ui_read_budget.h"
#include "decky_frontend_state.h"
#include "steam_failure_diagnostics.h"
#include "decky_update_recovery.h"
#include "steam_live_cdp.h"
namespace mode_producer_port {
using namespace mode_source_directed;
struct Identity {Session session=syntheticSession();int presence=1;std::uint64_t processEpoch=1,accountEpoch=1;};
struct IdentityObserver {std::shared_ptr<Identity> value=std::make_shared<Identity>();auto identitySnapshot(){return value;}bool sameIdentityEpoch(const std::shared_ptr<Identity>& i){const auto current=identitySnapshot();return i&&current&&i->processEpoch==current->processEpoch&&i->accountEpoch==current->accountEpoch&&i->session==current->session&&i->presence==current->presence;}};
inline Gate* liveGate=nullptr;inline Json liveResult;inline std::atomic<unsigned> liveCalls{0};
inline std::atomic<unsigned> sharedObserverCalls{0},invalidLiveCalls{0};
inline ymcc::steamlive::ModeTransportObservation liveObservation;
}
namespace ymcc::steamlive {
// Only this fixture identity selects the inert transport boundary.
inline Json runOwned(mode_producer_port::IdentityObserver& observer,const std::wstring& root,const Json& request,
    const std::function<bool()>& admitAction,DWORD budget,const std::function<bool()>& ownerCurrent,
    const steamsession::Session* expected,bool borrowCommand,steamcommand::Disposition* disposition,
    ModeTransportWait wait,ModeTransportObservation* observation) {
    using namespace mode_producer_port;
    ++liveCalls;
    if(wait==ModeTransportWait::SharedObserver)++sharedObserverCalls;
    if(!expected||!(*expected==observer.identitySnapshot()->session)||root!=expected->root||
        request!=Json{{"operation","ui.mode.get"}}||admitAction||budget!=4000||!ownerCurrent||borrowCommand||disposition)
        ++invalidLiveCalls;
    if(liveGate)liveGate->arrive();
    if(observation)*observation=liveObservation;
    return liveResult;
}
}
namespace ymcc::steamui {
using Json=nlohmann::json;
#include "producer_read.inc"
}
namespace mode_source_directed {
static void producerCacheChecks(Case& c,bool control){
    mode_producer_port::IdentityObserver identity;mode_producer_port::liveGate=nullptr;mode_producer_port::liveCalls=0;
    mode_producer_port::liveResult={{"ok",true},{"mode","bigpicture"},{"observedAccount",42},{"deckyAccountWatchAvailable",true},{"modeEpoch","original-cached-document"}};
    const auto execute=[&](const std::string& name){
        const auto warm=steamui::read(identity,true);
        c.check("original-force-warms-current-cache/"+name,warm.value("known",false)&&warm.value("mode",std::string{})=="bigpicture"&&warm.value("observedAccount",0)==42);
        const auto calls=mode_producer_port::liveCalls.load();bool owner=true;unsigned reads=0;
        const auto result=steamui::read(identity,false,[&]{const bool captured=owner;if(++reads==2){
            if(name=="cached-owner-cancel")owner=false;
            else if(name!="cached-stable") {auto next=std::make_shared<mode_producer_port::Identity>(*identity.value);
                if(name=="cached-account-epoch")++next->accountEpoch;
                else if(name=="cached-process-epoch")++next->processEpoch;
                else if(name=="cached-process-birth")++next->session.created;
                identity.value=std::move(next);}
        }return captured;});
        c.metadata["cacheReturns"].push_back({{"id",name},{"warm",warm},{"actual",result},{"ownerReads",reads},{"ownerAfter",owner},{"additionalLiveCalls",mode_producer_port::liveCalls.load()-calls}});
        if(name=="cached-stable"||name=="cached-equivalent-publication")c.check("stable-cache-unchanged/"+name,result==warm);
        else c.check("cached-return-revokes-proof/"+name,!result.value("known",true)&&result.value("mode",std::string{})=="unknown"&&!result.contains("observedAccount")&&result.value("reason",std::string{})==(name=="cached-owner-cancel"?"shared-mode-read-cancelled":"steam-session-changed"));
        c.check("cached-return-zero-discovery/"+name,mode_producer_port::liveCalls==calls);
        if(name=="cached-account-epoch"||name=="cached-process-epoch") {const auto after=steamui::read(identity);c.check("epoch-rejection-keeps-five-second-budget/"+name,after.value("mode",std::string{})=="unknown"&&!after.contains("observedAccount")&&mode_producer_port::liveCalls==calls);}
    };
    if(control)execute("cached-account-epoch");else for(const auto* name:{"cached-stable","cached-equivalent-publication","cached-account-epoch","cached-process-epoch","cached-process-birth","cached-owner-cancel"})execute(name);
}
static void producerChecks(Case& c){
    using Wait=steamlive::ModeTransportObservation::Wait;
    mode_producer_port::IdentityObserver identity;Gate hold;mode_producer_port::liveGate=&hold;
    mode_producer_port::liveResult={{"ok",false},{"reason","live-busy"}};mode_producer_port::liveCalls=0;
    mode_producer_port::sharedObserverCalls=0;mode_producer_port::invalidLiveCalls=0;
    Json first;JoiningThread worker([&]{first=steamui::read(identity,true);},[&]{hold.release();});
    c.check("actual-producer-first-thread-holds-native-mutex",hold.entered.wait()==WAIT_OBJECT_0);
    steamlive::ModeTransportObservation observation;
    observation.wait=Wait::Busy;observation.waitMs=123;observation.rendererBusy=true;
    const auto tagged=steamui::read(identity,true,{},steamlive::ModeTransportWait::SharedObserver,&observation);
    c.metadata["actualNativeTryLockMiss"]=tagged;
    c.check("native-trylock-miss-is-exact-seven-field-tag",tagged.size()==7&&tagged.value("localReadBusy",false)&&tagged.value("reason",std::string{})=="live-busy"&&tagged.value("mode",std::string{})=="unknown"&&tagged.value("known",false)&&tagged.value("running",false)&&tagged["pid"].is_number_integer()&&tagged["pid"]==identity.value->session.pid&&tagged["birth"]==std::to_string(identity.value->session.created));
    c.check("trylock-miss-does-not-call-live",mode_producer_port::liveCalls==1&&mode_producer_port::sharedObserverCalls==0);
    c.check("trylock-miss-clears-stale-transport-observation",observation.wait==Wait::None&&observation.waitMs==0&&!observation.rendererBusy);
    hold.release();worker.join();mode_producer_port::liveGate=nullptr;
    c.metadata["actualCdpLiveBusy"]=first;
    c.check("ordinary-cdp-livebusy-has-no-native-tag",first.value("reason",std::string{})=="live-busy"&&!first.contains("localReadBusy"));
    std::atomic<bool> owner{false};auto before=mode_producer_port::liveCalls.load();auto early=steamui::read(identity,true,[&]{return owner.load();});
    c.check("owner-already-cancelled-keeps-original-envelope",early.value("reason",std::string{})=="shared-mode-read-cancelled"&&!early.value("known",true)&&!early.contains("localReadBusy")&&mode_producer_port::liveCalls==before);
    Gate cancelGate;mode_producer_port::liveGate=&cancelGate;owner=true;Json late;
    JoiningThread cancellation([&]{late=steamui::read(identity,true,[&]{return owner.load();});},[&]{cancelGate.release();});
    c.check("owner-cancel-real-read-held",cancelGate.entered.wait()==WAIT_OBJECT_0);owner=false;cancelGate.release();cancellation.join();mode_producer_port::liveGate=nullptr;
    c.check("late-owner-cancel-preserves-cancel-receipt",late.value("reason",std::string{})=="shared-mode-read-cancelled"&&!late.value("known",true)&&!late.contains("localReadBusy"));
    mode_producer_port::liveObservation.wait=Wait::Busy;mode_producer_port::liveObservation.waitMs=266;
    const auto transportBusy=steamui::read(identity,true,{},steamlive::ModeTransportWait::SharedObserver,&observation);
    c.check("shared-transport-busy-is-not-a-producer-lock-miss",transportBusy.value("reason",std::string{})=="live-busy"&&
        !transportBusy.contains("localReadBusy")&&observation.wait==Wait::Busy&&observation.waitMs==266&&!observation.rendererBusy);
    mode_producer_port::liveObservation={};
    mode_producer_port::liveResult={{"ok",true},{"mode","bigpicture"},{"modeEpoch","new-document:1"}};
    const auto verified=steamui::read(identity,true,{},steamlive::ModeTransportWait::SharedObserver,&observation);
    c.check("shared-verified-read-keeps-current-mode-and-document",verified.value("known",false)&&verified.value("mode",std::string{})=="bigpicture"&&
        verified.value("modeEpoch",std::string{})=="new-document:1"&&observation.wait==Wait::None);
    c.check("verified-and-busy-reads-use-the-owned-read-only-port",mode_producer_port::sharedObserverCalls==2&&mode_producer_port::invalidLiveCalls==0);
    c.check("producer-thread-gates-returned",!hold.timedOut&&!cancelGate.timedOut);c.metadata["liveLeafCalls"]=mode_producer_port::liveCalls.load();
}
}
