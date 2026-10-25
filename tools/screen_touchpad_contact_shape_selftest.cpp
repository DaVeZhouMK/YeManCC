// Offline contact-shape admission tests using real selected cursor headers.
// Original harness main/down are compiled under another name, never invoked.
#define main cursor_original_main
#include "screen_touchpad_cursor_r1_simulation.cpp"
#undef main
#include <filesystem>
#ifndef YMCC_TEST_CONTACT_SHAPE
#define YMCC_TEST_CONTACT_SHAPE 0
#endif
namespace shape_test {
static Json assertions=Json::array(),replayRows=Json::array();
static unsigned failed=0;static bool inputUnchanged=true;
static const char* limits="Compressed records contain first/last/min/max and counters, not all frames. Only recorded first/last points are replayed. No interpolation/extrema frames are invented. source describes the first compressed sample; a last endpoint is modeled as Periodic, not granted a fabricated Contact source. First-show preparation rereads hold the current substitute sample; original preparation-time state is unknown. Offline helper decisions do not establish real pixels, display continuity or a field fix.";
static bool shown(){return cursorVisible&&cursorWindow&&sim::windows.count((uintptr_t)cursorWindow)&&sim::windows.at((uintptr_t)cursorWindow).visible;}
static Json owner(){return {{"down0",core.pads[0].down},{"down1",core.pads[1].down},{"mode0",(int)core.modes[0]},{"mode1",(int)core.modes[1]}};}
static Json counts(){return {{"queries",sim::reads},{"uploads",sim::uploads},{"moves",sim::moves},{"placements",sim::positions},{"shows",sim::shows},{"hides",sim::hides},{"clockReads",sim::tick64Reads}};}
static uintptr_t authorization(){
#if YMCC_TEST_CONTACT_SHAPE
 return (uintptr_t)cursorDeckContactShape;
#else
 return 0;
#endif
}
static unsigned long long bridgeShows(){
#if YMCC_TEST_CONTACT_SHAPE
 return cursorDeckContactShapeBridgeShows;
#else
 return 0;
#endif
}
static void expect(const std::string& name,bool ok,const char* category="preserved"){
 Json item={{"name",name},{"ok",ok},{"category",category},{"tick",sim::now},{"flags",sim::current.flags},{"osShape",(uintptr_t)sim::current.hCursor},{"cachedShape",(uintptr_t)cursorShape},{"contactShape",authorization()},{"bridgeShows",bridgeShows()},{"helperVisible",shown()},{"tracking",cursorTracking.load()},{"retained",cursorRetained.load()},{"deadline",cursorDeckNextPositionTick},{"calls",counts()}};
 assertions.push_back(item);if(!ok){++failed;std::cerr<<"FAIL "<<name<<'\n';}
}
static void operation(const std::function<void()>& fn){auto before=owner();fn();drain();inputUnchanged=inputUnchanged&&(before==owner());}
static void clean(){reset();sim::messages.clear();core={};}
static void raw(ULONGLONG tick,DWORD flags,LONG x,LONG y,uintptr_t shape){if(tick<sim::now)throw std::runtime_error("time moved backwards");sim::now=tick;sim::current={sizeof(CURSORINFO),flags,(HCURSOR)shape,{x,y}};}
static void startRaw(DWORD flags=CURSOR_SUPPRESSED,uintptr_t shape=7,LONG x=802,LONG y=404,int flavor=0,ULONGLONG tick=1000){
 raw(tick,flags,x,y,shape);core={};core.modes[0]=flavor==1?Mode::Mouse:Mode::Deck;core.pads[0].down=true;
 if(flavor==2){core.modes[1]=Mode::Mouse;core.pads[1].down=true;}
 operation([]{cursorContactChanged(sim::dispatchWindow);});
}
static void contact(){operation([]{cursorContactChanged(sim::dispatchWindow);});}
static void due(DWORD flags,uintptr_t shape,LONG x=802,LONG y=404){raw(std::max(sim::now+24,cursorDeckNextPositionTick),flags,x,y,shape);operation([]{cursorRequestAfterReport();});}
static void notify(DWORD flags,uintptr_t shape,LONG x=802,LONG y=404,bool atSlot=false){raw(atSlot?std::max(sim::now+24,cursorDeckNextPositionTick):sim::now+1,flags,x,y,shape);operation([=]{edge((flags&CURSOR_SHOWING)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);});}
static void up(){core.pads[0].down=core.pads[1].down=false;operation([]{cursorContactChanged(sim::dispatchWindow,true);});}
static bool atRaw(){if(!shown())return false;auto p=sim::windows.at((uintptr_t)cursorWindow).position;return p.x==sim::current.ptScreenPos.x-cursorHotspot.x&&p.y==sim::current.ptScreenPos.y-cursorHotspot.y;}
static void oldCache(){startRaw(CURSOR_SUPPRESSED,11,400,300,1);core.pads={};operation([]{cursorStop();});if(cursorShape!=(HCURSOR)(uintptr_t)11||!cursorImageUploaded)throw std::runtime_error("old cache setup failed");}
static void knownContact(){
 clean();auto before=bridgeShows();startRaw();expect("known-contact-stationary-shows-current-XY",shown()&&atRaw()&&cursorShape==(HCURSOR)(uintptr_t)7,"policy16");
 expect("known-contact-counts-one-bridge",bridgeShows()==before+1,"policy16");
 auto uploads=sim::uploads;due(CURSOR_SUPPRESSED,0);expect("known-contact-NULL-stationary-keeps-confirmed-shape",shown()&&atRaw()&&cursorShape==(HCURSOR)(uintptr_t)7&&sim::uploads==uploads,"policy16");
 up();expect("known-contact-UP-retains-existing-frame",shown()&&cursorRetained&&!cursorTracking,"policy16");
 notify(CURSOR_SHOWING,7);expect("retained-SHOWING-hands-back-and-releases-owner",!shown()&&!cursorRetained&&!cursorTracking);
}
static void noSpuriousAuthorization(){
 clean();oldCache();startRaw(CURSOR_SUPPRESSED,0,802,404,0,sim::now+1);
 expect("NULL-first-contact-cannot-show-old-cached-bitmap",!shown()&&cursorShape==(HCURSOR)(uintptr_t)11);
 due(CURSOR_SUPPRESSED,0);expect("NULL-stationary-contact-stays-hidden",!shown());
 due(CURSOR_SUPPRESSED,7);expect("Periodic-known-handle-cannot-authorize",!shown());
 notify(CURSOR_SUPPRESSED,7,802,404,true);expect("Visibility-known-handle-cannot-authorize",!shown());
 up();expect("unknown-UP-keeps-hidden-retained-owner",!shown()&&cursorRetained&&!cursorTracking);
 due(CURSOR_SUPPRESSED,7);expect("retained-known-handle-cannot-authorize",!shown());
 clean();startRaw();notify(CURSOR_SHOWING,7);expect("active-SHOWING-immediately-hides-bridge",!shown());
 due(CURSOR_SUPPRESSED,7);expect("Periodic-known-after-handoff-cannot-reauthorize",!shown());
 notify(CURSOR_SUPPRESSED,7,802,404,true);expect("Visibility-known-after-handoff-cannot-reauthorize",!shown());
 contact();expect("new-active-Contact-can-confirm-shape-again",shown()&&atRaw(),"policy16");
 notify(0,0);expect("flags0-immediately-hides-bridge",!shown());
 due(CURSOR_SUPPRESSED,0);expect("flags0-clears-contact-authorization",!shown()&&authorization()==0);
}
static void failuresAndCurrentShape(){
 clean();sim::failUpload=true;startRaw();expect("upload-failed-known-contact-remains-hidden",!shown());
 expect("upload-failure-retains-contact-shape-authorization",authorization()==7,"policy16");
 sim::readResults.push_back(false);due(CURSOR_SUPPRESSED,0);expect("query-failure-does-not-show",!shown());
 expect("query-failure-preserves-contact-authorization",authorization()==7,"policy16");
 sim::failUpload=false;due(CURSOR_SUPPRESSED,0);expect("NULL-stationary-retry-after-upload-and-query-failures-shows",shown()&&atRaw()&&cursorShape==(HCURSOR)(uintptr_t)7,"policy16");
 for(bool iconFailure:{true,false}){
  clean();oldCache();sim::failIcon=iconFailure;sim::failUpload=!iconFailure;auto priorShows=sim::shows;
  startRaw(CURSOR_SUPPRESSED,22,830,430,0,sim::now+1);
  expect(iconFailure?"prepare-failure-cannot-show-other-shape-cache":"upload-failure-cannot-show-other-shape-cache",!shown()&&sim::shows==priorShows);
  due(CURSOR_SUPPRESSED,0,830,430);expect(iconFailure?"NULL-retry-with-prepare-fault-stays-hidden":"NULL-retry-with-upload-fault-stays-hidden",!shown()&&sim::shows==priorShows);
  sim::failIcon=sim::failUpload=false;due(CURSOR_SUPPRESSED,0,830,430);
  expect(iconFailure?"prepare-retry-prefers-confirmed-shape-over-old-cache":"upload-retry-prefers-confirmed-shape-over-old-cache",shown()&&atRaw()&&cursorShape==(HCURSOR)(uintptr_t)22,"policy16");
 }
 clean();sim::failUpload=true;startRaw();up();expect("hidden-UP-before-upload-recovery-is-really-retained",!shown()&&cursorRetained&&!cursorTracking);
 sim::failUpload=false;due(CURSOR_SUPPRESSED,0);expect("hidden-UP-NULL-static-upload-retry-can-recover",shown()&&cursorRetained&&!cursorTracking&&cursorShape==(HCURSOR)(uintptr_t)7,"policy16");
 clean();sim::afterUpload=[]{sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=(HCURSOR)(uintptr_t)7;};startRaw();
 expect("first-preparation-recheck-SHOWING-never-shows",!shown()&&sim::shows==0);
 expect("known-contact-preparation-actually-rechecks",sim::uploads==1&&sim::reads>=2&&sim::current.flags==CURSOR_SHOWING,"policy16");
 expect("preparation-handoff-clears-contact-authorization",authorization()==0);
 clean();sim::failUpload=true;startRaw();notify(CURSOR_SHOWING,0,802,404,true);
 expect("SHOWING-NULL-visibility-clears-hidden-authorization",!shown()&&authorization()==0);
 sim::failUpload=false;due(CURSOR_SUPPRESSED,0);expect("same-XY-suppression-after-SHOWING-NULL-cannot-reappear",!shown());
 due(CURSOR_SUPPRESSED,0,803,404);expect("new-motion-after-SHOWING-NULL-can-recover",shown()&&atRaw());
}
static void resetBoundariesAndMotion(){
 clean();sim::failUpload=true;startRaw();operation([]{cursorStop();});expect("stop-clears-contact-authorization",authorization()==0);
 sim::failUpload=false;startRaw(CURSOR_SUPPRESSED,0,802,404,0,sim::now+1);expect("new-hidden-gesture-does-not-inherit-contact-shape",!shown());
 clean();sim::failUpload=true;startRaw();core.modes[0]=Mode::Mouse;raw(sim::now+1,0,802,404,0);contact();expect("mode-change-clears-contact-authorization",authorization()==0);
 sim::failUpload=false;core.modes[0]=Mode::Deck;raw(sim::now+1,CURSOR_SUPPRESSED,802,404,0);contact();expect("Mouse-to-Deck-NULL-is-not-bridge-authorized",!shown());
 clean();startRaw(CURSOR_SUPPRESSED,0);due(CURSOR_SUPPRESSED,0,810,410);expect("original-NULL-motion-path-can-show",shown()&&atRaw());
 auto uploads=sim::uploads;due(CURSOR_SUPPRESSED,0,825,420);expect("original-motion-path-keeps-position-only-update",shown()&&atRaw()&&sim::uploads==uploads);
 notify(CURSOR_SHOWING,7,825,420);expect("original-motion-path-handoff-is-preserved",!shown());
 for(int flavor:{1,2}){
  clean();startRaw(CURSOR_SUPPRESSED,7,802,404,flavor);expect(flavor==1?"Mouse-static-contact-keeps-fallback":"mixed-static-contact-keeps-fallback",shown()&&!cursorDeckOnlyTracking);
  raw(sim::now+1,CURSOR_SUPPRESSED,810,410,7);contact();expect(flavor==1?"Mouse-immediate-motion-is-preserved":"mixed-immediate-motion-is-preserved",shown()&&atRaw());
  raw(sim::now+1,CURSOR_SHOWING,810,410,7);contact();expect(flavor==1?"Mouse-existing-grace-is-preserved":"mixed-existing-grace-is-preserved",shown());
  raw(sim::now+40,CURSOR_SHOWING,810,410,7);contact();raw(sim::now+40,CURSOR_SHOWING,810,410,7);contact();
  expect(flavor==1?"Mouse-grace-completes":"mixed-grace-completes",!shown());
 }
}
static void budget(){
 // Prime through the preexisting motion path so both policies enter the same
 // already-visible state before measuring scheduling, independently of bridge.
 clean();startRaw(CURSOR_SUPPRESSED,0);due(CURSOR_SUPPRESSED,7,803,404);operation([]{cursorRequestAfterReport();});
 auto warm=sim::now;auto deadline=cursorDeckNextPositionTick;auto reads=sim::reads;
 raw(warm+24,CURSOR_SUPPRESSED,803,404,7);operation([]{cursorContactChanged(sim::dispatchWindow,false,true,sim::now);});
 expect("fresh-BUS-pulse-fences-UI-MOVE-query-and-deadline",sim::reads==reads&&cursorDeckNextPositionTick==deadline);
 sim::current.ptScreenPos.x=804;operation([]{cursorRequestAfterReport();});
 expect("after-report-due-slot-queries-once-and-follows-XY",sim::reads==reads+1&&shown()&&atRaw());
 reads=sim::reads;operation([]{cursorRequestAfterReport();});expect("same-slot-after-report-does-not-query-twice",sim::reads==reads);
 auto clocks=sim::tick64Reads;for(int i=0;i<3000;++i)cursorContactChanged(sim::dispatchWindow,false,true,sim::now);
 expect("3000-fresh-pulse-MOVEs-add-no-query-or-clock",sim::reads==reads&&sim::tick64Reads==clocks);
 raw(sim::now+48,CURSOR_SUPPRESSED,805,405,7);operation([]{cursorContactChanged(sim::dispatchWindow,false,true,sim::now);});
 expect("48ms-BUS-silence-keeps-UI-fallback",sim::reads==reads+1&&shown()&&atRaw());reads=sim::reads;operation([]{cursorRequestAfterReport();});
 expect("BUS-recovery-shares-fallback-slot",sim::reads==reads);
 clean();startRaw(CURSOR_SUPPRESSED,0);reads=sim::reads;deadline=cursorDeckNextPositionTick;
 for(unsigned i=1;i<24;++i){raw(1000+i,CURSOR_SUPPRESSED,802,404,7);operation([]{edge(EVENT_OBJECT_HIDE);cursorRequestAfterReport();});}
 expect("hidden-visibility-and-BUS-burst-respects-24ms-slot",sim::reads==reads&&cursorDeckNextPositionTick==deadline&&kCursorActiveFallbackMs==24);
 raw(deadline,CURSOR_SUPPRESSED,802,404,7);operation([]{edge(EVENT_OBJECT_HIDE);});
 expect("due-Visibility-known-handle-still-cannot-authorize",!shown()&&sim::reads==reads+1);
}
static Json replay(const Json& trace){
 if(!trace.is_array())throw std::runtime_error("trace is not an array");clean();unsigned valid=0,invalid=0,endpoints=0,knownContacts=0,knownShown=0;Json gaps=Json::array();
 for(size_t index=0;index<trace.size();++index){const auto& row=trace[index];auto seq=row.at("sequence").get<unsigned>();auto tick=row.at("tick").get<ULONGLONG>();
  if(!row.value("valid",false)){++invalid;sim::now=tick;core.pads={};operation([]{cursorStop();});continue;}++valid;
  const auto source=row.value("source",std::string("periodic"));bool active=row.value("active",false),retained=row.value("retained",false);
  for(int endpoint=0;endpoint<2;++endpoint){auto when=row.at(endpoint?"lastTick":"tick").get<ULONGLONG>();auto x=row.at(endpoint?"lastX":"x").get<LONG>(),y=row.at(endpoint?"lastY":"y").get<LONG>();
   if(endpoint&&when==tick&&x==row.at("x").get<LONG>()&&y==row.at("y").get<LONG>())continue;
   raw(when,row.at("flags").get<DWORD>(),x,y,row.at("shape").get<uintptr_t>());auto before=sim::reads;
   if(!endpoint&&source=="contact"){
    if(!cursorTracking){core={};core.pads[0].down=true;core.modes[0]=Mode::Deck;}contact();
   }else if(!endpoint&&source=="release")up();
   else if(!endpoint&&source=="visibility")operation([&]{edge((sim::current.flags&CURSOR_SHOWING)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);});
   else operation([]{cursorSync();});
   ++endpoints;replayRows.push_back({{"sequence",seq},{"endpoint",endpoint?"last":"first"},{"recordedSource",source},{"modeledSource",endpoint?"periodic":source},{"tick",when},{"x",x},{"y",y},{"flags",sim::current.flags},{"shape",(uintptr_t)sim::current.hCursor},{"recordedHelperVisible",row.value("fallbackVisible",false)},{"modeledHelperVisible",shown()},{"queries",sim::reads-before},{"contactShape",authorization()}});
   if(!endpoint&&active&&source=="contact"&&sim::current.flags==CURSOR_SUPPRESSED&&sim::current.hCursor){
    ++knownContacts;if(shown())++knownShown;ULONGLONG nextShowing=0;unsigned nextSeq=0;
    for(size_t j=index+1;j<trace.size();++j){if(!trace[j].value("valid",false))break;if((trace[j].at("flags").get<DWORD>()&CURSOR_SHOWING)!=0){nextShowing=trace[j].at("tick").get<ULONGLONG>();nextSeq=trace[j].at("sequence").get<unsigned>();break;}}
    gaps.push_back({{"contactSequence",seq},{"contactTick",when},{"nextRecordedShowingSequence",nextSeq},{"nextRecordedShowingTick",nextShowing},{"observedDeltaMs",nextShowing?nextShowing-when:0},{"recordedHelperVisibleAtContact",row.value("fallbackVisible",false)},{"modeledHelperVisibleAtContact",shown()},{"modeledAtCurrentXY",atRaw()}});
    expect("field-known-contact-seq"+std::to_string(seq)+"-bridges-current-XY",shown()&&atRaw(),"policy16");
   }
   if(sim::current.flags==CURSOR_SHOWING&&sim::current.hCursor&&cursorDeckOnlyTracking)expect("field-SHOWING-seq"+std::to_string(seq)+(endpoint?"-last":"-first")+"-does-not-overlay",!shown());
  }
  (void)retained;
 }
 expect("field-has-exactly-three-known-Contact-records",knownContacts==3);
 expect("field-three-known-Contacts-bridge-without-motion",knownShown==3,"policy16");
 return {{"limitations",limits},{"compressedRecords",trace.size()},{"validRecords",valid},{"invalidLifecycleRecords",invalid},{"knownEndpointsReplayed",endpoints},{"knownContacts",knownContacts},{"knownContactsWithModeledHelper",knownShown},{"contactWindows",gaps},{"calls",counts()},{"rows",replayRows}};
}
}
int main(int argc,char** argv){
 Json result={{"fixture","contact-shape-offline"},{"contactShapeFeatureCompiled",(bool)YMCC_TEST_CONTACT_SHAPE},{"limitations",shape_test::limits},{"realUser32GDIOrInputCalls",0}};
 try{if(argc!=3)throw std::runtime_error("usage: fixture trace.json result.json");std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary);if(!input)throw std::runtime_error("trace unavailable");auto trace=Json::parse(input);
  shape_test::knownContact();shape_test::noSpuriousAuthorization();shape_test::failuresAndCurrentShape();shape_test::resetBoundariesAndMotion();shape_test::budget();
  result["fieldReplay"]=shape_test::replay(trace);shape_test::expect("cursor-display-never-mutates-contact-owner-input",shape_test::inputUnchanged);result["cases"]=shape_test::assertions;result["failureCount"]=shape_test::failed;result["ok"]=shape_test::failed==0;cursorShutdown();
 }catch(const std::exception& e){result["ok"]=false;result["error"]=e.what();result["cases"]=shape_test::assertions;std::cerr<<e.what()<<'\n';}
 if(argc>=3){std::ofstream out(std::filesystem::path(argv[2]),std::ios::binary);out<<result.dump(2)<<'\n';}return result.value("ok",false)?0:1;
}