// Production Reader with inert PDH APIs + exact production no-top edge.
// Does not read Windows counters, actual hardware, YMCC settings or games.
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winperf.h>
#include <fstream>
#include <iostream>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
static unsigned opens=0,closes=0,collects=0,active=0,readCalls=0;
static unsigned long long fixtureTick=0;static bool failCollect=false;
static PDH_STATUS WINAPI fakeOpen(LPCWSTR,DWORD_PTR,PDH_HQUERY*out){*out=reinterpret_cast<PDH_HQUERY>(1);++opens;++active;return ERROR_SUCCESS;}
static PDH_STATUS WINAPI fakeAdd(PDH_HQUERY,LPCWSTR name,DWORD_PTR,PDH_HCOUNTER*out){*out=reinterpret_cast<PDH_HCOUNTER>(static_cast<ULONG_PTR>(std::wstring(name).find(L"Written")!=std::wstring::npos?3:2));return ERROR_SUCCESS;}
static PDH_STATUS WINAPI fakeClose(PDH_HQUERY){++closes;--active;return ERROR_SUCCESS;}
static PDH_STATUS WINAPI fakeCollect(PDH_HQUERY){++collects;return failCollect?PDH_NO_DATA:ERROR_SUCCESS;}
static PDH_STATUS WINAPI fakeBase(PDH_HCOUNTER,LONGLONG*out){*out=1000;return ERROR_SUCCESS;}
static PDH_STATUS WINAPI fakeRaw(PDH_HCOUNTER counter,LPDWORD type,PPDH_RAW_COUNTER out){++readCalls;out->CStatus=PDH_CSTATUS_VALID_DATA;out->FirstValue=static_cast<LONGLONG>(fixtureTick*10000+(counter==reinterpret_cast<PDH_HCOUNTER>(3)?123:0));out->SecondValue=static_cast<LONGLONG>(fixtureTick);*type=PERF_COUNTER_BULK_COUNT;return ERROR_SUCCESS;}
#define PdhOpenQueryW fakeOpen
#define PdhAddEnglishCounterW fakeAdd
#define PdhCloseQuery fakeClose
#define PdhCollectQueryData fakeCollect
#define PdhGetCounterTimeBase fakeBase
#define PdhGetRawCounterValue fakeRaw
#include "__ROOT__/native/monitor_filesystem_rates.h"
#undef PdhOpenQueryW
#undef PdhAddEnglishCounterW
#undef PdhCloseQuery
#undef PdhCollectQueryData
#undef PdhGetCounterTimeBase
#undef PdhGetRawCounterValue
using Json=nlohmann::json;
struct Passive {void stop(){}void reset(){}};
namespace before {static void edge(bool top,ymcc::monitorfs::Reader& fileSystemReader){Passive batteryFallback,batteryModel,fpsLimitReader;__BEFORE__}}
namespace after {static void edge(bool top,ymcc::monitorfs::Reader& fileSystemReader){Passive batteryFallback,batteryModel,fpsLimitReader;__AFTER__}}
int main(int argc,char**argv){try{
 if(argc!=2)return 2;Json checks=Json::array();const auto check=[&](const char*name,bool good){if(!good)throw std::runtime_error(name);checks.push_back(name);};
 {
  ymcc::monitorfs::Reader reader;fixtureTick=1000;reader.sample(fixtureTick,10000);fixtureTick=2000;const auto measured=reader.sample(fixtureTick,11000);check("real Reader computes measured rates before stop",measured.readMBps==10&&measured.writeMBps==10&&active==1);
  before::edge(false,reader);check("frozen old no-top edge retains PDH resources during FPS-only demand",active==1);fixtureTick=4000;const auto resumed=reader.sample(fixtureTick,13000);check("frozen old Reader crosses disabled interval using retained baseline",resumed.readMBps==10&&resumed.ts==13000);
 }
 check("old Reader destructor ultimately closes its held query",active==0);
 {
  ymcc::monitorfs::Reader reader;fixtureTick=5000;reader.sample(fixtureTick,14000);fixtureTick=6000;reader.sample(fixtureTick,15000);const auto beforeClose=closes,beforeOpen=opens;after::edge(true,reader);check("any remaining global top consumer retains one query",active==1&&closes==beforeClose&&opens==beforeOpen);
  after::edge(false,reader);check("new no-top edge immediately releases PDH resources",active==0&&closes==beforeClose+1);
  const auto stoppedCollects=collects,stoppedReads=readCalls,stoppedOpens=opens,stoppedCloses=closes;for(unsigned i=0;i<10000;++i)after::edge(false,reader);
  check("FPS-only repeated edges never poll/reopen/close or spin",collects==stoppedCollects&&readCalls==stoppedReads&&opens==stoppedOpens&&closes==stoppedCloses&&active==0);
  fixtureTick=8000;const auto warm=reader.sample(fixtureTick,17000);check("reenabling establishes a fresh baseline instead of publishing paused average",warm.readMBps<0&&warm.writeMBps<0&&warm.ts==0&&active==1);
  fixtureTick=9000;const auto fresh=reader.sample(fixtureTick,18000);check("next real sample publishes measured rates and its genuine timestamp",fresh.readMBps==10&&fresh.writeMBps==10&&fresh.ts==18000);
  failCollect=true;fixtureTick=10000;const auto failed=reader.sample(fixtureTick,19000);check("ordinary provider failure still closes query and invalidates rates",active==0&&failed.ts==0);failCollect=false;
  after::edge(false,reader);fixtureTick=11000;const auto retry=reader.sample(fixtureTick,20000);check("fresh global top demand resets retired provider backoff and warms once",active==1&&retry.ts==0);
 }
 check("all owned query handles released",active==0&&opens==closes);
 std::ofstream(argv[1])<<Json{{"ok",true},{"passed",checks.size()},{"checks",checks},{"opens",opens},{"closes",closes},{"collects",collects},{"rawReads",readCalls},{"realCounterOrHardwareCalls",false},{"productionReaderUsed",true},{"productionDemandEdgeUsed",true}}.dump(2);std::cout<<"FILESYSTEM_DEMAND_PASS "<<checks.size()<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
