#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include "json.hpp"
#include "mouse_mode_foreground.h"
#include "mouse_mode_application_rules.h"
using json=nlohmann::json;
static std::mutex g_gamepadSettingsMx,g_inputHostPersonaMx,g_mouseModeMx;
static std::string g_mouseBackend="joyxoff",g_inputHostBuildPersona,g_inputHostBoundPersona="xbox",requestedPersona="xbox";
static bool requestedEnabled=true,joyRunning=false,joyKnown=true,joyAvailable=true,gamebarOn=false;
static bool joyStartFails=false,joyStopFails=false,gamebarSetFails=false,switchToSdOnStart=false;
static HRESULT gamebarHr=S_OK;
enum class YmccFamily { Unknown,AsusRogAllyX,AsusRogAlly,AsusRogAllyClassic,AsusRogAllyXClassic,AsusGeneric };
static struct {YmccFamily family=YmccFamily::Unknown;} g_machineIdentity;
static int g_asyncMode=0;
static std::atomic<int> g_mousePolicyMode{2};
static unsigned reads=0,writes=0,joyStarts=0,joyStops=0,gamebarStarts=0,gamebarStops=0,saves=0,cases=0;
static unsigned readbackAdmissions=0,joyReadCount=0,joyUnknownAt=0;
static json lastEvent,lastFeedback;
static const std::wstring JOYXOFF_EXE=L"isolated-fixture/Joyxoff.exe";
namespace fspath {static bool exists(const std::wstring&){++reads;return joyAvailable;}}
namespace ymcc::processEfficiency {static void createProcess(){}}
namespace ymcc::mouseProcess {
struct Result {bool ok=false,running=false,known=false;DWORD error=0;const char* reason="fixture";};
static Result observe(const std::wstring&){++reads;++joyReadCount;const bool known=joyKnown&&joyReadCount!=joyUnknownAt;return {known,joyRunning,known,0,"process-observed"};}
template<class Creator>static Result set(const std::wstring&,bool enabled,Creator){
 ++writes;if(enabled)++joyStarts;else ++joyStops;
 if((enabled&&joyStartFails)||(!enabled&&joyStopFails))return {false,joyRunning,true,5,"fixture-process-failure"};
 joyRunning=enabled;
 if(enabled&&switchToSdOnStart){requestedPersona="steamdeck";g_inputHostBoundPersona="steamdeck";}
 return {true,joyRunning,true,0,enabled?"process-started":"process-stopped"};
}
}
static json inputShortcutRuntimeSnapshot(){return {{"persona",requestedPersona},{"virtualEnabled",requestedEnabled}};}
static HRESULT gamebarMouseGet(bool& value){++reads;value=SUCCEEDED(gamebarHr)&&gamebarOn;return gamebarHr;}
static HRESULT gamebarMouseSet(bool enabled,bool& actual){++writes;if(enabled)++gamebarStarts;else ++gamebarStops;if(gamebarSetFails){actual=gamebarOn;return E_FAIL;}gamebarOn=enabled;actual=enabled;if(enabled&&switchToSdOnStart){requestedPersona="steamdeck";g_inputHostBoundPersona="steamdeck";}return S_OK;}
static std::string gamebarMouseError(HRESULT){return "fixture-component-failure";}
static bool g_summonEnabled=true,g_bDoubleMinimize=true,g_startDoubleF7=false,g_tdpShortcut=true,g_fpsShortcut=true,g_killGame=true,g_openKeyboard=true,g_returnDesktop=true,g_mouseToggle=true;
static json savedGamepad=json::object();
static std::wstring summonPath(){return L"private-memory/summon.json";}
static std::string sgReadFile(const std::wstring&){return {};}
static bool ymSettingsExists(){return true;}
static json ymSettingsSection(const char*){return savedGamepad;}
static void summonSave(){++saves;savedGamepad["mouseBackend"]=g_mouseBackend;savedGamepad["mouseMode"]=g_mousePolicyMode.load()==2?"on":g_mousePolicyMode.load()==1?"auto":"off";}
static void gamepadSerialPostEvent(const std::string&,const json& value){lastEvent=value;}
static void ipc_emit(const std::string&,const json& value){lastFeedback=value;}
static unsigned long long currentPowerGeneration(){return 4;}
static std::map<std::string,std::function<json(const json&)>> handlers;
template<class F>static void ipc_on(const std::string& name,F callback){handlers[name]=callback;}
// The lower provider regression keeps policy scheduling as an explicit IO
// boundary. The actual production policy Runtime is exercised independently.
static json mouseModeGetState();
static json mouseModeApplyState(std::optional<bool>,const std::string&,const std::function<bool()>&);
static json mousePolicyGet();
static json mousePolicySet(const std::string& mode){return mouseModeApplyState(mode=="on",std::string{},{});}
static json mousePolicyCycle(){return mouseModeApplyState(std::nullopt,std::string{},{});}
struct PolicyScheduleBoundary {json receipt=json::object();int current(){return 0;}json set(int){return mouseModeGetState();}json refresh(){return mouseModeGetState();}json state(){return receipt;}
json acceptBackendReadback(json observed){++readbackAdmissions;
 for(const auto* key:{"mode","scene","policyReason","requestedOn","pending","sequence","observedAt","foregroundPid","foregroundCreated","foregroundWindow"})if(receipt.contains(key))observed[key]=receipt[key];
 if(receipt.contains("ok")&&!receipt.value("ok",true)){observed["ok"]=false;for(const auto* key:{"error","reason"})if(receipt.contains(key))observed[key]=receipt[key];}return observed;
}};
static PolicyScheduleBoundary g_mousePolicy;
// PRODUCTION_FUNCTIONS
static void need(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);++cases;}
static void reset(){
 g_mousePolicy.receipt=json::object();
 savedGamepad=json::object();g_mousePolicyMode=2;
 g_mouseBackend="joyxoff";g_inputHostBuildPersona.clear();g_inputHostBoundPersona="xbox";requestedPersona="xbox";
 requestedEnabled=true;joyRunning=false;joyKnown=true;joyAvailable=true;gamebarOn=false;g_machineIdentity.family=YmccFamily::Unknown;
 joyStartFails=joyStopFails=gamebarSetFails=switchToSdOnStart=false;gamebarHr=S_OK;
 readbackAdmissions=joyReadCount=joyUnknownAt=0; reads=writes=joyStarts=joyStops=gamebarStarts=gamebarStops=saves=0;lastEvent=lastFeedback=json::object();
}
int main(){try{
 registerMouseHandlers();
 for(const auto family:{YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic}){
  reset();g_machineIdentity.family=family;const auto rejected=mouseModeSetBackend("gamebar");
  need(!rejected.value("ok",true)&&rejected.value("reason",std::string{})=="gamebar_model_unsupported"&&writes==0&&reads==0&&saves==0,"all-four-shared-rog-families-block-before-io");
 }
 reset();g_machineIdentity.family=YmccFamily::AsusGeneric;need(mouseModeSetBackend("gamebar").value("ok",false),"non-rog-asus-not-blanket-blocked");
 reset();g_machineIdentity.family=YmccFamily::AsusRogAlly;joyRunning=true;auto model=mouseModeSetBackend("gamebar");
 need(!model.value("ok",true)&&model.value("reason",std::string{})=="gamebar_model_unsupported","rog-model-selection-must-reject");
 need(reads==0&&writes==0&&saves==0&&joyRunning&&g_mouseBackend=="joyxoff","rog-rejection-preserves-existing-backend-before-io");
 reset();g_machineIdentity.family=YmccFamily::AsusRogAlly;g_mouseBackend="gamebar";model=mouseModeApplyState(true);
 need(!model.value("ok",true)&&model.value("reason",std::string{})=="gamebar_model_unsupported"&&writes==0&&reads==0,"rog-old-saved-microsoft-on-cannot-bypass-guard");
 reset();g_machineIdentity.family=YmccFamily::AsusRogAlly;g_mouseBackend="gamebar";model=mouseModeToggleState();
 need(!model.value("ok",true)&&gamebarStarts==0&&writes==0,"rog-toggle-cannot-enable-microsoft");
 reset();g_machineIdentity.family=YmccFamily::AsusRogAlly;g_mouseBackend="gamebar";gamebarOn=true;model=mouseModeApplyState(false);
 need(model.value("ok",false)&&!gamebarOn&&gamebarStops==1,"rog-explicit-off-still-releases-microsoft");
 reset();g_machineIdentity.family=YmccFamily::AsusRogAlly;model=mouseModeApplyState(true);
 need(model.value("ok",false)&&joyRunning&&joyStarts==1,"rog-joyxoff-remains-available");
 need(!model.value("gamebarAvailable",true)&&!model.value("gamebarModelSupported",true),"rog-readback-advertises-microsoft-model-restriction");

 for(const int mode:{0,1,2}){g_asyncMode=mode;for(const std::string command:{"mouseMode.get","mouseMode.set","mouseMode.toggle","mouseMode.setBackend","mouseMode.releaseJoyxoff"})need(ipc_cmd_async(command),"mouse-io-must-stay-off-ui-in-all-async-modes");}
 for(int target=0;target<3;++target){reset();if(target==0)requestedPersona="steamdeck";if(target==1)g_inputHostBuildPersona="steamdeck";if(target==2)g_inputHostBoundPersona="steamdeck";
  for(const std::string name:{"mouseMode.toggle","mouseMode.setBackend","mouseMode.set"}){
   const auto result=handlers.at(name)(json{{"backend","gamebar"},{"on",true}});
   need(!result.value("ok",true)&&result.value("reason",std::string{})=="steamdeck_managed","steamdeck-entry-must-reject");
   need(reads==0&&writes==0&&saves==0,"steamdeck-rejection-before-backend-io");
  }
  const auto state=handlers.at("mouseMode.get")(json::object());need(state.value("ok",false)&&!state.value("controlAllowed",true)&&!state.value("stateKnown",true),"steamdeck-is-observer-only");
  toggleMouseMode("joyxoff");need(!lastEvent.value("ok",true)&&writes==0,"physical-shortcut-must-reject-steamdeck");
  nativeShortcutFeedbackFromResult("gamepad.mouse-toggle",lastEvent);need(lastFeedback.empty(),"blocked-mouse-shortcut-has-no-large-notice");
 }
 reset();requestedPersona="steamdeck";joyRunning=true;auto released=handlers.at("mouseMode.releaseJoyxoff")(json::object());need(released.value("ok",false)&&released.value("closed",false)&&!joyRunning&&gamebarStarts==0&&joyStarts==0,"sd-lifecycle-release-cannot-enable-any-provider");
 reset();joyRunning=joyStopFails=true;released=mouseModeReleaseJoyxoff();need(!released.value("ok",true)&&!released.value("closed",true)&&joyRunning,"failed-release-never-reports-closed");
 reset();requestedPersona="steamdeck";requestedEnabled=false;g_inputHostBoundPersona.clear();need(mouseModeGetState().value("controlAllowed",false),"disabled-sd-config-is-not-active-target");
 reset();auto state=handlers.at("mouseMode.set")(json{{"on",true}});need(state.value("ok",false)&&state.value("on",false)&&joyStarts==1,"explicit-direct-start");need(state.value("verification",std::string{})=="process-liveness"&&!state.value("mappingVerified",true),"process-liveness-is-not-mapping-proof");
 nativeShortcutFeedbackFromResult("gamepad.mouse-toggle",state);need(lastFeedback.empty(),"mouse-state-uses-hud-hint-not-large-notice");
 state=handlers.at("mouseMode.set")(json{{"on",true}});need(state.value("ok",false)&&joyRunning,"explicit-on-does-not-toggle-off");
 state=handlers.at("mouseMode.set")(json{{"on",false}});need(state.value("ok",false)&&!state.value("on",true)&&!joyRunning,"explicit-off-confirmed");
 reset();joyStartFails=true;state=mouseModeToggleState();need(!state.value("ok",true)&&!state.value("on",true),"start-failure-must-not-report-on");
 reset();joyKnown=false;state=mouseModeToggleState();need(!state.value("ok",true)&&writes==0,"unknown-process-no-write");
 reset();joyUnknownAt=2;state=mouseModeApplyState(false);need(!state.value("ok",true)&&!state.value("stateKnown",true)&&state.value("reason",std::string{})=="mouse_state_not_confirmed","final-unknown-is-returned-without-a-recovering-reread");need(joyReadCount==2&&writes==0,"final-confirmation-failure-does-not-repeat-provider-io");
 reset();gamebarOn=true;gamebarSetFails=true;state=mouseModeApplyState(true);need(!state.value("ok",true)&&joyStarts==0&&gamebarOn,"failed-gamebar-stop-prevents-joy-start");
 reset();g_mouseBackend="gamebar";joyRunning=true;joyStopFails=true;state=mouseModeApplyState(true);need(!state.value("ok",true)&&gamebarStarts==0,"failed-joy-stop-prevents-gamebar-start");
 reset();g_mouseBackend="gamebar";joyRunning=true;state=mouseModeApplyState(true);need(state.value("ok",false)&&!joyRunning&&gamebarOn&&joyStops==1,"confirmed-provider-handoff");
 reset();gamebarHr=E_ACCESSDENIED;state=mouseModeApplyState(true);need(!state.value("ok",true)&&writes==0,"unreadable-provider-no-write");
 reset();gamebarHr=REGDB_E_CLASSNOTREG;state=mouseModeApplyState(true);need(state.value("ok",false)&&joyRunning,"absent-microsoft-component-allows-joy");
 reset();g_mouseBackend="gamebar";gamebarHr=REGDB_E_CLASSNOTREG;state=mouseModeApplyState(true);need(!state.value("ok",true)&&writes==0,"absent-selected-component-fails");
 reset();g_mouseBackend="gamebar";gamebarHr=REGDB_E_CLASSNOTREG;joyRunning=true;state=mouseModeApplyState(false);need(state.value("ok",false)&&!joyRunning,"explicit-off-does-not-require-selected-provider");
 reset();joyRunning=gamebarOn=true;state=mouseModeApplyState(false);need(state.value("ok",false)&&!joyRunning&&!gamebarOn,"explicit-off-settles-both");
 reset();switchToSdOnStart=true;state=mouseModeApplyState(true);need(!state.value("ok",true)&&!joyRunning&&joyStops==1&&state.value("reason",std::string{})=="steamdeck_target_changed","late-sd-switch-withdraws-start");
 reset();switchToSdOnStart=joyStopFails=true;state=mouseModeApplyState(true);need(!state.value("ok",true)&&state.value("on",false)&&state.value("reason",std::string{})=="steamdeck_cleanup_failed","failed-sd-withdrawal-remains-visible");
 reset();state=handlers.at("mouseMode.set")(json{{"on","yes"}});need(!state.value("ok",true)&&writes==0,"invalid-explicit-state-rejected");
 reset();state=mouseModeApplyState(true,std::string{},[]{return false;});need(!state.value("ok",true)&&joyStarts==0,"changed-foreground-prevents-enable");
 reset();int observations=0;state=mouseModeApplyState(true,std::string{},[&]{return ++observations<2;});need(!state.value("ok",true)&&!joyRunning&&joyStops==1,"changed-foreground-withdraws-late-enable");
 reset();g_mousePolicy.receipt={{"ok",false},{"error","owned-stop-failed"},{"mode","auto"},{"scene","ymcc"}};state=mousePolicyGet();need(!state.value("ok",true)&&state.value("error",std::string{})=="owned-stop-failed","refresh-preserves-policy-failure");
 reset();state=mouseModeSetBackend("bad");need(!state.value("ok",true)&&writes==0&&saves==0,"invalid-provider-rejected");
 reset();joyRunning=true;state=mouseModeSetBackend("steam");need(!state.value("ok",true)&&state.value("reason",std::string{})=="steam-desktop-mouse-retired"&&g_mouseBackend=="joyxoff"&&joyRunning&&reads==0&&writes==0&&saves==0,"retired-steam-backend-cannot-touch-working-provider");
 reset();state=handlers.at("summon.set")({{"mouseBackend","steam"}});need(g_mouseBackend=="joyxoff"&&state["mouseBackend"]=="joyxoff"&&writes==0,"legacy-preference-ipc-cannot-select-steam");
 for(const char* mode:{"on","auto","off"}){
  reset();savedGamepad={{"mouseBackend","steam"},{"mouseMode",mode},{"enabled",true},{"bDoubleMinimize",false},{"startDoubleF7",false},{"tdpShortcut",true},{"fpsShortcut",false},{"killGame",true},{"openKeyboard",true},{"returnDesktop",true},{"mouseToggle",true},{"otherUserSetting",71}};
  summonLoad();need(g_mouseBackend=="joyxoff"&&g_mousePolicyMode.load()==0&&saves==1&&savedGamepad["mouseMode"]=="off"&&savedGamepad["otherUserSetting"]==71&&!g_bDoubleMinimize&&!g_fpsShortcut&&writes==0,"retired-steam-preference-migrates-off-without-input-io");
  summonLoad();need(saves==1,"migration-is-idempotent-after-persisted-normalization");
 }
 for(const char* backend:{"joyxoff","gamebar"}){
  reset();savedGamepad={{"mouseBackend",backend},{"mouseMode","auto"},{"enabled",true},{"bDoubleMinimize",true},{"startDoubleF7",false},{"tdpShortcut",true},{"fpsShortcut",true},{"killGame",true},{"openKeyboard",true},{"returnDesktop",true},{"mouseToggle",true}};
  summonLoad();need(g_mouseBackend==backend&&g_mousePolicyMode.load()==1&&saves==0&&writes==0,"valid-ordinary-provider-preference-remains-unchanged");
 }
 reset();joyRunning=true;state=handlers.at("mouseMode.get")(json::object());need(readbackAdmissions==1&&state.value("on",false)&&writes==0,"real-get-wires-fresh-on-through-policy-admission-without-control");
 reset();joyKnown=false;state=handlers.at("mouseMode.get")(json::object());need(readbackAdmissions==1&&!state.value("stateKnown",true)&&writes==0,"real-get-wires-unknown-through-policy-admission");
 std::cout<<json{{"status","PASS"},{"cases",cases},{"consumer","extracted-production-provider-consumers;policy-scheduling-explicit-boundary"}}.dump();return 0;
}catch(const std::exception& error){std::cout<<json{{"status","FAIL"},{"error",error.what()}}.dump();return 1;}}
