
#include "xinput_identity_refresh_policy.h"
struct FeedbackTargetPort {unsigned slot=1;unsigned long long identityEpoch=7,sourceGeneration=9,powerGeneration=1;ymcc::input::identityRefresh::Proof xinputIdentity{11,7,1,true,false};};
static std::mutex g_feedbackPulseMx;
static bool g_feedbackPulseActive=false,g_feedbackPulseTouch=false;
static unsigned long long g_rogVibratePulseGen=0,fixtureTick=1000;
static const unsigned kXInputSlotReprobeBackoffMs=100;
static FeedbackTargetPort g_feedbackPulseTarget;
static ymcc::input::identityRefresh::DeferredStop g_xinputDeferredFeedbackStop;
static bool unresolvedPort=true,acceptedPort=false;
static DWORD errorPort=0;
static unsigned stopCalls=0,scheduleCalls=0,finishCalls=0;
static void physicalFeedbackStopTarget(const FeedbackTargetPort&,bool,bool* unresolved,bool* accepted,DWORD* error){++stopCalls;*unresolved=unresolvedPort;*accepted=acceptedPort;*error=errorPort;}
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int){++scheduleCalls;return true;}
static void gamepadSerialGameOwnerFinished(unsigned long long){++finishCalls;}
static ULONGLONG fixtureFeedbackTick(){return fixtureTick;}
#define GetTickCount64 fixtureFeedbackTick
@@ACTUAL_FINISH@@
#undef GetTickCount64
static void beginPulse(unsigned long long gen){g_feedbackPulseActive=true;g_rogVibratePulseGen=gen;g_xinputDeferredFeedbackStop.clear();unresolvedPort=true;acceptedPort=false;errorPort=0;}
static bool payload(const json& row,const char* phase,bool retained){return row["event"]=="gamepad-feedback-stop-pending"&&row["phase"]==phase&&row["ownershipRetained"]==retained&&row["hardwareStopObserved"]==false&&row["pulseGeneration"]==g_rogVibratePulseGen&&row["slot"]==1;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 check(nativeLifecycleEventIsBase("gamepad-feedback-stop-pending"),"original-bounded-stop-event-registered");
 beginPulse(100);physicalFeedbackFinish(100);auto rows=readRecoveryRows(root);
 check(rows.size()==1&&payload(rows[0],"bounded-retry",true)&&rows[0]["retryInMs"]==100&&rows[0]["reason"]=="identity-unresolved"&&rows[0]["writeError"].is_null()&&g_feedbackPulseActive&&g_xinputDeferredFeedbackStop.pendingFor(1)==100,"actual-first-pending-off-visible-with-owned-identity-and-unknown-error");
#ifdef YMCC_FEEDBACK_STOP_OLD_ONLY
 closeStreams();
#else
 for(unsigned i=0;i<50000;++i)physicalFeedbackFinish(100);
 check(readRecoveryRows(root).size()==1&&g_nativeDetailWrites==0&&!g_nativeDetailLog.is_open(),"50000-original-pending-revisits-create-no-extra-record");
 fixtureTick+=100;physicalFeedbackFinish(100);rows=readRecoveryRows(root);
 check(rows.size()==2&&payload(rows[1],"awaiting-identity-event",true)&&rows[1]["retryInMs"]==0,"actual-bounded-deadline-to-dormant-edge-retained");
 const auto schedules=scheduleCalls;for(unsigned i=0;i<50000;++i)physicalFeedbackFinish(100);
 check(readRecoveryRows(root).size()==2&&scheduleCalls==schedules&&g_feedbackPulseActive,"50000-original-dormant-revisits-quiet-without-reschedule");
 unresolvedPort=false;acceptedPort=true;physicalFeedbackFinish(100);rows=readRecoveryRows(root);
 check(rows.size()==3&&payload(rows.back(),"settled",false)&&rows.back()["zeroApiAccepted"]==true&&rows.back()["reason"]=="zero-api-accepted"&&!g_feedbackPulseActive&&g_xinputDeferredFeedbackStop.pendingFor(1)==0&&finishCalls==1,"actual-successful-stop-settlement-retained-without-hardware-claim");
 beginPulse(101);errorPort=5;physicalFeedbackFinish(101);rows=readRecoveryRows(root);
 check(rows.size()==4&&rows.back()["reason"]=="zero-write-failed"&&rows.back()["writeError"]==5&&g_feedbackPulseActive,"actual-zero-write-error-and-new-pulse-preserved");
 unresolvedPort=false;acceptedPort=false;physicalFeedbackFinish(101);rows=readRecoveryRows(root);
 check(rows.size()==5&&payload(rows.back(),"settled",false)&&rows.back()["zeroApiAccepted"]==false&&rows.back()["reason"]=="source-retired-or-power-gated","retirement-settlement-does-not-claim-zero-api-or-physical-stop");
 beginPulse(102);const auto calls=stopCalls;physicalFeedbackFinish(101);
 check(stopCalls==calls&&readRecoveryRows(root).size()==5&&g_feedbackPulseActive,"stale-generation-keeps-original-early-return");
 g_domainLogEnabled[1]=true;physicalFeedbackFinish(102);g_domainLogEnabled[1]=false;rows=readRecoveryRows(root);
 check(rows.size()==6&&payload(rows.back(),"bounded-retry",true)&&g_nativeDetailWrites==0,"detail-on-retains-same-boundary-in-resident-file");
 const json high={{"reason","buttons-change"}};const auto writes=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(arm==0&&writes==g_nativeLifecycleFrameWrites,"unrelated-50000-high-frequency-off-samples-allocation-free-and-silent");
 const auto prior=readRecoveryRows(root).size();appendNativeLifecycleLog("gamepad-feedback-stop-pending-future",{{"ok",false}});check(readRecoveryRows(root).size()==prior,"event-name-registry-remains-exact");
 const auto failure=count(Failure::eventWrite);beginPulse(103);{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);physicalFeedbackFinish(103);}
 unresolvedPort=false;acceptedPort=true;physicalFeedbackFinish(103);rows=readRecoveryRows(root);
 check(count(Failure::eventWrite)==failure+1&&rows.size()==prior+1&&payload(rows.back(),"settled",false)&&!g_feedbackPulseActive,"sink-write-failure-counted-and-existing-business-settlement-still-completes");
 closeStreams();
#endif
 GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"completePhysicalFeedbackFinish",true},{"actualDeferredStop",true},{"deviceCalls",0},{"fullProductBuild",false}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& e){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<e.what()<<'\n';return 3;}}
