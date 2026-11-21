// Replays production rule selection and the actual LB+RB hold branch.
// The OS window/action, configuration and clock are in-memory seams only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {createRequire} from 'node:module';import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerSummonRebind-20261031/native');fs.mkdirSync(out,{recursive:true});
const sourceFile=process.argv[3]||path.join(root,'native/main.cpp'),source=fs.readFileSync(sourceFile,'utf8');
function extract(name){const regex=new RegExp('^static [^\\n;]*\\b'+name+'\\([^]*?\\n\\}','m'),match=regex.exec(source);assert(match,'Missing '+name);return match[0]}
function extractTray(name){const regex=new RegExp(String.raw`^    static void ${name}\([^\r\n;]*\) \{[^]*?\n    \}`,'m'),match=regex.exec(source);assert(match,'Missing '+name);return match[0]}
const trayStart=source.indexOf('    case WM_TRAYICON:');const trayEnd=source.indexOf('\n    //',source.indexOf('    case WM_COMMAND:',trayStart));assert(trayStart>0&&trayEnd>trayStart);
const trayCommands=source.match(/^#define (?:WM_TRAYICON|ID_TRAY_(?:SHOW|MIN|EXIT)) [^\r\n]+/gm);assert.equal(trayCommands?.length,4);
const structure=source.match(/struct NativeKeyboardShortcutRule \{[\s\S]*?\n\};/)[0];
const funcs=['nativeKeyboardShortcutMaskBit','nativeKeyboardShortcutDuration','nativeKeyboardShortcutRequiredMask','nativeKeyboardShortcutAllInputs','nativeYmccShortcutAction','nativeYmccShortcutDelta','nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed','nativeKeyboardShortcutsRefresh'].map(extract);
const legacyStart=source.indexOf('    // Reuse the existing hold tick');assert(legacyStart>0);const legacyEnd=source.indexOf('\n    // ── Start +',legacyStart);assert(legacyEnd>legacyStart);const legacy=source.slice(legacyStart,legacyEnd);
const serial=source.match(/^struct GamepadSerialEvent\s*\{[\s\S]*?^\};/m)[0];
const serialFence=serial.slice(serial.indexOf('    static inline std::atomic<unsigned long long> ConfigurationRevision'));
// Use the frontend's real default rows so native ownership cannot drift from the editor.
const require=createRequire(path.join(root,'package.json'));
const bundled=require('esbuild').buildSync({entryPoints:[path.join(root,'src/bridge/controllerShortcutRules.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text;
const module={exports:{}};new Function('module','exports','require',bundled)(module,module.exports,require);
const defaults=module.exports.NATIVE_DEFAULT_SHORTCUTS.map(def=>module.exports.nativeDefaultRule(def.defaultKey));
const defaultRow=defaults.find(rule=>rule.defaultKey==='enabled');
const normalize=raw=>module.exports.normalizeControllerShortcutRule(raw,0);
assert.equal(defaultRow.holdMs,250);
assert.equal(normalize({...defaultRow,holdMs:500}).holdMs,250,'the old factory timing is displayed as the new default');
assert.equal(normalize({...defaultRow,holdMs:500,params:{explicitSummonHold:true}}).holdMs,500,'explicit timing survives normalization');
assert.equal(normalize({...defaultRow,holdMs:500,origin:'custom'}).holdMs,500,'custom chord retains its original timing');
assert.equal(normalize({...defaultRow,holdMs:500,enabled:false}).enabled,false,'migration never changes a disabled setting');
const editorExports=process.argv[4]?JSON.parse(fs.readFileSync(process.argv[4],'utf8')):[];
const evaluatorStart=source.indexOf('static void nativeKeyboardShortcutsEvaluate(');
const coreStart=source.indexOf('        const uint64_t currentMask = ',evaluatorStart);
const coreEnd=source.indexOf('        if (emit) {',coreStart);
assert(coreStart>evaluatorStart&&coreEnd>coreStart);
const triggerCore=source.slice(coreStart,coreEnd);
const cpp=String.raw`
#define YMCC_SHORTCUT_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <Shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "input_shortcut_toggle.h"
#include "keyboard_shortcut_sequence.h"
#include "shortcut_window_toggle.h"
#include "shortcut_action_protocol.h"
#if __has_include("desktop_function_shortcut.h")
#include "desktop_function_shortcut.h"
#endif
#include "shortcut_frontend_button.h"
#include "boot_mode_shortcuts.h"
using json=nlohmann::json;
namespace ymcc::radial{static bool userAvailable(){return false;}static bool userActionAvailable(const std::string& s){return s!="radial.summon";}}
static constexpr int kOemProducerCount=8;
${structure}
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static uint32_t g_nativeShortcutLegacyOverrideMask=0;
static bool g_nativeShortcutRulesConfigured=false;
static bool g_nativeShortcutInventoryDirty=true;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=400;
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{0};
struct GamepadSerialEvent {
${serialFence}
static json fixtureInput;
static void appendNativeLifecycleLog(const char*,const json&){}
static json ymSettingsSection(const char*){return fixtureInput;}
// In-memory settings seam; the production getter is checked by its real Win32 leaf.
static json ymSettingsFields(const char* section, std::initializer_list<const char*> fields, bool* primary=nullptr) {
    if(primary)*primary=true;
    const auto input = ymSettingsSection(section);
    json selected = json::object();
    if (input.is_object()) for (const auto* key : fields) {
        const auto value = input.find(key);
        if (value != input.end()) selected[key] = *value;
    }
    return selected;
}
static bool nativeInputShortcutDesktopKeys(const json&,std::vector<WORD>&){return false;}
static bool nativeKeyboardShortcutVirtualKey(const std::string&,const std::string&,WORD&,bool&){return false;}
${funcs.join('\n')}
static ymcc::DefaultSummonHold g_defaultSummonHold;
static bool g_summonEnabled=true;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
struct FocusTargetSnapshot{bool valid=false;};static FocusTargetSnapshot g_pendingSummonTarget;
static int legacySummons=0,summonProbes=0,cloakReads=0,shortcutReturns=0,shortcutOpens=0;
static bool g_webviewReady=true,g_ctrl=true,g_view=true,g_deferFirstShow=false,g_webviewRecoveryInProgress=false;
static bool fixtureVisible=false,fixtureIconic=false,fixtureZoomed=false,fixtureCloakReadOk=true;
static DWORD fixtureCloak=0;
static std::atomic<bool> g_fullHeight{false};
static bool overlayActive=false;
static int overlayChecks=0,overlayStops=0;
static struct {bool active()const{return overlayActive;} void requestCheck(){++overlayChecks;}} g_gameOverlay;
static struct {ULONGLONG readContinuityGeneration=1;} g_inputCaptureGamepadSnapshot;
static ULONGLONG g_summonGeneration=0,fixturePower=1;
static std::atomic<ULONGLONG> g_summonRegistrationGeneration{0};
static ULONGLONG currentPowerGeneration(){return fixturePower;}
static BOOL fixtureIsWindow(HWND){++summonProbes;return TRUE;}
static BOOL fixtureIsWindowVisible(HWND){return fixtureVisible;}
static BOOL fixtureIsIconic(HWND){return fixtureIconic;}
static BOOL fixtureIsZoomed(HWND){return fixtureZoomed;}
static HRESULT fixtureDwmGetWindowAttribute(HWND,DWORD,PVOID out,DWORD){++cloakReads;*static_cast<DWORD*>(out)=fixtureCloak;return fixtureCloakReadOk?S_OK:E_FAIL;}
#define IsWindow fixtureIsWindow
#define IsWindowVisible fixtureIsWindowVisible
#define IsIconic fixtureIsIconic
#define IsZoomed fixtureIsZoomed
#define DwmGetWindowAttribute fixtureDwmGetWindowAttribute
static void focusShortcutInputEvidence(const char* phase,ULONGLONG,bool){if(std::string_view(phase)=="before-decision")++legacySummons;}
static void gameOverlayStop(const char*){overlayActive=false;++overlayStops;}
static void focusBeginReturnToPreviousWindow(ULONGLONG){++shortcutReturns;}
static void focusPrepareIsolationBeforeShow(){}
enum class GameOverlayAttempt {Ordinary,Begun,Refused};
static GameOverlayAttempt gameOverlayTryBegin(const FocusTargetSnapshot&,bool){return GameOverlayAttempt::Ordinary;}
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot&,ULONGLONG){return json::object();}
static void ipc_emit(const char*,const json& = json::object()){}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,ULONGLONG){}
static void focusEndIsolationOnUi(){}
static bool focusLightNoGameQualified(ULONGLONG){return false;}
static bool focusLightIsolationCurrent(){return false;}
static std::atomic<bool> g_focusIsolationGameOpen{false};
static struct {bool lightNoGame=false;} g_focusSession;
static void bringToFront(HWND,bool){++shortcutOpens;}
static bool g_mainWindowWasMaximized=false;
static bool shortcutSummonWaitForRender(const FocusTargetSnapshot&,bool){return false;}
static void shortcutSummonClearRenderWait(){}
${extract('shortcutSummonRenderBusy')}
${extract('readShortcutWindowVisibility')}
${extract('nativeYmccShortcutSummon')}
${extract('nativeDefaultSummonTry')}
${trayCommands.join('\n')}
static constexpr UINT WM_APP_EXIT=WM_APP+1;
static HWND g_menuHwnd=nullptr;
static std::atomic<bool> g_exitRequested{false},g_webviewDegraded{false};
static int trayShows=0,trayHides=0,trayMenus=0,trayExits=0,trayReloads=0,lastTrayShow=0,g_firstShowCmd=SW_SHOW;
static bool fixtureForeground=false,fixtureActivate=true;
static bool focusMainWindowIsForeground(){return fixtureForeground;}
static void showWindowAnimated(HWND,int cmd){++trayShows;lastTrayShow=cmd;if(!g_webviewReady){g_deferFirstShow=true;return;}
 fixtureVisible=true;fixtureIconic=false;if(fixtureActivate){fixtureForeground=true;fixtureCloak=0;}}
static void hideWindowAnimated(HWND){++trayHides;fixtureVisible=fixtureForeground=g_deferFirstShow=false;}
static void closeTrayMenu(){g_menuHwnd=nullptr;}
static void showTrayMenu(){g_menuHwnd=g_hwnd;++trayMenus;}
static void resetWebViewFailureAttempts(){}
enum class WebViewDeferredRecovery {RecreateController};
static void beginWebViewRecovery(const char*,const char*,size_t){++trayReloads;}
static void scheduleWebViewRecoveryAction(WebViewDeferredRecovery){}
static BOOL fixturePostMessageW(HWND,UINT msg,WPARAM,LPARAM){if(msg==WM_APP_EXIT)++trayExits;return TRUE;}
#define PostMessageW fixturePostMessageW
${extractTray('requestTrayShow')}
${extractTray('execMenuCmd')}
static LRESULT actualTrayDispatch(UINT message,WPARAM w=0,LPARAM l=0){switch(message){
${source.slice(trayStart,trayEnd)}
}return -1;}
#undef PostMessageW
static void legacyTick(WORD w,ULONGLONG now){const auto shoulderBits=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;const bool both=(w&shoulderBits)==shoulderBits;const bool shoulderDown=(w&shoulderBits)!=0;
${legacy}
}
static int configuredSummons=0;
static SHORT fixtureGetAsyncKeyState(int){return 0;}
#define GetAsyncKeyState fixtureGetAsyncKeyState
static bool nativeKeyboardSequenceOwnsKey(WORD){return false;}
static ymcc::input::desktopFunction::KeyboardKeys g_desktopChordKeys;
static auto nativeDesktopChordPhysicalKeys(){return g_desktopChordKeys;}
template<class... Args> static void nativeOemVirtualConsumeMatched(Args&&...){}
static void configuredTick(uint64_t oemMask,ULONGLONG now){
 const bool oemRule=true;const uint64_t controllerMask=0;
 for(auto& rule:g_nativeKeyboardShortcuts){
const uint32_t desktopBit=0;
 const bool chordReady=false,oemEdge=false;const int epoch=0,sampledVirtualObservation=0;
 const int* virtualWitness=nullptr;const int* capturedTokens=nullptr;
 const ymcc::input::desktopFunction::Edge* desktopEdge=nullptr;
 static std::atomic<bool>g_desktopFunctionHookActive{false};
 static std::atomic<uint32_t>g_desktopFunctionKeys{0},g_desktopFunctionPhysicalKeys{0};
${triggerCore}
  if(emit&&rule.ownAction=="window.summon")++configuredSummons;
  rule.matchedLastTick=matched;
 }
}
static int checks=0,regressions=0;
static void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
static void regression(bool ok,const char* name){++checks;if(!ok){++regressions;std::cout<<"REGRESSION "<<name<<'\n';}}
static json input(const char* code,const char* source="controller"){return{{"source",source},{"code",code}};}
static json rule(const char* id,const char* action,json inputs,const char* trigger="press",const char* slot=nullptr){json r={{"id",id},{"origin",slot?"native-default":"custom"},{"enabled",true},{"actionId",action},{"inputs",inputs},{"params",json::object()},{"trigger",trigger},{"holdMs",500},{"doubleWindowMs",500},{"intervalMs",150}};if(slot)r["defaultKey"]=slot;return r;}
static json defaultSummon(){return rule("native-enabled","window.summon",json::array({input("lb"),input("rb")}),"hold","enabled");}
static void load(json rules,bool configured=true){fixtureInput={{"revision",1},{"buttonMapping",{{"rules",json::object()}}}};if(configured){fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=rules;fixtureInput["buttonMapping"]["rules"]["shortcutRulesInitialized"]=true;}g_nativeKeyboardShortcutRevision=-1;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(10000);g_defaultSummonHold.release();legacySummons=configuredSummons=summonProbes=0;g_summonEnabled=g_webviewReady=true;fixtureVisible=fixtureZoomed=false;}
static void oldChord(){const WORD both=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;legacyTick(0,11000);legacyTick(both,11100);legacyTick(both,11700);legacyTick(both,12500);legacyTick(0,12600);}
static void refreshMode(json rows){fixtureInput["startupObservedBoot"]="boot-A";fixtureInput["bootModeShortcuts"]={{"schema",1},{"mode","steam"},{"boot","boot-A"},{"rules",rows}};fixtureInput["revision"]=2;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(20000);}
int main(){try{
 const auto frontendDefaults=json::parse(R"FRONTEND(${JSON.stringify(defaults)})FRONTEND");
 for(const auto& definition:frontendDefaults){
  const auto slot=definition["defaultKey"].get<std::string>();
  load(frontendDefaults);check(nativeShortcutLegacyDefaultAllowed(slot.c_str()),"every present pristine editor default keeps its single legacy owner");
  auto deleted=frontendDefaults;deleted.erase(std::remove_if(deleted.begin(),deleted.end(),[&](const json&r){return r.value("defaultKey","")==slot;}),deleted.end());
  load(deleted);regression(!nativeShortcutLegacyDefaultAllowed(slot.c_str()),"deleted default never survives outside its explicit rule list");
  for(const auto& other:deleted){const auto otherSlot=other["defaultKey"].get<std::string>();check(nativeShortcutLegacyDefaultAllowed(otherSlot.c_str()),"deleting one slot preserves every other explicitly configured default");}
  auto disabled=frontendDefaults;for(auto& row:disabled)if(row.value("defaultKey","")==slot)row["enabled"]=false;load(disabled);check(!nativeShortcutLegacyDefaultAllowed(slot.c_str()),"disabled editor default retires its original handler");
  auto edited=frontendDefaults;for(auto& row:edited)if(row.value("defaultKey","")==slot){row["inputs"]=json::array({input("l4","oem")});row["trigger"]="press";}
  load(edited);check(!nativeShortcutLegacyDefaultAllowed(slot.c_str())&&g_nativeKeyboardShortcuts.size()==1,"editing any default transfers ownership exactly once");
 }

 load(json::array(),false);oldChord();check(legacySummons==1,"first-run absent list keeps original LB+RB behavior");
 load(json::array({defaultSummon()}));oldChord();check(legacySummons==1&&g_nativeKeyboardShortcuts.empty(),"exact default delegates once to its legacy handler");
 const WORD both=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;
 load(json::array({defaultSummon()}));legacyTick(both,20000);legacyTick(both,20249);
 check(legacySummons==0,"249 ms does not trigger");legacyTick(both,20250);
 check(legacySummons==1,"stored default triggers at 250 ms");legacyTick(both,21000);
 check(legacySummons==1,"accepted hold never toggles again");
 for(ULONGLONG delay:{1251,1800,2600,3000}){
  load(json::array({defaultSummon()}));legacyTick(both,20000);legacyTick(both,20000+delay);
  legacyTick(both,23300);check(legacySummons==1,"delayed first hold dispatches once under UI contention");
 }
 load(json::array({defaultSummon()}));legacyTick(both,20000);legacyTick(both,23001);
 check(legacySummons==0,"expired first hold does not surprise-open");
 load(json::array({defaultSummon()}));g_webviewReady=false;legacyTick(both,20000);legacyTick(both,20250);
 check(legacySummons==0&&summonProbes==1,"the actual WebView readiness gate defers without dispatch");
 g_webviewReady=true;legacyTick(both,20299);check(summonProbes==1,"raw-input bursts cannot increase retry rate");
 legacyTick(both,20300);legacyTick(both,21250);check(legacySummons==1&&summonProbes==3,"same hold retries the gate once, then validates the window on its single dispatch");
 load(json::array({defaultSummon()}));g_deferFirstShow=true;legacyTick(both,20000);
 for(ULONGLONG t=20250;t<=21250;++t)legacyTick(both,t);
 check(summonProbes==21&&legacySummons==0,"waiting is bounded even at high raw-input rates");
 g_deferFirstShow=false;legacyTick(both,21251);
 check(legacySummons==0,"readiness after the wait window never dispatches a stale chord");
 for(int cancel=0;cancel<7;++cancel){
  load(json::array({defaultSummon()}));g_webviewReady=false;legacyTick(both,20000);legacyTick(both,20250);g_webviewReady=true;
  if(cancel==0)legacyTick(0,20260);
  if(cancel==1)++g_inputCaptureGamepadSnapshot.readContinuityGeneration;
  if(cancel==2)++fixturePower;
  if(cancel==3)++g_nativeKeyboardShortcutRevision;
  if(cancel==4)++g_summonGeneration;
  if(cancel==5)g_defaultSummonHold.cancelWhileHeld();
  legacyTick(cancel==0?0:both,cancel==6?21251:20300);
  check(legacySummons==0,"release, source, power, rules, competing session, owner or timeout cancels the old hold");
  legacyTick(0,22000);legacyTick(both,22100);legacyTick(both,22350);
  check(legacySummons==1,"a fresh hold works after cancellation");
 }
 load(json::array({defaultSummon()}));g_webviewReady=false;fixtureVisible=fixtureZoomed=true;
 legacyTick(both,20000);legacyTick(both,20250);check(legacySummons==1,"closing an open UI never waits for WebView readiness");
 shortcutOpens=shortcutReturns=cloakReads=0;fixtureCloak=DWM_CLOAKED_SHELL;
 check(!nativeDefaultSummonTry({true})&&shortcutReturns==0&&shortcutOpens==0,"cloaked maximized window must wait, not return to the previous game");
 g_webviewReady=true;fixtureForeground=false;
 check(nativeDefaultSummonTry({true})&&shortcutReturns==0&&shortcutOpens==1&&cloakReads==2,"default replay 14:52:14: one cloak read per decision; show and never return");
 check(nativeYmccShortcutSummon({true},true)&&shortcutReturns==0&&shortcutOpens==2,"configured shortcut also shows a cloaked window");
 fixtureCloak=0;
 check(nativeDefaultSummonTry({true})&&shortcutReturns==1&&shortcutOpens==2,"presentable maximized background window still toggles off");
 fixtureCloak=DWM_CLOAKED_SHELL;fixtureCloakReadOk=false;
 check(nativeDefaultSummonTry({true})&&shortcutReturns==2,"failed DWM read cannot assert cloak, even if the output buffer changed");
 fixtureIconic=true;fixtureCloakReadOk=true;const int beforeMinimized=cloakReads;
 check(nativeDefaultSummonTry({true})&&shortcutReturns==2&&shortcutOpens==3&&cloakReads==beforeMinimized,"minimized window shows without a DWM query");
 overlayActive=true;
 check(nativeDefaultSummonTry({true})&&overlayStops==1&&cloakReads==beforeMinimized,"game overlay toggles without inspecting the ordinary window");

 fixtureVisible=fixtureIconic=fixtureForeground=false;fixtureCloak=0;
 actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONUP);
 actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONDBLCLK);
 actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONUP);
 check(trayShows==1&&trayHides==0&&fixtureForeground,"tray double-click sequence must display once and never toggle back");
 fixtureIconic=true;fixtureForeground=false;actualTrayDispatch(WM_TRAYICON,0,NIN_KEYSELECT);
 check(trayShows==2&&lastTrayShow==SW_RESTORE&&!fixtureIconic,"tray restores a minimized window rather than just showing its taskbar button");
 actualTrayDispatch(WM_TRAYICON,0,WM_RBUTTONUP);
 actualTrayDispatch(WM_TRAYICON,0,MAKELPARAM(WM_CONTEXTMENU,1));
 check(trayMenus==1&&g_menuHwnd&&trayShows==2,"right-click and context menu notifications share one popup");
 fixtureCloak=DWM_CLOAKED_SHELL;fixtureForeground=false;fixtureActivate=false;
 execMenuCmd(ID_TRAY_SHOW,"tray-menu");
 check(!g_menuHwnd&&trayShows==3&&trayHides==0&&!fixtureForeground&&fixtureCloak==DWM_CLOAKED_SHELL,"right-click Show requests activation without treating cloak or foreground denial as hide");
 fixtureActivate=true;actualTrayDispatch(WM_COMMAND,ID_TRAY_SHOW);
 check(trayShows==4&&fixtureForeground&&fixtureCloak==0,"WM_COMMAND uses the same show-only action");
 overlayActive=true;actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONDBLCLK);overlayActive=false;
 check(overlayChecks==1&&overlayStops==1&&trayShows==4&&trayHides==0,"tray show keeps an active overlay owner");
 actualTrayDispatch(WM_COMMAND,ID_TRAY_MIN);
 check(trayHides==1&&!fixtureVisible,"only explicit tray Hide hides the window");
 g_webviewReady=false;actualTrayDispatch(WM_TRAYICON,0,NIN_SELECT);actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONDBLCLK);
 check(g_deferFirstShow&&trayShows==5&&trayHides==1,"startup holds one queued show through duplicate activation");
 g_deferFirstShow=false;g_webviewDegraded=true;actualTrayDispatch(WM_TRAYICON,0,WM_LBUTTONUP);
 check(trayReloads==0&&trayShows==5,"ordinary click does not implicitly reload the degraded view");
 execMenuCmd(ID_TRAY_SHOW,"tray-menu");actualTrayDispatch(WM_COMMAND,ID_TRAY_SHOW);
 check(trayReloads==1&&g_deferFirstShow&&!g_webviewDegraded&&g_firstShowCmd==SW_SHOW&&trayShows==5,"explicit degraded reload queues one show after compositor readiness");
 actualTrayDispatch(WM_COMMAND,ID_TRAY_EXIT);check(trayExits==1,"tray exit still posts the normal exit request");
 g_deferFirstShow=false;
 auto explicitTiming=defaultSummon();explicitTiming["params"]["explicitSummonHold"]=true;
 load(json::array({explicitTiming}));oldChord();
 check(legacySummons==0&&g_nativeKeyboardShortcuts.size()==1&&g_nativeKeyboardShortcuts[0].holdMs==500,"explicit 500 ms remains a configured rule, not a hidden 250 ms default");
 for(const char* code:{"l4","r4","m1","m2","legionr"})for(const char* trigger:{"press","hold","release","double","repeat"}){
  auto edited=defaultSummon();edited["inputs"]=json::array({input(code,"oem")});edited["trigger"]=trigger;load(json::array({edited}));
  check(g_nativeKeyboardShortcuts.size()==1,"edited native default has one configured runtime owner");uint64_t bit=0;nativeKeyboardShortcutMaskBit(code,bit);check(g_nativeKeyboardShortcuts[0].requiredMask==bit,"OEM physical source identity is preserved");oldChord();check(legacySummons==0,"edited default never allows old shoulder summon");
  configuredTick(bit,10010);check(configuredSummons==0,"new bindings never inherit held inputs before a neutral sample");configuredTick(0,10020);
  configuredTick(bit,10100);configuredTick(bit,10700);configuredTick(0,10800);configuredTick(bit,10900);configuredTick(bit,11500);configuredTick(0,11600);
  const int expected=std::string(trigger)=="double"?0:2;
  if(std::string(trigger)=="double"){configuredTick(bit,11700);configuredTick(0,11720);configuredTick(bit,11800);configuredTick(0,11820);check(configuredSummons==1,"double source emits exactly once for the configured double press");}
  else check(configuredSummons==expected,"actual new-source trigger state machine dispatches configured summon only");
  const auto releasedCount=configuredSummons;configuredTick(0,12500);check(configuredSummons==releasedCount,"released source stops emitting new summon actions");
  edited["enabled"]=false;load(json::array({edited}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.empty(),"disabled replacement never revives old binding");
 }
 auto changed=defaultSummon();changed["inputs"]=json::array({input("l4","oem")});
 auto replacement=changed;replacement["origin"]="custom";replacement.erase("defaultKey");replacement["id"]="replacement-L4";
 load(json::array({replacement}));oldChord();regression(legacySummons==0,"explicit custom L4 replacement must not leave hidden LB+RB fallback");check(g_nativeKeyboardShortcuts.size()==1,"replacement L4 parses to runtime");
 load(json::array());oldChord();regression(legacySummons==0,"explicitly empty shortcut list must not revive default summon");
 load(json::array({defaultSummon(),replacement}));oldChord();check(legacySummons==1&&g_nativeKeyboardShortcuts.size()==1,"two explicitly saved summon bindings both remain permitted");
 load(json::array({changed}));refreshMode(json::array({replacement}));oldChord();regression(legacySummons==0,"independent mode custom L4 list must not add implicit default summon");
 load(json::array({changed}));refreshMode(json::array({defaultSummon()}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.size()==1,"retired independent default cannot override the edited global list");
 fixtureInput["bootModeShortcuts"]["boot"]="old-boot";fixtureInput["revision"]=3;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(30000);legacySummons=0;g_defaultSummonHold.release();oldChord();check(legacySummons==0,"expired mode falls back to the configured global replacement");
 load(json::array({changed}));auto wrong=defaultSummon();wrong["inputs"]=json::array({input("no-such-key","oem")});load(json::array({wrong}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.empty(),"invalid edited input does not silently restore original default");
 const auto savedByEditor=json::parse(R"EDITOR(${JSON.stringify(editorExports)})EDITOR");
 for(const auto& saved:savedByEditor){
  fixtureInput=saved["input"];fixtureInput["revision"]=100;g_nativeKeyboardShortcutRevision=-1;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(40000);g_defaultSummonHold.release();legacySummons=configuredSummons=0;oldChord();
  check(legacySummons==0,"actual editor save receipt never retains implicit LB+RB");
  configuredTick(0,41000);configuredTick(1ull<<37,41100);configuredTick(0,41150);
  const bool enabled=saved["name"]=="edited-native-default-L4"||saved["name"]=="custom-replacement-L4";
  check(configuredSummons==(enabled?1:0),"actual editor saved L4 triggers only when its saved rule is enabled");
 }
 std::cout<<"CHECKS "<<checks<<" REGRESSIONS "<<regressions<<" device_io=none physical_input=none settings_io=memory\n";return regressions?2:0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
`;
const generated=path.join(out,'summon-rebind.cpp');fs.writeFileSync(generated,cpp);
const sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';const exe=path.join(out,'summon-rebind.exe'),batch=path.join(out,'build.cmd');
fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" '+version+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+['um','shared','ucrt'].map(p=>'/I"'+sdk+'/Include/'+version+'/'+p+'"').join(' ')+' /I"'+root+'/native" /I"'+root+'/deps/json" "'+generated+'" /Fo"'+out+'/summon-rebind.obj" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'/Lib/'+version+'/um/x64" /LIBPATH:"'+sdk+'/Lib/'+version+'/ucrt/x64"\r\nexit /b %errorlevel%\r\n');
const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'BUILD.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,build.stdout+build.stderr);
const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));console.log((run.stdout||'')+(run.stderr||''));const report={pass:run.status===0,exit:run.status,result:run.stdout,source:sourceFile,sourceSha256:createHash('sha256').update(source).digest('hex'),actualRuleParser:true,actualLegacyShoulderHandler:true,actualTriggerStateMachine:true,hardwareIo:false,installedSettingsWritten:false};fs.writeFileSync(path.join(out,'ACCEPTANCE.json'),JSON.stringify(report,null,2));assert.equal(run.status,0,'Production summon binding regression');
