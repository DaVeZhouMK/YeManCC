#include <thread>
static unsigned long long fixtureControlPending(){
    unsigned long long n=g_nativeControlWindow.observations;
    for(std::size_t i=0;i<g_nativeControlRetiredCount;++i)n+=g_nativeControlRetired[(g_nativeControlRetiredBegin+i)%g_nativeControlRetired.size()].observations;
    return n;
}
static void fixtureControlSeed(){
    controlAllocationProbe=false;controlArmForeground=false;loggerThrows=false;fixtureDetailEnabled=true;rows.clear();rows.shrink_to_fit();
    g_nativeControlWindow={};g_nativeControlRetired={};g_nativeControlRetiredBegin=g_nativeControlRetiredCount=0;
    g_nativeControlRetiredOverwritten=g_nativeControlRetiredObservationsLost=0;g_nativeControlWindowWasEnabled.store(false);
    request["runId"]=std::string(96,'r');request["persona"]=std::string(64,'p');
    FocusFrameContent frame;
    for(unsigned long long e=1;e<=3;++e){request["epoch"]=e;nativeControlWindowObserve(frame,request,1,e,1,true,true,1,&physical.pad,true,9);}
    check(fixtureControlPending()==3&&g_nativeControlRetiredCount==2,"seed current plus two actual retired binding windows");fixtureTick+=1000;
}
static void fixtureControlRuntime(){FocusFrameContent frame;record(frame,fixtureTick,true,true,false,fixtureTick);}
static int fixtureNativeControlDrainCases(){
    DWORD handlesBefore=0,handlesAfter=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"own handle query");
#if defined(YMCC_NATIVE_CONTROL_PUBLICATION_ONLY)
    for(const std::size_t at:{std::size_t{1},std::size_t{7}}){
        fixtureControlSeed();NativeControlWindowDrainTicket ticket;controlAllocationSequence=0;controlAllocationFailAt=at;controlAllocationProbe=true;bool threw=false;
        try{auto rejected=nativeControlWindowSnapshot(&ticket);(void)rejected;}catch(const std::bad_alloc&){threw=true;}
        controlAllocationProbe=false;check(threw&&!ticket.valid&&fixtureControlPending()==3,"failed snapshot never publishes a usable drain ticket");
    }
    fixtureControlSeed();NativeControlWindowDrainTicket success;auto prepared=nativeControlWindowSnapshot(&success);
    check(success.valid&&prepared["drainPendingLogOfferReturn"]==true&&nativeControlWindowCommitDrain(success)&&fixtureControlPending()==0,"completed snapshot alone publishes the exact revision ticket");
    std::cout<<json({{"pass",true},{"checks",checks},{"publicationChecks",3},{"inheritedRuntimeExitAndGates",true}}).dump()<<'\n';return 0;
#endif
    fixtureControlSeed();loggerThrows=true;const auto failures=g_commonFrameEvidenceFailures;fixtureControlRuntime();loggerThrows=false;
    check(g_commonFrameEvidenceFailures==failures+1&&fixtureControlPending()==4,"actual runtime sink throw retains current and both retired windows");
    const auto attempts=logAttempts;fixtureControlRuntime();check(logAttempts==attempts&&fixtureControlPending()==5,"same bucket stays quiet while observations continue");
    auto retained=nativeControlWindowSnapshot();check(retained["observations"]==3&&retained["retiredBindingWindows"].size()==2&&fixtureControlPending()==5,"read-only export snapshot preserves exact windows");
    unsigned snapshotFaults=0;
    for(const std::size_t at:{std::size_t{1},std::size_t{7}}){
        fixtureControlSeed();NativeControlWindowDrainTicket failedTicket;controlAllocationSequence=0;controlAllocationFailAt=at;controlAllocationProbe=true;bool threw=false;
        try{auto rejected=nativeControlWindowSnapshot(&failedTicket);(void)rejected;}catch(const std::bad_alloc&){threw=true;}
        controlAllocationProbe=false;
        check(threw&&!failedTicket.valid&&controlAllocationSequence==at&&fixtureControlPending()==3&&g_nativeControlRetiredCount==2,"selected actual snapshot copy or first JSON allocation failure retains all windows");++snapshotFaults;
    }
    fixtureControlSeed();controlArmForeground=true;const auto failedBefore=g_commonFrameEvidenceFailures;fixtureControlRuntime();controlAllocationProbe=false;
    check(!controlArmForeground&&g_commonFrameEvidenceFailures==failedBefore+1&&fixtureControlPending()==4,"actual foreground JSON allocation after prepared control snapshot retains all windows");
    fixtureControlSeed();fixtureControlRuntime();check(fixtureControlPending()==0&&g_nativeControlRetiredCount==0&&rows.size()==1,"normal runtime offer returns then commits unchanged window");
    const auto& offered=rows.back()["allObservedControls"];
    check(offered["drained"]==false&&offered["pendingTailAtExport"]==true&&offered["drainPendingLogOfferReturn"]==true,"offered row claims prepared snapshot rather than later clear or file delivery");
    fixtureControlSeed();loggerThrows=true;const auto edgeBefore=controlExitEdgeCalls;SetLastError(1234);inputEvidenceFlushForExit();loggerThrows=false;
    check(GetLastError()==1234&&controlExitEdgeCalls==edgeBefore+1&&fixtureControlPending()==3,"actual exit sink throw retains counters and original LastError/edge call");
    fixtureControlSeed();inputEvidenceFlushForExit();check(fixtureControlPending()==0&&rows.size()==1&&rows.back()["phase"]=="exit-tail"&&rows.back()["edgeDrain"]["drained"]==true,"normal complete exit consumer offers original receipt and commits");
    check(rows.back()["drained"]==false&&rows.back()["drainPendingLogOfferReturn"]==true&&rows.back()["durableWriteProven"]==false,"exit snapshot never claims durable write or completed drain");
    fixtureControlSeed();NativeControlWindowDrainTicket oldTicket;auto copy=nativeControlWindowSnapshot(&oldTicket);
    FocusFrameContent frame;nativeControlWindowObserve(frame,request,9,9,1,true,true,9,&physical.pad,true,10);
    check(!nativeControlWindowCommitDrain(oldTicket)&&fixtureControlPending()==4,"intervening actual observation prevents stale clear");
    NativeControlWindowDrainTicket first,second;auto a=nativeControlWindowSnapshot(&first);auto b=nativeControlWindowSnapshot(&second);
    check(nativeControlWindowCommitDrain(first)&&!nativeControlWindowCommitDrain(second)&&!nativeControlWindowCommitDrain(first)&&fixtureControlPending()==0,"overlapping or repeated ticket cannot clear another window");
    fixtureControlSeed();NativeControlWindowDrainTicket offTicket;auto offCopy=nativeControlWindowSnapshot(&offTicket);fixtureDetailEnabled=false;nativeControlWindowDisabled();
    check(!nativeControlWindowCommitDrain(offTicket)&&g_nativeControlWindow.observations==0,"OFF keeps original clearing and invalidates prepared drain");
    fixtureDetailEnabled=true;fixtureControlSeed();NativeControlWindowDrainTicket busyTicket;auto busyCopy=nativeControlWindowSnapshot(&busyTicket);
    std::atomic<bool> mutexHeld{false},mutexRelease{false};
    std::thread contender([&]{g_nativeControlWindowMutex.lock();mutexHeld.store(true);while(!mutexRelease.load())std::this_thread::yield();g_nativeControlWindowMutex.unlock();});
    while(!mutexHeld.load())std::this_thread::yield();const bool busyAccepted=nativeControlWindowCommitDrain(busyTicket);mutexRelease.store(true);contender.join();
    check(!busyAccepted&&!busyTicket.valid&&fixtureControlPending()==3,"contended commit consumes ticket and retains all bounded windows");
    NativeControlWindowDrainTicket freshTicket;auto freshCopy=nativeControlWindowSnapshot(&freshTicket);
    check(nativeControlWindowCommitDrain(freshTicket)&&fixtureControlPending()==0,"fresh ticket after contention can commit");
    fixtureControlSeed();const auto savedRevision=g_nativeControlWindowRevision;g_nativeControlWindowRevision=(std::numeric_limits<std::uint64_t>::max)();
    NativeControlWindowDrainTicket saturated;auto saturatedCopy=nativeControlWindowSnapshot(&saturated);
    check(!saturated.valid&&!nativeControlWindowCommitDrain(saturated)&&fixtureControlPending()==3,"saturated mutation stamp cannot authorize stale clear");g_nativeControlWindowRevision=savedRevision;
    fixtureControlSeed();const auto tickBefore=clockCalls,logsBefore=logAttempts;fixtureDetailEnabled=false;
    const auto allocBefore=allocations;countAllocations=true;for(unsigned i=0;i<10000;++i)fixtureControlRuntime();countAllocations=false;
    check(allocations==allocBefore&&clockCalls==tickBefore&&logAttempts==logsBefore,"OFF ten thousand original calls allocate no heap and perform no clock or log work");
    fixtureControlSeed();fixtureControlRuntime();const auto sampledBefore=rows.size();const auto highAllocBefore=allocations;countAllocations=true;
    for(unsigned i=0;i<10000;++i)fixtureControlRuntime();countAllocations=false;
    check(allocations==highAllocBefore&&rows.size()==sampledBefore&&fixtureControlPending()==10000,"ON same bucket ten thousand original calls retain scalar observations with zero heap or extra rows");
    check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE&&handlesBefore==handlesAfter,"all fixture thread handles returned");
    std::cout<<json({{"pass",true},{"checks",checks},{"checkedFaultMinimumAllocationBytes",64},{"checkedSmallProxyAllocationFailuresNotCovered",true},{"selectedSnapshotAllocationFaults",snapshotFaults},{"actualOuterForegroundAllocationFaults",1},{"runtimeSinkThrow",true},{"exitSinkThrow",true},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"offCalls",10000},{"sameBucketCalls",10000},{"arbitraryAllocationFailureContainment",false},{"priorExhaustiveFaultFailurePreserved",true},{"fileDeliveryProven",false},{"realHardwareUsed",false}}).dump()<<'\n';return 0;
}
