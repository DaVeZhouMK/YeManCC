// Inert transport: all WinHTTP calls are replaced; only fixture file IO and SHA-256 are real.
// Match the actual native preamble before including the Windows SDK.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef YMCC_MANIFEST_LOOPBACK
#include <winsock2.h>
#endif
#include <windows.h>
#include <winhttp.h>
#include <winioctl.h>
#include <wincrypt.h>
#include "json.hpp"
#include "update_download_resume.h"
#include "update_config_state.h"
#include "update_download_control.h"
#include "update_cache_maintenance.h"
#include "update_install_recovery.h"
#include "version.h"
#include "shared_game_media_capacity.h"
#include "sleep_power_state.h"
#include "sleep_power_revocation.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <mutex>
#include <optional>
#include <thread>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using json = nlohmann::json;
namespace fspath = std::filesystem;
static int checks = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string("FAIL: ") + label);
}
static bool makeFixtureJunction(const fspath::path& link,const fspath::path& target) {
    fspath::create_directories(link);
    const auto print=fspath::absolute(target).wstring(),sub=std::wstring(L"\\??\\")+print;
    const auto pathBytes=(sub.size()+1+print.size()+1)*sizeof(wchar_t);
    std::vector<unsigned char> buffer(16+pathBytes,0);
    const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(buffer.data(),&tag,4);
    const USHORT dataBytes=static_cast<USHORT>(8+pathBytes),subLength=static_cast<USHORT>(sub.size()*2),printOffset=static_cast<USHORT>((sub.size()+1)*2),printLength=static_cast<USHORT>(print.size()*2);
    memcpy(buffer.data()+4,&dataBytes,2);memcpy(buffer.data()+10,&subLength,2);memcpy(buffer.data()+12,&printOffset,2);memcpy(buffer.data()+14,&printLength,2);
    memcpy(buffer.data()+16,sub.data(),subLength);memcpy(buffer.data()+16+printOffset,print.data(),printLength);
    HANDLE handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    DWORD used=0;const bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr,0,&used,nullptr)!=FALSE;CloseHandle(handle);return ok;
}
struct Scenario {
    DWORD status = 200;
    std::map<DWORD, std::string> headers;
    std::string body;
    size_t failAfter = SIZE_MAX;
    bool cancelAtFailure = false;
    bool gameAfterFirstRead = false;
    bool stallHeaders = false, stallBody = false, failCallbackRegistration = false, failCallbackContext = false;
    size_t stallBodyAfter=SIZE_MAX;
    size_t chunkLimit=SIZE_MAX;
    ULONGLONG readTickMs=0;
};
static std::vector<Scenario> scenarios;
static size_t requestCount = 0, cursor = 0;
static Scenario current;
static std::vector<std::wstring> requestHeaders;
static std::atomic<bool> g_poolCancel{false};
static std::mutex g_updateOperationMtx;
static ymcc::update_download::Control g_updateDownloadControl;
static std::atomic<bool> g_updateBackgroundAllowed{true},g_updateGameDetected{false};
static std::atomic<ULONGLONG> fixtureTick{1000};
static std::atomic<ULONGLONG> g_updateGameObservationTick{1000};
static fspath::path fixtureRoot, dataRoot;
static std::vector<json> progressEvents;
static int metadataWrites = 0, failMetadataWrite = 0;
static HANDLE fixtureHeadersEvent=nullptr;
static HANDLE fixtureSyncManifestRelease=nullptr;
static int fixtureClosedHandles=0;
static WINHTTP_STATUS_CALLBACK fixtureCallback=nullptr;
static DWORD_PTR fixtureCallbackContext=0;
static WINHTTP_STATUS_CALLBACK fakeWinHttpSetStatusCallback(HINTERNET,WINHTTP_STATUS_CALLBACK callback,DWORD,DWORD_PTR) {
    if(current.failCallbackRegistration){SetLastError(ERROR_ACCESS_DENIED);return WINHTTP_INVALID_STATUS_CALLBACK;}
    const auto previous=fixtureCallback;fixtureCallback=callback;return previous;
}
static HINTERNET fakeWinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) {
    fixtureCallback=nullptr;fixtureCallbackContext=0;
    if (requestCount >= scenarios.size()) throw std::runtime_error("FIXTURE_FORBIDS_UNPLANNED_NETWORK_REQUEST");
    current = scenarios[requestCount++]; cursor = 0; requestHeaders.emplace_back();
    return reinterpret_cast<HINTERNET>(1);
}
static HINTERNET fakeWinHttpConnect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) { return reinterpret_cast<HINTERNET>(2); }
static HINTERNET fakeWinHttpOpenRequest(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) { return reinterpret_cast<HINTERNET>(3); }
static BOOL fakeWinHttpSetOption(HINTERNET,DWORD option,LPVOID value,DWORD) {
    if(option==WINHTTP_OPTION_CONTEXT_VALUE&&current.failCallbackContext){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(option==WINHTTP_OPTION_CONTEXT_VALUE)fixtureCallbackContext=*static_cast<DWORD_PTR*>(value);
    return TRUE;
}
static BOOL fakeWinHttpAddRequestHeaders(HINTERNET, LPCWSTR value, DWORD, DWORD) { requestHeaders.back() += value; return TRUE; }
static BOOL fakeWinHttpSendRequest(HINTERNET handle,LPCWSTR,DWORD,LPVOID,DWORD,DWORD,DWORD_PTR context) {
    if(fixtureCallback&&context)fixtureCallback(handle,context,WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,nullptr,0);return TRUE;
}
static BOOL fakeWinHttpReceiveResponse(HINTERNET handle,LPVOID) {
    if(current.stallHeaders){
        if(fixtureHeadersEvent)SetEvent(fixtureHeadersEvent);
        if(fixtureSyncManifestRelease&&!fixtureCallbackContext&&WaitForSingleObject(fixtureSyncManifestRelease,3000)!=WAIT_OBJECT_0){SetLastError(ERROR_WINHTTP_TIMEOUT);return FALSE;}
        return TRUE;
    }
    if(fixtureCallback&&fixtureCallbackContext)fixtureCallback(handle,fixtureCallbackContext,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,nullptr,0);return TRUE;
}
static BOOL fakeWinHttpCloseHandle(HINTERNET handle) {
    ++fixtureClosedHandles;
    if(handle==reinterpret_cast<HINTERNET>(3)&&fixtureCallback&&fixtureCallbackContext){const auto context=fixtureCallbackContext;fixtureCallbackContext=0;fixtureCallback(handle,context,WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING,nullptr,0);}return TRUE;
}
static BOOL fakeWinHttpQueryHeaders(HINTERNET, DWORD query, LPCWSTR, LPVOID output, LPDWORD bytes, LPDWORD) {
    if (query == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) {
        *static_cast<DWORD*>(output) = current.status; *bytes = sizeof(DWORD); return TRUE;
    }
    auto header = current.headers.find(query);
    if (header == current.headers.end()) { SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND); return FALSE; }
    std::wstring value(header->second.begin(), header->second.end());
    const DWORD required = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    if (!output || *bytes < required) { *bytes = required; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::memcpy(output, value.c_str(), required); *bytes = required; return TRUE;
}
static BOOL fakeWinHttpQueryDataAvailable(HINTERNET, LPDWORD available) {
    if (cursor >= current.failAfter) {
        if (current.cancelAtFailure) g_poolCancel.store(true);
        SetLastError(ERROR_WINHTTP_CONNECTION_ERROR); return FALSE;
    }
    DWORD bytes=static_cast<DWORD>((std::min)(current.body.size()-cursor,current.failAfter-cursor));
    if(available)*available=bytes;else if(fixtureCallback&&fixtureCallbackContext)fixtureCallback(reinterpret_cast<HINTERNET>(3),fixtureCallbackContext,WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,&bytes,sizeof(bytes));
    return TRUE;
}
static BOOL fakeWinHttpReadData(HINTERNET, LPVOID output, DWORD wanted, LPDWORD read) {
    if(current.stallBody||cursor>=current.stallBodyAfter){if(fixtureHeadersEvent)SetEvent(fixtureHeadersEvent);return TRUE;}
    if(cursor>=current.failAfter){if(current.cancelAtFailure)g_poolCancel=true;SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);return FALSE;}
    const DWORD bytes=static_cast<DWORD>((std::min)({static_cast<size_t>(wanted),current.body.size()-cursor,current.failAfter-cursor,current.chunkLimit}));
    fixtureTick+=current.readTickMs;
    std::memcpy(output,current.body.data()+cursor,bytes);cursor+=bytes;if(current.gameAfterFirstRead)g_updateGameDetected=true;
    if(read)*read=bytes;else if(fixtureCallback&&fixtureCallbackContext)fixtureCallback(reinterpret_cast<HINTERNET>(3),fixtureCallbackContext,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,output,bytes);
    return TRUE;
}
static ULONGLONG fakeGetTickCount64() { return ++fixtureTick; }
static void fakeSleep(DWORD ms) { fixtureTick += ms; }
#ifdef YMCC_MANIFEST_LOOPBACK
static INTERNET_PORT fixtureLoopbackPort=0;
static std::atomic<unsigned> fixtureRealHeaderWaits=0,fixtureRealBodyWaits=0,fixtureRealCloses=0,fixtureRealFinalCallbacks=0;
static std::atomic<unsigned> fixtureRealHeaderStarts=0,fixtureRealSendReturns=0,fixtureRealSendCallbacks=0;
static std::atomic<WINHTTP_STATUS_CALLBACK> fixtureRealCallback=nullptr;
static HINTERNET fixtureRealOpen(LPCWSTR agent,DWORD,LPCWSTR,LPCWSTR,DWORD flags){
    return ::WinHttpOpen(agent,WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,flags);
}
static HINTERNET fixtureRealConnect(HINTERNET session,LPCWSTR host,INTERNET_PORT port,DWORD reserved){
    if(wcscmp(host,L"127.0.0.1")!=0||!fixtureLoopbackPort||port!=fixtureLoopbackPort)throw std::runtime_error("FIXTURE_FORBIDS_NONLOOPBACK");
    return ::WinHttpConnect(session,host,port,reserved);
}
static void CALLBACK fixtureRealStatus(HINTERNET handle,DWORD_PTR context,DWORD status,LPVOID data,DWORD size){
    if(context&&status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)++fixtureRealSendCallbacks;
    if(const auto callback=fixtureRealCallback.load())callback(handle,context,status,data,size);
    if(context&&status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)++fixtureRealFinalCallbacks;
}
static WINHTTP_STATUS_CALLBACK fixtureRealSetCallback(HINTERNET handle,WINHTTP_STATUS_CALLBACK callback,DWORD flags,DWORD_PTR reserved){
    fixtureRealCallback=callback;return ::WinHttpSetStatusCallback(handle,fixtureRealStatus,flags,reserved);
}
static BOOL fixtureRealSend(HINTERNET handle,LPCWSTR headers,DWORD headSize,LPVOID body,DWORD bodySize,DWORD totalSize,DWORD_PTR context){
    const auto ok=::WinHttpSendRequest(handle,headers,headSize,body,bodySize,totalSize,context);++fixtureRealSendReturns;return ok;
}
static BOOL fixtureRealHeaders(HINTERNET handle,LPVOID reserved){++fixtureRealHeaderStarts;const auto ok=::WinHttpReceiveResponse(handle,reserved);++fixtureRealHeaderWaits;return ok;}
static BOOL fixtureRealRead(HINTERNET handle,LPVOID data,DWORD size,LPDWORD read){const auto ok=::WinHttpReadData(handle,data,size,read);++fixtureRealBodyWaits;return ok;}
static BOOL fixtureRealClose(HINTERNET handle){const auto ok=::WinHttpCloseHandle(handle);if(ok)++fixtureRealCloses;return ok;}
#define WinHttpOpen fixtureRealOpen
#define WinHttpConnect fixtureRealConnect
#define WinHttpSetStatusCallback fixtureRealSetCallback
#define WinHttpSendRequest fixtureRealSend
#define WinHttpReceiveResponse fixtureRealHeaders
#define WinHttpReadData fixtureRealRead
#define WinHttpCloseHandle fixtureRealClose
#else
#define WinHttpOpen fakeWinHttpOpen
#define WinHttpConnect fakeWinHttpConnect
#define WinHttpOpenRequest fakeWinHttpOpenRequest
#define WinHttpSetOption fakeWinHttpSetOption
#define WinHttpSetStatusCallback fakeWinHttpSetStatusCallback
#define WinHttpAddRequestHeaders fakeWinHttpAddRequestHeaders
#define WinHttpSendRequest fakeWinHttpSendRequest
#define WinHttpReceiveResponse fakeWinHttpReceiveResponse
#define WinHttpCloseHandle fakeWinHttpCloseHandle
#define WinHttpQueryHeaders fakeWinHttpQueryHeaders
#define WinHttpQueryDataAvailable fakeWinHttpQueryDataAvailable
#define WinHttpReadData fakeWinHttpReadData
#define GetTickCount64 fakeGetTickCount64
#define Sleep fakeSleep
#endif
#include "http_request_deadline.h"
static constexpr DWORD DEFAULT_HTTP_TIMEOUT_MS = 5000;
static constexpr DWORD UPDATE_HTTP_IO_TIMEOUT_MS = 120000;
static constexpr DWORD UPDATE_RETRY_INTERVAL_MS = 5000;
static std::wstring U2W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::string W2U(const std::wstring& s) { return std::string(s.begin(), s.end()); }
static std::string trim_ascii(const std::string& s) { return ymcc::update_download::trimHeader(s); }
static std::string ascii_lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
#ifdef YMCC_MANIFEST_LOOPBACK
//__PRODUCTION_MANIFEST_SETUP__
#else
static void setHttpTimeouts(HINTERNET, DWORD = DEFAULT_HTTP_TIMEOUT_MS) {}
#endif
static std::string downloadWinHttpError(const char* api, DWORD error) { return std::string(api) + ':' + std::to_string(error); }
static bool isSteamHostName(const wchar_t*) { return false; }
#ifdef YMCC_MANIFEST_LOOPBACK
//__PRODUCTION_MANIFEST_CRACK_URL__
#else
static bool crackHttpUrl(const std::string& url, URL_COMPONENTS& uc, wchar_t* host, wchar_t*, wchar_t*, std::wstring& objectPath) {
    if (url.rfind("https://", 0) != 0) return false;
    uc.nScheme = INTERNET_SCHEME_HTTPS; uc.nPort = 443; std::wcscpy(host, L"fixture.invalid"); objectPath = L"/YeManCC.zip"; return true;
}
#endif
//__PRODUCTION_ACTIVITY_ADMISSION__
//__PRODUCTION_RESULT_DECLARATION__
//__PRODUCTION_TRANSPORT__
static std::string sha256File(const std::wstring& path);
static void fixtureCoalescedReady();
static bool revokeAtHash=false;
static BOOL fixtureCryptHashData(HCRYPTHASH hash,const BYTE* bytes,DWORD count,DWORD flags){
    const auto ok=::CryptHashData(hash,bytes,count,flags);
    if(revokeAtHash){revokeAtHash=false;fixtureCoalescedReady();}
    return ok;
}
#define CryptHashData fixtureCryptHashData
//__PRODUCTION_SHA256__
#undef CryptHashData
static std::wstring app_data_dir() { return dataRoot.wstring(); }
static std::wstring fixtureExe,POWER_CONTROL_DIR;
static json fixtureRecoveryJournal=json::object();
static json fixtureRecoveryRead(const std::wstring& exe,const std::wstring& pc){
    if(ymcc::updaterecovery::normalized(exe)!=ymcc::updaterecovery::normalized(fixtureExe)||
       ymcc::updaterecovery::normalized(pc)!=ymcc::updaterecovery::normalized(POWER_CONTROL_DIR))
        throw std::runtime_error("FIXTURE_RECOVERY_CONTEXT_ESCAPED");
    return fixtureRecoveryJournal;
}
//__PRODUCTION_VERSION_ADMISSION__
static bool belowRoot(const fspath::path& path) {
    const auto relative = path.lexically_relative(fixtureRoot);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
static void writeText(const fspath::path& path, const std::string& bytes) {
    check(belowRoot(path), "fixture write remains within its unique output root");
    fspath::create_directories(path.parent_path()); std::ofstream file(path, std::ios::binary); file << bytes;
    if (!file) throw std::runtime_error("FIXTURE_FILE_WRITE_FAILED");
}
static std::string readText(const fspath::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }

static ymcc::sleeppower::SharedPowerV1 fixturePower;
static std::mutex fixturePowerMutex;
static ymcc::sleeppower::RevocationMirrors fixturePowerRevocations;
static void fixtureCoalescedReady(){
    {std::lock_guard lock(fixturePowerMutex);++fixturePower.suspendSerial;++fixturePower.publicationRevision;}
    fixturePowerRevocations.revoke();
}
static void resetFixturePower(){fixturePower={};fixturePower.generation=61;fixturePower.publicationRevision=1;fixturePower.observedNotificationSequence=3;fixturePower.flags=3;}
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t size){if(size<sizeof(fixturePower))return 0;std::lock_guard lock(fixturePowerMutex);*out=fixturePower;return 1;}
[[maybe_unused]] static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out){return ymcc::sleeppower::capturePower(readSharedSystemPower,out);}
[[maybe_unused]] static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value){return ymcc::sleeppower::powerCurrent(readSharedSystemPower,value);}
[[maybe_unused]] static auto observeSharedPowerRevocation(const ymcc::sleeppower::SharedPowerV1& value){return fixturePowerRevocations.observe(readSharedSystemPower,value);}
static json fixtureTarget(const json& args){return {{"version",args.at("version")},{"sha256",args.at("sha256")},
    {"power",{{"generation",fixturePower.generation},{"suspendSerial",fixturePower.suspendSerial},{"observedNotificationSequence",fixturePower.observedNotificationSequence}}}};}
static bool ioShortWrite=false,ioFailFlush=false,ioFailPublish=false,ioCorruptAfterPublish=false,ioSuspendAtFlush=false;
static BOOL fixtureAtomicWrite(HANDLE file,LPCVOID data,DWORD count,LPDWORD written,LPOVERLAPPED over){return ::WriteFile(file,data,ioShortWrite&&count>1?count/2:count,written,over);}
static BOOL fixtureAtomicFlush(HANDLE file){if(ioFailFlush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}const auto result=::FlushFileBuffers(file);if(ioSuspendAtFlush){ioSuspendAtFlush=false;++fixturePower.suspendSerial;++fixturePower.publicationRevision;}return result;}
static BOOL fixtureAtomicMove(LPCWSTR from,LPCWSTR to,DWORD flags){check(belowRoot(from)&&belowRoot(to),"atomic move remains in owned root");if(ioFailPublish){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const auto result=::MoveFileExW(from,to,flags);if(result&&ioCorruptAfterPublish)writeText(to,"{external-publication-corruption");return result;}
static BOOL fixtureAtomicReplace(LPCWSTR to,LPCWSTR from,LPCWSTR backup,DWORD flags,LPVOID exclude,LPVOID reserved){check(belowRoot(from)&&belowRoot(to),"atomic replacement remains in owned root");if(ioFailPublish){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::ReplaceFileW(to,from,backup,flags,exclude,reserved);}
#define WriteFile fixtureAtomicWrite
#define FlushFileBuffers fixtureAtomicFlush
#define MoveFileExW fixtureAtomicMove
#define ReplaceFileW fixtureAtomicReplace
//__PRODUCTION_ATOMIC_WRITER__
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReplaceFileW
static bool fixtureDownloadAtomicFlags(const std::wstring& path,const std::string& text,bool durable,
    const std::function<void()>& prepare={},const std::function<bool()>& current={}) {
    ++metadataWrites;
    if (failMetadataWrite > 0 && metadataWrites == failMetadataWrite) return false;
    check(belowRoot(path),"actual atomic writer path belongs to fixture");
    return fixtureAtomicFlags(path,text,durable,prepare,current);
}
static bool sgWriteFileAtomic(const std::wstring& path, const std::string& text) {return fixtureDownloadAtomicFlags(path,text,true);}
static std::string sgReadFile(const std::wstring& path) { return readText(path); }
static void updateProgressPost(const json& record) { progressEvents.push_back(record); }
namespace ymcc::dedicatedmemory { static std::string bootIdentity() { return "fixture-boot-A"; } }
static json updateProgressRead() { return progressEvents.empty() ? json::object() : progressEvents.back(); }
static std::mutex g_updateProgressMtx;
static json g_updateProgress=json::object();
static std::wstring updateProgressPath(){return app_data_dir()+L"\\update\\update-progress.json";}
static std::wstring updateTransactionStatePath(){return app_data_dir()+L"\\update\\update-state.json";}
static HWND g_hwnd=nullptr;
static constexpr UINT WM_UPDATE_PROGRESS=WM_USER+17;
//__PRODUCTION_CONFIG_WRITER__
//__PRODUCTION_CONFIG_READERS__

#define sgWriteFileAtomicFlags fixtureAtomicFlags
//__PRODUCTION_PREDOWNLOAD_POLICY__
#undef sgWriteFileAtomicFlags
static json fixtureTransaction=json::object();
static json updateStateReadWithTransaction(){return fixtureTransaction.empty()?(progressEvents.empty()?json::object():progressEvents.back()):fixtureTransaction;}
static std::map<std::string,std::function<json(const json&)>> policyHandlers;
static void ipc_on(const std::string& name,std::function<json(const json&)> handler) { policyHandlers[name]=std::move(handler); }
//__PRODUCTION_MANIFEST_TRANSPORT__
static json stampManifest(json args){
    //__PRODUCTION_MANIFEST_STAMP__
    return args;
}
static void fixtureRegisterPredownload() {
//__PRODUCTION_PREDOWNLOAD_REGISTRATION__
//__PRODUCTION_MANIFEST_REGISTRATION__
//__PRODUCTION_CACHE_REGISTRATION__
}
static json stampDownload(json args){
    //__PRODUCTION_DOWNLOAD_STAMP__
    return args;
}
static json fixtureDownloadUpdateReceived(const json& a) {
#define sgWriteFileAtomicFlags fixtureDownloadAtomicFlags
//__PRODUCTION_UPDATE_HANDLER__
#undef sgWriteFileAtomicFlags
}
static json fixtureDownloadUpdate(const json& args){return fixtureDownloadUpdateReceived(stampDownload(args));}
static void reset(const std::string& name, std::vector<Scenario> responses) {
    resetFixturePower();ioSuspendAtFlush=false;
    dataRoot = fixtureRoot / name; fspath::create_directories(dataRoot / "update");
    POWER_CONTROL_DIR=(dataRoot/"PowerControl").wstring();fixtureRecoveryJournal=json::object();
    scenarios = std::move(responses); requestCount = cursor = 0; requestHeaders.clear(); progressEvents.clear(); fixtureClosedHandles=0;
    g_poolCancel = false; g_updateBackgroundAllowed=true;g_updateGameDetected=false;
#ifdef YMCC_MANIFEST_LOOPBACK
    g_updateGameObservationTick=GetTickCount64();
#else
    g_updateGameObservationTick=fixtureTick.load();
#endif
    metadataWrites = failMetadataWrite = 0;
}
static Scenario full(const std::string& body, const std::string& etag = "\"A\"") {
    Scenario s; s.body = body; s.headers = {{WINHTTP_QUERY_CONTENT_LENGTH,std::to_string(body.size())},{WINHTTP_QUERY_ETAG,etag}}; return s;
}
static Scenario partial(size_t offset, const std::string& whole, const std::string& etag = "\"A\"") {
    auto s = full(whole.substr(offset), etag); s.status = 206;
    s.headers[WINHTTP_QUERY_CONTENT_RANGE] = "bytes " + std::to_string(offset) + '-' + std::to_string(whole.size()-1) + '/' + std::to_string(whole.size()); return s;
}

static int updaterConfigStorm(const std::string& selected = {}) {
    json rows=json::array();const auto test=[&](const std::string& name,const std::function<void()>& run){if(!selected.empty()&&name!=selected)return;try{run();rows.push_back({{"name",name},{"ok",true}});}catch(const std::exception& e){rows.push_back({{"name",name},{"ok",false},{"error",e.what()}});}};
    const auto putPolicy=[&](const std::string& raw){writeText(updatePredownloadPolicyPath(),raw);};
    const auto policyFile=[&](){return readText(updatePredownloadPolicyPath());};
    for(const auto& raw:std::vector<std::string>{"", "{broken", "[]", "null", std::string(65537,'x')})test("invalid policy uses new defaults with diagnostic "+std::to_string(rows.size()),[&]{reset("storm-policy-"+std::to_string(rows.size()),{});putPolicy(raw);const auto policy=updatePredownloadPolicyRead();check(policy.is_object()&&policy.at("enabled")==true&&policy.contains("configReadError")&&policyFile()==raw,"bad policy authorized automatic work or changed original");});
    const json valid={{"schemaVersion",1},{"enabled",false},{"failureCount",2},{"nextCheckAt",42},{"lastNotifiedBoot","fixture-boot-A"}};
    for(const auto& field:std::vector<std::pair<std::string,std::vector<json>>>{{"schemaVersion",{1.0,true,2,"1",nullptr}},{"enabled",{1,"true",nullptr}},{"failureCount",{-1,1.5,true,18446744073709551615ULL}},{"nextCheckAt",{-1,2.5,"10",18446744073709551615ULL}},{"lastNotifiedBoot",{false,json::array(),std::string(256,'x')}}})for(const auto& wrong:field.second)test("policy bad "+field.first+"="+wrong.dump(),[&]{reset("storm-policy-"+std::to_string(rows.size()),{});auto document=valid;document[field.first]=wrong;const auto raw=document.dump();putPolicy(raw);const auto policy=updatePredownloadPolicyRead();check(policy.at("enabled")==true&&policy.value("failureCount",0)==0&&policy.contains("configReadError")&&policyFile()==raw,"invalid policy partially consumed or mutated original");});
    test("duplicate policy cannot supply persisted authority",[&]{reset("storm-policy-duplicate",{});const auto raw="{\"schemaVersion\":1,\"enabled\":false,\"enabled\":true}";putPolicy(raw);const auto policy=updatePredownloadPolicyRead();check(policy.contains("configReadError")&&policy.at("failureCount")==0&&policyFile()==raw,"duplicate enabled value authorized background");});
    test("policy unknown field survives save",[&]{reset("storm-policy-unknown",{});auto document=valid;document["opaque"]={{"note","preserve"}};putPolicy(document.dump());auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;updatePredownloadPolicyWrite(policy);const auto actual=json::parse(policyFile());check(actual.contains("opaque")&&actual["opaque"]==document["opaque"],"saving policy erased unknown properties");});
    test("obsolete future pause cannot permanently veto native download",[&]{
        reset("storm-future-pause",{});auto saved=valid;saved["nextCheckAt"]=INT64_MAX;saved["cacheQuietUntil"]=INT64_MAX;putPolicy(saved.dump());
        const auto recovered=updatePredownloadPolicyRead();check(recovered.at("enabled")==false&&recovered.at("nextCheckAt")==0&&recovered.at("cacheQuietUntil")==0&&recovered.contains("configReadError"),"far future pause retained native veto or bypassed explicit OFF");
        auto ordinary=valid;const auto now=static_cast<int64_t>(time(nullptr))*1000;
        ordinary["cacheQuietUntil"]=now+60000;ordinary["nextCheckAt"]=now+300000;putPolicy(ordinary.dump());
        const auto kept=updatePredownloadPolicyRead();check(kept.at("cacheQuietUntil")==ordinary.at("cacheQuietUntil")&&kept.at("nextCheckAt")==ordinary.at("nextCheckAt")&&!kept.contains("configReadError"),"current bounded user pause was dropped");
    });
    test("future disposable policy cannot block replacement",[&]{reset("storm-policy-future",{});auto document=valid;document["schemaVersion"]=99;putPolicy(document.dump());auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;updatePredownloadPolicyWrite(policy);check(updatePredownloadPolicyRead().at("enabled")==true&&json::parse(policyFile()).at("schemaVersion")==1,"future old policy blocked a valid replacement");});
    test("source read sharing fault returns inactive then retry",[&]{reset("storm-policy-lock",{});putPolicy(valid.dump());HANDLE held=CreateFileW(updatePredownloadPolicyPath().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture lock setup");bool safe=false;try{const auto policy=updatePredownloadPolicyRead();safe=policy.at("enabled")==false;}catch(...){safe=false;}CloseHandle(held);check(safe,"read fault throws or opens background");check(updatePredownloadPolicyRead().at("failureCount")==2,"released fault does not reload current policy");});
    test("successor power scope before atomic publication",[&]{
        reset("storm-successor-power",{});fixtureRegisterPredownload();
        auto policy=valid;policy["enabled"]=true;putPolicy(policy.dump());const auto before=policyFile();
        const auto save=policyHandlers.at("app.updatePredownloadTarget");
        const json args={{"version",std::to_string(APP_VER_MAJOR+1)+".0.0"},{"sha256",std::string(64,'a')}};
        const auto queued=fixtureTarget(args);++fixturePower.suspendSerial;++fixturePower.publicationRevision;
        check(save(queued)==false&&policyFile()==before,"queued successor from previous suspend serial changed the durable policy");
        check(save(args)==false&&policyFile()==before,"successor without complete public scope was admitted");
        ioSuspendAtFlush=true;const auto writing=fixtureTarget(args);
        check(save(writing)==false&&!ioSuspendAtFlush&&policyFile()==before,"successor revoked after flush replaced the previous policy");
        for(const auto& entry:fspath::directory_iterator(dataRoot/"update"))
            check(entry.path().filename()=="predownload-policy.json","revoked successor retained a temporary image");
        const auto current=fixtureTarget(args);++fixturePower.publicationRevision;
        check(save(current)==true&&updatePredownloadPolicyRead().at("targetVersion")==args.at("version"),"current successor could not persist after revocation");
    });
    const auto progressFile=[&](const json& document){writeText(updateProgressPath(),document.dump());g_updateProgress=json::object();};
    test("uncommitted started marker is never completion",[&]{reset("storm-started",{});fixtureRecoveryJournal=json::object();progressFile({{"phase","installing"},{"stage","install"},{"version","0.0.99"}});writeText(updateTransactionStatePath(),"{\"phase\":\"started\",\"pid\":1}");const auto value=fixtureActualStateRead();check(value.value("phase",std::string())!="completed","started marker became completed without commit proof");});
    test("writable committed state is not protected completion",[&]{reset("storm-committed",{});fixtureRecoveryJournal=json::object();progressFile({{"phase","installing"},{"version","0.0.99"}});writeText(updateTransactionStatePath(),"{\"phase\":\"committed\",\"version\":\"0.0.99\",\"pid\":1}");check(fixtureActualStateRead().value("phase",std::string())!="completed","unprotected state fabricated completed install");});
    for(const auto& field:std::vector<std::pair<std::string,json>>{{"percent","100"},{"downloadedBytes",-1},{"repairAuthorized",true},{"installerActive",true},{"phase",json::array()},{"updatedAt",18446744073709551615ULL}})test("progress invalid "+field.first,[&]{reset("storm-progress-"+std::to_string(rows.size()),{});progressFile({{"phase","completed"},{"version","0.0.99"},{field.first,field.second}});const auto value=fixtureActualStateRead();check(value.value("phase",std::string())!="completed"&&!value.value("repairAuthorized",false)&&!value.value("installerActive",false),"bad progress fields fabricated applied/repair state");});
    test("cached malformed progress defaults without throw",[&]{reset("storm-cache",{});g_updateProgress=json::array({1,2});check(fixtureActualProgressRead().is_object(),"cached scalar or array escaped document guard");g_updateProgress=json::object();});

    const auto archives=[&](const fspath::path& source){std::set<fspath::path> out;for(const auto& file:fspath::directory_iterator(source.parent_path()))if(file.path().filename().wstring().starts_with(source.filename().wstring()+L".ymcc-invalid-"))out.insert(file.path());return out;};
    const auto exactArchive=[&](const fspath::path& path,const std::set<fspath::path>& old,const std::string& raw){auto current=archives(path);std::vector<fspath::path> fresh;for(const auto& file:current)if(!old.contains(file)&&readText(file)==raw)fresh.push_back(file);check(fresh.size()==1,"original updater preimage not preserved exactly once");};
    for(const auto& raw:std::vector<std::string>{"", "{broken", std::string(65537,'x')})test("explicit policy repair does not require original archive "+std::to_string(rows.size()),[&]{reset("storm-repair-"+std::to_string(rows.size()),{});putPolicy(raw);const auto old=archives(updatePredownloadPolicyPath());auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;updatePredownloadPolicyWrite(policy);check(ymcc::updateconfig::policyValid(json::parse(policyFile())),"policy replacement invalid");check(updatePredownloadPolicyRead().at("enabled")==true,"explicit policy repair did not become readable");});
    test("failed repaired policy write restores damaged original",[&]{reset("storm-repair-fault",{});putPolicy("{broken");auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;failMetadataWrite=metadataWrites+1;bool refused=false;try{updatePredownloadPolicyWrite(policy);}catch(...){refused=true;}failMetadataWrite=0;check(refused&&policyFile()=="{broken","failed repair lost original or reported success");updatePredownloadPolicyWrite(policy);check(updatePredownloadPolicyRead().at("enabled")==true,"repair did not retry after release");});
    test("removed update target remains absent after save",[&]{reset("storm-target-remove",{});auto p=valid;p["targetVersion"]="0.0.99";p["targetSha256"]=std::string(64,'a');putPolicy(p.dump());auto policy=updatePredownloadPolicyRead();policy.erase("targetVersion");policy.erase("targetSha256");updatePredownloadPolicyWrite(policy);check(!updatePredownloadPolicyRead().contains("targetVersion"),"merge resurrected explicitly retired target");});
    test("protected unfinished receipt overrides lost progress",[&]{reset("storm-protected-open",{});g_updateProgress=json::object();writeText(updateProgressPath(),"{broken");fixtureRecoveryJournal={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","protected-op"},{"phase","applying"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"exePath",ymcc::updaterecovery::utf8(fixtureExe)},{"powerControl",ymcc::updaterecovery::utf8(POWER_CONTROL_DIR)}};const auto value=fixtureActualStateRead();check(value.at("repairAuthorized")==true&&value.at("phase")=="interrupted"&&value.at("version")=="0.0.99","trusted unfinished transaction lost to bad diagnostics");});
    test("repair preserves only matching install failure",[&]{
        reset("storm-protected-failure",{});
        fixtureRecoveryJournal={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","protected-op"},{"phase","repair-required"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"exePath",ymcc::updaterecovery::utf8(fixtureExe)},{"powerControl",ymcc::updaterecovery::utf8(POWER_CONTROL_DIR)}};
        const json failure={{"phase","failed"},{"stage","install"},{"operationId","protected-op"},{"version","0.0.99"},{"sha256",std::string(64,'A')},{"error","startup did not finish"}};
        g_updateProgress=failure;
        const auto value=fixtureActualStateRead();
        check(value.at("repairAuthorized")==true&&value.at("phase")=="interrupted"&&value.at("error")==failure.at("error"),"same transaction lost its failure detail");
        for(const auto& [key,other]:std::initializer_list<std::pair<const char*,std::string>>{{"operationId","other-op"},{"version","0.0.98"},{"sha256",std::string(64,'b')},{"stage","download"}}){
            g_updateProgress=failure;g_updateProgress[key]=other;
            check(fixtureActualStateRead().at("error")=="","unrelated diagnostic leaked into a trusted repair");
        }
    });
    test("ordinary export allows only fixed update evidence",[&]{
        using ymcc::updateconfig::exportEvidence;
        const json progress={{"stage","download"},{"phase","downloading"},{"version","0.0.99"},{"downloadedBytes",7},{"attempt",2},
            {"lastError","HTTP 502: https://example.invalid/private?token=secret"},{"url","https://example.invalid/private"},{"operationId","secret-op"}};
        const json state={{"phase","rolled-back"},{"version","0.0.99"},{"token","secret-handshake"},{"error","new YeManCC process did not complete startup handshake"}};
        const json journal={{"phase","repair-required"},{"version","0.0.99"},{"exePath","C:\\private\\YeManCC.exe"},{"sha256",std::string(64,'a')}};
        const auto evidence=exportEvidence(progress,state,journal);const auto raw=evidence.dump();
        check(evidence.at("progress").at("httpStatus")==502&&evidence.at("progress").at("downloadedBytes")==7&&evidence.at("progress").at("version")=="0.0.99","download evidence lost");
        check(evidence.at("helperState").at("errorKind")=="startup-handshake"&&evidence.at("protectedJournal").at("phase")=="repair-required","helper or journal evidence lost");
        for(const auto* secret:{"private","secret","sha256","operationId","token"})check(raw.find(secret)==std::string::npos,"private update data exported");
        check(!exportEvidence(json::object(),{{"phase",123}},json::object()).at("helperState").at("present").get<bool>(),"invalid helper record claimed present");
    });
    test("protected committed receipt is the completion authority",[&]{reset("storm-protected-complete",{});g_updateProgress=json::object();fixtureRecoveryJournal={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","protected-op"},{"phase","committed"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"exePath",ymcc::updaterecovery::utf8(fixtureExe)},{"powerControl",ymcc::updaterecovery::utf8(POWER_CONTROL_DIR)}};const auto value=fixtureActualStateRead();check(value.at("phase")=="completed"&&value.at("percent")==100&&!value.at("repairAuthorized").get<bool>(),"trusted committed transaction could not complete safely");});
    test("protected commit for different version cannot complete current job",[&]{reset("storm-protected-wrong-version",{});progressFile({{"phase","installing"},{"version","0.0.99"}});fixtureRecoveryJournal={{"phase","committed"},{"version","0.0.98"}};check(fixtureActualStateRead().at("phase")!="completed","other version receipt completed current operation");});
    test("actual progress writer replaces corrupt snapshot without archive dependency",[&]{reset("storm-progress-write",{});g_updateProgress=json::object();writeText(updateProgressPath(),"{broken");const auto old=archives(updateProgressPath());fixtureActualProgressPost({{"phase","downloading"},{"version","0.0.99"},{"percent",10}});check(json::parse(readText(updateProgressPath())).at("percent")==10,"valid new diagnostic snapshot blocked by old bytes or archive dependency");check(fixtureActualProgressRead().at("percent")==10,"runtime progress not retained after persistence");});
    test("progress persistence failure stays live but reports unknown durable state",[&]{reset("storm-progress-write-failure",{});g_updateProgress=json::object();writeText(updateProgressPath(),"{broken");failMetadataWrite=metadataWrites+1;fixtureActualProgressPost({{"phase","downloading"},{"percent",10}});failMetadataWrite=0;const auto value=fixtureActualProgressRead();check(value.at("percent")==10&&value.contains("snapshotPersistenceError")&&readText(updateProgressPath())=="{broken","failed diagnostic save hid live state or pretended persistence");fixtureActualProgressPost({{"phase","downloading"},{"percent",11}});check(!fixtureActualProgressRead().contains("snapshotPersistenceError")&&json::parse(readText(updateProgressPath())).at("percent")==11,"released progress persistence fault cannot retry");});
    const std::string payload="UPDATER CONFIG INERT PAYLOAD";writeText(fixtureRoot/"config-expected.bin",payload);const auto hash=sha256File((fixtureRoot/"config-expected.bin").wstring());
    const auto target=std::to_string(APP_VER_MAJOR)+"."+std::to_string(APP_VER_MINOR)+"."+std::to_string(APP_VER_PATCH+1);
    const json args={{"url","https://fixture.invalid/YeManCC.zip"},{"version",target},{"sha256",hash},{"operationId","config-op"}};
    const json meta={{"url",args["url"]},{"version",target},{"sha256",hash},{"etag","\"A\""},{"totalBytes",payload.size()}};
    for(const auto& kind:std::vector<std::string>{"future","locked","directory"})test("resume "+kind+" uses bounded new-data-first recovery",[&]{
        reset("storm-resume-"+kind,{full(payload)});const auto part=dataRoot/"update/package.zip.part",side=dataRoot/"update/package.zip.part.json";
        writeText(part,payload.substr(0,4));HANDLE held=INVALID_HANDLE_VALUE;auto document=meta;document["schemaVersion"]=99;
        if(kind=="directory")fspath::create_directory(side);else writeText(side,kind=="future"?document.dump():meta.dump());
        if(kind=="locked")held=CreateFileW(side.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        bool refused=false;try{fixtureDownloadUpdate(args);}catch(...){refused=true;}if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
        if(kind=="locked")check(refused&&requestCount==0,"locked metadata must fail bounded before network");
        else check(!refused&&requestCount==1&&requestHeaders[0].find(L"Range:")==std::wstring::npos&&readText(dataRoot/"update/package.zip")==payload,"old resume annotations blocked fresh verified download");
    });
    test("duplicate resume identity preserved before fresh download",[&]{reset("storm-resume-duplicate",{full(payload)});const auto part=dataRoot/"update/package.zip.part",side=dataRoot/"update/package.zip.part.json";const auto raw=std::string("{\"url\":\"foreign\",")+meta.dump().substr(1);writeText(part,payload.substr(0,4));writeText(side,raw);const auto old=archives(side),oldPart=archives(part);fixtureDownloadUpdate(args);check(requestCount==1&&requestHeaders[0].find(L"Range:")==std::wstring::npos,"duplicate resume data was appended");exactArchive(side,old,raw);exactArchive(part,oldPart,payload.substr(0,4));check(readText(dataRoot/"update/package.zip")==payload,"fresh resumed recovery did not require final SHA");});

    test("actual fractional download speed remains valid progress",[&]{reset("storm-progress-fractional",{});g_updateProgress=json::object();fixtureActualProgressPost({{"phase","downloading"},{"version","0.0.99"},{"speedBps",12.345},{"etaSeconds",3},{"percent",42.1}});const auto value=fixtureActualProgressRead();check(value.at("phase")=="downloading"&&value.at("speedBps")==12.345&&value.at("percent")==42.1,"strict validation broke actual floating-point speed producer");});
    for(const auto& field:std::vector<std::string>{"sha256","operationId"})test("protected committed identity binds "+field,[&]{reset("storm-protected-id-"+field,{});progressFile({{"phase","installing"},{"version","0.0.99"},{field,field=="sha256"?std::string(64,'b'):std::string("new-op")}});fixtureRecoveryJournal={{"phase","committed"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"operationId","old-op"}};check(fixtureActualStateRead().at("phase")!="completed","same-version foreign commit completed another operation");});
    test("invalid input diagnostic preserves an actionable failure",[&]{reset("storm-invalid-request",{});g_updateProgress=json::object();fixtureActualProgressPost({{"phase","failed"},{"version","bad"},{"sha256","bad"},{"error","invalid-version"},{"message","cannot start invalid input"}});const auto value=fixtureActualProgressRead();check(value.at("phase")=="failed"&&value.at("error")=="invalid-version"&&!value.contains("version"),"invalid request erased the actual failure or kept false identity");});

    for(const auto& kind:std::vector<std::string>{"short","flush","publish"})test("actual atomic writer "+kind+" refuses and retries",[&]{reset("storm-real-atomic-"+kind,{});putPolicy(valid.dump());const auto original=policyFile();auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;ioShortWrite=kind=="short";ioFailFlush=kind=="flush";ioFailPublish=kind=="publish";bool refused=false;try{updatePredownloadPolicyWrite(policy);}catch(...){refused=true;}ioShortWrite=ioFailFlush=ioFailPublish=false;check(refused&&policyFile()==original,"failed real atomic writer changed source or acknowledged saved");for(const auto& file:fspath::directory_iterator(dataRoot/"update"))check(file.path().extension()!=L".tmp","failed real writer retained owned temp");updatePredownloadPolicyWrite(policy);check(updatePredownloadPolicyRead().at("enabled")==true,"released atomic failure cannot retry");});
    test("actual atomic writer post-publication fault is detected",[&]{reset("storm-real-readback",{});putPolicy(valid.dump());auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;ioCorruptAfterPublish=true;bool refused=false;try{updatePredownloadPolicyWrite(policy);}catch(...){refused=true;}ioCorruptAfterPublish=false;check(refused&&policyFile()=="{external-publication-corruption"&&updatePredownloadPolicyRead().contains("configReadError"),"unconfirmed actual write lost diagnostic");updatePredownloadPolicyWrite(policy);check(updatePredownloadPolicyRead().at("enabled")==true,"postpublish corruption cannot repair explicitly");});

    for(const auto& mode:std::vector<std::string>{"utf8-bom","utf16-le","utf16-be"})test("supported policy encoding "+mode,[&]{reset("storm-encoding-"+mode,{});const auto text=valid.dump();std::string raw;if(mode=="utf8-bom")raw="\xef\xbb\xbf"+text;else{raw=mode=="utf16-le"?std::string("\xff\xfe",2):std::string("\xfe\xff",2);for(unsigned char c:text){raw+=mode=="utf16-le"?std::string({static_cast<char>(c),'\0'}):std::string({'\0',static_cast<char>(c)});}}putPolicy(raw);check(updatePredownloadPolicyRead().at("failureCount")==2&&policyFile()==raw,"valid encoded policy rejected or changed by read");auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;updatePredownloadPolicyWrite(policy);check(updatePredownloadPolicyRead().at("enabled")==true,"encoded policy cannot save normally");});
    test("deep policy is inactive and bounded before normalization",[&]{reset("storm-depth",{});std::string nested="0";for(unsigned i=0;i<1500;++i)nested="{\"next\":"+nested+"}";const auto raw="{\"schemaVersion\":1,\"enabled\":true,\"unknown\":"+nested+"}";putPolicy(raw);check(updatePredownloadPolicyRead().contains("configReadError")&&policyFile()==raw,"deep policy consumed or rewritten by read");const auto old=archives(updatePredownloadPolicyPath());auto policy=updatePredownloadPolicyRead();policy["enabled"]=true;updatePredownloadPolicyWrite(policy);check(ymcc::updateconfig::policyValid(json::parse(policyFile())),"policy replacement invalid");check(updatePredownloadPolicyRead().at("enabled")==true,"deep original cannot be preserved and repaired");});
    test("decoded escaped duplicate policy fields are rejected",[&]{reset("storm-escaped-key",{});const std::string raw="{\"schemaVersion\":1,\"enabled\":false,\"enab\\u006ced\":true}";putPolicy(raw);check(updatePredownloadPolicyRead().contains("configReadError")&&policyFile()==raw,"escaped duplicate bypassed key check");});
    test("bad encoding is discarded without importing fields",[&]{reset("storm-encoding-bad",{});const auto raw=std::string("{\"schemaVersion\":1,\"enabled\":true,\"x\":\"")+std::string(1,static_cast<char>(255))+"\"}";putPolicy(raw);check(updatePredownloadPolicyRead().contains("configReadError")&&policyFile()==raw,"invalid UTF8 granted enabled preference");});
    test("well formed writable completed progress is still unconfirmed",[&]{reset("storm-writable-complete",{});progressFile({{"phase","completed"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"percent",100}});fixtureRecoveryJournal=json::object();check(fixtureActualStateRead().at("phase")!="completed","writable progress promoted itself to committed");});
    unsigned failed=(!selected.empty()&&rows.size()!=1)?1:0;for(const auto& row:rows)if(!row.at("ok").get<bool>())++failed;
    const json report={{"status",failed?"FAIL":"PASS"},{"caseCount",rows.size()},{"failed",failed},{"cases",rows},{"selectedCase",selected},{"actualExtractedConsumer",true},{"networkIo",false},{"installedOperation",false}};
    writeText(fixtureRoot/"config-storm.json",report.dump(2));std::cout<<"UPDATER_CONFIG_STORM "<<report.dump()<<"\n";return failed?1:0;
}

// Directed continuation of the original native updater consumer. Time/network
// are inert ports; file IO, actual request owner, cancellation and SHA are real.
static int updaterWeakNetwork(){
 const std::string payload="WEAK NETWORK EXACT PRODUCTION PAYLOAD";
 writeText(fixtureRoot/"weak-expected.bin",payload);const auto hash=sha256File((fixtureRoot/"weak-expected.bin").wstring());
 const std::string version=std::to_string(APP_VER_MAJOR)+"."+std::to_string(APP_VER_MINOR)+"."+std::to_string(APP_VER_PATCH+1);
 const json args={{"url","https://fixture.invalid/YeManCC.zip"},{"version",version},{"sha256",hash},{"operationId","weak-network"}};
 json records=json::array();const auto pass=[&](const char* name){records.push_back({{"name",name},{"pass",true}});};
 reset("slow-progressing-body",{});auto slow=full(payload);slow.chunkLimit=1;slow.readTickMs=10000;scenarios={slow};
 const auto began=fixtureTick.load();fixtureDownloadUpdate(args);
 check(requestCount==1&&fixtureTick-began>120000&&readText(dataRoot/"update/package.zip")==payload,"slow progressing transfer restarts at a total deadline or loses bytes");
 check(fixtureClosedHandles==3&&g_updateDownloadControl.status().operation.empty(),"slow success retains transport or operation owner");pass("slow progress survives total duration above per-IO idle timeout");
 for(bool exit:{false,true}){
  reset(exit?"exit-with-pending-body":"game-with-pending-body",{});auto body=full(payload);body.stallBody=true;scenarios={body};
  fixtureHeadersEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(fixtureHeadersEvent!=nullptr,"owned pending-body event setup");
  auto background=args;background["background"]=!exit;std::atomic<bool> signalled{false};
  std::thread cancel([&]{if(WaitForSingleObject(fixtureHeadersEvent,2000)==WAIT_OBJECT_0){signalled=true;if(exit)g_poolCancel=true;else g_updateGameDetected=true;}});
  const auto started=std::chrono::steady_clock::now();bool stopped=false;
  try{fixtureDownloadUpdate(background);}catch(const std::exception&){stopped=true;}
  cancel.join();CloseHandle(fixtureHeadersEvent);fixtureHeadersEvent=nullptr;
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
  check(signalled&&stopped&&elapsed<3000&&requestCount==1,"silent body ignores exit/game cancellation");
  check(!fspath::exists(dataRoot/"update/package.zip")&&fixtureClosedHandles==3&&fixtureCallbackContext==0&&g_updateDownloadControl.status().operation.empty(),"cancelled silent-body transfer leaves a ready cache or owned native handles");
  pass(exit?"pending body cancelled by exit with handles returned":"pending body cancelled by game with handles returned");
 }
 reset("offline-round-then-recover",{});Scenario outage;outage.status=503;scenarios={outage,outage,outage};auto background=args;background["background"]=true;bool exhausted=false;
 try{fixtureDownloadUpdate(background);}catch(const std::exception&){exhausted=true;}
 check(exhausted&&requestCount==3&&g_updateDownloadControl.status().operation.empty(),"offline background round does not release ownership after bounded retries");
 scenarios={full(payload)};requestCount=0;cursor=0;requestHeaders.clear();fixtureDownloadUpdate(background);
 check(requestCount==1&&readText(dataRoot/"update/package.zip")==payload,"released network outage cannot self-heal on next admitted round");pass("three offline attempts release ownership and next healthy round recovers");
 reset("power-revoked-partial",{});
 auto held=full(payload);held.chunkLimit=4;held.stallBodyAfter=4;scenarios={held,partial(4,payload)};
 const auto oldRequest=stampDownload(background);
 fixtureHeadersEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(fixtureHeadersEvent!=nullptr,"owned power-stalled body event setup");
 std::promise<std::string> retired;auto outcome=retired.get_future();
 std::jthread old([&]{try{fixtureDownloadUpdateReceived(oldRequest);retired.set_value("success");}catch(const std::exception& e){retired.set_value(e.what());}});
 const bool entered=WaitForSingleObject(fixtureHeadersEvent,2000)==WAIT_OBJECT_0;
 fixtureCoalescedReady();
 const bool returned=outcome.wait_for(std::chrono::milliseconds(650))==std::future_status::ready;
 if(!returned)g_poolCancel=true;
 old.join();CloseHandle(fixtureHeadersEvent);fixtureHeadersEvent=nullptr;g_poolCancel=false;
 const auto terminal=outcome.get();
 check(entered&&returned&&terminal=="Background download paused for power transition","public power revocation returns pending package transport without renderer cancellation");
 check(g_updateBackgroundAllowed&&!g_updateGameDetected&&requestCount==1&&fixtureClosedHandles==3&&!fixtureCallbackContext&&g_updateDownloadControl.status().operation.empty(),"background remains idle and old transfer returns every transport owner");
 check(readText(dataRoot/"update/package.zip.part")==payload.substr(0,4)&&!fspath::exists(dataRoot/"update/package.zip")&&updateProgressRead().value("phase","")=="interrupted","power pause retains exact partial and publishes no ready package");
 const auto checkpoint=readText(dataRoot/"update/package.zip.part.json");
 bool oldQueueRejected=false;try{fixtureDownloadUpdateReceived(oldRequest);}catch(const std::exception& e){oldQueueRejected=std::string(e.what())==terminal;}
 check(oldQueueRejected&&requestCount==1&&readText(dataRoot/"update/package.zip.part.json")==checkpoint,"queued old power request reaches neither download nor sidecar write");
 const auto sidecar=dataRoot/"update/package.zip.part.json";
 const auto attributes=GetFileAttributesW(sidecar.c_str());
 check(attributes!=INVALID_FILE_ATTRIBUTES&&SetFileAttributesW(sidecar.c_str(),attributes|FILE_ATTRIBUTE_READONLY),"mark retained sidecar read-only before fresh admission");
 auto next=background;next["operationId"]="weak-network-next";fixtureDownloadUpdate(next);
 check(requestCount==2&&requestHeaders.back().find(L"Range: bytes=4-")!=std::wstring::npos&&readText(dataRoot/"update/package.zip")==payload,"current operation resumes exact accepted range and verifies SHA");
 pass("coalesced public Ready retires package transfer then resumes retained range with a read-only checkpoint");
 reset("power-revoked-cache-hash",{});writeText(dataRoot/"update/package.zip.part",payload);
 auto cache=background;cache["cacheOnly"]=true;revokeAtHash=true;
 bool hashPaused=false;try{fixtureDownloadUpdate(cache);}catch(const std::exception& e){hashPaused=std::string(e.what())==terminal;}
 check(hashPaused&&!revokeAtHash&&requestCount==0&&g_updateDownloadControl.status().operation.empty()&&!fspath::exists(dataRoot/"update/package.zip")&&readText(dataRoot/"update/package.zip.part")==payload,"cache-only SHA revocation preserves the unpromoted package");
 fixtureDownloadUpdate(cache);
 check(requestCount==0&&readText(dataRoot/"update/package.zip")==payload&&!fspath::exists(dataRoot/"update/package.zip.part"),"current cache-only verification self-heals with no transport");
 pass("cache-only SHA returns on public revocation and recovers offline");
 const json report={{"status","PASS"},{"records",records},{"checks",checks},{"networkIo",false},{"installedOperation",false},{"actualExtractedConsumer",true}};
 writeText(fixtureRoot/"weak-network.json",report.dump(2));std::cout<<"UPDATER_WEAK_NETWORK "<<report.dump()<<"\n";return 0;
}

static int updaterManifestSerialization(){
    const std::string payload=R"({"version":"99.0.0","sha256":"fixture-value"})";
    auto held=full(payload);held.stallHeaders=true;
    auto bad=full("not-json");
    reset("manifest-serialization",{held,bad,full(payload)});fixtureRegisterPredownload();
    const auto probe=policyHandlers.at("app.checkUpdate");
    const json foreground={{"url","https://fixture.invalid/manifest"}},background={{"url","https://fixture.invalid/manifest"},{"background",true}};
    const auto openFixtureWait=[] {
        fixtureHeadersEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        fixtureSyncManifestRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(fixtureHeadersEvent&&fixtureSyncManifestRelease,"fixture transport events exist");
    };
    const auto closeFixtureWait=[] {
        CloseHandle(fixtureHeadersEvent);CloseHandle(fixtureSyncManifestRelease);
        fixtureHeadersEvent=fixtureSyncManifestRelease=nullptr;
    };
    openFixtureWait();
    json first;
    std::exception_ptr firstError;
    std::jthread old([&]{try{first=probe(stampManifest(background));}catch(...){firstError=std::current_exception();}});
    const bool started=WaitForSingleObject(fixtureHeadersEvent,2000)==WAIT_OBJECT_0;
    bool declined=false;
    if(started){try{probe(stampManifest(foreground));}catch(const std::exception& e){declined=std::string(e.what())=="Update manifest check already running";}}
    const auto heldCalls=requestCount;
    if(started&&fixtureCallback&&fixtureCallbackContext)fixtureCallback(reinterpret_cast<HINTERNET>(3),fixtureCallbackContext,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,nullptr,0);
    SetEvent(fixtureSyncManifestRelease);old.join();closeFixtureWait();
    check(started&&!firstError&&first==json::parse(payload),"background manifest completes after explicit release");
    check(declined&&heldCalls==1,"foreground and background callers share one native manifest slot");
    bool failed=false;try{probe(stampManifest(foreground));}catch(const json::parse_error&){failed=true;}
    check(failed,"native slot allows another request after old completion");
    check(probe(stampManifest(background))==json::parse(payload)&&requestCount==3&&fixtureClosedHandles==9&&!fixtureCallbackContext,
        "HTTP error returns the native slot and next admitted request succeeds");
    for(const bool stallBody:{false,true}) {
        auto stalled=full(payload);stalled.stallHeaders=!stallBody;stalled.stallBody=stallBody;
        reset(stallBody?"manifest-stalled-body":"manifest-stalled-headers",{stalled,full(payload)});
        openFixtureWait();const auto args=stampManifest(stallBody?foreground:background);
        std::promise<std::string> completion;auto completed=completion.get_future();
        std::jthread worker([&]{try{probe(args);completion.set_value("success");}catch(const std::exception& e){completion.set_value(e.what());}});
        const bool pending=WaitForSingleObject(fixtureHeadersEvent,2000)==WAIT_OBJECT_0;
        const auto queuedBeforeSleep=stampManifest(background);
        {std::lock_guard lock(fixturePowerMutex);++fixturePower.suspendSerial;++fixturePower.publicationRevision;}
        fixturePowerRevocations.revoke();
        const bool settled=completed.wait_for(std::chrono::milliseconds(650))==std::future_status::ready;
        SetEvent(fixtureSyncManifestRelease);worker.join();closeFixtureWait();const auto result=completed.get();
        check(pending&&settled&&result=="Update manifest check retired by power transition",
              stallBody?"public power retirement releases stalled manifest body":"public power retirement releases stalled manifest headers");
        check(requestCount==1&&fixtureClosedHandles==3&&!fixtureCallbackContext,"retired manifest releases each handle and callback context once");
        bool queueRetired=false;try{probe(queuedBeforeSleep);}catch(const std::exception& e){queueRetired=std::string(e.what())=="Update manifest check retired by power transition";}
        check(queueRetired&&requestCount==1,"sleep-retired queued manifest reaches no transport");
        check(probe(stampManifest(background))==json::parse(payload)&&requestCount==2&&fixtureClosedHandles==6&&!fixtureCallbackContext,
              "new Ready reuses manifest slot and closes its own transport");
    }
    const auto expired=stampManifest(foreground);fixtureTick+=40001;
    bool deadline=false;try{probe(expired);}catch(const std::exception& e){deadline=std::string(e.what()).find("deadline exceeded (phase=queue)")!=std::string::npos;}
    check(deadline&&requestCount==2,"manifest uses one native deadline including queue wait");
    const json report={{"status","PASS"},{"checks",checks},{"realNetwork",false},{"actualCheckUpdateHandler",true},{"actualManifestTransport",true}};
    writeText(fixtureRoot/"manifest-serialization.json",report.dump(2));
    std::cout<<"UPDATER_MANIFEST_SERIALIZATION "<<report.dump()<<"\n";return 0;
}

#ifdef YMCC_MANIFEST_LOOPBACK
struct ManifestFixtureSocket {
    SOCKET value=INVALID_SOCKET;
    explicit ManifestFixtureSocket(SOCKET owned):value(owned){}
    ~ManifestFixtureSocket(){reset();}
    ManifestFixtureSocket(const ManifestFixtureSocket&)=delete;
    void reset(){if(value!=INVALID_SOCKET){closesocket(value);value=INVALID_SOCKET;}}
};
static SOCKET manifestAccept(SOCKET listener) {
    fd_set readable;FD_ZERO(&readable);FD_SET(listener,&readable);timeval bound{2,0};
    return select(0,&readable,nullptr,nullptr,&bound)==1?accept(listener,nullptr,nullptr):INVALID_SOCKET;
}
static bool manifestReadRequest(SOCKET peer){
    if(peer==INVALID_SOCKET)return false;
    DWORD timeout=1000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    setsockopt(peer,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    char buffer[512];std::string request;const auto deadline=GetTickCount64()+2000;
    while(request.size()<4096&&GetTickCount64()<deadline){
        const int count=recv(peer,buffer,sizeof(buffer),0);if(count<=0)return false;
        request.append(buffer,static_cast<size_t>(count));
        if(request.find("\r\n\r\n")!=std::string::npos)return request.starts_with("GET /manifest HTTP/");
    }
    return false;
}
static bool manifestSend(SOCKET peer,const std::string& value){
    for(size_t offset=0;offset<value.size();){
        const int count=send(peer,value.data()+offset,static_cast<int>(value.size()-offset),0);
        if(count<=0)return false;
        offset+=static_cast<size_t>(count);
    }
    return true;
}
static int updaterManifestLoopback(){
    WSADATA socketData{};check(WSAStartup(MAKEWORD(2,2),&socketData)==0,"Winsock start");
    const struct SocketCleanup {~SocketCleanup(){WSACleanup();}} sockets;
    const std::string payload=R"({"version":"99.0.0","sha256":"fixture-value"})";
    json records=json::array();
    for(const bool stalledBody:{false,true}){
        reset(stalledBody?"real-async-body":"real-async-headers",{});fixtureRegisterPredownload();
        ManifestFixtureSocket listener(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        sockaddr_in bound{};bound.sin_family=AF_INET;bound.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(listener.value!=INVALID_SOCKET&&bind(listener.value,reinterpret_cast<const sockaddr*>(&bound),sizeof(bound))==0&&
              listen(listener.value,1)==0,"fixture listener binds only to loopback");
        int size=sizeof(bound);check(getsockname(listener.value,reinterpret_cast<sockaddr*>(&bound),&size)==0,"fixture listener identity");
        fixtureLoopbackPort=ntohs(bound.sin_port);
        const json input={{"url","http://127.0.0.1:"+std::to_string(fixtureLoopbackPort)+"/manifest"},{"background",!stalledBody}};
        const auto probe=policyHandlers.at("app.checkUpdate");
        const auto headersBefore=fixtureRealHeaderWaits.load(),bodyBefore=fixtureRealBodyWaits.load();
        const auto headerStartsBefore=fixtureRealHeaderStarts.load(),sendsBefore=fixtureRealSendReturns.load(),callbacksBefore=fixtureRealSendCallbacks.load();
        const auto closesBefore=fixtureRealCloses.load(),finalBefore=fixtureRealFinalCallbacks.load();
        std::promise<std::string> completion;auto done=completion.get_future();
        std::jthread worker([&]{try{probe(stampManifest(input));completion.set_value("success");}catch(const std::exception& e){completion.set_value(e.what());}});
        ManifestFixtureSocket peer(manifestAccept(listener.value));
        bool reached=manifestReadRequest(peer.value);
        if(reached&&stalledBody)reached=manifestSend(peer.value,"HTTP/1.1 200 OK\r\nContent-Length: 1024\r\nConnection: close\r\n\r\nx");
        // A peer can receive the wire request before WinHTTP finishes its send callback.
        const auto phase=[&]{return stalledBody?fixtureRealBodyWaits.load()>bodyBefore:fixtureRealSendReturns.load()>sendsBefore;};
        const auto stageDeadline=GetTickCount64()+2000;
        while(reached&&!phase()&&GetTickCount64()<stageDeadline)Sleep(5);
        const bool pending=reached&&phase()&&done.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
        const auto began=GetTickCount64();
        {std::lock_guard lock(fixturePowerMutex);++fixturePower.suspendSerial;++fixturePower.publicationRevision;}
        fixturePowerRevocations.revoke();
        const bool retired=done.wait_for(std::chrono::milliseconds(650))==std::future_status::ready;
        const auto elapsed=GetTickCount64()-began;
        // The server stays connected through the measurement and only then falls back to close.
        peer.reset();worker.join();
        const auto result=done.get();
        const auto finalDeadline=GetTickCount64()+2000;
        while(fixtureRealFinalCallbacks.load()==finalBefore&&GetTickCount64()<finalDeadline)Sleep(5);
        std::cout<<"UPDATER_MANIFEST_LOOPBACK_STAGE "<<json{{"phase",stalledBody?"body":"headers"},{"peerAccepted",reached},
            {"phaseEntered",phase()},{"pending",pending},{"retired",retired},{"elapsedMs",elapsed},{"terminal",result},
            {"headerStarts",fixtureRealHeaderStarts.load()-headerStartsBefore},{"headerReturns",fixtureRealHeaderWaits.load()-headersBefore},
            {"sendReturns",fixtureRealSendReturns.load()-sendsBefore},
            {"sendCallbacks",fixtureRealSendCallbacks.load()-callbacksBefore},
            {"handleCloses",fixtureRealCloses.load()-closesBefore},{"finalCallbacks",fixtureRealFinalCallbacks.load()-finalBefore}}.dump()<<'\n';
        check(pending&&retired&&result=="Update manifest check retired by power transition","real pending request retires while peer is still connected");
        check(fixtureRealCloses.load()==closesBefore+3&&fixtureRealFinalCallbacks.load()==finalBefore+1,"real request, connection, session and terminal callback returned");
        std::promise<json> current;auto resultNow=current.get_future();
        g_updateGameObservationTick=GetTickCount64();
        std::jthread successor([&]{try{current.set_value(probe(stampManifest(input)));}catch(...){current.set_exception(std::current_exception());}});
        ManifestFixtureSocket next(manifestAccept(listener.value));
        const bool served=manifestReadRequest(next.value)&&manifestSend(next.value,"HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(payload.size())+"\r\nConnection: close\r\n\r\n"+payload);
        next.reset();successor.join();
        check(served&&resultNow.get()==json::parse(payload),"current Ready reuses slot with real loopback transport");
        records.push_back({{"phase",stalledBody?"body":"headers"},{"retiredMs",elapsed},{"terminal",result},{"realHandleCloses",3},{"realFinalCallbacks",1},{"currentManifest",true}});
    }
    const json report={{"status","PASS"},{"checks",checks},{"realWinHttp",true},{"syntheticPowerInput",true},{"peer","loopback-only"},{"records",records}};
    writeText(fixtureRoot/"manifest-loopback.json",report.dump(2));std::cout<<"UPDATER_MANIFEST_LOOPBACK "<<report.dump()<<"\n";return 0;
}
#endif

int wmain(int argc, wchar_t** argv) {
    try {
#ifdef YMCC_MANIFEST_LOOPBACK
        if(argc!=3||std::wstring(argv[2])!=L"--manifest-loopback")throw std::runtime_error("LOOPBACK_MODE_REQUIRED");
#else
        if (argc<2 || argc>4 || (argc>=3 && std::wstring(argv[2])!=L"--config-storm" && !(argc==3&&(std::wstring(argv[2])==L"--weak-network"||std::wstring(argv[2])==L"--manifest-serialization"))))
            throw std::runtime_error("UNIQUE_FIXTURE_ROOT_REQUIRED");
#endif
        fixtureRoot = fspath::absolute(argv[1]);
        wchar_t ownExe[32768]{};if(!GetModuleFileNameW(nullptr,ownExe,32768))throw std::runtime_error("FIXTURE_EXE_IDENTITY_MISSING");fixtureExe=ownExe;
        if (fspath::exists(fixtureRoot)) throw std::runtime_error("FIXTURE_ROOT_ALREADY_EXISTS");
        fspath::create_directories(fixtureRoot);
#ifdef YMCC_MANIFEST_LOOPBACK
        return updaterManifestLoopback();
#else
        if((argc==3||argc==4)&&std::wstring(argv[2])==L"--config-storm")return updaterConfigStorm(argc==4?W2U(argv[3]):std::string{});
        if(argc==3&&std::wstring(argv[2])==L"--weak-network")return updaterWeakNetwork();
        if(argc==3&&std::wstring(argv[2])==L"--manifest-serialization")return updaterManifestSerialization();
        using namespace ymcc::update_download;
        uint64_t number = 0;
        check(parseUnsigned64("4294967297",number) && number == 4294967297ULL,"64-bit length above 4 GiB");
        check(parseUnsigned64("18446744073709551615",number) && number == UINT64_MAX,"full uint64 range");
        for (const std::string bad : {"","-1","+1"," 1","1 ","1.5","18446744073709551616","1\r\nRange: bytes=0-"})
            check(!parseUnsigned64(bad,number),"invalid/overflow unsigned length rejected");
        ContentRange range;
        check(parseContentRange("bytes 4294967296-4294967300/5000000000",range) && range.total==5000000000ULL,"large range uses 64-bit offsets");
        check(parseContentRange("bytes */0",range) && range.unsatisfied,"empty unsatisfied response parsed");
        for (const std::string bad : {"bytes +1-2/3","bytes -1-2/3","bytes 0-3/3","bytes 3-2/4","bytes */*","bytes 0-0/0","bytes 0-1/-1","bytes 0-1/3/4"})
            check(!parseContentRange(bad,range),"malformed/out-of-bounds range rejected");
        check(resumeValidator("\"A\"")=="\"A\"","strong ETag preserved");
        for (const std::string weak : {"W/\"A\"","not-quoted","\"a\r\nb\"","\"a b\""}) check(resumeValidator(weak).empty(),"weak/unsafe ETag not sent as If-Range");
        check(evaluateResponse(200,0,"5000000000","","identity","","").totalBytes==5000000000ULL,"200 total is not truncated to DWORD");
        check(evaluateResponse(206,4294967296ULL,"4","bytes 4294967296-4294967299/5000000000","","","").bodyEnd==4294967300ULL,"large resumed segment bound");
        check(evaluateResponse(200,4,"8","","","","").restartRequired,"ignored range requires clean restart");
        check(!evaluateResponse(206,4,"4","bytes 4-7/8","gzip","","").accepted,"compressed partial response rejected");
        check(!evaluateResponse(206,4,"3","bytes 4-7/8","","","").accepted,"mismatched range length rejected");
        check(!evaluateResponse(206,4,"4","bytes 4-7/*","","","").accepted,"unknown resumed total refused safely");
        check(!evaluateResponse(206,4,"4","bytes 4-7/8","","\"A\"","\"B\"").accepted,"changed entity rejected before append");
        check(evaluateResponse(416,8,"","bytes */8","","","").alreadyComplete,"matching 416 only allows later SHA proof");
        check(evaluateResponse(416,8,"","bytes */9","","","").restartRequired,"wrong 416 cannot authenticate cached file");
        check(!evaluateResponse(206,0,"4","bytes 1-4/8","","","").restartRequired,"malformed fresh response stops instead of infinite clean-restart loop");
        const std::string payload="INERT ZIP-LIKE PAYLOAD NEVER EXECUTED";
        writeText(fixtureRoot/"expected.bin",payload); const auto sha=sha256File((fixtureRoot/"expected.bin").wstring());
        const auto fixtureVersion=[](int offset){return std::to_string(APP_VER_MAJOR)+"."+std::to_string(APP_VER_MINOR)+"."+std::to_string(static_cast<long long>(APP_VER_PATCH)+offset);};
        const std::string targetVersion=fixtureVersion(1),successorVersion=fixtureVersion(2);
        const json args={{"url","https://fixture.invalid/YeManCC.zip"},{"sha256",sha},{"version",targetVersion},{"operationId","fixture"}};
        auto seed = [&](size_t bytes, json metadata) { writeText(dataRoot/"update/package.zip.part",payload.substr(0,bytes)); writeText(dataRoot/"update/package.zip.part.json",metadata.dump()); };
        // Exercise the real new version fence before any transport/cache IO.
        reset("version-admission",{full(payload)});
        for(const std::string badVersion : {std::string(APP_VER_STR),std::string("0.0.34"),std::string("01.0.36")}){
            auto rejectedArgs=args;rejectedArgs["version"]=badVersion;bool rejected=false;
            try{fixtureDownloadUpdate(rejectedArgs);}catch(const std::exception&){rejected=true;}
            check(rejected&&requestCount==0,"actual version admission rejects same/old/malformed versions without authority or network");
        }
        const json metadata={{"url",args["url"]},{"sha256",sha},{"version",targetVersion},{"etag","\"A\""},{"totalBytes",payload.size()}};
        reset("old-file-in-update-directory",{full(payload)});fspath::remove(dataRoot/"update");writeText(dataRoot/"update","old-file-in-directory-slot");
        fixtureDownloadUpdate(args);check(requestCount==1&&readText(dataRoot/"update/package.zip")==payload,"old file in cache directory slot blocked new download");
        reset("completed-cache-offline",{}); writeText(dataRoot/"update/package.zip",payload); fixtureDownloadUpdate(args);
        check(requestCount==0 && progressEvents.back()["phase"]=="downloaded","completed SHA-bound cache reused without HTTP");
        reset("complete-part-offline",{}); seed(payload.size(),metadata); fixtureDownloadUpdate(args);
        check(requestCount==0 && readText(dataRoot/"update/package.zip")==payload,"complete partial finalized offline");
        reset("complete-part-without-total",{}); auto unknown=metadata; unknown.erase("totalBytes"); seed(payload.size(),unknown); fixtureDownloadUpdate(args);
        check(requestCount==0 && readText(dataRoot/"update/package.zip")==payload,"legacy complete partial without saved headers finalized offline");
        auto broken=full(payload); broken.failAfter=4; broken.cancelAtFailure=true;
        // Old resume annotations cannot veto a fresh hash-bound package. This
        // uses the actual handler, transport, file operations and SHA consumer.
        for(const auto& kind:std::vector<std::string>{"future","invalid","wrong-types","duplicate","directory","readonly"}){
            reset("obsolete-cache-"+kind,{full(payload)});seed(4,metadata);
            const auto side=dataRoot/"update/package.zip.part.json";
            if(kind=="directory"){fspath::remove(side);fspath::create_directory(side);writeText(side/"owned-marker.txt","keep isolated old contents");}
            else if(kind=="invalid")writeText(side,"{broken");
            else if(kind=="duplicate")writeText(side,"{\"version\":\"foreign\","+metadata.dump().substr(1));
            else {auto obsolete=metadata;obsolete["schemaVersion"]=99;if(kind=="wrong-types")obsolete["etag"]=17;writeText(side,obsolete.dump());
                if(kind=="readonly")check(SetFileAttributesW(side.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"readonly cache fixture setup");}
            fixtureDownloadUpdate(args);
            check(requestCount==1&&requestHeaders[0].find(L"Range:")==std::wstring::npos&&readText(dataRoot/"update/package.zip")==payload,"obsolete resume metadata blocked download or appended wrong bytes");
        }
        reset("readonly-old-complete",{full(payload)});writeText(dataRoot/"update/package.zip","old-wrong-package");
        check(SetFileAttributesW((dataRoot/"update/package.zip").c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"readonly old package setup");
        fixtureDownloadUpdate(args);check(requestCount==1&&readText(dataRoot/"update/package.zip")==payload,"readonly obsolete package blocked replacement");
        reset("directory-in-complete-slot",{full(payload)});writeText(dataRoot/"update/package.zip/marker.txt","do not recurse");
        fixtureDownloadUpdate(args);check(readText(dataRoot/"update/package.zip")==payload,"old directory in package slot blocked update");
        reset("verified-cache-with-locked-annotations",{});seed(payload.size(),metadata);
        const auto lockedSide=dataRoot/"update/package.zip.part.json";
        HANDLE obsoleteLock=CreateFileW(lockedSide.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(obsoleteLock!=INVALID_HANDLE_VALUE,"locked optional metadata fixture setup");
        auto offline=args;offline["cacheOnly"]=true;bool accepted=false;
        try{fixtureDownloadUpdate(offline);accepted=true;}catch(...){CloseHandle(obsoleteLock);throw;}
        CloseHandle(obsoleteLock);
        check(accepted&&requestCount==0&&readText(dataRoot/"update/package.zip")==payload&&progressEvents.back()["phase"]=="downloaded","optional locked metadata overrode proven completed cache");
        reset("cross-restart",{broken}); bool cancelled=false;
        try {fixtureDownloadUpdate(args);} catch(const std::exception&){cancelled=true;}
        check(cancelled && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"interruption retains exactly the written prefix");
        const auto saved=json::parse(readText(dataRoot/"update/package.zip.part.json"));
        check(saved["etag"]=="\"A\"" && saved["totalBytes"]==payload.size(),"response identity persisted before interrupted first body");
        scenarios={partial(4,payload)}; requestCount=0; cursor=0; requestHeaders.clear(); g_poolCancel=false; fixtureDownloadUpdate(args);
        check(requestCount==1 && requestHeaders[0].find(L"Range: bytes=4-")!=std::wstring::npos,"restart requests actual preserved byte offset");
        check(requestHeaders[0].find(L"If-Range: \"A\"")!=std::wstring::npos && readText(dataRoot/"update/package.zip")==payload,"restart uses stored strong validator and final real SHA-256");
        reset("range-ignored",{full(payload),full(payload)}); seed(4,metadata); fixtureDownloadUpdate(args);
        check(requestCount==2 && requestHeaders[1].find(L"Range:")==std::wstring::npos && readText(dataRoot/"update/package.zip")==payload,"200 resume response restarts without mixing prefix");
        reset("etag-changed",{partial(4,payload,"\"B\""),full(payload,"\"B\"")}); seed(4,metadata); fixtureDownloadUpdate(args);
        check(requestCount==2 && readText(dataRoot/"update/package.zip")==payload,"changed resumed ETag restarts cleanly");
        reset("weak-etag",{partial(4,payload,"W/\"A\"")}); auto weak=metadata;weak["etag"]="W/\"A\"";seed(4,weak);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"If-Range:")==std::wstring::npos,"weak ETag not emitted into request");
        auto shortRange=partial(4,payload);shortRange.body=payload.substr(4,3);shortRange.headers[WINHTTP_QUERY_CONTENT_LENGTH]="3";
        shortRange.headers[WINHTTP_QUERY_CONTENT_RANGE]="bytes 4-6/"+std::to_string(payload.size());
        reset("bounded-segments",{shortRange,partial(7,payload)});seed(4,metadata);fixtureDownloadUpdate(args);
        check(requestCount==2 && requestHeaders[1].find(L"Range: bytes=7-")!=std::wstring::npos && readText(dataRoot/"update/package.zip")==payload,"server-bounded segments advance the preserved offset");
        reset("metadata-write-failure",{partial(4,payload)}); seed(4,metadata); failMetadataWrite=2;bool failed=false;
        try{fixtureDownloadUpdate(args);}catch(const std::exception&){failed=true;}
        check(failed && cursor==0 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"metadata failure stops before appending any bytes");
        reset("wrong-identity",{full(payload)});auto stale=metadata;stale["sha256"]=std::string(64,'0');seed(4,stale);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"partial from a different expected SHA not reused");
        reset("malformed-metadata",{full(payload)});auto malformed=metadata;malformed["totalBytes"]=-1;seed(4,malformed);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"negative metadata length resets before download");
        reset("checksum-restart",{full(std::string(payload.size(),'X')),full(payload)});fixtureDownloadUpdate(args);
        check(requestCount==2 && readText(dataRoot/"update/package.zip")==payload,"wrong complete SHA never becomes install input");
        reset("bad-completed-cache",{full(payload)});writeText(dataRoot/"update/package.zip","STALE CACHE");fixtureDownloadUpdate(args);
        check(requestCount==1 && readText(dataRoot/"update/package.zip")==payload,"wrong completed cache hash forces a fresh request");
        reset("large-header-real-transport",{full(payload)}); scenarios[0].headers[WINHTTP_QUERY_CONTENT_LENGTH]="5000000000";
        const auto large=downloadFileAttempt(args["url"].get<std::string>(),(dataRoot/"update/package.zip.part").wstring(),{},0,0,{},true);
        check(!large.ok && large.expectedBytes==5000000000ULL && readText(dataRoot/"update/package.zip.part")==payload,"real extracted WinHTTP path retains 64-bit total without allocating it");
        reset("initial-metadata-failure",{}); failMetadataWrite=1;failed=false;
        try{fixtureDownloadUpdate(args);}catch(const std::exception&){failed=true;}
        check(failed && requestCount==0 && progressEvents.back()["phase"]=="failed" && progressEvents.back()["version"]==targetVersion,"initial cache preflight failure publishes the current failed operation");
        reset("url-binding",{full(payload)});auto wrongUrl=metadata;wrongUrl["url"]="https://other.invalid/YeManCC.zip";seed(4,wrongUrl);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"different original URL cannot reuse a partial file");
        reset("version-binding",{full(payload)});auto wrongVersion=metadata;wrongVersion["version"]=successorVersion;seed(4,wrongVersion);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"different version cannot reuse a partial file");
        reset("oversized-metadata",{full(payload)});writeText(dataRoot/"update/package.zip.part",payload.substr(0,4));writeText(dataRoot/"update/package.zip.part.json",std::string(65537,'x'));fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"oversized metadata discarded without unbounded JSON read");
        reset("typed-metadata",{full(payload)});auto wrongType=metadata;wrongType["etag"]=1;seed(4,wrongType);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"wrong metadata optional type resets safely");
        reset("local-offset-mismatch",{});writeText(dataRoot/"update/package.zip.part",payload.substr(0,4));
        const auto wrongOffset=downloadFileAttempt(args["url"].get<std::string>(),(dataRoot/"update/package.zip.part").wstring(),{},0,5,"\"A\"",true);
        check(!wrongOffset.ok && wrongOffset.restartRequired && requestCount==0 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"changed local file size rejected before any request or append");
        Scenario unavailable;unavailable.status=503;unavailable.headers[WINHTTP_QUERY_ETAG]="\"WRONG-ERROR-PAGE\"";
        reset("error-page-validator",{unavailable,partial(4,payload)});seed(4,metadata);fixtureDownloadUpdate(args);
        check(requestHeaders[1].find(L"If-Range: \"A\"")!=std::wstring::npos,"HTTP error headers cannot replace the accepted partial-file validator");
        reset("date-not-proven-strong",{partial(4,payload)});auto dated=metadata;dated.erase("etag");dated["lastModified"]="Mon, 01 Jan 2024 00:00:00 GMT";seed(4,dated);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"If-Range:")==std::wstring::npos && readText(dataRoot/"update/package.zip")==payload,"unproven modification date not emitted as If-Range; full SHA stays mandatory");
        reset("encoded-rejected-before-append",{partial(4,payload)});scenarios[0].headers[WINHTTP_QUERY_CONTENT_ENCODING]="gzip";seed(4,metadata);
        const auto encoded=downloadFileAttempt(args["url"].get<std::string>(),(dataRoot/"update/package.zip.part").wstring(),{},0,4,"\"A\"",true);
        check(!encoded.ok && encoded.restartRequired && cursor==0 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"encoded body never appended to the approved prefix");
        reset("matching-416-sha-still-required",{});Scenario complete;complete.status=416;complete.headers[WINHTTP_QUERY_CONTENT_RANGE]="bytes */"+std::to_string(payload.size());scenarios={complete};seed(payload.size(),metadata);
        const auto completeResult=downloadFileAttempt(args["url"].get<std::string>(),(dataRoot/"update/package.zip.part").wstring(),{},0,payload.size(),"\"A\"",true);
        check(completeResult.ok && completeResult.receivedBytes==payload.size() && sha256File((dataRoot/"update/package.zip.part").wstring())==sha,"matching 416 does not bypass file SHA identity proof");
        reset("rejected-response-no-append",{partial(4,payload,"\"B\"")});seed(4,metadata);
        const auto direct=downloadFileAttempt(args["url"].get<std::string>(),(dataRoot/"update/package.zip.part").wstring(),{},0,4,"\"A\"",true);
        check(!direct.ok && direct.restartRequired && cursor==0 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"rejected resumed entity does not mutate prior file");
        const json backgroundArgs = [&]{auto value=args;value["background"]=true;return value;}();
        reset("background-round-bounded",{unavailable,unavailable,unavailable});seed(4,metadata);bool exhausted=false;
        try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception& error){exhausted=std::string(error.what()).find("round exhausted")!=std::string::npos;}
        check(exhausted && requestCount==3 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"background stops after three attempts and retains Range prefix");
        check(g_updateDownloadControl.status().operation.empty(),"failed round releases live native ownership");
        reset("cache-only-missing",{});auto cacheArgs=backgroundArgs;cacheArgs["cacheOnly"]=true;bool missing=false;
        try{fixtureDownloadUpdate(cacheArgs);}catch(const std::exception&){missing=true;}
        check(missing && requestCount==0,"startup cache validation cannot fall through to HTTP");
        reset("cache-only-valid",{});writeText(dataRoot/"update/package.zip",payload);fixtureDownloadUpdate(cacheArgs);
        check(requestCount==0 && progressEvents.back()["phase"]=="downloaded","offline ready receipt requires real cached SHA proof");
        reset("cache-only-corrupt",{});writeText(dataRoot/"update/package.zip","CORRUPT");bool corrupt=false;
        try{fixtureDownloadUpdate(cacheArgs);}catch(const std::exception&){corrupt=true;}
        check(corrupt && requestCount==0,"corrupt cached archive is never marked ready or downloaded during cache-only probe");
        reset("cache-only-complete-part",{});seed(payload.size(),metadata);fixtureDownloadUpdate(cacheArgs);
        check(requestCount==0 && readText(dataRoot/"update/package.zip")==payload,"cache-only probe heals final-byte crash without HTTP");
        reset("game-before-background",{});seed(4,metadata);g_updateGameDetected=true;bool gameBlocked=false;
        try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){gameBlocked=true;}
        check(gameBlocked && requestCount==0 && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"detected game refuses background work before hashing network or cache cleanup");
        reset("stale-game-observation",{});fixtureTick+=15001;bool staleBlocked=false;
        try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){staleBlocked=true;}
        check(staleBlocked&&requestCount==0,"stale game observation fails closed before any background IO");
        reset("unknown-game-state",{});g_updateBackgroundAllowed=false;bool unknownBlocked=false;
        try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){unknownBlocked=true;}
        check(unknownBlocked&&requestCount==0,"unknown or unacknowledged game activity fails closed");
        reset("game-during-body",{});seed(4,metadata);auto gameResponse=partial(4,payload);gameResponse.gameAfterFirstRead=true;scenarios={gameResponse};bool bodyPaused=false;
        try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){bodyPaused=true;}
        check(bodyPaused && readText(dataRoot/"update/package.zip.part")==payload.substr(0,4),"game appearing during read prevents append and preserves prior Range prefix");
        g_updateGameDetected=false;scenarios={partial(4,payload)};requestCount=0;cursor=0;fixtureDownloadUpdate(backgroundArgs);
        check(requestHeaders.back().find(L"Range: bytes=4-")!=std::wstring::npos&&readText(dataRoot/"update/package.zip")==payload,"game exit resumes the retained valid prefix");
        check(sha256FileCancellable((dataRoot/"update/package.zip").wstring(),[]{return true;}).empty(),"background SHA hashing has a cancellation boundary between file reads");
        for(const bool manualExit:{false,true}){
        reset(manualExit?"manual-exit-while-headers-pending":"game-while-headers-pending",{});auto stalledHeaders=full(payload);stalledHeaders.stallHeaders=true;scenarios={stalledHeaders};
        fixtureHeadersEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(fixtureHeadersEvent!=nullptr,"fixture creates own header wait event");
        bool signalled=false,headerPaused=false;std::thread pauseHeaders([&]{signalled=WaitForSingleObject(fixtureHeadersEvent,1500)==WAIT_OBJECT_0;if(signalled){if(manualExit)g_poolCancel=true;else g_updateGameDetected=true;}});
        try{fixtureDownloadUpdate(manualExit?args:backgroundArgs);}catch(const std::exception& error){headerPaused=std::string(error.what()).find(manualExit?"cancelled":"paused")!=std::string::npos;}
        pauseHeaders.join();CloseHandle(fixtureHeadersEvent);fixtureHeadersEvent=nullptr;
        check(signalled&&headerPaused&&requestCount==1,"pending native headers stop on game admission without receiving server headers");
        check(!fspath::exists(dataRoot/"update/package.zip")&&g_updateDownloadControl.status().operation.empty(),"cancelled header wait releases original owner without a ready cache");
        check(fixtureClosedHandles==3&&fixtureCallbackContext==0,"header cancellation closes each adopted native handle and retires callback context once");
        }
        for(const bool failRegistration:{true,false}){
            reset(failRegistration?"async-callback-registration-failure":"async-context-failure",{});auto response=full(payload);
            response.failCallbackRegistration=failRegistration;response.failCallbackContext=!failRegistration;scenarios={response};
            const auto failed=downloadFileAttempt(args["url"],(dataRoot/"update/package.zip.part").wstring(),{},0,0,{},true,{},0,[]{return false;});
            check(!failed.ok&&fixtureClosedHandles==3&&fixtureCallbackContext==0,"failed async construction releases all three handles and leaves no callback owner");
        }
        reset("manual-during-game",{full(payload)});g_updateGameDetected=true;g_updateBackgroundAllowed=false;fixtureDownloadUpdate(args);
        check(requestCount==1&&readText(dataRoot/"update/package.zip")==payload,"explicit user update retains its own admission separate from background pause");
        reset("policy-roundtrip",{});fixtureRegisterPredownload();
        const auto readPolicy=[&](){return policyHandlers.at("app.updatePredownloadRead")(json::object());};
        check(readPolicy()["enabled"]==true && readPolicy()["bootId"]=="fixture-boot-A","policy defaults on with explicit OS boot identity");
        policyHandlers.at("app.updatePredownloadSet")({{"enabled",false}});
        check(readPolicy()["enabled"]==false,"disabled preference persists with actual atomic file readback");
        policyHandlers.at("app.updatePredownloadSchedule")({{"failureCount",2},{"nextCheckAt",0}});
        check(readPolicy()["enabled"]==false && readPolicy()["failureCount"]==2,"schedule write preserves user preference");
        auto notifyArgs=args;notifyArgs["bootId"]="fixture-boot-A";
        check(policyHandlers.at("app.updatePredownloadNotified")(notifyArgs)==false,"notice cannot be acknowledged without a verified ready package receipt");
        updateProgressPost({{"phase","downloaded"},{"version",args["version"]},{"sha256",args["sha256"]}});
        check(policyHandlers.at("app.updatePredownloadNotified")(notifyArgs)==true && readPolicy()["lastNotifiedBoot"]=="fixture-boot-A","accepted notice boot receipt persists alongside disabled preference");
        notifyArgs["bootId"]="another-boot";
        check(policyHandlers.at("app.updatePredownloadNotified")(notifyArgs)==false,"late receipt for another boot cannot overwrite deduplication");
        policyHandlers.at("app.updatePredownloadTarget")(fixtureTarget(args));
        check(readPolicy()["targetVersion"]==args["version"] && readPolicy()["nextCheckAt"]==0 && readPolicy()["lastNotifiedBoot"]=="fixture-boot-A","successor identity is durable before cancellation and preserves boot receipt");
        bool disabledBlocked=false;try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){disabledBlocked=true;}
        check(disabledBlocked&&requestCount==0,"persisted disabled policy rejects late queued background work");
        policyHandlers.at("app.updatePredownloadActivity")({{"known",true},{"idle",false}});
        check(!g_updateBackgroundAllowed.load(),"game activity gate revokes native background admission");
        policyHandlers.at("app.updatePredownloadActivity")({{"known",true},{"idle",true}});
        auto before=readText(updatePredownloadPolicyPath());failMetadataWrite=metadataWrites+1;bool saveFailed=false;
        try{policyHandlers.at("app.updatePredownloadSet")({{"enabled",true}});}catch(const std::exception&){saveFailed=true;}
        check(saveFailed && readText(updatePredownloadPolicyPath())==before,"atomic persistence failure cannot report a fictitious enabled state");failMetadataWrite=0;
        writeText(updatePredownloadPolicyPath(),"{broken JSON");check(readPolicy()["enabled"]==true&&readPolicy().contains("configReadError"),"malformed disposable policy blocks default background preparation");
        writeText(updatePredownloadPolicyPath(),"");check(readPolicy()["enabled"]==true&&readPolicy().contains("configReadError"),"empty old policy blocks default background preparation");
        writeText(updatePredownloadPolicyPath(),std::string(65537,'x'));check(readPolicy()["enabled"]==true&&readPolicy().contains("configReadError"),"oversized disposable policy blocks bounded default recovery");
        policyHandlers.at("app.updatePredownloadSet")({{"enabled",false}});check(readPolicy()["enabled"]==false,"recovered policy can be saved and read back normally");
        Control control;
        { Control::Lease lease(control,"background-owner",true);
          check(!control.cancel("stale-owner") && !control.cancelled("background-owner"),"stale operation cancellation cannot affect a newer owner");
          check(control.cancel("background-owner") && control.cancelled("background-owner"),"matching background owner can be cancelled"); }
        check(control.status().operation.empty(),"RAII clears operation and cancellation state");
        { Control::Lease lease(control,"manual-owner",false);check(!control.cancel("manual-owner"),"background off cannot cancel explicit manual update"); }
        // Real filesystem cache maintenance; every path is below this run's
        // fresh fixture directory. No installed application state is touched.
        check(control.retire("queued-owner"),"queued background owner can be fenced before it starts");
        bool retiredStart=false;try {Control::Lease lease(control,"queued-owner",true);}catch(const std::exception&){retiredStart=true;}
        check(retiredStart && control.status().operation.empty(),"retired queued background POST never acquires ownership");
        {Control::Lease lease(control,"manual-owner",false);check(!control.retire("manual-owner")&&!control.cancelled("manual-owner"),"retire cannot stop explicit manual download");
         check(control.retire("unrelated-owner")&&control.status().operation=="manual-owner","fencing unrelated queued work preserves active manual owner");}
        {Control::Lease lease(control,"active-background",true);check(control.retire("active-background")&&control.cancelled("active-background"),"retirement requests stop without clearing live lease");check(control.status().operation=="active-background","retired live owner retains ownership until RAII exit");}
        reset("cache-clear",{});fixtureTransaction=json::object();
        auto cacheRoot=dataRoot/"update";fspath::create_directories(cacheRoot/"staging");fspath::create_directories(cacheRoot/"rollback");
        writeText(cacheRoot/"staging/keep","staging");writeText(cacheRoot/"rollback/keep","rollback");writeText(cacheRoot/"transaction.json","transaction");
        policyHandlers.at("app.updatePredownloadSet")({{"enabled",false}});
        policyHandlers.at("app.updatePredownloadTarget")(fixtureTarget(args));
        writeText(cacheRoot/"package.zip","123456");writeText(cacheRoot/"package.zip.part","1234");writeText(cacheRoot/"package.zip.part.json","12");
        const auto clearArgs=json{{"operationId","maintenance-owner"}};
        {std::unique_lock<std::mutex> lease(g_updateOperationMtx);const auto busy=policyHandlers.at("app.clearUpdateCache")(clearArgs);
         check(busy["busy"]==true&&busy["ok"]==false&&fspath::exists(cacheRoot/"package.zip"),"cache clear try-lock never waits or deletes under active download/install");}
        fixtureTransaction={{"stage","install"},{"phase","interrupted"}};
        check(policyHandlers.at("app.clearUpdateCache")(clearArgs)["busy"]==true&&fspath::exists(cacheRoot/"package.zip"),"incomplete installation preserves recovery package");fixtureTransaction=json::object();
        const auto cleared=policyHandlers.at("app.clearUpdateCache")(clearArgs);
        check(cleared["ok"]==true&&cleared["bytesFreed"]==12,"actual cache clear deletes only complete/partial/sidecar and counts removed bytes");
        check(!fspath::exists(cacheRoot/"package.zip")&&!fspath::exists(cacheRoot/"package.zip.part")&&!fspath::exists(cacheRoot/"package.zip.part.json"),"all three cache leaves deleted");
        check(readText(cacheRoot/"staging/keep")=="staging"&&readText(cacheRoot/"rollback/keep")=="rollback"&&readText(cacheRoot/"transaction.json")=="transaction","staging rollback and journal remain unchanged");
        const auto savedClear=readPolicy();
        check(savedClear["enabled"]==false&&!savedClear.contains("targetVersion")&&!savedClear.contains("targetSha256")&&savedClear["nextCheckAt"].get<int64_t>()>static_cast<int64_t>(time(nullptr))*1000+590000,"cleanup preserves enabled preference and starts ten minute quiet window");
        check(policyHandlers.at("app.updatePredownloadTarget")(fixtureTarget(args))==false&&!readPolicy().contains("targetVersion"),"late successor hint cannot revive cleared cache within quiet window");
        check(policyHandlers.at("app.clearUpdateCache")(clearArgs)["bytesFreed"]==0,"empty cache clear is idempotent");
        policyHandlers.at("app.updatePredownloadSet")({{"enabled",true}});
        auto unknownQueued=backgroundArgs;unknownQueued["operationId"]="queued-before-renderer-loss";
        bool quietBlocked=false;try{fixtureDownloadUpdate(unknownQueued);}catch(const std::exception& error){quietBlocked=std::string(error.what()).find("paused after cache cleanup")!=std::string::npos;}
        check(quietBlocked&&requestCount==0,"persisted quiet policy rejects unidentified queued background work after renderer loss");
        writeText(cacheRoot/"package.zip",payload);fixtureDownloadUpdate(args);
        check(requestCount==0&&progressEvents.back()["phase"]=="downloaded","manual cache verification is allowed during cleanup quiet period");
        policyHandlers.at("app.clearUpdateCache")(clearArgs);
        writeText(cacheRoot/"package.zip","locked");writeText(cacheRoot/"package.zip.part","remove");
        HANDLE cacheLock=CreateFileW((cacheRoot/"package.zip").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(cacheLock!=INVALID_HANDLE_VALUE,"fixture holds cache file open without delete sharing");
        const auto locked=policyHandlers.at("app.clearUpdateCache")(clearArgs);CloseHandle(cacheLock);
        check(locked["ok"]==false&&locked["bytesFreed"]==6&&fspath::exists(cacheRoot/"package.zip")&&!fspath::exists(cacheRoot/"package.zip.part"),"partial clear reports locked file honestly and removes unlocked file");
        SetFileAttributesW((cacheRoot/"package.zip").c_str(),FILE_ATTRIBUTE_READONLY);
        check(!ymcc::update_download::clearPackageCache(cacheRoot).errors.empty()&&fspath::exists(cacheRoot/"package.zip"),"read-only deletion failure cannot be reported as success");
        SetFileAttributesW((cacheRoot/"package.zip").c_str(),FILE_ATTRIBUTE_NORMAL);
        ymcc::update_download::clearPackageCache(cacheRoot);fspath::create_directories(cacheRoot/"package.zip");writeText(cacheRoot/"package.zip/keep","keep");
        check(!ymcc::update_download::clearPackageCache(cacheRoot).errors.empty()&&fspath::exists(cacheRoot/"package.zip/keep"),"cache leaf directory never recursively deleted");
        check(!ymcc::update_download::clearPackageCache(cacheRoot/"transaction.json").errors.empty(),"non-directory cache root rejected");
        check(ymcc::update_download::clearPackageCache(dataRoot/"missing-cache").errors.empty(),"missing cache root is harmless");
        const auto outside=dataRoot/"outside-cache";fspath::create_directories(outside);writeText(outside/"package.zip","outside bytes");
        const auto linkedRoot=dataRoot/"linked-cache";
        check(makeFixtureJunction(linkedRoot,outside),"fixture creates real directory reparse point");
        check(!ymcc::update_download::clearPackageCache(linkedRoot).errors.empty()&&readText(outside/"package.zip")=="outside bytes","linked cache root refuses deletion without following the junction");
        const auto leafCache=dataRoot/"leaf-cache";fspath::create_directories(leafCache);
        check(makeFixtureJunction(leafCache/"package.zip",outside),"fixture creates linked cache leaf");
        check(!ymcc::update_download::clearPackageCache(leafCache).errors.empty()&&readText(outside/"package.zip")=="outside bytes","linked cache leaf is never traversed or recursively removed");
                const auto retiredReply=policyHandlers.at("app.retireUpdateDownload")({{"operationId","late-background"}});
        check(retiredReply["retired"]==true&&retiredReply["active"]==false,"real retirement handler proves queued owner inactive");
        bool blockedQueued=false;auto queuedArgs=backgroundArgs;queuedArgs["operationId"]="late-background";
        policyHandlers.at("app.updatePredownloadSet")({{"enabled",true}});
        {auto policy=updatePredownloadPolicyRead();policy["cacheQuietUntil"]=0;updatePredownloadPolicyWrite(policy);}
        try{fixtureDownloadUpdate(queuedArgs);}catch(const std::exception&){blockedQueued=true;}
        check(blockedQueued&&requestCount==0,"real downloader rejects retired queued POST without network or cache writes");
                json report={{"status","PASS"},{"checks",checks+1},{"scope","Extracted production policy/WinHTTP downloader/update handler/SHA-256. All network calls mocked; only unique fixture files touched. No product, installation, remote request or hardware."}};
        writeText(fixtureRoot/"result.json",report.dump(2));
        std::cout<<"UPDATER_RESUME_NATIVE_SELFTEST_OK checks="<<checks<<"\n";
        return 0;
#endif
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
}
