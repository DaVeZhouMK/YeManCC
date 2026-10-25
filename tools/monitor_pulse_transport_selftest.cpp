// Compile complete production emit/post helpers with inert WebView targets.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
using json=nlohmann::json;
static bool tracking=false,recordMessages=true;
static std::size_t allocations=0,allocationBytes=0,checks=0,comparisons=0,dumpCalls=0,conversionCalls=0,scopeCalls=0,postCalls=0;
void* operator new(std::size_t n){if(tracking){++allocations;allocationBytes+=n;}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
static void check(bool ok,const char*label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct Message {int id=0;std::wstring text;bool operator==(const Message&)const=default;};
static std::vector<Message> messages;
static std::function<void(int)> postHook;
static std::uint64_t checksum=0;
struct View {
 int id=0;HRESULT result=S_OK;
 HRESULT PostWebMessageAsJson(const wchar_t* text){++postCalls;if(recordMessages)messages.push_back({id,text});else checksum+=std::wcslen(text);if(postHook)postHook(id);return result;}
};
struct Child {std::shared_ptr<View> view;};
struct UiCostIpcScope {explicit UiCostIpcScope(const char*name){++scopeCalls;if(std::string_view(name)!="monitor.hudPulse"){std::cerr<<"unexpected diagnostic scope\n";std::exit(1);}}};
static std::string countedDump(const json&value){++dumpCalls;return value.dump();}
__UTF8__
static unsigned long long currentPowerGeneration(){return 99;}
#define SIDE_GLOBALS \
 static std::shared_ptr<View> g_view; \
 static std::map<int,std::shared_ptr<Child>> g_children; \
 static std::size_t lookupCalls=0; \
 static std::shared_ptr<View> ipc_view_for_child(int childId){++lookupCalls;if(!childId)return g_view;auto i=g_children.find(childId);return i==g_children.end()||!i->second?nullptr:i->second->view;}
namespace before {SIDE_GLOBALS
__BEFORE__
static void emit(json value){ipc_emit("monitor.hudPulse",value);}
}
namespace after {SIDE_GLOBALS
__AFTER__
static void emit(json value){ipc_emit_monitor_hud_pulse(std::move(value));}
}
#undef SIDE_GLOBALS
struct Topology {bool main=false;unsigned children=0;bool deadEntries=false;bool failPost=false;};
static void configure(const Topology&t){
 before::g_view=after::g_view={};before::g_children.clear();after::g_children.clear();postHook={};
 if(t.main){before::g_view=std::make_shared<View>(View{0,t.failPost?E_FAIL:S_OK});after::g_view=std::make_shared<View>(View{0,t.failPost?E_FAIL:S_OK});}
 for(unsigned k=0;k<t.children;++k){const int id=int(k+1)*3;before::g_children[id]=std::make_shared<Child>(Child{std::make_shared<View>(View{id,t.failPost?E_FAIL:S_OK})});after::g_children[id]=std::make_shared<Child>(Child{std::make_shared<View>(View{id,t.failPost?E_FAIL:S_OK})});}
 if(t.deadEntries){before::g_children[1]=after::g_children[1]=nullptr;before::g_children[2]=std::make_shared<Child>();after::g_children[2]=std::make_shared<Child>();}
}
struct Effects {std::vector<Message> messages;bool threw=false;std::size_t dumps=0,conversions=0,scopes=0,posts=0,lookups=0;};
static Effects observe(bool modern,json value){
 messages.clear();dumpCalls=conversionCalls=scopeCalls=postCalls=0;before::lookupCalls=after::lookupCalls=0;
 Effects out;
 try{if(modern)after::emit(std::move(value));else before::emit(std::move(value));}catch(const json::exception&){out.threw=true;}
 out.messages=messages;out.dumps=dumpCalls;out.conversions=conversionCalls;out.scopes=scopeCalls;out.posts=postCalls;out.lookups=modern?after::lookupCalls:before::lookupCalls;
 return out;
}
static std::string payload(std::size_t n){const std::string head="{\"note\":\"",tail="\"}";return n<head.size()+tail.size()?std::string(n,'x'):head+std::string(n-head.size()-tail.size(),'T')+tail;}
static json event(std::size_t bytes=0){
 json s=json::object();s["schema"]=1;s["available"]=true;s["generation"]=7ULL;s["seq"]=19ULL;s["session"]=123ULL;s["relayEpoch"]=2ULL;s["topActive"]=true;s["fpsActive"]=true;s["collectedAt"]=1700000000100LL;s["heartbeatAt"]=1700000000100LL;s["hwinfoOkAt"]=1700000000000LL;s["hwDown"]=false;
 s["top"]=bytes?payload(bytes):"{\"cpu\":30,\"note\":\"\xe4\xb8\xad\xe6\x96\x87 / \xf0\x9f\x90\x88 / \\\" / \\\\ / \\n\"}";
 s["fps"]="{\"fps\":120,\"frameTimeSource\":\"rtss-current-frame\",\"frameTimes\":[8,9]}";
 json e=json::object();e["nonce"]="monitor-pulse-local-lease";e["snapshot"]=std::move(s);return e;
}
static void compare(const json& value,const Topology&t){
 configure(t);auto baseline=observe(false,value);auto current=observe(true,value);
 check(baseline.threw==current.threw,"strict serialization failure behavior matches");
 check(baseline.messages==current.messages,"all renderer IDs, order and UTF-16 message bytes match");
 check(baseline.scopes==current.scopes&&baseline.scopes==1,"cost scope retained for all topology paths");
 check(baseline.posts==current.posts&&baseline.lookups==current.lookups,"recipient lookups and delivery attempts unchanged");
 const auto recipients=t.children+(t.main?1:0);
 if(!baseline.threw){check(baseline.posts==recipients,"live renderer count matches topology");check(baseline.dumps==recipients&&current.dumps==(recipients?1:0),"one serialization instead of one per live renderer");check(baseline.conversions==recipients&&current.conversions==(recipients?1:0),"one UTF8 to UTF16 conversion per broadcast");}
 else check(baseline.posts==0&&current.posts==0&&baseline.conversions==0&&current.conversions==0,"invalid document never delivers partial output");
 for(const auto&m:current.messages){const auto exact=(json{{"data",value.is_object()?value:json::object()},{"event","monitor.hudPulse"}}).dump();const auto wire=U2W(exact);check(wire==m.text,"emitted message matches independently formed canonical envelope");}
 ++comparisons;
}
static json measure(bool modern,const json&value,const Topology&t){
 configure(t);recordMessages=false;
 for(unsigned k=0;k<20;++k){auto owned=value;if(modern)after::emit(std::move(owned));else before::emit(std::move(owned));}
 std::size_t totalAlloc=0,totalBytes=0,totalDumps=0,totalConversions=0,totalPosts=0;
 for(unsigned k=0;k<2500;++k){auto owned=value;allocations=allocationBytes=dumpCalls=conversionCalls=postCalls=0;tracking=true;
  if(modern)after::emit(std::move(owned));else before::emit(std::move(owned));
  tracking=false;totalAlloc+=allocations;totalBytes+=allocationBytes;totalDumps+=dumpCalls;totalConversions+=conversionCalls;totalPosts+=postCalls;
 }
 recordMessages=true;return {{"calls",2500},{"allocations",totalAlloc},{"bytes",totalBytes},{"dumps",totalDumps},{"conversions",totalConversions},{"posts",totalPosts}};
}
int main(int argc,char**argv){
 check(argc==2,"result path provided");
 std::vector<json> inputs={nullptr,true,17,"not-object",json::array({1,2}),json::object(),event(),event(16384),event(24576)};
 auto absent=event();absent["snapshot"]={{"schema",1},{"available",false},{"hwDown",true}};inputs.push_back(absent);
 auto controls=event();controls["nonce"]=std::string("null\0byte",9);inputs.push_back(controls);
 auto invalid=event();invalid["nonce"]=std::string(1,char(0xff));inputs.push_back(invalid);
 auto numbers=event();numbers["values"]=json::array({std::numeric_limits<std::uint64_t>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),-0.0});inputs.push_back(numbers);
 for(const auto&v:inputs)for(bool main:{false,true})for(unsigned children:{0U,1U,2U,5U})for(bool dead:{false,true})for(bool fail:{false,true})compare(v,{main,children,dead,fail});
 // One earlier renderer can close another target before its lookup. The
 // callback remains inert apart from changing a retained child view.
 configure({true,2,false,false});postHook=[](int id){if(id==0)before::g_children[3]->view.reset();};auto a=observe(false,event());
 postHook=[](int id){if(id==0)after::g_children[3]->view.reset();};auto b=observe(true,event());
 check(a.messages==b.messages&&a.posts==2&&b.posts==2,"later closed child is skipped in the same original broadcast order");postHook={};
 // Ensure the caller's source cache did not move into a returned event or get modified.
 auto original=event(),keep=original;configure({true,2,false,false});after::emit(json(original));check(original==keep,"independent publisher/cache data is untouched by owned local event transfer");
 json work=json::array();
 for(std::size_t bytes:{0U,16000U})for(unsigned recipients:{0U,1U,2U,4U}){
  const Topology t{recipients>0,recipients>0?recipients-1:0,false,false};const auto value=event(bytes);
  const auto old=measure(false,value,t),next=measure(true,value,t);
  check(old["posts"]==next["posts"],"measured delivery count unchanged");
  if(recipients){check(next["allocations"]<old["allocations"]&&next["bytes"]<old["bytes"],"actual monitor broadcast allocates less");check(old["dumps"]==2500*recipients&&next["dumps"]==2500,"measured complete envelopes serialize once");}
  else check(next["allocations"]<=old["allocations"]&&next["bytes"]<=old["bytes"]&&old["dumps"]==0&&next["dumps"]==0,"empty broadcast creates no extra work");
  work.push_back({{"payloadTopBytes",bytes},{"renderers",recipients},{"before",old},{"after",next}});
 }
 json result={{"ok",true},{"checks",checks},{"eventComparisons",comparisons},{"work",work},{"scope","Complete production old ipc_emit/ipc_post_json and actual new HUD-only sender; real UTF8 conversion and JSON serializer with inert WebView targets. Output UTF16 bytes/order/recipients/errors checked. Allocation counts exclude input preparation, real COM/renderer work and whole-process CPU timing."}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<"PASS "<<checks<<" checks / "<<comparisons<<" broadcasts\n";
}