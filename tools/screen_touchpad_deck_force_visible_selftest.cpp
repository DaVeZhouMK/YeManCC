// Executes the production cursor header with the existing offline OS substitutes.
// No actual cursor, window, hook, input, controller, or Steam process is used.
#define main historical_cursor_fixture_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
static void freshForce() {
    reset();cursorDeckVisibilityShape=nullptr;
    cursorDeckForcedHiddenSamples=cursorDeckForcedShows=0;
}
static void setSample(DWORD flags,uintptr_t shape,LONG x=1521,LONG y=514) {
    sim::current={sizeof(CURSORINFO),flags,(HCURSOR)shape,{x,y}};
}
static void sampled() {cursorSyncInfo(sim::current,sim::now);}
static void runForce() {
    freshForce();sim::now=10780718;setSample(CURSOR_SHOWING,65539);down();drain();
    check("field-438-visible-system-has-no-software-duplicate",!visible()&&sim::uploads==0);
    setSample(0,0);sampled();
    check("field-439-same-tick-hidden-is-covered",visible()&&cursorShape==(HCURSOR)65539);
    check("field-439-uses-observed-position-and-hotspot",cursorPosition.x+cursorHotspot.x==1521&&cursorPosition.y+cursorHotspot.y==514);
    const auto firstUploads=sim::uploads;
    for(unsigned i=0;i<256;++i) {
        sim::now+=24;setSample(0,0,1521+i,514);sampled();
        if(!visible())throw std::runtime_error("hidden continuity lost");
        setSample(CURSOR_SHOWING,65539,1521+i,514);sampled();
        if(visible())throw std::runtime_error("duplicate system cursor");
    }
    check("256-show-hide-pairs-reuse-one-bitmap",sim::uploads==firstUploads);
    setSample(0,0);sampled();check("hidden-override-has-bounded-evidence-counter",cursorDeckForcedHiddenSamples>=257&&cursorDeckForcedShows>=257);
    setSample(CURSOR_SHOWING|CURSOR_SUPPRESSED,65539);sampled();check("drawable-SHOWING-wins-over-suppressed-bit",!visible());
    setSample(CURSOR_SHOWING,0);sampled();check("SHOWING-without-shape-has-software-coverage",visible());
    setSample(CURSOR_SUPPRESSED,0);sampled();check("suppressed-null-remains-covered",visible());
    check("display-remains-disabled-transparent-and-noactivate",!sim::windows.at((uintptr_t)cursorWindow).enabled&&
        (sim::windows.at((uintptr_t)cursorWindow).style&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED));

    freshForce();setSample(0,0);down();drain();
    check("unknown-shape-contact-prepares-default-arrow",visible()&&cursorShape==(HCURSOR)1);
    const auto deadline=cursorDeckNextPositionTick;
    const auto start=sim::now;const auto positions=sim::positions,moves=sim::moves,uploads=sim::uploads;
    for(unsigned i=1;i<24;++i) {
        sim::now=start+i;sim::current.ptScreenPos.x++;
        edge(EVENT_OBJECT_HIDE);drain();bus();
    }
    check("dense-hidden-events-do-not-extend-motion-deadline",cursorDeckNextPositionTick==deadline);
    check("dense-hidden-events-do-not-position-before-slot",sim::positions==positions&&sim::moves==moves);
    sim::now=deadline;bus();
    check("hidden-motion-is-not-starved-at-existing-slot",visible()&&sim::moves==moves+1&&cursorPosition.x+cursorHotspot.x==sim::current.ptScreenPos.x);
    for(unsigned i=0;i<128;++i){sim::now+=24;sim::current.ptScreenPos.x++;bus();}
    check("129-hidden-moves-never-rerender-the-bitmap",sim::uploads==uploads);
    const auto unchangedPositions=sim::positions+sim::moves;sim::now+=24;bus();
    check("unchanged-position-does-no-positioning",sim::positions+sim::moves==unchangedPositions);
    cursorNextActivePoll.store(0);const auto reads=sim::reads;
    for(unsigned i=0;i<10000;++i)cursorRequestSync();
    check("ten-thousand-BUS-requests-only-queue-one-message",sim::messages.size()==1&&sim::reads==reads);
    sim::now=cursorDeckNextPositionTick;drain();
    check("coalesced-BUS-message-queries-once",sim::reads==reads+1);

    freshForce();setSample(0,0);down();drain();
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    check("normal-UP-in-hidden-state-exits-force-without-retention",!visible()&&!cursorTracking&&!cursorRetained&&!cursorDeckVisibilityShape);
    const auto idleReads=sim::reads;for(unsigned i=0;i<10000;++i)cursorRequestSync();drain();
    check("inactive-force-adds-no-poll-or-message",sim::reads==idleReads&&sim::messages.empty());
    freshForce();setSample(0,0);down();drain();cursorNextActivePoll.store(0);cursorRequestSync();cursorStop();drain();
    check("cancel-and-stale-notification-cannot-resurrect-force",!visible()&&!cursorTracking&&!cursorRetained);
    freshForce();setSample(CURSOR_SUPPRESSED,1);down();drain();
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    check("old-suppression-retention-still-exists",visible()&&cursorRetained&&!cursorTracking);
    setSample(0,0);sampled();check("retained-owner-never-acquires-hidden-override",!visible()&&!cursorRetained);

    freshForce();core.modes[0]=Mode::Mouse;setSample(0,0);down();drain();
    check("direct-Mouse-keeps-explicit-hidden-policy",!visible());
    freshForce();core.modes[1]=Mode::Mouse;core.pads[1].down=true;setSample(0,0);down();drain();
    check("mixed-Mouse-and-Deck-do-not-force-display",!visible()&&!cursorDeckOnlyTracking);
    freshForce();setSample(0,0);down();drain();core.modes[0]=Mode::Mouse;cursorContactChanged(sim::dispatchWindow);drain();
    check("mode-change-clears-active-Deck-override",!visible()&&!cursorDeckOnlyTracking&&!cursorDeckVisibilityShape);

    freshForce();setSample(0,7);down();drain();
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    setSample(0,0);down();drain();
    check("next-contact-does-not-resurrect-old-contact-shape",visible()&&cursorShape==(HCURSOR)1);
    freshForce();setSample(0,0);sim::failUpload=true;down();drain();
    check("failed-first-upload-cannot-reveal-old-bitmap",!visible());
    sim::failUpload=false;sim::now+=24;bus();check("failed-upload-retries-on-original-BUS-budget",visible());
    freshForce();setSample(0,0);down();drain();sim::readResults.push_back(false);sim::now+=24;bus();
    check("transient-query-failure-preserves-existing-valid-frame",visible());
    freshForce();setSample(CURSOR_SUPPRESSED,1);sim::afterUpload=[]{setSample(0,0);};down();drain();
    check("post-prepare-hidden-recheck-still-permits-active-override",visible());
    freshForce();setSample(0,0);sim::afterUpload=[]{setSample(CURSOR_SHOWING,1);};down();drain();
    check("post-prepare-native-recovery-retires-duplicate",!visible());
    freshForce();setSample(0,0);sim::afterUpload=[]{cursorStop();};down();drain();
    check("post-prepare-cancel-never-shows-pixels",!visible()&&!cursorTracking&&!cursorRetained);
    check("original-motion-and-fallback-budgets-unchanged",kCursorActiveFallbackMs==24&&kCursorDeckMoveFallbackMs==48);
    cursorShutdown();
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {runForce();} catch(const std::exception& e){check(e.what(),false);}
    Json result={{"ok",failures==0},{"checks",cases.size()},{"cases",cases},
        {"scope","Actual production cursor headers with existing substituted OS APIs; no real compositor, Steam or G3E hardware"},
        {"realWindowsCreated",0},{"realInputCalls",0},{"fieldFlickerVerified",false}};
    std::ofstream(argv[1])<<result.dump(2)<<'\n';return failures?1:0;
}