#pragma once
#include <winsock2.h> // SDK IPv6 owner-table types only; all IO remains in WinHTTP.
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cwctype>
#include <vector>
#include <string>
#include <stdexcept>
#include <iterator>
#include <map>
#include <optional>
#include <functional>
#include <set>
#include "json.hpp"
#include "steam_live_script.h"
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")

// Dynamically discovered, Steam-owned loopback CDP client. Never enables a debugging port, launches
// a helper, installs Node, or accepts JavaScript/URLs from the frontend.
namespace ymcc::steamlive {
using Json = nlohmann::json;
inline std::mutex transportMutex;
struct HttpHandle {
    HINTERNET h = nullptr;
    explicit HttpHandle(HINTERNET value) : h(value) { if (!h) throw std::runtime_error("live-transport-failed"); }
    ~HttpHandle() { if (h) WinHttpCloseHandle(h); }
    HttpHandle(const HttpHandle&) = delete;
};
inline std::wstring normalizedPath(std::wstring s) {
    std::replace(s.begin(), s.end(), L'/', L'\\');
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
}
inline std::wstring processImage(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t path[32768]; DWORD size = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(h, 0, path, &size) != FALSE;
    CloseHandle(h);
    return ok ? normalizedPath(std::wstring(path, size)) : std::wstring();
}
struct Endpoint {
    DWORD pid = 0;
    unsigned short port = 0;
    std::wstring host = L"127.0.0.1";
    bool operator==(const Endpoint&) const = default;
};
inline std::vector<Endpoint> listeners(const std::wstring& steam) {
    // Only enumerate Steam's own listeners, not arbitrary localhost ports.
    const auto root = normalizedPath(steam);
    const auto prefix = root + L"\\";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("live-debug-unavailable");
    std::map<DWORD,DWORD> parents;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) do { parents[entry.th32ProcessID] = entry.th32ParentProcessID; }
    while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    std::map<DWORD,bool> verified;
    const auto trusted = [&](DWORD pid) {
        const auto known = verified.find(pid);
        if (known != verified.end()) return known->second;
        const auto image = processImage(pid);
        const auto parent = parents.find(pid);
        const bool valid = image.compare(0, prefix.size(), prefix) == 0 &&
            std::filesystem::path(image).filename() == L"steamwebhelper.exe" && parent != parents.end() &&
            processImage(parent->second) == root + L"\\steam.exe";
        verified[pid] = valid;
        return valid;
    };
    std::vector<Endpoint> result;
    std::set<std::pair<DWORD,unsigned short>> networkExposed;
    const auto decodePort = [](DWORD value) { return static_cast<unsigned short>(((value & 0xff) << 8) | ((value >> 8) & 0xff)); };
    const auto add = [&](DWORD pid, DWORD port, const wchar_t* host) {
        if (!trusted(pid)) return;
        Endpoint endpoint{pid, decodePort(port), host};
        if (endpoint.port && std::find(result.begin(), result.end(), endpoint) == result.end()) result.push_back(endpoint);
    };
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, 2, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (size && size <= 16 * 1024 * 1024) {
        std::vector<unsigned char> bytes(size);
        if (GetExtendedTcpTable(bytes.data(), &size, FALSE, 2, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                if (row.dwLocalAddr == 0x0100007f) add(row.dwOwningPid, row.dwLocalPort, L"127.0.0.1");
                else if (row.dwLocalAddr == 0 && trusted(row.dwOwningPid)) networkExposed.emplace(row.dwOwningPid, decodePort(row.dwLocalPort));
            }
        }
    }
    size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, 23, TCP_TABLE_OWNER_PID_LISTENER, 0); // AF_INET6, no Winsock ABI dependency
    if (size && size <= 16 * 1024 * 1024) {
        std::vector<unsigned char> bytes(size);
        if (GetExtendedTcpTable(bytes.data(), &size, FALSE, 23, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                const bool loopback = row.ucLocalAddr[15] == 1 && std::all_of(row.ucLocalAddr, row.ucLocalAddr + 15, [](unsigned char b) { return b == 0; });
                if (loopback) add(row.dwOwningPid, row.dwLocalPort, L"::1");
                else if (std::all_of(row.ucLocalAddr, row.ucLocalAddr + 16, [](unsigned char b) { return b == 0; }) && trusted(row.dwOwningPid))
                    networkExposed.emplace(row.dwOwningPid, decodePort(row.dwLocalPort));
            }
        }
    }
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& endpoint) {
        return networkExposed.count({endpoint.pid, endpoint.port}) != 0;
    }), result.end());
    if (result.empty()) throw std::runtime_error(networkExposed.empty() ? "live-debug-unavailable" : "live-debug-not-loopback");
    if (result.size() > 16) throw std::runtime_error("live-context-ambiguous");
    return result;
}
inline bool stillOwned(const std::wstring& steam, const Endpoint& endpoint) {
    const auto current = listeners(steam);
    return std::find(current.begin(), current.end(), endpoint) != current.end();
}
inline std::wstring widenAscii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
inline void requireStatus(HINTERNET request, DWORD expected) {
    DWORD code = 0, bytes = sizeof(code);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &bytes, nullptr) || code != expected)
        throw std::runtime_error("live-transport-failed");
}
inline std::string getTargets(HINTERNET connection) {
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr))
        throw std::runtime_error("live-debug-unavailable");
    requireStatus(request.h, 200);
    std::string result;
    char buffer[8192]; DWORD read = 0;
    while (WinHttpReadData(request.h, buffer, sizeof(buffer), &read) && read) {
        result.append(buffer, read);
        if (result.size() > 1024 * 1024) throw std::runtime_error("live-targets-invalid");
    }
    return result;
}
inline std::string targetPath(const Json& targets, const Endpoint& endpoint) {
    if (!targets.is_array()) throw std::runtime_error("live-targets-invalid");
    std::string result;
    if (!endpoint.port || (endpoint.host != L"127.0.0.1" && endpoint.host != L"::1")) throw std::runtime_error("live-targets-invalid");
    const std::string prefix = std::string("ws://") + (endpoint.host == L"::1" ? "[::1]" : "127.0.0.1") + ":" + std::to_string(endpoint.port) + "/devtools/page/";
    for (const auto& t : targets) {
        if (!t.is_object() || t.value("type", std::string()) != "page" || t.value("title", std::string()) != "SharedJSContext" ||
            t.value("url", std::string()).rfind("https://steamloopback.host/", 0) != 0) continue;
        const auto ws = t.value("webSocketDebuggerUrl", std::string());
        if (ws.rfind(prefix, 0) != 0) throw std::runtime_error("live-targets-invalid");
        const auto id = ws.substr(prefix.size());
        if (id.empty() || id.size() > 128 || id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") != std::string::npos)
            throw std::runtime_error("live-targets-invalid");
        if (!result.empty()) throw std::runtime_error("live-context-ambiguous");
        result = "/devtools/page/" + id;
    }
    if (result.empty()) throw std::runtime_error("live-context-unavailable");
    return result;
}
// A watchdog closes the socket on deadline, cancelling synchronous receive.
// The JS has its own shorter stage deadlines; timeout after send is uncertain,
// never permission to fall back to writing a live Steam file.
struct BoundedSocket {
    HINTERNET h;
    std::mutex mx;
    std::condition_variable cv;
    bool finished = false;
    std::thread watchdog;
    explicit BoundedSocket(HINTERNET socket) : h(socket), watchdog([this] {
        std::unique_lock<std::mutex> lock(mx);
        if (!cv.wait_for(lock, std::chrono::seconds(16), [this] { return finished; }) && h) {
            WinHttpCloseHandle(h); h = nullptr;
        }
    }) {}
    ~BoundedSocket() {
        { std::lock_guard<std::mutex> lock(mx); finished = true; }
        cv.notify_all(); watchdog.join();
        if (h) WinHttpCloseHandle(h);
    }
};
inline Json run(const std::wstring& steam, const Json& request, const std::function<bool()>& admitAction = {}) {
    std::lock_guard<std::mutex> serial(transportMutex);
    bool sent = false;
    try {
        const auto candidates = listeners(steam);
        HttpHandle session(WinHttpOpen(L"YMCC-SteamLive/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        WinHttpSetTimeouts(session.h, 1000, 1000, 1500, 1500);
        std::optional<Endpoint> selected;
        std::string path, discoveryError = "live-context-unavailable";
        const auto started = GetTickCount64();
        for (const auto& candidate : candidates) {
            if (GetTickCount64() - started > 5000) throw std::runtime_error("live-discovery-timeout");
            // IPv4/IPv6 aliases of the same owning PID/port are one endpoint.
            if (selected && selected->pid == candidate.pid && selected->port == candidate.port) continue;
            try {
                HttpHandle probe(WinHttpConnect(session.h, candidate.host.c_str(), candidate.port, 0));
                const auto found = targetPath(Json::parse(getTargets(probe.h)), candidate);
                if (selected) throw std::runtime_error("live-context-ambiguous");
                selected = candidate; path = found;
            } catch (const std::exception& e) {
                const std::string error = e.what();
                if (error == "live-context-ambiguous" || error == "live-targets-invalid") throw;
                if (error == "live-context-unavailable") discoveryError = error;
            }
        }
        if (!selected) throw std::runtime_error(discoveryError);
        const auto endpoint = *selected;
        if (!stillOwned(steam, endpoint)) throw std::runtime_error("live-debug-owner-mismatch");
        WinHttpSetTimeouts(session.h, 1500, 1500, 2000, 3000);
        HttpHandle connection(WinHttpConnect(session.h, endpoint.host.c_str(), endpoint.port, 0));
        HttpHandle upgrade(WinHttpOpenRequest(connection.h, L"GET", widenAscii(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
        DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(upgrade.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));
        if (!WinHttpSetOption(upgrade.h, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
            !WinHttpSendRequest(upgrade.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(upgrade.h, nullptr))
            throw std::runtime_error("live-transport-failed");
        requireStatus(upgrade.h, 101);
        HINTERNET socket = WinHttpWebSocketCompleteUpgrade(upgrade.h, 0);
        if (!socket) throw std::runtime_error("live-transport-failed");
        BoundedSocket bounded(socket);
        if (!stillOwned(steam, endpoint)) throw std::runtime_error("live-debug-owner-mismatch");
        const std::string expression = std::string(kScript) + "(" + request.dump() + ")";
        const std::string message = Json{{"id", 1}, {"method", "Runtime.evaluate"}, {"params", {
            {"expression", expression}, {"returnByValue", true}, {"awaitPromise", true}, {"timeout", 14000}}}}.dump();
        // Recheck event/config/power admission after discovery and socket setup.
        // A cancelled action must not be sent merely because it entered the queue earlier.
        if (admitAction && !admitAction()) return {{"ok", false}, {"reason", "live-action-cancelled"}, {"mutated", false}};
        sent = true;
        if (WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(message.data()), static_cast<DWORD>(message.size())) != NO_ERROR)
            throw std::runtime_error("live-transport-uncertain");
        std::string response;
        unsigned messages = 0;
        for (;;) {
            char buffer[16384]; DWORD read = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            if (WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &read, &kind) != NO_ERROR || kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                throw std::runtime_error("live-transport-uncertain");
            if (kind != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                throw std::runtime_error("live-response-invalid");
            response.append(buffer, read);
            if (response.size() > 1024 * 1024) throw std::runtime_error("live-response-invalid");
            if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
            const auto packet = Json::parse(response); response.clear();
            if (++messages > 128) throw std::runtime_error("live-response-invalid");
            if (packet.value("id", 0) != 1) continue;
            if (packet.contains("error") || !packet.contains("result") || packet["result"].contains("exceptionDetails"))
                throw std::runtime_error("live-api-error");
            const auto& remote = packet["result"]["result"];
            if (!remote.contains("value") || !remote["value"].is_object()) throw std::runtime_error("live-response-invalid");
            return remote["value"];
        }
    } catch (const std::exception& e) {
        return {{"ok", false}, {"reason", sent ? "live-transport-uncertain" : e.what()}, {"mutated", sent}};
    }
}
inline bool canDefer(const Json& result) {
    if (result.value("mutated", false)) return false;
    const auto reason = result.value("reason", std::string());
    return reason == "live-read-unavailable" || reason == "live-busy" || reason == "steam-session-starting" || reason == "steam-session-changed" || reason == "steam-account-changed" || reason == "live-debug-unavailable" || reason == "live-context-unavailable" || reason == "live-api-unavailable" || reason == "live-controller-not-connected";
}
}
