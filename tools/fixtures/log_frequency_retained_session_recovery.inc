#include "log_ai_index.h"
#include "input_host_process_retirement.h"
static void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
@@ACTUAL_MEMORY_LEAVES@@
@@ORIGINAL_CLEAR_PORTS@@
#include "input_host_shared_power.h"
static ymcc::inputHost::sharedPower::Cache g_inputHostInputPowerCache,g_inputHostLifecyclePowerCache;
@@ACTUAL_QUARANTINE_DECLARATIONS@@
static std::string g_inputHostFrameReplyBinding,g_xinputReconfirmPhysicalRoot;
static bool g_inputHostCompactFrameRequest=false;
static std::mutex g_hidHidePhysicalIdentityMx;
@@ACTUAL_PIPE_CLOSE@@
#define CloseHandle memoryApi::close
@@ACTUAL_PIPE_WRAPPER@@
#undef CloseHandle
@@ACTUAL_PRIMARY_HELPER@@
@@ACTUAL_CLEAR@@
static void retainedClearCall(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostClearLocked();}
static json retainedSessionRows(const fspath::path& root,const char* event,bool detail=false){
 std::ifstream input(root/(detail?L"native-detail.log":L"native-lifecycle.log"));json rows=json::array();std::string line;
 while(std::getline(input,line))if(!line.empty()){auto row=json::parse(line);if(row.value("event",std::string{})==event)rows.push_back(std::move(row));}return rows;
}
static void retainedTuple(unsigned epoch,const char* host="fixture-host"){
 g_inputHostRunId="fixture-run";g_inputHostHostInstanceId=host;g_inputHostEpoch=epoch;g_inputHostSequence=23;g_inputHostBoundPowerGeneration=9;
 g_inputHostHello=g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=true;g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();(void)ymcc::sleepfactlog::writerIdentity();g_powerGeneration=9;
 DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
 const auto finish=[&]{closeStreams();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;check(memoryApi::live.empty()&&before==after,"all-owned-real-fixture-handles-returned-before-exit");unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"actualCompleteClearExecuted",true},{"actualOriginalNativeLogger",true},{"originalMemoryLeavesAndRuntime",true},{"externalTargetAndDeviceProofExecuted",false}}.dump(2)<<'\n';return failures?2:0;};
 retainedTuple(17);g_inputHostProcess=memoryApi::source();g_domainLogEnabled[1]=false;retainedClearCall();flushNativeLifecycleLogs(false);
 auto rows=retainedSessionRows(root,"input-host-session-retained");
 check(rows.size()==1&&rows.back()["repeatObservation"]==false&&g_inputHostRunId=="fixture-run"&&g_inputHostProcess!=nullptr&&g_inputHostEpoch==17,"original-complete-Clear-retains-first-refusal-and-old-tuple");
 const auto stableWrites=g_nativeLifecycleFrameWrites;const auto priorRevoke=ports.revoke,priorForget=ports.forget;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)retainedClearCall();const auto stableArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(stableArm==0&&g_nativeLifecycleFrameWrites==stableWrites&&ports.revoke==priorRevoke+100000&&ports.forget==priorForget+100000,"100000-stable-Clear-calls-skip-diagnostic-allocation-but-preserve-original-precleanup-actions");
 closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();g_inputHostHostInstanceId="changed-host";const auto openBefore=count(Failure::eventOpen);retainedClearCall();
 check(count(Failure::eventOpen)==openBefore+1&&g_inputHostRunId=="fixture-run"&&g_inputHostHostInstanceId=="changed-host","original-file-open-failure-cannot-clear-retained-session");
 const auto failedWrites=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)retainedClearCall();const auto failedArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(failedArm==0&&g_nativeLifecycleFrameWrites==failedWrites,"unresolved-file-failure-does-not-create-retained-session-log-retry-storm");
 const auto epochBefore=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"retainedSessionLogRecovery",1}});const auto beforeRecoveryRows=retainedSessionRows(root,"input-host-session-retained").size();retainedClearCall();flushNativeLifecycleLogs(false);rows=retainedSessionRows(root,"input-host-session-retained");
 check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>epochBefore&&rows.size()==beforeRecoveryRows+1&&rows.back()["repeatObservation"]==false,"same-retained-session-reoffered-resident-after-original-file-recovery");
#ifdef YMCC_RETAINED_SESSION_OLD_ONLY
 require(memoryApi::close(g_inputHostProcess)!=FALSE,"owned source close");g_inputHostProcess=nullptr;retainedClearCall();return finish();
#else
 check(rows.back()["sourceLogRecoveryEpoch"]==ymcc::logging::nativeLogFailureEvidence().recoveryEpoch(),"recovery-row-reports-original-log-epoch-without-device-or-return-authority");
 const auto onceWrites=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)retainedClearCall();const auto onceArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(onceArm==0&&g_nativeLifecycleFrameWrites==onceWrites,"confirmed-recovery-reoffers-once-then-original-detail-OFF-is-quiet");
 g_domainLogEnabled[1]=true;const auto detailBefore=retainedSessionRows(root,"input-host-session-retained",true).size();for(unsigned i=0;i<5;++i)retainedClearCall();flushNativeLifecycleLogs(false);
 check(retainedSessionRows(root,"input-host-session-retained").size()==rows.size()&&retainedSessionRows(root,"input-host-session-retained",true).size()==detailBefore+5,"detail-ON-repeats-remain-detailed-with-original-repeatObservation-true");
 g_domainLogEnabled[1]=false;++g_inputHostEpoch;timestampAllocationFault=true;const auto preparationBefore=count(Failure::preparationException);retainedClearCall();timestampAllocationFault=false;
 check(count(Failure::preparationException)==preparationBefore+1&&g_inputHostEpoch==18&&g_inputHostProcess!=nullptr,"actual-Native-preparation-failure-preserves-complete-Clear-refusal");
 appendNativeLifecycleLog("session-clean-exit",{{"retainedSessionLogRecovery",2}});const auto prepareRows=retainedSessionRows(root,"input-host-session-retained").size();retainedClearCall();flushNativeLifecycleLogs(false);
 check(retainedSessionRows(root,"input-host-session-retained").size()==prepareRows+1,"same-session-reoffered-after-original-Native-preparation-recovery");
 const auto detailOnlyEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto detailOnlyWrites=g_nativeLifecycleFrameWrites;ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail-only");appendNativeLifecycleLog("session-clean-exit",{{"detailOnly",true}});const auto afterDetailOther=g_nativeLifecycleFrameWrites;retainedClearCall();
 check(afterDetailOther>detailOnlyWrites&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==detailOnlyEpoch&&g_nativeLifecycleFrameWrites==afterDetailOther,"detail-only-failure-does-not-rearm-retained-session-resident-log");
 g_inputHostHello=false;const auto changedRows=retainedSessionRows(root,"input-host-session-retained").size();retainedClearCall();flushNativeLifecycleLogs(false);
 check(retainedSessionRows(root,"input-host-session-retained").size()==changedRows+1&&!g_inputHostHello&&g_inputHostPrepared,"real-retained-flags-transition-remains-resident-with-tuple-held");
 require(memoryApi::close(g_inputHostProcess)!=FALSE,"owned source close");g_inputHostProcess=nullptr;const auto retiredBefore=retainedSessionRows(root,"input-host-session-local-retired").size();retainedClearCall();flushNativeLifecycleLogs(false);auto retired=retainedSessionRows(root,"input-host-session-local-retired");
 check(retired.size()==retiredBefore+1&&retired.back()["runId"]=="fixture-run"&&retired.back()["epoch"]==18&&g_inputHostRunId.empty()&&g_inputHostEpoch==0&&!g_inputHostPrepared,"successful-original-Clear-emits-old-local-boundary-before-tuple-reset");
 const auto emptyWrites=g_nativeLifecycleFrameWrites;for(unsigned i=0;i<1000;++i)retainedClearCall();
 check(g_nativeLifecycleFrameWrites==emptyWrites&&memoryApi::live.empty(),"1000-empty-original-Clear-calls-add-no-retained-or-terminal-rows");
 retainedTuple(19,"raw-pipe-host");g_inputHostUnadoptedPipe=memoryApi::source();memoryApi::failRoles=memoryApi::Original;const auto rawRows=retainedSessionRows(root,"input-host-session-retained").size();retainedClearCall();flushNativeLifecycleLogs(false);rows=retainedSessionRows(root,"input-host-session-retained");
 check(rows.size()==rawRows+1&&rows.back()["pipeRetirementProven"]==false&&g_inputHostUnadoptedPipe&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&g_inputHostEpoch==19,"actual-original-raw-HANDLE-close-refusal-retains-tuple-and-blocks-start");
 memoryApi::failRoles=0;g_inputHostReleaseQuarantined=false;retainedClearCall();flushNativeLifecycleLogs(false);
 check(!g_inputHostUnadoptedPipe&&memoryApi::live.empty()&&g_inputHostRunId.empty()&&g_inputHostEpoch==0,"next-original-Clear-releases-owned-raw-HANDLE-and-resets-tuple-after-allowed-close");
 retainedTuple(20);g_inputHostProcess=memoryApi::source();const auto nextRows=retainedSessionRows(root,"input-host-session-retained").size();retainedClearCall();flushNativeLifecycleLogs(false);
 check(retainedSessionRows(root,"input-host-session-retained").size()==nextRows+1,"new-retained-session-after-successful-Clear-rearms-original-first-witness");
 require(memoryApi::close(g_inputHostProcess)!=FALSE,"owned source close");g_inputHostProcess=nullptr;retainedClearCall();flushNativeLifecycleLogs(false);
 const auto completeGuide=ymcc::logexport::aiReadingGuide();const auto& guide=completeGuide["retainedSessionLogRecovery"];
 check(guide["field"]=="sourceLogRecoveryEpoch"&&guide["oldRowsRestored"]==false&&guide["ownedReturnOrInputHealthProven"]==false,"actual-guide-keeps-retained-diagnostic-epoch-separate-from-resource-return");
 return finish();
#endif
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 4;}}
