// Directed branch of the original UI receipt consumer; transport, held process
// checks, mode read and Native48 capture/current are the existing real code.
static unsigned publicUiSetCalls=0;
static struct PublicUiModeSnapshotPort {
 Json value;std::function<void()> onSnapshot;unsigned reads=0;
 Json passiveSnapshot(const ymcc::steamsession::Session&,const ymcc::sleeppower::SharedPowerV1* power){
  ++reads;if(!power||!ymcc::sleeppower::samePowerRevocation(*power,modePowerValue))throw std::runtime_error("public passive read needs captured power");
  if(onSnapshot)onSnapshot();return value;
 }
} g_steamModeObserver;
#include "main_ui_state_read.inc"
namespace ymcc::steamui {
// The unchanged final fixed client action is a controlled leaf for this read
// test. No actual Shell/Steam action or claim of output-kernel coverage.
inline Json set(::DeckySelectedSessionPort&,const Json&,const std::function<bool()>& owner){
 const bool current=owner&&owner();if(current)++publicUiSetCalls;return {{"ok",current}};
}
#include "ui_show_read.inc"
}
static void publicUiReadOwnerFixture(bool showOnly){
 const auto base=listenerChecks;std::vector<std::string> names;
 auto fresh=[] {ymcc::steamsession::IdentitySnapshot f;f.session={L"P:\\YMCC-identity-fixture",processIdentityPid,0,processIdentityBirth};f.presence=1;f.registeredPid=processIdentityPid;f.processEpoch=1;f.accountEpoch=1;f.revision=1;return f;};
 auto setup=[&]{
  ++processIdentityBirth;processIdentityProbe=dispatchProbe=admissionProbe=true;
  processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
  dispatchRead=0;dispatchOwner=true;dispatchExitOnUpgrade=dispatchSendFails=false;dispatchReplyHook={};
  dispatchReplyValue=R"({"ok":true,"mode":"bigpicture","modeEpoch":"public:1","observedAccount":42,"mutated":false})";
  failureDiagnosticsProbe=false;failurePacket.clear();g_steamSettingsObserver.onRead={};const auto f=fresh();g_steamSettingsObserver.publish(f);g_steamSettingsObserver.value=f.session;
  g_exitRequested=false;g_aiFanMockSession.isolated=false;g_steamModeObserver.value=Json{};g_steamModeObserver.onSnapshot={};g_steamModeObserver.reads=0;publicUiSetCalls=0;
  {std::lock_guard lock(modePowerMutex);modePowerValue={};modePowerValue.generation=17;modePowerValue.publicationRevision=100;modePowerValue.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;}
  ymcc::processInventory::invalidate();
 };
 const auto call=[&](const char* name,const std::function<void()>& body){setup();body();expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"public UI read returns original process HTTP and socket leases");names.emplace_back(name);};
 const auto displayArgs=[&]{return Json{{"pid",uint64_t(processIdentityPid)},{"birth",std::to_string(processIdentityBirth)},{"userInitiated",true}};};
 const auto displayOwner=[&]{ymcc::sleeppower::SharedPowerV1 captured;expect(captureSharedPowerLifecycle(captured),"display captures admitted original Native power");return [captured]{return !g_exitRequested.load()&&!g_aiFanMockSession.isolated&&sharedPowerLifecycleCurrent(captured);};};
 const auto showPreRead=[&]{const auto sent=dispatchSends;auto owner=displayOwner();g_steamSettingsObserver.onRead=[]{g_steamSettingsObserver.onRead={};++modePowerValue.generation;};
  const auto result=ymcc::steamui::showBigPicture(g_steamSettingsObserver,displayArgs(),owner);
  expect(!result.value("ok",true)&&dispatchSends==sent&&publicUiSetCalls==0,"display subread inherits revoked owner before CDP");};
 if(showOnly){call("show-pre-read-revocation",showPreRead);}
 else{
  call("main-nonready",[&]{modePowerValue.phase=2;modePowerValue.flags=ymcc::sleeppower::kKnown;const auto sent=dispatchSends;const auto r=productionUiStateRead({{"force",true}});expect(!r.value("known",true)&&r.value("mode",std::string{})=="unknown"&&r.value("reason",std::string{})=="shared-mode-read-cancelled"&&dispatchSends==sent&&processIdentityOpens==0,"nonready public mode read starts no CDP or process lease");});
  call("main-isolated",[&]{g_aiFanMockSession.isolated=true;const auto sent=dispatchSends;const auto r=productionUiStateRead({{"force",true}});expect(!r.value("known",true)&&dispatchSends==sent&&processIdentityOpens==0,"isolated public mode read starts no CDP");});
  call("main-exit",[&]{g_exitRequested=true;const auto sent=dispatchSends;const auto r=productionUiStateRead({{"force",true}});expect(!r.value("known",true)&&dispatchSends==sent&&processIdentityOpens==0,"exiting public mode read starts no CDP");});
  call("main-normal-force",[&]{const auto sent=dispatchSends;const auto r=productionUiStateRead({{"force",true}});expect(r.value("known",false)&&r.value("mode",std::string{})=="bigpicture"&&dispatchSends==sent+1&&g_steamModeObserver.reads==0,"explicit public refresh retains one verified direct read");});
  call("main-late-power",[&]{dispatchReplyHook=[]{++modePowerValue.generation;};const auto sent=dispatchSends;const auto r=productionUiStateRead({{"force",true}});expect(!r.value("known",true)&&r.value("reason",std::string{})=="shared-mode-read-cancelled"&&dispatchSends==sent+1,"late power change rejects original public read reply");});
  call("main-late-exit",[&]{dispatchReplyHook=[]{g_exitRequested=true;};const auto r=productionUiStateRead({{"force",true}});expect(!r.value("known",true)&&r.value("reason",std::string{})=="shared-mode-read-cancelled","late native exit rejects original public read reply");});
  call("main-publication-only",[&]{dispatchReplyHook=[]{++modePowerValue.publicationRevision;};const auto r=productionUiStateRead({{"force",true}});expect(r.value("known",false)&&r.value("mode",std::string{})=="bigpicture","publication alone does not cancel a healthy public mode read");});
  call("main-passive-shared",[&]{g_steamModeObserver.value={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",processIdentityPid},{"birth",std::to_string(processIdentityBirth)},{"reason",""}};const auto sent=dispatchSends;const auto r=productionUiStateRead(Json::object());expect(r.value("mode",std::string{})=="bigpicture"&&g_steamModeObserver.reads==1&&dispatchSends==sent,"automatic public read consumes shared snapshot without CDP");});
  call("main-passive-revoked",[&]{g_steamModeObserver.value={{"known",true},{"mode","bigpicture"}};g_steamModeObserver.onSnapshot=[]{++modePowerValue.generation;};const auto sent=dispatchSends;const auto r=productionUiStateRead(Json::object());expect(!r.value("known",true)&&r.value("mode",std::string{})=="unknown"&&dispatchSends==sent,"shared cache retired during read is not published as current");});
  call("main-automatic-fallback",[&]{dispatchReplyHook=[]{++modePowerValue.generation;};const auto sent=dispatchSends;const auto r=productionUiStateRead(Json::object());expect(!r.value("known",true)&&g_steamModeObserver.reads==1&&dispatchSends==sent+1,"automatic no-owner fallback inherits the same cancellation scope");});
  call("main-invalid-arguments",[&]{const auto sent=dispatchSends;bool rejected=false;try{(void)productionUiStateRead({{"extra",true}});}catch(const std::runtime_error&){rejected=true;}expect(rejected&&dispatchSends==sent&&g_steamModeObserver.reads==0,"invalid public mode request retains original early rejection");});
  call("show-pre-read-revocation",showPreRead);
  call("show-late-power",[&]{auto owner=displayOwner();dispatchReplyHook=[]{++modePowerValue.generation;};const auto sent=dispatchSends;const auto r=ymcc::steamui::showBigPicture(g_steamSettingsObserver,displayArgs(),owner);expect(!r.value("ok",true)&&dispatchSends==sent+1&&publicUiSetCalls==0,"display rejects late cancelled mode observation without an action");});
  call("show-normal",[&]{auto owner=displayOwner();const auto sent=dispatchSends;const auto r=ymcc::steamui::showBigPicture(g_steamSettingsObserver,displayArgs(),owner);expect(r.value("ok",false)&&dispatchSends==sent+1&&publicUiSetCalls==1,"healthy explicit display retains one read and one original output leaf");});
 }
 dispatchProbe=processIdentityProbe=admissionProbe=false;g_steamSettingsObserver.onRead={};g_steamModeObserver.onSnapshot={};
 std::cout<<"PUBLIC_UI_OWNER_RESULT "<<Json{{"ok",true},{"selection",showOnly?"public-show-owner":"public-read-owner"},{"cases",names},{"checks",listenerChecks-base},{"realSteamOperations",0},{"finalOutputKernelProven",false}}.dump()<<'\n';
}
