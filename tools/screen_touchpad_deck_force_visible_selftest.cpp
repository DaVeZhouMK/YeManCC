// Executes the production cursor header with the existing offline OS substitutes.
// No actual cursor, window, hook, input, controller, or Steam process is used.
// Superseded Deck compensation expectations are replaced by Mouse-only ownership.
#define main historical_cursor_fixture_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
static void freshForce() {
    reset();
    cursorDeckForcedHiddenSamples=cursorDeckForcedShows=0;
}
static void setSample(DWORD flags,uintptr_t shape,LONG x=1521,LONG y=514) {
    sim::current={sizeof(CURSORINFO),flags,(HCURSOR)shape,{x,y}};
}
static void sampled() {cursorSyncInfo(sim::current,sim::now);}
static void runForce() {
    // Replay every native visibility class through real contact/owner functions.
    for(DWORD flags:{DWORD{0},DWORD{CURSOR_SHOWING},DWORD{CURSOR_SUPPRESSED},DWORD{CURSOR_SHOWING|CURSOR_SUPPRESSED}}) {
        for(uintptr_t shape:{uintptr_t{0},uintptr_t{65539}}) {
            freshForce();setSample(flags,shape);down();drain();
            check("Deck-DOWN-never-creates-cursor-or-observer",!visible()&&!cursorWindow&&!cursorTracking&&!cursorRetained&&sim::reads==0&&sim::hookInstalls==0);
            for(unsigned i=0;i<128;++i){sim::now+=24;sim::current.ptScreenPos.x++;cursorContactChanged(sim::dispatchWindow,false,true,sim::now);cursorRequestBeforeReport();cursorRequestAfterReport();bus();sampled();}
            check("Deck-MOVE-never-borrows-shape-or-fallback",!visible()&&!cursorWindow&&sim::uploads==0&&sim::positions==0&&sim::moves==0&&sim::reads==0&&sim::hookInstalls==0);
            core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
            check("Deck-UP-never-retains-cursor-or-poll",!cursorTracking&&!cursorRetained&&sim::messages.empty()&&sim::reads==0);
        }
    }
    freshForce();setSample(CURSOR_SUPPRESSED,65539);down();drain();
    const auto coreDown=core.pads[0].down;
    for(unsigned i=0;i<10000;++i){cursorRequestSync();cursorRequestSync(true);cursorRequestBeforeReport();cursorRequestAfterReport();}
    drain();check("idle-Deck-BUS-adds-no-queries-messages-or-draws",sim::reads==0&&sim::messages.empty()&&sim::uploads==0&&sim::positions==0);
    check("cursor-policy-does-not-release-actual-Deck-contact",coreDown&&core.pads[0].down&&core.mode(0)==Mode::Deck);
    check("retired-Deck-shape-bridge-and-force-counters-stay-zero",cursorDeckContactShapeBridgeShows==0&&cursorDeckForcedShows==0&&cursorDeckForcedHiddenSamples==0);
    // A stale UI query/upload cannot bypass the mode owner after the queued edge.
    cursorSyncInfo(sim::current);check("direct-stale-Deck-sync-cannot-create-arrow",!cursorWindow&&sim::uploads==0);

    freshForce();core.modes[0]=Mode::Mouse;setSample(0,0);down();drain();
    check("direct-Mouse-respects-explicit-hidden-state",!visible()&&sim::uploads==0);
    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);down();drain();
    check("direct-Mouse-original-suppression-compensation-remains",visible()&&sim::uploads==1);
    check("Mouse-display-remains-disabled-transparent-noactivate",!sim::windows.at((uintptr_t)cursorWindow).enabled&&
        (sim::windows.at((uintptr_t)cursorWindow).style&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED));
    const auto initialUploads=sim::uploads;sim::now+=24;sim::current.ptScreenPos.x+=10;bus();
    check("direct-Mouse-movement-reuses-bitmap",visible()&&sim::uploads==initialUploads&&sim::positions==2);
    const auto placed=sim::positions;sim::now+=24;bus();check("direct-Mouse-unchanged-position-does-no-placement",sim::positions==placed);
    setSample(CURSOR_SHOWING,0);sampled();check("Mouse-transient-null-keeps-valid-frame",visible());
    setSample(CURSOR_SUPPRESSED,0);sampled();check("Mouse-suppressed-null-keeps-valid-shape",visible());
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    check("Mouse-normal-UP-keeps-original-retention",visible()&&cursorRetained&&!cursorTracking);
    setSample(0,0);sampled();check("Mouse-retention-respects-later-hidden-state",!visible()&&!cursorRetained);

    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);down();drain();
    cursorNextActivePoll.store(0);cursorRequestSync();auto queued=sim::messages.front();sim::messages.pop_front();
    core.modes[0]=Mode::Deck;const auto oldReads=sim::reads;cursorContactChanged(sim::dispatchWindow);
    check("Mouse-to-Deck-withdraws-existing-bitmap-immediately",!visible()&&!cursorTracking&&!cursorRetained);
    dispatch(queued);sampled();check("late-Mouse-message-cannot-resurrect-Deck-cursor",!visible()&&sim::reads==oldReads);

    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);down();drain();
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    core.modes[1]=Mode::Deck;core.pads[1].down=true;const auto retainedReads=sim::reads;cursorContactChanged(sim::dispatchWindow);
    check("new-Deck-contact-cannot-inherit-retained-Mouse-frame",!visible()&&!cursorRetained&&sim::reads==retainedReads);

    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);
    sim::afterUpload=[]{core.modes[0]=Mode::Deck;};down();drain();
    check("mode-change-inside-GDI-preparation-cannot-show-Deck-cursor",!visible()&&!cursorTracking&&!cursorRetained&&sim::shows==0);
    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);sim::afterUpload=[]{cursorStop();};down();drain();
    check("preparation-cancel-cannot-show-stale-frame",!visible()&&!cursorTracking&&!cursorRetained);

    freshForce();setSample(CURSOR_SUPPRESSED,1);core.modes[1]=Mode::Mouse;core.pads[1].down=true;down();drain();
    check("mixed-contact-compensation-requires-real-Mouse-contact",visible()&&!cursorDeckOnlyTracking);
    core.pads[1].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    check("remaining-Deck-contact-ends-mixed-Mouse-compensation",!visible()&&!cursorTracking&&!cursorRetained);
    freshForce();setSample(CURSOR_SUPPRESSED,1);down();drain();core.modes[0]=Mode::Mouse;cursorContactChanged(sim::dispatchWindow);drain();
    check("explicit-Deck-to-Mouse-mode-retains-Mouse-functionality",visible());

    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);sim::failUpload=true;down();drain();
    check("Mouse-failed-first-upload-cannot-show-old-bitmap",!visible());
    sim::failUpload=false;sim::now+=24;bus();check("Mouse-upload-recovery-uses-original-BUS",visible());
    sim::readResults.push_back(false);sim::now+=24;bus();check("Mouse-transient-query-failure-keeps-valid-frame",visible());
    core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,false);drain();check("Mouse-cancel-releases-display",!visible()&&!cursorRetained&&!cursorTracking);
    freshForce();core.modes[0]=Mode::Mouse;setSample(CURSOR_SUPPRESSED,1);down();drain();
    setSample(CURSOR_SHOWING,1);sim::now+=1;sampled();sim::now+=40;sampled();check("Mouse-native-handoff-keeps-original-grace",visible());
    sim::now+=40;sampled();check("Mouse-native-handoff-completes-original-grace",!visible());
    check("direct-Mouse-existing-motion-budget-remains-24ms",kCursorActiveFallbackMs==24&&kCursorRetainedPollMs==24);
    cursorShutdown();check("all-fake-window-and-GDI-resources-returned",sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty()&&!sim::activeHook);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {runForce();} catch(const std::exception& e){check(e.what(),false);}
    Json result={{"ok",failures==0},{"checks",cases.size()},{"cases",cases},
        {"scope","Actual production cursor headers with existing substituted OS APIs; no real compositor, Steam or G3E hardware"},
        {"realWindowsCreated",0},{"realInputCalls",0},{"fieldFlickerVerified",false}};
    std::ofstream(argv[1])<<result.dump(2)<<'\n';return failures?1:0;
}
