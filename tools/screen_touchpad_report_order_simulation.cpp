// Actual production cursor policy, deterministic API substitutes from the existing fixture.
// No real HWND, mouse/device input or Steam process. The report/Steam steps are modeled.
#define main retained_cursor_fixture_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
static void beforeReport(){
#ifdef YMCC_CURSOR_REPORT_ORDER_LEGACY_FIXTURE
    cursorRequestSync();
#else
    cursorRequestBeforeReport();
#endif
}
static void afterReport(){
#ifndef YMCC_CURSOR_REPORT_ORDER_LEGACY_FIXTURE
    cursorRequestAfterReport();
#endif
}
static bool atObservedPosition(){return cursorPosition.x==sim::current.ptScreenPos.x-cursorHotspot.x&&cursorPosition.y==sim::current.ptScreenPos.y-cursorHotspot.y;}
static void orderedFrame(){beforeReport();drain();afterReport();drain();}
static void ordinaryDeckMove(){
#ifdef YMCC_CURSOR_MOVE_SUBMIT_ORDER_FIXTURE_ENABLED
    cursorContactChanged(sim::dispatchWindow,false,true,sim::now);
#else
    cursorContactChanged(sim::dispatchWindow,false,true);
#endif
    drain();
}
#ifdef YMCC_CURSOR_MOVE_SUBMIT_ORDER_FIXTURE_ENABLED
static constexpr bool moveDispatchTickReused=true;
#else
static constexpr bool moveDispatchTickReused=false;
#endif
static Json moveOrderMeasurements=Json::object();
static void runMoveReportOrder(){
    reset();down();drain();afterReport();drain();sim::now+=24;
    const auto queries=sim::reads;const auto deadline=cursorDeckNextPositionTick;
    ordinaryDeckMove();
    check("ordinary-Deck-MOVE-before-report-keeps-query-slot-unspent",sim::reads==queries&&cursorDeckNextPositionTick==deadline);
    sim::current.ptScreenPos.x+=80;afterReport();drain();
    check("report-after-ordinary-MOVE-observes-current-OS-coordinate",sim::reads==queries+1&&atObservedPosition());

    reset();down();drain();afterReport();drain();sim::now+=48;
    sim::current.ptScreenPos.x+=35;const auto stalled=sim::reads;ordinaryDeckMove();
    check("ordinary-Deck-MOVE-keeps-observation-when-report-lane-stalls",sim::reads==stalled+1&&atObservedPosition());
    const auto recovered=sim::reads;afterReport();drain();
    check("report-recovery-after-UI-fallback-does-not-double-query",sim::reads==recovered);
    sim::now=cursorDeckNextPositionTick;const auto restored=sim::reads;ordinaryDeckMove();
    sim::current.ptScreenPos.x+=18;afterReport();drain();
    check("recovered-report-lane-regains-next-original-slot",sim::reads==restored+1&&atObservedPosition());

    reset();down();drain();sim::now+=24;sim::current.ptScreenPos.x+=25;const auto cold=sim::reads;ordinaryDeckMove();
    check("no-report-pulse-preserves-original-UI-fallback",sim::reads==cold+1&&atObservedPosition());

    reset();down();drain();const auto begin=sim::now;unsigned long long moveClockCalls=0;unsigned moveQueries=0;constexpr unsigned duration=1000,movesPerTick=3;
    for(unsigned tick=1;tick<=duration;++tick){
        sim::now=begin+tick;++sim::current.ptScreenPos.x;afterReport();drain();
        const auto beforeClock=sim::tick64Reads;const auto beforeQueries=sim::reads;
        for(unsigned move=0;move<movesPerTick;++move)ordinaryDeckMove();
        moveClockCalls+=sim::tick64Reads-beforeClock;moveQueries+=sim::reads-beforeQueries;
    }
    moveOrderMeasurements={{"ordinaryDeckMoveCalls",duration*movesPerTick},{"moveClockCalls",moveClockCalls},{"moveQueries",moveQueries},
        {"virtualDurationMs",duration},{"processCpuMeasured",false},{"productionDispatchTickReused",moveDispatchTickReused}};
    check("healthy-report-lane-MOVE-adds-no-clock-query-or-position-work",moveClockCalls==0&&moveQueries==0);
    reset();
}
static void runReportOrder(){
    reset();down();drain();sim::now+=24;
    const auto reads=sim::reads;const auto deadline=cursorDeckNextPositionTick;const auto old=cursorPosition;
    beforeReport();drain();
    check("Deck-before-submit-keeps-query-and-deadline-unspent",sim::reads==reads&&cursorDeckNextPositionTick==deadline&&sim::messages.empty());
    // Model a report that Steam consumes before the delayed UI notification.
    sim::current.ptScreenPos.x+=80;sim::current.ptScreenPos.y+=45;
    const auto submittedReads=sim::reads;afterReport();drain();
    check("Deck-after-submit-queries-current-position-once",sim::reads==submittedReads+1&&atObservedPosition()&&visible()&&cursorPosition.x!=old.x);
    const auto afterReads=sim::reads;beforeReport();afterReport();drain();
    check("same-tick-requests-still-share-original-budget",sim::reads==afterReads);

    // A Host receipt still cannot prove that asynchronous Steam output is ready.
    reset();down();drain();sim::now+=24;const auto lateReads=sim::reads;
    orderedFrame();const auto beforeSteam=cursorPosition;sim::current.ptScreenPos.x+=100;
    check("late-Steam-consumption-is-not-faked-or-polled-again",sim::reads==lateReads+1&&cursorPosition.x==beforeSteam.x&&!atObservedPosition());
    sim::now+=23;orderedFrame();check("late-output-does-not-increase-refresh-rate",sim::reads==lateReads+1);
    sim::now+=1;orderedFrame();check("late-output-recovers-at-next-original-slot",sim::reads==lateReads+2&&atObservedPosition());

    reset();core.modes[0]=Mode::Mouse;down();drain();sim::now+=24;const auto mouseReads=sim::reads;
    beforeReport();drain();check("Mouse-keeps-pre-report-query",sim::reads==mouseReads+1);
    afterReport();drain();check("Mouse-adds-no-post-report-query",sim::reads==mouseReads+1&&!cursorDeckOnlyTracking);
    reset();down();core.modes[1]=Mode::Mouse;core.pads[1].down=true;cursorContactChanged(sim::dispatchWindow);drain();sim::now+=24;
    const auto mixedReads=sim::reads;orderedFrame();check("mixed-Mouse-Deck-keeps-one-original-query",sim::reads==mixedReads+1&&!cursorDeckOnlyTracking);

    reset();down();core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();sim::now+=24;
    const auto retainedReads=sim::reads;beforeReport();drain();
    check("retained-Deck-before-report-keeps-budget-unspent",sim::reads==retainedReads&&cursorRetained);
    sim::current.ptScreenPos.y+=65;const auto retainedAfter=sim::reads;afterReport();drain();
    check("retained-Deck-after-report-keeps-existing-poll",sim::reads==retainedAfter+1&&cursorRetained&&atObservedPosition());

    reset();down();drain();sim::now+=24;const auto absentReads=sim::reads;
    beforeReport();drain(); // Model host gate closed: no report/Steam action.
    afterReport();drain();check("no-host-report-does-not-disable-existing-observation",sim::reads==absentReads+1&&visible());
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,false);const auto cancelledReads=sim::reads;sim::now+=24;
    orderedFrame();check("transport-cancel-does-not-resurrect-cursor-or-poll",sim::reads==cancelledReads&&!cursorTracking&&!cursorRetained&&!visible());

    reset();down();drain();const auto budgetReads=sim::reads;const auto start=sim::now;
    for(int i=0;i<160;i++){sim::now+=8;beforeReport();drain();++sim::current.ptScreenPos.x;afterReport();drain();}
    const auto used=sim::reads-budgetReads;
    check("160-BUS-ticks-keep-original-24ms-query-cap",used>0&&used<=(sim::now-start)/kCursorActiveFallbackMs+1);
    reset();down();drain();sim::now+=24;beforeReport();drain();core.modes[0]=Mode::Mouse;cursorContactChanged(sim::dispatchWindow);drain();const auto switchedReads=sim::reads;afterReport();drain();
    check("Deck-to-Mouse-switch-does-not-add-after-query",sim::reads==switchedReads&&!cursorDeckOnlyTracking);
    reset();core.modes[0]=Mode::Mouse;down();drain();sim::now+=24;beforeReport();drain();core.modes[0]=Mode::Deck;cursorContactChanged(sim::dispatchWindow);drain();const auto deckReads=sim::reads;afterReport();drain();
    check("Mouse-to-Deck-switch-coalesces-with-immediate-contact-query",sim::reads==deckReads&&cursorDeckOnlyTracking);
    reset();down();drain();sim::now+=24;sim::failPost=true;orderedFrame();check("post-failure-keeps-pending-clear",!cursorSyncPending&&sim::messages.empty());
    sim::failPost=false;sim::now+=24;const auto retryReads=sim::reads;orderedFrame();check("post-failure-retries-on-next-original-slot",sim::reads==retryReads+1);
    reset();check("report-order-fixture-releases-all-fake-resources",sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty()&&!sim::activeHook);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{runReportOrder();runMoveReportOrder();}catch(const std::exception& e){++failures;cases.push_back({{"name","harness-exception"},{"ok",false},{"error",e.what()}});}
    Json result={{"ok",failures==0},{"failures",failures},{"cases",cases},{"scope","actual cursor headers; source-checked BUS boundary; modeled Host/Steam schedule"},
        {"realInputInjected",0},{"realWindowsCreated",0},{"realHooksRegistered",0},{"hardwareCalls",0},{"fieldFlickerVerified",false},{"steamConsumptionVerified",false},{"moveOrderMeasurements",moveOrderMeasurements}};
    std::ofstream out(argv[1]);out<<result.dump(2)<<'\n';return out?(failures?1:0):2;
}
