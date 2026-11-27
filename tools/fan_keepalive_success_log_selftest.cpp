// Exact production keepalive tick and real JSON/receipt code. Inert transport,
// clock/log/power boundaries; no WinHTTP, Host, HC, devices, sleep or benchmark.
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "fan_response_contract.h"
using json=nlohmann::json;
using DWORD=unsigned long;
using ULONGLONG=unsigned long long;
std::atomic<bool> g_fanLeaseKeepaliveArmed{true},g_fanLeaseKeepaliveInFlight{false};
std::atomic<ULONGLONG> g_fanLeaseKeepaliveLastRenewTick{0};
std::atomic<unsigned long long> g_fanKeepaliveRendererEpoch{1};
std::mutex g_fanLeaseKeepaliveMx;
unsigned long long g_fanKeepaliveRendererEpochSnapshot=1;
unsigned long long g_fanKeepaliveLogLeaseSequence=1,fixturePowerGeneration=1;
unsigned long long fixtureNotificationSequence=1;
unsigned long long currentFanPowerGeneration(){return fixturePowerGeneration;}
ULONGLONG g_fanKeepaliveRendererGraceUntil=0,fixtureClock=100000;
std::string g_fanLeaseId="fixture-lease-0001";
bool mayRun=true,postOk=true,throwSuccessDetail=false,throwLeaseCopy=false,throwFailureDetail=false;
int failMayRunCall=0,mayRunCalls=0,postCalls=0,detailConstructions=0,failureDetailConstructions=0,disarmCalls=0,caughtExceptions=0,assertions=0;
DWORD postStatus=200;
std::string postError,postRenewed;
std::function<void()> postHook,missLogHook,clockHook;
std::vector<json> logs,disarms;
ULONGLONG GetTickCount64(){if(clockHook){auto hook=std::move(clockHook);clockHook={};hook();}return fixtureClock;}
bool fanLeaseKeepaliveMayRun(){++mayRunCalls;return mayRun&&(failMayRunCall==0||mayRunCalls!=failMayRunCall);}
bool isFanLeaseIdValid(const std::string& s){return ymcc::fan_response::leaseIdValid(s);}
void appendFanLifecycleLog(const char* name,const json& details){
 if(std::string_view(name)=="native-lease-keepalive-stopped")disarms.push_back(details);
 else logs.push_back(json{{"event",name},{"details",details}});
 if(missLogHook&&std::string_view(name)=="native-lease-keepalive-miss")missLogHook();
}
struct FanHttpPowerAdmission {
 unsigned long long generation=0,sequence=0;
 bool captureCurrent(){generation=currentFanPowerGeneration();sequence=fixtureNotificationSequence;return current();}
 bool current() const{return mayRun&&generation==currentFanPowerGeneration()&&sequence==fixtureNotificationSequence;}
};
struct {void invalidate(){++disarmCalls;}} g_monitorFanDisplay;
int g_fanLeaseKeepaliveWake=0;
void SetEvent(int){}
void fanWritePowerCancel(){}
void traceLogTry(const char*){}
static void fanLeaseKeepaliveDisarm(const char*,const std::string* = nullptr);
// Extract supplies the production guard and retirement. The adapter overloads
// below are only for the old/new transport signatures in the red/green runs.
#include "DisarmExtract.inc"
#ifdef FIXTURE_BOUND_RECEIPT
static void fanLeaseKeepaliveDisarm(const char* reason,const std::string* expected){fanLeaseKeepaliveDisarm(reason,expected,0,nullptr);}
#endif
bool fanLeaseKeepalivePostHeartbeat(const std::string& requested,std::string* renewed,DWORD* status,std::string* code,const FanHttpPowerAdmission& = {}){++postCalls;*status=postStatus;*code=postError;*renewed=postOk?(postRenewed.empty()?requested:postRenewed):std::string{};if(postHook)postHook();return postOk;}
json CountSuccessDetail(json value){++detailConstructions;if(throwSuccessDetail){throwSuccessDetail=false;throw std::bad_alloc();}return value;}
json FixtureControlSnapshot(){return json{{"postCalls",postCalls},{"armed",g_fanLeaseKeepaliveArmed.load()},{"inFlight",g_fanLeaseKeepaliveInFlight.load()},{"lastRenewTick",g_fanLeaseKeepaliveLastRenewTick.load()},{"lease",g_fanLeaseId},{"disarms",disarms},{"caughtPayloadExceptions",caughtExceptions},{"mayRunCalls",mayRunCalls},{"logs",logs}};}
json CountFailureDetail(json value){++failureDetailConstructions;if(throwFailureDetail)throw std::bad_alloc();return value;}
void FixtureLeaseCopy(std::string& target,const std::string& source){if(throwLeaseCopy){throwLeaseCopy=false;throw std::bad_alloc();}target=source;}
#include "TickExtract.inc"
void expect(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
void tick(){fanLeaseKeepaliveTick();fixtureClock+=5000;}
int countEvent(const char* name){int count=0;for(const auto& log:logs)if(log.at("event")==name)++count;return count;}
int main(int argc,char** argv){
 if(argc!=3)return 9;
 const std::string scenario=argv[1],mode=argv[2];const bool guarded=mode=="guarded";
 try{
  int expectedDetails=-1;
  if(scenario=="steady"){
   for(int i=0;i<100;++i)tick();
   expect(postCalls==100,"optimization skipped real renewal decisions");expect(g_fanLeaseKeepaliveLastRenewTick==fixtureClock-5000,"last renewal not advanced each tick");expect(countEvent("native-lease-keepalive-success")==1,"steady success evidence changed");expect(disarmCalls==0&&g_fanLeaseKeepaliveArmed&&!g_fanLeaseKeepaliveInFlight,"steady owner state changed");expectedDetails=guarded?1:100;
  }else if(scenario=="transitions"){
   for(int i=0;i<3;++i)tick();postOk=false;postStatus=503;postError="FIXTURE_TRANSIENT";tick();postOk=true;postStatus=200;postError.clear();for(int i=0;i<3;++i)tick();postOk=false;postStatus=503;postError="FIXTURE_TRANSIENT";tick();postOk=true;postStatus=200;postError.clear();for(int i=0;i<2;++i)tick();
   expect(postCalls==10,"transition transport count changed");expect(countEvent("native-lease-keepalive-success")==3,"recovery success evidence lost");expect(countEvent("native-lease-keepalive-miss")==2&&disarmCalls==0,"transient failure policy changed");expectedDetails=guarded?3:8;
  }else if(scenario=="auth401"||scenario=="auth403"||scenario=="rejected409"){
   postOk=false;postStatus=scenario=="auth401"?401:scenario=="auth403"?403:409;postError="FIXTURE_REJECTED";tick();expect(!g_fanLeaseKeepaliveArmed&&disarmCalls==1,"rejected request did not retire its lease");tick();expect(postCalls==1,"rejected lease kept renewing");g_fanLeaseKeepaliveArmed=true;g_fanLeaseId="fixture-manual-0002";postOk=true;postStatus=200;postError.clear();tick();expect(postCalls==2&&countEvent("native-lease-keepalive-success")==1,"new manual lease not reachable");expectedDetails=1;
  }else if(scenario=="stale-owner"){
   postHook=[](){g_fanLeaseId="fixture-successor-0002";};tick();expect(g_fanLeaseId=="fixture-successor-0002"&&g_fanLeaseKeepaliveLastRenewTick==0,"stale success overwrote successor lease");expect(detailConstructions==0&&logs.empty(),"stale reply gained successful evidence");postHook={};tick();expect(postCalls==2&&countEvent("native-lease-keepalive-success")==1,"current successor did not renew");expectedDetails=1;
  }else if(scenario=="off-after-post"){
   postHook=[](){g_fanLeaseKeepaliveArmed=false;};tick();expect(g_fanLeaseKeepaliveLastRenewTick==0&&detailConstructions==0&&logs.empty(),"OFF stale reply published");g_fanLeaseKeepaliveArmed=true;postHook={};tick();expect(postCalls==2&&countEvent("native-lease-keepalive-success")==1,"manual rearm after OFF failed");expectedDetails=1;
  }else if(scenario=="withdrawn-success"||scenario=="withdrawn-rejection"||
           scenario=="republished-success"||scenario=="new-power-success"||
           scenario=="same-id-rearm-success"||scenario=="same-id-rearm-rejection"){
   const auto originalLease=g_fanLeaseId;
   const bool reject=scenario=="withdrawn-rejection"||scenario=="same-id-rearm-rejection";
   postOk=!reject;postStatus=reject?409:200;postError=reject?"LEASE_INVALID":"";
   postRenewed="fixture-obsolete-renewal";
   postHook=[&](){
    if(scenario=="withdrawn-success"||scenario=="withdrawn-rejection")mayRun=false;
    if(scenario=="republished-success")++fixtureNotificationSequence;
    if(scenario=="new-power-success")++fixturePowerGeneration;
    if(scenario=="same-id-rearm-success"||scenario=="same-id-rearm-rejection")++g_fanKeepaliveLogLeaseSequence;
   };
   tick();
   expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseId==originalLease&&g_fanLeaseKeepaliveLastRenewTick==0&&disarmCalls==0,
          "stale receipt changed the active lease or renewal clock");
   expect(logs.empty()&&!g_fanLeaseKeepaliveInFlight,"stale receipt published current outcome or retained the worker");
   postHook={};mayRun=true;postOk=true;postStatus=200;postError.clear();postRenewed="fixture-current-renewal";
   tick();
   expect(postCalls==2&&g_fanLeaseId==postRenewed&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock-5000,
          "legitimate current heartbeat did not advance the lease and time");
   expectedDetails=1;
  }else if(scenario=="withdrawn-before-disarm"){
   const auto originalLease=g_fanLeaseId;
   postOk=false;postStatus=409;postError="LEASE_INVALID";
   missLogHook=[](){mayRun=false;};tick();
   expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseId==originalLease&&disarmCalls==0,
          "power revoked during diagnostic still let the old rejection disarm");
   missLogHook={};mayRun=true;postOk=true;postStatus=200;postError.clear();tick();
   expect(postCalls==2&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock-5000,"worker did not continue after power returned");
   expectedDetails=1;
  }else if(scenario=="early-gates"){
   g_fanLeaseKeepaliveArmed=false;tick();expect(postCalls==0,"unarmed request ran");g_fanLeaseKeepaliveArmed=true;mayRun=false;tick();expect(postCalls==0,"power-blocked request ran");mayRun=true;g_fanLeaseId="bad";tick();expect(postCalls==0&&!g_fanLeaseKeepaliveInFlight,"invalid lease entered transport or left in-flight set");g_fanLeaseId="fixture-lease-0001";failMayRunCall=mayRunCalls+2;tick();expect(postCalls==0&&!g_fanLeaseKeepaliveInFlight,"second admission check bypassed");failMayRunCall=0;tick();expect(postCalls==1&&countEvent("native-lease-keepalive-success")==1,"new admissible manual lease was cached denied");expectedDetails=1;
  }else if(scenario=="throttle-and-inflight"){
   tick();fixtureClock-=4000;fanLeaseKeepaliveTick();expect(postCalls==1,"two-second minimum changed");fixtureClock+=5000;g_fanLeaseKeepaliveInFlight=true;fanLeaseKeepaliveTick();expect(postCalls==1&&g_fanLeaseKeepaliveInFlight,"second owner entered or cleared prior in-flight owner");g_fanLeaseKeepaliveInFlight=false;tick();expect(postCalls==2&&countEvent("native-lease-keepalive-success")==1,"current worker failed after busy gate");expectedDetails=guarded?1:2;
  }else if(scenario=="renderer-grace"){
   g_fanKeepaliveRendererEpoch=2;g_fanKeepaliveRendererGraceUntil=fixtureClock+90000;tick();tick();expect(postCalls==2&&countEvent("native-lease-keepalive-grace-active")==1,"grace policy changed");fixtureClock=g_fanKeepaliveRendererGraceUntil;tick();expect(!g_fanLeaseKeepaliveArmed&&postCalls==2&&disarmCalls==1,"expired renderer grace still renewed");g_fanKeepaliveRendererEpochSnapshot=2;g_fanLeaseId="fixture-lease-0001";g_fanLeaseKeepaliveArmed=true;tick();expect(postCalls==3&&countEvent("native-lease-keepalive-success")==1,"current rearm changed existing evidence policy");expectedDetails=guarded?1:3;
  }else if(scenario=="renderer-expiry-same-lease"||scenario=="renderer-expiry-new-lease"||
           scenario=="renderer-expiry-second-reload"||scenario=="renderer-expiry-suspend"||scenario=="renderer-expiry-off"){
   g_fanKeepaliveRendererEpoch=2;g_fanKeepaliveRendererGraceUntil=fixtureClock;
   const auto lease=scenario=="renderer-expiry-new-lease"?"fixture-next-lease":"fixture-lease-0001";
   const auto deadline=fixtureClock+90000;
   const bool suspending=scenario=="renderer-expiry-suspend",off=scenario=="renderer-expiry-off";
   clockHook=[&](){
    if(off){fanLeaseKeepaliveDisarm("fixture-off");return;}
    if(suspending){mayRun=false;++fixtureNotificationSequence;return;}
    // The renderer's successful receipt refreshes the owner under this lock.
    std::lock_guard<std::mutex> lock(g_fanLeaseKeepaliveMx);
    if(g_fanLeaseId!=lease)++g_fanKeepaliveLogLeaseSequence;
    g_fanLeaseId=lease;g_fanKeepaliveRendererEpochSnapshot=2;g_fanKeepaliveRendererGraceUntil=deadline;
    if(scenario=="renderer-expiry-second-reload")g_fanKeepaliveRendererEpoch=3;
   };
   tick();
   expect(!clockHook&&postCalls==0&&!g_fanLeaseKeepaliveInFlight,"old expiry must not dispatch a heartbeat");
   if(off){
    expect(!g_fanLeaseKeepaliveArmed&&g_fanLeaseId.empty()&&disarmCalls==1,"stale expiry invalidated the already retired OFF owner");
    tick();expect(postCalls==0,"OFF rearmed the worker");expectedDetails=0;
   }else{
    expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseId==lease&&disarmCalls==0&&disarms.empty(),
           "stale renderer expiry cleared a live lease or bypassed shared suspend ownership");
    if(suspending){
     expect(g_fanKeepaliveRendererGraceUntil==fixtureClock-5000,"obsolete expiry mutated the pending sleep scope");
     fanLeaseKeepaliveDisarm("power-suspend");tick();
     expect(!g_fanLeaseKeepaliveArmed&&postCalls==0&&disarmCalls==1,"shared suspend did not retire once");
     mayRun=true;g_fanLeaseId=lease;g_fanLeaseKeepaliveArmed=true;
    }else expect(g_fanKeepaliveRendererGraceUntil==deadline,"old expiry changed the new deadline");
    tick();expect(postCalls==1&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock-5000,"current owner could not continue at the next cadence");
    expectedDetails=1;
   }
  }else if(scenario=="payload-failure-before-exchange"){
   throwSuccessDetail=true;try{tick();}catch(const std::bad_alloc&){++caughtExceptions;fixtureClock+=5000;}expect(caughtExceptions==1&&!g_fanLeaseKeepaliveInFlight&&countEvent("native-lease-keepalive-success")==0,"payload failure left wrong owner/log state");tick();tick();expect(postCalls==3&&countEvent("native-lease-keepalive-success")==1,"construction failure falsely consumed first-success flag");expectedDetails=guarded?2:3;
  }else if(scenario=="transient-miss-throttle"){
   postOk=false;postStatus=503;postError="FIXTURE_TRANSIENT";for(int i=0;i<100;++i){fanLeaseKeepaliveTick();fixtureClock+=100;}expect(postCalls==100&&countEvent("native-lease-keepalive-miss")==1&&g_fanLeaseKeepaliveArmed,"existing miss throttle/rejection changed");postOk=true;postStatus=200;postError.clear();tick();expect(postCalls==101&&countEvent("native-lease-keepalive-success")==1,"first recovery success lost");expectedDetails=1;
  }else if(scenario=="miss-outcome-scope"){
   postOk=false;postStatus=0;postError.clear();
   const auto observe=[&](){fanLeaseKeepaliveTick();fixtureClock+=100;};
   observe();observe();expect(countEvent("native-lease-keepalive-miss")==1,"same failure flooded resident log");
   postStatus=503;observe();expect(countEvent("native-lease-keepalive-miss")==2&&logs.back().at("details").at("missLogSelection")=="reported-outcome-changed","status change swallowed");
   expect(logs.front().at("details").at("missLogSelection")=="first-failure","first miss has no selection evidence");
   postError="FIXTURE_CHANGED";observe();expect(countEvent("native-lease-keepalive-miss")==3&&logs.back().at("details").at("errorCode")==postError,"error code change swallowed or overwritten");
   ++fixturePowerGeneration;observe();expect(countEvent("native-lease-keepalive-miss")==4&&logs.back().at("details").at("requestFanPowerGeneration")==2,"new power scope swallowed");
   ++g_fanKeepaliveLogLeaseSequence;observe();expect(countEvent("native-lease-keepalive-miss")==5&&logs.back().at("details").at("requestLeaseSequence")==2,"new lease scope swallowed");
   postStatus=401;observe();expect(countEvent("native-lease-keepalive-miss")==6&&logs.back().at("details").at("missLogSelection")=="rejection"&&disarmCalls==1&&!g_fanLeaseKeepaliveArmed,"terminal rejection lost or cleanup changed");
   expect(postCalls==7,"diagnostic gate changed request count");expectedDetails=0;
  }else if(scenario=="miss-after-recovery"){
   postOk=false;postStatus=503;postError="FIXTURE_TRANSIENT";tick();
   postOk=true;postStatus=200;postError.clear();fanLeaseKeepaliveTick();fixtureClock+=2000;
   postOk=false;postStatus=503;postError="FIXTURE_TRANSIENT";fanLeaseKeepaliveTick();
   expect(postCalls==3&&countEvent("native-lease-keepalive-success")==1,"existing recovery receipt or cadence changed");
   expect(countEvent("native-lease-keepalive-miss")==2&&logs.back().at("details").at("missLogSelection")=="first-failure","same failure after recovery swallowed");
   fixtureClock+=9999;fanLeaseKeepaliveTick();expect(countEvent("native-lease-keepalive-miss")==2,"same failure written before 10 seconds");
   ++fixtureClock;fanLeaseKeepaliveTick();expect(countEvent("native-lease-keepalive-miss")==3&&logs.back().at("details").at("missLogSelection")=="continuing-failure","10-second witness missing");
   expectedDetails=1;
  }else if(scenario=="miss-detail-exception-on-rejection"){
   postOk=false;postStatus=401;postError="LEASE_INVALID";throwFailureDetail=true;
   tick();expect(disarmCalls==1&&!g_fanLeaseKeepaliveArmed&&!g_fanLeaseKeepaliveInFlight,"diagnostic construction blocked rejection cleanup");
   expect(postCalls==1&&countEvent("native-lease-keepalive-miss")==0,"diagnostic fault changed transport or invented a row");
   --failureDetailConstructions; // One explicitly failed construction, no row.
   expectedDetails=0;
  }else if(scenario=="busy-admission"){
   const auto candidate=g_fanLeaseId;postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";
   tick();expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseId==candidate&&disarmCalls==0,"unsubmitted HC busy retired the only native renewal owner");expect(g_fanLeaseKeepaliveLastRenewTick==0&&countEvent("native-lease-keepalive-success")==0,"busy response invented lease renewal");
   tick();expect(postCalls==2&&g_fanLeaseKeepaliveArmed&&g_fanLeaseKeepaliveLastRenewTick==0,"second existing-cadence probe lost its candidate or claimed success");
   postOk=true;postStatus=200;postError.clear();tick();expect(postCalls==3&&g_fanLeaseKeepaliveArmed&&disarmCalls==0&&g_fanLeaseKeepaliveLastRenewTick!=0,"current native owner could not recover after transient busy");expect(countEvent("native-lease-keepalive-success")==1,"first confirmed recovery lost its evidence");expectedDetails=1;
  }else if(scenario=="busy-auth-refused"){
   for(const DWORD code:{401UL,403UL}){g_fanLeaseId="fixture-lease-0001";g_fanLeaseKeepaliveArmed=true;postOk=false;postStatus=code;postError="HC_OPERATION_BUSY";tick();expect(!g_fanLeaseKeepaliveArmed,"auth refusal was hidden by misleading busy code");}
   expect(disarmCalls==2&&g_fanLeaseKeepaliveLastRenewTick==0,"auth response became a renewal or failed to disarm");expectedDetails=0;
  }else if(scenario=="busy-lifecycle-refused"){
   postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";tick();expect(g_fanLeaseKeepaliveArmed&&disarmCalls==0,"initial busy did not preserve candidate");
   for(const auto* code:{"HC_CLOSE_PENDING","HOST_CLOSING","LEASE_INVALID",""}){g_fanLeaseId="fixture-lease-0001";g_fanLeaseKeepaliveArmed=true;postError=code;tick();expect(!g_fanLeaseKeepaliveArmed,"real lifecycle/lease rejection or unknown 409 did not disarm");}
   expect(disarmCalls==4&&g_fanLeaseKeepaliveLastRenewTick==0,"lifecycle rejection changed lease truth");expectedDetails=0;
  }else if(scenario=="busy-off-inflight"){
   postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";postHook=[](){g_fanLeaseId.clear();g_fanLeaseKeepaliveArmed=false;};tick();expect(!g_fanLeaseKeepaliveArmed&&g_fanLeaseId.empty()&&g_fanLeaseKeepaliveLastRenewTick==0,"busy reply rearmed explicit OFF");postHook={};tick();expect(postCalls==1,"OFF left a new periodic request");expectedDetails=0;
  }else if(scenario=="busy-new-owner-inflight"){
   postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";postHook=[](){g_fanLeaseId="fixture-successor-lease";};tick();expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseId=="fixture-successor-lease"&&disarmCalls==0,"old busy result touched successor lease");expect(g_fanLeaseKeepaliveLastRenewTick==0,"old busy reply confirmed a newer renewal");postHook={};postOk=true;postStatus=200;postError.clear();tick();expect(postCalls==2&&g_fanLeaseKeepaliveArmed&&countEvent("native-lease-keepalive-success")==1,"successor native candidate could not continue");expectedDetails=1;
  }else if(scenario=="busy-sleep-boundary"){
   postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";postHook=[](){mayRun=false;fanLeaseKeepaliveDisarm("power-suspend");};tick();expect(!g_fanLeaseKeepaliveArmed&&disarmCalls==1,"busy response bypassed explicit sleep disarm");postHook={};tick();expect(postCalls==1,"sleep permitted another heartbeat");mayRun=true;g_fanLeaseId="fixture-next-awake-lease";g_fanLeaseKeepaliveArmed=true;postOk=true;postStatus=200;postError.clear();tick();expect(postCalls==2&&g_fanLeaseKeepaliveArmed&&countEvent("native-lease-keepalive-success")==1,"next explicit awake candidate stayed blocked");expectedDetails=1;
  }else if(scenario=="busy-renderer-grace"){
   postOk=false;postStatus=409;postError="HC_OPERATION_BUSY";g_fanKeepaliveRendererEpoch=2;g_fanKeepaliveRendererGraceUntil=fixtureClock+10000;tick();tick();expect(g_fanLeaseKeepaliveArmed&&postCalls==2&&g_fanLeaseKeepaliveLastRenewTick==0,"temporary busy killed bounded renderer grace");tick();expect(!g_fanLeaseKeepaliveArmed&&postCalls==2&&disarmCalls==1,"busy response extended renderer grace forever");expectedDetails=0;
  }else if(scenario=="post-exception-retry"||scenario=="post-allocation-exception"||scenario=="lease-copy-exception"||scenario=="throw-after-owner-change"||scenario=="throw-after-off"||scenario=="reentrant-post-exception"){
   const auto originalLease=g_fanLeaseId;
   if(scenario=="lease-copy-exception")throwLeaseCopy=true;
   else postHook=[&](){
    if(scenario=="throw-after-owner-change")g_fanLeaseId="fixture-next-owner-0002";
    if(scenario=="throw-after-off"){g_fanLeaseId.clear();g_fanLeaseKeepaliveArmed=false;}
    if(scenario=="reentrant-post-exception"){fanLeaseKeepaliveTick();expect(postCalls==1,"reentrant fallback entered a second request");}
    if(scenario=="post-allocation-exception")throw std::bad_alloc();
    throw std::runtime_error("inert POST/credential observation failure");
   };
   try{tick();}catch(const std::exception&){++caughtExceptions;fixtureClock+=5000;}
   expect(caughtExceptions==1,"fixture exception did not exercise the production request owner");
   expect(!g_fanLeaseKeepaliveInFlight,"exception permanently retained native request ownership");
   expect(g_fanLeaseKeepaliveLastRenewTick==0&&disarmCalls==0,"unknown exception invented a renewal or retired an owner");
   const int beforeRecoveryPosts=postCalls;postHook={};
   if(scenario=="throw-after-off"){
    tick();expect(postCalls==beforeRecoveryPosts&&!g_fanLeaseKeepaliveArmed,"exception revived OFF");
    g_fanLeaseId="fixture-new-manual-0003";g_fanLeaseKeepaliveArmed=true;
   }else if(scenario=="throw-after-owner-change")expect(g_fanLeaseId=="fixture-next-owner-0002","exception overwrote newer owner");
   else expect(g_fanLeaseId==originalLease&&g_fanLeaseKeepaliveArmed,"exception erased current candidate");
   tick();expect(postCalls==beforeRecoveryPosts+1&&!g_fanLeaseKeepaliveInFlight&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock-5000,"next existing-cadence/manual heartbeat could not progress");
   expectedDetails=1;
  }else throw std::runtime_error("unknown scenario");
  expect(detailConstructions==expectedDetails,"unexpected success JSON materialization count");
  for(const auto& row:logs)if(row.at("event")=="native-lease-keepalive-success")expect(row.at("details")==json{{"status",200},{"leasePresent",true},{"source","native.ready"}},"success log payload changed");
  expect(failureDetailConstructions==countEvent("native-lease-keepalive-miss"),"suppressed misses still materialized failure JSON");
  const json control=FixtureControlSnapshot();
  std::cout<<json{{"ok",true},{"scenario",scenario},{"mode",mode},{"checks",assertions},{"successDetailsConstructed",detailConstructions},{"failureDetailsConstructed",failureDetailConstructions},{"control",control},{"hardwareCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& ex){std::cout<<json{{"ok",false},{"scenario",scenario},{"checks",assertions},{"error",ex.what()},{"postCalls",postCalls},{"armed",g_fanLeaseKeepaliveArmed.load()},{"disarmCalls",disarmCalls},{"lastRenewTick",g_fanLeaseKeepaliveLastRenewTick.load()},{"successDetailsConstructed",detailConstructions},{"failureDetailsConstructed",failureDetailConstructions},{"control",FixtureControlSnapshot()}}.dump()<<'\n';return 1;}
}
