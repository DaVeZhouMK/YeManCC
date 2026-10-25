// Offline dependency shim; production baseline/current-record functions included verbatim.
// No windows.h, process handles, memory access, network, deployment, or copied helper.
#include "../native/steam_native_record_identity.h"
#include "../native/steam_shortcut_metadata_identity.h"
#include "../native/sleep_steam_tracking.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>
using json=nlohmann::json;
using DWORD=std::uint32_t;
using ULONGLONG=unsigned long long;
namespace offline {
constexpr std::uint64_t account64=76561198012345678ULL;
constexpr DWORD appid=4131894616U;
constexpr DWORD account32=static_cast<DWORD>(account64&0xffffffffULL);
constexpr std::uint64_t gameId=(std::uint64_t(appid)<<32)|0x02000000ULL;
constexpr std::uint64_t fileTimeBase=133000000000000000ULL;
struct World {
 std::uint64_t tick=100000,generation=17;std::int64_t fileTimeAdjustment=0;DWORD windowsSession=2;
 bool gameAlive=true,sessionAvailable=true,nativeAvailable=true,primaryMatched=true,metadataAvailable=true;
 bool nativeThrows=false,metadataThrows=false,rpcThrows=false,malformedCounts=false,zeroCounts=false;
 unsigned nativeCalls=0,metadataCalls=0,rpcCalls=0,postRpcActiveCalls=0,hashCalls=0;
 bool rpcReturned=false,rpcAdmitBefore=false,rpcAdmitAfter=false,nativeAdmit=false,metadataAdmit=false;
 json reply,request;
 std::function<void()> nativeHook,metadataHook,rpcHook;
 std::function<void(unsigned)> postRpcActiveHook;
 ymcc::shortcutmetadata::Witness producedWitness;
} world;
}
static std::uint64_t GetTickCount64(){return offline::world.tick++;}
static std::uint64_t sgNowFileTime(){return offline::fileTimeBase+offline::world.tick*10000+offline::world.fileTimeAdjustment;}
static bool ProcessIdToSessionId(DWORD,DWORD* out){*out=offline::world.windowsSession;return offline::world.sessionAvailable;}
static std::string W2U(const std::wstring& s){std::string out;for(const auto c:s){if(c>127)throw std::runtime_error("non-ASCII fixture path");out.push_back(static_cast<char>(c));}return out;}
static std::string sha256File(const std::wstring&){++offline::world.hashCalls;return "offline-unused";}
enum class PowerLifecycle{Ready,Suspending};
static std::atomic<bool> g_sleepRecoveryAutoEnabled{true},g_exitRequested{false};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<std::uint64_t> g_sleepRecoveryAutoConfigEpoch{21},g_gameRulesEpoch{31};
static std::uint64_t currentPowerGeneration(){return offline::world.generation;}
// Dependency-only shapes from sleep_steam_recovery.h / Win32 provider wrappers.
// Actual CurrentRecordPre/Read, proof, owner, read and commit are NOT redefined.
namespace SleepSteamRecovery {
using namespace ymcc::sleepsteam;
struct Game{DWORD pid=0;ULONGLONG created=0;std::wstring path;};
struct Session{
 DWORD account=0,pid=0,registeredPid=0;ULONGLONG created=0;std::wstring root;
 std::string source="unverified",reason="steam-process-unverified",accountSource="unverified";
 bool accountPinned=false,liveAccountPinned=false,foreignRegistryAccountIgnored=false;
};
static Session currentSession;static Game actualGame;
static bool validSession(const Session& s){return s.account&&s.pid&&s.created&&!s.root.empty();}
static bool sameClientProcess(const Session& a,const Session& b){return a.pid&&a.created&&!a.root.empty()&&a.pid==b.pid&&a.created==b.created&&pathKey(W2U(a.root))==pathKey(W2U(b.root));}
static Session session(){return currentSession;}
static bool sameGame(const Game& g){return offline::world.gameAlive&&g.pid&&g.created&&!g.path.empty()&&g.pid==actualGame.pid&&g.created==actualGame.created&&pathKey(W2U(g.path))==pathKey(W2U(actualGame.path));}
static json liveRequest(const Session&,json request,const std::function<bool()>& admit,ymcc::steamrecordidentity::Read* identity){
 auto& w=offline::world;++w.rpcCalls;w.request=std::move(request);w.rpcAdmitBefore=admit&&admit();w.tick+=7;
 if(w.rpcHook)w.rpcHook();w.rpcAdmitAfter=admit&&admit();
 if(w.rpcThrows)throw std::runtime_error("offline-rpc-exception");
 auto reply=w.reply;*identity=ymcc::steamrecordidentity::consume(reply);w.rpcReturned=true;
 return reply; // A stale success deliberately reaches the caller's owner gate.
}
}
namespace ymcc::steamrecordwin {
struct Context{
 DWORD steamPid=0,gamePid=0,windowsSession=0;std::uint64_t steamBirth=0,gameBirth=0,account64=0,gameId64=0;
 std::wstring steamRoot,gamePath;bool sessionBound=false;
};
using HashFile=std::function<std::string(const std::wstring&)>;
inline json observe(const Context&,const HashFile&,const std::function<bool()>& owned){
 auto& w=offline::world;++w.nativeCalls;w.nativeAdmit=owned&&owned();w.tick+=3;if(w.nativeHook)w.nativeHook();
 if(w.nativeThrows)throw std::runtime_error("offline-native-exception");
 return {{"available",w.nativeAvailable},{"originalPrimaryMatched",w.primaryMatched},
 {"readCalls",w.malformedCounts?json(-1):json(w.zeroCounts?0:3)},{"readBytes",w.malformedCounts?json("unknown"):json(w.zeroCounts?0:24)},{"source","offline-native-provider"}};
}
}
namespace ymcc::shortcutmetadatawin {
using Outcome=ymcc::shortcutmetadata::Outcome;using Phase=ymcc::shortcutmetadata::Phase;using Witness=ymcc::shortcutmetadata::Witness;
struct Request{ymcc::steamrecordwin::Context original;std::uint32_t appid=0;std::uint64_t generation=0,epoch=0;Phase phase=Phase::FollowOriginal;Witness witness;bool beforeCaptureAdmitted=false;};
inline Outcome unavailable(const char* reason){return {{{"available",false},{"reason",reason}},{}};}
inline Outcome observe(const Request& r,const ymcc::steamrecordwin::HashFile&,const std::function<bool()>& owned){
 auto& w=offline::world;++w.metadataCalls;w.metadataAdmit=owned&&owned()&&r.beforeCaptureAdmitted&&r.phase==Phase::Baseline;
 w.tick+=4;if(w.metadataHook)w.metadataHook();if(w.metadataThrows)throw std::runtime_error("offline-metadata-exception");
 Witness witness;const auto& c=r.original;
 witness.scope={c.steamPid,c.gamePid,c.windowsSession,c.steamBirth,c.gameBirth,r.generation,r.epoch,ymcc::sleepsteam::pathKey(W2U(c.steamRoot+L"\\steam.exe")),ymcc::sleepsteam::pathKey(W2U(c.gamePath))};
 witness.account64=c.account64;witness.gameId64=c.gameId64;witness.appid=r.appid;witness.moduleBase=0x100000;witness.moduleSize=0x10000;
 witness.user=0x210000;witness.manager=0x220000;witness.node=0x230000;witness.record=0x240000;witness.valid=w.metadataAvailable;w.producedWitness=witness;
 return {{{"available",w.metadataAvailable},{"loadedByte",1},{"field68Nonzero",true},
 {"readCalls",w.malformedCounts?json(-1):json(w.zeroCounts?0:5)},{"readBytes",w.malformedCounts?json(false):json(w.zeroCounts?0:32)}},witness};
}
}
namespace SleepSteamRecovery {
struct NativeRecordBinding{std::uint64_t account64=0,gameId64=0;std::uint32_t appid=0;DWORD windowsSession=0;bool bound=false;std::string reason="native-record-pre-capture-identity-unavailable";ymcc::shortcutmetadatawin::Witness shortcutMetadata;};
struct CaptureState{std::uint64_t generation=0,configEpoch=0;};
static std::mutex captureMx;static bool preparedPending=false;static CaptureState captured;
struct NativeRecordPrefetch{Session client;Game game;NativeRecordBinding identity;ymcc::steamrecordidentity::PrefetchWitness witness;json nativeBefore;};
static std::mutex nativeRecordPrefetchMx;static NativeRecordPrefetch nativeRecordPrefetch;
}
// Include the real implementations, not extracted function copies.
#include "../native/steam_native_current_probe.h"
#include "../native/steam_shortcut_metadata_baseline.h"
namespace offline {
namespace prod=SleepSteamRecovery;
unsigned cases=0,assertions=0,failures=0;json caseResults=json::array();std::vector<std::string> errors;
void expect(bool pass,const char* why){++assertions;if(!pass){++failures;errors.emplace_back(why);}}
json reply(){return {{"ok",true},{"mutated",false},{"observedAccount",account32},
 {"_ymccPrivateNativeRecord",{{"status","observed"},{"source","steam-ui-exact-native-record-identity"},{"steamid64",std::to_string(account64)},{"gameid64",std::to_string(gameId)},{"appid",appid}}},
 {"overviews",json::array({{{"appid",appid},{"gameidString",std::to_string(gameId)},{"localDisplayStatus",11},{"inRunningList",false}}})}};}
void reset(){
 world={};world.tick=100000+std::uint64_t(cases)*100000;world.reply=reply();
 g_sleepRecoveryAutoEnabled=true;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;g_sleepRecoveryAutoConfigEpoch=21;g_gameRulesEpoch=31;
 prod::preparedPending=false;prod::captured={};prod::nativeRecordPrefetch={};prod::currentSession={};auto& s=prod::currentSession;
 s.account=account32;s.pid=20044;s.created=700100;s.root=L"G:\\OfflineFixture\\Steam";s.accountPinned=true;s.liveAccountPinned=true;
 s.accountSource="running-link-independent-steam-ui-read";s.source="verified-session-observer";
 prod::actualGame={52460,700200,L"G:\\OfflineFixture\\Game\\fixture.exe"};
}
prod::CurrentRecordPre makePre(std::uint64_t sequence=71){
 auto initial=world.reply;auto identity=ymcc::steamrecordidentity::consume(initial);const auto t=world.tick,ft=sgNowFileTime();
 return prod::currentRecordProof(prod::currentSession,prod::actualGame,appid,identity,initial,world.generation,21,31,41,sequence,world.windowsSession,std::to_string(gameId),t-20,t-10,ft-200000,ft-100000);
}
std::function<bool()> active(){return []{if(world.rpcReturned&&world.postRpcActiveHook)world.postRpcActiveHook(++world.postRpcActiveCalls);return g_sleepRecoveryAutoEnabled.load()&&!g_exitRequested.load();};}
prod::ShortcutBaselineAttemptResult run(const prod::CurrentRecordPre& p,std::uint64_t& sequence){world.rpcReturned=false;world.postRpcActiveCalls=0;return prod::boundedShortcutBaseline(p,sequence,active());}
void noCommit(const prod::ShortcutBaselineAttemptResult& r){
 expect(!r.committed,"no diagnostic commit accepted");expect(!prod::nativeRecordPrefetch.identity.bound,"no prefetch publication");
 expect(!prod::nativeRecordPrefetch.identity.shortcutMetadata.valid,"no private witness publication");expect(prod::nativeRecordPrefetch.witness.observedTick==0,"no committed timestamp");expect(r.diagnostic.is_null(),"no published diagnostic");
}
void consumed(const prod::ShortcutBaselineAttemptResult& r,std::uint64_t sequence){
 expect(r.attempted&&r.identityPostflightInvoked,"native and POST attempted");expect(world.rpcCalls==1,"exactly one extra known RPC");expect(sequence==72,"POST advances identity sequence once");
 expect(!ymcc::currentrecord::eligible(r.nextPre.proof),"nextPre ineligible");expect(r.nextPre.proof.account64==0&&r.nextPre.proof.sequence==0&&r.nextPre.client.pid==0&&r.nextPre.game.pid==0,"failed POST leaves empty nextPre");expect(!r.postflightIdentityAccepted,"failed identity not accepted");noCommit(r);
}
void skipped(const prod::ShortcutBaselineAttemptResult& r,std::uint64_t sequence,std::uint64_t initial=71){
 expect(!r.attempted&&!r.identityPostflightInvoked,"guard skips baseline");expect(world.nativeCalls==0&&world.metadataCalls==0&&world.rpcCalls==0,"no provider or RPC calls");expect(sequence==initial,"sequence unchanged");expect(!r.postflightIdentityAccepted,"no fabricated acceptance");
}
void discarded(const json& m){
 expect(m.is_object(),"discarded scalar metadata exists");if(!m.is_object())return;
 const std::vector<std::string> keys={"source","authority","nativeObservationInvoked","nativeDiagnosticCommitAccepted","discarded","readCalls","readBytes","countsKnown","shortcutMetadataObservationInvoked","shortcutMetadataReadCalls","shortcutMetadataReadBytes","countsScope","zeroIsProofOfNoRead","readStartedTick","readFinishedTick","readStartedFileTime","readFinishedFileTime"};
 expect(m.size()==keys.size(),"exact production scalar allowlist");for(const auto& k:keys)expect(m.contains(k),"required scalar key");
 expect(m.value("discarded",false)&&!m.value("nativeDiagnosticCommitAccepted",true),"discard explicit");expect(!m.value("zeroIsProofOfNoRead",true),"zero grants no absence-of-read proof");
 expect(m.value("countsScope",std::string())=="provider-reported-not-whole-call-accounting","count scope retained");
 const auto text=m.dump();expect(text.find(std::to_string(account64))==std::string::npos&&text.find("OfflineFixture")==std::string::npos,"no full identity/path export");
 for(const auto* k:{"loadedByte","field68Nonzero","shortcutMetadataDiagnostic","scope","witness","record","manager"})expect(!m.contains(k),"no native field or private witness export");
}
void success(const prod::ShortcutBaselineAttemptResult& r,const prod::CurrentRecordPre& p,std::uint64_t sequence){
 expect(r.attempted&&r.identityPostflightInvoked&&r.committed&&r.postflightIdentityAccepted,"success completes real adapter");
 expect(!r.postflightAccountChanged&&!r.postflightSessionChanged&&!r.postflightSameOwnerMismatch,"no mismatch classification");
 expect(world.nativeCalls==1&&world.metadataCalls==1&&world.rpcCalls==1,"one native provider, metadata provider and extra RPC");
 expect(sequence==p.proof.sequence+1&&r.nextPre.proof.sequence==sequence,"only identity sequence advances once");
 expect(ymcc::currentrecord::eligible(r.nextPre.proof)&&ymcc::currentrecord::sameOwner(p.proof,r.nextPre.proof),"real POST proof retains original owner");
 expect(world.rpcAdmitBefore&&world.rpcAdmitAfter&&world.nativeAdmit&&world.metadataAdmit,"owner callbacks passed to seams");
 expect(world.request==json{{"operation","sleep.session.inspect"},{"appids",json::array({p.proof.appid})},{"runningLinkRead",true}},"existing read-only RPC request only");
 const auto& c=prod::nativeRecordPrefetch;const auto& v=c.identity.shortcutMetadata;const auto& w=c.witness;
 expect(c.identity.bound&&v.valid,"real commit publishes bound private witness");
 expect(v.scope==world.producedWitness.scope&&v.record==world.producedWitness.record&&v.manager==world.producedWitness.manager&&v.user==world.producedWitness.user&&v.node==world.producedWitness.node,"provider witness copied without reconstruction");
 expect(v.account64==p.proof.account64&&v.gameId64==p.proof.gameId64&&v.appid==p.proof.appid,"full original witness identity retained");
 expect(w.generation==p.proof.generation&&w.epoch==p.proof.epoch,"original generation and epoch retained");
 expect(w.observedTick>=r.nextPre.proof.uiFinishedTick&&w.observedTick>p.proof.uiFinishedTick,"witness stamped after POST, not PRE");
 expect(w.observedFileTime>=r.nextPre.proof.uiFinishedFileTime,"witness filetime follows POST");
 expect(w.observedTick-p.proof.uiStartedTick<=ymcc::currentrecord::maximumBracketMs,"existing 5s bracket unchanged");
 expect(r.diagnostic.value("identityBasis",std::string())=="owned-ui-read-plus-bounded-identity-postflight","actual postflight basis");
 expect(r.diagnostic.value("loginGenerationLease",std::string())=="unknown"&&!r.diagnostic.value("uiAndNativeAtomic",true),"no lease or atomicity claim");
 expect(r.diagnostic.at("uiOverview").at("localDisplayStatus")==11&&!r.diagnostic.at("uiOverview").at("inRunningList").get<bool>(),"non-running overview not promoted");
 expect(r.readMetadata.value("nativeDiagnosticCommitAccepted",false),"real accepted metadata");
 expect(ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch,w.observedTick,w.observedFileTime),"old capture boundary accepts equality");
 expect(!ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch,w.observedTick-1,w.observedFileTime),"capture before commit rejects later witness");
 expect(!ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch,w.observedTick,w.observedFileTime-1),"filetime fence rejects later witness");
 expect(ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch,w.observedTick+60000,w.observedFileTime+600000000),"old 60s prefetch age includes equality");
 expect(!ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch,w.observedTick+60001,w.observedFileTime+600010000),"old prefetch age not extended");
 expect(!ymcc::steamrecordidentity::beforeCapture(w,w.generation+1,w.epoch,w.observedTick,w.observedFileTime),"cannot adopt witness into later generation");
 expect(!ymcc::steamrecordidentity::beforeCapture(w,w.generation,w.epoch+1,w.observedTick,w.observedFileTime),"cannot adopt witness into later epoch");
}
template<class F>void test(const char* name,F&& body){
 ++cases;const auto before=assertions,failedBefore=failures;errors.clear();reset();
 try{body();}catch(const std::exception& e){expect(false,e.what());}catch(...){expect(false,"unexpected exception");}
 caseResults.push_back({{"name",name},{"assertions",assertions-before},{"failed",failures-failedBefore},{"errors",errors},{"nativeProviderCalls",world.nativeCalls},{"metadataProviderCalls",world.metadataCalls},{"rpcCalls",world.rpcCalls}});
}
void populateFresh(const prod::CurrentRecordPre& p){
 auto& c=prod::nativeRecordPrefetch;c.client=p.client;c.game=p.game;c.identity.account64=p.proof.account64;c.identity.gameId64=p.proof.gameId64;c.identity.appid=p.proof.appid;
 c.identity.windowsSession=p.proof.windowsSession;c.identity.bound=true;c.identity.shortcutMetadata.valid=true;c.witness={p.proof.generation,p.proof.epoch,world.tick-1,sgNowFileTime()-10000};
}
void guardCases(){
 const std::vector<std::pair<const char*,std::function<void(prod::CurrentRecordPre&)>>> variants={
 {"prepared-before-attempt",[](auto&){prod::preparedPending=true;}},
 {"captured-unbound-same-generation-other-epoch",[](auto& p){prod::captured={p.proof.generation,p.proof.epoch+1};}},
 {"off-before-attempt",[](auto&){g_sleepRecoveryAutoEnabled=false;}},
 {"config-epoch-before-attempt",[](auto&){++g_sleepRecoveryAutoConfigEpoch;}},
 {"rules-epoch-before-attempt",[](auto&){++g_gameRulesEpoch;}},
 {"power-generation-before-attempt",[](auto&){++world.generation;}},
 {"exit-requested-before-attempt",[](auto&){g_exitRequested=true;}},
 {"power-suspending-before-attempt",[](auto&){g_powerLifecycle=PowerLifecycle::Suspending;}},
 {"game-exited-before-attempt",[](auto&){world.gameAlive=false;}},
 {"game-pid-reuse",[](auto&){++prod::actualGame.created;}},
 {"game-path-changed",[](auto&){prod::actualGame.path+=L".other";}},
 {"steam-pid-reuse",[](auto&){++prod::currentSession.created;}},
 {"steam-root-changed",[](auto&){prod::currentSession.root+=L"\\other";}},
 {"windows-session-changed",[](auto&){++world.windowsSession;}},
 {"process-session-query-failure",[](auto&){world.sessionAvailable=false;}},
 {"unpinned-client",[](auto& p){p.client.accountPinned=false;}},
 {"untrusted-account-source",[](auto& p){p.client.accountSource="registry";}},
 {"pre-older-than-5s",[](auto&){world.tick+=5001;}},
 {"standard-app-not-shortcut",[](auto& p){p.proof.appid=570;p.proof.gameId64=570;}},
 {"shortcut-type-lowbits-mismatch",[](auto& p){++p.proof.gameId64;}},
 {"shortcut-appid-highbits-mismatch",[](auto& p){p.proof.gameId64^=1ULL<<32;}},
 {"shortcut-appid-below-old-bound",[](auto& p){p.proof.appid=0x80001387U;p.proof.gameId64=(std::uint64_t(p.proof.appid)<<32)|0x02000000;}}
 };
 for(const auto& v:variants)test(v.first,[&]{auto p=makePre();expect(ymcc::currentrecord::eligible(p.proof),"fixture built by real proof");v.second(p);auto seq=p.proof.sequence;const auto initial=seq;const auto r=run(p,seq);skipped(r,seq,initial);noCommit(r);});
 test("empty-active-callback",[]{const auto p=makePre();auto seq=p.proof.sequence;const auto r=prod::boundedShortcutBaseline(p,seq,{});skipped(r,seq);noCommit(r);});
 test("inactive-callback",[]{const auto p=makePre();auto seq=p.proof.sequence;const auto r=prod::boundedShortcutBaseline(p,seq,[]{return false;});skipped(r,seq);noCommit(r);});
 test("identity-sequence-mismatch",[]{const auto p=makePre();auto seq=p.proof.sequence+1;const auto r=run(p,seq);skipped(r,seq,p.proof.sequence+1);noCommit(r);});
 test("identity-sequence-overflow",[]{const auto p=makePre(std::numeric_limits<std::uint64_t>::max());auto seq=p.proof.sequence;const auto r=run(p,seq);skipped(r,seq,p.proof.sequence);noCommit(r);});
 test("fresh-cache-no-execution",[]{const auto p=makePre();populateFresh(p);auto seq=p.proof.sequence;const auto tick=prod::nativeRecordPrefetch.witness.observedTick;const auto r=run(p,seq);skipped(r,seq);expect(prod::nativeRecordPrefetch.witness.observedTick==tick&&prod::nativeRecordPrefetch.identity.bound,"cache remains intact");});
 test("old-capture-generation-does-not-block",[]{const auto p=makePre();prod::captured.generation=p.proof.generation-1;auto seq=p.proof.sequence;const auto r=run(p,seq);success(r,p,seq);});
 test("precise-unbound-global-account-fallback",[]{const auto p=makePre();prod::currentSession.account=0;prod::currentSession.foreignRegistryAccountIgnored=true;prod::currentSession.reason="steam-account-not-ready";auto seq=p.proof.sequence;const auto r=run(p,seq);success(r,p,seq);});
 test("ambiguous-unbound-global-account-refused",[]{const auto p=makePre();prod::currentSession.account=0;auto seq=p.proof.sequence;const auto r=run(p,seq);skipped(r,seq);noCommit(r);});
}
void postCases(){
 const std::vector<std::pair<const char*,std::function<void(json&)>>> variants={
 {"post-full-account64-changed-same-low32",[](auto& r){r["_ymccPrivateNativeRecord"]["steamid64"]=std::to_string(account64+(1ULL<<32));}},
 {"post-low32-account-changed",[](auto& r){r["observedAccount"]=account32+1;}},
 {"post-account-changed-reason",[](auto& r){r["reason"]="steam-account-changed";}},
 {"post-session-changed-reason",[](auto& r){r["reason"]="steam-session-changed";}},
 {"post-ok-false",[](auto& r){r["ok"]=false;}},{"post-ok-absent",[](auto& r){r.erase("ok");}},
 {"post-mutated-true",[](auto& r){r["mutated"]=true;}},{"post-mutated-absent",[](auto& r){r.erase("mutated");}},
 {"post-mutated-nonboolean",[](auto& r){r["mutated"]=0;}},
 {"post-observed-account-missing",[](auto& r){r.erase("observedAccount");}},{"post-observed-account-zero",[](auto& r){r["observedAccount"]=0;}},
 {"post-private-identity-missing",[](auto& r){r.erase("_ymccPrivateNativeRecord");}},
 {"post-private-source-untrusted",[](auto& r){r["_ymccPrivateNativeRecord"]["source"]="untrusted";}},
 {"post-private-raced",[](auto& r){r["_ymccPrivateNativeRecord"]["status"]="unavailable";r["_ymccPrivateNativeRecord"]["reason"]="native-record-ui-identity-raced";}},
 {"post-wrong-appid",[](auto& r){r["_ymccPrivateNativeRecord"]["appid"]=appid+1;}},
 {"post-wrong-full-gameid",[](auto& r){r["_ymccPrivateNativeRecord"]["gameid64"]=std::to_string(gameId+(1ULL<<32));}},
 {"post-full-account-zero",[](auto& r){r["_ymccPrivateNativeRecord"]["steamid64"]="0";}},
 {"post-full-account-overflow",[](auto& r){r["_ymccPrivateNativeRecord"]["steamid64"]="18446744073709551616";}}
 };
 for(const auto& v:variants)test(v.first,[&]{const auto p=makePre();auto seq=p.proof.sequence;v.second(world.reply);const auto r=run(p,seq);consumed(r,seq);discarded(r.readMetadata);
 const std::string name=v.first;
 if(name=="post-full-account64-changed-same-low32"){expect(DWORD((account64+(1ULL<<32))&0xffffffffULL)==account32,"fixture low32 identical");expect(r.postflightAccountChanged,"full account change classified");}
 if(name=="post-low32-account-changed"||name=="post-account-changed-reason")expect(r.postflightAccountChanged,"account change classified");
 if(name=="post-session-changed-reason")expect(r.postflightSessionChanged,"session change classified");});
 test("post-rpc-throws",[]{const auto p=makePre();auto seq=p.proof.sequence;world.rpcThrows=true;const auto r=run(p,seq);consumed(r,seq);discarded(r.readMetadata);});
 test("post-null-reply",[]{const auto p=makePre();auto seq=p.proof.sequence;world.reply=nullptr;const auto r=run(p,seq);consumed(r,seq);});
}
void callbackCases(){
 const std::vector<std::pair<const char*,std::function<void()>>> changes={
 {"off",[]{g_sleepRecoveryAutoEnabled=false;}},{"config-epoch",[]{++g_sleepRecoveryAutoConfigEpoch;}},
 {"game-exit",[]{world.gameAlive=false;}},{"rules-epoch",[]{++g_gameRulesEpoch;}},
 {"power-generation",[]{++world.generation;}},{"steam-birth",[]{++prod::currentSession.created;}},
 {"windows-session",[]{++world.windowsSession;}},{"prepared",[]{prod::preparedPending=true;}},
 {"captured",[]{prod::captured.generation=world.generation;}},{"exit-requested",[]{g_exitRequested=true;}}
 };
 for(const auto& c:changes)for(const auto* stage:{"native","metadata","rpc","commit-entry","commit-final-lock"}){
 // Real prepared/captured writers hold captureMx. They cannot modify that state
 // while this thread already holds the final commit lock. Do not invent such
 // an interleaving by bypassing the writer's mutex from the active callback.
 if(std::string(stage)=="commit-final-lock"&&(std::string(c.first)=="prepared"||std::string(c.first)=="captured"))continue;
 const auto name=std::string("callback-")+stage+"-"+c.first;
 test(name.c_str(),[&]{const auto p=makePre();auto seq=p.proof.sequence;const std::string where=stage;
 if(where=="native")world.nativeHook=c.second;else if(where=="metadata")world.metadataHook=c.second;else if(where=="rpc")world.rpcHook=c.second;
 else{const unsigned target=where=="commit-entry"?2:3;world.postRpcActiveHook=[&,target](unsigned n){if(n==target)c.second();};}
 const auto r=run(p,seq);noCommit(r);expect(r.attempted&&world.nativeCalls==1,"mutation follows actual native attempt");
 if(where=="native"||where=="metadata"){expect(world.rpcCalls==0&&seq==p.proof.sequence,"ownership loss blocks extra RPC");discarded(r.readMetadata);}
 else if(where=="rpc"){consumed(r,seq);expect(!world.rpcAdmitAfter,"RPC admission observes ownership loss");discarded(r.readMetadata);}
 else{expect(world.rpcCalls==1&&seq==p.proof.sequence+1,"one settled POST before commit race");expect(world.postRpcActiveCalls>=(where=="commit-entry"?2U:3U),"injection reaches real commit boundary");expect(r.postflightIdentityAccepted,"settled identity distinct from later commit refusal");discarded(r.readMetadata);}
 });}
 // Explicit changes must still be classified when owned() simultaneously fails.
 for(const auto* kind:{"account","session"})test((std::string("post-classification-before-owned-")+kind).c_str(),[&]{
 const auto p=makePre();auto seq=p.proof.sequence;
 world.reply["reason"]=std::string(kind)=="account"?"steam-account-changed":"steam-session-changed";
 world.rpcHook=[]{g_sleepRecoveryAutoEnabled=false;};const auto r=run(p,seq);consumed(r,seq);
 expect(std::string(kind)=="account"?r.postflightAccountChanged:r.postflightSessionChanged,"explicit reply classification retained after Off");});
}
void providerAndBoundaryCases(){
 test("native-unavailable",[]{const auto p=makePre();auto seq=p.proof.sequence;world.nativeAvailable=false;const auto r=run(p,seq);noCommit(r);expect(world.nativeCalls==1&&world.metadataCalls==0&&world.rpcCalls==0,"native failure stops downstream");expect(seq==71,"sequence unchanged");discarded(r.readMetadata);});
 test("original-primary-unmatched",[]{const auto p=makePre();auto seq=p.proof.sequence;world.primaryMatched=false;const auto r=run(p,seq);noCommit(r);expect(world.metadataCalls==0&&world.rpcCalls==0,"primary mismatch skips metadata and POST");discarded(r.readMetadata);});
 test("metadata-unavailable",[]{const auto p=makePre();auto seq=p.proof.sequence;world.metadataAvailable=false;const auto r=run(p,seq);noCommit(r);expect(world.metadataCalls==1&&world.rpcCalls==0,"metadata failure skips POST");discarded(r.readMetadata);});
 test("native-provider-throws",[]{const auto p=makePre();auto seq=p.proof.sequence;world.nativeThrows=true;const auto r=run(p,seq);noCommit(r);expect(world.rpcCalls==0&&r.nextPre.proof.sequence==0,"native exception clears PRE");});
 test("metadata-provider-throws",[]{const auto p=makePre();auto seq=p.proof.sequence;world.metadataThrows=true;const auto r=run(p,seq);noCommit(r);expect(world.rpcCalls==0&&r.nextPre.proof.sequence==0,"metadata exception clears PRE");});
 test("provider-delay-cannot-extend-5s",[]{const auto p=makePre();auto seq=p.proof.sequence;world.metadataHook=[]{world.tick+=5001;};const auto r=run(p,seq);noCommit(r);expect(world.rpcCalls==0&&seq==71,"expired PRE blocks POST");});
 test("rpc-delay-cannot-extend-5s",[]{const auto p=makePre();auto seq=p.proof.sequence;world.rpcHook=[]{world.tick+=5001;};const auto r=run(p,seq);consumed(r,seq);});
 test("commit-delay-cannot-extend-5s",[]{const auto p=makePre();auto seq=p.proof.sequence;world.postRpcActiveHook=[](unsigned n){if(n==2)world.tick+=5001;};const auto r=run(p,seq);noCommit(r);expect(r.postflightIdentityAccepted&&seq==72,"POST settled but original bracket expired");});
 test("filetime-regression",[]{const auto p=makePre();auto seq=p.proof.sequence;world.rpcHook=[]{world.fileTimeAdjustment=-10000000;};const auto r=run(p,seq);consumed(r,seq);});
 test("malformed-counts-null-on-discard",[]{const auto p=makePre();auto seq=p.proof.sequence;world.malformedCounts=true;world.reply["ok"]=false;const auto r=run(p,seq);consumed(r,seq);discarded(r.readMetadata);expect(r.readMetadata["readCalls"].is_null()&&r.readMetadata["readBytes"].is_null()&&r.readMetadata["shortcutMetadataReadCalls"].is_null()&&r.readMetadata["shortcutMetadataReadBytes"].is_null(),"invalid counts remain unknown");});
 test("zero-counts-not-no-read-proof",[]{const auto p=makePre();auto seq=p.proof.sequence;world.zeroCounts=true;world.reply["ok"]=false;const auto r=run(p,seq);consumed(r,seq);discarded(r.readMetadata);expect(world.nativeCalls==1&&world.metadataCalls==1&&r.readMetadata["readCalls"]==0&&r.readMetadata["shortcutMetadataReadCalls"]==0,"actual invocations even with zero reported counts");});
 test("immediate-retry-throttled-after-failure",[]{const auto p=makePre();auto seq=p.proof.sequence;world.reply["ok"]=false;const auto first=run(p,seq);consumed(first,seq);world.reply=reply();world.nativeCalls=0;world.metadataCalls=0;world.rpcCalls=0;seq=71;const auto r=run(p,seq);skipped(r,seq);noCommit(r);});
 test("stale-cache-needs-new-baseline",[]{const auto p=makePre();populateFresh(p);prod::nativeRecordPrefetch.witness.observedTick=world.tick-45000;auto seq=p.proof.sequence;const auto r=run(p,seq);success(r,p,seq);});
 test("fresh-cache-other-full-account-not-suppressing",[]{const auto p=makePre();populateFresh(p);prod::nativeRecordPrefetch.identity.account64+=(1ULL<<32);auto seq=p.proof.sequence;const auto r=run(p,seq);success(r,p,seq);});
 test("fresh-cache-no-witness-not-suppressing",[]{const auto p=makePre();populateFresh(p);prod::nativeRecordPrefetch.identity.shortcutMetadata.valid=false;auto seq=p.proof.sequence;const auto r=run(p,seq);success(r,p,seq);});
 test("real-proof-primitive-old-boundaries",[]{
 auto p=makePre().proof;auto post=p;post.sequence=p.sequence+1;const auto t=p.uiStartedTick,ft=p.uiStartedFileTime;
 p.uiFinishedTick=t+1;p.uiFinishedFileTime=ft+10000;post.uiStartedTick=t+4;post.uiFinishedTick=t+5;post.uiStartedFileTime=ft+40000;post.uiFinishedFileTime=ft+50000;
 auto can=[&](std::uint64_t at,bool captured=false){return ymcc::currentrecord::canCommit(p,post,t+2,t+3,ft+20000,ft+30000,at,ft+(at-t)*10000,true,captured);};
 expect(can(t+5000),"real commit accepts exact 5000ms");expect(!can(t+5001),"real commit rejects 5001ms");expect(!can(t+6,true),"real commit rejects captured/prepared");
 post.account64+=(1ULL<<32);expect(ymcc::currentrecord::eligible(post),"different full account independently eligible with same low32");expect(!ymcc::currentrecord::sameOwner(p,post)&&!can(t+6),"real sameOwner rejects full account mismatch");post.account64=p.account64;++post.sequence;expect(!can(t+6),"real commit requires adjacent identity sequence");
 });
}
}
int main(){
 using namespace offline;
 test("normal-success-single-known-extra-rpc",[]{const auto p=makePre();auto seq=p.proof.sequence;expect(ymcc::currentrecord::eligible(p.proof),"fixture uses real currentRecordProof");const auto r=run(p,seq);success(r,p,seq);});
 guardCases();postCases();callbackCases();providerAndBoundaryCases();
 json result={{"schemaVersion",1},{"suite","S25-bounded-shortcut-baseline-production-integration"},{"cases",cases},{"assertions",assertions},{"failed",failures},{"passed",failures==0},
 {"liveProcessAccess",false},{"networkAccess",false},{"deployment",false},
 {"productionFunctions",{"boundedShortcutBaseline","shortcutBaselineFresh","currentRecordProof","currentRecordOwner","currentRecordProcessOwner","currentRecordCapturePresent","readCurrentRecordBeforeExistingUi","commitCurrentRecordAfterExistingUi","currentRecordReadMetadata","ymcc::currentrecord::eligible/sameOwner/canRead/canCommit","ymcc::steamrecordidentity::consume/beforeCapture"}},
 {"mockedDependencies",{"clock","process/session snapshots and queries","native read provider","shortcut metadata provider","liveRequest transport","capture/global owner state","Session/Game/NativeRecordBinding/NativeRecordPrefetch dependency shapes"}},
 {"limitations",{"Deterministic callback interleavings; no real concurrent threads or OS scheduling.","No real Steam process, memory, module hash/profile, login lease, network, sleep, input or Continue verification.","Native and metadata collectors replaced by canned providers; collector correctness is outside this suite.","steam_game_link_runtime.h and the complete application translation unit are not compiled/executed by this shim.","Same-owner mismatch primitive checked directly; exact production proof construction prevents independently forcing that classification branch."}},
 {"caseResults",caseResults}};
 std::cout<<result.dump(2)<<'\n';return failures?1:0;
}
