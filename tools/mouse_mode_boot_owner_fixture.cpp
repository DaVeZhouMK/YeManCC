#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include <memory>
#include <vector>
#include "mouse_mode_policy.h"
static void fixtureProviderRead();
static void fixtureSubmitted(bool);
// ORIGINAL_PORTS
static std::mutex fixtureSettingsMx;
static json fixtureDocument;
static unsigned fixtureSettingsReads=0;
static bool fixtureReadFailure=false;
static const std::wstring YM_SETTINGS_FILE=L"owned-memory/yeman-settings.json";
struct SettingsFileGuard { std::lock_guard<std::mutex> lock{fixtureSettingsMx}; };
static std::string ymSettingsReadRaw(const std::wstring&){++fixtureSettingsReads;if(fixtureReadFailure)throw std::runtime_error("owned settings read failure");return fixtureDocument.dump();}
static bool ymSettingsWriteDocumentUnlocked(const std::string& content,long long*,const json*,json* committed,bool,long long,bool*,bool*,bool){fixtureDocument=json::parse(content);if(committed)*committed=fixtureDocument;savedGamepad=fixtureDocument.value("gamepad",json::object());++saves;return true;}
static ymcc::mousePolicy::Runtime g_mousePolicy;
// ORIGINAL_GUARD
// ORIGINAL_PROVIDER
static void registerBootMouseHandlers(){
// ORIGINAL_HANDLERS
}
static std::string scenario,change;
static std::atomic<bool> armed{false};
static json expected;
static unsigned checks=0;
static void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static void changeOwner(){
 std::lock_guard lock(fixtureSettingsMx);
 if(change=="revision")fixtureDocument["input"]["revision"]=8;
 else if(change=="hash")fixtureDocument["input"]["diagnostics"]["inputLoggingEnabled"]=true;
 else if(change=="mode")fixtureDocument["extensions"]["bootModes"]["revision"]=4;
 else if(change=="result")fixtureDocument["extensions"]["bootModesStartupResult"]={{"schema",1},{"revision",99}};
 else if(change=="claim")fixtureDocument["extensions"]["bootModesClaimWriteId"]="peer-mouse-claim";
 else if(change=="observed")fixtureDocument["input"]["startupObservedBoot"]="new-boot";
 else if(change=="ack")fixtureDocument["input"]["hostAcknowledgedRevision"]=7;
 else if(change=="unrelated")fixtureDocument["extensions"]["peer"]={{"keep",true}};
 else if(change=="read-failure")fixtureReadFailure=true;
}
static void fixtureProviderRead(){if((scenario=="stale-read"||scenario=="retired-idle"||scenario=="retry")&&armed.exchange(false))changeOwner();}
static void fixtureSubmitted(bool enabled){if(enabled&&scenario=="already-submitted"&&armed.exchange(false))changeOwner();}
int main(int argc,char** argv){
 if(argc!=6)return 2;scenario=argv[1];change=argv[2];const auto mode=std::string(argv[3]);
 const std::vector<std::string> scenarios={"stale-dispatch","stale-read","backend-read","already-submitted","current","retired-idle","retry"};
 if(std::find(scenarios.begin(),scenarios.end(),scenario)==scenarios.end())return 2;
 const std::vector<std::string> changes={"revision","hash","mode","result","observed","ack","unrelated","read-failure","none","claim","claim-current"};
 if(std::find(changes.begin(),changes.end(),change)==changes.end())return 2;
 bool joined=false;std::string error;json receipt;unsigned writesBefore=0,savesBefore=0,readsAfter=0;
 try{
  std::ifstream defaults(argv[4]);defaults>>fixtureDocument;
  fixtureDocument["input"]["revision"]=7;fixtureDocument["input"]["startupObservedBoot"]="mouse-test-boot";
  fixtureDocument["input"]["outputTarget"]["persona"]="elite";
  fixtureDocument["extensions"]["bootModes"]={{"schema",1},{"revision",3},{"current",mode},{"boot",mode}};
  fixtureDocument["extensions"]["bootModesAppliedBoot"]="mouse-test-boot";
  if(change=="claim"||change=="claim-current")fixtureDocument["extensions"]["bootModesClaimWriteId"]="original-mouse-claim";
  expected={{"input",fixtureDocument["input"]},{"extensions",fixtureDocument["extensions"]}};
  registerBootMouseHandlers();g_mousePolicyMode=0;
  g_mousePolicy.start({[]{return ymcc::mousePolicy::Context{1,10,100,true,false,false};},
   [](bool on,const std::function<bool()>& valid){return mouseModeApplyState(on,std::string{},valid);},
   []{return mouseModeApplyState(false);},{},[](const json& r){return r.value("backend",std::string{})=="joyxoff";}},ymcc::mousePolicy::Mode::Off);
  need(g_mousePolicy.set(ymcc::mousePolicy::Mode::Off).value("ok",false),"original policy warmup failed");
  writesBefore=writes;savesBefore=saves;armed=true;
  if(scenario=="stale-dispatch")changeOwner();
  if(scenario=="backend-read"){
   scenario="stale-read";receipt=handlers.at("mouseMode.setBackend")({{"backend","gamebar"},{"bootContext",expected}});scenario="backend-read";
  }else receipt=handlers.at("mouseMode.set")({{"on",true},{"bootContext",expected}});
  const bool allowed=change=="ack"||change=="unrelated"||change=="none"||scenario=="current";
  if(scenario=="already-submitted"){
   need(joyStarts==1&&joyStops>=1&&!joyRunning,"already submitted own enable must withdraw before returning");
   need(!receipt.value("ok",true),"expired submitted action must not report applied");
  }else if(allowed){
   need(receipt.value("ok",false)&&receipt.value("stateKnown",false)&&joyRunning,"current owner must apply without false cancellation");
  }else{
   need(!receipt.value("ok",true),"expired boot mouse request must be rejected");
   need(writes==writesBefore,"expired boot mouse request must not issue a provider write");
   need(!receipt.value("backendReadbackFresh",false),"expired unsubmitted request must not claim fresh backend evidence");
   if(scenario=="stale-dispatch"||scenario=="backend-read")need(saves==savesBefore,"expired admission must not persist mouse policy or backend");
   if(scenario=="backend-read")need(g_mouseBackend=="joyxoff","expired backend must preserve original selection");
  }
  if(scenario=="retired-idle"){
   readsAfter=fixtureSettingsReads;const auto submitted=writes;
   for(unsigned i=0;i<4;++i)g_mousePolicy.wake();std::this_thread::sleep_for(std::chrono::milliseconds(1100));
   need(fixtureSettingsReads==readsAfter&&writes==submitted,"retired request must not poll settings or replay provider work");
  }
  if(scenario=="retry"){
   {std::lock_guard lock(fixtureSettingsMx);fixtureReadFailure=false;expected={{"input",fixtureDocument["input"]},{"extensions",fixtureDocument["extensions"]}};}
   receipt=handlers.at("mouseMode.set")({{"on",true},{"bootContext",expected}});
   need(receipt.value("ok",false)&&joyRunning,"fresh explicit current request must recover after retirement");
  }
  g_mousePolicy.stop();joined=true;
 }catch(const std::exception& e){error=e.what();g_mousePolicy.stop();joined=true;}
 const json result={{"status",error.empty()?"PASS":"FAIL"},{"scenario",scenario},{"change",change},{"mode",mode},{"checks",checks},{"receipt",receipt},{"error",error},
  {"providerWrites",writes},{"settingsSaves",saves},{"settingsReads",fixtureSettingsReads},{"workerJoined",joined},{"realProviderOperations",0},{"settingsStorage","memory terminal; original boot guard and actual policy/provider/IPC handlers"}};
 const auto target=std::filesystem::path(argv[5]);const auto text=result.dump(2);const auto file=CreateFileW(target.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return 2;DWORD wrote=0;const bool saved=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)&&wrote==text.size();CloseHandle(file);if(!saved)return 2;
 std::cout<<text;return error.empty()?0:1;
}
