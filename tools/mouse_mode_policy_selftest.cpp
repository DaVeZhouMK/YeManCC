#define NOMINMAX
#include "mouse_mode_policy.h"
#include "mouse_mode_foreground_rules.h"
#include "mouse_mode_application_rules.h"
#include <iostream>
#include <future>
#include <vector>

// Only OS identity and existing game/rule snapshots are memory leaves. The
// production consumer below is extracted verbatim from main.cpp by the runner.
using HWND=void*;
static ymcc::mousePolicy::Context applicationForeground;
static std::uintptr_t applicationWindow=0;
static unsigned applicationForegroundReads=0;
static HWND GetForegroundWindow(){return reinterpret_cast<HWND>(applicationWindow);}
static bool mouseModeSteamDeckBlocked(){return applicationForeground.steam;}
namespace ymcc::mousePolicy {
 inline Context observeForeground(bool steam,bool inspectIdentity=true){if(steam||!inspectIdentity){Context c;c.steam=steam;return c;}++applicationForegroundReads;auto c=applicationForeground;c.steam=steam;return c;}
 inline std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s;}
}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
enum class NativeGameDiscoveryState{Complete,Pending,Unavailable};
static const char* nativeGameDiscoveryStateName(NativeGameDiscoveryState state){return state==NativeGameDiscoveryState::Complete?"complete":state==NativeGameDiscoveryState::Pending?"pending":"unavailable";}
struct NativeGameValveSnapshot{
 struct {std::uint32_t pid=0;std::uint64_t processCreated=0;std::wstring path;}game;
 NativeGameDiscoveryState discoveryState=NativeGameDiscoveryState::Complete;
 unsigned long long generation=1,rulesEpoch=100;
};
static NativeGameValveSnapshot g_gameValveTarget;
static std::atomic<std::shared_ptr<const NativeGameValveSnapshot>> g_gameValveObservation;
static std::mutex g_gameValveMx,g_gameRulesMx;
static std::atomic<unsigned long long>g_gameRulesEpoch{100};
static std::atomic<int>g_mousePolicyMode{1};
static unsigned mouseRuleWakes=0;
static struct {void wake(){++mouseRuleWakes;}ymcc::mousePolicy::Mode current()const{return g_mousePolicyMode.load()==1?ymcc::mousePolicy::Mode::Auto:g_mousePolicyMode.load()==2?ymcc::mousePolicy::Mode::On:ymcc::mousePolicy::Mode::Off;}} g_mousePolicy;
static std::vector<std::wstring> applicationWhitelist,applicationConfigured;
static bool applicationRulesFail=false;static unsigned applicationRuleReads=0;
static void nativeRefreshGameRulesEpochFromDiskLocked(){if(applicationRulesFail)throw std::runtime_error("rules unavailable");}
static std::vector<std::wstring> sgGameWhitelist(){++applicationRuleReads;if(applicationRulesFail)throw std::runtime_error("rules unavailable");return applicationWhitelist;}
static std::vector<std::wstring> nativeConfiguredGameExes(){++applicationRuleReads;return applicationConfigured;}
struct MouseAutoRulesState {nlohmann::json data={{"blacklist",nlohmann::json::array()},{"whitelist",nlohmann::json::array()}};ymcc::mousePolicy::AutoOverrides overrides;std::string error;};
static std::atomic<std::shared_ptr<const MouseAutoRulesState>> g_mouseAutoRules{std::make_shared<const MouseAutoRulesState>()};
#include "mouse_mode_application_consumer.inc"

using namespace ymcc::mousePolicy;
using json=nlohmann::json;
static std::mutex g_gamepadSettingsMx;
static bool mouseRuleWriteFails=false;
static json persistedMouseRules=json::object();
static std::string W2U(const std::wstring& s){return {s.begin(),s.end()};}
static bool ymSettingsPatchSection(const char* section,const json& patch){if(mouseRuleWriteFails||std::string(section)!="gamepad")return false;persistedMouseRules=patch;return true;}
#include "mouse_auto_rules_consumer.inc"
static unsigned cases=0;
static void need(bool v,const char* reason){if(!v)throw std::runtime_error(reason);++cases;}
static bool until(const std::function<bool()>& f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;}

static void semiStaticOffCases(){
 std::mutex guard;Context foreground{90,90,900,false,false,false};
 std::atomic<unsigned> applies{0},releases{0};std::atomic<bool> known{true},succeeds{true};std::atomic<int> selectedProvider{0};std::atomic<bool> backendLive{false},injectReadback{false};std::atomic<std::uint64_t> injectedSequence{0};
 Runtime cached;
 auto observe=[&]{std::lock_guard lock(guard);return foreground;};
 auto receipt=[&](bool on){return Json{{"ok",succeeds.load()},{"on",on},{"stateKnown",known.load()},{"controlAllowed",true},{"provider","ymcc"},{"backend",selectedProvider.load()?"gamebar":"joyxoff"}};};
 cached.start({observe,[&](bool on,const std::function<bool()>& valid){++applies;if(on)need(valid&&valid(),"semi-static-enable-still-validates-foreground");backendLive=on;return receipt(on);},[&]{++releases;return receipt(false);},{},[&](const Json& state){if(injectReadback.exchange(false)){backendLive=true;const auto observed=cached.acceptBackendReadback(receipt(true));injectedSequence=observed.at("sequence").get<std::uint64_t>();}return state.value("backend",std::string{})==(selectedProvider.load()?"gamebar":"joyxoff");}},Mode::Off);
 auto move=[&](std::uintptr_t id,bool desktop=false,bool steam=false){{std::lock_guard lock(guard);foreground={id,static_cast<std::uint32_t>(id),id*10,desktop,false,steam};}cached.wake();need(until([&]{return cached.state().value("foregroundPid",0u)==id;}),"semi-static-new-context-receipt-arrives");};
 const auto fresh=cached.set(Mode::Off);need(fresh.value("backendReadbackFresh",false)&&fresh.value("backendObservedAt",0ull)>0,"explicit-off-has-fresh-provider-receipt");
 const auto initial=applies.load();const auto observedAt=fresh.at("backendObservedAt");
 for(unsigned i=0;i<12;++i)move(100+i);
 need(applies.load()==initial,"background-off-contexts-do-not-requery-backends");
 need(!cached.state().value("backendReadbackFresh",true)&&cached.state().at("backendObservedAt")==observedAt,"retained-off-never-claims-fresh-provider-observation");
 cached.set(Mode::Off);need(applies.load()==initial+1&&cached.state().value("backendReadbackFresh",false),"explicit-same-off-forces-fresh-io");
 // Foreground GET is backend evidence, not a mere context label.
 const auto beforeFreshOff=applies.load();const auto freshOff=cached.acceptBackendReadback(receipt(false));move(114);
 need(applies.load()==beforeFreshOff&&cached.state().at("backendObservedAt")==freshOff.at("backendObservedAt"),"fresh-confirmed-off-is-adopted-without-extra-provider-query");
 const auto beforeFreshOn=applies.load();backendLive=true;const auto freshOn=cached.acceptBackendReadback(receipt(true));
 need(freshOn.value("on",false)&&cached.state().value("on",false),"foreground-on-readback-is-admitted");move(115);
 need(applies.load()>beforeFreshOn&&!backendLive.load(),"foreground-on-readback-invalidates-old-off");
 known=false;const auto beforeReadUnknown=applies.load();const auto unknownRead=cached.acceptBackendReadback(receipt(false));
 need(!unknownRead.value("stateKnown",true),"foreground-unknown-remains-unknown");move(116);
 need(applies.load()>beforeReadUnknown&&!cached.state().value("stateKnown",true),"foreground-unknown-cannot-be-overwritten-by-old-known-off");
 known=true;cached.set(Mode::Off);injectReadback=true;const auto beforeInterleaved=applies.load();move(117);
 need(applies.load()==beforeInterleaved+1&&!backendLive.load()&&cached.state().value("backendReadbackFresh",false),"inflight-retained-off-must-not-overwrite-new-readback");
 need(cached.state().at("sequence").get<std::uint64_t>()>injectedSequence.load(),"new-physical-result-orders-after-interleaved-read");
 const auto oldGet=cached.acceptBackendReadback(receipt(false));
 {std::lock_guard lock(guard);foreground.identityKnown=true;foreground.foregroundImage="c:/apps/browser.exe";}
 const auto newOn=cached.set(Mode::On);
 need(oldGet.at("sequence").get<std::uint64_t>()<newOn.at("sequence").get<std::uint64_t>()&&newOn.value("on",false),"late-get-delivery-keeps-lower-sequence-than-new-set");
 const auto stableOnCalls=applies.load();for(unsigned i=0;i<5;++i)cached.acceptBackendReadback(receipt(true));std::this_thread::sleep_for(std::chrono::milliseconds(1100));
 need(applies.load()==stableOnCalls,"stable-on-does-not-become-a-periodic-provider-query");cached.set(Mode::Off);
 selectedProvider=1;const auto beforeProvider=applies.load();move(120);need(applies.load()==beforeProvider+1&&cached.state().value("backend",std::string{})=="gamebar","provider-change-invalidates-retained-off");
 known=false;cached.set(Mode::Off);const auto beforeUnknown=applies.load();move(121);need(applies.load()==beforeUnknown+1&&!cached.state().value("stateKnown",true),"unknown-off-receipt-cannot-be-retained");
 known=true;succeeds=false;cached.set(Mode::Off);const auto beforeFailure=applies.load();move(122);need(applies.load()==beforeFailure+1&&!cached.state().value("ok",true),"failed-off-receipt-cannot-be-retained");
 succeeds=true;cached.set(Mode::Off);cached.set(Mode::Auto);const auto beforeEnable=applies.load();move(130,true);need(applies.load()==beforeEnable+1&&cached.state().value("on",false),"off-cache-never-skips-real-enable");
 const auto beforeDisable=applies.load();move(131);need(applies.load()==beforeDisable+1&&!cached.state().value("on",true),"enabled-provider-must-really-disable-before-retaining-off");
 const auto beforeSteam=releases.load();move(140,true,true);need(releases.load()==beforeSteam+1&&cached.state().value("provider",std::string{})=="steam"&&!cached.state().value("stateKnown",true),"steam-handoff-never-reuses-ymcc-off-receipt");
 const auto stableSteamCalls=releases.load();for(unsigned i=0;i<5;++i){auto read=receipt(false);read["provider"]="steam";read["stateKnown"]=false;read["controlAllowed"]=false;cached.acceptBackendReadback(read);}std::this_thread::sleep_for(std::chrono::milliseconds(1100));need(releases.load()==stableSteamCalls,"stable-steam-does-not-become-a-periodic-release");
 move(141);need(cached.state().value("backendReadbackFresh",false),"return-from-steam-needs-fresh-provider-readback");
 // ON decisions can reuse the provider across different ordinary windows.
 cached.set(Mode::Auto);move(150,true);const auto onApplies=applies.load();const auto onObservedAt=cached.state().at("backendObservedAt");
 for(unsigned i=0;i<6;++i)move(151+i,true);
 need(applies.load()==onApplies&&backendLive.load(),"on-to-on-windows-reuse-confirmed-provider");
 need(!cached.state().value("backendReadbackFresh",true)&&cached.state().at("backendObservedAt")==onObservedAt,"retained-on-preserves-original-observation-time");
 backendLive=false;cached.acceptBackendReadback(receipt(false));move(160,true);
 need(applies.load()==onApplies+1&&backendLive.load(),"explicit-off-readback-invalidates-cached-on");
 {std::lock_guard lock(guard);foreground.window=161;foreground.customRule=-1;}cached.wake();
 need(until([&]{return cached.state().value("scene",std::string{})=="blacklist"&&!backendLive.load();}),"new-blacklist-never-retains-old-on");
 cached.stop();
}
static void applicationScopeCases(){
 auto observe=[&](const std::string& image,std::uint32_t pid=10,std::uint64_t created=100){
  g_gameValveObservation.store(std::make_shared<const NativeGameValveSnapshot>(g_gameValveTarget));
  applicationForeground=Context{900,pid,created,false,false,false};applicationForeground.identityKnown=true;
  applicationForeground.foregroundImage=image;applicationWindow=900;return mouseModeObserveForeground();
 };
 g_gameValveTarget={};g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();
 auto browser=observe("c:\\apps\\chrome.exe");need(decide(Mode::Auto,browser).on,"ordinary-browser-with-no-game-enables");
 const auto skippedReads=applicationRuleReads,skippedIdentityReads=applicationForegroundReads;
 g_mousePolicyMode=0;observe("c:\\apps\\ordinary.exe");
 need(applicationRuleReads==skippedReads&&applicationForegroundReads==skippedIdentityReads,"manual-off-does-not-query-foreground-or-load-categories");
 g_mousePolicyMode=2;const auto manualBrowser=observe("c:\\apps\\ordinary.exe");
 need(applicationForegroundReads==skippedIdentityReads+1&&decide(Mode::On,manualBrowser).on,"manual-on-observes-foreground-and-shares-cached-rules");
 need(applicationRuleReads==skippedReads,"manual-on-reuses-current-shared-rule-cache");
 g_mousePolicyMode=1;
 {
  std::promise<void> locked,release;auto ready=locked.get_future();auto done=release.get_future();
  std::thread owner([&]{std::lock_guard lock(g_gameRulesMx);locked.set_value();done.wait_for(std::chrono::milliseconds(500));});ready.wait();
  const auto begin=std::chrono::steady_clock::now();const auto stable=observe("c:\\apps\\chrome.exe");const auto elapsed=std::chrono::steady_clock::now()-begin;
  release.set_value();owner.join();need(decide(Mode::Auto,stable).on&&elapsed<std::chrono::milliseconds(100),"brief-game-rule-owner-lock-does-not-flicker-confirmed-app-off");
 }
 const auto reads=applicationRuleReads;for(int i=0;i<5;++i)observe("c:\\apps\\chrome.exe");need(applicationRuleReads==reads,"unchanged-rules-epoch-does-not-reparse-lists");
 for(const auto* exe:{"notepad.exe","WINWORD.EXE","ChatGPT.exe","explorer.exe","settings.exe","unknown-editor.exe"})need(decide(Mode::Auto,observe(std::string("c:\\apps\\")+exe)).on,"ordinary-game-blacklist-or-unknown-app-keeps-mouse");
 for(const auto& profile:ymcc::gameApplications::emulatorProfiles())for(const auto& exe:profile.executableNames){
  const auto file=profile.executablePrefix?exe+"-test.exe":exe;const auto c=observe(std::string("c:\\emu\\")+file);
  need(c.emulator&&!decide(Mode::Auto,c).on,"shared-emulator-profile-blocked-before-game-threshold");
 }
 for(const auto& rule:ymcc::gameApplications::gameLibraries()){
  const std::string name(rule.begin(),rule.end());const auto c=observe("c:\\apps\\"+name+".exe");
  need(c.gameLibrary&&!decide(Mode::Auto,c).on,"shared-library-blacklist-is-mouse-blacklist");
 }
 auto platform=observe("c:\\Steam\\steam.exe");need(!decide(Mode::On,platform).on,"library-exclusion-shared-by-auto-and-on");
 need(!decide(Mode::On,observe("c:\\Steam\\steam.exe")).on,"manual-on-inherits-platform-blacklist");
 need(!decide(Mode::On,observe("c:\\emu\\rpcs3.exe")).on,"manual-on-inherits-emulator-blacklist");

 need(decide(Mode::Auto,observe("c:\\apps\\steam-game.exe")).on,"platform-rules-do-not-block-similar-game-or-app-names");
 g_gameValveTarget.game={20,200,L"c:\\games\\title.exe"};
 need(decide(Mode::Auto,observe("c:\\apps\\chrome.exe")).on,"background-game-does-not-disable-normal-app");
 auto game=observe("c:\\games\\title.exe",20,200);need(game.game&&!decide(Mode::Auto,game).on,"same-top-game-instance-foreground-disables");
 need(!observe("c:\\apps\\replacement.exe",20,201).game,"reused-pid-does-not-match-old-game-birth");
 need(!observe("c:\\apps\\replacement.exe",20,200).game,"same-pid-and-birth-with-wrong-path-is-not-game-proof");
 applicationWhitelist={L"renamed-emu",L"custom-game*"};applicationConfigured={L"configured-game"};++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();
 for(const auto* exe:{"renamed-emu.exe","custom-game-two.exe","configured-game.exe"})need(!decide(Mode::Auto,observe(std::string("c:\\apps\\")+exe)).on,"shared-white-and-configured-games-block-auto-mouse");
 applicationWhitelist.clear();applicationConfigured.clear();++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();need(decide(Mode::Auto,observe("c:\\apps\\renamed-emu.exe")).on,"removed-whitelist-rule-is-not-a-permanent-mouse-exclusion");
 auto emulator=observe("c:\\emu\\rpcs3.exe");auto steam=observe("c:\\Steam\\steam.exe");auto ordinary=observe("c:\\apps\\chrome.exe");
 for(const auto state:{NativeGameDiscoveryState::Pending,NativeGameDiscoveryState::Unavailable}){
  g_gameValveTarget.discoveryState=state;
  const auto pendingBrowser=observe("c:\\apps\\chrome.exe");
  need(!pendingBrowser.classificationKnown&&decide(Mode::Auto,pendingBrowser).on,"ordinary-browser-pending-detection-enables");
  need(decide(Mode::Auto,observe("c:\\apps\\unknown.exe")).on,"unlisted-readable-app-defaults-to-allow");
  need(!decide(Mode::Auto,observe("c:\\games\\title.exe",20,200)).on,"positive-game-identity-remains-blocked-during-refresh");
  need(decide(Mode::Auto,observe("c:\\games\\title.exe",20,201)).on,"pending-game-snapshot-cannot-block-reused-pid");
  need(!decide(Mode::Auto,observe("c:\\Steam\\steam.exe")).on,"platform-block-does-not-wait-for-game-discovery");
  need(!decide(Mode::Auto,observe("c:\\emu\\rpcs3.exe")).on,"emulator-block-does-not-wait-for-game-discovery");
 }
 // Missing shared observations also leave unlisted apps usable.
 observe("c:\\apps\\chrome.exe");g_gameValveObservation.store(nullptr);
 need(decide(Mode::Auto,mouseModeObserveForeground()).on,"missing-shared-game-snapshot-does-not-block-browser");
 // Dedicated mouse overrides retain priority, even while game detection is pending.
 const auto savedOverrides=g_mouseAutoRules.load();auto overrides=std::make_shared<MouseAutoRulesState>();
 overrides->overrides.blacklist={L"chrome.exe"};overrides->overrides.whitelist={L"title.exe",L"rpcs3.exe",L"steam.exe"};g_mouseAutoRules.store(overrides);
 need(!decide(Mode::Auto,observe("c:\\apps\\chrome.exe")).on,"dedicated-browser-blacklist-blocks-pending-foreground");
 for(const auto& c:{observe("c:\\games\\title.exe",20,200),observe("c:\\emu\\rpcs3.exe"),observe("c:\\Steam\\steam.exe")})need(decide(Mode::Auto,c).on,"dedicated-whitelist-still-overrides-system-exclusion");
 g_mouseAutoRules.store(savedOverrides);
 g_gameValveTarget.discoveryState=NativeGameDiscoveryState::Complete;
 // A busy new rule epoch must preserve both default allow and prior positive exclusions.
 applicationWhitelist={L"retained-game"};++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();observe("c:\\apps\\chrome.exe");
 {
  std::promise<void> locked,release;auto ready=locked.get_future();auto done=release.get_future();++g_gameRulesEpoch;
  std::thread owner([&]{std::lock_guard lock(g_gameRulesMx);locked.set_value();done.wait_for(std::chrono::milliseconds(500));});ready.wait();
  const auto browserWhileBusy=observe("c:\\apps\\chrome.exe");const auto ruleWhileBusy=observe("c:\\games\\retained-game.exe");const auto gameWhileBusy=observe("c:\\games\\title.exe",20,200);
  release.set_value();owner.join();
  need(decide(Mode::Auto,browserWhileBusy).on,"new-rules-epoch-contention-does-not-block-browser");
  need(!decide(Mode::Auto,ruleWhileBusy).on,"busy-shared-rules-retain-last-positive-exclusion");
  need(!decide(Mode::Auto,gameWhileBusy).on,"busy-shared-rules-retain-exact-game-identity");
 }
 applicationWhitelist.clear();g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();observe("c:\\apps\\chrome.exe");
 ++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();applicationRulesFail=true;const auto beforeFaultReads=applicationRuleReads;
 need(decide(Mode::Auto,observe("c:\\apps\\unknown.exe")).on,"unavailable-game-rules-do-not-block-unlisted-app");
 need(!decide(Mode::Auto,observe("c:\\Steam\\steam.exe")).on,"platform-remains-blocked-during-rule-fault");
 need(!decide(Mode::Auto,observe("c:\\games\\title.exe",20,200)).on,"exact-game-remains-blocked-during-rule-fault");
 for(unsigned i=0;i<30;++i)observe("c:\\apps\\unknown.exe");
 need(applicationRuleReads==beforeFaultReads+1,"failed-shared-rule-epoch-is-attempted-once");applicationRulesFail=false;
 observe("c:\\apps\\unknown.exe");need(applicationRuleReads==beforeFaultReads+1,"rule-source-recovery-alone-does-not-trigger-mouse-polling");
 applicationWhitelist={L"recovered-game"};++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();
 need(!decide(Mode::Auto,observe("c:\\games\\recovered-game.exe")).on,"new-shared-rule-epoch-recovers-failed-mouse-cache");
 applicationWhitelist.clear();++g_gameRulesEpoch;g_gameValveTarget.rulesEpoch=g_gameRulesEpoch.load();observe("c:\\apps\\unknown.exe");
 auto unreadable=observe("c:\\apps\\chrome.exe");unreadable.identityKnown=false;
 need(!decide(Mode::Auto,unreadable).on,"unreadable-foreground-identity-is-still-unavailable");
 unreadable.identityKnown=true;unreadable.foregroundImage.clear();need(!decide(Mode::Auto,unreadable).on,"missing-executable-cannot-bypass-mouse-lists");
 applicationWindow=901;need(!decide(Mode::Auto,mouseModeObserveForeground()).on,"foreground-replacement-during-classification-discards-enable");
 // Run the real sustained policy over the expanded contexts, including a late
 // application enable that becomes a game before the provider returns.
 Runtime runtime;Context current=ordinary;std::mutex mx;bool active=false,lateGame=false;
 auto receipt=[&](bool on){return Json{{"ok",true},{"on",on},{"stateKnown",true},{"controlAllowed",true},{"provider","ymcc"}};};
 runtime.start({[&]{std::lock_guard lock(mx);return current;},[&](bool on,const std::function<bool()>&valid){if(on&&valid&&!valid())return receipt(false);std::lock_guard lock(mx);active=on;if(on&&lateGame){current=game;lateGame=false;}return receipt(on);},[&]{std::lock_guard lock(mx);active=false;return receipt(false);},{}},Mode::Off);
 auto move=[&](Context c){{std::lock_guard lock(mx);current=c;}runtime.wake();};auto live=[&]{std::lock_guard lock(mx);return active;};
 need(runtime.set(Mode::Auto).value("on",false),"runtime-expanded-app-starts-mouse");
 for(const auto& c:{game,emulator,steam}){move(c);need(until([&]{return !live();}),"runtime-game-emulator-library-stops");move(ordinary);need(until(live),"runtime-return-to-normal-app-restores");}
 auto own=ordinary;own.ymcc=true;move(own);need(until([&]{return !live();}),"runtime-ymcc-still-pauses");move(ordinary);need(until(live),"runtime-ymcc-minimize-to-app-restores");
 runtime.set(Mode::Off);{std::lock_guard lock(mx);current=ordinary;lateGame=true;}runtime.set(Mode::Auto);need(until([&]{return !live()&&runtime.state().value("scene",std::string{})=="game";}),"late-ordinary-enable-withdrawn-on-game-focus");
 runtime.set(Mode::Off);runtime.stop();
}

namespace requestRegression {
 static std::atomic<int> g_mousePolicyMode{2};static std::mutex g_gamepadSettingsMx,gateMx;static std::condition_variable gateCv;
 static Runtime g_mousePolicy;static bool savePaused=false,saveEntered=false,releaseSave=false;static std::string persisted="on";
 static bool mouseModeSteamDeckBlocked(){return false;}static json mouseModeSteamDeckState(bool){return {{"ok",false}};}
 static void summonSave(){persisted=g_mousePolicyMode.load()==1?"auto":g_mousePolicyMode.load()==2?"on":"off";if(savePaused&&persisted=="off"){std::unique_lock lock(gateMx);saveEntered=true;gateCv.notify_all();gateCv.wait_for(lock,std::chrono::seconds(2),[]{return releaseSave;});}}
#include "mouse_policy_set_consumer.inc"
 static void run(){
  auto receipt=[](bool on){return Json{{"ok",true},{"on",on},{"stateKnown",true},{"controlAllowed",true},{"provider","ymcc"}};};
  g_mousePolicy.start({[]{return Context{1,1,1,true,false,false};},[&](bool on,const auto&){return receipt(on);},[&]{return receipt(false);},{},{}},Mode::On);g_mousePolicy.set(Mode::On);
  savePaused=true;auto first=std::async(std::launch::async,[]{return mousePolicySet("off");});
  {std::unique_lock lock(gateMx);need(gateCv.wait_for(lock,std::chrono::seconds(2),[]{return saveEntered;}),"mode-save-gate-reached");}
  std::promise<void> started;auto ready=started.get_future();auto second=std::async(std::launch::async,[&]{started.set_value();return mousePolicySet("on");});ready.wait();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));const bool admissionOrdered=g_mousePolicyMode.load()==0;
  {std::lock_guard lock(gateMx);releaseSave=true;}gateCv.notify_all();first.get();second.get();savePaused=false;
  need(admissionOrdered,"concurrent-mode-cannot-change-persisted-selection-outside-admission-lock");
  need(persisted=="on"&&g_mousePolicyMode.load()==2&&g_mousePolicy.current()==Mode::On,"concurrent-save-and-runtime-have-same-final-authority");
  std::atomic<bool> blockOff{true};std::promise<void> entered,release;auto providerReady=entered.get_future(),providerRelease=release.get_future();
  g_mousePolicy.stop();Runtime blocked;
  blocked.start({[]{return Context{1,1,1,true,false,false};},[&](bool on,const auto&){if(!on&&blockOff.exchange(false)){entered.set_value();providerRelease.wait_for(std::chrono::seconds(2));}return receipt(on);},[&]{return receipt(false);},{},{}},Mode::On);
  const auto offTicket=blocked.request(Mode::Off);need(providerReady.wait_for(std::chrono::seconds(2))==std::future_status::ready,"slow-provider-has-off-request");
  const auto onTicket=blocked.request(Mode::On);need(onTicket>offTicket&&blocked.current()==Mode::On,"new-mode-admitted-without-waiting-for-provider");release.set_value();
  const auto newer=blocked.wait(Mode::On,onTicket),older=blocked.wait(Mode::Off,offTicket);
  need(newer.value("on",false)&&older.value("mode",std::string{})=="on"&&older.value("superseded",false),"superseded-provider-wait-returns-current-mode");blocked.stop();
  // Start a fresh policy because Runtime owns its lifetime state.
  Runtime cycling;cycling.start({[]{return Context{1,1,1,true,false,false};},[&](bool on,const auto&){return receipt(on);},[&]{return receipt(false);},{},{}},Mode::Off);
  const auto cycleTicket=cycling.request(Mode::Auto);cycling.refresh();need(cycling.current()==Mode::Auto,"backend-refresh-never-replays-captured-old-mode");cycling.wait(Mode::Auto,cycleTicket);cycling.stop();
 }
}

int main(){try{
 const std::wstring win=L"c:\\windows",explorer=win+L"\\explorer.exe";
 need(desktopSurface(explorer,win,L"DV2ControlHost"),"logged-explorer-popup-remains-desktop");
 for(const auto cls:{L"TopLevelWindowForOverflowXamlIsland",L"NotifyIconOverflowWindow",L"Shell_TrayWnd",L"Windows.UI.Composition.DesktopWindowContentBridge",L"#32768",L"CabinetWClass",L"WorkerW",L"Progman"})need(desktopSurface(explorer,win,cls),"verified-explorer-surface-does-not-depend-on-class-list");
 need(!desktopSurface(L"c:\\games\\explorer.exe",win,L"Shell_TrayWnd"),"same-name-non-system-explorer-must-not-qualify");
 need(!desktopSurface(L"c:\\windows\\explorer.exe.malware",win,L"Progman"),"shell-path-must-match-completely");
 need(!desktopSurface(L"c:\\games\\tmnt.exe",win,L"SDL_app"),"game-window-class-does-not-qualify-as-desktop");
 need(!desktopSurface(explorer,L"",L"Progman"),"missing-windows-root-never-qualifies");
 Context desktop{1,10,100,true,false,false},windowed{2,20,200,false,false,false},full{3,20,200,false,false,false},ymcc{4,30,300,false,true,false},unknown{};
 windowed.game=full.game=true;windowed.identityKnown=full.identityKnown=true;windowed.foregroundImage=full.foregroundImage="c:/games/title.exe";
 need(decide(Mode::Auto,desktop).on,"confirmed-desktop-enable");need(!decide(Mode::Auto,windowed).on,"windowed-game-off");need(!decide(Mode::Auto,full).on,"fullscreen-game-off");need(!decide(Mode::Auto,ymcc).on,"ymcc-pauses-auto");need(!decide(Mode::Auto,unknown).on,"null-foreground-never-desktop");need(decide(Mode::On,full).on,"manual-on-ignores-game");need(!decide(Mode::Off,desktop).on,"manual-off-ignores-desktop");auto sd=desktop;sd.steam=true;need(!decide(Mode::On,sd).on,"sd-admission-before-manual-on");
 applicationScopeCases();
 auto custom=desktop;custom.customRule=-1;
 need(!decide(Mode::Auto,custom).on,"custom-blacklist-overrides-desktop");
 need(!decide(Mode::On,custom).on,"manual-on-inherits-custom-blacklist");
 custom=full;custom.customRule=1;
 need(decide(Mode::Auto,custom).on,"custom-whitelist-overrides-focused-game");
 custom.emulator=true;custom.gameLibrary=true;
 need(decide(Mode::Auto,custom).on,"custom-whitelist-overrides-system-categories");
 need(!decide(Mode::Off,custom).on,"manual-off-precedes-custom-whitelist");
 custom.ymcc=true;need(!decide(Mode::Auto,custom).on,"ymcc-precedes-custom-whitelist");need(!decide(Mode::On,custom).on,"manual-on-ymcc-precedes-custom-whitelist");
 custom.ymcc=false;custom.steam=true;need(!decide(Mode::On,custom).on,"sd-precedes-custom-whitelist");
 custom.steam=false;custom.window=0;need(!decide(Mode::Auto,custom).on,"null-foreground-precedes-custom-whitelist");
 custom=desktop;custom.customRulesKnown=false;need(!decide(Mode::Auto,custom).on,"invalid-custom-rules-fail-closed");need(!decide(Mode::On,custom).on,"manual-on-invalid-mouse-rules-fail-closed");
 AutoOverrides overrides;overrides.blacklist={L"explorer.exe"};overrides.whitelist={L"steam.exe"};
 custom=desktop;custom.identityKnown=true;
 need(applyAutoOverrides(custom,L"c:\\windows\\explorer.exe",overrides).customRule==-1,"custom-list-exact-basename");
 need(applyAutoOverrides(custom,L"c:\\apps\\steam.exe",overrides).customRule==1,"custom-whitelist-exact-basename");
 need(applyAutoOverrides(custom,L"c:\\apps\\steam.exe.other.exe",overrides).customRule==0,"custom-list-no-substring-match");
 const auto initialRules=g_mouseAutoRules.load();
 auto saved=mouseAutoRulesSet({{"blacklist",{"CHROME.EXE","chrome.exe"}}});
 need(saved.value("ok",false)&&saved["blacklist"].size()==1,"custom-list-save-normalizes-deduplicates");
 need(persistedMouseRules["mouseAutoRules"]==g_mouseAutoRules.load()->data,"custom-list-persistence-matches-published-snapshot");
 need(mouseAutoRulesSet({{"whitelist",{"chrome.exe"}}}).value("ok",false)&&g_mouseAutoRules.load()->overrides.blacklist.empty(),"custom-list-move-removes-opposite-entry");
 auto beforeRules=g_mouseAutoRules.load();auto beforeWakes=mouseRuleWakes;
 mouseRuleWriteFails=true;need(!mouseAutoRulesSet({{"blacklist",{"steam.exe"}}}).value("ok",true),"custom-list-write-failure-rejected");
 need(g_mouseAutoRules.load()==beforeRules&&mouseRuleWakes==beforeWakes,"custom-list-write-failure-keeps-current-runtime");mouseRuleWriteFails=false;
 for(const auto* bad:{"*.exe","C:\\Apps\\a.exe","a",".exe","foo.exe ","foo?.exe"})need(!mouseAutoRulesSet({{"blacklist",{bad}}}).value("ok",true),"custom-list-invalid-exe-rejected");
 need(!mouseAutoRulesSet({{"blacklist",json::array()},{"whitelist",json::array()}}).value("ok",true),"custom-list-ambiguous-patch-rejected");
 need(!mouseAutoRulesParse(json{{"blacklist",{"same.exe"}},{"whitelist",{"same.exe"}}},3)->overrides.valid,"custom-list-persisted-overlap-not-silently-accepted");
 need(!mouseAutoRulesParse(json::array(),3)->overrides.valid,"custom-list-invalid-persisted-shape-rejected");
 auto tooMany=json::array();for(int i=0;i<65;++i)tooMany.push_back(std::to_string(i)+".exe");
 need(!mouseAutoRulesSet({{"blacklist",tooMany}}).value("ok",true),"custom-list-size-bound-enforced");
 g_mouseAutoRules.store(initialRules);
 std::mutex io;Context observed=desktop;bool backend=false,failStop=false,moveOnEnable=false,manualMove=false,manualYmccMove=false;unsigned starts=0,stops=0;std::vector<Json> events;
 Runtime runtime;const auto observe=[&]{std::lock_guard lock(io);return observed;};
 auto release=[&]{std::lock_guard lock(io);++stops;if(!failStop)backend=false;return Json{{"ok",!failStop},{"on",backend},{"stateKnown",true},{"provider","ymcc"},{"controlAllowed",true},{"verification","backend-state"}};};
 runtime.start({observe,[&](bool on,const std::function<bool()>& valid){
   if(on&&valid&&!valid())return Json{{"ok",false},{"on",false},{"stateKnown",true}};
   {std::lock_guard lock(io);if(on){++starts;backend=true;if(moveOnEnable||manualMove){observed=full;moveOnEnable=manualMove=false;}if(manualYmccMove){observed=ymcc;manualYmccMove=false;}}else{++stops;if(!failStop)backend=false;}}
   std::lock_guard lock(io);return Json{{"ok",on||!failStop},{"on",backend},{"stateKnown",true},{"provider","ymcc"},{"controlAllowed",true},{"mappingVerified",false},{"verification",on?"process-liveness":"backend-state"}};
 },release,[&](const Json& state){std::lock_guard lock(io);events.push_back(state);}},Mode::Off);
 auto move=[&](Context c){{std::lock_guard lock(io);observed=c;}runtime.wake();};
 auto on=[&]{std::lock_guard lock(io);return backend;};
 need(runtime.set(Mode::Auto).value("on",false),"auto-start-on-desktop");need(!runtime.state().value("mappingVerified",true),"liveness-not-mapping-proof");
 move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="ymcc"&&!on();}),"ymcc-foreground-disables");need(runtime.current()==Mode::Auto,"ymcc-keeps-automatic-choice");
 move(desktop);need(until(on),"minimize-to-desktop-restores");move(windowed);need(until([&]{return !on();}),"windowed-game-disables");move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="ymcc";}),"summon-from-game-pauses");move(full);need(until([&]{return runtime.state().value("scene",std::string{})=="game"&&!on();}),"minimize-to-fullscreen-stays-off");
 need(runtime.set(Mode::On).value("on",false),"manual-on-in-game");move(ymcc);need(until([&]{return runtime.state().value("scene",std::string{})=="ymcc"&&!on();}),"manual-on-pauses-in-ymcc");
 need(runtime.current()==Mode::On,"ymcc-pause-keeps-on-selected");move(full);need(until(on),"manual-on-restores-in-game-after-ymcc-minimize");
 auto blockedApp=full;blockedApp.customRule=-1;move(blockedApp);need(until([&]{return !on();}),"manual-on-stops-in-custom-blacklist");
 move(full);need(until(on),"manual-on-game-restores-after-blacklist");
 auto blockedEmulator=full;blockedEmulator.emulator=true;move(blockedEmulator);need(until([&]{return !on();}),"manual-on-stops-in-emulator");
 move(full);need(until(on),"manual-on-restores-after-emulator");
 auto blockedLibrary=full;blockedLibrary.gameLibrary=true;move(blockedLibrary);need(until([&]{return !on();}),"manual-on-stops-in-platform");
 move(full);need(until(on),"manual-on-restores-after-platform");
 need(!runtime.set(Mode::Off).value("on",true),"manual-off-confirmed");move(desktop);std::this_thread::sleep_for(std::chrono::milliseconds(220));need(!on(),"off-kept-on-desktop");
 // Reproduce the late-enable race inside actual policy consumer IO.
 {std::lock_guard lock(io);moveOnEnable=true;observed=desktop;}
 runtime.set(Mode::Auto);need(until([&]{return runtime.state().value("scene",std::string{})=="game"&&!on();}),"late-desktop-enable-withdrawn-in-game");
 {std::lock_guard lock(io);observed=desktop;manualMove=true;}
 runtime.set(Mode::On);need(until(on),"manual-on-survives-game-foreground-race");
 runtime.set(Mode::Off);{std::lock_guard lock(io);observed=desktop;manualYmccMove=true;}
 runtime.set(Mode::On);need(until([&]{return !on()&&runtime.state().value("scene",std::string{})=="ymcc";}),"manual-late-enable-withdrawn-on-ymcc-focus");
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
 semiStaticOffCases();requestRegression::run();
 std::cout<<Json{{"status","PASS"},{"cases",cases},{"consumer","production mouse_mode_policy::Runtime"},{"hardware","NOT_TOUCHED"}}.dump();return 0;
}catch(const std::exception& e){std::cout<<Json{{"status","FAIL"},{"error",e.what()},{"cases",cases}}.dump();return 1;}}
