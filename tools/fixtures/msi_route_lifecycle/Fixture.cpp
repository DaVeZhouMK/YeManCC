#include <windows.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include "virtual_start_protection.h"
#include "msi_virtual_wake.h"
#include "json.hpp"
using json=nlohmann::json;
@@ENUM_AND_CONSTANTS@@
using Action=MsiDinputRouteWindowAction;
static ymcc::VirtualStartProtection protection;
static bool knownBoard=true,exitFence=false;
static struct {bool enabled=true,virtualTargetEnabled=true;} g_realStickTest;
static std::atomic<bool> g_exitRequested{false},g_inputCaptureStop{false};
static std::atomic<unsigned long long> g_msiVirtualWakeGeneration{0},g_msiVirtualWakeSourceWaitLoggedGeneration{0};
static ymcc::msi::VirtualWakeFacts wake;
static std::vector<json> logRows;
static ymcc::VirtualStartProtection& g_virtualStartProtection=protection;
static bool msiClawKnownBoardAuthority(){return knownBoard;}
static bool inputHostExitFenceRaised(){return exitFence;}
static unsigned long long inputHostEpochToken(){return 1;}
static ymcc::msi::VirtualWakeFacts msiVirtualWakeFacts(ULONGLONG now){auto f=wake;f.now=now;return f;}
static bool msiVirtualWakeWaitSource(ULONGLONG now){return ymcc::msi::virtualWakeMayWaitSource(msiVirtualWakeFacts(now),true,1);}
static void appendNativeLifecycleLog(const char* event,json row){row["event"]=event;logRows.push_back(row);}
@@PRODUCTION_DECIDE@@
@@PRODUCTION_WRAPPER@@
@@PRODUCTION_BASE_EVENT@@
static int checks=0;static std::vector<std::string> failures;static std::vector<json> cases;
static void check(bool ok,const std::string& label){++checks;if(!ok)failures.push_back(label);}
static ULONGLONG clockBase=100000;
static auto begin(const std::string& persona="steamdeck"){
 clockBase+=100000;knownBoard=true;exitFence=false;g_realStickTest={true,true};g_exitRequested=false;g_inputCaptureStop=false;
 wake={};g_msiVirtualWakeGeneration=0;g_msiVirtualWakeSourceWaitLoggedGeneration=0;logRows.clear();
 return protection.admit(persona,clockBase);
}
static Action decide(ULONGLONG offset,ULONGLONG routeWait,bool resent=true,bool observed=false){return msiDinputRouteWindowDecideForWake(true,false,resent,routeWait,observed,clockBase+offset);}
static void finish(const char* name,std::size_t previous){cases.push_back({{"name",name},{"pass",failures.size()==previous}});}
int main(){
 auto start=failures.size();
 // This window is the same source ordering in the 19:45 trace: route=+0.5s,
 // SDK mount=+9.3s, legacy revert=+10.5s, first independent DInput=+11.8s.
 auto admission=begin();check(admission.accepted,"R3 original request admitted");
 check(protection.beginVerification(admission.requestId,"success",clockBase+9318),"actual R3 starts active inspection after mount");
 check(decide(10500,10000)==Action::Poll,"late-DInput: route remains readable after legacy10s while original request is alive");
 for(ULONGLONG t:{10750ull,11000ull,11307ull,11800ull})check(decide(t,t-500)==Action::Poll,"late-DInput: preserves original request until late readable source");
 check(logRows.size()==1&&logRows[0]["event"]=="msi-virtual-start-source-wait","one low-frequency wait record per request");
 if(!logRows.empty()){check(logRows[0].value("modeRetryAdded",true)==false&&logRows[0].value("publicationGranted",true)==false,"waiting grants neither a mode retry nor sample admission");check(logRows[0].value("remainingMs",0ull)==19500,"remaining budget is from original admission");}
 check(protection.view(clockBase+11800).fallbackAttempts==0,"execution waiting consumes no fallback");
 check(!protection.finishVerification(admission.requestId,"success",clockBase+11800),"source availability cannot bypass actual ten-second inspection");
 check(protection.finishVerification(admission.requestId,"success",clockBase+19318),"same request can finish after actual inspection");
 finish("captured-late-source-ordering",start);
 start=failures.size();auto never=begin();
 protection.beginVerification(never.requestId,"success",clockBase+9000);
 check(decide(29999,29499)==Action::Poll,"original30s: last millisecond may still read");
 check(decide(30000,29500)==Action::Revert,"original30s: missing source expires without extension");
 check(protection.view(clockBase+30000).phase=="expired"&&!protection.view(clockBase+30000).locked,"original30s: UI request budget unchanged");
 check(!protection.reserveFallback(never.requestId,clockBase+30000),"expired request cannot reserve fallback");
 finish("source-never-arrives",start);
 start=failures.size();auto duringWake=begin();wake={true,true,true,false,3,3,clockBase-1000,0};g_msiVirtualWakeGeneration=3;
 check(decide(10500,10000)==Action::Poll,"player during wake remains within original request");
 check(decide(30000,29500)==Action::Revert,"wake60s cannot lend budget to a later expired player request");
 check(decide(40000,39500)==Action::Revert,"no repeated polling extends expired player through wake budget");
 finish("player-and-prior-wake-budget-order",start);
 start=failures.size();auto beforeWake=begin();protection.view(clockBase+30000);wake={true,true,true,false,4,4,clockBase+40000,0};g_msiVirtualWakeGeneration=4;
 check(decide(50000,10000)==Action::Poll,"later genuine wake retains its existing generation-owned source wait");
 wake.currentGeneration=5;check(decide(50000,10000)==Action::Revert,"later wake cannot borrow a stale power generation");
 finish("later-independent-wake-owner",start);
 start=failures.size();auto pending=begin();
 check(decide(11000,10000)==Action::Poll,"pending factory source wait uses original admission even without prepare receipt");
 check(!protection.admit("disabled",clockBase+11000).accepted,"player cannot submit OFF during this original request");
 check(!protection.admit("elite",clockBase+11000).accepted,"player cannot change persona during source wait");
 protection.beginVerification(pending.requestId,"failure",clockBase+12000);
 check(decide(13000,12500)==Action::Poll,"provisional failure does not create a competing route owner");
 check(protection.reserveFallback(pending.requestId,clockBase+22000),"existing R3 fallback may reserve only after active inspection");
 check(decide(23000,22500)==Action::Poll&&protection.view(clockBase+23000).fallbackAttempts==1,"read-only wait neither resets nor adds fallback quota");
 check(!protection.reserveFallback(pending.requestId,clockBase+24000),"source wait cannot reopen the exhausted fallback quota");
 finish("actual-pending-verification-fallback-authority",start);
 start=failures.size();auto completed=begin();protection.beginVerification(completed.requestId,"success",clockBase+1000);
 check(protection.finishVerification(completed.requestId,"success",clockBase+11000),"actual successful verification completes");
 check(decide(12000,11500)==Action::Revert,"completed player intent cannot extend a later source episode");
 auto newer=protection.admit("elite",clockBase+13000);check(newer.accepted&&newer.requestId!=completed.requestId,"only a fresh admitted request gets a new deadline");
 check(decide(24000,23500)==Action::Poll,"fresh request uses its own existing R3 admission, not a hidden route timer");
 check(protection.view(clockBase+24000).remainingMs==19000,"new request keeps exact original remaining budget");
 finish("unique-completion-and-new-admission",start);
 start=failures.size();begin();knownBoard=false;check(decide(11000,10000)==Action::Revert,"non-MSI has no player source-wait extension");
 knownBoard=true;g_realStickTest.enabled=false;check(decide(11000,10000)==Action::Revert,"disabled capture is not a source-wait permission");g_realStickTest.enabled=true;
 g_realStickTest.virtualTargetEnabled=false;check(decide(11000,10000)==Action::Revert,"target OFF cannot inherit an ON wait");g_realStickTest.virtualTargetEnabled=true;
 exitFence=true;check(decide(11000,10000)==Action::Revert,"exit fence revokes wait");exitFence=false;
 g_exitRequested=true;check(decide(11000,10000)==Action::Revert,"app exit revokes wait");g_exitRequested=false;
 g_inputCaptureStop=true;check(decide(11000,10000)==Action::Revert,"capture stop revokes wait");g_inputCaptureStop=false;
 finish("family-off-and-exit-boundaries",start);
 start=failures.size();begin("disabled");check(decide(11000,10000)==Action::Revert,"R3 OFF intent never acquires source wait");finish("disabled-intent",start);
 start=failures.size();begin();
 check(msiDinputRouteWindowDecideForWake(false,false,false,12000,false,clockBase+12000)==Action::None,"unarmed route unchanged");
 check(msiDinputRouteWindowDecideForWake(true,true,false,12000,false,clockBase+12000)==Action::None,"confirmed route unchanged");
 check(decide(5000,5000,false)==Action::Resend,"single existing 5s resend retained");
 check(decide(5000,5000,true)==Action::Poll,"resend flag prevents a second resend");
 check(decide(1000,1000,false,true)==Action::Resend,"observed XInput return preserves existing resend");
 finish("existing-base-decisions",start);
 start=failures.size();begin("disabled");
 for(bool armed:{false,true})for(bool confirmed:{false,true})for(bool resent:{false,true})for(bool observed:{false,true})for(ULONGLONG waited:{0ull,1ull,4999ull,5000ull,9999ull,10000ull,10001ull,30000ull}) {
  auto actual=msiDinputRouteWindowDecideForWake(armed,confirmed,resent,waited,observed,clockBase+11000);
  check(actual==msiDinputRouteWindowDecide(armed,confirmed,resent,waited,observed),"outside ON: existing route policy grid stays identical");
 }
 finish("unowned-route-policy-property-grid",start);
 start=failures.size();begin();check(nativeLifecycleEventIsBase("msi-virtual-start-source-wait"),"wait evidence survives detailed-log OFF");finish("ordinary-export-evidence",start);
 json result={{"status",failures.empty()?"PASS":"FAIL"},{"passed",failures.empty()},{"checks",checks},{"cases",cases},{"failures",failures},{"actualR3AuthorityExecuted",true},{"actualRouteDecisionExecuted",true},{"actualPumpExecuted",false},{"sdkOrDeviceOperations",false},{"normalProductMainExecuted",false}};
 std::cout<<result.dump()<<std::endl;return failures.empty()?0:1;
}
