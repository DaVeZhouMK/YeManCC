#include <atomic>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "steam_native_record_identity.h"
using json=nlohmann::json;
using ULONGLONG=unsigned long long;
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<bool> g_sleepRecoveryAutoEnabled{true},g_exitRequested{false};
static std::atomic<unsigned long long> g_sleepRecoveryAutoConfigEpoch{1};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long powerGeneration=1,clockTick=10000,clockFileTime=10000000;
static unsigned long long currentPowerGeneration(){return powerGeneration;}
static ULONGLONG GetTickCount64(){return clockTick;}
static ULONGLONG sgNowFileTime(){return clockFileTime;}
static bool sleepSteamRecoveryAllows(unsigned long long g,unsigned long long e){return g&&e&&g==powerGeneration&&e==g_sleepRecoveryAutoConfigEpoch&&g_sleepRecoveryAutoEnabled&&!g_exitRequested;}
struct Game {unsigned pid=0;ULONGLONG created=0;std::wstring path;};
struct Session {unsigned pid=20044,account=7;ULONGLONG created=42;std::wstring root=L"Steam";};
struct NativeDetectedGame {unsigned pid=0;ULONGLONG processCreated=0;std::wstring path;};
struct NativeRecordBinding {bool bound=false;std::string reason="unbound";std::uint64_t account64=0,gameId64=0;std::string metadata;};
struct Captured;
static void bindNativeRecordIdentity(Captured&);
static Session currentClient;
static Game nextGame{34764,134367168921759042ULL,L"original.exe"};
static unsigned selections=0,binds=0,sessionReads=0,queued=0;
static ymcc::steamrecordidentity::PrefetchWitness witness;
static Session prepareSession(){++sessionReads;return currentClient;}
static bool nativeValveReadCurrentTarget(NativeDetectedGame* t){*t={nextGame.pid,nextGame.created,nextGame.path};return true;}
struct Selection {Game game;json evidence;};
static Selection selectPreSleepSteamTarget(const Session&,ULONGLONG,const Game& g){++selections;return {g,{{"jointBindingVerified",true},{"selectedAppid",4131894616ULL}}};}
static std::string W2U(const std::wstring& v){return {v.begin(),v.end()};}
static json clientIdentity(const Session& s){return {{"pid",s.pid},{"accountKnown",s.account!=0}};}
static std::vector<json> journal;
static void sgRecordFact(const char* e,const json& j){journal.push_back({{"event",e},{"data",j}});}
static json inspect(unsigned long long){return json::object();}
static bool sameGame(const Game&){return true;}
static bool sameSession(const Session&){return true;}
template<class F>static bool poolSubmit(F&&){++queued;return false;}
// This file supplies bounded external dependencies only. All capture control
// flow and Captured storage below are extracted verbatim from the real header.
#include "steam_duplicate_suspend_capture_functions.inc"
static void bindNativeRecordIdentity(Captured& v){
 ++binds;
 if(ymcc::steamrecordidentity::beforeCapture(witness,v.boundaryGeneration,v.configEpoch,v.at,v.powerBoundaryFileTime)){
  v.nativeRecord={true,"original-bound",111,17746352246272032768ULL,"metadata-original"};
  v.inputBefore={{"source","first-native-before"}};
 }else v.nativeRecord.reason="native-record-pre-capture-identity-not-fresh";
}
static int cases=0,assertions=0,failed=0;
static std::vector<std::string> failures;
static void check(bool b,const char* message){++assertions;if(!b){++failed;failures.emplace_back(message);}}
static void reset(){
 g_sleepRecoveryAutoEnabled=true;g_exitRequested=false;g_sleepRecoveryAutoConfigEpoch=1;powerGeneration=1;g_powerLifecycle=PowerLifecycle::Ready;
 captured={};prepared={};preparedPending=false;cancelPreparedSuspend();
 currentClient={};nextGame={34764,134367168921759042ULL,L"original.exe"};clockTick=10000;clockFileTime=10000000;
 witness={1,1,9990,9990000};selections=binds=sessionReads=queued=0;journal.clear();
}
static void first(){prepareSuspend();powerGeneration=2;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(2);g_powerLifecycle=PowerLifecycle::Suspended;}
static void duplicate(unsigned long long g){clockTick+=5008;clockFileTime+=50080000;prepareSuspend();powerGeneration=g;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(g);g_powerLifecycle=PowerLifecycle::Suspended;}
#include "steam_duplicate_suspend_capture_resume.inc"
static bool retainedFact(){for(const auto& e:journal)if(e["event"]=="sleep-steam-original-captured"&&e["data"].value("originalRetainedForRepeatedSuspend",false))return true;return false;}
int main(){
 ++cases;reset();first();
 check(captured.generation==2&&captured.boundaryGeneration==1&&captured.nativeRecord.bound,"first boundary must bind before generation advance");
 check(captured.game.pid==34764&&binds==1&&selections==1,"first capture uses one exact selection and bind");
 const auto firstTick=captured.at,firstTime=captured.powerBoundaryFileTime;const auto firstInput=captured.inputBefore;
 duplicate(3);
 check(captured.generation==3&&powerGeneration==3,"duplicate keeps shared generation authoritative");
 check(captured.boundaryGeneration==1&&captured.at==firstTick&&captured.powerBoundaryFileTime==firstTime,"duplicate keeps original boundary and times");
 check(captured.nativeRecord.bound&&captured.nativeRecord.account64==111&&captured.nativeRecord.metadata=="metadata-original","duplicate retains original private identity and metadata");
 check(captured.inputBefore==firstInput&&binds==1&&selections==1&&sessionReads==1,"duplicate performs no new bind selection or session read");
 check(retainedFact(),"retention path emits its own truthful receipt");
 ++cases;duplicate(4);check(captured.generation==4&&captured.boundaryGeneration==1&&captured.nativeRecord.bound&&binds==1,"repeated duplicates retain the first original, never refresh its proof");
 ++cases;reset();first();nextGame={99999,77,L"later.exe"};currentClient.account=9;duplicate(3);
 check(captured.game.pid==34764&&captured.client.account==7&&captured.nativeRecord.account64==111,"changed target/account cannot replace the suspend original");
 ++cases;reset();witness.generation=99;first();const auto reason=captured.nativeRecord.reason;duplicate(3);
 check(!captured.nativeRecord.bound&&captured.nativeRecord.reason==reason&&binds==1,"unbound original stays unbound across duplicate notifications");
 ++cases;reset();first();prepareSuspend();g_sleepRecoveryAutoEnabled=false;g_sleepRecoveryAutoConfigEpoch=2;powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(captured.generation==2&&binds==1,"explicit Off cancels the handoff");
 ++cases;reset();first();prepareSuspend();g_sleepRecoveryAutoConfigEpoch=2;powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(!captured.nativeRecord.bound&&captured.configEpoch==2&&captured.boundaryGeneration==3&&binds==2,"epoch change cannot retain prior proof");
 ++cases;reset();first();prepareSuspend();cancelPreparedSuspend();powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(!captured.nativeRecord.bound&&captured.boundaryGeneration==3&&binds==2,"cancel discards duplicate handoff");
 ++cases;reset();first();prepareSuspend();g_powerLifecycle=PowerLifecycle::Resuming;const auto wake=wakeBoundaryFor(2);powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(wake.nativeRecord.bound&&!captured.nativeRecord.bound&&captured.boundaryGeneration==3,"resume closes even a coalesced duplicate that never reached capture");
 ++cases;reset();first();prepareSuspend();g_powerLifecycle=PowerLifecycle::Ready;nextGame={22222,333,L"next-episode.exe"};currentClient.account=12;witness={2,1,clockTick-1,clockFileTime-1};prepareSuspend();powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(captured.game.pid==22222&&captured.client.account==12&&captured.boundaryGeneration==2&&captured.nativeRecord.bound&&selections==2,"next Ready episode performs its own fresh original capture");
 ++cases;reset();first();g_powerLifecycle=PowerLifecycle::Resuming;prepareSuspend();powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(!captured.nativeRecord.bound&&captured.boundaryGeneration==3&&binds==2,"Resuming preparation cannot classify a new suspend as the old episode");
 ++cases;reset();first();prepareSuspend();powerGeneration=4;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(4);
 check(!captured.nativeRecord.bound&&captured.boundaryGeneration==4&&binds==2,"a generation gap never relays an old original");
 ++cases;reset();first();prepareSuspend();powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;captureSuspend(3);
 check(!captured.nativeRecord.bound&&captured.boundaryGeneration==3,"Ready capture cannot consume a suspended-only handoff");
 ++cases;reset();first();const auto n=journal.size();captureSuspend(2);
 check(captured.nativeRecord.bound&&binds==1&&journal.size()==n,"same-generation duplicate worker is idempotent");
 ++cases;reset();first();prepareSuspend();g_exitRequested=true;powerGeneration=3;captureSuspend(3);
 check(captured.generation==2&&binds==1,"exit blocks capture work");
 ++cases;reset();powerGeneration=ULLONG_MAX;captured.generation=ULLONG_MAX;captured.configEpoch=1;g_powerLifecycle=PowerLifecycle::Suspended;prepareSuspend();powerGeneration=0;captureSuspend(0);
 check(captured.generation==ULLONG_MAX,"generation overflow cannot carry to zero");
 for(unsigned event:{1U,2U,3U}){
  ++cases;reset();first();prepareSuspend();g_powerLifecycle=PowerLifecycle::Resuming;
  dispatchProductionResumeNotice(event);publishProductionReady();
  clockTick+=70000;clockFileTime+=700000000;nextGame={99999,77,L"next-episode.exe"};currentClient.account=9;
  powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
  check(captured.game.pid==99999&&captured.client.account==9&&captured.boundaryGeneration==3&&!captured.nativeRecord.bound&&selections==2,"real resume notice closes marker before a direct next-episode capture");
 }
 ++cases;reset();first();prepareSuspend();publishProductionReady();clockTick+=70000;nextGame={99999,77,L"ready-only.exe"};currentClient.account=9;
 powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(captured.game.pid==99999&&captured.boundaryGeneration==3&&!captured.nativeRecord.bound&&selections==2,"every real Ready publisher closes stale marker without wakeBoundaryFor or prepare");
 ++cases;reset();first();prepareSuspend();sleepRecoveryAutoArm(2);g_powerLifecycle=PowerLifecycle::Ready;nextGame={99999,77,L"arm-only.exe"};
 powerGeneration=3;g_powerLifecycle=PowerLifecycle::Suspending;captureSuspend(3);
 check(captured.game.pid==99999&&captured.boundaryGeneration==3&&!captured.nativeRecord.bound&&selections==2,"real recovery arm wrapper closes stale marker");
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failed",failed},{"failures",failures},{"actualProductionControlFlow",true},{"externalDependenciesShimmed",true},{"runtimeRecoveryProven",false}}).dump()<<'\n';
 return failed?1:0;
}
