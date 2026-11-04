// Audits the production parser and trigger state machine for every visible output.
// The OS window/action, configuration and clock are in-memory seams only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {createRequire} from 'node:module';import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerShortcutFullAudit-20261031/native');fs.mkdirSync(out,{recursive:true});
const sourceFile=process.argv[3]||path.join(root,'native/main.cpp'),source=fs.readFileSync(sourceFile,'utf8');
function extract(name){const regex=new RegExp(String.raw`^static [^\n;]*\b${name}\([^;{]*\)\s*\{[^]*?\n\}`,'m'),match=regex.exec(source);assert(match,'Missing '+name);return match[0]}
const structure=source.match(/struct NativeKeyboardShortcutRule \{[\s\S]*?\n\};/)[0];
const funcs=['nativeKeyboardShortcutMaskBit','nativeKeyboardShortcutDuration','nativeKeyboardShortcutRequiredMask','nativeInputShortcutDesktopKey','nativeInputShortcutDesktopKeys','nativeKeyboardShortcutVirtualKey','nativeKeyboardShortcutInputFlags','nativeKeyboardShortcutAllInputs','nativeYmccShortcutAction','nativeYmccShortcutDelta','nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed','nativeKeyboardShortcutsRefresh'].map(extract);

const req=createRequire(path.join(root,'package.json')),defaultsModule={exports:{}};new Function('module','exports','require',req('esbuild').buildSync({entryPoints:[path.join(root,'src/bridge/controllerShortcutRules.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text)(defaultsModule,defaultsModule.exports,req);const factoryDefaults=defaultsModule.exports.NATIVE_DEFAULT_SHORTCUTS.map(d=>defaultsModule.exports.nativeDefaultRule(d.defaultKey));
const legacyStart=source.indexOf('    const bool shoulderDown = ',source.indexOf('static void nativeKeyboardShortcutsEvaluate(')),legacyEnd=source.indexOf('\n    gamepadProcessUiInput(w, now);',legacyStart);assert(legacyStart>0&&legacyEnd>legacyStart);const allLegacyCore=source.slice(legacyStart,legacyEnd);
const repairExports=process.argv[6]?JSON.parse(fs.readFileSync(process.argv[6],'utf8')):[];
const savedExports=process.argv[5]?JSON.parse(fs.readFileSync(process.argv[5],'utf8')):[];
const controlExports=process.argv[7]?JSON.parse(fs.readFileSync(process.argv[7],'utf8')):[];
const variants=JSON.parse(fs.readFileSync(process.argv[4]||'G:/YeManCC-Work/Build/Tasks/ControllerShortcutFullAudit-20261031/model/POSITIVE-VARIANTS.json','utf8'));
const evaluatorStart=source.indexOf('static void nativeKeyboardShortcutsEvaluate('),coreStart=source.indexOf('        const uint64_t currentMask = ',evaluatorStart),coreEnd=source.indexOf('        if (emit) {',coreStart);assert(coreStart>evaluatorStart&&coreEnd>coreStart);const triggerCore=source.slice(coreStart,coreEnd);
const cpp=String.raw`
#define YMCC_SHORTCUT_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <fstream>
#include <functional>
#include <string>
#include <set>
#include <vector>
#include "input_shortcut_toggle.h"
#include "shortcut_action_protocol.h"
#include "keyboard_shortcut_sequence.h"
#include "shortcut_frontend_button.h"
#include "boot_mode_shortcuts.h"
using json=nlohmann::json;
namespace ymcc::radial{static bool userAvailable(){return false;}static bool userActionAvailable(const std::string& s){return s!="radial.summon";}}
static constexpr int kOemProducerCount=8;
${structure}
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static uint32_t g_nativeShortcutLegacyOverrideMask=0;
static bool g_nativeShortcutRulesConfigured=false;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=400;
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{0};
static json fixtureInput;
static json ymSettingsSection(const char*){return fixtureInput;}
// In-memory settings seam; the production getter is checked by its real Win32 leaf.
static json ymSettingsFields(const char* section, std::initializer_list<const char*> fields) {
    const auto input = ymSettingsSection(section);
    json selected = json::object();
    if (input.is_object()) for (const auto* key : fields) {
        const auto value = input.find(key);
        if (value != input.end()) selected[key] = *value;
    }
    return selected;
}
static bool nativeInputShortcutDesktopKey(const std::string&,WORD&);
${funcs.join('\n')}
static std::set<int> desktopDown;
static SHORT fixtureGetAsyncKeyState(int key){return desktopDown.count(key)?SHORT(0x8000):0;}
#define GetAsyncKeyState fixtureGetAsyncKeyState
static void appendNativeLifecycleLog(const char*,const json&){}

namespace ymcc::keyboardEvents {enum class SourceId{NativeKeyboardShortcut};}
struct OemProducerTokens{};
static constexpr uint64_t kOemShortcutUnboundEpoch=0;
static std::atomic<bool> fixtureWindowCurrent{true};
static std::atomic<bool> g_exitRequested{false},g_shortcutRecordingActive{false},g_inputReady{true},fixtureProducerCurrent{true};
static std::atomic<uint64_t> fixturePowerGeneration{1};
static uint64_t currentPowerGeneration(){return fixturePowerGeneration.load();}
static bool sgVirtualWriterAllowedNow(){return true;}
static bool radialHasInputOwner(){return false;}
static bool oemProducerTokensCurrent(OemProducerTokens){return fixtureProducerCurrent.load();}
static bool msiWmiGenerationCurrent(uint64_t){return true;}
static bool msiWmiShortcutAuthoritative(){return false;}
static HWND fixtureForegroundWindow(){return reinterpret_cast<HWND>(uintptr_t(fixtureWindowCurrent.load()?42:43));}
#define GetForegroundWindow fixtureForegroundWindow
static std::mutex sequenceTraceMx;static std::condition_variable sequenceTraceCv;
struct SequenceTrace {WORD vk;bool up;uint64_t at;bool extended=false;};static std::vector<SequenceTrace> sequenceTrace;
static std::atomic<bool> fixtureRejectKeyUps{false};static std::atomic<unsigned> fixtureRejectedKeyUps{0};
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT requested,INPUT* inputs){
 if(fixtureRejectKeyUps.load()&&requested&&(inputs[0].ki.dwFlags&KEYEVENTF_KEYUP)){fixtureRejectedKeyUps.fetch_add(1);sequenceTraceCv.notify_all();return 0;}
 std::lock_guard<std::mutex> lock(sequenceTraceMx);for(UINT i=0;i<requested;++i)sequenceTrace.push_back({inputs[i].ki.wVk,(inputs[i].ki.dwFlags&KEYEVENTF_KEYUP)!=0,GetTickCount64(),(inputs[i].ki.dwFlags&KEYEVENTF_EXTENDEDKEY)!=0});sequenceTraceCv.notify_all();return requested;
}
enum class SerialJobKind{ExternalInjection};
static bool nativeYmccShortcutSubmit(std::function<void()> job,SerialJobKind,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){job();return true;}
#include "keyboard_shortcut_sequence_runtime.h"

static int emissions=0;
static void tick(uint64_t controllerMask,uint64_t oemMask,ULONGLONG now){for(auto& rule:g_nativeKeyboardShortcuts){const bool oemRule=(rule.requiredMask>>20)!=0;
${triggerCore}
if(emit)++emissions;rule.matchedLastTick=matched;
}}

static std::array<int,8> legacyCounts{};
static ULONGLONG fixtureNow=0;
static ULONGLONG fixtureTickCount(){return fixtureNow;}
#define GetTickCount64 fixtureTickCount
#define IsWindow(handle) true
#define IsWindowVisible(handle) true
static HWND g_hwnd=reinterpret_cast<HWND>(1);
struct FocusTargetSnapshot{};static FocusTargetSnapshot g_pendingSummonTarget;
static bool focusMainWindowIsForeground(){return false;}
static FocusTargetSnapshot focusCaptureForegroundTarget(){return {};}
static void nativeYmccShortcutSummon(const FocusTargetSnapshot&){++legacyCounts[0];}
static void focusBeginReturnToPreviousWindow(ULONGLONG){++legacyCounts[1];}
static void nativeAdjustTdp(int){++legacyCounts[2];}
static void nativeAdjustBrightness(int){++legacyCounts[3];}
static void runKillBat(){++legacyCounts[4];}
static void openTouchKeyboard(){++legacyCounts[5];}
static void returnToDesktop(){++legacyCounts[6];}
static void toggleMouseMode(const std::string&){++legacyCounts[7];}
template<class F>static void gamepadSerialSubmit(F job){job();}
static std::mutex g_gamepadSettingsMx;static std::string g_mouseBackend="fixture";
static bool g_summonEnabled=true,g_bDoubleMinimize=true,g_tdpShortcut=true,g_fpsShortcut=true,g_killGame=true,g_openKeyboard=true,g_returnDesktop=true,g_mouseToggle=true;
static WORD g_prevW=0;
static ULONGLONG gpHoldStart=0,gpLastBPress=0,g_bClosePendingSince=0,g_bCloseReadyAt=0,s_dpHeldStart=0,s_dpLastEmit=0,s_brightnessHeldStart=0,s_brightnessLastEmit=0,gpKbHoldStart=0,gpKxHoldStart=0,gpKyHoldStart=0;
static bool gpArmed=true,gpPrevB=false,g_bClosePending=false,gpKbArmed=true,gpKxArmed=true,gpKyArmed=true;
static void allLegacyTick(WORD w,ULONGLONG now){fixtureNow=now;constexpr ULONGLONG HOLD_MS=500,B_DOUBLE_MS=500;
const bool both=(w&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER))==(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER),startHeld=w&XINPUT_GAMEPAD_START,selectHeld=w&XINPUT_GAMEPAD_BACK,bPressed=w&XINPUT_GAMEPAD_B,xPressed=w&XINPUT_GAMEPAD_X,yPressed=w&XINPUT_GAMEPAD_Y,aPressed=w&XINPUT_GAMEPAD_A,dpUp=w&XINPUT_GAMEPAD_DPAD_UP,dpDown=w&XINPUT_GAMEPAD_DPAD_DOWN,dpLeft=w&XINPUT_GAMEPAD_DPAD_LEFT,dpRight=w&XINPUT_GAMEPAD_DPAD_RIGHT;
${allLegacyCore}
g_prevW=w;
}
static std::array<int,8> replayAllLegacy(){legacyCounts.fill(0);g_prevW=0;gpHoldStart=gpLastBPress=g_bClosePendingSince=g_bCloseReadyAt=s_dpHeldStart=s_dpLastEmit=s_brightnessHeldStart=s_brightnessLastEmit=gpKbHoldStart=gpKxHoldStart=gpKyHoldStart=0;gpArmed=gpKbArmed=gpKxArmed=gpKyArmed=true;gpPrevB=g_bClosePending=false;
const WORD chords[]={XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_Y};
ULONGLONG base=20000;for(const auto chord:chords){allLegacyTick(0,base);allLegacyTick(chord,base+100);allLegacyTick(chord,base+700);allLegacyTick(0,base+710);allLegacyTick(chord,base+800);allLegacyTick(0,base+810);allLegacyTick(chord,base+900);allLegacyTick(chord,base+1500);allLegacyTick(0,base+1510);allLegacyTick(0,base+1600);base+=3000;}return legacyCounts;}
static int checks=0;static json failures=json::array();
static void check(bool ok,const std::string& name){++checks;if(!ok){failures.push_back(name);std::cout<<"FAIL "<<name<<'\n';}}
static json key(const char* code,const char* src="controller"){return{{"source",src},{"code",code}};}
static json row(const char* id="good"){return{{"id",id},{"origin","custom"},{"enabled",true},{"actionId","window.summon"},{"inputs",json::array({key("a")})},{"trigger","press"},{"params",json::object()}};}
static void refresh(){g_nativeKeyboardShortcutLastRefresh=0;g_nativeKeyboardShortcutRevision=-1;nativeKeyboardShortcutsRefresh(10000);emissions=0;desktopDown.clear();}
static void load(json rows,bool initialized=true){fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRules",rows},{"shortcutRulesInitialized",initialized}}}}}};refresh();}
static int repeats(json rule,bool acceleration,const char* action="power.tdpAdjust"){rule["actionId"]=action;rule["params"]={{"direction",std::string(action)=="power.tdpAdjust"?"up":"right"},{"amount",1},{"linearAcceleration",acceleration}};rule["trigger"]="repeat";rule["holdMs"]=0;rule["intervalMs"]=150;load(json::array({rule}));tick(0,0,10000);for(ULONGLONG t=10100;t<=15100;t+=10)tick(XINPUT_GAMEPAD_A,0,t);return emissions;}

static uint64_t observedKeyboardHoldMs=0;
static void keyboardSequenceChecks(){
 using namespace ymcc::keyboardSequence;
 const auto item=[](const char* action,const char* code){return json{{"actionId",action},{"code",code}};};
 const json chord={{"keys",json::array({item("keyboard.letter","KeyS"),item("keyboard.leftFunction","ControlLeft")})},{"holdMs",80}};
 const json seq={{"version",1},{"steps",json::array({chord})},{"mode","once"},{"repeatCount",1},{"intervalMs",150}};
 auto parseSequence=[&](const json& value,Plan& plan){return parse(json{{"keySequence",value.dump()}},"keyboard.letter",nativeKeyboardShortcutVirtualKey,plan);};
 Plan plan;check(parseSequence(seq,plan)&&plan.steps[0].keys[0].vk==VK_LCONTROL&&plan.steps[0].keys[1].vk=='S',"native combo parser always presses modifiers before main keys");
 Plan legacy;check(parse(json{{"key","KeyX"}},"keyboard.letter",nativeKeyboardShortcutVirtualKey,legacy)&&legacy.steps[0].duration==80,"existing single keys migrate to 80ms without changing configuration");
 std::vector<std::pair<unsigned short,bool>> trace;const auto send=[&](Key k,bool up){trace.push_back({k.vk,up});return true;};const auto neutral=[](Key){return false;};
 Engine e;e.start("chord",plan,0);e.tick(0,true,send,neutral);check(trace.size()==2&&!trace[0].second,"chord key-downs are separate from delayed key-ups");e.tick(79,true,send,neutral);check(trace.size()==2,"no 2ms pulse: no release before hold deadline");e.tick(80,true,send,neutral);check(trace.size()==4&&trace[2]==std::make_pair((unsigned short)'S',true)&&trace[3]==std::make_pair((unsigned short)VK_LCONTROL,true)&&!e.active(),"80ms chord releases main key before modifier");
 auto delayed=seq;delayed["steps"]=json::array({chord,json{{"delayMs",2000}},json{{"keys",json::array({item("keyboard.letter","KeyX")})},{"holdMs",80}}});Plan waits;check(parseSequence(delayed,waits),"native parser accepts 2 second wait");trace.clear();Engine delay;delay.start("wait",waits,0);delay.tick(0,true,send,neutral);delay.tick(80,true,send,neutral);delay.tick(2079,true,send,neutral);check(trace.size()==4,"explicit delay sends no early key-down");delay.tick(2080,true,send,neutral);check(trace.size()==5&&trace.back().first=='X'&&!trace.back().second,"next step starts after complete wait");delay.tick(2160,true,send,neutral);check(!delay.active()&&!delay.pending(),"one-shot sequence completes with all keys released");
 Plan repeated=legacy;repeated.steps.push_back(legacy.steps[0]);trace.clear();Engine duplicates;duplicates.start("twice",repeated,0);duplicates.tick(0,true,send,neutral);duplicates.tick(80,true,send,neutral);duplicates.tick(119,true,send,neutral);check(trace.size()==2,"repeated identical key has a real 40ms released gap");duplicates.tick(120,true,send,neutral);duplicates.tick(200,true,send,neutral);check(trace.size()==4&&!duplicates.active(),"duplicate key steps both execute");
 Plan loop=legacy;loop.toggle=true;trace.clear();Engine looping;looping.start("loop",loop,0);looping.tick(0,true,send,neutral);looping.tick(80,true,send,neutral);looping.tick(230,true,send,neutral);check(trace.size()==3,"loop executes next cycle after saved interval");looping.start("loop",loop,231);looping.tick(231,true,send,neutral);check(trace.size()==4&&!looping.active()&&!looping.pending(),"second trigger stops loop and releases held key immediately");looping.tick(10000,true,send,neutral);check(trace.size()==4,"stopped loop cannot resume by itself");
 trace.clear();Engine cancel;cancel.start("cancel",plan,0);cancel.tick(0,true,send,neutral);cancel.tick(1,false,send,neutral);check(trace.size()==4&&!cancel.active()&&!cancel.pending(),"revoked owner cancels before hold deadline and releases all owned keys");
 trace.clear();Engine partial;partial.start("partial",plan,0);partial.tick(0,true,[&](Key k,bool up){if(!up&&k.vk=='S')return false;return send(k,up);},neutral);check(trace.size()==2&&trace.back().second&&!partial.active()&&!partial.pending(),"failed main key-down repairs already pressed modifier without continuing");
 trace.clear();Engine release;release.start("debt",plan,0);release.tick(0,true,send,neutral);release.tick(80,true,[](Key,bool){return false;},neutral);check(release.pending()&&!release.active(),"failed key-up is retained as release-only debt");release.tick(329,true,send,neutral);check(trace.size()==2,"release retry respects backoff");release.tick(330,true,send,neutral);check(!release.pending()&&trace.size()==4&&trace[2].second&&trace[3].second,"failed release retries only owned ups in reverse order");
 trace.clear();Engine physical;physical.start("physical",plan,0);physical.tick(0,true,send,[](Key k){return k.vk==VK_LCONTROL;});physical.tick(80,true,send,neutral);check(trace.size()==2&&trace[0].first=='S'&&trace[1].first=='S',"physical held modifier is never released by the macro");
 for(int n=0;n<9;++n){auto bad=seq;if(n==0)bad["steps"]=json::array();if(n==1)bad["steps"][0]["holdMs"]=2;if(n==2)bad["steps"][0]["holdMs"]=2001;if(n==3)bad["repeatCount"]=101;if(n==4)bad["mode"]="bad";if(n==5)bad["steps"]=json::array({json{{"delayMs",2000}}});if(n==6)bad["steps"][0]["keys"][0]["code"]="Unknown";if(n==7)bad["steps"][0]["keys"][1]=bad["steps"][0]["keys"][0];if(n==8)bad["intervalMs"]=0;Plan invalid;check(!parseSequence(bad,invalid),"native keyboard sequence rejects malformed boundary "+std::to_string(n));}
 // Exercise the actual worker/adapter with only the lowest OS send port mocked.
 NativeKeyboardSequenceOwner owner;const auto waitSize=[](size_t count){std::unique_lock<std::mutex> lock(sequenceTraceMx);return sequenceTraceCv.wait_for(lock,std::chrono::seconds(2),[&]{return sequenceTrace.size()>=count;});};
 check(owner.submit("actual-chord",plan,[]{return true;}),"actual worker accepts combo without blocking serial input");check(waitSize(4),"actual worker sends both down and delayed reverse ups");{
  std::lock_guard<std::mutex> lock(sequenceTraceMx);observedKeyboardHoldMs=sequenceTrace.size()>=4?sequenceTrace[2].at-sequenceTrace[1].at:0;check(sequenceTrace.size()==4&&observedKeyboardHoldMs>=80,"actual native worker separates down/up by approximately 80ms rather than one batch");}
 check(owner.submit("actual-loop",loop,[]{return true;}),"actual worker starts toggle loop");check(waitSize(5),"actual loop produces first down");check(owner.submit("actual-loop",loop,[]{return true;}),"actual same trigger is accepted as stop");check(waitSize(6),"actual stop sends outstanding up");
 check(owner.submit("revoked",plan,[]{return fixtureProducerCurrent.load();}),"actual worker accepts revocable producer");check(waitSize(8),"actual producer presses chord before revoke");fixtureProducerCurrent=false;check(waitSize(10),"actual source revoke releases chord on worker without another physical trigger");owner.stop();fixtureProducerCurrent=true;
 g_keyboardSequenceInputFence['S'].store(1);desktopDown.insert('S');check(nativeKeyboardSequenceOwnsKey('S'),"injected held output is excluded from physical shortcut input");g_keyboardSequenceInputFence['S'].store(2);check(nativeKeyboardSequenceOwnsKey('S'),"release boundary remains fenced while OS still reports the injected down");desktopDown.clear();check(nativeKeyboardSequenceOwnsKey('S')&&!nativeKeyboardSequenceOwnsKey('S'),"neutral sample retires injected-key fence before later real input");nativeKeyboardSequenceOwnsKey(VK_LCONTROL);nativeKeyboardSequenceOwnsKey('X');

 NativeKeyboardShortcutRule dispatchedRule;dispatchedRule.outputId="real-dispatch";dispatchedRule.keyboardPlan=plan;
 const auto baseTrace=sequenceTrace.size();check(nativeKeyboardSequenceDispatch(dispatchedRule,kOemShortcutUnboundEpoch,0,0,false,{}),"production dispatcher submits recorded keyboard plan to worker");check(waitSize(baseTrace+2),"production dispatcher reaches separate real worker key-down calls");fixtureWindowCurrent=false;check(waitSize(baseTrace+4),"foreground window change cancels real dispatch and releases chord");fixtureWindowCurrent=true;
 for(int guard=0;guard<4;++guard){dispatchedRule.outputId="guard-"+std::to_string(guard);const auto before=sequenceTrace.size();check(nativeKeyboardSequenceDispatch(dispatchedRule,kOemShortcutUnboundEpoch,0,0,false,{}),"actual dispatch guard admitted before transition "+std::to_string(guard));check(waitSize(before+2),"actual dispatch guard key-down "+std::to_string(guard));if(guard==0)g_shortcutRecordingActive=true;if(guard==1)g_inputReady=false;if(guard==2)fixturePowerGeneration.fetch_add(1);if(guard==3)g_exitRequested=true;check(waitSize(before+4),"actual recording/input/power/exit revoke releases owned chord "+std::to_string(guard));g_shortcutRecordingActive=false;g_inputReady=true;g_exitRequested=false;}
 g_nativeKeyboardSequenceOwner.stop();
 uint64_t slowNow=0;Engine slow;slow.start("slow-port",plan,0);slow.tick(0,true,[&](Key,bool){slowNow+=20;return true;},neutral,[&]{return slowNow;});int earlyUps=0;slow.tick(119,true,[&](Key,bool up){if(up)++earlyUps;return true;},neutral,[&]{return uint64_t(119);});check(earlyUps==0,"hold duration starts after the final down send returns, even if sender is slow");
 const auto count=sequenceTrace.size();std::this_thread::sleep_for(std::chrono::milliseconds(30));check(sequenceTrace.size()==count,"native worker stop joins and emits no later downs");
 // Reverse-check two independent OS key identities and cross-rule release debt.
 auto enterSequence=seq;enterSequence["steps"]=json::array({json{{"keys",json::array({item("keyboard.leftFunction","Enter"),item("keyboard.numeric","NumpadEnter")})},{"holdMs",80}}});Plan enters;
 check(parseSequence(enterSequence,enters)&&enters.steps[0].keys.size()==2&&!enters.steps[0].keys[0].extended&&enters.steps[0].keys[1].extended,"main Enter and numeric Enter stay separate keys in a chord");
 Plan manualEnters;manualEnters.steps.push_back({{{VK_RETURN,false},{VK_RETURN,true}},80});
 NativeKeyboardSequenceOwner enterPlayback;const auto playbackStart=sequenceTrace.size();check(enterPlayback.submit("enters",manualEnters,[]{return true;}),"distinct Enter playback starts");check(waitSize(playbackStart+4),"both main and numeric Enter receive their own down and up");enterPlayback.stop();{std::lock_guard<std::mutex> lock(sequenceTraceMx);check(sequenceTrace.size()>=playbackStart+4&&!sequenceTrace[playbackStart].extended&&sequenceTrace[playbackStart+1].extended&&sequenceTrace[playbackStart+2].extended&&!sequenceTrace[playbackStart+3].extended,"extended numeric Enter packets keep their identity through reverse release");}
 NativeKeyboardSequenceOwner debtOwner;const auto debtStart=sequenceTrace.size();fixtureRejectKeyUps=true;fixtureRejectedKeyUps=0;
 check(debtOwner.submit("failed-release",plan,[]{return true;}),"release-failure scenario starts original chord");check(waitSize(debtStart+2),"release-failure scenario owns modifier and main key");{
  std::unique_lock<std::mutex> lock(sequenceTraceMx);check(sequenceTraceCv.wait_for(lock,std::chrono::seconds(2),[]{return fixtureRejectedKeyUps.load()>0;}),"actual worker observes rejected key-up");}
 const bool admittedDuringDebt=debtOwner.submit("must-not-run-under-stuck-Ctrl",legacy,[]{return true;});check(!admittedDuringDebt,"pending failed release blocks new rules instead of typing under a stuck modifier");
 fixtureRejectKeyUps=false;check(waitSize(debtStart+4),"release-only cleanup completes once sender recovers");check(debtOwner.submit("explicit-retry-after-release",legacy,[]{return true;}),"repaired release debt allows an explicit later user trigger again");check(waitSize(debtStart+6),"explicit retry executes without replaying rejected tasks");debtOwner.stop();

}

int main(){try{
keyboardSequenceChecks();
json variants;std::ifstream("VARIANTS.json")>>variants;
std::set<std::string> ui={"os.switchProgram","monitor.toggle","display.frameRateControl","fan.switchPreset","app.launchQuickApp"};

const auto factory=json::parse(R"FACTORY(${JSON.stringify(factoryDefaults)})FACTORY");
load(factory);const auto originals=replayAllLegacy();for(int slot=0;slot<8;slot++)check(originals[slot]>0,"actual legacy branch positive control "+factory[slot]["defaultKey"].get<std::string>());
for(int slot=0;slot<8;slot++)for(int scenario=0;scenario<4;scenario++){auto rows=factory;auto& r=rows[slot];if(scenario==0){r["inputs"]=json::array({key("l4","oem")});r["trigger"]="press";}else if(scenario==1)r["enabled"]=false;else if(scenario==2)rows.erase(rows.begin()+slot);else{r["origin"]="custom";r.erase("defaultKey");r["inputs"]=json::array({key("l4","oem")});r["trigger"]="press";}load(rows);const auto counts=replayAllLegacy();const auto name=factory[slot]["defaultKey"].get<std::string>();check(counts[slot]==0,"actual old branch disabled after edit/disable/delete/replacement "+name+" scenario "+std::to_string(scenario));for(int other=0;other<8;other++)if(other!=slot)check(counts[other]>0,"other original branches preserved while editing "+name+" scenario "+std::to_string(scenario));}
// The saved rule table is the only shortcut authority even if the old
// compatibility flags are stale after a failed secondary IPC sync.
load(factory);g_summonEnabled=g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=false;
const auto staleFlags=replayAllLegacy();for(int slot=0;slot<8;slot++)check(staleFlags[slot]>0,"restored enabled template survives stale legacy flag in slot "+std::to_string(slot));
load(json::array(),false);const auto firstRunOff=replayAllLegacy();for(int slot=0;slot<8;slot++)check(firstRunOff[slot]==0,"unconfigured first-run still respects old disabled flags");
g_summonEnabled=false;g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=true;load(json::array({factory[1]}));check(replayAllLegacy()[1]>0,"keeping hide enabled does not depend on retaining the summon action");
g_summonEnabled=g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=true;
// Valid explicit templates must survive an opaque/damaged sibling carrying
// the same historical slot; restoring defaults is a real recovery operation.
for(int slot=0;slot<8;slot++){
 auto damaged=factory[slot];damaged.erase("actionId");damaged["id"]="opaque-default";
 load(json::array({factory[slot],damaged}));check(nativeShortcutLegacyDefaultAllowed(factory[slot]["defaultKey"].get<std::string>().c_str()),"restored template survives damaged sibling in slot "+std::to_string(slot));check(replayAllLegacy()[slot]>0,"restored original branch really dispatches in slot "+std::to_string(slot));
 auto changed=factory[slot];changed["id"]="explicit-second-binding";changed["inputs"]=json::array({key("l4","oem")});changed["trigger"]="press";
 load(json::array({factory[slot],changed}));check(replayAllLegacy()[slot]>0&&g_nativeKeyboardShortcuts.size()==1,"two explicitly saved old/new bindings both remain active in slot "+std::to_string(slot));
}
// Every available output/parameter variant is also legal on every default identity.
const json invalidInputLists=json::array({json::array(),json::array({key("a"),key("b"),key("x"),key("y"),key("lb")}),json::array({key("a"),key("a")}),json::array({key("a","future")}),json::array({key("a","oem")}),json::array({key("F25","keyboard")}),json::array({key("MouseUnknown","mouse")}),json::array({7}),json::array({json::object()})});
for(int slot=0;slot<8;slot++)for(const auto& inputs:invalidInputLists){auto broken=factory[slot];broken["inputs"]=inputs;load(json::array({broken,row()}));check(g_nativeKeyboardShortcuts.size()==1&&g_nativeShortcutLegacyOverrideMask==255,"invalid default chord cannot execute, truncate or revive its old binding; valid sibling remains");}
fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRulesInitialized",true}}}}}};refresh();check(g_nativeKeyboardShortcuts.empty()&&g_nativeShortcutLegacyOverrideMask==255,"missing initialized array stays user-owned and never enables defaults");
for(int slot=0;slot<8;slot++)for(const auto& variant:variants){auto r=variant["rule"];r["origin"]="native-default";r["defaultKey"]=factory[slot]["defaultKey"];r["inputs"]=json::array({key("l4","oem")});load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,"every default slot accepts visible output: "+variant["name"].get<std::string>());check(g_nativeShortcutLegacyOverrideMask==255,"replacing any default output leaves no hidden old shortcut");}
// A damaged initialized table must not turn user-owned defaults back on.
for(const auto& bad:json::array({nullptr,7,"bad",json::object()})){
 fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRulesInitialized",true},{"shortcutRules",bad}}}}}};refresh();check(g_nativeShortcutLegacyOverrideMask==255&&g_nativeKeyboardShortcuts.empty(),"initialized damaged table fails closed instead of reviving factory shortcuts");
}
for(const auto& variant:variants){const auto r=variant["rule"];const auto action=r["actionId"].get<std::string>(),name=variant["name"].get<std::string>();load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,name+" parses exactly once");if(g_nativeKeyboardShortcuts.empty())continue;const auto p=g_nativeKeyboardShortcuts[0];
 if(action.rfind("keyboard.",0)==0){check(p.virtualKey!=0&&p.ownAction.empty()&&p.uiAction.empty()&&p.inputAction.empty(),name+" keyboard dispatch category");check(p.requiresShift==(action=="keyboard.symbol"),name+" symbol shift");const auto code=r["params"]["key"].get<std::string>();check(p.forceExtendedKey==(code=="NumpadEnter"),name+" keypad Enter flag");if(action=="keyboard.letter")check(p.virtualKey==code[3],name+" letter virtual key");if(code.rfind("Digit",0)==0)check(p.virtualKey==code[5],name+" digit virtual key");}
 else if(ui.count(action)){check(p.uiAction==action&&p.uiParams==r["params"],name+" UI output params preserved");}
 else if(action=="input.gyroToggle"||action=="input.virtualGamepadToggle"){check(p.inputAction==action&&p.preset==r["params"]["preset"].get<std::string>(),name+" input toggle preset");}
 else {check(p.ownAction==action,name+" native dispatch category");if(action=="power.tdpAdjust"||action=="display.brightnessAdjust"){const std::string dir=r["params"]["direction"];const auto amount=r["params"]["amount"];const int n=amount.is_string()?std::stoi(amount.get<std::string>()):amount.get<int>();check(p.actionDelta==((dir=="up"||dir=="right")?n:-n),name+" signed delta");}}
 for(const char* trigger:{"press","hold","release","double","repeat"}){auto edit=r;edit["trigger"]=trigger;edit["holdMs"]=500;edit["doubleWindowMs"]=500;edit["intervalMs"]=150;load(json::array({edit}));if(edit["params"].contains("keySequence")&&json::parse(edit["params"]["keySequence"].get<std::string>())["mode"]=="toggle"&&std::string(trigger)=="repeat"){check(g_nativeKeyboardShortcuts.empty(),"loop cannot toggle on every repeat frame");continue;}tick(XINPUT_GAMEPAD_A,0,10000);check(emissions==0,name+" "+trigger+" revision held blocked");tick(0,0,10010);tick(XINPUT_GAMEPAD_A,0,10100);tick(XINPUT_GAMEPAD_A,0,10600);tick(0,0,10610);tick(XINPUT_GAMEPAD_A,0,10650);tick(XINPUT_GAMEPAD_A,0,11150);tick(0,0,11160);const int expected=std::string(trigger)=="double"?0:2;check(emissions==expected,name+" "+trigger+" edge counts");}
}
for(const char* trigger:{"press","hold","release","double","repeat"})for(int count=1;count<=4;count++){auto r=row();r["trigger"]=trigger;r["holdMs"]=100;r["inputs"]=json::array({key("a")});if(count>=2)r["inputs"].push_back(key("l4","oem"));if(count>=3)r["inputs"].push_back(key("F14","keyboard"));if(count>=4)r["inputs"].push_back(key("KeyK","keyboard"));load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,std::string(trigger)+" mixed input count "+std::to_string(count));if(g_nativeKeyboardShortcuts.empty())continue;tick(0,0,10000);uint64_t oem=0;if(count>=2)nativeKeyboardShortcutMaskBit("l4",oem);if(count>=3)desktopDown.insert(VK_F14);if(count>=4)desktopDown.insert('K');tick(XINPUT_GAMEPAD_A,oem,10100);tick(XINPUT_GAMEPAD_A,oem,10200);desktopDown.clear();tick(0,0,10210);if(count>=3)desktopDown.insert(VK_F14);if(count>=4)desktopDown.insert('K');tick(XINPUT_GAMEPAD_A,oem,10300);tick(XINPUT_GAMEPAD_A,oem,10400);desktopDown.clear();tick(0,0,10410);check(emissions==(std::string(trigger)=="double"?1:2),std::string(trigger)+" mixed input trigger semantics");}
json savedDocuments;std::ifstream("SAVED-INPUTS.json")>>savedDocuments;
for(const auto& saved:savedDocuments){const auto input=saved["input"];const auto rows=input["buttonMapping"]["rules"]["shortcutRules"];
if(saved.value("name","")=="actual editor combo sequence") {
 fixtureInput=input;refresh();check(rows.size()==9&&g_nativeKeyboardShortcuts.size()==1,"actual sequence save retains eight original defaults and one custom owner");
 if(!g_nativeKeyboardShortcuts.empty()){const auto& rule=g_nativeKeyboardShortcuts[0];check(rule.requiredMask==XINPUT_GAMEPAD_START,"actual sequence save preserves its real Start input");check(rule.keyboardPlan.toggle&&rule.keyboardPlan.steps.size()==4&&rule.keyboardPlan.steps[1].keys.empty()&&rule.keyboardPlan.steps[1].duration==2000,"actual UI JSON becomes native loop with complete 2-second wait");check(rule.keyboardPlan.steps[0].keys.size()==2&&rule.keyboardPlan.steps[0].keys[0].vk==VK_LCONTROL&&rule.keyboardPlan.steps[0].keys[1].vk=='S',"actual UI Ctrl+S becomes ordered native chord");check(rule.keyboardPlan.steps[2].keys.size()==2&&rule.keyboardPlan.steps[2].keys[0].vk==VK_LSHIFT&&rule.keyboardPlan.steps[2].keys[1].vk=='X'&&rule.keyboardPlan.steps[3].keys[1].vk=='X',"actual repeated Shift+X steps survive native parse");}
 continue;
}
size_t expected=0;for(const auto& r:rows){load(json::array({r}));const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==1,"actual editor saved row has exactly one runtime owner: "+r.value("id",""));expected+=g_nativeKeyboardShortcuts.size();if(!g_nativeKeyboardShortcuts.empty()){const auto& p=g_nativeKeyboardShortcuts[0];check(p.requiredMask==(uint64_t(XINPUT_GAMEPAD_X)|(1ull<<20)),"actual editor saved source/code becomes the exact physical mask");}}
fixtureInput=input;refresh();check(g_nativeKeyboardShortcuts.size()==expected,"actual editor saved table preserves every individually valid runtime rule");check(expected==(saved["name"].get<std::string>().rfind("defaults-",0)==0?8:21),"actual editor edited defaults/new outputs have expected native ownership count");}

json repairDocuments;std::ifstream("REPAIR-SAVED-INPUTS.json")>>repairDocuments;
for(const auto& saved:repairDocuments){const auto rows=saved["input"]["buttonMapping"]["rules"]["shortcutRules"];size_t expected=0;
 for(const auto& r:rows){load(json::array({r}));const auto action=ymcc::inputShortcutString(r,"actionId");const bool enabled=r.value("enabled",true);const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());const bool supported=!action.empty()&&(nativeYmccShortcutAction(action)||ymcc::shortcutUiAction(action)||ymcc::inputShortcutAction(action)||action.rfind("keyboard.",0)==0);
 check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==(enabled&&supported?1:0),"repaired UI receipt has the correct enabled runtime owner: "+saved["name"].get<std::string>());expected+=g_nativeKeyboardShortcuts.size();if(g_nativeKeyboardShortcuts.empty())continue;const auto& p=g_nativeKeyboardShortcuts[0];
 check(p.trigger==r.value("trigger","press"),"repaired UI receipt trigger survives native parsing");check(p.holdMs==r.value("holdMs",500ull)&&p.intervalMs==r.value("intervalMs",150ull)&&p.doubleWindowMs==r.value("doubleWindowMs",500ull),"repaired UI receipt timing survives native parsing");
 uint64_t expectedMask=0;bool known=true;for(const auto& input:r["inputs"]){const auto code=input["code"].get<std::string>();if(input["source"]!="controller"){known=false;break;}if(code=="a")expectedMask|=XINPUT_GAMEPAD_A;else if(code=="b")expectedMask|=XINPUT_GAMEPAD_B;else if(code=="x")expectedMask|=XINPUT_GAMEPAD_X;else if(code=="y")expectedMask|=XINPUT_GAMEPAD_Y;else{known=false;break;}}
 if(known)check(p.requiredMask==expectedMask,"repaired UI receipt has the exact newly recorded physical mask");
 if(!p.uiAction.empty()){check(p.uiAction==action,"repaired UI output action is unchanged");for(auto it=p.uiParams.begin();it!=p.uiParams.end();++it)check(r["params"].contains(it.key())&&r["params"][it.key()]==it.value(),"repaired UI known output params survive while native ignores extensions");}
 if(action=="keyboard.letter")check(p.virtualKey==r["params"]["key"].get<std::string>()[3],"repaired keyboard key becomes the exact native virtual key");
 }
 fixtureInput=saved["input"];refresh();check(g_nativeKeyboardShortcuts.size()==expected,"repaired UI table parses without losing valid sibling rules");
}
json controlDocuments;std::ifstream("CONTROL-SAVED-INPUTS.json")>>controlDocuments;
for(const auto& saved:controlDocuments){
 const auto rows=saved["input"]["buttonMapping"]["rules"]["shortcutRules"];size_t expected=0;
 for(const auto& r:rows){if(!r.is_object()||!r.contains("actionId")||!r["actionId"].is_string())continue;const auto action=r["actionId"].get<std::string>();if(!nativeYmccShortcutAction(action)&&!ymcc::shortcutUiAction(action)&&!ymcc::inputShortcutAction(action)&&action.rfind("keyboard.",0)!=0)continue;
  load(json::array({r}));const bool active=r.value("enabled",true);const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==(active?1:0),"UI control receipt has exactly the enabled runtime owner: "+saved["name"].get<std::string>());expected+=g_nativeKeyboardShortcuts.size();
  if(!g_nativeKeyboardShortcuts.empty()){const auto& parsed=g_nativeKeyboardShortcuts[0];check(parsed.trigger==r["trigger"].get<std::string>(),"UI control receipt trigger is unchanged");check(parsed.holdMs==r["holdMs"].get<ULONGLONG>()&&parsed.doubleWindowMs==r["doubleWindowMs"].get<ULONGLONG>()&&parsed.intervalMs==r["intervalMs"].get<ULONGLONG>(),"UI control receipt timing survives exactly");uint64_t expectedMask=0;std::vector<WORD> desktop;check(nativeKeyboardShortcutAllInputs(r["inputs"],expectedMask,desktop)&&parsed.requiredMask==expectedMask&&parsed.desktopKeys==desktop,"UI control receipt input sources reverse to the exact physical chord");if(action=="monitor.toggle")check(parsed.uiAction==action,"default slot can dispatch its chosen monitor output");if(action=="keyboard.letter")check(parsed.virtualKey=='K',"default slot can dispatch its chosen real keyboard output");}
 }
 fixtureInput=saved["input"];refresh();check(g_nativeKeyboardShortcuts.size()==expected,"whole control receipt retains all individually valid rules: "+saved["name"].get<std::string>());
 const auto old=replayAllLegacy();for(int slot=0;slot<8;slot++){bool explicitOriginal=false;for(const auto& r:rows)if(r.is_object()&&r.value("enabled",true)&&nativeShortcutLegacyTemplateMatches(r,slot)&&r.value("origin","")=="native-default"&&r.value("defaultKey","")==factory[slot]["defaultKey"].get<std::string>())explicitOriginal=true;check((old[slot]>0)==explicitOriginal,"no hidden original branch beyond explicitly saved templates: "+saved["name"].get<std::string>()+" slot "+std::to_string(slot));}
}
for(int field=0;field<3;field++)for(int position=0;position<3;position++){auto bad=row("bad");if(field==0)bad["trigger"]=123;else if(field==1){bad["actionId"]="power.tdpAdjust";bad["params"]={{"direction",123}};}else bad["params"]=nullptr;json rows=json::array({row("a"),row("b")});rows.insert(rows.begin()+position,bad);load(rows);check(g_nativeKeyboardShortcuts.size()==2,"malformed row "+std::to_string(field)+" position "+std::to_string(position)+" does not remove good siblings");}
load(json::array(),false);check(nativeShortcutLegacyDefaultAllowed("enabled"),"uninitialized empty global uses factory defaults");load(json::array(),true);check(!nativeShortcutLegacyDefaultAllowed("enabled"),"explicit initialized empty list disables all defaults");fixtureInput["startupObservedBoot"]="boot-a";fixtureInput["bootModeShortcuts"]={{"schema",1},{"mode","steam"},{"boot","boot-a"},{"rules",json::array()}};refresh();check(!nativeShortcutLegacyDefaultAllowed("enabled"),"retired empty mode cannot override the explicit global empty table");
load(json::array({row()}));fixtureInput["bootModeShortcuts"]={{"schema","bad"}};refresh();check(g_nativeKeyboardShortcuts.size()==1,"malformed mode metadata cannot swallow valid global rules");
fixtureInput={{"revision",1},{"startupObservedBoot","boot-a"},{"bootModeShortcuts",{{"schema",1},{"mode","steam"},{"boot","boot-a"},{"rules",json::array({row()})}}}};refresh();check(g_nativeKeyboardShortcuts.empty()&&nativeShortcutLegacyDefaultAllowed("enabled"),"retired mode rules cannot replace the unconfigured global defaults");
auto damagedDefault=row();damagedDefault["origin"]="native-default";damagedDefault["defaultKey"]="enabled";damagedDefault["trigger"]=12;load(json::array({damagedDefault,row()}));check(g_nativeKeyboardShortcuts.size()==1&&!nativeShortcutLegacyDefaultAllowed("enabled"),"damaged edited default cannot revive its old binding or remove siblings");
const auto normal=repeats(row(),false),accelerated=repeats(row(),true);check(accelerated>normal,"saved linear acceleration increases repeated TDP rate");check(normal==34,"disabled acceleration honors fixed 150ms interval");check(repeats(row(),true,"display.brightnessAdjust")==accelerated&&repeats(row(),false,"display.brightnessAdjust")==normal,"brightness and TDP share the saved acceleration cadence");
for(const char* src:{"oem","controller","keyboard","mouse","future"}){auto r=row();r["inputs"]=json::array({key("no-such-key",src)});load(json::array({r}));check(g_nativeKeyboardShortcuts.empty(),std::string(src)+" unknown input fails closed");}
json report={{"nativeWorkerObservedHoldMs",observedKeyboardHoldMs},{"pass",failures.empty()},{"checks",checks},{"variants",variants.size()},{"failures",failures},{"acceleratedCount",accelerated},{"constantCount",normal},{"physicalInput",false},{"settingsIo","memory"}};std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:2;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'VARIANTS.json'),JSON.stringify(variants));fs.writeFileSync(path.join(out,'SAVED-INPUTS.json'),JSON.stringify(savedExports));fs.writeFileSync(path.join(out,'REPAIR-SAVED-INPUTS.json'),JSON.stringify(repairExports));fs.writeFileSync(path.join(out,'CONTROL-SAVED-INPUTS.json'),JSON.stringify(controlExports));
const generated=path.join(out,'shortcut-full-runtime.cpp');fs.writeFileSync(generated,cpp);
const sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';const exe=path.join(out,'shortcut-full-runtime.exe'),batch=path.join(out,'build.cmd');
fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" '+version+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+['um','shared','ucrt'].map(p=>'/I"'+sdk+'/Include/'+version+'/'+p+'"').join(' ')+' /I"'+root+'/native" /I"'+root+'/deps/json" "'+generated+'" /Fo"'+out+'/shortcut-full-runtime.obj" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'/Lib/'+version+'/um/x64" /LIBPATH:"'+sdk+'/Lib/'+version+'/ucrt/x64"\r\nexit /b %errorlevel%\r\n');
const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'BUILD.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,build.stdout+build.stderr);
const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));console.log((run.stdout||'')+(run.stderr||''));const report={sourceHashes:Object.fromEntries(['native/main.cpp','native/keyboard_shortcut_sequence.h','native/keyboard_shortcut_sequence_runtime.h','tools/controller_shortcut_full_runtime_selftest.mjs'].map(f=>[f,createHash('sha256').update(fs.readFileSync(path.join(root,f))).digest('hex')])),pass:run.status===0,exit:run.status,result:run.stdout,source:sourceFile,sourceSha256:createHash('sha256').update(source).digest('hex'),actualRuleParser:true,allVisibleOutputs:true,actualTriggerStateMachine:true,hardwareIo:false,installedSettingsWritten:false};fs.writeFileSync(path.join(out,'ACCEPTANCE.json'),JSON.stringify(report,null,2));assert.equal(run.status,0,'Production full shortcut runtime regression');
