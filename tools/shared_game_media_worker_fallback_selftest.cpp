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

int wmain(int argc,wchar_t**argv){try{
 if(argc!=4)throw std::runtime_error("fresh isolated root, loopback port, case required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fresh root required");fs::create_directories(root);fixturePort=static_cast<INTERNET_PORT>(_wtoi(argv[2]));const std::string mode=utf8(argv[3]);
 Queue q(root);q.enqueue(Json::array({{{"appId",570},{"name","worker real transport regression"},{"verified",true}}}));const auto token=q.start();const auto started=GetTickCount64();const auto limits=mode=="slow-bytes"||mode=="slow-metadata"||mode=="session"?Limits{90000,500,mode=="session"?1400ull:6000ull}:Limits{};

#ifdef YMCC_MEDIA_OLD_WORKER
 const auto code=Worker(root,token,GetCurrentProcess()).run();
#else
 const auto code=Worker(root,token,GetCurrentProcess(),{},{},limits).run();
#endif
const auto e=Store(root).resolve({{"appId",570}});Json report={{"ok",true},{"mode",mode},{"workerCode",code},{"elapsedMs",GetTickCount64()-started},{"entry",e},{"state",q.snapshot()},{"jobs",q.document()["jobs"]}};atomicWrite(root/L"result.json",report);std::cout<<report.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
