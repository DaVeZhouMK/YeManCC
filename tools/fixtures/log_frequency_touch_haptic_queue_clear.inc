#include <condition_variable>
#include <deque>
#include <functional>
#include "touch_haptic_dispatch.h"
@@ACTUAL_KIND@@
@@ACTUAL_LIMIT@@
static std::atomic<bool> g_exitRequested{false};
static std::mutex g_gamepadSerialMx,g_gamepadSerialRetireMx;
static std::condition_variable g_gamepadSerialCv;
static std::deque<std::function<void()>> g_gamepadSerialQ;
static bool g_gamepadSerialStopping=false,g_gamepadSerialStarted=false;
static std::thread g_gamepadSerialThread;
static bool gamepadSerialStartLocked(){return true;} // Start admission fact only;no normal worker.
@@ACTUAL_SUBMIT@@
static ymcc::touchhaptic::Summary g_touchHapticSummary;
static std::atomic<bool> g_touchHapticSummaryQueued{false};
static std::array<std::atomic<unsigned long long>,static_cast<size_t>(ymcc::touchhaptic::Reason::Count)> g_touchHapticIngressFailures{};
static json* g_touchHapticSummaryTestRows=nullptr;
static void sourceBeforeOfferProbe(){}
static void sourceAfterOfferProbe(){}
@@ACTUAL_SOURCE@@
@@ACTUAL_QUEUE_CLEAR_HELPER@@
static HANDLE releaseOwnedWorker=nullptr;
static void nativeKeyboardSequenceStop(){if(releaseOwnedWorker)SetEvent(releaseOwnedWorker);}
@@ACTUAL_STOP@@
static void fixtureWorkerClosing(bool closing){std::lock_guard<std::mutex> lock(g_gamepadSerialMx);
@@ACTUAL_CLOSING_BLOCK@@
}
static void fixtureWorkerClaimGuard(){std::lock_guard<std::mutex> lock(g_gamepadSerialMx);for(unsigned i=0;i<1;++i){
@@ACTUAL_CLAIM_REFUSAL@@
}}
static json summarySourceEvidence(){json availability;@@ACTUAL_SOURCE_EXPORT@@
    return availability;}
static unsigned long long pendingClearCount(){return ymcc::logging::touchHapticSummaryPreparationEvidence().observations[ymcc::logging::TouchHapticSummaryPreparationEvidence::queueClearPendingObserved].load();}
static void seedQueueSummary(){g_exitRequested=false;g_gamepadSerialStopping=false;g_gamepadSerialQ.clear();g_touchHapticSummaryQueued=false;g_touchHapticSummary={};for(auto& n:g_touchHapticIngressFailures)n=0;
    g_touchHapticSummary.record(true,false);g_touchHapticSummary.failure(ymcc::touchhaptic::Reason::WriteFailed,false,ymcc::touchhaptic::FailureAttempt::Attempted);g_touchHapticIngressFailures[static_cast<size_t>(ymcc::touchhaptic::Reason::SourcePending)]=3;}
static bool countsKept(){return g_touchHapticSummary.sent==1&&g_touchHapticSummary.failed==1&&g_touchHapticIngressFailures[static_cast<size_t>(ymcc::touchhaptic::Reason::SourcePending)].load()==3;}
static void drainSummary(){auto job=std::move(g_gamepadSerialQ.front());g_gamepadSerialQ.pop_front();job();}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();(void)ymcc::sleepfactlog::writerIdentity();DWORD cold=0,before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&cold);std::thread initialized([]{});initialized.join();GetProcessHandleCount(GetCurrentProcess(),&before);
 seedQueueSummary();unsigned otherExecuted=0;gamepadSerialSubmit([&]{++otherExecuted;});nativeTouchHapticSummaryEnd();const bool admitted=g_touchHapticSummaryQueued.load()&&g_gamepadSerialQ.size()==2;
 releaseOwnedWorker=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!releaseOwnedWorker)throw std::runtime_error("owned worker event failed");g_gamepadSerialThread=std::thread([]{WaitForSingleObject(releaseOwnedWorker,1000);});g_gamepadSerialStarted=true;const bool stopped=gamepadSerialStop(1000);CloseHandle(releaseOwnedWorker);releaseOwnedWorker=nullptr;
 check(admitted&&stopped&&!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted&&g_gamepadSerialStopping&&g_gamepadSerialQ.empty()&&otherExecuted==0&&countsKept(),"actual-original-stop-clears-accepted-queue-and-joins-finite-owned-worker-with-counts-kept");
 check(!g_touchHapticSummaryQueued.load(),"original-stop-queue-clear-rearms-only-touch-summary-latch");
#ifdef YMCC_TOUCH_QUEUE_CLEAR_OLD_ONLY
 closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-stop-worker-event-and-file-handles-return");unsigned failed=0;for(const auto& q:checks)failed+=!q["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"coldHandles",cold},{"handlesBefore",before},{"handlesAfter",after},{"sourceEvidence",summarySourceEvidence()},{"actualOriginalStopAndSerialSubmit",true},{"finiteOwnedWorkerNotProductWorker",true}}.dump(2)<<std::endl;return failed?2:0;
#else
 const auto firstCount=pendingClearCount();nativeTouchHapticSummaryEnd();check(!g_touchHapticSummaryQueued.load()&&g_gamepadSerialQ.empty()&&countsKept()&&pendingClearCount()==firstCount,"existing-stopping-admission-still-refuses-and-preserves-counts");
 g_gamepadSerialStopping=false;nativeTouchHapticSummaryEnd();const bool reentered=g_gamepadSerialQ.size()==1&&g_touchHapticSummaryQueued.load();drainSummary();closeStreams();auto rows=readRecoveryRows(root);check(reentered&&rows.size()==1&&rows[0]["sent"]==1&&rows[0]["failed"]==4&&g_touchHapticSummary.empty()&&!g_touchHapticSummaryQueued.load(),"next-existing-end-after-fixture-opens-admission-reoffers-diagnostic-once-without-pulse-replay");
 seedQueueSummary();nativeTouchHapticSummaryEnd();const auto closingBefore=pendingClearCount();fixtureWorkerClosing(false);const bool kept=!g_gamepadSerialQ.empty()&&g_touchHapticSummaryQueued.load();fixtureWorkerClosing(true);fixtureWorkerClosing(true);check(kept&&g_gamepadSerialQ.empty()&&!g_touchHapticSummaryQueued.load()&&countsKept()&&pendingClearCount()==closingBefore+1,"actual-worker-closing-block-observes-pending-once-and-preserves-nonclosing-queue");
 seedQueueSummary();nativeTouchHapticSummaryEnd();const auto claimBefore=pendingClearCount();fixtureWorkerClaimGuard();const bool notExiting=!g_gamepadSerialQ.empty()&&g_touchHapticSummaryQueued.load();g_exitRequested=true;fixtureWorkerClaimGuard();g_exitRequested=false;check(notExiting&&g_gamepadSerialQ.empty()&&!g_touchHapticSummaryQueued.load()&&countsKept()&&pendingClearCount()==claimBefore+1,"actual-original-exit-CAS-refusal-block-rearms-only-on-its-existing-clear");
 seedQueueSummary();nativeTouchHapticSummaryEnd();auto claimed=std::move(g_gamepadSerialQ.front());g_gamepadSerialQ.pop_front();const auto claimedBefore=pendingClearCount();fixtureWorkerClosing(true);const bool pendingObserved=!g_touchHapticSummaryQueued.load()&&countsKept()&&pendingClearCount()==claimedBefore+1;claimed();closeStreams();check(pendingObserved&&g_touchHapticSummary.empty(),"already-claimed-callback-keeps-original-owner-and-observation-is-not-exact-cancellation-count");
 seedQueueSummary();g_touchHapticSummaryQueued=true;const auto waitingBefore=pendingClearCount();fixtureWorkerClosing(true);check(!g_touchHapticSummaryQueued.load()&&g_gamepadSerialQ.empty()&&countsKept()&&pendingClearCount()==waitingBefore+1,"pending-submitter-with-empty-queue-is-labelled-latch-observation-not-accepted-task");
 const auto emptyBefore=pendingClearCount();fixtureWorkerClosing(true);fixtureWorkerClaimGuard();check(pendingClearCount()==emptyBefore,"no-pending-latch-does-not-create-clear-observation");
 seedQueueSummary();nativeTouchHapticSummaryEnd();ymcc::testing::allocationFault::failAfter(0);fixtureWorkerClosing(true);const bool noAllocation=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);check(noAllocation&&!g_touchHapticSummaryQueued.load()&&countsKept(),"original-clear-plus-atomic-diagnostic-helper-consumes-no-Cpp-allocation");
 const auto key=ymcc::logging::TouchHapticSummaryPreparationEvidence::queueClearPendingObserved;auto& stats=ymcc::logging::touchHapticSummaryPreparationEvidence();stats.observations[key]=~0ULL;g_touchHapticSummaryQueued=true;fixtureWorkerClosing(true);const auto evidence=summarySourceEvidence();check(evidence["touchHapticSummaryPreparationEvidence"]["stats"]["schema"]==2&&evidence["touchHapticSummaryPreparationEvidence"]["stats"]["observations"]["queueClearPendingObserved"]==~0ULL&&evidence["touchHapticSummaryPreparationEvidence"]["stats"]["counterSaturated"]==true,"schema2-clear-observation-saturates-without-changing-four-original-counters");
 closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-stop-worker-event-and-file-handles-return");unsigned failed=0;for(const auto& q:checks)failed+=!q["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"coldHandles",cold},{"handlesBefore",before},{"handlesAfter",after},{"sourceEvidence",evidence},{"actualOriginalStopAndSerialSubmit",true},{"actualThreeQueueClearBlocks",true},{"finiteOwnedWorkerNotProductWorker",true},{"normalWorkerNotStarted",true},{"fixtureReopenedAdmissionExplicitlyNotAutomaticRecovery",true},{"physicalActions",0}}.dump(2)<<std::endl;return failed?2:0;
#endif
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);if(releaseOwnedWorker)SetEvent(releaseOwnedWorker);if(g_gamepadSerialThread.joinable())g_gamepadSerialThread.join();if(releaseOwnedWorker)CloseHandle(releaseOwnedWorker);std::cerr<<error.what()<<std::endl;return 4;}}
