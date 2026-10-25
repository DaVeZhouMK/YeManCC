#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
// Test-only socket adapter: the production HTTPS allowlist still runs first.
// All network reads, HTTP framing and timeouts use the real WinHTTP engine.
static INTERNET_PORT fixturePort = 0;
static bool supervisedFixture = false;
static std::function<void()> afterRead;
static HINTERNET WINAPI fixtureOpen(LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD flags) {
  return WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                     WINHTTP_NO_PROXY_BYPASS, flags);
}
static HINTERNET WINAPI fixtureConnect(HINTERNET session, LPCWSTR, INTERNET_PORT, DWORD reserved) {
  return WinHttpConnect(session, L"127.0.0.1", fixturePort, reserved);
}
static HINTERNET WINAPI fixtureRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR path,
    LPCWSTR version, LPCWSTR referer, LPCWSTR* types, DWORD flags) {
  return WinHttpOpenRequest(connection, verb, path, version, referer, types,
                            flags & ~WINHTTP_FLAG_SECURE);
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
  if (ok && *read && afterRead) { auto action = std::move(afterRead); afterRead = {}; action(); }
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
int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 5 && std::wstring_view(argv[1]) == L"--media-background") {
      supervisedFixture = true;
      wchar_t value[16]{};
      GetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT", value, 16);
      fixturePort = static_cast<INTERNET_PORT>(_wtoi(value));
      return backgroundEntrypoint(fs::absolute(argv[0]).parent_path()).value_or(99);
    }
    if (argc != 4) throw std::runtime_error("isolated root, port and case required");
    const fs::path root = fs::absolute(argv[1]);
    fixturePort = static_cast<INTERNET_PORT>(_wtoi(argv[2]));
    const auto mode = utf8(argv[3]);
    if (fs::exists(root)) throw std::runtime_error("fresh test directory required");
    Queue q(root); Store store(root);
    Json target = {{"appId",570},{"name","fixture game"},{"verified",true}};
    if (mode == "unverified-target") target["verified"] = false;
    if (mode == "unverified-exe") target["executable"] = "C:/Fixture/Unconfirmed/game.exe";
    q.enqueue(Json::array({target}));
    int code = 0;
    Json recovery = Json::object();
    if (mode == "supervised-recovery") {
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
      SetEnvironmentVariableW(L"YMCC_WEAKNET_FIXTURE_PORT",argv[2]);
      atomicWrite(fs::absolute(argv[0]).parent_path()/L"package-manifest.json",{{"backgroundMediaProtocol",1}});
      const auto started = GetTickCount64();
      uint64_t failedAt = 0, lastPulse = 0; bool completed = false;
      {
        Supervisor owner(root,fs::absolute(argv[0]),Limits{1500,10000,20000,100,0},
                         ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});
        owner.wake(true);
        while (GetTickCount64()-started < 75000) {
          const auto state = q.snapshot();
          const auto elapsed = GetTickCount64()-started;
          if (!lastPulse || elapsed-lastPulse >= 5000) {
            lastPulse = elapsed;
            std::cerr << Json({{"elapsedMs",elapsed},{"wallNow",now()},
              {"phase",state.value("phase",std::string{})},
              {"dueAt",state.value("nextDueAt",int64_t{-1})},
              {"pid",state.value("workerPid",DWORD{0})}}).dump() << std::endl;
          }
          for (const auto& item:state.value("history",Json::array()))
            if (item.value("reason",std::string{}) == "worker-stalled" && !failedAt)
              failedAt = GetTickCount64()-started;
          if (failedAt && state.value("remaining",size_t{1}) == 0 &&
              state.value("workerPid",DWORD{0}) == 0) {completed=true;break;}
          Sleep(100);
        }
      }
      recovery={{"completed",completed},{"firstFailureMs",failedAt},
                {"totalMs",GetTickCount64()-started},{"retryBackoffAccelerated",false},
                {"watchdogMs",1500}};
    } else {
      const auto token = q.start();
      code = token.empty() ? 0 : Worker(root,token,GetCurrentProcess()).run();
    }
    auto state = q.snapshot(); auto doc = q.document(); auto entry = store.resolve(target);
    size_t partials = 0;
    std::error_code ec;
    for (fs::directory_iterator it(root/L"cache"/L"jobs"/L"game-media",ec),end;
         !ec && it!=end; it.increment(ec)) if (it->path().extension() == L".part") ++partials;
    Json report = {{"case",mode},{"workerCode",code},{"state",state},{"entry",entry},
                   {"jobs",doc["jobs"]},{"partialFiles",partials},{"recovery",recovery},
                   {"transport","real WinHTTP via test-only loopback adapter; 1s accelerated timeouts"}};
    atomicWrite(root/L"result.json",report);
    std::cout << report.dump() << std::endl;
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 2; }
}
