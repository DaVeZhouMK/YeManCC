// Same-process microbenchmark of the old per-CREATE lookup and production cache.
// All Win32 process APIs here are real; only this test process is queried.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.hpp>
#include "steam_window_event_budget.h"
static ULONGLONG cpu(){FILETIME b{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return (ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
static unsigned classify(std::wstring path){std::replace(path.begin(),path.end(),L'/',L'\\');std::transform(path.begin(),path.end(),path.begin(),[](wchar_t c){return std::towlower(c);});return path.empty()?0:1;}
static void check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
int wmain(int argc,wchar_t** argv){try{
 check(argc==2,"isolated output required");constexpr unsigned count=2000;const auto pid=GetCurrentProcessId();
 DWORD handlesBefore=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore),"handle count");
 const auto beforeCpu=cpu(),beforeWall=GetTickCount64();
 for(unsigned i=0;i<count;++i){HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);check(h!=nullptr,"old open");wchar_t image[32768];DWORD length=static_cast<DWORD>(std::size(image));const bool read=QueryFullProcessImageNameW(h,0,image,&length)!=FALSE;CloseHandle(h);check(read&&classify(std::wstring(image,length))==1,"old image read");}
 const auto oldCpu=cpu()-beforeCpu,oldWall=GetTickCount64()-beforeWall;
 ymcc::steamsession::WindowProcessCache cache;unsigned images=0;const auto cachedCpu=cpu(),cachedWall=GetTickCount64();
 for(unsigned i=0;i<count;++i)check(cache.classify(pid,GetTickCount64(),[&](auto path){++images;return classify(std::move(path));})==1,"cached identity");
 const auto newCpu=cpu()-cachedCpu,newWall=GetTickCount64()-cachedWall;cache.clear();DWORD handlesAfter=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter),"final handle count");
 check(images==1&&handlesAfter==handlesBefore,"one query and no handle leak");
 nlohmann::json r{{"ok",true},{"syntheticWindowHints",count},{"realProcessQueried","self only"},{"before",{{"imageQueries",count},{"cpuMs",oldCpu/10000.0},{"wallMs",oldWall}}},{"after",{{"imageQueries",images},{"cpuMs",newCpu/10000.0},{"wallMs",newWall}}},{"handleDelta",int(handlesAfter)-int(handlesBefore)},{"realSteamOperations",0},{"wholeAppCpuMeasured",false}};
 std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);std::ofstream f(out/L"real-process-cost.json");f<<r.dump(2);std::cout<<r.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
