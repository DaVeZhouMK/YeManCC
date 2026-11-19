// Exact native Carrier frame admission on isolated hidden HWNDs, without WebView.
#include "wire-carrier-under-test.h"
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>
using namespace ymcc::desktophud;

namespace {
struct WindowClassCleanup {
 const WNDCLASSW& cls;bool active=true;
 ~WindowClassCleanup(){if(active)UnregisterClassW(cls.lpszClassName,cls.hInstance);}
};
struct HapticCleanup {
 Carrier& carrier;
 ~HapticCleanup(){carrier.config.haptic={};}
};
struct AppearanceCase {
 std::string name;Json frame;bool accepted=true;int exception=0;
 bool rich=false,reentry=false;unsigned oldRevision=0;
};

// Extend this consumer in place: one main, one original wire-image run.
Json ownedAppearanceChecks(const WNDCLASSW& cls,Json& checks){
 Json failures=Json::array(),results=Json::array();unsigned windows=0,destroyed=0,rescue=0;
 const auto check=[&](const std::string& name,bool good){
  const auto label="owned appearance: "+name;checks.push_back(label);if(!good)failures.push_back(label);
 };
 const std::string nonce="hud-appearance-owned-0001";
 Json appearance={{"layout","capsule"},{"scale",1.25},{"gap",12},{"fields",{{"cpuLoad",true},{"gpuLoad",false}}},
  {"crosshair","ring-dot"},{"visibility","game-focused"},{"dragLocked",true},{"dragRevision",9ULL},{"dragX",.25},{"dragY",.75},
  {"underlay","custom"},{"underlayImage","data:image/png;base64,"+std::string(44000,'A')}};
 Json base={{"hudId",1},{"nonce",nonce},{"seq",1ULL},{"expiresAt",wallNow()+6000},{"show",true},
  {"data",{{"source","ymcc-uniform"}}},{"policyGame",{{"pid",123},{"processCreated","12345"}}},
  {"preferences",{{"ymcc",appearance},{"ymccTemplate","B"},{"ymccTemplateRevision",4ULL}}}};
 std::vector<AppearanceCase> cases;cases.push_back({"valid-rich",base,true,0,true});
 const auto add=[&](std::string name,Json frame,bool accepted,int exception=0){cases.push_back({std::move(name),std::move(frame),accepted,exception});};
 auto p=base;p["data"]["gameLogo"]=std::string(44000,'B');add("double-image-over64KiB",p,false);
 p=base;p["nonce"]="not-the-owned-nonce";add("wrong-wire-nonce",p,false);
 p=base;p["hudId"]=2;add("wrong-wire-id",p,false);
 p=base;p["data"]["source"]="fixture";add("fixture-source-denied",p,false);
 p=base;p.erase("data");add("data-missing",p,false);
 p=base;p.erase("preferences");add("preferences-missing",p,true);
 p=base;p["preferences"].erase("ymcc");add("appearance-missing",p,true);
 p=base;p["preferences"]["ymcc"]=Json::object();add("appearance-empty",p,true);
 for(const auto& [name,value]:std::vector<std::pair<std::string,Json>>{{"null",nullptr},{"array",Json::array()},{"number",7},{"string","bad"},{"boolean",true}}){
  p=base;p["preferences"]=value;add("preferences-"+name,p,false,306);
  p=base;p["preferences"]["ymcc"]=value;add("appearance-"+name,p,false,306);
 }
 p=base;p["preferences"]["ymcc"]["dragLocked"]="bad";add("invalid-drag-type",p,false,302);
 p=base;p["preferences"]["ymccTemplate"]=7;cases.push_back({"stale-template-skips-name-read",p,true,0,false,false,10});
 cases.push_back({"haptic-reentry-keeps-owned-snapshot",base,true,0,true,true});
 cases.push_back({"expired-frame",base,false});cases.push_back({"excessive-expiry",base,false});
 for(auto c:cases){
  Carrier carrier;auto s=std::make_shared<Carrier::State>();s->owner=&carrier;s->id=1;s->nonce=nonce;s->global=true;s->ready=true;s->templateRevision=c.oldRevision;
  s->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,cls.lpszClassName,L"",WS_POPUP|WS_DISABLED,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,s.get());
  if(!s->hwnd)throw std::runtime_error("owned appearance hidden HWND unavailable");++windows;const HWND held=s->hwnd;carrier.current=s;
  c.frame["expiresAt"]=c.name=="expired-frame"?wallNow():wallNow()+(c.name=="excessive-expiry"?100000:6000);
  const auto input=c.frame.dump();bool reentered=false;HapticCleanup hapticCleanup{carrier};
  if(c.reentry){
   check(c.name+": contact armed",s->drag.begin(1,true,1,1,0,0,1));
   carrier.config.haptic=[&](bool contact,bool pulse){if(!contact&&!pulse){
    reentered=true;c.frame["preferences"]["ymcc"]["layout"]="bar";c.frame["preferences"]["ymcc"]["crosshair"]="none";
    c.frame["preferences"]["ymccTemplate"]="A";c.frame["preferences"]["ymccTemplateRevision"]=5ULL;
   }};
  }
  bool accepted=false;int errorId=0;std::string error;
  try{accepted=carrier.frame(1,nonce,c.frame);}catch(const Json::exception&e){errorId=e.id;error=e.what();}catch(const std::exception&e){errorId=-1;error=e.what();}
  carrier.config.haptic={};
  check(c.name+": actual frame terminal",accepted==c.accepted&&errorId==c.exception);
  check(c.name+": no unpainted test window shown",!IsWindowVisible(held));
  check(c.name+": input ownership",c.reentry?reentered:c.frame.dump()==input);
  if(c.rich&&accepted){
   check(c.name+": copied appearance crosshair",s->crosshair);
   check(c.name+": copied layout retained",s->layoutKey==Json({{"crosshair",true},{"layout","capsule"},{"scale",1.25},{"gap",12},{"fields",{{"cpuLoad",true},{"gpuLoad",false}}}}).dump());
   check(c.name+": drag snapshot",s->drag.position.locked&&s->drag.position.revision==9&&s->drag.position.placed&&s->drag.position.x==.25&&s->drag.position.y==.75);
   check(c.name+": template snapshot",s->activeTemplate=="B"&&s->templateRevision==4);
   check(c.name+": visibility policy snapshot",s->visibility==ymcc::hudvisibility::Mode::GameFocused&&s->policyGamePid==123);
  }
  if(c.name=="stale-template-skips-name-read")check(c.name+": original template retained",s->activeTemplate=="A"&&s->templateRevision==10);
  Json state={{"name",c.name},{"accepted",accepted},{"exceptionId",errorId},{"exception",error},{"seq",s->seq},{"armed",s->armed},{"crosshair",s->crosshair},
   {"template",s->activeTemplate},{"templateRevision",s->templateRevision},{"layoutKey",s->layoutKey},{"visibility",static_cast<int>(s->visibility)},
   {"policyGamePid",s->policyGamePid},{"drag",{{"locked",s->drag.position.locked},{"revision",s->drag.position.revision},{"placed",s->drag.position.placed},{"x",s->drag.position.x},{"y",s->drag.position.y}}},{"reentered",reentered}};
  if(c.reentry&&accepted){
   // Reuse this Carrier with the mutated caller payload: no reference may survive a frame.
   c.frame["seq"]=2ULL;c.frame["expiresAt"]=wallNow()+6000;
   auto& next=c.frame["preferences"]["ymcc"];next["visibility"]="always";next["dragLocked"]=false;next["dragRevision"]=10ULL;next["dragX"]=.75;next["dragY"]=.25;
   bool nextAccepted=false;int nextError=0;
   try{nextAccepted=carrier.frame(1,nonce,c.frame);}catch(const Json::exception&e){nextError=e.id;}catch(const std::exception&){nextError=-1;}
   check(c.name+": next frame admitted",nextAccepted&&nextError==0&&s->seq==2);
   check(c.name+": next appearance replaces prior snapshot",!s->crosshair&&s->layoutKey==Json({{"crosshair",false},{"layout","bar"},{"scale",1.25},{"gap",12},{"fields",{{"cpuLoad",true},{"gpuLoad",false}}}}).dump());
   check(c.name+": next preferences replace prior snapshot",s->activeTemplate=="A"&&s->templateRevision==5&&s->visibility==ymcc::hudvisibility::Mode::Always&&s->policyGamePid==0);
   check(c.name+": next drag replaces prior snapshot",!s->drag.position.locked&&s->drag.position.revision==10&&s->drag.position.placed&&s->drag.position.x==.75&&s->drag.position.y==.25);
   check(c.name+": next frame stays hidden",!IsWindowVisible(held));
   state["nextFrame"]={{"accepted",nextAccepted},{"exceptionId",nextError},{"seq",s->seq},{"layoutKey",s->layoutKey}};
  }
  const bool closed=carrier.close(1,nonce);check(c.name+": actual carrier close",closed&&!carrier.current&&!IsWindow(held));
  if(IsWindow(held)){++rescue;DestroyWindow(held);}if(!IsWindow(held))++destroyed;
  state["closed"]=closed;results.push_back(std::move(state));
 }
 check("all private HWNDs destroyed",windows==destroyed&&rescue==0);
 return {{"ok",failures.empty()},{"failures",failures},{"cases",results},{"createdWindows",windows},{"destroyedWindows",destroyed},{"rescueCount",rescue}};
}
}
int main(int argc,char**argv){try{
 if(argc!=3)return 2;const std::string root=argv[1];Json checks=Json::array();
 const auto check=[&](const char* name,bool good){if(!good)throw std::runtime_error(name);checks.push_back(name);};
 WNDCLASSW cls{};cls.lpfnWndProc=Carrier::proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_WireFrameIsolatedTest";if(!RegisterClassW(&cls))return 3;
 WindowClassCleanup classCleanup{cls};
 Carrier carrier;const auto read=[&](const char* name){std::ifstream in(root+"/"+name);Json frame;in>>frame;return frame;};
 const auto accept=[&](Json frame){
  auto state=std::make_shared<Carrier::State>();state->owner=&carrier;state->id=frame["hudId"].get<int>();state->nonce=frame["nonce"].get<std::string>();state->global=true;state->ready=true;
  state->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,cls.lpszClassName,L"",WS_POPUP|WS_DISABLED,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,state.get());if(!state->hwnd)throw std::runtime_error("isolated window unavailable");carrier.current=state;
  const bool accepted=carrier.frame(state->id,state->nonce,frame);check("admission never shows unpainted test window",!IsWindowVisible(state->hwnd));carrier.close(state->id,state->nonce);return accepted;
 };
 auto before=read("BEFORE-FRAME.json"),after=read("AFTER-FRAME.json"),custom=read("CUSTOM-FRAME.json"),none=read("NONE-FRAME.json");
 const auto now=wallNow();for(auto* frame:{&before,&after,&custom,&none})(*frame)["expiresAt"]=now+6000;
 check("original native body exceeds 64KiB",before.dump().size()>65536);check("actual native Carrier rejects original double-image body",!accept(before));
 check("projected native body fits unchanged 64KiB limit",after.dump().size()<=65536);check("actual native Carrier admits active game logo",accept(after));
 check("actual native Carrier admits original custom image after switching back",accept(custom));check("actual native Carrier admits built-in/no-image body",accept(none));
 auto expired=after;expired["expiresAt"]=wallNow();check("size optimization cannot bypass original expired-frame gate",!accept(expired));
 auto wrongNonce=after;wrongNonce["nonce"]="different-frame-owner";
 // Bind the original identity but change only the wire token.
 {auto s=std::make_shared<Carrier::State>();s->owner=&carrier;s->id=1;s->nonce=after["nonce"];s->ready=true;s->global=true;s->hwnd=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,s.get());carrier.current=s;check("size optimization cannot bypass original nonce gate",!carrier.frame(1,s->nonce,wrongNonce));carrier.close(1,s->nonce);}
 auto fixture=after;fixture["data"]["source"]="fixture";check("size optimization cannot admit fixture telemetry",!accept(fixture));
 const auto originalChecks=checks.size();const auto ownedAppearance=ownedAppearanceChecks(cls,checks);
 const bool unregistered=UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE;classCleanup.active=!unregistered;
 check("isolated window class unregistered",unregistered);const bool ok=ownedAppearance["ok"].get<bool>();
 const Json result={{"ok",ok},{"passed",checks.size()-ownedAppearance["failures"].size()},{"checks",checks},{"originalWireImageChecks",originalChecks},{"ownedAppearance",ownedAppearance},{"beforeNativeBytes",before.dump().size()},{"afterNativeBytes",after.dump().size()},{"actualCarrierFrameUsed",true},{"actualIsolatedWin32Windows",true},{"actualWebViewOrGame",false}};
 std::ofstream(argv[2])<<result.dump(2);std::cout<<(ok?"NATIVE_WIRE_IMAGE_PASS ":"NATIVE_WIRE_IMAGE_FAIL ")<<checks.size()<<"\n";return ok?0:1;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
