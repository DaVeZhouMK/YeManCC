// Offline actual-header replay. No Windows graphics/input DLL imports.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static unsigned fixtureExportThreadOpens=0,fixtureExportThreadCloses=0;
static BOOL fixtureCloseExportThread(HANDLE h){++fixtureExportThreadCloses;return CloseHandle(h);}
#define CloseHandle fixtureCloseExportThread
static DWORD actualFixtureThread(){return GetCurrentThreadId();}
static HANDLE fixtureOpenExportThread(DWORD access,BOOL inherit,DWORD tid){++fixtureExportThreadOpens;return OpenThread(access,inherit,tid);}
#define OpenThread fixtureOpenExportThread
#define main retained_fixture_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
#include "screen_touchpad_field_193544_fixture.h"
static Json fieldReplay;
static void runField(){
 reset();down();drain();sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=(HCURSOR)(uintptr_t)65539;sim::now+=24;edge();drain();
 const auto reads=sim::reads,shows=sim::shows,hides=sim::hides,uploads=sim::uploads,placements=sim::positions+sim::moves;
 const auto begin=sim::now;const auto base=capturedCursorSamples[0].tick;
 for(const auto& sample:capturedCursorSamples){
    sim::now=begin+(sample.tick-base);sim::current={sizeof(CURSORINFO),sample.flags,(HCURSOR)sample.shape,{sample.x,sample.y}};
    edge(sample.flags?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);drain();bus();
 }
 const auto used=sim::reads-reads;const auto cap=std::size(capturedCursorSamples);
 fieldReplay={{"capturedQueryRows",std::size(capturedCursorSamples)},{"windowMs",sim::now-begin},{"currentQueryCalls",used},{"capturedQueryRowCount",cap},{"shows",sim::shows-shows},{"hides",sim::hides-hides},{"uploads",sim::uploads-uploads},{"positionCalls",sim::positions+sim::moves-placements},
    {"scope","One notification per captured query row plus existing BUS opportunity; query row is not original callback identity. Not real Steam consumption or full-field query count."}};
 check("captured-0-1-rows-keep-helper-hidden-with-no-output",!visible()&&sim::shows==shows&&sim::hides==hides&&sim::uploads==uploads&&sim::positions+sim::moves==placements);
 check("hidden-active-query-replay-skips-ineligible-reads",used<cap);

 // Skip within the old slot, but do not extend the original recovery time.
 reset();down();drain();sim::now+=1;sim::current.flags=CURSOR_SHOWING;edge();drain();
 const auto deadline=cursorDeckNextPositionTick;sim::now+=1;const auto beforeHiddenReads=sim::reads;edge();drain();
 check("hidden-query-before-slot-is-eliminated",sim::reads==beforeHiddenReads&&!visible()&&cursorDeckNextPositionTick==deadline);
 sim::now=deadline;sim::current.flags=CURSOR_SHOWING;edge();drain();
 const auto afterDueDeadline=cursorDeckNextPositionTick;sim::now+=1;sim::current.flags=CURSOR_SUPPRESSED;sim::current.ptScreenPos.x++;edge(EVENT_OBJECT_HIDE);drain();
 check("due-0-1-query-does-not-delay-later-suppression-recovery",visible()&&afterDueDeadline==deadline&&cursorDeckNextPositionTick==sim::now+kCursorActiveFallbackMs);
 // Existing pixels must still be withdrawn immediately despite the budget.
 sim::now+=1;sim::current.flags=0;const auto hideBefore=sim::hides;edge(EVENT_OBJECT_HIDE);drain();
 check("visible-helper-still-respects-explicit-hidden-state-immediately",!visible()&&sim::hides==hideBefore+1);
 reset();down();drain();core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
 sim::now+=1;sim::current.flags=CURSOR_SHOWING;edge();drain();
 check("retained-handoff-is-not-throttled",!cursorRetained&&!visible()&&!cursorvisibility::hook);
 reset();down();drain();sim::now+=1;sim::current.flags=CURSOR_SHOWING;edge();drain();
 sim::now=cursorDeckNextPositionTick;const auto failedDeadline=cursorDeckNextPositionTick;sim::readResults.push_back(false);edge();drain();
 check("failed-hidden-query-keeps-original-retry-deadline",cursorDeckNextPositionTick==failedDeadline&&!visible());
 sim::current.flags=CURSOR_SUPPRESSED;sim::current.ptScreenPos.x++;sim::now+=1;edge(EVENT_OBJECT_HIDE);drain();
 check("failed-hidden-query-can-recover-next-original-opportunity",visible());
 reset();down();drain();sim::now+=1;sim::current.flags=CURSOR_SHOWING;edge();drain();
 sim::now=cursorDeckNextPositionTick;sim::current.flags=CURSOR_SUPPRESSED;sim::current.ptScreenPos.x++;cursorRequestSync();auto periodic=sim::messages.front();sim::messages.pop_front();edge(EVENT_OBJECT_HIDE);auto note=sim::messages.front();sim::messages.pop_front();dispatch(note);const auto queried=sim::reads;dispatch(periodic);
 check("eligible-visibility-and-queued-periodic-still-share-one-slot",visible()&&sim::reads==queried);
 reset();down();drain();const auto traceBefore=cursorvisibility::state().value("eventTrace",Json::object()).value("total",0ULL);
 cursorvisibility::onEvent(cursorvisibility::hook,EVENT_OBJECT_SHOW,nullptr,OBJID_CURSOR,0,101,(DWORD)sim::now);
 cursorvisibility::onEvent(cursorvisibility::hook,EVENT_OBJECT_HIDE,nullptr,OBJID_CURSOR,0,202,(DWORD)sim::now);
 auto raw=cursorvisibility::state().value("eventTrace",Json::object());auto entries=raw.value("entries",Json::array());
 check("callback-source-threads-survive-message-coalescing",raw.value("total",0ULL)==traceBefore+2&&entries.size()>=2&&entries[entries.size()-2].value("sourceThreadId",0)==101&&entries.back().value("sourceThreadId",0)==202&&entries.back().value("observerThreadId",0)==7);
 for(int i=0;i<80;++i)cursorvisibility::onEvent(cursorvisibility::hook,EVENT_OBJECT_HIDE,nullptr,OBJID_CURSOR,0,303,(DWORD)sim::now);
 raw=cursorvisibility::state().value("eventTrace",Json::object());entries=raw.value("entries",Json::array());
 check("callback-source-record-is-fixed-capacity-and-keeps-newest",entries.size()==64&&raw.value("total",0ULL)==traceBefore+82&&entries.front().value("sequence",0ULL)==traceBefore+19&&entries.back().value("sourceThreadId",0)==303);
 drain();
 const auto opensBefore=fixtureExportThreadOpens,closesBefore=fixtureExportThreadCloses;
 Json captured={{"entries",Json::array({{{"sourceThreadId",actualFixtureThread()}},{{"sourceThreadId",actualFixtureThread()}},{{"sourceThreadId",0}}})}};
 Json owners=Json::object();
#ifdef YMCC_CURSOR_EVENT_EXPORT_FIXTURE_ENABLED
 owners=cursorvisibility::eventThreadOwnersAtExport(captured);
#endif
 const auto rows=owners.value("owners",Json::array());
 check("explicit-export-resolves-live-thread-once-and-closes-handle",fixtureExportThreadOpens==opensBefore+1&&fixtureExportThreadCloses==closesBefore+1&&rows.size()==1&&rows[0].value("resolved",false)&&rows[0].value("sameProcessAtExport",false));
 check("late-thread-resolution-never-claims-historical-identity",owners.value("lateResolution",false)&&!owners.value("historicalEventProcessProven",true));
 Json many={{"entries",Json::array()}};for(unsigned i=0;i<16;++i)many["entries"].push_back({{"sourceThreadId",0xF0000000u+i}});
 const auto manyBefore=fixtureExportThreadOpens;Json bounded=Json::object();
#ifdef YMCC_CURSOR_EVENT_EXPORT_FIXTURE_ENABLED
 bounded=cursorvisibility::eventThreadOwnersAtExport(many);
#endif
 check("explicit-export-limits-thread-probes-to-eight",fixtureExportThreadOpens==manyBefore+8&&bounded.value("owners",Json::array()).size()==8&&bounded.value("omittedUnresolvedEntries",0)==8);
 reset();check("field-replay-cleans-fake-resources",sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty()&&!sim::activeHook);
}
int main(int argc,char** argv){if(argc!=2)return 2;try{runField();}catch(const std::exception& e){++failures;cases.push_back({{"name","fixture-exception"},{"ok",false},{"error",e.what()}});}Json report={{"ok",failures==0},{"cases",cases},{"fieldReplay",fieldReplay},{"failures",failures},{"fieldFlickerVerified",false},{"realInputInjected",0},{"realWindowsCreated",0},{"hardwareCalls",0}};std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';return out?(failures?1:0):2;}
