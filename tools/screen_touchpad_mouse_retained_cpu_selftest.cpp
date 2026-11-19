// Actual cursor header through the existing deterministic API substitutes.
// Exercise Mouse only; historical Deck cursor scenarios are intentionally not run.
#define main original_cursor_fixture_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main

static void mouseDown() {
    reset();core.modes[0]=Mode::Mouse;down();drain();
    check("mouse-contact-shows-suppressed-cursor",visible()&&cursorTracking&&!cursorRetained);
}
static void mouseUp() {core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);}
static Json mouseMetrics;
static void mouseRetainedCases() {
    mouseDown();const auto readsBeforeUp=sim::reads;
    mouseUp();
    check("release-fresh-sample-does-not-enqueue-a-duplicate",sim::messages.empty()&&!cursorSyncPending);
    drain();
    const auto afterUp=sim::reads;
    for(unsigned i=1;i<24;++i){sim::now=1000+i;bus();}
    check("release-keeps-visual-with-no-query-before-next-slot",visible()&&cursorRetained&&sim::reads==afterUp);
    sim::now=1024;bus();
    check("release-retained-query-still-runs-at-original-24ms",sim::reads==afterUp+1&&visible());
    mouseMetrics["queriesAtRelease"]=afterUp-readsBeforeUp;

    // UI processing is late; another BUS opportunity must not cause catch-up work.
    mouseDown();mouseUp();drain();sim::now=1024;cursorRequestSync();
    check("retained-due-slot-posts-one-message",sim::messages.size()==1&&cursorSyncPending);
    sim::now=1124;drain();const auto lateReads=sim::reads;
    for(unsigned i=1;i<24;++i){sim::now=1124+i;bus();}
    check("late-UI-query-does-not-immediately-catch-up",sim::reads==lateReads&&visible());
    sim::now=1148;bus();
    check("late-UI-query-resumes-24ms-after-actual-processing",sim::reads==lateReads+1&&visible());

    // A previously queued active query keeps its authority through normal UP.
    mouseDown();sim::now=1024;cursorRequestSync();const auto queued=sim::messages.front();
    sim::now=1025;mouseUp();
    check("UP-preserves-an-already-pending-active-query",sim::messages.size()==1&&cursorSyncPending&&sim::messages.front().id==queued.id);
    drain();const auto pendingReads=sim::reads;
    sim::now=1048;bus();
    check("pending-active-query-also-anchors-retained-budget",sim::reads==pendingReads);
    sim::now=1049;bus();
    check("pending-active-query-keeps-next-retained-opportunity",sim::reads==pendingReads+1&&visible());

    // A failed late query retains the existing frame, with the same retry cadence.
    mouseDown();mouseUp();drain();sim::now=1024;cursorRequestSync();
    sim::now=1124;sim::readResults.push_back(false);drain();const auto failedReads=sim::reads;
    check("failed-retained-query-keeps-last-visual",visible()&&cursorRetained);
    for(unsigned i=1;i<24;++i){sim::now=1124+i;bus();}
    check("failed-late-query-does-not-busy-retry",sim::reads==failedReads);
    sim::now=1148;sim::current.ptScreenPos.x+=12;bus();
    check("failed-query-recovers-on-next-original-slot",sim::reads==failedReads+1&&visible()&&atCurrentPosition());

    for(DWORD flags:{DWORD{0},DWORD{CURSOR_SHOWING}}) {
        mouseDown();mouseUp();drain();sim::now=1024;sim::current.flags=flags;bus();
        check(flags?"system-cursor-takes-over-retained-frame":"explicit-hidden-state-releases-retained-frame",!visible()&&!cursorRetained&&!cursorTracking);
    }
    mouseDown();sim::current.flags=CURSOR_SHOWING;sim::now=1010;cursorSync();
    sim::now=1050;cursorSync();check("active-Mouse-keeps-existing-handoff-grace",visible());
    sim::now=1090;cursorSync();check("active-Mouse-handoff-still-completes-after-80ms",!visible());

    mouseDown();sim::now=1024;cursorRequestSync();mouseUp();cursorStop();const auto stoppedReads=sim::reads;drain();
    for(unsigned i=0;i<10000;++i){++sim::now;bus();}
    check("stop-rejects-stale-pending-query-and-idle-work",sim::reads==stoppedReads&&!visible()&&!cursorRetained&&!cursorSyncPending);
    mouseDown();mouseUp();core.modes[0]=Mode::Deck;core.pads[0].down=true;
    cursorContactChanged(sim::dispatchWindow);const auto nativeReads=sim::reads;drain();
    for(unsigned i=0;i<10000;++i){++sim::now;cursorContactChanged(sim::dispatchWindow);bus();}
    check("native-Deck-takeover-never-inherits-Mouse-poll-or-pixels",sim::reads==nativeReads&&!visible()&&!cursorTracking&&!cursorRetained&&!sim::activeHook);

    // Preserve continuous appearance across swipes while counting actual API calls.
    reset();core.modes[0]=Mode::Mouse;unsigned queries=0;
    for(unsigned swipe=0;swipe<32;++swipe) {
        sim::now=1000+swipe*100;const auto prior=sim::reads;
        core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);drain();mouseUp();drain();
        queries+=sim::reads-prior;
        check("successive-swipes-retain-one-visible-frame",visible()&&cursorRetained&&sim::uploads==1&&sim::shows==1&&sim::hides==0);
    }
    check("32-swipes-use-only-contact-and-release-samples",queries==64);
    mouseMetrics["swipes"]=32;mouseMetrics["queriesAcrossSwipes"]=queries;
    reset();check("all-simulated-resources-released",sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty()&&sim::messages.empty()&&!sim::activeHook);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try{mouseRetainedCases();}catch(const std::exception& error){++failures;cases.push_back({{"name","fixture-exception"},{"ok",false},{"error",error.what()}});}
    const Json result={{"ok",failures==0},{"failures",failures},{"cases",cases},{"metrics",mouseMetrics},
        {"realWindowsCreated",0},{"realInputInjected",0},{"hardwareCalls",0},{"processCpuMeasured",false},
        {"scope","actual Mouse cursor functions with deterministic API substitution; not game/pixel/CPU acceptance"}};
    std::ofstream out(argv[1]);out<<result.dump(2)<<'\n';std::cout<<"MOUSE_RETAINED checks="<<cases.size()<<" failures="<<failures<<"\n";
    return out?(failures?1:0):2;
}
