#pragma once
#include <fstream>
#include <iostream>

namespace fixture {
struct View final:ICoreWebView2 {
 Json outputs=Json::array();
 HRESULT PostWebMessageAsJson(LPCWSTR value) override {
  const auto raw=memory::narrowAscii(std::wstring(value));
  outputs.push_back({{"raw",raw},{"decoded",Json::parse(raw)}});
  memory::log("PostWebMessageAsJson",{{"decoded",outputs.back().at("decoded")}});return S_OK;
 }
};
struct Controller final:ICoreWebView2Controller {
 unsigned closes=0;
 HRESULT Close() override {++closes;memory::log("Controller.Close");return S_OK;}
};
struct Ports {
 View view;Controller controller;ICoreWebView2CompositionController composition;
 IDCompositionDevice device;IDCompositionTarget target;IDCompositionVisual visual;
};
struct Args final:ICoreWebView2WebMessageReceivedEventArgs {
 std::wstring source,raw;unsigned sourceCalls=0,rawCalls=0;
 Args(std::string s,std::string r):source(memory::wideAscii(s)),raw(memory::wideAscii(r)){}
 HRESULT get_Source(LPWSTR* out) override {++sourceCalls;memory::log("get_Source");*out=memory::allocate(source);return S_OK;}
 HRESULT get_WebMessageAsJson(LPWSTR* out) override {++rawCalls;memory::log("get_WebMessageAsJson");*out=memory::allocate(raw);return S_OK;}
};
struct Assertion final:std::runtime_error {using std::runtime_error::runtime_error;};
inline void require(bool condition,const char* name){if(!condition)throw Assertion(name);}
inline std::size_t operations(const char* op){std::size_t count=0;for(const auto&row:memory::trace)if(row.at("op")==op)++count;return count;}
inline bool zeroResources(){return memory::windows.empty()&&memory::regions.empty()&&memory::wireAllocations.empty()&&memory::messages.empty();}
}

namespace ymcc::radial {
struct OverlayTestAccess {
 using State=Overlay::State;using StatePtr=std::shared_ptr<State>;
 static StatePtr admitted(Overlay& overlay,fixture::Ports& ports,int id) {
  if(overlay.current)throw std::runtime_error("FIXTURE_CANNOT_REPLACE_ACTIVE_NATIVE_OWNER");
  auto s=std::make_shared<State>();s->owner=&overlay;s->id=id;
  s->nonce="fixture-nonce-"+std::to_string(id);s->url="https://radial.ymcc.invalid/wheel";
  s->deadline=memory::now+3000;s->scale=1.25;
  s->payload={{"wheelId",id},{"nonce",s->nonce},{"feedback",true},{"items",Json::array({{{"id","action-one"}}})}};
  s->view=&ports.view;s->controller=&ports.controller;s->composition=&ports.composition;
  s->device=&ports.device;s->target=&ports.target;s->visual=&ports.visual;
  s->window=memory::window(reinterpret_cast<LONG_PTR>(s.get()));
  s->inputWindow=memory::window(reinterpret_cast<LONG_PTR>(s.get()));
  overlay.current=s;
  memory::log("fixture-post-create-admission",{{"id",id},{"actualCreateExecuted",false}});return s;
 }
 static void receive(Overlay& overlay,const StatePtr& state,ICoreWebView2WebMessageReceivedEventArgs* args){overlay.receive(state,args);}
 static void drain(){std::size_t count=0;while(!memory::messages.empty()){
  if(++count>8)throw std::runtime_error("FIXTURE_QUEUE_LIMIT");const auto m=memory::messages.front();memory::messages.erase(memory::messages.begin());
  memory::log("dispatch-actual-proc",{{"message",m.message},{"window",memory::number(m.window)}});Overlay::proc(m.window,m.message,m.w,m.l);
 }}
 static bool environmentReturned(const Overlay& overlay){return !overlay.environment;}
 static bool currentReturned(const Overlay& overlay){return !overlay.current;}
 static Json snapshot(const StatePtr& s){return {
  {"id",s->id},{"nonce",s->nonce},{"ready",s->ready},{"closed",s->closed},{"ownerNull",s->owner==nullptr},
  {"window",memory::number(s->window)},{"inputWindow",memory::number(s->inputWindow)},
  {"viewNull",!s->view},{"controllerNull",!s->controller},{"compositionNull",!s->composition},
  {"deviceNull",!s->device},{"targetNull",!s->target},{"visualNull",!s->visual},
  {"surface",{{"cancelCalls",s->input.cancelCalls},{"closeCalls",s->input.closeCalls},{"syncCalls",s->input.syncCalls}}}
 };}
};
}

namespace fixture {
using Access=ymcc::radial::OverlayTestAccess;
struct Session {
 ICoreWebView2Environment environment;ymcc::screeninput::Runtime inputRuntime;
 Json callbacks=Json::array(),inputs=Json::array();std::vector<std::shared_ptr<Ports>> ports;
 std::vector<Access::StatePtr> states;ymcc::radial::Overlay overlay;
 Session(){ymcc::radial::Config c;
  c.serve=[](ICoreWebView2*)->bool{throw std::runtime_error("EXCLUDED_CREATE_SERVE_CALLED");};
  c.notify=[this](const std::string&name,const Json&data){callbacks.push_back({{"kind","notify"},{"name",name},{"data",data}});memory::log("config.notify",{{"name",name},{"data",data}});};
  c.ownership=[this](bool value){callbacks.push_back({{"kind","ownership"},{"value",value}});memory::log("config.ownership",{{"value",value}});};
  c.visibility=[this](bool value){callbacks.push_back({{"kind","visibility"},{"value",value}});memory::log("config.visibility",{{"value",value}});};
  c.haptic=[this](bool a,bool b){callbacks.push_back({{"kind","haptic"},{"a",a},{"b",b}});memory::log("config.haptic",{{"a",a},{"b",b}});};
  c.inputRuntime=&inputRuntime;overlay.initialize(&environment,std::move(c));admit(41);
 }
 Access::StatePtr admit(int id){auto p=std::make_shared<Ports>();ports.push_back(p);auto s=Access::admitted(overlay,*p,id);states.push_back(s);return s;}
 Access::StatePtr state()const{return states.back();}
 Json request(const std::string&cmd,int id=7)const{const auto s=state();return {{"id",id},{"cmd",cmd},{"args",{{"wheelId",s->id},{"nonce",s->nonce}}}};}
 void raw(const Access::StatePtr&s,const std::string&source,const std::string&text){Args args(source,text);
  inputs.push_back({{"stateId",s->id},{"source",source},{"raw",text},{"asciiWireCodeUnits",text.size()}});
  Access::receive(overlay,s,&args);inputs.back()["sourceCalls"]=args.sourceCalls;inputs.back()["rawCalls"]=args.rawCalls;
 }
 void send(Json value){raw(state(),state()->url,value.dump());}
 Json outputs(std::size_t index=0)const{return ports.at(index)->view.outputs;}
 bool ack(int id=7)const{for(const auto&item:outputs()){
  const auto&value=item.at("decoded");if(value.value("id",-2)==id&&value.contains("result")&&value.at("result")==true&&!value.contains("error"))return true;
 }return false;}
 bool error(const std::string&text)const{for(const auto&item:outputs())if(item.at("decoded").value("error",std::string{})==text)return true;return false;}
 std::size_t callback(const char*kind)const{std::size_t count=0;for(const auto&c:callbacks)if(c.at("kind")==kind)++count;return count;}
 std::size_t notify(const char*name)const{std::size_t count=0;for(const auto&c:callbacks)if(c.at("kind")=="notify"&&c.at("name")==name)++count;return count;}
 bool visibleTrue()const{for(const auto&c:callbacks)if(c.at("kind")=="visibility"&&c.at("value")==true)return true;return false;}
 Json snapshot()const{Json all=Json::array(),sinks=Json::array();for(const auto&s:states)all.push_back(Access::snapshot(s));
  for(const auto&p:ports)sinks.push_back({{"outputs",p->view.outputs},{"controllerCloseCalls",p->controller.closes}});
  return {{"states",all},{"sinks",sinks},{"active",overlay.active()},{"currentNull",Access::currentReturned(overlay)},
   {"environmentNull",Access::environmentReturned(overlay)},{"resources",memory::resources()},{"inputs",inputs},{"callbacks",callbacks}};
 }
};
inline void scenario(const std::string&id,Session&s){
 if(id=="ready-ack-frame"){
  s.send(s.request("radial.ready"));const auto out=s.outputs();
  require(s.state()->ready&&s.overlay.active(),"ready-must-mark-actual-state");
  require(out.size()==2&&out.at(0).at("decoded").value("event",std::string{})=="radial.frame"&&out.at(0).at("decoded").at("data")==s.state()->payload,"ready-must-deliver-actual-frame");
  const auto&reply=out.at(1).at("decoded");require(reply.at("id")==7&&reply.at("result").at("id")==41&&reply.at("result").at("nonce")==s.state()->nonce,"ready-must-ack-owner");
  require(s.visibleTrue()&&operations("ShowWindow")==1,"ready-must-request-visibility");
 }else if(id=="foreign-origin-denied"){
  s.raw(s.state(),"https://foreign.invalid/wheel",s.request("radial.ready").dump());
  require(s.outputs().empty()&&!s.state()->ready&&s.callbacks.empty(),"foreign-origin-must-not-mutate-or-reply");
  require(s.inputs.at(0).at("sourceCalls")==1&&s.inputs.at(0).at("rawCalls")==0,"foreign-origin-must-stop-before-reading-payload");
 }else if(id=="wrong-nonce-denied"||id=="wrong-wheel-id-denied"){
  auto q=s.request("radial.ready");if(id=="wrong-nonce-denied")q["args"]["nonce"]="wrong-owner";else q["args"]["wheelId"]=99;s.send(q);
  require(s.error("Wheel owner mismatch"),id=="wrong-nonce-denied"?"wrong-nonce-must-return-native-owner-error":"wrong-id-must-return-native-owner-error");
  require(s.outputs().size()==1&&!s.state()->ready&&s.callbacks.empty(),"owner-mismatch-must-not-enter-ready-or-visibility");
 }else if(id=="malformed-json-error"){
  s.raw(s.state(),s.state()->url,"{broken-json");const auto out=s.outputs();
  require(out.size()==1&&out.at(0).at("decoded").contains("error")&&out.at(0).at("decoded").at("error").get<std::string>().find("parse_error")!=std::string::npos,"malformed-json-must-return-actual-parser-error");
  require(!s.state()->ready&&s.callbacks.empty(),"parse-error-must-not-mutate-state");
 }else if(id=="overlong-payload-silent"){
  s.raw(s.state(),s.state()->url,std::string(65537,'x'));
  require(s.outputs().empty()&&!s.state()->ready&&s.callbacks.empty(),"overlong-payload-must-drop-silently");
  require(memory::wireAllocations.empty(),"overlong-payload-must-return-wire-allocation");
 }else if(id=="invalid-region-cleanup"){
  auto q=s.request("radial.regions");q["args"]["polygons"]=Json::array({Json::array({{0,0},{497,0},{0,100}})});s.send(q);
  require(s.error("Wheel point out of bounds"),"invalid-region-must-return-native-validation-error");
  require(operations("CreateRectRgn")==1&&operations("DeleteObject")==1&&operations("SetWindowRgn")==0&&memory::regions.empty(),"invalid-region-must-return-joined-region");
 }else if(id=="valid-region-port-roundtrip"){
  auto q=s.request("radial.regions");q["args"]["polygons"]=Json::array({Json::array({{0,0},{100,0},{0,100}})});s.send(q);
  require(s.ack(),"valid-region-must-ack");require(operations("CombineRgn")==2&&operations("SetWindowRgn")==1&&s.state()->input.syncCalls==1&&memory::regions.size()==1,"valid-region-must-transfer-joined-region-to-window-port");
  bool polygon=false,hole=false,joined=false,cut=false;
  for(const auto&t:memory::trace){const auto op=t.at("op").get<std::string>();
   if(op=="CreatePolygonRgn")polygon=t.at("points")==Json::array({{0,0},{125,0},{0,125}});
   if(op=="CreateEllipticRgn")hole=t.at("rect")==Json::array({227,227,393,393});
   if(op=="CombineRgn"){joined=joined||t.at("mode")==RGN_OR;cut=cut||t.at("mode")==RGN_DIFF;}
  }require(polygon&&hole&&joined&&cut,"valid-region-must-pass-scaled-polygon-and-hole-to-ports");
 }else if(id=="intent-forward-ack"){
  auto q=s.request("radial.intent");const Json intent={{"type","test-only-intent"},{"value",17}};q["args"]["intent"]=intent;s.send(q);
  require(s.ack()&&s.notify("radial.intent")==1,"intent-must-forward-native-notify-and-ack");
  require(s.callbacks.at(0).at("data")==Json{{"wheelId",41},{"nonce",s.state()->nonce},{"intent",intent}},"intent-must-preserve-owner-and-payload");
 }else if(id=="overlong-intent-denied"){
  auto q=s.request("radial.intent");q["args"]["intent"]={{"data",std::string(2049,'i')}};s.send(q);
  require(s.error("Invalid wheel intent")&&s.notify("radial.intent")==0,"overlong-intent-must-not-forward");
 }else if(id=="retired-generation-denied"){
  const auto old=s.state();require(s.overlay.close(old->id,old->nonce,"fixture-retire"),"fixture-retirement-must-close");
  const auto next=s.admit(42);const auto callbacksBefore=s.callbacks.size(),traceBefore=memory::trace.size();
  Json q={{"id",8},{"cmd","radial.ready"},{"args",{{"wheelId",old->id},{"nonce",old->nonce}}}};s.raw(old,old->url,q.dump());
  require(old->closed&&!old->ready&&s.callbacks.size()==callbacksBefore&&memory::trace.size()==traceBefore,"retired-generation-must-not-reenter-ready-or-visibility");
  require(s.inputs.back().at("sourceCalls")==0&&s.inputs.back().at("rawCalls")==0,"retired-generation-must-stop-before-source-read");
  require(!next->closed&&!next->ready&&s.overlay.active()&&s.outputs(1).empty(),"retired-generation-must-preserve-current-owner");
 }else if(id=="close-reply-return"){
  const auto current=s.state();s.send(s.request("radial.close"));
  require(s.ack()&&memory::messages.size()==1&&!current->closed,"close-must-ack-before-queued-return");Access::drain();
  require(current->closed&&current->owner==nullptr&&!s.overlay.active()&&Access::currentReturned(s.overlay),"close-must-release-actual-current-owner");
  const auto snap=Access::snapshot(current);require(snap.at("viewNull")==true&&snap.at("controllerNull")==true&&snap.at("compositionNull")==true&&snap.at("deviceNull")==true&&snap.at("targetNull")==true&&snap.at("visualNull")==true,"close-must-reset-all-owned-native-pointers");
  require(current->input.cancelCalls==1&&current->input.closeCalls==1&&s.ports.at(0)->controller.closes==1&&s.callback("ownership")==1&&s.callback("visibility")==1&&s.callback("haptic")==1,"close-must-call-resource-return-ports-once");
  require(s.notify("radial.closed")==1&&s.callbacks.back().at("data").at("reason")=="window-close"&&zeroResources(),"close-must-deliver-return-receipt-and-free-memory-endpoints");
  const auto size=s.callbacks.size();require(s.overlay.close(current->id,current->nonce)&&s.callbacks.size()==size,"close-must-be-idempotent");
 }else throw std::runtime_error("UNDECLARED_CASE");
 require(memory::wireAllocations.empty(),"receive-must-return-wire-allocations");
}
inline const std::vector<std::string> cases={"ready-ack-frame","foreign-origin-denied","wrong-nonce-denied","wrong-wheel-id-denied","malformed-json-error","overlong-payload-silent","invalid-region-cleanup","valid-region-port-roundtrip","intent-forward-ack","overlong-intent-denied","retired-generation-denied","close-reply-return"};
}

int main(int argc,char**argv){
 Json result={{"schemaVersion",1},{"chain","actual-extracted-native-post-create-consumer"},{"postCreateSeedOnly",true},{"actualCreateExecuted",false},{"realOverlayOrWindowCreated",false},{"cases",Json::array()}};
 bool semanticFailure=false,harnessFailure=false;
 const std::string selected=argc==2?argv[1]:"";
 if(argc>2||(!selected.empty()&&std::find(fixture::cases.begin(),fixture::cases.end(),selected)==fixture::cases.end())){std::cerr<<"INVALID_CASE_SELECTION\n";return 24;}
 for(const auto&id:fixture::cases){if(!selected.empty()&&id!=selected)continue;
  Json row={{"id",id},{"status","PASS"}};
  try{memory::begin();fixture::Session session;
   row["before"]=session.snapshot();
   try{fixture::scenario(id,session);}catch(const fixture::Assertion&e){row["status"]="ASSERTION_FAILED";row["assertion"]=e.what();semanticFailure=true;}
   catch(const std::exception&e){row["status"]="HARNESS_ERROR";row["error"]=e.what();harnessFailure=true;}
   row["beforeCleanup"]=session.snapshot();session.overlay.shutdown();ymcc::radial::OverlayTestAccess::drain();
   row["afterCleanup"]=session.snapshot();row["trace"]=memory::trace;
   if(!fixture::zeroResources()||!ymcc::radial::OverlayTestAccess::environmentReturned(session.overlay)){
    row["cleanupError"]="MEMORY_ENDPOINT_NOT_RETURNED";row["status"]="HARNESS_ERROR";harnessFailure=true;
   }
  }catch(const std::exception&e){row["status"]="HARNESS_ERROR";row["error"]=e.what();row["trace"]=memory::trace;harnessFailure=true;}
  std::cout<<row.at("id").get<std::string>()<<":"<<row.at("status").get<std::string>()<<"\n";result["cases"].push_back(std::move(row));
 }
 const int code=harnessFailure?24:semanticFailure?23:0;result["naturalExit"]=code;result["status"]=harnessFailure?"HARNESS_ERROR":semanticFailure?"ASSERTION_FAILED":"PASS";
 std::ofstream out("native-result.json",std::ios::binary|std::ios::trunc);if(!out){std::cerr<<"RESULT_WRITE_FAILED\n";return 24;}out<<result.dump(2)<<"\n";out.close();if(!out){std::cerr<<"RESULT_FLUSH_FAILED\n";return 24;}return code;
}
