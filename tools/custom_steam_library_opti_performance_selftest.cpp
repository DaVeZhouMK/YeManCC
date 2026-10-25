#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "custom_steam_library_storage_isolation.h"
#include <psapi.h>
#include <map>
static bool measuring=false;
static uint64_t readCalls=0,readBytes=0,copyCalls=0,replaceCalls=0;
static std::map<std::wstring,uint64_t> readPaths;
static BOOL WINAPI measuredRead(HANDLE file,LPVOID bytes,DWORD count,LPDWORD read,LPOVERLAPPED overlapped){
    const auto result=ReadFile(file,bytes,count,read,overlapped);
    if(measuring){++readCalls;if(result&&read)readBytes+=*read;wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(file,path,32768,FILE_NAME_NORMALIZED);if(n&&n<32768)++readPaths[std::wstring(path,n)];}
    return result;
}
static BOOL WINAPI measuredCopy(LPCWSTR a,LPCWSTR b,BOOL fail){if(measuring)++copyCalls;return CopyFileW(a,b,fail);}
static BOOL WINAPI measuredMove(LPCWSTR a,LPCWSTR b,DWORD flags){if(measuring)++replaceCalls;return MoveFileExW(a,b,flags);}
#define ReadFile measuredRead
#define CopyFileW measuredCopy
#define MoveFileExW measuredMove
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#undef MoveFileExW
#undef CopyFileW
#undef ReadFile
#include <iostream>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static double cpuMs(){FILETIME c{},e{},k{},u{};check(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u),"CPU metrics unavailable");auto ticks=[](FILETIME t){ULARGE_INTEGER v{};v.LowPart=t.dwLowDateTime;v.HighPart=t.dwHighDateTime;return v.QuadPart;};return double(ticks(k)+ticks(u))/10000.;}
int wmain(int argc,wchar_t** argv){try{
    check(argc==3,"Pass fresh evidence Build directory and scenario");
    const auto root=fs::absolute(argv[1]).lexically_normal();
    const auto workspace=fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
    const auto prefix=normalizedPathKey(workspace/L"Testing"/L"YMCC"/L"Evidence"/L"Build")+L"\\";
    check(normalizedPathKey(root).starts_with(prefix)&&!fs::exists(root),"Performance fixture escaped fresh core evidence Build directory");
    const std::wstring mode=argv[2];const bool complete=mode==L"complete400";
    const unsigned total=complete?400:40,joined=40;
    g_dataRoot=root/L"data";const auto client=root/L"OptiscalerClient";fs::create_directories(client);
    const auto utf=[](const fs::path& p){return toUtf8(p.wstring());};
    json games=json::array(),items=json::array(),catalog=json::array();
    for(unsigned i=0;i<total;++i){
        const auto dir=root/L"Games"/toWide("Game"+std::to_string(i));const auto exe=dir/L"Game.exe";
        writeHostBytesAtomic(exe,"fixture");
        games.push_back({{"gameDirectory",utf(dir)},{"primaryExecutable",utf(exe)},{"directoryName","Game"+std::to_string(i)},{"status","ready"}});
        const auto cover=g_dataRoot/L"artwork"/toWide("Game"+std::to_string(i))/L"tall.jpg";
        writeHostBytesAtomic(cover,std::string(4096,'x'));
        writeJsonWithLocalBackup(cover.parent_path()/L"manifest.json",{{"exe",utf(exe)},{"artwork",json::array({{{"ok",true},{"type","tall"},{"portableFile",utf(cover)}}})}},false);
        if(i<joined){items.push_back({{"gameDirectory",utf(dir)},{"primaryExecutable",utf(exe)},{"formalName","Game"+std::to_string(i)},{"status","already-in-steam"}});
            if(complete)catalog.push_back({{"Name","User name"},{"InstallPath",utf(dir)},{"ExecutablePath",utf(exe)},{"Platform",8},{"AppId","Manual_"+std::to_string(i)},{"CoverImageUrl","user-cover.jpg"},{"UserField",true}});
        }
    }
    writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",games}},false);
    writeJsonWithLocalBackup(workspacePlanPath(),{{"libraryState",utf(g_dataRoot/L"state"/L"library-scan.json")},{"summary",json::object()},{"items",items}},false);
    writeOptiStartupBytesOnce(client/L"games.json",catalog.dump());
    const auto original=readText(client/L"games.json");
    // Fixture seeding is excluded from CPU/IO deltas. Whole-process peak memory
    // includes the seeded JSON documents and is reported separately as such.
    games=nullptr;items=nullptr;catalog=nullptr;
    IO_COUNTERS before{},after{};GetProcessIoCounters(GetCurrentProcess(),&before);
    const auto cpu=cpuMs();const auto tick=GetTickCount64();measuring=true;
    const auto receipt=runOptiCatalogSupplement([]{return false;},[]{return true;},"performance-fixture",[&]{return client;},[]{return false;});
    measuring=false;const auto elapsed=GetTickCount64()-tick;const auto cpuUsed=cpuMs()-cpu;
    GetProcessIoCounters(GetCurrentProcess(),&after);PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof memory;GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof memory);
    check(receipt.value("status",std::string{})==(complete?"idle":"synced"),"Unexpected real consumer outcome");
    if(complete)check(readText(client/L"games.json")==original,"Unchanged catalog was rewritten");
    uint64_t manifestReads=0;for(const auto& [name,count]:readPaths)if(fs::path(name).filename()==L"manifest.json")manifestReads+=count;
    if(complete)check(manifestReads==0&&copyCalls==0&&readCalls<=4&&replaceCalls==1,"Unchanged pass performed redundant artwork or catalog IO");
    else check(manifestReads<=joined&&copyCalls==joined,"Artwork was rescanned per missing cover");
    json result={{"scenario",utf(fs::path(mode))},{"libraryGames",total},{"joinedGames",joined},{"receipt",receipt},
        {"elapsedMs",elapsed},{"cpuMs",cpuUsed},{"readFileCalls",readCalls},{"readFileBytes",readBytes},{"manifestReads",manifestReads},
        {"copyCalls",copyCalls},{"atomicReplacements",replaceCalls},{"ioReadBytes",after.ReadTransferCount-before.ReadTransferCount},
        {"ioWriteBytes",after.WriteTransferCount-before.WriteTransferCount},{"peakProcessWorkingSetBytesIncludesSeeding",memory.PeakWorkingSetSize},
        {"workingSetBytes",memory.WorkingSetSize},{"privateBytes",memory.PrivateUsage},{"network",false},{"installedApplicationChanged",false}};
    writeJsonWithLocalBackup(root/L"summary.json",result,false);std::cout<<result.dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
