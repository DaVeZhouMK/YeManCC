#define NOMINMAX
#include "mouse_mode_policy.h"
#include "mouse_mode_foreground_rules.h"
#include <iostream>
#include <future>
#include <vector>
using namespace ymcc::mousePolicy;
static unsigned cases=0;
static void need(bool v,const char* reason){if(!v)throw std::runtime_error(reason);++cases;}
static bool until(const std::function<bool()>& f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;}
int main(){try{
 const std::wstring win=L"c:\\windows",explorer=win+L"\\explorer.exe";
 need(desktopSurface(explorer,win,L"DV2ControlHost"),"logged-explorer-popup-remains-desktop");
 for(const auto cls:{L"TopLevelWindowForOverflowXamlIsland",L"NotifyIconOverflowWindow",L"Shell_TrayWnd",L"Windows.UI.Composition.DesktopWindowContentBridge",L"#32768",L"CabinetWClass",L"WorkerW",L"Progman"})need(desktopSurface(explorer,win,cls),"verified-explorer-surface-does-not-depend-on-class-list");
 need(!desktopSurface(L"c:\\games\\explorer.exe",win,L"Shell_TrayWnd"),"same-name-non-system-explorer-must-not-qualify");
 need(!desktopSurface(L"c:\\windows\\explorer.exe.malware",win,L"Progman"),"shell-path-must-match-completely");
 need(!desktopSurface(L"c:\\games\\tmnt.exe",win,L"SDL_app"),"game-window-class-does-not-qualify-as-desktop");
 need(!desktopSurface(explorer,L"",L"Progman"),"missing-windows-root-never-qualifies");
 Context desktop{1,10,100,true,false,false},windowed{2,20,200,false,false,false},full{3,20,200,false,false,false},ymcc{4,30,300,false,true,false},unknown{};
 need(decide(Mode::Auto,desktop).on,"confirmed-desktop-enable");need(!decide(Mode::Auto,windowed).on,"windowed-game-off");need(!decide(Mode::Auto,full).on,"fullscreen-game-off");need(!decide(Mode::Auto,ymcc).on,"ymcc-pauses-auto");need(!decide(Mode::Auto,unknown).on,"null-foreground-never-desktop");need(decide(Mode::On,full).on,"manual-on-ignores-game");need(!decide(Mode::Off,desktop).on,"manual-off-ignores-desktop");auto sd=desktop;sd.steam=true;need(!decide(Mode::On,sd).on,"sd-admission-before-manual-on");
 std::mutex io;Context observed=desktop;bool backend=false,failStop=false,moveOnEnable=false,manualMove=false;unsigned starts=0,stops=0;std::vector<Json> events;
 Runtime runtime;const auto observe=[&]{std::lock_guard lock(io);return observed;};
 auto release=[&]{std::lock_guard lock(io);++stops;if(!failStop)backend=false;return Json{{"ok",!failStop},{"on",backend},{"stateKnown",true},{"provider","ymcc"},{"controlAllowed",true},{"verification","backend-state"}};};
 runtime.start({observe,[&](bool on,const std::function<bool()>& valid){
   if(on&&valid&&!valid())return Json{{"ok",false},{"on",false},{"stateKnown",true}};
   {std::lock_guard lock(io);if(on){++starts;backend=true;if(moveOnEnable||manualMove){observed=full;moveOnEnable=manualMove=false;}}else{++stops;if(!failStop)backend=false;}}
   std::lock_guard lock(io);return Json{{"ok",on||!failStop},{"on",backend},{"stateKnown",true},{"provider","ymcc"},{"controlAllowed",true},{"mappingVerified",false},{"verification",on?"process-liveness":"backend-state"}};
 },release,[&](const Json& state){std::lock_guard lock(io);events.push_back(state);}},Mode::Off);
 auto move=[&](Context c){{std::lock_guard lock(io);observed=c;}runtime.wake();};
 auto on=[&]{std::lock_guard lock(io);return backend;};
 need(runtime.set(Mode::Auto).value("on",false),"auto-start-on-desktop");need(!runtime.state().value("mappingVerified",true),"liveness-not-mapping-proof");
 move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="ymcc"&&!on();}),"ymcc-foreground-disables");need(runtime.current()==Mode::Auto,"ymcc-keeps-automatic-choice");
 move(desktop);need(until(on),"minimize-to-desktop-restores");move(windowed);need(until([&]{return !on();}),"windowed-game-disables");move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="ymcc";}),"summon-from-game-pauses");move(full);need(until([&]{return runtime.state().value("scene",std::string{})=="external"&&!on();}),"minimize-to-fullscreen-stays-off");
 need(runtime.set(Mode::On).value("on",false),"manual-on-in-game");move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="manual"&&on();}),"manual-on-in-ymcc");
 need(!runtime.set(Mode::Off).value("on",true),"manual-off-confirmed");move(desktop);std::this_thread::sleep_for(std::chrono::milliseconds(220));need(!on(),"off-kept-on-desktop");
 // Reproduce the late-enable race inside actual policy consumer IO.
 {std::lock_guard lock(io);moveOnEnable=true;observed=desktop;}
 runtime.set(Mode::Auto);need(until([&]{return runtime.state().value("scene",std::string{})=="external"&&!on();}),"late-desktop-enable-withdrawn-in-game");
 {std::lock_guard lock(io);observed=desktop;manualMove=true;}
 need(runtime.set(Mode::On).value("on",false),"manual-on-survives-foreground-race");
 move(desktop);runtime.set(Mode::Auto);need(until(on),"desktop-before-null-transition");
 move(unknown);std::this_thread::sleep_for(std::chrono::milliseconds(40));need(on(),"short-null-foreground-keeps-admitted-mouse-state");
 move(desktop);need(until(on),"desktop-return-within-grace-keeps-mouse");
 move(unknown);std::this_thread::sleep_for(std::chrono::milliseconds(40));move(windowed);need(until([&]{return !on();}),"real-game-interrupts-null-grace");
 move(desktop);need(until(on),"desktop-restores-after-game");
 move(unknown);std::this_thread::sleep_for(std::chrono::milliseconds(40));const auto duringGrace=runtime.set(Mode::Off);need(!duringGrace.value("on",true),"manual-off-interrupts-null-grace");
 move(desktop);runtime.set(Mode::Auto);need(until(on),"desktop-before-sustained-null");
 move(unknown);for(int i=0;i<12;i++){runtime.wake();std::this_thread::sleep_for(std::chrono::milliseconds(30));}
 need(until([&]{return !on()&&runtime.state().value("scene",std::string{})=="unknown";}),"null-event-storm-cannot-renew-grace-indefinitely");
 move(desktop);runtime.set(Mode::On);need(until(on),"manual-on-before-sd-admission");
 move(sd);need(until([&]{return runtime.state().value("provider",std::string{})=="steam"&&!on();}),"sd-transition-releases-owned-mouse");need(!runtime.state().value("stateKnown",true),"sd-does-not-claim-steam-output-known");
 move(desktop);need(until(on),"leaving-sd-restores-selected-manual-mode");
 runtime.set(Mode::Off);move(desktop);runtime.set(Mode::Auto);move(unknown);need(until([&]{return !on()&&runtime.state().value("scene",std::string{})=="unknown";}),"unknown-foreground-disables");
 // A same-HWND/PID identity change invalidates an old instance.
 auto recycled=desktop;recycled.created=999;move(desktop);move(recycled);need(until(on),"fresh-desktop-instance-qualifies-after-debounce");
 // Off failure must remain visible and must never appear as a confirmed stop.
 {std::lock_guard lock(io);failStop=true;}
 const auto failed=runtime.set(Mode::Off);need(!failed.value("ok",true)&&failed.value("on",false),"failed-close-remains-on-with-error");
 {std::lock_guard lock(io);failStop=false;}
 runtime.set(Mode::Off);need(!on(),"explicit-retry-settles-provider");
 move(desktop);runtime.set(Mode::Off);
 auto older=std::async(std::launch::async,[&]{return runtime.set(Mode::Auto);});
 std::this_thread::sleep_for(std::chrono::milliseconds(25));
 const auto newer=runtime.set(Mode::Off);const auto late=older.get();
 need(newer.value("mode",std::string{})=="off"&&!on(),"newer-explicit-off-wins-over-auto");
 need(late.value("mode",std::string{})=="off"&&late.value("superseded",false),"old-response-cannot-relabel-newer-mode");need(runtime.records().size()>8,"native-transition-history-recorded");runtime.stop();need(!events.empty(),"native-consumer-publishes-receipts");
 std::cout<<Json{{"status","PASS"},{"cases",cases},{"consumer","production mouse_mode_policy::Runtime"},{"hardware","NOT_TOUCHED"}}.dump();return 0;
}catch(const std::exception& e){std::cout<<Json{{"status","FAIL"},{"error",e.what()},{"cases",cases}}.dump();return 1;}}
