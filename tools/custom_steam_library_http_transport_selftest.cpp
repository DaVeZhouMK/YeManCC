// Runs the unmodified production WinHTTP request/read/retry path. Every API
// capable of HTTP I/O is replaced before including the worker; URL/header
// parsing, transport classification, scheduling and persistence stay real.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace httpPorts {
struct Response {
    DWORD status = 200;
    std::optional<DWORD> length;
    std::wstring retryAfter;
    std::string body = "{\"ok\":true}";
    DWORD receiveError = 0;
    DWORD readError = 0;
    DWORD chunk = 3;
    bool availableAtEof = false;
    DWORD prepareMs = 0, sendMs = 0, receiveMs = 0, availableMs = 0, readMs = 0;
};
struct TimeoutSetting {
    HINTERNET handle;
    int receive;
};
static std::vector<Response> responses;
static std::wstring finalUrl;
static size_t requests = 0, queries = 0, reads = 0, offset = 0, closes = 0, sends = 0, receives = 0;
static std::vector<TimeoutSetting> settings;
static size_t failTimeoutCall = 0, receiveTimeoutSetting = 0;
// Only WinHTTP phase ports advance this monotonic clock. Wall-clock/cache
// timestamps remain real, and no fixture waits for simulated network delay.
static ULONGLONG clock = 0;
static ULONGLONG WINAPI tick() { return clock; }
static Response& current() { return responses.at((std::min)(requests - 1, responses.size() - 1)); }
static void reset(const std::string& url, std::vector<Response> values) {
    finalUrl.assign(url.begin(), url.end()); responses = std::move(values);
    requests = queries = reads = offset = closes = sends = receives = 0;
    settings.clear(); failTimeoutCall = 0; clock = ::GetTickCount64();
}
static HINTERNET WINAPI open(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) { return reinterpret_cast<HINTERNET>(1); }
static HINTERNET WINAPI connect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) { return reinterpret_cast<HINTERNET>(2); }
static HINTERNET WINAPI request(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) {
    ++requests; offset = 0; clock += current().prepareMs; return reinterpret_cast<HINTERNET>(3);
}
static BOOL WINAPI close(HINTERNET) { ++closes; return TRUE; }
static BOOL WINAPI timeouts(HINTERNET handle, int, int, int, int receive) {
    settings.push_back({handle, receive});
    if (failTimeoutCall && settings.size() == failTimeoutCall) {
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_STATE); return FALSE;
    }
    return TRUE;
}
static BOOL WINAPI option(HINTERNET, DWORD, LPVOID, DWORD) { return TRUE; }
static BOOL WINAPI headers(HINTERNET, LPCWSTR, DWORD, DWORD) { return TRUE; }
static BOOL WINAPI send(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) {
    ++sends; clock += current().sendMs; return TRUE;
}
static BOOL WINAPI receive(HINTERNET, LPVOID) {
    receiveTimeoutSetting = settings.size() - 1;
    ++receives; clock += current().receiveMs;
    if (current().receiveError) { SetLastError(current().receiveError); return FALSE; }
    return TRUE;
}
static BOOL wideValue(const std::wstring& value, LPVOID buffer, LPDWORD size) {
    const DWORD needed = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    if (!buffer || *size < needed) { *size = needed; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::memcpy(buffer, value.c_str(), needed); *size = needed; return TRUE;
}
static BOOL WINAPI queryOption(HINTERNET, DWORD key, LPVOID value, LPDWORD size) {
    if (key != WINHTTP_OPTION_URL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return wideValue(finalUrl, value, size);
}
static BOOL WINAPI queryHeaders(HINTERNET, DWORD key, LPCWSTR, LPVOID value, LPDWORD size, LPDWORD) {
    if (key == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) {
        *static_cast<DWORD*>(value) = current().status; *size = sizeof(DWORD); return TRUE;
    }
    if (key == (WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER) && current().length) {
        *static_cast<DWORD*>(value) = *current().length; *size = sizeof(DWORD); return TRUE;
    }
    if (key == WINHTTP_QUERY_RETRY_AFTER && !current().retryAfter.empty()) return wideValue(current().retryAfter, value, size);
    if (key == WINHTTP_QUERY_RAW_HEADERS_CRLF) return wideValue(L"HTTP/1.1 fixture\r\n\r\n", value, size);
    SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND); return FALSE;
}
static BOOL WINAPI available(HINTERNET, LPDWORD bytes) {
    ++queries; clock += current().availableMs;
    // A broken EOF loop fails deterministically rather than spinning until a
    // wall-clock deadline. One read of zero is a complete unknown-size body.
    if (queries > current().body.size() + 4) { SetLastError(ERROR_WINHTTP_CONNECTION_ERROR); return FALSE; }
    *bytes = offset < current().body.size() ? static_cast<DWORD>(current().body.size() - offset) : (current().availableAtEof ? 8 : 0);
    return TRUE;
}
static BOOL WINAPI read(HINTERNET, LPVOID target, DWORD size, LPDWORD count) {
    ++reads; clock += current().readMs;
    if (current().readError) { SetLastError(current().readError); return FALSE; }
    *count = static_cast<DWORD>((std::min)({static_cast<size_t>(size), static_cast<size_t>(current().chunk), current().body.size() - offset}));
    if (*count) std::memcpy(target, current().body.data() + offset, *count);
    offset += *count; return TRUE;
}
// Execute the production direct-IP fallback, including its DNS packet parser,
// while preventing fixture UDP traffic from reaching a real resolver.
static bool dnsEnabled = false;
static size_t dnsQueries = 0;
static DWORD dnsReceiveMs = 0;
static std::vector<unsigned char> dnsRequest;
static DWORD WINAPI networkParams(PFIXED_INFO info, PULONG bytes) {
    if (!dnsEnabled) return ERROR_NO_DATA;
    if (!info || *bytes < sizeof(FIXED_INFO)) { *bytes = sizeof(FIXED_INFO); return ERROR_BUFFER_OVERFLOW; }
    std::memset(info, 0, sizeof(*info)); strcpy_s(info->DnsServerList.IpAddress.String, "192.0.2.53"); return NO_ERROR;
}
static SOCKET WSAAPI dnsSocket(int, int, int) { return 123; }
static int WSAAPI dnsClose(SOCKET) { return 0; }
static int WSAAPI dnsOption(SOCKET, int, int, const char*, int) { return 0; }
static int WSAAPI dnsSend(SOCKET, const char* bytes, int size, int, const sockaddr*, int) {
    ++dnsQueries; dnsRequest.assign(bytes, bytes + size); return size;
}
static int WSAAPI dnsReceive(SOCKET, char* buffer, int size, int, sockaddr*, int*) {
    clock += dnsReceiveMs;
    if (dnsRequest.size() < 12) return SOCKET_ERROR;
    auto response = dnsRequest; response[2] = 0x81; response[3] = 0x80; response[7] = 1;
    response.insert(response.end(), {0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,203,0,113,10});
    if (response.size() > static_cast<size_t>(size)) return SOCKET_ERROR;
    std::memcpy(buffer,response.data(),response.size()); return static_cast<int>(response.size());
}
static void WINAPI sleep(DWORD) { }
}
#define GetNetworkParams httpPorts::networkParams
#define socket httpPorts::dnsSocket
#define closesocket httpPorts::dnsClose
#define setsockopt httpPorts::dnsOption
#define sendto httpPorts::dnsSend
#define recvfrom httpPorts::dnsReceive
#define WinHttpOpen httpPorts::open
#define WinHttpConnect httpPorts::connect
#define WinHttpOpenRequest httpPorts::request
#define WinHttpCloseHandle httpPorts::close
#define WinHttpSetTimeouts httpPorts::timeouts
#define WinHttpSetOption httpPorts::option
#define WinHttpAddRequestHeaders httpPorts::headers
#define WinHttpSendRequest httpPorts::send
#define WinHttpReceiveResponse httpPorts::receive
#define WinHttpQueryOption httpPorts::queryOption
#define WinHttpQueryHeaders httpPorts::queryHeaders
#define WinHttpQueryDataAvailable httpPorts::available
#define WinHttpReadData httpPorts::read
#define Sleep httpPorts::sleep
#define GetTickCount64 httpPorts::tick
#include "custom_steam_library_storage_isolation.h"
#define wmain workerMainUnusedByHttpPorts
#include "../native/custom-steam-library/steam_artwork_lab.cpp"
#undef wmain
#undef Sleep
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void fails(const std::function<void()>& action, bool retryable) {
    try { action(); } catch (const HttpTransportError& e) { check(e.retryable() == retryable, "Incorrect retry classification"); return; }
    throw std::runtime_error("Expected a controlled transport failure");
}
int wmain(int argc, wchar_t** argv) { try {
    check(argc == 2, "Pass a fresh Build fixture root");
    const auto root = fs::absolute(argv[1]);
    const auto build = fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(canonicalPathKey(root).starts_with(canonicalPathKey(build) + "\\") && !fs::exists(root), "Unsafe/reused fixture root");
    fs::create_directories(root);
    g_steamAcceleratorStatus.checked = true; g_steamAcceleratorStatus.state = "fixture-direct";
    for (const auto* host : {"store.steampowered.com", "shared.steamstatic.com"}) g_steamAcceleratorStatus.localHostCache[host] = false;
    json cases = json::array();
    const std::string metadata = "https://store.steampowered.com/api/storesearch/?term=Portal";
    const std::string image = "https://shared.steamstatic.com/store_item_assets/steam/apps/620/library_600x900.jpg";
    auto test = [&](const std::string& name, const std::function<void()>& action) {
        resetReusableHttpSession(); g_metadataBackoffs.clear(); g_workerDataRoot = root/std::to_wstring(cases.size())/L"data";
        g_fastMetadataMode = false; g_optionalArtworkDeadline = 0; g_identityNetworkDeadline = 0;
        httpPorts::dnsEnabled = false; httpPorts::dnsQueries = 0; httpPorts::dnsRequest.clear(); httpPorts::dnsReceiveMs = 0;
        for (const auto* host : {"store.steampowered.com", "shared.steamstatic.com"}) g_steamAcceleratorStatus.localHostCache[host] = false;
        try { action(); cases.push_back({{"name",name},{"passed",true}}); }
        catch (const std::exception& e) { cases.push_back({{"name",name},{"passed",false},{"error",e.what()}}); }
        resetReusableHttpSession();
    };
    auto raw = [&](const std::string& url, size_t limit = 64) {
        return httpRequest("GET", url, {}, {}, limit, url == image, "", GetTickCount64() + 10000);
    };
    for (const DWORD status : {429u, 503u, 404u}) test("network-raw-error-large-" + std::to_string(status), [&] {
        httpPorts::Response r; r.status = status; r.length = 32u << 20; r.retryAfter = L"120";
        httpPorts::reset(metadata, {r}); const auto value = raw(metadata);
        check(value.status == status && value.serverRetryAfterMs == 120000, "Error size masked status/Retry-After");
        check(httpPorts::queries == 0 && httpPorts::reads == 0 && value.body.empty(), "Error body was downloaded");
    });
    for (bool fast : {false,true}) for (bool isImage : {false,true}) for (DWORD status : {429u,503u}) {
        test("network-raw-provider-wait-" + std::to_string(status) + (isImage?"-image":"-metadata") + (fast?"-auto":"-manual"), [&] {
            const auto& url = isImage ? image : metadata; g_fastMetadataMode = fast;
            httpPorts::Response r; r.status = status; r.length = 32u << 20; r.retryAfter = L"120";
            httpPorts::reset(url,{r}); json attempts = json::array();
            const auto value = httpGet(url,{},64,isImage,"steam","",&attempts);
            check(value.status == status && httpPorts::requests == 1 && attempts.back().value("retryDeferred",false), "Provider wait was lost");
            g_metadataBackoffs.clear(); fails([&] { httpGet(url,{},64,isImage,"steam","",&attempts); },true);
            check(httpPorts::requests == 1 && attempts.back().value("skippedNetwork",false), "Persisted wait allowed another request");
        });
    }
    for (bool length : {false,true}) test(std::string("network-raw-short-reads-") + (length?"known":"unknown"), [&] {
        httpPorts::Response r; if (length) r.length = static_cast<DWORD>(r.body.size());
        httpPorts::reset(metadata,{r}); const auto value = raw(metadata);
        check(std::string(value.body.begin(),value.body.end()) == r.body && httpPorts::reads > 1, "Short reads lost bytes");
    });
    for (const auto& body : {std::string{},std::string{"ok"}}) test("network-raw-zero-read-eof-" + std::to_string(body.size()), [&] {
        httpPorts::Response r; r.body = body; r.availableAtEof = true;
        httpPorts::reset(metadata,{r}); const auto value = raw(metadata);
        check(value.body.size() == body.size() && httpPorts::reads <= 2, "Zero-byte EOF kept reading");
    });
    test("network-raw-truncated-declared-body", [&] {
        httpPorts::Response r; r.length = static_cast<DWORD>(r.body.size() + 1);
        httpPorts::reset(metadata,{r}); fails([&] { raw(metadata); },true);
    });
    test("network-raw-zero-before-declared-end", [&] {
        httpPorts::Response r; r.body.clear(); r.length = 4; r.availableAtEof = true;
        httpPorts::reset(metadata,{r}); fails([&] { raw(metadata); },true);
        check(httpPorts::reads == 1, "Incomplete response kept reading");
    });
    test("network-raw-oversized-success-header", [&] {
        httpPorts::Response r; r.length = 32u << 20; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },false); check(httpPorts::reads == 0, "Oversized response downloaded");
    });
    test("network-raw-oversized-success-stream", [&] {
        httpPorts::Response r; r.body.assign(65,'x'); httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },false);
    });
    for (bool fast : {false,true}) for (bool isImage : {false,true}) test(std::string("network-raw-invalid-server-response") + (isImage?"-image":"-metadata") + (fast?"-auto":"-manual"), [&] {
        g_fastMetadataMode = fast; const auto& url = isImage?image:metadata;
        httpPorts::Response broken; broken.receiveError = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
        httpPorts::reset(url,{broken,httpPorts::Response{}}); json attempts = json::array(); const auto generation = g_reusableHttpSession.generation;
        const auto value = httpGet(url,{},64,isImage,"steam","",&attempts);
        check(value.status == 200 && httpPorts::requests == 2 && attempts.size() == 2, "Truncated headers were not retried");
        check(attempts.front().value("retryable",false) && g_reusableHttpSession.generation >= generation + 2, "Retry reused failed session");
    });
    for (DWORD error : {DWORD{ERROR_WINHTTP_TIMEOUT},DWORD{ERROR_WINHTTP_CONNECTION_ERROR}}) test("network-raw-read-error-" + std::to_string(error), [&] {
        httpPorts::Response r; r.readError = error; httpPorts::reset(metadata,{r}); fails([&] { raw(metadata); },true);
    });
    test("network-raw-certificate-error-is-not-retried", [&] {
        httpPorts::Response r; r.receiveError = ERROR_WINHTTP_SECURE_FAILURE; httpPorts::reset(metadata,{r});
        fails([&] { httpGet(metadata,{},64,false,"steam",""); },false);
        check(httpPorts::requests == 1, "Certificate validation failure retried");
    });
    test("network-deadline-send-exhaustion-stops-before-receive", [&] {
        httpPorts::Response r; r.sendMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
        check(httpPorts::receives == 0 && httpPorts::queries == 0, "An exhausted send started another blocking phase");
    });
    test("network-deadline-receive-uses-remaining-budget", [&] {
        httpPorts::Response r; r.sendMs = 4000; httpPorts::reset(metadata,{r});
        const auto value = raw(metadata);
        check(value.status == 200 && httpPorts::receives == 1, "Healthy response was rejected");
        // The receive call records the timeout active at its actual boundary.
        check(httpPorts::settings.at(httpPorts::receiveTimeoutSetting).receive <= 6000, "Receive reused the pre-send timeout");
    });
    test("network-deadline-request-preparation-stops-before-send", [&] {
        httpPorts::Response r; r.prepareMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
        check(httpPorts::sends == 0 && httpPorts::receives == 0, "Expired request preparation continued into send");
    });
    test("network-deadline-availability-exhaustion-stops-before-read", [&] {
        httpPorts::Response r; r.availableMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
        check(httpPorts::reads == 0, "An exhausted availability query started a body read");
    });
    test("network-deadline-availability-eof-still-obeys-budget", [&] {
        httpPorts::Response r; r.body.clear(); r.availableMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
    });
    test("network-deadline-zero-read-eof-still-obeys-budget", [&] {
        httpPorts::Response r; r.body.clear(); r.availableAtEof = true; r.readMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
        check(httpPorts::queries == 1 && httpPorts::reads == 1, "Deadline failure was masked by a later EOF/connection error");
    });
    test("network-deadline-error-headers-still-obey-budget", [&] {
        httpPorts::Response r; r.status = 503; r.receiveMs = 10000; httpPorts::reset(metadata,{r});
        fails([&] { raw(metadata); },true);
        check(httpPorts::queries == 0, "Expired headers started a body query");
    });
    for (bool optional : {false,true}) test(std::string("network-deadline-effective-") + (optional?"artwork":"identity"), [&] {
        httpPorts::Response r; r.availableMs = 1000; httpPorts::reset(optional?image:metadata,{r});
        if (optional) g_optionalArtworkDeadline = GetTickCount64() + 1000;
        else g_identityNetworkDeadline = GetTickCount64() + 1000;
        fails([&] { raw(optional?image:metadata); },true);
        check(httpPorts::reads == 0, "A nested job deadline was ignored before body read");
    });
    for (bool imageRequest : {false,true}) test(std::string("network-deadline-retry-shares-total-budget-") + (imageRequest?"image":"metadata"), [&] {
        const auto& url = imageRequest?image:metadata;
        httpPorts::Response r; r.sendMs = 1000; httpPorts::reset(url,{r});
        g_identityNetworkDeadline = GetTickCount64() + 1000;
        json attempts = json::array();
        fails([&] { httpGet(url,{},64,imageRequest,"steam","",&attempts); },true);
        check(httpPorts::requests == 1 && httpPorts::receives == 0, "Exhausted total budget admitted more network work");
        check(!g_reusableHttpSession.handle.value && !attempts.empty() && attempts.front().value("retryable",false), "Deadline failure did not discard its session/record retryability");
        check(!attempts.back().value("reconnectScheduled",false), "Trace claimed a reconnect after the budget rejected it");
    });
    test("network-deadline-healthy-slow-short-reads", [&] {
        httpPorts::Response r; r.sendMs = 1000; r.receiveMs = 1000; r.availableMs = 400; r.readMs = 400;
        httpPorts::reset(metadata,{r}); const auto value = raw(metadata);
        check(std::string(value.body.begin(),value.body.end()) == r.body && httpPorts::reads > 1, "Healthy paced short reads were lost");
    });
    for (size_t failureCall : {1u,2u,3u,4u}) test("network-deadline-timeout-setting-failure-" + std::to_string(failureCall), [&] {
        httpPorts::reset(metadata,{httpPorts::Response{}}); httpPorts::failTimeoutCall = failureCall;
        fails([&] { raw(metadata); },true);
        check(httpPorts::settings.size() == failureCall && httpPorts::queries == 0, "Failed timeout setup admitted further I/O");
    });
    auto semantic = [&](const std::string& url, json& log) {
        httpPorts::dnsEnabled = true;
        for (const auto* host : {"store.steampowered.com", "shared.steamstatic.com"}) g_steamAcceleratorStatus.localHostCache[host] = true;
        return retrySteamResponseAfterSemanticFailure(url,{},64,url==image,"","fixture invalid initial payload",&log);
    };
    for (bool fast : {false,true}) for (bool isImage : {false,true}) {
        const auto suffix = std::string(isImage?"image":"metadata") + (fast?"-auto":"-manual");
        for (const auto [status, wait] : {std::pair<DWORD,DWORD>{429,120000},{503,120000},{503,1000}})
            test("network-semantic-provider-wait-" + std::to_string(status) + "-" + std::to_string(wait) + "-" + suffix, [&] {
                const auto& url = isImage?image:metadata;
                httpPorts::Response r; r.status = status; r.retryAfter = std::to_wstring(wait/1000);
                httpPorts::reset(url,{r}); g_fastMetadataMode = fast;
                json log=json::array(); const auto value=semantic(url,log);
                check(value && value->status==status && httpPorts::requests==1 && httpPorts::dnsQueries==1,"Fallback did not consume the original status");
                check(networkVerificationPending(log) && log.size()==1 && log[0].value("retryDeferred",false),"Fallback lost its deferred retry receipt");
                check(log[0].value("retryAfterMs",DWORD{0})>=wait,"Fallback shortened the server wait");
                g_metadataBackoffs.clear(); const auto saved=loadMetadataBackoff(url);
                check(!saved.empty() && metadataProviderRetryAt(saved)>=unixTimeMs()+wait-1000,"Fallback did not persist the canonical endpoint wait");
                check(loadMetadataBackoff("https://203.0.113.10/api/storesearch/").empty(),"Fallback poisoned an IP endpoint instead of the source endpoint");
                fails([&] {httpGet(url,{},64,isImage,"steam","",&log);},true);
                check(httpPorts::requests==1 && httpPorts::dnsQueries==1,"A following request bypassed the provider wait");
            });
        test("network-semantic-existing-provider-wait-" + suffix,[&] {
            const auto& url=isImage?image:metadata; httpPorts::reset(url,{httpPorts::Response{}}); g_fastMetadataMode=fast;
            rememberMetadataBackoff(url,503,120000,true); json log=json::array();
            check(!semantic(url,log),"Semantic fallback bypassed an existing provider wait");
            check(httpPorts::requests==0 && httpPorts::dnsQueries==0,"Provider wait admitted DNS or HTTP I/O");
            check(networkVerificationPending(log),"Skipped semantic retry lost its recovery receipt");
        });
        for (DWORD error : {DWORD{ERROR_WINHTTP_TIMEOUT},DWORD{ERROR_WINHTTP_CANNOT_CONNECT},DWORD{ERROR_WINHTTP_SECURE_FAILURE}})
            test("network-semantic-transport-" + std::to_string(error) + "-" + suffix,[&] {
                const auto& url=isImage?image:metadata; httpPorts::Response r; r.receiveError=error;
                httpPorts::reset(url,{r}); g_fastMetadataMode=fast; json log=json::array();
                check(!semantic(url,log) && httpPorts::requests==1,"Failed fallback did not terminate");
                const bool retryable=error!=ERROR_WINHTTP_SECURE_FAILURE;
                check(log.size()==1 && log[0].value("win32Error",DWORD{0})==error,"Fallback lost the system error code");
                check(log[0].value("retryable",false)==retryable && networkVerificationPending(log)==retryable,"Fallback lost transport retry classification");
                if(retryable)check(!g_reusableHttpSession.handle.value,"Fallback kept the broken reusable session");
            });
    }
    for (bool optional : {false,true}) test(std::string("network-semantic-expired-budget-")+(optional?"artwork":"identity"),[&] {
        httpPorts::reset(metadata,{httpPorts::Response{}});
        if(optional)g_optionalArtworkDeadline=GetTickCount64(); else g_identityNetworkDeadline=GetTickCount64();
        json log=json::array(); check(!semantic(metadata,log),"Expired semantic fallback returned a response");
        check(httpPorts::dnsQueries==0 && httpPorts::requests==0,"Expired semantic fallback still attempted DNS/HTTP");
        check(networkVerificationPending(log),"Expired fallback lost its pending recovery state");
    });
    test("network-semantic-healthy-json-fallback-still-recovers",[&] {
        httpPorts::Response bad; bad.body="<html>gateway</html>"; httpPorts::Response good;
        httpPorts::reset(metadata,{bad,good}); httpPorts::dnsEnabled=true;
        g_steamAcceleratorStatus.localHostCache["store.steampowered.com"]=true;
        json log=json::array(); const auto value=getJson(metadata,"steam",&log);
        check(value.value("ok",false) && httpPorts::requests==2 && httpPorts::dnsQueries==1,"Healthy semantic fallback no longer recovers");
        check(loadMetadataBackoff(metadata).empty() && !networkVerificationPending(log),"Successful fallback entered a failure cooldown");
    });
    // A complete JSON fragment or decodable image in a partial response is
    // still not a complete representation. These requests never send Range.
    for (bool isImage : {false,true}) {
        const auto& url = isImage ? image : metadata;
        const auto kind = std::string(isImage ? "image" : "metadata");
        test("network-integrity-raw-partial-" + kind, [&] {
            httpPorts::Response partial; partial.status = 206;
            httpPorts::reset(url,{partial});
            fails([&] { raw(url); },true);
            check(httpPorts::queries==0 && httpPorts::reads==0,"Partial representation body was consumed");
        });
        for (bool fast : {false,true}) test("network-integrity-partial-recovers-" + kind + (fast?"-auto":"-manual"), [&] {
            httpPorts::Response partial; partial.status = 206; partial.body = "{\"ok\":false}";
            httpPorts::Response full;
            httpPorts::reset(url,{partial,full}); g_fastMetadataMode=fast; json log=json::array();
            const auto generation=g_reusableHttpSession.generation;
            const auto value=httpGet(url,{},64,isImage,"steam","",&log);
            check(value.status==200 && httpPorts::requests==2,"Partial success was accepted without recovery");
            check(log.size()==2 && !log[0].value("ok",true) && log[0].value("retryable",false),"Partial failure was not retryable");
            check(g_reusableHttpSession.generation>=generation+2,"Partial response reused its failed session");
            check(log.back().value("ok",false),"Complete recovery did not publish a successful terminal receipt");
        });
        test("network-integrity-partial-exhaustion-" + kind, [&] {
            httpPorts::Response partial; partial.status=206; httpPorts::reset(url,{partial});
            g_optionalArtworkDeadline=GetTickCount64()+10000; json log=json::array();
            fails([&] { httpGet(url,{},64,isImage,"steam","",&log); },true);
            check(httpPorts::requests==1 && networkVerificationPending(log),"Partial response lost bounded recovery evidence");
            check(!g_reusableHttpSession.handle.value,"Partial response retained a broken session");
        });
        for (bool exhaustedBeforeDns : {false,true}) test("network-integrity-fallback-budget-" + kind + (exhaustedBeforeDns?"-before-dns":"-after-dns"), [&] {
            httpPorts::Response bad;
            if (exhaustedBeforeDns) bad.sendMs=1000;
            else bad.receiveError=ERROR_WINHTTP_CANNOT_CONNECT;
            httpPorts::reset(url,{bad}); httpPorts::dnsEnabled=true;
            httpPorts::dnsReceiveMs=exhaustedBeforeDns?0:1000;
            g_identityNetworkDeadline=GetTickCount64()+1000;
            for (const auto* host : {"store.steampowered.com","shared.steamstatic.com"}) g_steamAcceleratorStatus.localHostCache[host]=true;
            json log=json::array(); fails([&] { httpGet(url,{},64,isImage,"steam","",&log); },true);
            check(httpPorts::dnsQueries==(exhaustedBeforeDns?0:1),"Expired fallback admitted unnecessary DNS");
            check(httpPorts::requests==1,"Expired fallback admitted another HTTP request");
            check(!log.empty() && std::none_of(log.begin(),log.end(),[](const json& row) { return row.value("reconnectScheduled",false); }),"Expired fallback claimed a scheduled reconnect");
            check(networkVerificationPending(log),"Expired fallback lost retry state");
        });
    }
    for (bool isImage : {false,true}) test(std::string("network-integrity-healthy-route-fallback-")+(isImage?"image":"metadata"),[&] {
        const auto& url=isImage?image:metadata;
        httpPorts::Response bad; bad.receiveError=ERROR_WINHTTP_CANNOT_CONNECT;
        httpPorts::reset(url,{bad,httpPorts::Response{}}); httpPorts::dnsEnabled=true; httpPorts::dnsReceiveMs=100;
        g_identityNetworkDeadline=GetTickCount64()+10000;
        for (const auto* host : {"store.steampowered.com","shared.steamstatic.com"}) g_steamAcceleratorStatus.localHostCache[host]=true;
        json log=json::array(); const auto value=httpGet(url,{},64,isImage,"steam","",&log);
        check(value.status==200 && httpPorts::requests==2 && httpPorts::dnsQueries==1,"Available budget no longer admits healthy fallback");
        check(log.size()==2 && log[0].value("reconnectScheduled",false) && log[1].value("networkRoute","")=="direct-ip","Healthy fallback lost its route trace");
    });
    test("network-integrity-json-partial-cannot-become-identity",[&] {
        httpPorts::Response partial; partial.status=206; partial.body="{\"ok\":false}";
        httpPorts::reset(metadata,{partial,httpPorts::Response{}}); json log=json::array();
        check(getJson(metadata,"steam",&log).value("ok",false) && httpPorts::requests==2,"Partial JSON reached the identity consumer");
    });
    test("network-integrity-post-partial-cannot-become-absence",[&] {
        const std::string url="https://api2.playnite.link/api/igdb/search";
        httpPorts::Response partial; partial.status=206; partial.body="{\"ok\":false}";
        httpPorts::reset(url,{partial,httpPorts::Response{}}); json log=json::array();
        check(postJson(url,{{"SearchTerm","Portal 2"}},"playnite-igdb",&log).value("ok",false) && httpPorts::requests==2,"Partial POST JSON reached the metadata consumer");
    });
    test("network-integrity-semantic-partial-rejected",[&] {
        httpPorts::Response partial; partial.status=206; httpPorts::reset(metadata,{partial});
        json log=json::array(); check(!semantic(metadata,log),"Semantic fallback accepted a partial representation");
        check(networkVerificationPending(log) && !g_reusableHttpSession.handle.value,"Semantic partial response lost recovery evidence");
    });
    size_t failed = 0; for (const auto& item : cases) if (!item["passed"].get<bool>()) ++failed;
    const json report = {{"allPassed",failed==0},{"caseCount",cases.size()},{"failedCount",failed},{"cases",cases},
        {"liveNetworkUsedByFixtures",false},{"realSteamFilesModified",false},{"scope","actual raw WinHTTP request/read/retry with isolated API ports"}};
    writeJsonAtomic(root/L"summary.json",report); std::cout << report.dump(2) << "\n"; return failed?1:0;
} catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; } }
