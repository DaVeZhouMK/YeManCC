// Canonical frame bytes and allocation counts; no WebView, hardware, timers or CPU timing.
#include "../deps/json/json.hpp"
#include "../native/monitor_hud_serialization.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
using Json=nlohmann::json;
static bool tracking=false;static size_t allocations=0;
void* operator new(size_t n){if(tracking)++allocations;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
static int beforeDumps=0,afterDumps=0,comparisons=0;
static bool before(const Json& body,std::string& text){if(!body.is_object())return false;++beforeDumps;if(body.dump().size()>65536)return false;++beforeDumps;text=Json{{"event","desktopHud.frame"},{"data",body}}.dump();return true;}
static bool after(const Json& body,std::string& text){if(!body.is_object())return false;++afterDumps;const auto serialized=body.dump();if(serialized.size()>65536)return false;text=ymcc::hudserialization::desktopFrameEvent(serialized);return true;}
static void check(bool ok,const char* why){if(!ok){std::cerr<<why<<"\n";std::exit(1);}}
static void compare(const Json& body){std::string a,b;bool left=false,right=false,failA=false,failB=false;try{left=before(body,a);}catch(const Json::exception&){failA=true;}try{right=after(body,b);}catch(const Json::exception&){failB=true;}check(failA==failB&&left==right&&a==b,"canonical serialization/bounds/error behavior changed");if(left){const auto decoded=Json::parse(b);check(decoded["event"]=="desktopHud.frame"&&decoded["data"]==body,"event envelope changed");}++comparisons;}
int main(int argc,char** argv){check(argc==2,"report path required");Json payload={{"type","desktop-hud"},{"protocol",1},{"hudId",1},{"nonce","cpu-work-test-only-nonce"},{"seq",1},{"expiresAt",1700000006000LL},{"show",true},{"preferences",{{"ymcc",{{"position",1},{"layout","corner"},{"scale",1.4}}}}},{"data",{{"source","ymcc-uniform"},{"stale",false},{"values",{{"fps",60},{"batteryPower",-24},{"clock","23:59"},{"note","CPU °C / 中文 / 🐈 / quotes \" \\ \n"}}},{"coreUsage",Json::array()},{"coreClocks",Json::array()},{"frameTimes",Json::array()}}}};
 for(int i=0;i<128;++i)payload["data"]["coreUsage"].push_back(i%17?Json(i%101):Json(nullptr));for(int i=0;i<120;++i)payload["data"]["frameTimes"].push_back(i%11?Json(i/7.0):Json(nullptr));
 compare(payload);compare(Json::object());compare(nullptr);compare(Json::array());compare(1);compare(true);
 for(int i=0;i<1000;++i){auto value=payload;value["seq"]=i+1;value["show"]=i%2==0;value["data"]["values"]["fps"]=i;value["data"]["values"]["fraction"]=-i/3.0;value["data"]["values"]["missing"]=nullptr;value["data"]["values"]["text"]=std::string(i%40,'x')+"中文";compare(value);}
 for(int n:{0,65520,65521,65522,65523,65524,65525,65526,65527,65528,65529,65530,65531,65532,65533,65534,65535,65536,65537,65538,65539,65540,70000})compare(Json{{"padding",std::string(n,'x')}});
 auto invalid=payload;invalid["data"]["values"]["text"]=std::string(1,char(0xff));compare(invalid);
 auto large=payload;large["data"]["values"]["integer"]=std::numeric_limits<unsigned long long>::max();large["data"]["values"]["finite"]=std::numeric_limits<double>::max();compare(large);
 std::string a,b;beforeDumps=afterDumps=0;allocations=0;tracking=true;bool okA=before(payload,a);tracking=false;const auto oldAlloc=allocations;allocations=0;tracking=true;bool okB=after(payload,b);tracking=false;const auto newAlloc=allocations;
 check(okA&&okB&&a==b,"allocation fixture differs");check(beforeDumps==2&&afterDumps==1&&newAlloc<oldAlloc,"expected duplicate dump/tree-copy work removed");
 Json result={{"ok",true},{"canonicalFrameComparisons",comparisons},{"frameBodyDumpCalls",{{"before",beforeDumps},{"after",afterDumps}}},{"syntheticFrameHeapAllocations",{{"before",oldAlloc},{"after",newAlloc}}},{"timingOrCpuUsageMeasured",false},{"hardwareIo",false},{"scope","same JSON dependency as production: byte-identical event envelope, Unicode/escape/float/integer/null/65536-byte bounds/invalid UTF-8; synthetic fixture allocation counts only"}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<result.dump(2)<<"\n";
}
