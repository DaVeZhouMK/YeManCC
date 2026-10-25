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
  wchar_t port[16]{};GetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",port,16);fixturePort=static_cast<INTERNET_PORT>(_wtoi(port));return backgroundEntrypoint(fs::absolute(argv[0]).parent_path()).value_or(99);
 }
 if(argc!=4)throw std::runtime_error("fresh root, loopback port, case required");
 const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("root exists");fixturePort=static_cast<INTERNET_PORT>(_wtoi(argv[2]));const auto mode=utf8(argv[3]);
 SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());SetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",argv[2]);
 atomicWrite(fs::absolute(argv[0]).parent_path()/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",mode=="catalog-legacy"?0:1}});
 Queue q(root);Store store(root);std::atomic<size_t> maximum{0};bool pausedProtected=true,recovered=false,firstIdle=false;Json firstState;
 {
  Controller owner(root,fs::absolute(argv[0]),[&](const Json&state){auto prior=maximum.load();auto count=state.value("activeCount",size_t{0});while(count>prior&&!maximum.compare_exchange_weak(prior,count)){};},Limits{90000,240000,900000,100,0},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});
  Json locals=Json::array();if(mode=="catalog-locals")locals={{{"appId",1091500},{"name","Cyberpunk"},{"verified",true},{"sourceTier",1}},{{"appId",1361510},{"name","TMNT"},{"verified",true},{"sourceTier",2}}};
  owner.background({{"enabled",true},{"pauseReason","game-running"},{"catalog",true},{"targets",locals}});Sleep(300);firstState=q.snapshot();pausedProtected=firstState.value("workerPid",DWORD{0})==0&&firstState.value("catalogPending",false);
  owner.background({{"enabled",true},{"pauseReason",""},{"catalog",true}});
  auto start=GetTickCount64();while(GetTickCount64()-start<30000){auto state=q.snapshot();if(state.value("catalogStatus",std::string{})=="ready"&&!state.value("workerPid",DWORD{0})&&state.value("remaining",size_t{99})<=1){recovered=true;break;}
   if(mode=="catalog-legacy"&&state.value("catalogError",std::string{})=="download-worker-upgrade-required"&&!state.value("workerPid",DWORD{0})){recovered=true;break;}
   if(mode=="catalog-failure"&&state.value("catalogAttempts",0)>0&&!state.value("workerPid",DWORD{0})){firstIdle=true;break;}Sleep(50);}
  if(mode=="catalog-failure"&&firstIdle){firstState=q.snapshot();const auto token=firstState.value("session",std::string{});q.update(token,[](Json&j){j["catalogRetryAt"]=0;});owner.background({{"enabled",true},{"catalog",true},{"pauseReason",""}});start=GetTickCount64();while(GetTickCount64()-start<30000){auto state=q.snapshot();if(state.value("catalogStatus",std::string{})=="ready"&&!state.value("workerPid",DWORD{0})&&state.value("remaining",size_t{99})<=1){recovered=true;break;}Sleep(50);}}
 }
 auto entries=store.snapshot()["entries"];Json report={{"case",mode},{"pausedProtected",pausedProtected},{"recovered",recovered},{"firstIdle",firstIdle},{"firstState",firstState},{"state",q.snapshot()},{"jobs",q.document()["jobs"]},{"entries",entries},{"maxActive",maximum.load()}};atomicWrite(root/L"result.json",report);std::cout<<report.dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
