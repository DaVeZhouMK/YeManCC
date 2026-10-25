#pragma once
#include <fstream>
#include <iostream>
namespace cases {
struct Assertion:std::runtime_error{using std::runtime_error::runtime_error;};
inline void check(bool ok,const char*name){if(!ok)throw Assertion(name);}
struct View:ICoreWebView2{Json messages=Json::array();HRESULT PostWebMessageAsJson(LPCWSTR raw)override{const auto decoded=Json::parse(memory::narrowAscii(raw));messages.push_back(decoded);memory::log("port.WebView-receipt",decoded);return S_OK;}};
struct Controller:ICoreWebView2Controller{unsigned closes=0;HRESULT Close()override{++closes;memory::log("port.Controller.Close");return S_OK;}};
struct Sink{View view;Controller controller;};
struct Args:ICoreWebView2WebMessageReceivedEventArgs{std::string source,raw;unsigned reads=0;Args(std::string s,std::string r):source(std::move(s)),raw(std::move(r)){}HRESULT get_Source(LPWSTR*out)override{++reads;*out=memory::allocate(memory::wideAscii(source));return S_OK;}HRESULT get_WebMessageAsJson(LPWSTR*out)override{++reads;*out=memory::allocate(memory::wideAscii(raw));return S_OK;}};
}
namespace ymcc::radial {
struct OverlayTestAccess {
 using State=Overlay::State;using StatePtr=std::shared_ptr<State>;
 static StatePtr admitted(Overlay&o,cases::Sink&sink,int id){if(o.current)throw std::runtime_error("SEED_CANNOT_REPLACE_CURRENT_OWNER");auto s=std::make_shared<State>();s->owner=&o;s->id=id;s->nonce="return-nonce-"+std::to_string(id);s->url="https://radial.ymcc.invalid/wheel";s->ready=true;s->neutral=true;s->deadline=10000;s->payload={{"feedback",false}};s->view=&sink.view;s->controller=&sink.controller;s->window=memory::window(reinterpret_cast<LONG_PTR>(s.get()));s->inputWindow=memory::window(reinterpret_cast<LONG_PTR>(s.get()));o.current=s;
  memory::log("fixture-post-create-admission",{{"id",id},{"actualCreateExecuted",false}});o.config.ownership(true);return s;
 }
 static void receive(Overlay&o,const StatePtr&s,ICoreWebView2WebMessageReceivedEventArgs*a){o.receive(s,a);}
 static bool environmentReturned(const Overlay&o){return !o.environment;}
 static Json snapshot(const StatePtr&s){return {{"id",s->id},{"ready",s->ready},{"closed",s->closed},{"ownerNull",s->owner==nullptr},{"window",memory::number(s->window)},{"inputWindow",memory::number(s->inputWindow)},{"viewNull",!s->view},{"controllerNull",!s->controller},{"cancelCalls",s->input.cancelCalls},{"closeCalls",s->input.closeCalls}};}
};
}
namespace cases {
using Access=ymcc::radial::OverlayTestAccess;
inline void queueDrain(){unsigned n=0;while(!mainports::queued.empty()){if(++n>8)throw std::runtime_error("QUEUE_BOUND");auto job=std::move(mainports::queued.front());mainports::queued.erase(mainports::queued.begin());memory::log("port.queue-dispatch");job();}}
struct Session{
 ICoreWebView2Environment environment;std::vector<std::shared_ptr<Sink>> sinks;std::vector<Access::StatePtr> states;
 Session(){memory::begin();mainports::sample={};mainports::connected=true;mainports::parentLaneCalls=mainports::returnDesktopCalls=0;mainports::events=Json::array();mainports::actions=Json::array();mainports::queued.clear();mainports::foreground=true;mainports::customSuppressed=false;mainports::customForeground=false;
  g_radialUiOwnsInput.store(false);g_radialReturning.store(false);g_inputReady.store(true);g_inputReleaseRequired.store(false);g_exitRequested.store(false);g_shortcutRecordingActive.store(false);g_shortcutRecordingMask=0;
  g_gamepadConnectionWasLive=true;g_gamepadAxisNeutralAdmissionRequired=false;g_gamepadReleaseBarrierLogged=false;g_padConnected=true;g_customSteamLibraryInputPhase.store(0);g_customSteamLibraryInputDeadline.store(0);
  g_curW=g_prevW=0;g_curPad=g_prevPad={};g_bClosePending=false;g_bClosePendingSince=g_bCloseReadyAt=0;g_gpHoldTimerOn=false;gpArmed=true;gpHoldStart=0;gpPrevB=false;gpLastBPress=0;
  gpKbArmed=gpKxArmed=gpKyArmed=true;gpKbHoldStart=gpKxHoldStart=gpKyHoldStart=0;s_dpHeldStart=s_dpLastEmit=s_brightnessHeldStart=s_brightnessLastEmit=0;
  g_hwnd=memory::window();g_env=&environment;radialInitialize();
 }
 Access::StatePtr admit(int id=61){auto sink=std::make_shared<Sink>();sinks.push_back(sink);auto s=Access::admitted(g_radialOverlay,*sink,id);states.push_back(s);return s;}
 void close(){const auto s=states.back();check(g_radialOverlay.close(s->id,s->nonce,"fixture-exit"),"actual-close-must-succeed");}
 void evaluate(){gamepadEval();}
 Json snapshot()const{Json all=Json::array();for(const auto&s:states)all.push_back(Access::snapshot(s));Json messages=Json::array();for(const auto&p:sinks)messages.push_back(p->view.messages);
 return {{"mainOwned",g_radialUiOwnsInput.load()},{"mainReturning",g_radialReturning.load()},{"mainHasOwner",radialHasInputOwner()},{"active",g_radialOverlay.active()},{"states",all},{"messages",messages},{"parentLaneCalls",mainports::parentLaneCalls},{"parentActionEndpointCalls",mainports::returnDesktopCalls},{"queuedActions",mainports::queued.size()},{"events",mainports::events},{"actions",mainports::actions},{"previousButtons",g_prevW},{"resources",memory::resources()}};}
 void cleanup(){g_radialOverlay.shutdown();if(!mainports::queued.empty()){memory::log("fixture-cancel-owned-memory-queue",{{"count",mainports::queued.size()}});mainports::queued.clear();}if(g_gpHoldTimerOn){KillTimer(g_hwnd,GP_HOLD_TIMER_ID);g_gpHoldTimerOn=false;}if(g_hwnd)DestroyWindow(g_hwnd);g_hwnd=nullptr;g_env.Reset();}
};
inline void scenario(const std::string&id,Session&s){
 if(id=="ownership-claim-suppresses-parent"){
  s.admit();check(g_radialUiOwnsInput.load()&&radialHasInputOwner(),"actual-main-ownership-callback-must-claim");gamepadEmitUiAction("accept");check(mainports::events.empty(),"wheel-owner-must-block-parent-UI-emission");
  check(!nativeYmccShortcutEmit("os.returnDesktop",0)&&mainports::queued.empty(),"wheel-owner-must-block-native-shortcut-admission");mainports::sample.wButtons=XINPUT_GAMEPAD_A;s.evaluate();check(mainports::parentLaneCalls==0&&g_prevW==XINPUT_GAMEPAD_A,"active-wheel-must-consume-snapshot-before-parent");
 }else if(id=="close-enters-return"){
  s.admit();s.close();check(!g_radialUiOwnsInput.load()&&g_radialReturning.load()&&radialHasInputOwner(),"actual-main-close-callback-must-enter-return");gamepadEmitUiAction("accept");check(mainports::events.size()==1&&mainports::events.at(0).at("event")=="radial.closed","returning-must-block-parent-UI-emission");check(!nativeYmccShortcutEmit("os.returnDesktop",0),"returning-must-block-native-action-admission");
 }else if(id=="held-button-blocks-return"){
  s.admit();s.close();mainports::sample.wButtons=XINPUT_GAMEPAD_A;s.evaluate();check(g_radialReturning.load()&&radialHasInputOwner()&&mainports::parentLaneCalls==0,"held-button-must-keep-main-return-gate");check(g_prevW==XINPUT_GAMEPAD_A,"returning-must-drain-held-edge-into-previous-snapshot");
 }else if(id=="held-trigger-blocks-return"){
  s.admit();s.close();for(int side=0;side<2;++side){mainports::sample={};if(side==0)mainports::sample.bLeftTrigger=30;else mainports::sample.bRightTrigger=255;s.evaluate();check(g_radialReturning.load()&&mainports::parentLaneCalls==0,"held-trigger-must-keep-main-return-gate");}
 }else if(id=="held-axis-blocks-return"){
  s.admit();s.close();for(int axis=0;axis<4;++axis){mainports::sample={};switch(axis){case 0:mainports::sample.sThumbLX=16000;break;case 1:mainports::sample.sThumbLY=-16000;break;case 2:mainports::sample.sThumbRX=32767;break;default:mainports::sample.sThumbRY=-32768;break;}s.evaluate();check(g_radialReturning.load()&&mainports::parentLaneCalls==0,"held-axis-must-keep-main-return-gate");}
 }else if(id=="neutral-releases-without-replay"){
  s.admit();s.close();mainports::sample.wButtons=XINPUT_GAMEPAD_A;s.evaluate();mainports::sample={};s.evaluate();check(!g_radialReturning.load()&&!radialHasInputOwner()&&mainports::parentLaneCalls==0&&g_prevW==0,"neutral-frame-must-release-and-return-before-parent-consumption");
 }else if(id=="next-parent-action-admitted"){
  s.admit();s.close();s.evaluate();check(!radialHasInputOwner(),"neutral-must-complete-return");mainports::sample.wButtons=XINPUT_GAMEPAD_A;s.evaluate();check(mainports::parentLaneCalls==1,"next-press-must-reach-parent-lane-after-return");gamepadEmitUiAction("accept");check(mainports::events.back().at("event")=="gamepad.ui-input","actual-parent-emitter-must-admit-after-neutral");check(nativeYmccShortcutEmit("os.returnDesktop",0),"actual-native-shortcut-must-admit-after-neutral");queueDrain();check(mainports::returnDesktopCalls==1,"admitted-shortcut-must-reach-memory-action-endpoint");
 }else if(id=="reopen-before-neutral-keeps-owner"){
  s.admit();s.close();s.admit(62);s.evaluate();check(g_radialUiOwnsInput.load()&&g_radialReturning.load()&&radialHasInputOwner()&&mainports::parentLaneCalls==0,"new-wheel-must-hold-owner-even-if-previous-return-pending");
  check(nativeYmccShortcutEmit("radial.summon",0),"radial-summon-exception-must-remain-admitted");queueDrain();
 }else if(id=="retired-ready-keeps-current"){
  const auto old=s.admit();s.close();old->ready=false;const auto next=s.admit(62);const auto before=mainports::events.size();Args args(old->url,Json{{"id",7},{"cmd","radial.ready"},{"args",{{"wheelId",old->id},{"nonce",old->nonce}}}}.dump());Access::receive(g_radialOverlay,old,&args);
  check(args.reads==0&&!old->ready&&old->closed&&next->owner==&g_radialOverlay&&g_radialUiOwnsInput.load()&&mainports::events.size()==before,"retired-ready-must-not-change-main-owner-or-new-state");
 }else if(id=="retired-close-keeps-current"){
  const auto old=s.admit();s.close();const auto next=s.admit(62);const auto before=mainports::events.size();check(!g_radialOverlay.close(old->id,old->nonce,"late-close"),"retired-close-must-be-rejected");check(!next->closed&&g_radialUiOwnsInput.load()&&mainports::events.size()==before,"retired-close-must-not-release-current-main-owner");check(!nativeYmccShortcutEmit("os.returnDesktop",0),"retired-owner-must-not-authorize-current-parent-actions");
 }else if(id=="queued-parent-action-current"){
  check(nativeYmccShortcutEmit("os.returnDesktop",0)&&mainports::queued.size()==1,"current-parent-action-must-enter-actual-queued-admission");queueDrain();check(mainports::returnDesktopCalls==1&&mainports::queued.empty(),"current-parent-action-must-consume-on-memory-endpoint");
 }else if(id=="queued-parent-action-after-claim"){
  check(nativeYmccShortcutEmit("os.returnDesktop",0)&&mainports::queued.size()==1,"queued-parent-action-must-exist-before-radial-claim");s.admit();queueDrain();check(mainports::returnDesktopCalls==0&&mainports::queued.empty()&&g_radialUiOwnsInput.load(),"queued-parent-action-must-not-execute-after-radial-claim");
 }else throw std::runtime_error("UNDECLARED_CASE");
}
inline const std::vector<std::string> ids={"ownership-claim-suppresses-parent","close-enters-return","held-button-blocks-return","held-trigger-blocks-return","held-axis-blocks-return","neutral-releases-without-replay","next-parent-action-admitted","reopen-before-neutral-keeps-owner","retired-ready-keeps-current","retired-close-keeps-current","queued-parent-action-current","queued-parent-action-after-claim"};
}
int main(int argc,char**argv){
 const std::string chosen=argc==2?argv[1]:"";if(argc>2||(!chosen.empty()&&std::find(cases::ids.begin(),cases::ids.end(),chosen)==cases::ids.end()))return 24;
 Json result={{"schemaVersion",1},{"cases",Json::array()},{"actualMainReturnAndAdmission",true},{"realHostIntentExecuted",false},{"postCreateSeedOnly",true}};bool failed=false,harness=false;
 for(const auto&id:cases::ids){if(!chosen.empty()&&id!=chosen)continue;Json row={{"id",id},{"status","PASS"}};
  try{cases::Session session;row["before"]=session.snapshot();try{cases::scenario(id,session);}catch(const cases::Assertion&e){row["status"]="ASSERTION_FAILED";row["assertion"]=e.what();failed=true;}catch(const std::exception&e){row["status"]="HARNESS_ERROR";row["error"]=e.what();harness=true;}
   row["beforeCleanup"]=session.snapshot();session.cleanup();row["afterCleanup"]=session.snapshot();row["trace"]=memory::trace;
   if(!memory::windows.empty()||!memory::regions.empty()||!memory::wireAllocations.empty()||!memory::messages.empty()||!mainports::queued.empty()||!ymcc::radial::OverlayTestAccess::environmentReturned(g_radialOverlay)){row["status"]="HARNESS_ERROR";row["cleanupError"]="ENDPOINT_NOT_RETURNED";harness=true;}
  }catch(const std::exception&e){row["status"]="HARNESS_ERROR";row["error"]=e.what();harness=true;}
  std::cout<<id<<":"<<row.at("status").get<std::string>()<<"\n";result["cases"].push_back(row);
 }
 const int code=harness?24:failed?23:0;result["status"]=harness?"HARNESS_ERROR":failed?"ASSERTION_FAILED":"PASS";result["naturalExit"]=code;std::ofstream output("native-result.json",std::ios::binary);if(!output)return 24;output<<result.dump(2)<<"\n";output.close();return output?code:24;
}
