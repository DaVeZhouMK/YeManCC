// Real UI/BUS threads, PostMessage and layered cursor windows on a never-shown desktop.
// Cursor observations, time, report completion and Steam output are fixture adapters.
// No input injection, real Steam/Host operations, foreground changes or desktop switch.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <array>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <json.hpp>
#include <screen_touchpad_display_policy.h>
static std::atomic<ULONGLONG> fixtureTick{1000};
static std::atomic<LONG> fixtureX{200},fixtureY{150};
static HCURSOR fixtureShape=nullptr;
static std::atomic<unsigned long long> fixtureQueries{0};
static BOOL fixtureInfo(PCURSORINFO info){++fixtureQueries;*info={sizeof(CURSORINFO),CURSOR_SUPPRESSED,fixtureShape,{fixtureX.load(),fixtureY.load()}};return TRUE;}
static ULONGLONG fixtureNow(){return fixtureTick.load();}
static std::atomic<unsigned long long> fixtureHeaderClockCalls{0};
static ULONGLONG fixtureHeaderNow(){++fixtureHeaderClockCalls;return fixtureNow();}
static DWORD fixtureNow32(){return (DWORD)fixtureNow();}
static HWINEVENTHOOK fixtureNoObserver(DWORD,DWORD,HMODULE,WINEVENTPROC,DWORD,DWORD,DWORD){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
namespace ymcc::screenpads {
using Json=nlohmann::json;
enum class Mode{Mouse,Deck};
struct FixturePad{bool down=false;};
struct FixtureCore{std::array<FixturePad,2> pads{};std::array<Mode,2> modes{Mode::Deck,Mode::Deck};Mode mode(int i)const{return modes[i];}};
inline FixtureCore core;inline std::mutex mutex;inline HINSTANCE instance=nullptr;
#define GetCursorInfo fixtureInfo
#define GetTickCount64 fixtureHeaderNow
#define GetTickCount fixtureNow32
#define SetWinEventHook fixtureNoObserver
#include <screen_touchpad_cursor.h>
#undef SetWinEventHook
#undef GetTickCount
#undef GetTickCount64
#undef GetCursorInfo
}
using namespace ymcc::screenpads;
static constexpr UINT commandMessage=WM_APP+710,fenceMessage=WM_APP+711;
enum Command:WPARAM{DeckDown=1,MouseDown,MixedDown,RetainUp,Cancel,Pause,PauseThenCancel,SwitchMouse,SwitchDeck,DeckMove,Shutdown};
static HANDLE readyEvent=nullptr,fenceEvent=nullptr,pausedEvent=nullptr,resumeEvent=nullptr;
static HWND dispatchWindow=nullptr,lowerWindow=nullptr;static HDESK privateDesktop=nullptr;
static std::atomic<bool> uiFailed{false};static DWORD busThread=0,uiThread=0;
static nlohmann::json cases=nlohmann::json::array(),observations=nlohmann::json::array();static bool passed=true;
struct View{unsigned long long queries=0,uploads=0,placements=0,headerClockCalls=0;ULONGLONG deadline=0,reportPulse=0;HWND active=nullptr,focus=nullptr;POINT cached{},hotspot{};RECT actual{};HWND cursor=nullptr;bool visible=false,tracking=false,retained=false,pending=false,deck=false,rectOk=false,inert=false,lowerHit=false;};
static std::mutex viewMutex;static View lastView{};
static std::wstring desktopName(HDESK h){wchar_t text[256]{};DWORD size=0;return h&&GetUserObjectInformationW(h,UOI_NAME,text,sizeof(text),&size)?text:L"";}
static std::wstring inputName(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);const auto s=desktopName(h);if(h)CloseDesktop(h);return s;}
static void check(const std::string& name,bool ok){passed=passed&&ok;cases.push_back({{"name",name},{"ok",ok}});}
static void capture(){
 View v;v.headerClockCalls=fixtureHeaderClockCalls.load();v.queries=fixtureQueries.load();v.uploads=cursorUploads;v.placements=cursorWindowPlacements+cursorDeckPositionOnlyMoves;v.deadline=cursorDeckNextPositionTick;v.cached=cursorPosition;v.hotspot=cursorHotspot;v.cursor=cursorWindow;
 v.visible=cursorVisible&&cursorWindow&&IsWindowVisible(cursorWindow);v.tracking=cursorTracking.load();v.retained=cursorRetained.load();v.pending=cursorSyncPending.load();v.deck=cursorDeckOnlyTracking;v.active=GetActiveWindow();v.focus=GetFocus();
#ifdef YMCC_CURSOR_DECK_MOVE_REPORT_PULSE_FIXTURE_ENABLED
 v.reportPulse=cursorDeckReportPulseTick.load(std::memory_order_acquire);
#endif
 if(cursorWindow){v.rectOk=GetWindowRect(cursorWindow,&v.actual)!=FALSE;const auto ex=(DWORD)GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE);v.inert=!IsWindowEnabled(cursorWindow)&&(ex&ymcc::screeninput::displayExStyle)==ymcc::screeninput::displayExStyle;
    POINT p{v.cached.x+4,v.cached.y+4};v.lowerHit=WindowFromPoint(p)==lowerWindow;}
 {std::lock_guard<std::mutex> lock(viewMutex);lastView=v;}SetEvent(fenceEvent);
}
static LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==kCursorSyncMessage){cursorSyncPending.store(false,std::memory_order_release);cursorSync();return 0;}
 if(m==cursorvisibility::kMessage){cursorVisibilitySync(h,w);return 0;}
 if(m==fenceMessage){capture();return 0;}
 if(m==commandMessage){
  switch((Command)w){
   case DeckDown:case MouseDown:case MixedDown:
    cursorShutdown();{std::lock_guard<std::mutex> lock(mutex);core={};core.pads[0].down=true;core.modes[0]=w==MouseDown?Mode::Mouse:Mode::Deck;if(w==MixedDown){core.pads[1].down=true;core.modes[1]=Mode::Mouse;}}
    cursorContactChanged(h);
#ifdef YMCC_CURSOR_MOTION_GATE_FIXTURE_ENABLED
    // Prime real-motion eligibility with a substituted OS coordinate sample;
    // subsequent native queue checks exercise an existing layered fallback.
    if(w==DeckDown){fixtureX+=1;cursorSync(CursorSampleSource::Contact);}
#endif
    break;
   case DeckMove:
#ifdef YMCC_CURSOR_DECK_MOVE_TICK_FIXTURE_ENABLED
    cursorContactChanged(h,false,true,fixtureNow());
#else
    cursorContactChanged(h,false,true);
#endif
    break;
   case RetainUp:{std::lock_guard<std::mutex> lock(mutex);core.pads[0].down=core.pads[1].down=false;}cursorContactChanged(h,true);break;
   case Cancel:{std::lock_guard<std::mutex> lock(mutex);core.pads[0].down=core.pads[1].down=false;}cursorContactChanged(h,false);break;
   case Pause:case PauseThenCancel:
    SetEvent(pausedEvent);if(WaitForSingleObject(resumeEvent,3000)!=WAIT_OBJECT_0)uiFailed=true;
    if(w==PauseThenCancel){{std::lock_guard<std::mutex> lock(mutex);core.pads[0].down=core.pads[1].down=false;}cursorContactChanged(h,false);}break;
   case SwitchMouse:case SwitchDeck:{std::lock_guard<std::mutex> lock(mutex);core.modes[0]=w==SwitchMouse?Mode::Mouse:Mode::Deck;core.pads[0].down=true;core.pads[1].down=false;}cursorContactChanged(h);break;
   case Shutdown:cursorShutdown();if(lowerWindow)DestroyWindow(lowerWindow);lowerWindow=nullptr;DestroyWindow(h);PostQuitMessage(0);break;
  }return 0;
 }
 return DefWindowProcW(h,m,w,l);
}
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static View fence(){ResetEvent(fenceEvent);require(PostMessageW(dispatchWindow,fenceMessage,0,0)!=FALSE,"fence post failed");require(WaitForSingleObject(fenceEvent,3000)==WAIT_OBJECT_0,"UI fence timeout");std::lock_guard<std::mutex> lock(viewMutex);return lastView;}
static View command(Command c){require(PostMessageW(dispatchWindow,commandMessage,c,0)!=FALSE,"command post failed");auto v=fence();
 // UP can post its own sync behind the first fence. Settle only setup commands,
 // so later measurements start after their transitive queued work has completed.
 for(int i=0;v.pending&&i<8;++i)v=fence();require(!v.pending,"setup queue did not settle");return v;}
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
static void observe(const char* stage,const View& v){observations.push_back({{"stage",stage},{"queries",v.queries},{"uploads",v.uploads},{"placements",v.placements},{"deadline",v.deadline},{"headerClockCalls",v.headerClockCalls},{"reportPulseTick",v.reportPulse},{"fixtureTick",fixtureNow()},{"activeWindow",(ULONG_PTR)v.active},{"focusWindow",(ULONG_PTR)v.focus},{"x",v.cached.x},{"y",v.cached.y},{"visible",v.visible},{"tracking",v.tracking},{"retained",v.retained},{"pending",v.pending}});}
static bool atCurrent(const View& v){return v.rectOk&&v.actual.left==v.cached.x&&v.actual.top==v.cached.y&&v.cached.x==fixtureX.load()-v.hotspot.x&&v.cached.y==fixtureY.load()-v.hotspot.y;}
static void pauseUI(Command c){ResetEvent(pausedEvent);ResetEvent(resumeEvent);require(PostMessageW(dispatchWindow,commandMessage,c,0)!=FALSE,"pause post failed");require(WaitForSingleObject(pausedEvent,3000)==WAIT_OBJECT_0,"pause entry timeout");}
static void runDeckMoveOrder(){
 // Establish one actual BUS after-report call before a due UI MOVE. The clock
 // and OS XY are adapters; fences, posting, UI dispatch and pixels are native.
 auto ready=command(DeckDown);afterReport();auto armed=fence();observe("R13-BUS-activity-established",armed);
 check("report-pulse-establishment-adds-no-query",armed.queries==ready.queries&&armed.deadline==ready.deadline);
 fixtureTick+=24;auto beforeMove=fence();auto moved=command(DeckMove);observe("R13-due-MOVE-before-report-drained",moved);
 check("live-Deck-MOVE-before-report-spends-no-query-or-deadline",moved.queries==beforeMove.queries&&moved.deadline==beforeMove.deadline&&!moved.pending);
 check("fresh-Deck-MOVE-uses-no-header-clock",moved.headerClockCalls==beforeMove.headerClockCalls);
 fixtureX+=37;fixtureY+=11;afterReport();auto delivered=fence();observe("R13-report-after-due-MOVE",delivered);
 check("post-MOVE-report-uses-one-latest-query",delivered.queries==moved.queries+1&&atCurrent(delivered)&&delivered.visible);
 check("Deck-MOVE-after-report-keeps-contact-mode-and-image",delivered.tracking&&!delivered.retained&&delivered.deck&&delivered.uploads==ready.uploads&&delivered.inert&&delivered.lowerHit);
 const auto lastReportTick=fixtureNow();afterReport();auto sameSlot=fence();
 check("second-after-report-same-slot-adds-no-query",sameSlot.queries==delivered.queries&&sameSlot.deadline==delivered.deadline);

 // No BUS calls for exactly 48ms. MOVE must keep the original due-query fallback;
 // repeated UI work must not falsely advertise BUS liveness or slide its slot.
 fixtureTick.store(lastReportTick+48);fixtureX+=13;fixtureY+=5;auto silent=command(DeckMove);observe("R13-silent-BUS-48ms-fallback",silent);
 check("silent-BUS-48ms-Deck-MOVE-falls-back-once",silent.queries==sameSlot.queries+1&&atCurrent(silent)&&silent.deadline==fixtureNow()+24);
 check("UI-fallback-does-not-claim-a-BUS-report-pulse",silent.reportPulse==sameSlot.reportPulse);
 check("silent-fallback-preserves-image-hit-and-focus",silent.uploads==sameSlot.uploads&&silent.inert&&silent.lowerHit&&silent.active==sameSlot.active&&silent.focus==sameSlot.focus);
 const auto silentTick=fixtureNow();fixtureTick.store(silentTick+23);auto tooSoon=command(DeckMove);observe("R13-silent-fallback-plus23ms",tooSoon);
 check("silent-MOVE-before-24ms-slot-adds-no-query",tooSoon.queries==silent.queries&&tooSoon.deadline==silent.deadline);
 fixtureTick.store(silentTick+24);fixtureX+=17;fixtureY+=3;auto nextSlot=command(DeckMove);observe("R13-silent-fallback-plus24ms",nextSlot);
 check("silent-MOVE-at-next-24ms-slot-follows-once",nextSlot.queries==silent.queries+1&&atCurrent(nextSlot)&&nextSlot.deadline==fixtureNow()+24);
 afterReport();auto recovered=fence();observe("R13-BUS-recovery-same-slot",recovered);
 check("recovery-after-report-same-slot-does-not-query-twice",recovered.queries==nextSlot.queries&&recovered.deadline==nextSlot.deadline);
 fixtureTick+=24;auto recoveredMove=command(DeckMove);observe("R13-live-MOVE-after-BUS-recovery",recoveredMove);
 check("recovered-BUS-live-MOVE-still-waits-for-report",recoveredMove.queries==recovered.queries&&recoveredMove.deadline==recovered.deadline);
 fixtureX+=19;afterReport();auto recoveredReport=fence();observe("R13-report-after-recovered-live-MOVE",recoveredReport);
 check("recovered-report-queries-latest-once-after-MOVE",recoveredReport.queries==recoveredMove.queries+1&&atCurrent(recoveredReport));

 // Repeat lifecycle edges after an actual recent report pulse, in addition to
 // retaining all twenty original queue assertions below.
 auto fresh=command(DeckDown);afterReport();fence();
 check("DOWN-remains-immediate-after-report-pulse-session",fresh.visible&&fresh.tracking&&!fresh.retained&&fresh.queries>recoveredReport.queries);
 auto lifted=command(RetainUp);check("UP-remains-immediate-and-retained-after-live-BUS",lifted.visible&&!lifted.tracking&&lifted.retained&&lifted.queries>fresh.queries);
 auto mouse=command(SwitchMouse);check("mode-switch-keeps-immediate-Mouse-query-after-live-BUS",!mouse.deck&&mouse.tracking&&!mouse.retained&&mouse.queries>lifted.queries);
 auto cancelled=command(Cancel);afterReport();auto cancelledReport=fence();
 check("cancel-after-live-BUS-does-not-resurrect-or-query",!cancelledReport.visible&&!cancelledReport.tracking&&!cancelledReport.retained&&!cancelledReport.pending&&cancelledReport.queries==cancelled.queries);
}
static void run(){
 auto start=command(DeckDown);check("actual-layered-cursor-is-shown-inert-and-mouse-transparent",start.visible&&start.inert&&start.lowerHit&&start.rectOk);
 fixtureTick+=24;beforeReport();auto pre=fence();observe("before-report",pre);
 check("real-queue-Deck-before-submit-spends-no-query",pre.queries==start.queries&&pre.deadline==start.deadline);
 fixtureX+=80;fixtureY+=40;afterReport();auto post=fence();observe("after-report",post);
 check("real-queue-Deck-after-submit-uses-one-current-query",post.queries==pre.queries+1&&atCurrent(post)&&post.visible);
 check("native-motion-retains-image-and-inert-policy",post.uploads==start.uploads&&post.inert&&post.lowerHit);
 beforeReport();afterReport();auto repeat=fence();check("same-slot-native-requests-do-not-add-query",repeat.queries==post.queries);

 auto mouse=command(MouseDown);fixtureTick+=24;beforeReport();auto mousePre=fence();afterReport();auto mousePost=fence();
 check("Mouse-original-pre-report-query-is-preserved",mousePre.queries==mouse.queries+1);
 check("Mouse-no-extra-after-report-query",mousePost.queries==mousePre.queries);
 auto mixed=command(MixedDown);fixtureTick+=24;beforeReport();auto mixedPre=fence();afterReport();auto mixedPost=fence();
 check("mixed-mode-stays-on-original-Mouse-schedule",mixedPre.queries==mixed.queries+1&&mixedPost.queries==mixedPre.queries&&!mixedPost.deck);

 command(DeckDown);auto retained=command(RetainUp);fixtureTick+=24;beforeReport();auto retainedPre=fence();
 check("retained-Deck-before-report-spends-no-query",retainedPre.queries==retained.queries&&retainedPre.retained);
 fixtureX+=33;afterReport();auto retainedPost=fence();check("retained-Deck-after-report-still-polls-and-moves",retainedPost.queries==retainedPre.queries+1&&atCurrent(retainedPost)&&retainedPost.retained);

 auto backlog=command(DeckDown);pauseUI(Pause);fixtureTick+=24;
 for(int i=0;i<200;++i){fixtureTick+=8;beforeReport();fixtureX.fetch_add(1);afterReport();}
 SetEvent(resumeEvent);auto drained=fence();observe("after-200-paused-BUS-frames",drained);
 check("200-queued-BUS-frames-coalesce-to-one-native-query",drained.queries==backlog.queries+1&&!drained.pending);
 check("delayed-native-message-uses-latest-observation",atCurrent(drained)&&drained.visible&&drained.uploads==backlog.uploads);

 command(DeckDown);fixtureTick+=24;beforeReport();fence();command(SwitchMouse);auto toMouse=fence();afterReport();auto switchedMouse=fence();
 check("Deck-to-Mouse-switch-does-not-add-after-query",switchedMouse.queries==toMouse.queries&&!switchedMouse.deck);
 command(MouseDown);fixtureTick+=24;beforeReport();fence();command(SwitchDeck);auto toDeck=fence();afterReport();auto switchedDeck=fence();
 check("Mouse-to-Deck-switch-uses-existing-immediate-query",switchedDeck.queries==toDeck.queries&&switchedDeck.deck);

 auto cancelling=command(DeckDown);pauseUI(PauseThenCancel);fixtureTick+=24;beforeReport();afterReport();SetEvent(resumeEvent);auto cancelled=fence();observe("queued-query-after-cancel",cancelled);
 check("queued-sync-after-cancel-cannot-show-or-retain",!cancelled.visible&&!cancelled.tracking&&!cancelled.retained&&!cancelled.pending);
 check("queued-sync-after-cancel-skips-useless-cursor-query",cancelled.queries==cancelling.queries);
 auto inactive=cancelled;fixtureTick+=24;beforeReport();afterReport();auto idle=fence();check("inactive-BUS-requests-remain-atomic-only",idle.queries==inactive.queries);
 check("BUS-and-UI-executed-on-different-real-threads",busThread&&uiThread&&busThread!=uiThread);
 runDeckMoveOrder();
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const std::filesystem::path output(argv[1]);const auto originalDesktop=GetThreadDesktop(GetCurrentThreadId());const auto originalInput=inputName();busThread=GetCurrentThreadId();
 readyEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);fenceEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);pausedEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);resumeEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 const auto name=L"YMCC_CursorQueue_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());privateDesktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
 std::thread ui;std::string failure;
 try{
  require(readyEvent&&fenceEvent&&pausedEvent&&resumeEvent&&privateDesktop,"fixture resource creation failed");
  ui=std::thread([]{
   uiThread=GetCurrentThreadId();if(!SetThreadDesktop(privateDesktop)){uiFailed=true;SetEvent(readyEvent);return;}
   instance=GetModuleHandleW(nullptr);fixtureShape=LoadCursorW(nullptr,IDC_ARROW);
   WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=fixtureProc;cls.lpszClassName=L"YMCC_CursorQueueFixture";
   if(!RegisterClassW(&cls)){uiFailed=true;SetEvent(readyEvent);return;}
   dispatchWindow=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
   lowerWindow=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"private fixture lower",WS_POPUP,0,0,2000,1200,nullptr,nullptr,instance,nullptr);
   if(!dispatchWindow||!lowerWindow||!fixtureShape){uiFailed=true;SetEvent(readyEvent);if(lowerWindow)DestroyWindow(lowerWindow);if(dispatchWindow)DestroyWindow(dispatchWindow);return;}
   ShowWindow(lowerWindow,SW_SHOWNOACTIVATE);SetEvent(readyEvent);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
  });
  require(WaitForSingleObject(readyEvent,5000)==WAIT_OBJECT_0&&!uiFailed,"native UI startup failed");run();
 }catch(const std::exception& e){passed=false;failure=e.what();}
 SetEvent(resumeEvent);if(dispatchWindow)PostMessageW(dispatchWindow,commandMessage,Shutdown,0);if(ui.joinable())ui.join();
 const bool mainUnchanged=GetThreadDesktop(GetCurrentThreadId())==originalDesktop;const bool inputUnchanged=!originalInput.empty()&&inputName()==originalInput;
 const bool desktopClosed=privateDesktop&&CloseDesktop(privateDesktop);privateDesktop=nullptr;
 check("native-UI-thread-and-private-desktop-cleaned",desktopClosed&&!uiFailed);
 check("input-and-main-desktop-unchanged",inputUnchanged&&mainUnchanged);
 for(HANDLE h:{readyEvent,fenceEvent,pausedEvent,resumeEvent})if(h)CloseHandle(h);
 nlohmann::json report={{"ok",passed},{"cases",cases},{"observations",observations},{"failure",failure},{"nativeUIThread",uiThread},{"busThread",busThread},{"hiddenDesktop",true},{"privateDesktopClosed",desktopClosed},{"uiWorkerTerminated",true},{"inputDesktopUnchanged",inputUnchanged},{"mainThreadDesktopUnchanged",mainUnchanged},
 {"systemInputInjected",false},{"systemCursorManipulated",false},{"visibleDesktopSwitched",false},{"productDeployed",false},{"fieldFlickerVerified",false},{"steamConsumptionVerified",false},{"cursorObservationsSubstituted",true},{"clockSubstituted",true},{"WinEventRegistrationSubstituted",true},
#ifdef YMCC_CURSOR_DECK_MOVE_TICK_FIXTURE_ENABLED
 {"dispatchTickArgumentAvailable",true},
#else
 {"dispatchTickArgumentAvailable",false},
#endif
#ifdef YMCC_CURSOR_DECK_MOVE_REPORT_PULSE_FIXTURE_ENABLED
 {"reportPulseAvailable",true}
#else
 {"reportPulseAvailable",false}
#endif
 };
 std::ofstream file(output);file<<report.dump(2)<<'\n';return file.good()?(passed?0:3):4;
}
