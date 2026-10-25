#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
// Test-only socket adapter: the production HTTPS allowlist still runs first.
// All network reads, HTTP framing and timeouts use the real WinHTTP engine.
static INTERNET_PORT fixturePort = 0;
static bool supervisedFixture = false;
static std::function<void()> afterRead;
static std::mutex fixtureMutex;
static std::map<HINTERNET,std::wstring> fixturePaths;
static HINTERNET WINAPI fixtureOpen(LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD flags) {
  return WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                     WINHTTP_NO_PROXY_BYPASS, flags);
}
static HINTERNET WINAPI fixtureConnect(HINTERNET session, LPCWSTR, INTERNET_PORT, DWORD reserved) {
  return WinHttpConnect(session, L"127.0.0.1", fixturePort, reserved);
}
static HINTERNET WINAPI fixtureRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR path,
    LPCWSTR version, LPCWSTR referer, LPCWSTR* types, DWORD flags) {
  const auto request=WinHttpOpenRequest(connection,verb,path,version,referer,types,flags & ~WINHTTP_FLAG_SECURE);
  std::lock_guard lock(fixtureMutex);fixturePaths[request]=path;return request;
}
static BOOL WINAPI fixtureTimeouts(HINTERNET handle, int, int, int, int) {
  const int timeout = supervisedFixture ? 15000 : 1000;
  return WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout);
}
static BOOL WINAPI fixtureOption(HINTERNET handle, DWORD option, LPVOID data, DWORD size) {
  if (option == WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT) {
    DWORD accelerated = supervisedFixture ? 15000 : 1000;
    return WinHttpSetOption(handle, option, &accelerated, sizeof accelerated);
  }
  return WinHttpSetOption(handle, option, data, size);
}
static BOOL WINAPI fixtureRead(HINTERNET request, LPVOID buffer, DWORD count, LPDWORD read) {
  const auto ok = WinHttpReadData(request, buffer, count, read);
  std::function<void()> action;
  if(ok&&*read) {std::lock_guard lock(fixtureMutex);
    if(afterRead&&fixturePaths[request].find(L"/store_trailers/730/")!=std::wstring::npos&&fixturePaths[request].find(L".m4s")!=std::wstring::npos) {action=std::move(afterRead);afterRead={};}}
  if(action) action();
  return ok;
}
static BOOL WINAPI fixturePower(LPSYSTEM_POWER_STATUS status) {
  *status = {}; status->ACLineStatus = 1; status->BatteryLifePercent = 100; return TRUE;
}
#define WinHttpOpen fixtureOpen
#define WinHttpConnect fixtureConnect
#define WinHttpOpenRequest fixtureRequest
#define WinHttpSetTimeouts fixtureTimeouts
#define WinHttpSetOption fixtureOption
#define WinHttpReadData fixtureRead
#define GetSystemPowerStatus fixturePower
#include "shared_game_media_download_worker.h"
#undef WinHttpOpen
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpSetTimeouts
#undef WinHttpSetOption
#undef WinHttpReadData
#undef GetSystemPowerStatus
#include "shared_game_media_download_supervisor.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static size_t partialCount(const fs::path &root) {
  size_t count=0;std::error_code ec;
  for(fs::directory_iterator it(root/L"cache"/L"jobs"/L"game-media",ec),end;!ec&&it!=end;it.increment(ec))
    if(it->path().extension()==L".part")++count;
  return count;
}
int wmain(int argc,wchar_t**argv){try{
 supervisedFixture=true;
 if(argc==5&&std::wstring_view(argv[1])==L"--media-background"){
  wchar_t port[16]{};GetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",port,16);fixturePort=static_cast<INTERNET_PORT>(_wtoi(port));
  return backgroundEntrypoint(fs::absolute(argv[0]).parent_path()).value_or(99);
 }
 if(argc!=4)throw std::runtime_error("fresh root, loopback port, declared case required");
 const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("root exists");fixturePort=static_cast<INTERNET_PORT>(_wtoi(argv[2]));const auto mode=utf8(argv[3]);
 wchar_t source[32768]{};GetEnvironmentVariableW(L"YMCC_POPULAR_TEST_PROFILE",source,32768);const auto profile=read(source);
 size_t unsupported=0;for(const auto &item:profile["items"])if(item["sourceDocument"][std::to_string(item.value("appId",0))]["data"].value("type",std::string{})!="game")++unsupported;
 Json targets=Json::array();for(const auto &item:profile["items"]){targets.push_back({{"appId",item["appId"]},{"name",item["name"]},{"verified",true},{"lastPlayed",item["lastPlayed"]}});if(item.contains("selectedVideo"))targets.back()["selectedVideo"]=item["selectedVideo"];}
 Queue queue(root);Store store(root);
 for(const auto& item:profile["items"])if(item.contains("selectedVideo")){
   auto entry=store.registerEntry({{"appId",item["appId"]},{"verified",true}});entry["action"]="select-video";entry["selectedVideo"]=item["selectedVideo"];store.edit(entry);
 }
 queue.enqueue(targets);
 for(const auto& item:profile["items"]){
   Json target={{"appId",item["appId"]},{"verified",true}};
   if(item.contains("selectionAfterQueue")){auto entry=store.resolve(target);entry["action"]="select-video";entry["selectedVideo"]=item["selectionAfterQueue"];store.edit(entry);}
   if(item.value("staticOnly",false)){
     const auto sample=root/L"sample.mp4";{std::ofstream out(sample,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}
     auto lease=store.begin(target);lease["videoQualityPolicy"]=videoQualityPolicy;lease=store.publish(lease,sample,false);store.finish(lease);
     auto entry=store.resolve(target);entry["action"]="redownload";entry["slot"]="static";store.edit(entry);queue.enqueue(Json::array({target}));
   }
 }
 std::atomic<size_t> maximum{0};std::atomic<uint64_t> resumed{0};
 ymcc::media::downloads::Watcher watcher(root,L"main",[&]{const auto state=queue.snapshot();auto old=maximum.load();const auto active=state.value("activeCount",size_t{0});while(active>old&&!maximum.compare_exchange_weak(old,active)){};auto prior=resumed.load();const auto bytes=state.value("resumedBytes",uint64_t{0});while(bytes>prior&&!resumed.compare_exchange_weak(prior,bytes)){};});
 Json firstState,firstJobs,firstEntries=Json::object(),recovery=Json::object();bool edited=false;
 if(mode=="auto20"){
  SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());SetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",argv[2]);
  atomicWrite(fs::absolute(argv[0]).parent_path()/L"package-manifest.json",{{"backgroundMediaProtocol",1}});const auto start=GetTickCount64();uint64_t failed=0;bool complete=false;
  {Supervisor owner(root,fs::absolute(argv[0]),Limits{1500,20000,40000,100,0},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});owner.wake(true);
   while(GetTickCount64()-start<110000){auto state=queue.snapshot();for(const auto &h:state.value("history",Json::array()))if(h.value("reason",std::string{})=="worker-stalled"&&!failed)failed=GetTickCount64()-start;
    if(failed&&state.value("remaining",size_t{100})<=unsupported&&state.value("workerPid",DWORD{0})==0){complete=true;break;}Sleep(100);}
  }recovery={{"complete",complete},{"firstFailureMs",failed},{"elapsedMs",GetTickCount64()-start},{"retryAccelerated",false}};
 }else{
  if(mode=="stale-edit20")afterRead=[&]{auto entry=store.resolve(targets[0]);entry["action"]="delete";entry["slot"]="dynamic";store.edit(entry);edited=true;};
  auto token=queue.start();Worker(root,token,GetCurrentProcess()).run();firstState=queue.snapshot();firstJobs=queue.document()["jobs"];
  for(const auto &target:targets)firstEntries[std::to_string(target.value("appId",0))]=store.resolve(target);
  if(mode=="transient20"||mode=="disconnect20"||mode=="changed20"||mode=="plan20"||mode=="rate20"){
   Json retry=Json::array();for(const auto &target:targets){const auto key="steam-"+std::to_string(target.value("appId",0));if(firstJobs[key].value("state",std::string{})=="pending"){
     auto entry=store.resolve(target);entry["action"]="redownload";entry["slot"]="dynamic";store.edit(entry);retry.push_back(target);}}
   queue.enqueue(retry);token=queue.start();if(token.empty())throw std::runtime_error("retry not admitted");Worker(root,token,GetCurrentProcess()).run();
  }
 }
 Json entries=Json::object();for(const auto &target:targets)entries[std::to_string(target.value("appId",0))]=store.resolve(target);
 Json result={{"case",mode},{"entries",entries},{"state",queue.snapshot()},{"jobs",queue.document()["jobs"]},{"firstState",firstState},{"firstJobs",firstJobs},{"firstEntries",firstEntries},{"maxActive",maximum.load()},{"resumedBytes",resumed.load()},{"edited",edited},{"recovery",recovery}};
 atomicWrite(root/L"result.json",result);std::cout<<result.dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
