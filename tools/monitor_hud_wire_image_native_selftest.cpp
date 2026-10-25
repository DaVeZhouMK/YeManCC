// Exact native Carrier frame admission on isolated hidden HWNDs, without WebView.
#include "wire-carrier-under-test.h"
#include <fstream>
#include <iostream>
using namespace ymcc::desktophud;
int main(int argc,char**argv){try{
 if(argc!=3)return 2;const std::string root=argv[1];Json checks=Json::array();
 const auto check=[&](const char* name,bool good){if(!good)throw std::runtime_error(name);checks.push_back(name);};
 WNDCLASSW cls{};cls.lpfnWndProc=Carrier::proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_WireFrameIsolatedTest";if(!RegisterClassW(&cls))return 3;
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
 UnregisterClassW(cls.lpszClassName,cls.hInstance);const Json result={{"ok",true},{"passed",checks.size()},{"checks",checks},{"beforeNativeBytes",before.dump().size()},{"afterNativeBytes",after.dump().size()},{"actualCarrierFrameUsed",true},{"actualIsolatedWin32Windows",true},{"actualWebViewOrGame",false}};
 std::ofstream(argv[2])<<result.dump(2);std::cout<<"NATIVE_WIRE_IMAGE_PASS "<<checks.size()<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
