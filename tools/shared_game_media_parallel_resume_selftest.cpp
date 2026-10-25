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
    if(afterRead&&fixturePaths[request].find(L"video")!=std::wstring::npos) {action=std::move(afterRead);afterRead={};}}
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
int wmain(int argc,wchar_t** argv) {
 try {
  supervisedFixture=true;
  if(argc==5&&std::wstring_view(argv[1])==L"--media-background") {
    wchar_t port[16]{};GetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",port,16);
    fixturePort=static_cast<INTERNET_PORT>(_wtoi(port));
    return backgroundEntrypoint(fs::absolute(argv[0]).parent_path()).value_or(99);
  }
  if(argc!=4)throw std::runtime_error("fresh root, loopback port and case required");
  const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("root exists");
  fixturePort=static_cast<INTERNET_PORT>(_wtoi(argv[2]));const auto mode=utf8(argv[3]);
  const bool parallel=mode.starts_with("parallel")||mode=="auto-five-resume";
  const auto count=parallel?(mode=="auto-five-resume"||mode=="parallel-cancel"?5:12):1;
  Json targets=Json::array();for(int n=0;n<count;++n)targets.push_back({{"appId",570+n},{"name","fixture "+std::to_string(570+n)},{"verified",true},{"lastPlayed",1000-n}});
  Queue queue(root);Store store(root);queue.enqueue(targets);
  std::atomic<size_t> maximum{0};std::atomic<uint64_t> resumed{0};
  auto sample=[&]{const auto state=queue.snapshot();const auto active=state.value("activeCount",size_t{0});auto old=maximum.load();while(active>old&&!maximum.compare_exchange_weak(old,active)){}
    const auto bytes=state.value("resumedBytes",uint64_t{0});auto prior=resumed.load();while(bytes>prior&&!resumed.compare_exchange_weak(prior,bytes)){};};
  ymcc::media::downloads::Watcher watcher(root,L"main",sample);
  int firstCode=0,secondCode=-1;Json firstState,firstJobs,firstEntries=Json::object(),recovery=Json::object();size_t partials=0;
  if(mode=="auto-five-resume") {
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
    SetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",argv[2]);
    atomicWrite(fs::absolute(argv[0]).parent_path()/L"package-manifest.json",{{"backgroundMediaProtocol",1}});
    const auto start=GetTickCount64();uint64_t failedAt=0;bool complete=false;
    {
      Supervisor owner(root,fs::absolute(argv[0]),Limits{1500,15000,30000,100,0},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});
      owner.wake(true);
      while(GetTickCount64()-start<110000) {
        sample();const auto state=queue.snapshot();
        for(const auto &event:state.value("history",Json::array())) if(event.value("reason",std::string{})=="worker-stalled"&&!failedAt)failedAt=GetTickCount64()-start;
        if(failedAt&&state.value("remaining",size_t{1})==0&&state.value("workerPid",DWORD{0})==0){complete=true;break;}
        Sleep(100);
      }
    }
    recovery={{"complete",complete},{"firstFailureMs",failedAt},{"elapsedMs",GetTickCount64()-start},{"retryAccelerated",false}};
    firstState=queue.snapshot();firstJobs=queue.document()["jobs"];
  } else {
    auto token=queue.start();
    if(mode=="parallel-two")queue.update(token,[](Json&j){j["maxParallel"]=2;});
    if(mode=="parallel-clamp")queue.update(token,[](Json&j){j["maxParallel"]=100;});
    if(mode=="parallel-cancel")afterRead=[&]{queue.configure(false,"");};
    firstCode=Worker(root,token,GetCurrentProcess()).run();sample();
    firstState=queue.snapshot();firstJobs=queue.document()["jobs"];partials=partialCount(root);
    for(const auto &target:targets)firstEntries[std::to_string(target.value("appId",0))]=store.resolve(target);
    if(!parallel||mode=="parallel-cancel") {
      if(mode=="resume-corrupt-meta"||mode=="resume-missing-part"||mode=="resume-416") {
        const auto folder=root/L"cache"/L"jobs"/L"game-media";std::error_code ec;
        for(fs::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)) {
          if(it->path().extension()!=L".part")continue;
          auto metadata=it->path();metadata.replace_extension(L".resume.json");
          if(mode=="resume-corrupt-meta"){std::ofstream file(metadata);file<<"{damaged";}
          if(mode=="resume-missing-part")fs::remove(it->path());
          if(mode=="resume-416") {
            wchar_t seed[32768]{};GetEnvironmentVariableW(L"YMCC_RESUME_SEED_VIDEO",seed,32768);
            if(!*seed)throw std::runtime_error("complete-cache fixture required");
            fs::copy_file(seed,it->path(),fs::copy_options::overwrite_existing);
            auto m=read(metadata);m["complete"]=true;m["expectedSize"]=fs::file_size(it->path());atomicWrite(metadata,m);
          }
        }
      }
      if(mode=="resume-app-isolation") {
        targets=Json::array({{{"appId",571},{"name","fixture 571"},{"verified",true}}});queue.enqueue(targets);
      } else {
        for(const auto &target:targets) {auto entry=store.resolve(target);entry["action"]="redownload";entry["slot"]="dynamic";store.edit(entry);}
        queue.configure(true,"");queue.enqueue(targets);
      }
      token=queue.start();if(token.empty())throw std::runtime_error("explicit retry was not admitted");
      secondCode=Worker(root,token,GetCurrentProcess()).run();sample();
    }
  }
  Json entries=Json::object();for(const auto &target:targets)entries[std::to_string(target.value("appId",0))]=store.resolve(target);
  Json report={{"case",mode},{"firstCode",firstCode},{"secondCode",secondCode},{"firstState",firstState},{"firstJobs",firstJobs},{"firstEntries",firstEntries},
    {"firstPartialCount",partials},{"state",queue.snapshot()},{"jobs",queue.document()["jobs"]},{"entries",entries},
    {"maxActive",maximum.load()},{"resumedBytes",resumed.load()},{"remainingPartials",partialCount(root)},{"recovery",recovery}};
  atomicWrite(root/L"result.json",report);std::cout<<report.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}
}
