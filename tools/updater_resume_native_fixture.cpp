// Inert transport: all WinHTTP calls are replaced; only fixture file IO and SHA-256 are real.
// Match the actual native preamble before including the Windows SDK.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <winioctl.h>
#include <wincrypt.h>
#include "json.hpp"
#include "update_download_resume.h"
#include "update_download_control.h"
#include "update_cache_maintenance.h"
#include "update_install_recovery.h"
#include "version.h"
#include "shared_game_media_capacity.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
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
    bool stallHeaders = false, failCallbackRegistration = false, failCallbackContext = false;
};
static std::vector<Scenario> scenarios;
static size_t requestCount = 0, cursor = 0;
static Scenario current;
static std::vector<std::wstring> requestHeaders;
static std::atomic<bool> g_poolCancel{false};
static std::mutex g_updateOperationMtx;
static ymcc::update_download::Control g_updateDownloadControl;
static std::atomic<bool> g_updateBackgroundAllowed{true},g_updateGameDetected{false};
static ULONGLONG fixtureTick = 1000;
static std::atomic<ULONGLONG> g_updateGameObservationTick{1000};
static fspath::path fixtureRoot, dataRoot;
static std::vector<json> progressEvents;
static int metadataWrites = 0, failMetadataWrite = 0;
static HANDLE fixtureHeadersEvent=nullptr;
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
    if(current.stallHeaders){if(fixtureHeadersEvent)SetEvent(fixtureHeadersEvent);return TRUE;}
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
    if(cursor>=current.failAfter){if(current.cancelAtFailure)g_poolCancel=true;SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);return FALSE;}
    const DWORD bytes=static_cast<DWORD>((std::min)({static_cast<size_t>(wanted),current.body.size()-cursor,current.failAfter-cursor}));
    std::memcpy(output,current.body.data()+cursor,bytes);cursor+=bytes;if(current.gameAfterFirstRead)g_updateGameDetected=true;
    if(read)*read=bytes;else if(fixtureCallback&&fixtureCallbackContext)fixtureCallback(reinterpret_cast<HINTERNET>(3),fixtureCallbackContext,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,output,bytes);
    return TRUE;
}
static ULONGLONG fakeGetTickCount64() { return ++fixtureTick; }
static void fakeSleep(DWORD ms) { fixtureTick += ms; }
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
#include "http_request_deadline.h"
static constexpr DWORD DEFAULT_HTTP_TIMEOUT_MS = 5000;
static constexpr DWORD UPDATE_HTTP_IO_TIMEOUT_MS = 120000;
static constexpr DWORD UPDATE_RETRY_INTERVAL_MS = 5000;
static std::wstring U2W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::string W2U(const std::wstring& s) { return std::string(s.begin(), s.end()); }
static std::string trim_ascii(const std::string& s) { return ymcc::update_download::trimHeader(s); }
static std::string ascii_lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
static void setHttpTimeouts(HINTERNET, DWORD) {}
static std::string downloadWinHttpError(const char* api, DWORD error) { return std::string(api) + ':' + std::to_string(error); }
static bool isSteamHostName(const wchar_t*) { return false; }
static bool crackHttpUrl(const std::string& url, URL_COMPONENTS& uc, wchar_t* host, wchar_t*, wchar_t*, std::wstring& objectPath) {
    if (url.rfind("https://", 0) != 0) return false;
    uc.nScheme = INTERNET_SCHEME_HTTPS; uc.nPort = 443; std::wcscpy(host, L"fixture.invalid"); objectPath = L"/YeManCC.zip"; return true;
}
//__PRODUCTION_ACTIVITY_ADMISSION__
//__PRODUCTION_RESULT_DECLARATION__
//__PRODUCTION_TRANSPORT__
static std::string sha256File(const std::wstring& path);
//__PRODUCTION_SHA256__
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
static bool sgWriteFileAtomic(const std::wstring& path, const std::string& text) {
    ++metadataWrites;
    if (failMetadataWrite > 0 && metadataWrites == failMetadataWrite) return false;
    auto temporary = path + L".fixture.tmp"; writeText(temporary, text);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    return true;
}
static std::string sgReadFile(const std::wstring& path) { return readText(path); }
static void updateProgressPost(const json& record) { progressEvents.push_back(record); }
namespace ymcc::dedicatedmemory { static std::string bootIdentity() { return "fixture-boot-A"; } }
static json updateProgressRead() { return progressEvents.empty() ? json::object() : progressEvents.back(); }
//__PRODUCTION_PREDOWNLOAD_POLICY__
static json fixtureTransaction=json::object();
static json updateStateReadWithTransaction(){return fixtureTransaction.empty()?(progressEvents.empty()?json::object():progressEvents.back()):fixtureTransaction;}
static std::map<std::string,std::function<json(const json&)>> policyHandlers;
static void ipc_on(const std::string& name,std::function<json(const json&)> handler) { policyHandlers[name]=std::move(handler); }
static void fixtureRegisterPredownload() {
//__PRODUCTION_PREDOWNLOAD_REGISTRATION__
//__PRODUCTION_CACHE_REGISTRATION__
}
static json fixtureDownloadUpdate(const json& a) {
//__PRODUCTION_UPDATE_HANDLER__
}
static void reset(const std::string& name, std::vector<Scenario> responses) {
    dataRoot = fixtureRoot / name; fspath::create_directories(dataRoot / "update");
    POWER_CONTROL_DIR=(dataRoot/"PowerControl").wstring();fixtureRecoveryJournal=json::object();
    scenarios = std::move(responses); requestCount = cursor = 0; requestHeaders.clear(); progressEvents.clear(); fixtureClosedHandles=0;
    g_poolCancel = false; g_updateBackgroundAllowed=true;g_updateGameDetected=false; g_updateGameObservationTick=fixtureTick; metadataWrites = failMetadataWrite = 0;
}
static Scenario full(const std::string& body, const std::string& etag = "\"A\"") {
    Scenario s; s.body = body; s.headers = {{WINHTTP_QUERY_CONTENT_LENGTH,std::to_string(body.size())},{WINHTTP_QUERY_ETAG,etag}}; return s;
}
static Scenario partial(size_t offset, const std::string& whole, const std::string& etag = "\"A\"") {
    auto s = full(whole.substr(offset), etag); s.status = 206;
    s.headers[WINHTTP_QUERY_CONTENT_RANGE] = "bytes " + std::to_string(offset) + '-' + std::to_string(whole.size()-1) + '/' + std::to_string(whole.size()); return s;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("UNIQUE_FIXTURE_ROOT_REQUIRED");
        fixtureRoot = fspath::absolute(argv[1]);
        wchar_t ownExe[32768]{};if(!GetModuleFileNameW(nullptr,ownExe,32768))throw std::runtime_error("FIXTURE_EXE_IDENTITY_MISSING");fixtureExe=ownExe;
        if (fspath::exists(fixtureRoot)) throw std::runtime_error("FIXTURE_ROOT_ALREADY_EXISTS");
        fspath::create_directories(fixtureRoot);
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
        reset("completed-cache-offline",{}); writeText(dataRoot/"update/package.zip",payload); fixtureDownloadUpdate(args);
        check(requestCount==0 && progressEvents.back()["phase"]=="downloaded","completed SHA-bound cache reused without HTTP");
        reset("complete-part-offline",{}); seed(payload.size(),metadata); fixtureDownloadUpdate(args);
        check(requestCount==0 && readText(dataRoot/"update/package.zip")==payload,"complete partial finalized offline");
        reset("complete-part-without-total",{}); auto unknown=metadata; unknown.erase("totalBytes"); seed(payload.size(),unknown); fixtureDownloadUpdate(args);
        check(requestCount==0 && readText(dataRoot/"update/package.zip")==payload,"legacy complete partial without saved headers finalized offline");
        auto broken=full(payload); broken.failAfter=4; broken.cancelAtFailure=true;
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
        policyHandlers.at("app.updatePredownloadTarget")({{"version",args["version"]},{"sha256",args["sha256"]}});
        check(readPolicy()["targetVersion"]==args["version"] && readPolicy()["nextCheckAt"]==0 && readPolicy()["lastNotifiedBoot"]=="fixture-boot-A","successor identity is durable before cancellation and preserves boot receipt");
        bool disabledBlocked=false;try{fixtureDownloadUpdate(backgroundArgs);}catch(const std::exception&){disabledBlocked=true;}
        check(disabledBlocked&&requestCount==0,"persisted disabled policy rejects late queued background work");
        policyHandlers.at("app.updatePredownloadActivity")({{"known",true},{"idle",false}});
        check(!g_updateBackgroundAllowed.load(),"game activity gate revokes native background admission");
        policyHandlers.at("app.updatePredownloadActivity")({{"known",true},{"idle",true}});
        auto before=readText(updatePredownloadPolicyPath());failMetadataWrite=metadataWrites+1;bool saveFailed=false;
        try{policyHandlers.at("app.updatePredownloadSet")({{"enabled",true}});}catch(const std::exception&){saveFailed=true;}
        check(saveFailed && readText(updatePredownloadPolicyPath())==before,"atomic persistence failure cannot report a fictitious enabled state");failMetadataWrite=0;
        writeText(updatePredownloadPolicyPath(),"{broken JSON");check(readPolicy()["enabled"]==true,"malformed policy recovers without blocking updater");
        writeText(updatePredownloadPolicyPath(),"");check(readPolicy()["enabled"]==true,"zero-byte policy recovers after an interrupted write");
        writeText(updatePredownloadPolicyPath(),std::string(65537,'x'));check(readPolicy()["enabled"]==true,"oversized policy is bounded before JSON IO");
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
        policyHandlers.at("app.updatePredownloadTarget")({{"version",args["version"]},{"sha256",args["sha256"]}});
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
        check(policyHandlers.at("app.updatePredownloadTarget")({{"version",args["version"]},{"sha256",args["sha256"]}})==false&&!readPolicy().contains("targetVersion"),"late successor hint cannot revive cleared cache within quiet window");
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
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
}
