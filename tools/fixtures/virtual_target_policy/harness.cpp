// Actual production predicates/start prefix/shortcut consumer, with platform effects stubbed.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "virtual_target_policy.h"
#include "input_shortcut_toggle.h"
using json=nlohmann::json;
#include "model.inc"
static std::atomic<bool> g_externalPriorityVirtualSuspended{false},g_exitRequested{false},g_shortcutRecordingActive{false};
static std::string configured;
static int boundaryCalls=0,refreshCalls=0,closeCalls=0;
static json stored=json::object(),lastReceipt=json::object(),lastLog=json::object();
static std::mutex g_inputShortcutRuntimeMx;
static ymcc::InputShortcutRuntime g_inputShortcutRuntime;
static std::string inputHostConfiguredPersona(){return configured;}
static void appendNativeLifecycleLog(const char*,const json& p){lastLog=p;}
static json ymSettingsSection(const char*){return stored;}
static const std::string& gameInputOwnerSession(){static const std::string owner="offline-test";return owner;}
static bool inputCaptureAssetsPresent(){return true;}
static void inputCaptureRefreshRealStickSettings(bool){++refreshCalls;}
static void closeJoyXoffAsync(const char*){++closeCalls;}
static void ipc_emit(const char*,const json& receipt){lastReceipt=receipt;}
#include "consumers.inc"
static unsigned checks=0,failures=0;
static void check(bool value,const char* label){++checks;if(!value){++failures;std::cout<<"FAIL "<<label<<'\n';}}
static void reset(const MachineIdentity& id,const std::string& persona){g_machineIdentity=id;configured=persona;boundaryCalls=refreshCalls=closeCalls=0;lastLog=lastReceipt=json::object();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;}
int main(){
 struct Row{const char* manufacturer;const char* product;const char* system;bool realDeck;};
 const Row machines[]={
  {"VALVE","JUPITER","",true},{"VALVE","GALILEO","",true},
  {"Valve","unknown","Jupiter",true},{"Valve","unknown","Galileo",true},
  {"VALVE","FREMONT","",false},{"VALVE","UNKNOWN","",false},
  {"MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T91","CLAW 8 AI+ A2VM",false},
  {"ASUSTEK COMPUTER INC.","RC73XA","",false},{"GPD","G1618-05","GPD WIN 5",false},
  {"LENOVO","83E1","LEGION GO",false},{"PC VENDOR","JUPITER","GALILEO",false}};
 const char* personas[]={"disabled","dualshock4","xbox360","steamdeck","dualsense","elite","dualsense-edge"};
 for(const auto& row:machines){
  MachineIdentity id;id.manufacturer=ymccDmiNormalize(row.manufacturer);id.product=ymccDmiNormalize(row.product);id.systemProduct=ymccDmiNormalize(row.system);ymccResolveFamily(id);
  reset(id,"disabled");check(inputHostSteamDeckSelfOutputBlocked()==row.realDeck,"actual machine resolver restriction");
  for(const char* persona:personas){
   reset(id,persona);const bool blocked=row.realDeck&&std::string(persona)=="steamdeck";
   const auto* reason=inputHostMachineTargetRejectionReason(configured);
   check((reason!=nullptr)==blocked,"machine/persona policy matrix");
   if(blocked)check(reason&&std::string(reason)=="physical-steamdeck-self-output-blocked","specific rejection reason");
   for(int bits=0;bits<8;++bits){
    g_externalPriorityVirtualSuspended=(bits&4)!=0;
    json target={{"persona",persona},{"buttonMappingEnabled",(bits&1)!=0},{"gyroEnabled",(bits&2)!=0}};const auto original=target.dump();
    const auto got=projectTarget(target);
    const bool requested=(bits&3)!=0,external=(bits&4)!=0;
    check(got.enabled==(requested&&std::string(persona)!="disabled"&&!blocked&&!external),"actual refresh target admission");
    check(got.gyro==((bits&2)!=0&&!blocked&&!external),"actual refresh gyro gate");
    check(target.dump()==original,"old stored request not rewritten");
   }
   g_externalPriorityVirtualSuspended=false;
   const bool admitted=inputHostStart();check(admitted==!blocked,"actual start prefix rejects direct/bypassed request");
   check(boundaryCalls==(blocked?0:1),"reject before first device boundary");
   if(blocked)check(lastLog.value("reason",std::string{})==(reason?reason:""),"native rejection diagnostic");
   const auto status=policyStatusOnly(false);check(status.is_null()!=blocked,"native runtime policy status");
   if(blocked&&!status.is_null()){check(status.at("state")=="disabled","not reported waiting or active");check(status.at("requestedPersona")=="steamdeck","no DS4 fallback");}
  }
  for(const char* preset:{"steamdeck","dualsense-edge","elite"}){
   reset(id,"disabled");stored={{"outputTarget",{{"persona","disabled"},{"buttonMappingEnabled",false},{"gyroEnabled",false}}},{"gyroMotion",{{"enabled",false}}}};
   const auto before=stored.dump();const bool blocked=row.realDeck&&std::string(preset)=="steamdeck";
   nativeInputShortcutToggle("input.virtualGamepadToggle",preset,nullptr);
   check(lastReceipt.value("ok",false)==!blocked,"actual shortcut consumer policy");
   check(g_inputShortcutRuntime.active()==!blocked,"rejected shortcut does not commit candidate");
   check(refreshCalls==(blocked?0:1),"rejected shortcut cannot refresh output");
   check(closeCalls==(!blocked&&std::string(preset)=="steamdeck"?1:0),"rejected shortcut no environment action");
   check(stored.dump()==before,"shortcut preserves durable settings");
   if(!blocked){nativeInputShortcutToggle("input.virtualGamepadToggle",preset,nullptr);check(lastReceipt.value("ok",false)&&!lastReceipt.value("enabled",true),"allowed preset can turn off");}
  }
  reset(id,"steamdeck");stored={{"outputTarget",{{"persona","steamdeck"},{"buttonMappingEnabled",true},{"gyroEnabled",false}}},{"gyroMotion",{{"enabled",false}}}};
  nativeInputShortcutToggle("input.gyroToggle","custom",nullptr);
  check(lastReceipt.value("ok",false)==!row.realDeck,"gyro shortcut cannot revive forbidden saved target");
  reset(id,"steamdeck");nativeInputShortcutToggle("input.virtualGamepadToggle","steamdeck",nullptr);
  check(lastReceipt.value("ok",false)&&!lastReceipt.value("enabled",true),"forbidden old target can still be turned off");
 }
 std::cout<<"SUMMARY checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
