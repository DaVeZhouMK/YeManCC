#include <condition_variable>
#include <deque>
#include <functional>
#include "touch_haptic_dispatch.h"
@@ACTUAL_KIND@@
@@ACTUAL_LIMIT@@
static std::atomic<bool> g_exitRequested{false};
static std::mutex g_gamepadSerialMx;
static std::condition_variable g_gamepadSerialCv;
static std::deque<std::function<void()>> g_gamepadSerialQ;
static bool g_gamepadSerialStopping=false;
static bool gamepadSerialStartLocked(){return true;} // Original worker-start fact only;no worker is started here.
@@ACTUAL_SUBMIT@@
static ymcc::touchhaptic::Summary g_touchHapticSummary;
static std::atomic<bool> g_touchHapticSummaryQueued{false};
static std::array<std::atomic<unsigned long long>,static_cast<size_t>(ymcc::touchhaptic::Reason::Count)> g_touchHapticIngressFailures{};
static json* g_touchHapticSummaryTestRows=nullptr;
static constexpr int sourceCensusLimit=2000000000;
static bool measureSource=false,measureCleanup=false,injectIngress=false;
static int allocationsBeforeOffer=-1;
static unsigned ingressAdded=0;
static void sourceBeforeOfferProbe(){
    if(measureSource){allocationsBeforeOffer=sourceCensusLimit-ymcc::testing::allocationFault::countdown.load();}
    ymcc::testing::allocationFault::failAfter(-1);
    if(injectIngress){g_touchHapticIngressFailures[static_cast<size_t>(ymcc::touchhaptic::Reason::SourcePending)].fetch_add(3);ingressAdded+=3;}
}
static void sourceAfterOfferProbe(){if(measureCleanup)ymcc::testing::allocationFault::failAfter(sourceCensusLimit);}
@@ACTUAL_SOURCE@@
static json summarySourceEvidence(){json availability;@@ACTUAL_SOURCE_EXPORT@@
    return availability;}
static bool sameSummary(const ymcc::touchhaptic::Summary& a,const ymcc::touchhaptic::Summary& b){return
    a.sent==b.sent&&a.dropped==b.dropped&&a.failed==b.failed&&a.xinput==b.xinput&&a.msi==b.msi&&a.minimal==b.minimal&&a.medium==b.medium&&a.strong==b.strong&&
    a.outputBaselineAttempts==b.outputBaselineAttempts&&a.outputBaselineAccepted==b.outputBaselineAccepted&&a.attemptedFailures==b.attemptedFailures&&
    a.notAttemptedFailures==b.notAttemptedFailures&&a.unknownAttemptFailures==b.unknownAttemptFailures&&a.reasons==b.reasons&&a.errors==b.errors;}
static unsigned long long ingressNow(){return g_touchHapticIngressFailures[static_cast<size_t>(ymcc::touchhaptic::Reason::SourcePending)].load();}
static void seedSummary(){
    ymcc::testing::allocationFault::failAfter(-1);std::deque<std::function<void()>>{}.swap(g_gamepadSerialQ);g_touchHapticSummaryQueued=false;g_touchHapticSummary={};for(auto& n:g_touchHapticIngressFailures)n=0;
    g_touchHapticSummary.record(true,false);g_touchHapticSummary.record(true,false);g_touchHapticSummary.record(true,true,ymcc::touchhaptic::kStrongStrengthPercent);
    ymcc::touchhaptic::Receipt failed;failed.reason=ymcc::touchhaptic::Reason::WriteFailed;failed.error=5;failed.attempted=true;failed.outputBaselineAttempted=true;g_touchHapticSummary.record(failed);
    g_touchHapticSummary.failure(ymcc::touchhaptic::Reason::SourceChanged,false,ymcc::touchhaptic::FailureAttempt::NotAttempted);
    g_touchHapticSummary.failure(ymcc::touchhaptic::Reason::QueueFull,true);g_touchHapticIngressFailures[static_cast<size_t>(ymcc::touchhaptic::Reason::SourcePending)]=4;
}
static bool drainOne(){if(g_gamepadSerialQ.empty())return false;auto job=std::move(g_gamepadSerialQ.front());g_gamepadSerialQ.pop_front();try{job();return false;}catch(const std::bad_alloc&){return true;}}
static json lastNativeSummary(){const auto rows=readRecoveryRows(fspath::path(g_testRoot));for(auto i=rows.rbegin();i!=rows.rend();++i)if(i->value("event",std::string{})=="gamepad-touch-haptics-summary")return *i;return nullptr;}
static unsigned long long sourceCount(ymcc::logging::TouchHapticSummaryPreparationEvidence::Kind kind){return ymcc::logging::touchHapticSummaryPreparationEvidence().observations[kind].load();}
int wmain(int argc,wchar_t** argv){try{
 if(argc<2||argc>3)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();const std::wstring mode=argc==3?argv[2]:L"all";const auto wanted=[&](const wchar_t* selected){return mode==L"all"||mode==selected;};
 (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);json payload=nullptr,matrix=json::array();unsigned sourceCalls=0;
 using SourceFailure=ymcc::logging::TouchHapticSummaryPreparationEvidence;
 if(wanted(L"construction")){
    seedSummary();const auto original=g_touchHapticSummary;nativeTouchHapticSummaryEnd();ymcc::testing::allocationFault::failAfter(0);const bool thrown=drainOne();ymcc::testing::allocationFault::failAfter(-1);
    check(thrown&&g_gamepadSerialQ.empty()&&!g_touchHapticSummaryQueued.load(),"actual-first-source-allocation-failure-propagates-through-original-manual-consumer");
    check(sameSummary(original,g_touchHapticSummary)&&ingressNow()==4,"source-allocation-exception-retains-original-summary-and-ingress-counts");
 }
 if(wanted(L"submit")){
    seedSummary();const auto original=g_touchHapticSummary;bool thrown=false;ymcc::testing::allocationFault::failAfter(0);try{nativeTouchHapticSummaryEnd();}catch(const std::bad_alloc&){thrown=true;}ymcc::testing::allocationFault::failAfter(-1);
    check(thrown&&g_gamepadSerialQ.empty()&&sameSummary(original,g_touchHapticSummary)&&ingressNow()==4,"real-original-deque-push-allocation-exception-keeps-unexecuted-counts");
    check(!g_touchHapticSummaryQueued.load(),"submission-exception-releases-only-original-summary-queued-latch");
 }
 if(wanted(L"normal")){
    seedSummary();nativeTouchHapticSummaryEnd();const bool thrown=drainOne();closeStreams();payload=lastNativeSummary();
    check(!thrown&&payload.is_object()&&payload["sent"]==3&&payload["failed"]==6&&payload["dropped"]==1&&payload["failureAttemptEvidence"]["attempted"]==1&&payload["failureAttemptEvidence"]["notAttempted"]==5,"actual-original-serial-submit-source-and-Native-writer-retain-unchanged-full-payload");
    check(g_touchHapticSummary.empty()&&ingressNow()==0&&!g_touchHapticSummaryQueued.load(),"original-offer-boundary-consumes-only-materialized-counters");
 }
#ifndef YMCC_TOUCH_SUMMARY_PREPARATION_OLD_ONLY
 if(wanted(L"matrix")){
    json capture=json::array();g_touchHapticSummaryTestRows=&capture;seedSummary();nativeTouchHapticSummaryEnd();allocationsBeforeOffer=-1;measureSource=true;measureCleanup=true;ymcc::testing::allocationFault::failAfter(sourceCensusLimit);const bool thrown=drainOne();const int afterOffer=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);measureSource=measureCleanup=false;
    check(!thrown&&allocationsBeforeOffer>0&&allocationsBeforeOffer<2048&&afterOffer==sourceCensusLimit&&capture.size()==1,"actual-source-allocation-census-and-fixed-row-cleanup-consume-no-post-offer-allocation");
    sourceCalls=static_cast<unsigned>(allocationsBeforeOffer);const auto expected=capture.back();bool retained=true,reoffered=true,optional=true;unsigned failedMandatory=0,omitted=0;
    for(unsigned index=0;index<sourceCalls;++index){
        seedSummary();const auto original=g_touchHapticSummary;const auto optionalBefore=sourceCount(SourceFailure::attemptEvidenceOmission),materialBefore=sourceCount(SourceFailure::materializationOrOfferException);const auto rowsBefore=capture.size();nativeTouchHapticSummaryEnd();ymcc::testing::allocationFault::failAfter(static_cast<int>(index));const bool failed=drainOne();ymcc::testing::allocationFault::failAfter(-1);
        if(failed){++failedMandatory;retained&=sameSummary(original,g_touchHapticSummary)&&ingressNow()==4&&!g_touchHapticSummaryQueued.load()&&capture.size()==rowsBefore&&sourceCount(SourceFailure::materializationOrOfferException)==materialBefore+1;
            nativeTouchHapticSummaryEnd();const bool again=drainOne();reoffered&=!again&&capture.size()==rowsBefore+1&&capture.back()==expected&&g_touchHapticSummary.empty()&&ingressNow()==0;
        }else{++omitted;optional&=sourceCount(SourceFailure::attemptEvidenceOmission)==optionalBefore+1&&capture.size()==rowsBefore+1&&!capture.back().contains("failureAttemptEvidence")&&capture.back()["failed"]==6&&g_touchHapticSummary.empty()&&ingressNow()==0;}
        matrix.push_back({{"allocationIndex",index},{"exceptionPropagated",failed},{"retainedBeforeReoffer",retained},{"optionalOmissionCoherent",optional}});
    }
    check(retained&&failedMandatory>0,"each-mandatory-source-allocation-failure-retains-all-original-counters");check(reoffered,"next-original-end-reoffers-retained-facts-once-without-replaying-pulses");check(optional&&omitted>0&&omitted+failedMandatory==sourceCalls,"original-optional-attempt-partition-omission-stays-explicit-and-keeps-primary-counts");g_touchHapticSummaryTestRows=nullptr;
 }
 if(wanted(L"ingress")){
    json capture=json::array();g_touchHapticSummaryTestRows=&capture;seedSummary();injectIngress=true;ingressAdded=0;nativeTouchHapticSummaryEnd();const bool first=drainOne();injectIngress=false;const auto remaining=ingressNow();nativeTouchHapticSummaryEnd();const bool second=drainOne();
    check(!first&&!second&&ingressAdded==3&&remaining==3&&ingressNow()==0&&capture.size()==2&&capture[0]["failed"]==6&&capture[1]["failed"]==3,"ingress-increment-between-snapshot-and-original-offer-is-retained-for-next-summary");g_touchHapticSummaryTestRows=nullptr;
 }
 if(wanted(L"refusal")){
    seedSummary();const auto original=g_touchHapticSummary;const auto refused=sourceCount(SourceFailure::submitRefused);for(size_t i=0;i<GAMEPAD_SERIAL_QUEUE_LIMIT;++i)g_gamepadSerialQ.push_back([]{});nativeTouchHapticSummaryEnd();
    check(!g_touchHapticSummaryQueued.load()&&sameSummary(original,g_touchHapticSummary)&&ingressNow()==4&&g_gamepadSerialQ.size()==GAMEPAD_SERIAL_QUEUE_LIMIT&&sourceCount(SourceFailure::submitRefused)==refused+1,"original-full-queue-refusal-never-runs-or-clears-summary-counters");g_gamepadSerialQ.clear();
    g_exitRequested=true;nativeTouchHapticSummaryEnd();g_exitRequested=false;check(!g_touchHapticSummaryQueued.load()&&sameSummary(original,g_touchHapticSummary)&&ingressNow()==4&&sourceCount(SourceFailure::submitRefused)==refused+2,"original-exit-refusal-keeps-counts-and-distinct-refusal-observation");
 }
 if(wanted(L"native-offer")){
    seedSummary();closeStreams();g_testRoot=(root/L"missing-parent").wstring();const auto failures=count(Failure::eventOpen);nativeTouchHapticSummaryEnd();const bool thrown=drainOne();
    check(!thrown&&g_touchHapticSummary.empty()&&ingressNow()==0&&count(Failure::eventOpen)==failures+1,"original-Native-offer-still-has-no-delivery-acknowledgement-or-device-retry");g_testRoot=root.wstring();closeStreams();
 }
 if(mode==L"all")check(sourceCount(SourceFailure::materializationOrOfferException)>0,"materialization-failures-remain-distinct-from-submission-and-refusal-observations");
#endif
 ymcc::testing::allocationFault::failAfter(-1);g_gamepadSerialQ.clear();closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-original-file-and-serial-port-handles-return");unsigned failed=0;for(const auto& q:checks)failed+=!q["pass"].get<bool>();
 for(const char* key:{"time","event","pid","hwnd","powerGeneration","powerLifecycle","writerProcessCreation100ns"})if(payload.is_object())payload.erase(key);
 std::cout<<json{{"mode",mode==L"all"?"all":mode==L"construction"?"construction":mode==L"submit"?"submit":mode==L"normal"?"normal":"selected"},{"checks",checks},{"failures",failed},{"sourceCalls",sourceCalls},{"matrix",matrix},{"payload",payload},{"sourceEvidence",summarySourceEvidence()},{"handlesBefore",before},{"handlesAfter",after},{"actualOriginalSerialSubmit",true},{"manualConsumerNoProductWorkerStarted",true},{"single-thread-atomic-interleaving-not-live-UI",true},{"nativeDeliveryAcknowledged",false},{"physicalActions",0}}.dump(2)<<std::endl;return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<std::endl;return 4;}}
