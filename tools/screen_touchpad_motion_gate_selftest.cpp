// Dedicated offline motion-admission tests. The included harness substitutes every
// cursor/User32/GDI dependency; its old scenario setup and main are never run.
#define main cursor_original_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
#include <filesystem>
#include <set>

#ifndef YMCC_TEST_MOTION_GATE
#define YMCC_TEST_MOTION_GATE 0
#endif
namespace motion_test {
static Json assertions=Json::array(),fieldRows=Json::array(),scenarios=Json::array();
static unsigned failed=0;
static const char* limitation="Compressed records retain first/last/min/max, not every frame. Only known first/last endpoints are replayed; extrema and missing samples are never synthesized. The ring begins mid-session; modeled segment starts are not evidence of physical DOWN times. A last endpoint is Periodic; the first sample source is not reused as a second Contact.";
static bool shown(){return cursorVisible&&cursorWindow&&sim::windows.count((uintptr_t)cursorWindow)&&sim::windows.at((uintptr_t)cursorWindow).visible;}
static Json counts(){return {{"queries",sim::reads},{"uploads",sim::uploads},{"positionOnlyMoves",sim::moves},{"placements",sim::positions},{"shows",sim::shows},{"hides",sim::hides}};}
static void expect(const char* name,bool ok,const char* category="preserved"){
    Json row={{"name",name},{"ok",ok},{"category",category},{"tick",sim::now},{"flags",sim::current.flags},{"shape",(uintptr_t)sim::current.hCursor},
        {"helperVisible",shown()},{"tracking",cursorTracking.load()},{"retained",cursorRetained.load()},{"deadline",cursorDeckNextPositionTick},{"calls",counts()}};
#if YMCC_TEST_MOTION_GATE
    row["motionPointKnown"]=cursorDeckMotionPointKnown;row["motionAuthorized"]=cursorDeckSuppressedMotion;
#endif
    assertions.push_back(row);if(!ok){++failed;std::cerr<<"FAIL "<<name<<'\n';}
}
static void clean(){reset();sim::messages.clear();}
static void setRaw(ULONGLONG tick,DWORD flags,LONG x,LONG y,uintptr_t shape=1){
    if(tick<sim::now)throw std::runtime_error("Replay time moved backwards");
    sim::now=tick;sim::current={sizeof(CURSORINFO),flags,(HCURSOR)shape,{x,y}};
}
// Set actual contacts and call the production owner adapter, without the old down().
static void startRaw(ULONGLONG tick=1000,DWORD flags=CURSOR_SUPPRESSED,LONG x=200,LONG y=150,uintptr_t shape=1,int flavor=0){
    setRaw(tick,flags,x,y,shape);core={};core.modes[0]=flavor==1?Mode::Mouse:Mode::Deck;
    core.pads[0].down=true;if(flavor==2){core.modes[1]=Mode::Mouse;core.pads[1].down=true;}
    cursorContactChanged(sim::dispatchWindow);drain();
}
static void due(DWORD flags,LONG x,LONG y,uintptr_t shape=1){
    const ULONGLONG tick=std::max(sim::now+kCursorActiveFallbackMs,cursorDeckNextPositionTick);
    setRaw(tick,flags,x,y,shape);cursorRequestSync();drain();
}
static void notify(DWORD flags,LONG x,LONG y,uintptr_t shape=1,ULONGLONG advance=1){
    setRaw(sim::now+advance,flags,x,y,shape);edge((flags&CURSOR_SHOWING)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);drain();
}
static void up(){core.pads[0].down=core.pads[1].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();}
static bool atRaw(){if(!shown())return false;const auto& w=sim::windows.at((uintptr_t)cursorWindow);return w.position.x==sim::current.ptScreenPos.x-cursorHotspot.x&&w.position.y==sim::current.ptScreenPos.y-cursorHotspot.y;}
static void save(const char* name){scenarios.push_back({{"name",name},{"calls",counts()}});}
static void stationary(){
    clean();startRaw(1000,CURSOR_SUPPRESSED,200,150,0);expect("stationary-down-no-fallback",!shown()&&sim::shows==0&&sim::uploads==0,"motion-policy14");
    const auto reads=sim::reads;for(int i=0;i<100;++i)cursorContactChanged(sim::dispatchWindow,false,true);
    expect("Deck-MOVE-does-not-query-immediately",sim::reads==reads);
    due(CURSOR_SUPPRESSED,200,150,0);expect("stationary-due-no-old-arrow",!shown()&&sim::shows==0,"motion-policy14");
    up();expect("stationary-up-retained-no-fallback",!shown()&&sim::shows==0,"motion-policy14");
    expect("stationary-UP-may-retain-hidden-owner",!cursorTracking&&cursorRetained);
    due(CURSOR_SUPPRESSED,200,150,0);expect("stationary-retained-retry-no-old-arrow",!shown()&&sim::shows==0,"motion-policy14");
    // A hidden retained session is a new gesture too: no inherited authorization.
    setRaw(sim::now+1,CURSOR_SUPPRESSED,200,150,0);core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);drain();
    expect("hidden-retained-recontact-is-anchor-only",!shown()&&sim::shows==0,"motion-policy14");
    save("stationary-active-retained-recontact");
}
static void moving(){
    clean();startRaw();due(CURSOR_SUPPRESSED,212,157);
    expect("suppressed-real-OS-motion-admits-fallback",shown()&&atRaw());
    const auto imageUploads=sim::uploads;due(CURSOR_SUPPRESSED,223,165);
    expect("admitted-fallback-follows-suppressed-motion",shown()&&atRaw()&&sim::uploads==imageUploads);
    const auto writes=sim::moves+sim::positions;due(CURSOR_SUPPRESSED,223,165);
    expect("visible-stationary-frame-is-kept-without-placement",shown()&&sim::moves+sim::positions==writes);
    notify(CURSOR_SHOWING,223,165);expect("SHOWING-handoff-is-immediate-even-before-deadline",!shown());
    due(CURSOR_SUPPRESSED,223,165);expect("SHOWING-revokes-motion-authorization",!shown(),"motion-policy14");
    due(CURSOR_SUPPRESSED,230,170);expect("new-suppressed-motion-rearms-after-SHOWING",shown()&&atRaw());
    notify(0,230,170,0);expect("flags0-hides-immediately-even-before-deadline",!shown());
    due(CURSOR_SUPPRESSED,230,170,0);expect("flags0-revokes-motion-authorization",!shown(),"motion-policy14");
    due(CURSOR_SUPPRESSED,231,170,0);expect("new-suppressed-motion-rearms-after-flags0",shown()&&atRaw());
    save("movement-handoffs-and-rearming");
}
static void fallbackAndRecontact(){
    clean();startRaw(1000,CURSOR_SUPPRESSED,200,150,0);due(CURSOR_SUPPRESSED,210,155,0);
    expect("NULL-shape-can-fallback-after-real-motion",shown()&&cursorShape&&atRaw());
    const auto cached=cursorShape;notify(CURSOR_SHOWING,210,155);cursorStop();core.pads={};drain();
    startRaw(sim::now+1,CURSOR_SUPPRESSED,600,400,0);
    expect("cached-image-is-not-permission-on-new-stationary-DOWN",!shown()&&cursorShape==cached,"motion-policy14");
    up();expect("cached-image-is-not-permission-on-new-stationary-UP",!shown(),"motion-policy14");
    save("null-shape-and-cached-recontact");
    clean();startRaw(1000,CURSOR_SHOWING);due(CURSOR_SUPPRESSED,211,156);
    notify(CURSOR_SHOWING,211,156,0);
    expect("visible-SHOWING-null-preserves-existing-frame",shown());
    notify(0,211,156,0);expect("visible-null-shape-still-respects-flags0",!shown());
    save("visible-frame-null-shape-and-hidden-state");
}
static void failureRetries(){
    clean();startRaw(1000,CURSOR_SHOWING);sim::failUpload=true;due(CURSOR_SUPPRESSED,215,160);
    expect("upload-failure-keeps-hidden-contact-owner",!shown()&&cursorTracking&&core.pads[0].down);
    sim::failUpload=false;due(CURSOR_SUPPRESSED,215,160);
    expect("upload-failure-authorized-stationary-retry-recovers",shown()&&atRaw());
    save("upload-failure-retry");

    clean();startRaw(1000,CURSOR_SHOWING);sim::readResults={true,false};due(CURSOR_SUPPRESSED,214,159);
    expect("first-show-recheck-failure-does-not-show",!shown()&&cursorTracking&&sim::readResults.empty());
    due(CURSOR_SUPPRESSED,214,159);
    expect("recheck-failure-authorized-stationary-retry-recovers",shown()&&atRaw());
    save("preparation-query-failure-retry");

    clean();startRaw(1000,CURSOR_SHOWING);sim::failUpload=true;due(CURSOR_SUPPRESSED,216,161);sim::failUpload=false;
    sim::readResults={false};due(CURSOR_SUPPRESSED,216,161);
    expect("periodic-query-failure-does-not-revoke-known-motion-or-show",!shown()&&cursorTracking&&sim::readResults.empty());
    due(CURSOR_SUPPRESSED,216,161);
    expect("query-failure-authorized-stationary-retry-recovers",shown()&&atRaw());
    save("periodic-query-failure-after-motion");

    clean();startRaw(1000,CURSOR_SHOWING);sim::failUpload=true;due(CURSOR_SUPPRESSED,225,165);
    expect("SHOWING-null-boundary-begins-with-hidden-upload-failure",!shown()&&cursorTracking);
    sim::failUpload=false;const auto nullReads=sim::reads;
    const auto nullTick=std::max(sim::now+kCursorActiveFallbackMs,cursorDeckNextPositionTick);
    notify(CURSOR_SHOWING,225,165,0,nullTick-sim::now);
    expect("SHOWING-null-visibility-query-runs-at-deadline",sim::reads==nullReads+1&&!shown());
    due(CURSOR_SUPPRESSED,225,165,0);
    expect("SHOWING-null-visibility-clears-failed-upload-motion-latch",!shown(),"motion-policy14");
    due(CURSOR_SUPPRESSED,226,165,0);
    expect("fresh-motion-after-SHOWING-null-can-recover",shown()&&atRaw());
    save("SHOWING-null-visibility-revokes-hidden-authorization");

    clean();startRaw();up();cursorHideVisual();const auto deferredReads=sim::reads;
    const auto deferredWrites=sim::uploads+sim::moves+sim::positions;
    notify(CURSOR_SUPPRESSED,209,156,1,1);notify(CURSOR_SUPPRESSED,200,150,1,1);
    expect("retained-deferred-motion-observations-query-without-placement",sim::reads==deferredReads+2&&sim::uploads+sim::moves+sim::positions==deferredWrites&&!shown());
    due(CURSOR_SUPPRESSED,200,150);
    expect("retained-deferred-roundtrip-latches-motion-for-stationary-retry",shown()&&cursorRetained);
    save("retained-deferred-real-roundtrip");

    clean();sim::readResults={false};startRaw();
    expect("failed-first-query-is-not-a-coordinate-anchor",!shown());
    due(CURSOR_SUPPRESSED,250,190);
    expect("first-success-after-query-failure-only-anchors",!shown(),"motion-policy14");
    due(CURSOR_SUPPRESSED,250,190);expect("second-stationary-success-still-does-not-show",!shown(),"motion-policy14");
    due(CURSOR_SUPPRESSED,251,190);expect("motion-after-first-query-failure-can-show",shown());
    save("initial-query-failure-is-not-motion");

    clean();startRaw(1000,CURSOR_SHOWING);sim::failUpload=true;due(CURSOR_SUPPRESSED,217,162);
    up();expect("authorized-hidden-UP-does-not-lose-retained-owner",!shown()&&cursorRetained&&!cursorTracking);
    sim::failUpload=false;due(CURSOR_SUPPRESSED,217,162);expect("authorized-upload-retry-can-recover-after-UP",shown()&&cursorRetained);
    save("authorized-retained-retry");
}
static void budget(){
    clean();startRaw(1000,0,200,150,0);const auto before=sim::reads;const auto deadline=cursorDeckNextPositionTick;
    for(int i=0;i<100;++i){setRaw(1001+(i/5),0,200,150,0);edge(EVENT_OBJECT_HIDE);drain();}
    expect("hidden-event-burst-before-slot-adds-no-query",sim::reads==before);
    setRaw(deadline,CURSOR_SUPPRESSED,201,150,1);edge(EVENT_OBJECT_HIDE);drain();const auto after=sim::reads;
    expect("hidden-event-at-slot-allows-one-opportunity-and-preparation-recheck",after==before+2&&shown());
    cursorRequestSync();drain();expect("queued-periodic-shares-event-slot",sim::reads==after);
    expect("original-24ms-budget-preserved",kCursorActiveFallbackMs==24&&cursorDeckNextPositionTick>=deadline+24);
    save("hidden-slot-budget");
}
static void mouseAndMixed(){
    for(int flavor:{1,2}){
        const char* prefix=flavor==1?"Mouse":"mixed";clean();startRaw(1000,CURSOR_SUPPRESSED,200,150,1,flavor);
        expect(flavor==1?"Mouse-stationary-DOWN-keeps-existing-fallback":"mixed-stationary-DOWN-keeps-existing-fallback",shown()&&!cursorDeckOnlyTracking);
        setRaw(1001,CURSOR_SUPPRESSED,201,150);cursorContactChanged(sim::dispatchWindow);drain();
        expect(flavor==1?"Mouse-MOVE-keeps-immediate-positioning":"mixed-MOVE-keeps-immediate-positioning",shown()&&atRaw());
        setRaw(1002,CURSOR_SHOWING,201,150);cursorContactChanged(sim::dispatchWindow);drain();
        expect(flavor==1?"Mouse-SHOWING-keeps-existing-grace":"mixed-SHOWING-keeps-existing-grace",shown());
        setRaw(1042,CURSOR_SHOWING,201,150);cursorContactChanged(sim::dispatchWindow);drain();
        setRaw(1082,CURSOR_SHOWING,201,150);cursorContactChanged(sim::dispatchWindow);drain();
        expect(flavor==1?"Mouse-grace-completes":"mixed-grace-completes",!shown());
        save(prefix);
    }
    clean();startRaw();due(CURSOR_SUPPRESSED,220,160);notify(0,220,160,0);
    core.modes[1]=Mode::Mouse;core.pads[1].down=true;setRaw(sim::now+1,0,220,160,0);cursorContactChanged(sim::dispatchWindow);drain();
    core.pads[1].down=false;setRaw(sim::now+1,CURSOR_SUPPRESSED,220,160,0);cursorContactChanged(sim::dispatchWindow);drain();
    expect("mixed-to-hidden-Deck-switch-requires-new-anchor",!shown()&&cursorDeckOnlyTracking,"motion-policy14");
    due(CURSOR_SUPPRESSED,221,160);expect("mixed-to-Deck-motion-can-rearm",shown());
    cursorStop();core.pads={};drain();expect("cancel-stops-display-and-owner",!shown()&&!cursorTracking&&!cursorRetained&&!cursorvisibility::hook);
    save("mode-switch-and-cancel");
}
static Json load(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Replay input unavailable");return Json::parse(in);}
static Json replay(const Json& trace,const Json& segments,Json& replayInput){
    if(!trace.is_array()||!segments.is_array())throw std::runtime_error("Expected array replay inputs");
    unsigned valid=0,invalid=0,endpoints=0,suppressed=0,suppressedMotion=0;bool correlated=true,allSuppressedHidden=true,allShowingHidden=true;
    bool contactConfirmed=false,allUnconfirmedSuppressedHidden=true;
    std::set<unsigned long long> observedSegments;Json endpointsInput=Json::array();
    clean();ULONGLONG previous=sim::now;
    for(const auto& row:trace){
        const bool good=row.value("valid",false);const auto sequence=row.at("sequence").get<unsigned long long>();
        if(!good){++invalid;const auto tick=row.at("tick").get<ULONGLONG>();if(tick<previous)throw std::runtime_error("Unordered lifecycle record");sim::now=tick;previous=tick;core.pads={};cursorStop();drain();continue;}
        ++valid;const Json* segment=nullptr;for(const auto& item:segments){if(sequence>=item.at("startSeq").get<unsigned long long>()&&sequence<=item.at("lastSeq").get<unsigned long long>()){if(segment)throw std::runtime_error("Overlapping contact segments");segment=&item;}}
        if(!segment){correlated=false;throw std::runtime_error("Valid trace row has no contact segment");}
        const auto start=segment->at("startSeq").get<unsigned long long>();const bool newSegment=observedSegments.insert(start).second;
        const DWORD flags=row.at("flags").get<DWORD>();const bool isSuppressed=(flags&CURSOR_SUPPRESSED)&&!(flags&CURSOR_SHOWING);
        if(isSuppressed){++suppressed;if(row.value("positionChanges",0)>0||row.at("minX")!=row.at("maxX")||row.at("minY")!=row.at("maxY"))++suppressedMotion;}
        const auto first=row.at("tick").get<ULONGLONG>(),last=row.at("lastTick").get<ULONGLONG>();
        const bool second=last!=first||row.at("x")!=row.at("lastX")||row.at("y")!=row.at("lastY");
        for(int endpoint=0;endpoint<(second?2:1);++endpoint){
            const auto tick=endpoint?last:first;const LONG x=row.at(endpoint?"lastX":"x").get<LONG>(),y=row.at(endpoint?"lastY":"y").get<LONG>();
            if(tick<previous)throw std::runtime_error("Overlapping compressed record endpoints");previous=tick;
            const auto shape=row.at("shape").get<uintptr_t>();const auto before=counts();
            const auto source=endpoint?std::string("periodic"):row.value("source",std::string("periodic"));
            if(newSegment&&endpoint==0){contactConfirmed=false;if(cursorTracking||cursorRetained){core.pads={};cursorStop();drain();}startRaw(tick,flags,x,y,shape);}
            else{setRaw(tick,flags,x,y,shape);
                if(source=="contact"){cursorContactChanged(sim::dispatchWindow);drain();}
                else if(source=="visibility"){edge((flags&CURSOR_SHOWING)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);drain();}
                else{cursorRequestSync();drain();}}
            // Oracle input is only the modeled Contact source and observed flags/handle,
            // never production authorization fields or the field helper visibility.
            if(!isSuppressed)contactConfirmed=false;
            else if(shape && ((newSegment&&endpoint==0)||source=="contact"))contactConfirmed=true;
            if(isSuppressed&&shown()&&!contactConfirmed)allUnconfirmedSuppressedHidden=false;
            ++endpoints;endpointsInput.push_back({{"sequence",sequence},{"segmentStartSeq",start},{"endpoint",endpoint?"last":"first"},{"tick",tick},{"flags",flags},{"shape",shape},{"x",x},{"y",y},{"recordedSource",row.at("source")},{"modeledSource",source},{"modeledGestureStart",newSegment&&endpoint==0}});
            fieldRows.push_back({{"sequence",sequence},{"endpoint",endpoint?"last":"first"},{"tick",tick},{"flags",flags},{"x",x},{"y",y},{"fieldHelperVisible",row.at("fallbackVisible")},{"replayHelperVisible",shown()},{"callsBefore",before},{"callsAfter",counts()}});
            if(isSuppressed&&shown())allSuppressedHidden=false;if((flags&CURSOR_SHOWING)&&shape&&shown())allShowingHidden=false;
        }
    }
    expect("field-input-is-64-compressed-records-45-valid-19-lifecycle",trace.size()==64&&valid==45&&invalid==19);
    expect("field-contact-segments-match-valid-records",correlated&&observedSegments.size()==segments.size());
    expect("field-contains-no-recorded-suppressed-coordinate-motion",suppressed==26&&suppressedMotion==0);
    expect("field-SHOWING-never-leaves-helper-visible",allShowingHidden);
    #if YMCC_TEST_CONTACT_SHAPE_BRIDGE
    expect("field-no-fallback-from-unconfirmed-stationary-suppression",allUnconfirmedSuppressedHidden,"contact-shape-policy16");
#else
    expect("field-no-fallback-from-stationary-suppression",allSuppressedHidden&&sim::shows==0,"motion-policy14");
#endif
    replayInput={{"schemaVersion",1},{"limitation",limitation},{"recordCount",trace.size()},{"validRecordCount",valid},{"invalidLifecycleRecordCount",invalid},{"endpointCount",endpoints},
        {"originalCompressedRecords",trace},{"contactSegments",segments},{"knownEndpointPlan",endpointsInput},{"interpolatedPositions",0},{"extremaUsedAsSamples",false},{"fieldHelperVisibleUsedAsDecisionInput",false}};
    return {{"compressedRecords",trace.size()},{"validRecords",valid},{"invalidLifecycleRecords",invalid},{"knownEndpointsReplayed",endpoints},{"contactSegments",observedSegments.size()},
        {"suppressedRecords",suppressed},{"recordedSuppressedMotionRecords",suppressedMotion},{"helperShows",sim::shows},{"helperHides",sim::hides},{"calls",counts()},
        {"modeledInitialState","hidden cold-start at first retained segment; cache preserved between recorded lifecycle boundaries"},{"limitation",limitation},{"rows",fieldRows}};
}
} // namespace motion_test
int wmain(int argc,wchar_t** argv){
    if(argc!=5){std::cerr<<"Usage: motion-gate.exe trace.json segments.json report.json verify|baseline\n";return 2;}
    using namespace motion_test;Json field,replayInput;std::string error;const auto output=std::filesystem::path(argv[3]);
    try{const auto trace=load(argv[1]),segments=load(argv[2]);stationary();moving();fallbackAndRecontact();failureRetries();budget();mouseAndMixed();field=replay(trace,segments,replayInput);clean();}
    catch(const std::exception& e){error=e.what();expect("fixture-exception",false);}
    Json result={{"schemaVersion",1},{"ok",failed==0},{"failures",failed},{"baseline",std::wstring(argv[4])==L"baseline"},{"motionGateFeatureCompiled",YMCC_TEST_MOTION_GATE!=0},
        {"scope","actual selected production cursor headers; API substitutes only; compressed field endpoint replay plus independent synthetic assertions"},
        {"realWindowsCreated",0},{"realHooksRegistered",0},{"realUser32Calls",0},{"realGdiCalls",0},{"realInputInjected",0},{"hardwareCalls",0},
        {"fieldFlickerVerified",false},{"physicalInputCoverageVerified",false},{"processCpuMeasured",false},{"cases",assertions},{"scenarios",scenarios},{"fieldReplay",field},{"error",error}};
    std::ofstream out(output,std::ios::binary);out<<result.dump(2)<<'\n';if(!out)return 2;
    if(!replayInput.is_null()){std::ofstream plan(output.parent_path()/L"replay-input.json",std::ios::binary);plan<<replayInput.dump(2)<<'\n';if(!plan)return 2;}
    std::cout<<"Motion gate: "<<assertions.size()<<" assertions, "<<failed<<" failures; field replay is compressed endpoint evidence, not a physical run.\n";return failed?1:0;
}