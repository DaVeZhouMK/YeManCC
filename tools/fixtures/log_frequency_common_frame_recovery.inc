#include <Xinput.h>
#include "log_ai_index.h"
struct CommonFrameSample {
 std::string inputSource="xinput";bool live=true,readContextStable=true,specialXbox=false;
 unsigned long long readTopologyGeneration=7,readPowerGeneration=1,readAttemptSequence=1,readStartedTick=100,readCompletedTick=100,readMsiSourceRevision=0,readMsiModeGeneration=0,physicalReaderSequence=0;
 unsigned primarySlot=0,connectedMask=1,physicalReaderPid=0,physicalReaderPacket=0,physicalReaderActualSlot=0;
 unsigned long long commonHidReportSequence=0,commonHidObservedTick=0;
 bool commonHidIndependentTriggers=false,commonHidCompleteBasic=false,commonHidIsolationSharedRead=false;
 unsigned commonHidCombinedTriggerRaw=0;
 XINPUT_GAMEPAD pad{};
 XINPUT_GAMEPAD physicalReaderRawPad{};
};
static const CommonFrameSample* t_commonFrameSource=nullptr;
static unsigned long long g_commonFrameEvidenceRows=0,g_commonFrameEvidenceFailures=0;
static std::mutex g_hidHidePhysicalIdentityMx;
static struct {bool resolved=false;} g_hidHidePhysicalIdentity;
static unsigned fixtureIdentityCalls=0;
static json hidHideIdentityJson(const decltype(g_hidHidePhysicalIdentity)&){++fixtureIdentityCalls;return {{"fixture",true}};}
static struct {int family=0;std::string product="fixture-board";} g_machineIdentity;
static const char* ymccFamilyName(int){return "gpd";}
static std::atomic<unsigned long long> g_xinputTopologyEpoch{7};
static ULONGLONG fixtureTick=1000;
static ULONGLONG commonFrameFixtureTick(){return fixtureTick;}
#define GetTickCount64 commonFrameFixtureTick
@@PRODUCER@@
#undef GetTickCount64
static json readDetailRows(const fspath::path& root){
 std::ifstream input(root/L"native-detail.log");json rows=json::array();std::string line;while(std::getline(input,line))if(!line.empty())rows.push_back(json::parse(line));return rows;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);fspath::create_directories(root);g_testRoot=root.wstring();(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
 CommonFrameSample sample;t_commonFrameSource=&sample;
 const json frame={{"hostSubmission","frame-accepted"},{"frameProof",{{"hostReceipt",{{"epoch",1ULL},{"hostInstanceId","host"},{"runId","run"}}}}}};
 const auto initialFailures=g_commonFrameEvidenceFailures;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)inputCaptureCommonFrameEvidence(frame,false);const auto offArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(offArm==0&&g_commonFrameEvidenceRows==0&&g_commonFrameEvidenceFailures==initialFailures&&!g_nativeLifecycleLog.is_open(),"virtual-detail-OFF100000-source-calls-stay-silent-and-allocation-free");
 g_domainLogEnabled[1]=true;inputCaptureCommonFrameEvidence(frame,false);flushNativeLifecycleLogs(false);auto rows=readRecoveryRows(root);
 check(rows.size()==1&&rows.back()["snapshotReason"]=="state-change"&&rows.back()["source"]["inputSource"]=="xinput","original-first-common-frame-evidence-is-resident");
 const auto writesBefore=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)inputCaptureCommonFrameEvidence(frame,i%2!=0);const auto hotArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(hotArm==0&&g_nativeLifecycleFrameWrites==writesBefore&&g_commonFrameEvidenceRows==1,"unchanged100000-frame-and-neutrality-offers-keep-original-quiet-hot-path");
 closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();++sample.readTopologyGeneration;const auto failBefore=count(Failure::eventOpen);inputCaptureCommonFrameEvidence(frame,false);
 check(count(Failure::eventOpen)==failBefore+1&&ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"actual-changed-frame-reaches-original-file-open-failure");
 g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"commonFrameRecovery",1}});const auto residentBefore=readRecoveryRows(root).size();inputCaptureCommonFrameEvidence(frame,false);flushNativeLifecycleLogs(false);rows=readRecoveryRows(root);
 bool recovered=rows.size()==residentBefore+1&&rows.back()["event"]=="input-host-frame-evidence"&&rows.back()["snapshotReason"]=="state-change";
#ifndef YMCC_COMMON_FRAME_OLD_ONLY
 recovered=recovered&&rows.back()["diagnosticRecoveryReoffer"]==true;
#endif
 check(recovered,"same-current-frame-reoffered-resident-after-original-successful-event-recovery");
#ifndef YMCC_COMMON_FRAME_OLD_ONLY
 const auto recoveryRows=g_commonFrameEvidenceRows;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)inputCaptureCommonFrameEvidence(frame,false);const auto recoveredArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(recoveredArm==0&&g_commonFrameEvidenceRows==recoveryRows,"successful-recovery-is-offered-once-without-new-frame-allocations");
 fixtureTick=1999;inputCaptureCommonFrameEvidence(frame,true);check(g_commonFrameEvidenceRows==recoveryRows,"original-one-Hz-sampling-window-not-shortened");
 fixtureTick=2000;inputCaptureCommonFrameEvidence(frame,true);flushNativeLifecycleLogs(false);auto detailed=readDetailRows(root);
 check(detailed.size()==1&&detailed.back()["snapshotReason"]=="periodic"&&detailed.back()["diagnosticRecoveryReoffer"]==false&&detailed.back()["semanticNeutral"]==true&&readRecoveryRows(root).size()==rows.size(),"original-periodic-neutrality-sample-remains-detailed");
 ++sample.readPowerGeneration;++g_powerGeneration;inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.back()["snapshotReason"]=="state-change"&&rows.back()["diagnosticRecoveryReoffer"]==false&&rows.back()["source"]["powerGeneration"]==2,"real-source-generation-transition-keeps-original-meaning");
 ++sample.readTopologyGeneration;timestampAllocationFault=true;const auto preparationBefore=count(Failure::preparationException);inputCaptureCommonFrameEvidence(frame,false);timestampAllocationFault=false;
 check(count(Failure::preparationException)==preparationBefore+1&&ymcc::logging::nativeLogFailureEvidence().residentRecordPreparationFailurePending.load(),"generic-Native-record-preparation-failure-is-actual-and-separate");
 appendNativeLifecycleLog("session-clean-exit",{{"commonFrameRecovery",2}});const auto prepRows=readRecoveryRows(root).size();inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.size()==prepRows+1&&rows.back()["diagnosticRecoveryReoffer"]==true,"same-source-also-reoffered-after-original-Native-preparation-recovery");
 closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();appendNativeLifecycleLog("session-clean-exit",{{"commonFrameFailure",3}});g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"commonFrameRecovery",3}});
 g_domainLogEnabled[1]=false;const auto offRows=g_commonFrameEvidenceRows;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)inputCaptureCommonFrameEvidence(frame,false);const auto offEpochArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(offEpochArm==0&&g_commonFrameEvidenceRows==offRows,"log-recovery-does-not-bypass-virtual-detail-OFF-gate");
 g_domainLogEnabled[1]=true;const auto reenabled=readRecoveryRows(root).size();inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.size()==reenabled+1&&rows.back()["diagnosticRecoveryReoffer"]==true,"original-virtual-gate-ON-can-offer-current-state-after-confirmed-recovery");
 const auto errors=g_commonFrameEvidenceFailures;const json malformed={{"frameProof",false}};inputCaptureCommonFrameEvidence(malformed,false);inputCaptureCommonFrameEvidence(json::array(),false);
 check(g_commonFrameEvidenceFailures==errors+2&&readRecoveryRows(root).size()==rows.size(),"malformed-original-frame-is-counted-without-claiming-a-file-failure");
 sample.inputSource="msi-dinput";sample.readMsiSourceRevision=8;sample.readMsiModeGeneration=9;inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.back()["source"]["msiProducerReadProofVersion"]==1&&rows.back()["source"]["producerSourceRevision"]==8&&rows.back()["source"]["producerModeGeneration"]==9,"original-MSI-source-proof-fields-retained");
 sample.physicalReaderPid=37;sample.physicalReaderSequence=123;sample.physicalReaderRawPad.wButtons=19;++sample.readTopologyGeneration;inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.back()["source"]["physicalReader"]["pid"]==37&&rows.back()["source"]["physicalReader"]["rawPad"]["buttons"]==19&&rows.back()["physicalIdentitySampleBindingProven"]==false&&rows.back()["steamConsumptionProven"]==false,"original-physical-reader-payload-and-unproven-consumption-boundaries-retained");
 t_commonFrameSource=nullptr;inputCaptureCommonFrameEvidence(frame,false);rows=readRecoveryRows(root);
 check(rows.back()["source"]["snapshotKnown"]==false&&rows.back()["snapshotReason"]=="state-change","original-missing-source-transition-remains-visible");
 CommonFrameSample idle;idle.inputSource="none";idle.live=false;idle.connectedMask=0;t_commonFrameSource=&idle;
 const json disabled={{"hostSubmission","publication-suppressed"},{"publicationReason","virtual-target-disabled"},{"hostActive",false}};
 const auto beforeIdle=rows.size();fixtureTick=3000;inputCaptureCommonFrameEvidence(disabled,false);rows=readRecoveryRows(root);
 check(rows.size()==beforeIdle+1&&rows.back()["snapshotReason"]=="state-change","first-confirmed-disabled-no-source-observation-is-resident");
 fixtureTick=3999;const auto idleRows=g_commonFrameEvidenceRows;const auto idleErrors=g_commonFrameEvidenceFailures;
 ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i){++idle.readTopologyGeneration;inputCaptureCommonFrameEvidence(disabled,false);}const auto idleArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(idleArm==0&&g_commonFrameEvidenceRows==idleRows&&g_commonFrameEvidenceFailures==idleErrors,"50000-none-topology-advances-within-window-do-not-allocate-or-write");
 const auto oldDetail=readDetailRows(root).size();fixtureTick=4000;inputCaptureCommonFrameEvidence(disabled,false);flushNativeLifecycleLogs(false);detailed=readDetailRows(root);
 check(readRecoveryRows(root).size()==rows.size()&&detailed.size()==oldDetail+1&&detailed.back()["snapshotReason"]=="periodic"&&detailed.back()["source"]["inactiveTopologySample"]==true&&detailed.back()["source"]["topologyGeneration"]==idle.readTopologyGeneration,"due-sample-of-current-inactive-topology-uses-detail-file");
 const auto expectResident=[&](const json& observation,const char* name){const auto count=readRecoveryRows(root).size();inputCaptureCommonFrameEvidence(observation,false);const auto result=readRecoveryRows(root);check(result.size()==count+1&&result.back()["snapshotReason"]=="state-change"&&!result.back()["source"].contains("inactiveTopologySample"),name);};
 --idle.readTopologyGeneration;expectResident(disabled,"topology-regression-retains-resident-evidence");
 ++idle.readTopologyGeneration;++idle.readPowerGeneration;expectResident(disabled,"power-generation-change-is-resident-without-waiting");
 ++idle.readTopologyGeneration;idle.readContextStable=false;expectResident(disabled,"unstable-read-is-resident-without-waiting");
 idle.readContextStable=true;idle.live=true;idle.inputSource="xinput";idle.connectedMask=1;expectResident(disabled,"source-admission-transition-remains-resident");
 ++idle.readTopologyGeneration;expectResident(disabled,"live-physical-source-topology-change-remains-resident");
 idle.live=false;idle.inputSource="none";idle.connectedMask=0;
 auto active=disabled;active["hostActive"]=true;inputCaptureCommonFrameEvidence(active,false);++idle.readTopologyGeneration;expectResident(active,"active-host-in-disabled-setting-is-not-downgraded");
 auto other=disabled;other["publicationReason"]="unknown";inputCaptureCommonFrameEvidence(other,false);++idle.readTopologyGeneration;expectResident(other,"other-suppression-reasons-are-not-downgraded");
 inputCaptureCommonFrameEvidence(disabled,false);closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();appendNativeLifecycleLog("session-clean-exit",{{"commonFrameFailure",4}});g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"commonFrameRecovery",4}});
 const auto preReplay=readRecoveryRows(root).size();++idle.readTopologyGeneration;inputCaptureCommonFrameEvidence(disabled,false);rows=readRecoveryRows(root);
 check(rows.size()==preReplay+1&&rows.back()["diagnosticRecoveryReoffer"]==true&&rows.back()["snapshotReason"]=="state-change"&&!rows.back()["source"].contains("inactiveTopologySample"),"resident-recovery-reoffers-even-when-only-topology-version-advanced");
 auto held=disabled;held["publicationReason"]="backend-fault-hold";
 expectResident(held,"same-outcome-with-a-different-publication-cause-is-resident");
 rows=readRecoveryRows(root);
 check(rows.back()["publicationReason"]=="backend-fault-hold"&&rows.back()["hostActive"]==false&&rows.back()["steamConsumptionProven"]==false,"publication-cause-and-explicit-inactive-stay-local-to-this-observation");
 held["hostActive"]=true;expectResident(held,"changed-reported-host-active-flag-is-resident");
 auto malformedPublication=held;malformedPublication["publicationReason"]="unrecognized";malformedPublication["hostActive"]=1;
 expectResident(malformedPublication,"unrecognized-publication-context-breaks-known-context");
 rows=readRecoveryRows(root);
 check(rows.back()["publicationReason"].is_null()&&rows.back()["hostActive"].is_null(),"unrecognized-reason-and-nonboolean-host-state-are-unknown");
 expectResident(held,"known-publication-context-returns-as-an-edge");
 rows=readRecoveryRows(root);const auto beforeHeldRows=g_commonFrameEvidenceRows;
 ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)inputCaptureCommonFrameEvidence(held,false);const auto heldArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(heldArm==0&&g_commonFrameEvidenceRows==beforeHeldRows&&readRecoveryRows(root).size()==rows.size(),"stable-publication-context-skips-allocation-and-log-writes-before-window");
 const auto beforeHeldDetail=readDetailRows(root).size();fixtureTick+=1000;
 inputCaptureCommonFrameEvidence(held,false);flushNativeLifecycleLogs(false);detailed=readDetailRows(root);
 check(readRecoveryRows(root).size()==rows.size()&&detailed.size()==beforeHeldDetail+1&&detailed.back()["publicationReason"]=="backend-fault-hold"&&detailed.back()["hostActive"]==true,"stable-publication-context-keeps-the-original-detailed-sample-window");
 const auto completeGuide=ymcc::logexport::aiReadingGuide();const auto& guide=completeGuide["commonFrameLogRecovery"];
 check(guide["field"]=="diagnosticRecoveryReoffer"&&guide["oldRowRestored"]==false&&guide["sourceTransitionOrDeviceRecoveryProven"]==false,"actual-guide-distinguishes-log-reoffer-from-source-or-device-recovery");
 const auto& periodicScope=guide.at("periodicScope");
 const auto marker=periodicScope.at("markerField").get<std::string>();
 const auto isCompact=[&](const json& row){
  const auto& source=row.at("source");
  return row.at("snapshotReason")=="periodic"&&row.at("formatVersion")==2&&row.at(marker)==periodicScope.at("markerValue")&&
      !row.contains("transitionScope")&&!source.contains("identityBindingScope")&&!source.contains("freshnessScope");
 };
 check(isCompact(detailed.back())&&detailed.back().at("source").at("snapshotKnown")==true,"periodic-frame-scopes-are-in-guide-and-source-facts-stay-inline");
 check(!rows.back().contains(marker)&&rows.back().at("transitionScope")==periodicScope.at("transitionScope")&&
     rows.back().at("source").at("identityBindingScope")==periodicScope.at("sourceIdentityBindingScope")&&
     rows.back().at("source").at("freshnessScope")==periodicScope.at("sourceFreshnessScope").at("other"),"resident-scope-stays-inline-and-matches-guide");
 idle.inputSource="msi-dinput";idle.live=true;idle.readMsiSourceRevision=7;idle.readMsiModeGeneration=8;
 expectResident(held,"MSI-read-source-change-stays-resident");
 rows=readRecoveryRows(root);
 check(rows.back().at("source").at("freshnessScope")==periodicScope.at("sourceFreshnessScope").at("msi-dinput"),"MSI-resident-freshness-convention-remains-exact");
 fixtureTick+=1000;inputCaptureCommonFrameEvidence(held,false);flushNativeLifecycleLogs(false);detailed=readDetailRows(root);
 check(isCompact(detailed.back())&&detailed.back().at("source").at("producerSourceRevision")==7&&detailed.back().at("source").at("producerModeGeneration")==8,"MSI-periodic-keeps-producer-binding");
 t_commonFrameSource=nullptr;expectResident(held,"missing-source-remains-a-resident-change");
 fixtureTick+=1000;inputCaptureCommonFrameEvidence(held,false);flushNativeLifecycleLogs(false);detailed=readDetailRows(root);
 check(isCompact(detailed.back())&&detailed.back().at("source").at("snapshotKnown")==false&&!detailed.back().at("source").contains("inputSource"),"periodic-missing-source-stays-unknown-with-no-freshness-observation");
#endif
 closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-file-handles-returned-before-exit");unsigned failed=0;for(const auto& item:checks)failed+=!item["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"sourceFunctionExecuted",true},{"controlledClockLeaf",true},{"deviceOrSteamActions",0},{"identityQueryPortCalls",fixtureIdentityCalls}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
