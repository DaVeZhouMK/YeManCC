#include "log_ai_index.h"
@@ACTUAL_SOURCE@@
static unsigned fixtureBuilds=0;
static bool fixtureBuildThrow=false;
static json quarantinePayload(const ymcc::logging::FrameEvidenceView& view,unsigned producer){
 ++fixtureBuilds;if(fixtureBuildThrow)throw std::bad_alloc();
 json row={{"runId",std::string(view.text[0])},{"hostInstanceId",std::string(view.text[1])},
  {"epoch",view.numbers[0]},{"producerPowerGeneration",view.numbers[1]},
  {"hostTupleKnown",true},{"pXinput","local-readback-only"}};
 if(producer==2)row["reason"]="wake-hold";
 else {
  row["virtualEndpointAbsent"]=view.flags[0];row["xinputPlaneAbsent"]=view.flags[1];
  row["msiSdDebtRequired"]=view.flags[2];row["connectedMask"]=view.numbers[2];
  row["physicalSourceMask"]=view.numbers[3];row["virtualEndpointAbsentScope"]="xinput-plane";
  if(producer==1){row["owner"]="capture-lifecycle-thread";row["ownedSdReturnCandidate"]=false;}
 }
 return row;
}
static json quarantineRows(const fspath::path& root,bool detail=false){
 std::ifstream input(root/(detail?L"native-detail.log":L"native-lifecycle.log"));json rows=json::array();std::string line;
 while(std::getline(input,line))if(!line.empty()){auto row=json::parse(line);const auto event=row.value("event",std::string{});if(event=="input-host-quarantine-endpoint-probe"||event=="input-host-quarantine-restore-held")rows.push_back(std::move(row));}
 return rows;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();(void)ymcc::sleepfactlog::writerIdentity();
 DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
 const auto finish=[&]{closeStreams();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;check(before==after,"all-owned-file-handles-returned-before-exit");unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"actualOriginalSourceHelper",true},{"sourceProofAndTargetActionsExecuted",false},{"clockCostMeasurement",false}}.dump(2)<<'\n';return failures?2:0;};
 std::string run="fixture-run",host="fixture-host";
 ymcc::logging::FrameEvidenceView bus{{run,host,"xinput-plane","BUS"},{1,1,1,1,0},{false,false,false}};
 auto lifecycle=bus;lifecycle.text[3]="lifecycle";auto held=bus;held.text[2]="wake-hold";held.text[3]="";
 const auto offerBus=[&]{inputHostLogQuarantineObservationLocked(g_inputHostQuarantineBusLogState,bus,"input-host-quarantine-endpoint-probe",[&]{return quarantinePayload(bus,0);});};
 const auto offerLifecycle=[&]{inputHostLogQuarantineObservationLocked(g_inputHostQuarantineLifecycleLogState,lifecycle,"input-host-quarantine-endpoint-probe",[&]{return quarantinePayload(lifecycle,1);});};
 const auto offerHeld=[&]{inputHostLogQuarantineObservationLocked(g_inputHostQuarantineHeldLogState,held,"input-host-quarantine-restore-held",[&]{return quarantinePayload(held,2);});};
 g_domainLogEnabled[1]=false;offerBus();offerLifecycle();offerHeld();flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==3&&fixtureBuilds==3,"original-three-producer-first-states-remain-resident-with-detail-OFF");
 const auto stableBuilds=fixtureBuilds;const auto stableWrites=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i){offerBus();offerLifecycle();offerHeld();}const auto stableArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(stableArm==0&&fixtureBuilds==stableBuilds&&g_nativeLifecycleFrameWrites==stableWrites,"100000-alternating-three-source-observations-stay-silent-and-allocation-free");
 closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();++bus.numbers[0];const auto openBefore=count(Failure::eventOpen);offerBus();
 check(count(Failure::eventOpen)==openBefore+1&&g_inputHostQuarantineBusLogState.known,"actual-original-file-open-failure-leaves-source-memo-known");
 const auto failedBuilds=fixtureBuilds;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)offerBus();const auto failedArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(failedArm==0&&fixtureBuilds==failedBuilds,"unresolved-file-failure-does-not-create-source-retry-storm");
 const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"quarantineLogRecovery",1}});flushNativeLifecycleLogs(false);
 check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>epoch,"only-original-successful-resident-append-advances-recovery-epoch");
 const auto recoveredBefore=quarantineRows(root).size();offerBus();flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==recoveredBefore+1,"same-current-BUS-state-reoffered-resident-after-confirmed-log-recovery");
#ifdef YMCC_QUARANTINE_SOURCE_OLD_ONLY
 return finish();
#else
 offerLifecycle();offerHeld();flushNativeLifecycleLogs(false);auto rows=quarantineRows(root);const auto currentEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
 check(rows.size()==recoveredBefore+3&&rows[rows.size()-3]["sourceLogRecoveryEpoch"]==currentEpoch&&rows[rows.size()-2]["sourceLogRecoveryEpoch"]==currentEpoch&&rows.back()["sourceLogRecoveryEpoch"]==currentEpoch,"all-three-independent-source-memos-reoffer-with-current-diagnostic-epoch");
 const auto onceBuilds=fixtureBuilds;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i){offerHeld();offerLifecycle();offerBus();}const auto onceArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(onceArm==0&&fixtureBuilds==onceBuilds&&quarantineRows(root).size()==rows.size(),"each-source-reoffers-once-and-returns-to-quiet-original-OFF-path");
 g_domainLogEnabled[1]=true;const auto detailBefore=quarantineRows(root,true).size();for(unsigned i=0;i<5;++i){offerBus();offerLifecycle();offerHeld();}flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==rows.size()&&quarantineRows(root,true).size()==detailBefore+15,"detail-ON-keeps-original-repeats-without-extra-resident-marker-transition");
 g_domainLogEnabled[1]=false;++lifecycle.numbers[0];timestampAllocationFault=true;const auto prepareBefore=count(Failure::preparationException);offerLifecycle();timestampAllocationFault=false;
 check(count(Failure::preparationException)==prepareBefore+1&&ymcc::logging::nativeLogFailureEvidence().residentRecordPreparationFailurePending.load(),"actual-original-Native-preparation-failure-is-retained-separately");
 appendNativeLifecycleLog("session-clean-exit",{{"quarantineLogRecovery",2}});const auto prepRows=quarantineRows(root).size();offerLifecycle();flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==prepRows+1&&quarantineRows(root).back()["sourceLogRecoveryEpoch"]==ymcc::logging::nativeLogFailureEvidence().recoveryEpoch(),"same-lifecycle-source-reoffered-after-original-preparation-recovery");
 offerBus();offerHeld();const auto detailOnlyBuilds=fixtureBuilds;const auto detailEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail-only");appendNativeLifecycleLog("session-clean-exit",{{"detailOnly",true}});offerBus();offerLifecycle();offerHeld();
 check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==detailEpoch&&fixtureBuilds==detailOnlyBuilds,"detail-only-failure-does-not-rearm-resident-source-memos");
 ++held.numbers[0];fixtureBuildThrow=true;const auto throwBuilds=fixtureBuilds;offerHeld();fixtureBuildThrow=false;
 check(!g_inputHostQuarantineHeldLogState.known&&fixtureBuilds==throwBuilds+1,"original-builder-exception-forgets-only-that-source-without-escaping");
 const auto catchRows=quarantineRows(root).size();offerHeld();flushNativeLifecycleLogs(false);
 check(g_inputHostQuarantineHeldLogState.known&&quarantineRows(root).size()==catchRows+1,"next-original-held-offer-can-record-after-builder-exception");
 ++g_powerGeneration;++bus.numbers[1];++lifecycle.numbers[1];++held.numbers[1];const auto generationRows=quarantineRows(root).size();offerBus();offerLifecycle();offerHeld();flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==generationRows+3,"original-power-and-source-context-transitions-remain-resident");
 appendNativeLifecycleLog("input-host-quarantine-cleared",{{"runId",run},{"epoch",1}});g_inputHostQuarantineBusLogState.known=false;g_inputHostQuarantineLifecycleLogState.known=false;g_inputHostQuarantineHeldLogState.known=false;
 const auto resetRows=quarantineRows(root).size();offerBus();offerLifecycle();offerHeld();flushNativeLifecycleLogs(false);
 check(quarantineRows(root).size()==resetRows+3,"original-clear-and-known-reset-rearm-all-three-source-slots");
 rows=quarantineRows(root);bool retained=true;for(const auto& row:rows)retained&=row["runId"]==run&&row["hostInstanceId"]==host&&row["hostTupleKnown"]==true&&row["pXinput"]=="local-readback-only";
 check(retained,"original-tuple-and-local-readback-not-physical-consumption-proof-fields-retained");
 const auto completeGuide=ymcc::logexport::aiReadingGuide();const auto& guide=completeGuide["quarantineSourceLogRecovery"];
 check(guide["field"]=="sourceLogRecoveryEpoch"&&guide["oldRowsRestored"]==false&&guide["targetReturnOrDeviceHealthProven"]==false,"actual-guide-distinguishes-diagnostic-epoch-from-source-or-target-recovery");
 return finish();
#endif
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 4;}}
