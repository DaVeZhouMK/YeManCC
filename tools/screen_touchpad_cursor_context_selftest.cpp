// Real selected cursor headers with substituted OS APIs; never creates windows or input.
#define main cursor_original_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
namespace ctxtest {
static void fresh(){reset();cursorcontext::entries={};cursorcontext::count=cursorcontext::gestures=0;
 sim::contextForegroundReads=sim::contextHitReads=sim::contextAncestorReads=sim::contextIdentityReads=sim::contextClassReads=0;
 sim::contextForeground=(HWND)(uintptr_t)800;sim::contextForegroundAfter=nullptr;
 sim::contextHit=(HWND)(uintptr_t)801;sim::contextRoot=(HWND)(uintptr_t)802;sim::contextIdentityFails=false;sim::contextClass=L"SDL_app";}
static unsigned windowCalls(){return sim::contextForegroundReads+sim::contextHitReads+sim::contextAncestorReads+sim::contextIdentityReads+sim::contextClassReads;}
static void raw(DWORD flags,uintptr_t shape=1,CursorSampleSource source=CursorSampleSource::Periodic,bool valid=true,bool active=true,bool deck=true){
 CURSORINFO ci{sizeof(ci),flags,(HCURSOR)shape,{300,200}};
 cursorcontext::observe(ci,valid,source,sim::now,31,active,deck);
}
static void run(){
 fresh();cursorcontext::begin(true);raw(0,0);raw(CURSOR_SUPPRESSED,0);raw(CURSOR_SHOWING,0);
 check("unknown-hidden-suppressed-and-SHOWING-NULL-do-not-query-windows",cursorcontext::count==0&&windowCalls()==0);
 raw(CURSOR_SHOWING);check("first-known-SHOWING-only-arms-without-queries",cursorcontext::count==0&&windowCalls()==0);
 sim::error=1234;const auto clocks=sim::tick64Reads;const auto queries=sim::reads;raw(0,0,CursorSampleSource::Visibility);
 check("first-hidden-after-SHOWING-captures-once",cursorcontext::count==1&&sim::contextHitReads==1);
 check("capture-adds-no-clock-or-cursor-query",sim::tick64Reads==clocks&&sim::reads==queries);
 check("capture-preserves-thread-last-error",sim::error==1234);
 auto j=cursorcontext::state();const auto& e=j["entries"][0];
 check("cursor-sample-identity-and-point-recorded",e["cursorTraceSequence"]==31&&e["flags"]==0&&e["x"]==300&&e["y"]==200&&e["source"]=="visibility");
 check("target-root-foreground-are-separate",e["hit"]["hwnd"]==801&&e["hitRoot"]["hwnd"]==802&&e["foregroundBefore"]["hwnd"]==800);
 check("known-class-is-bounded-enum",e["hit"]["windowClass"]=="SDL_app");
 raw(CURSOR_SHOWING);check("first-recovery-captures-second-and-final-context",cursorcontext::count==2&&sim::contextHitReads==2);
 const auto calls=windowCalls();for(int n=0;n<10000;n++){raw(n%2?CURSOR_SHOWING:0,n%2?1:0);}
 check("10000-alternating-samples-add-no-more-window-queries",cursorcontext::count==2&&windowCalls()==calls);
 const auto exported=cursorcontext::state();check("state-export-adds-no-window-queries",windowCalls()==calls);
 check("export-does-not-claim-cursor-caller-or-history",exported["cursorApiCallerProven"]==false&&exported["windowIdentityHistorical"]==false&&exported["windowTextRetained"]==false);
 cursorcontext::end();raw(CURSOR_SHOWING);raw(0,0);check("end-prevents-retained-or-queued-rearming",windowCalls()==calls);
 cursorcontext::begin(true);raw(CURSOR_SHOWING);raw(0,0);check("new-gesture-has-independent-pair-budget",cursorcontext::count==3&&sim::contextHitReads==3);
 fresh();cursorcontext::begin(true);raw(CURSOR_SHOWING);raw(0,0,CursorSampleSource::Periodic,false);raw(0,0,CursorSampleSource::Release,true,false);raw(0,0,CursorSampleSource::Periodic,true,true,false);
 check("invalid-retained-and-mixed-observations-do-not-capture",windowCalls()==0);
 cursorcontext::begin(false);raw(CURSOR_SHOWING);raw(0,0);check("Mouse-session-does-not-capture",windowCalls()==0);
 fresh();cursorcontext::begin(true);raw(CURSOR_SHOWING);sim::contextForegroundAfter=(HWND)(uintptr_t)803;raw(0,0);
 check("foreground-change-during-sequential-reads-is-labeled",cursorcontext::state()["entries"][0]["foregroundHandleStable"]==false);
 fresh();cursorcontext::begin(true);raw(CURSOR_SHOWING);sim::contextHit=nullptr;sim::contextIdentityFails=true;sim::contextClass=L"PRIVATE_NAME_NEVER_EXPORT";raw(0,0);
 j=cursorcontext::state();check("null-hit-keeps-zero-root-without-ancestor-query",j["entries"][0]["hit"]["hwnd"]==0&&j["entries"][0]["hitRoot"]["hwnd"]==0&&sim::contextAncestorReads==0);
 check("failed-identity-is-not-reported-as-known",j["entries"][0]["foregroundBefore"]["identityReadOk"]==false);
 check("arbitrary-window-class-is-not-exported",j.dump().find("PRIVATE_NAME_NEVER_EXPORT")==std::string::npos);
 fresh();for(int n=0;n<20;n++){cursorcontext::begin(true);raw(CURSOR_SHOWING);raw(0,0);raw(CURSOR_SHOWING);}
 j=cursorcontext::state();check("fixed16-entry-ring-retains-last-pairs",j["total"]==40&&j["entries"].size()==16&&j["entries"][0]["sequence"]==25);
 check("ring-gestures-and-pair-limit-remain-honest",j["gestures"]==20&&j["maxCapturesPerGesture"]==2);
 // Integration: production contact adapter starts and ends context without changing display.
 fresh();sim::current={sizeof(CURSORINFO),CURSOR_SHOWING,(HCURSOR)1,{300,200}};core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);drain();
 sim::now+=24;sim::current.flags=0;sim::current.hCursor=nullptr;cursorSync();
 check("production-SHOWING-hidden-observation-captures-with-Deck-visibility-override",cursorcontext::count==1&&visible()&&sim::shows==1&&sim::uploads==1);
 sim::now+=24;sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=(HCURSOR)1;cursorSync();
 check("production-recovery-captures-and-retires-Deck-visibility-override",cursorcontext::count==2&&!visible()&&sim::shows==1&&sim::uploads==1);
 cursorStop();check("production-stop-ends-context",!cursorcontext::enabled);
 check("original-position-and-MOVE-fallback-budgets-retained",kCursorActiveFallbackMs==24&&kCursorDeckMoveFallbackMs==48);
}
}
int main(int argc,char** argv){if(argc!=2)return 2;ctxtest::run();Json out={{"ok",failures==0},{"checks",cases.size()},{"cases",cases},{"windowCallsAreSubstituted",true},{"actualInputCalls",0},{"realWindowsCreated",0},{"fieldFlickerVerified",false}};std::ofstream(argv[1])<<out.dump(2)<<'\n';return failures?1:0;}
