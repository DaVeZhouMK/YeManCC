// Inert transport: all WinHTTP calls are replaced; only fixture file IO and SHA-256 are real.
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include "json.hpp"
#include "update_download_resume.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
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
struct Scenario {
    DWORD status = 200;
    std::map<DWORD, std::string> headers;
    std::string body;
    size_t failAfter = SIZE_MAX;
    bool cancelAtFailure = false;
};
static std::vector<Scenario> scenarios;
static size_t requestCount = 0, cursor = 0;
static Scenario current;
static std::vector<std::wstring> requestHeaders;
static std::atomic<bool> g_poolCancel{false};
static std::mutex g_updateOperationMtx;
static ULONGLONG fixtureTick = 0;
static fspath::path fixtureRoot, dataRoot;
static std::vector<json> progressEvents;
static int metadataWrites = 0, failMetadataWrite = 0;
static HINTERNET fakeWinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) {
    if (requestCount >= scenarios.size()) throw std::runtime_error("FIXTURE_FORBIDS_UNPLANNED_NETWORK_REQUEST");
    current = scenarios[requestCount++]; cursor = 0; requestHeaders.emplace_back();
    return reinterpret_cast<HINTERNET>(1);
}
static HINTERNET fakeWinHttpConnect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) { return reinterpret_cast<HINTERNET>(2); }
static HINTERNET fakeWinHttpOpenRequest(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) { return reinterpret_cast<HINTERNET>(3); }
static BOOL fakeWinHttpSetOption(HINTERNET, DWORD, LPVOID, DWORD) { return TRUE; }
static BOOL fakeWinHttpAddRequestHeaders(HINTERNET, LPCWSTR value, DWORD, DWORD) { requestHeaders.back() += value; return TRUE; }
static BOOL fakeWinHttpSendRequest(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) { return TRUE; }
static BOOL fakeWinHttpReceiveResponse(HINTERNET, LPVOID) { return TRUE; }
static BOOL fakeWinHttpCloseHandle(HINTERNET) { return TRUE; }
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
    *available = static_cast<DWORD>((std::min)(current.body.size() - cursor, current.failAfter - cursor));
    return TRUE;
}
static BOOL fakeWinHttpReadData(HINTERNET, LPVOID output, DWORD wanted, LPDWORD read) {
    *read = static_cast<DWORD>((std::min)(static_cast<size_t>(wanted), current.body.size() - cursor));
    std::memcpy(output, current.body.data() + cursor, *read); cursor += *read; return TRUE;
}
static ULONGLONG fakeGetTickCount64() { return ++fixtureTick; }
static void fakeSleep(DWORD ms) { fixtureTick += ms; }
#define WinHttpOpen fakeWinHttpOpen
#define WinHttpConnect fakeWinHttpConnect
#define WinHttpOpenRequest fakeWinHttpOpenRequest
#define WinHttpSetOption fakeWinHttpSetOption
#define WinHttpAddRequestHeaders fakeWinHttpAddRequestHeaders
#define WinHttpSendRequest fakeWinHttpSendRequest
#define WinHttpReceiveResponse fakeWinHttpReceiveResponse
#define WinHttpCloseHandle fakeWinHttpCloseHandle
#define WinHttpQueryHeaders fakeWinHttpQueryHeaders
#define WinHttpQueryDataAvailable fakeWinHttpQueryDataAvailable
#define WinHttpReadData fakeWinHttpReadData
#define GetTickCount64 fakeGetTickCount64
#define Sleep fakeSleep
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
//__PRODUCTION_RESULT_DECLARATION__
//__PRODUCTION_TRANSPORT__
//__PRODUCTION_SHA256__
static std::wstring app_data_dir() { return dataRoot.wstring(); }
static bool isStrictUpdateSha256(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}
static void requireNewerUpdateVersion(const std::string& version) {
    if (version != "0.0.34") throw std::runtime_error("Fixture expects a synthetic newer version");
}
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
static json fixtureDownloadUpdate(const json& a) {
//__PRODUCTION_UPDATE_HANDLER__
}
static void reset(const std::string& name, std::vector<Scenario> responses) {
    dataRoot = fixtureRoot / name; fspath::create_directories(dataRoot / "update");
    scenarios = std::move(responses); requestCount = cursor = 0; requestHeaders.clear(); progressEvents.clear();
    g_poolCancel = false; metadataWrites = failMetadataWrite = 0;
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
        const json args={{"url","https://fixture.invalid/YeManCC.zip"},{"sha256",sha},{"version","0.0.34"},{"operationId","fixture"}};
        auto seed = [&](size_t bytes, json metadata) { writeText(dataRoot/"update/package.zip.part",payload.substr(0,bytes)); writeText(dataRoot/"update/package.zip.part.json",metadata.dump()); };
        const json metadata={{"url",args["url"]},{"sha256",sha},{"version","0.0.34"},{"etag","\"A\""},{"totalBytes",payload.size()}};
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
        check(failed && requestCount==0 && progressEvents.back()["phase"]=="failed" && progressEvents.back()["version"]=="0.0.34","initial cache preflight failure publishes the current failed operation");
        reset("url-binding",{full(payload)});auto wrongUrl=metadata;wrongUrl["url"]="https://other.invalid/YeManCC.zip";seed(4,wrongUrl);fixtureDownloadUpdate(args);
        check(requestHeaders[0].find(L"Range:")==std::wstring::npos,"different original URL cannot reuse a partial file");
        reset("version-binding",{full(payload)});auto wrongVersion=metadata;wrongVersion["version"]="0.0.35";seed(4,wrongVersion);fixtureDownloadUpdate(args);
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
        json report={{"status","PASS"},{"checks",checks+1},{"scope","Extracted production policy/WinHTTP downloader/update handler/SHA-256. All network calls mocked; only unique fixture files touched. No product, installation, remote request or hardware."}};
        writeText(fixtureRoot/"result.json",report.dump(2));
        std::cout<<"UPDATER_RESUME_NATIVE_SELFTEST_OK checks="<<checks<<"\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
}
