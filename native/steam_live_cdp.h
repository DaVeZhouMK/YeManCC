#pragma once
#include "steam_command_coordinator.h"
#include <winsock2.h> // SDK IPv6 owner-table types only; all IO remains in WinHTTP.
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cwctype>
#include <vector>
#include <string>
#include <stdexcept>
#include <iterator>
#include <cstring>
#include <map>
#include <optional>
#include <functional>
#include <set>
#include "json.hpp"
#include "steam_live_script.h"
#include "game_process_inventory.h"
#include "steam_request_policy.h"
#include "steam_endpoint_discovery.h"
#include "steam_listener_source.h"
#include "steam_failure_diagnostics.h"
#include "steam_native_return_evidence.h"
#include "steam_session_observer.h"
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")

// Dynamically discovered, Steam-owned loopback CDP client. Never enables a debugging port, launches
// a helper, installs Node, or accepts JavaScript/URLs from the frontend.
namespace ymcc::steamlive {
using Json = nlohmann::json;
// Only fixed, typed read-only operations participate in the running-link budget.
inline bool isRunningLinkRead(const Json& request) {
    const auto operation=request.find("operation"), flag=request.find("runningLinkRead");
    return operation!=request.end()&&operation->is_string()&&
        steamrequest::classify(operation->get_ref<const std::string&>()).runningLinkRead&&
        flag!=request.end()&&flag->is_boolean()&&flag->get<bool>();
}
// One conservative classification for transport failure and its native wrapper.
// The JS dispose-only branch accepts exactly bool true. A present menuGame
// remains a possible alias write even when its payload is invalid.
inline bool menuGameMutation(const Json& request, std::string_view operation) {
    if (!request.is_object()) return false;
    if (request.contains("menuGame")) return true;
    const auto disposeOnly = request.find("menuGameDisposeOnly");
    return operation == "menu.qam.dispose" && disposeOnly != request.end() &&
        disposeOnly->is_boolean() && disposeOnly->get<bool>();
}
// Parse only the optional PID routing hint consumed by this operation. This
// is not a process/birth/account lease and cannot replace expectedSession.
// Missing witness/PID and explicit zero preserve the legacy unbound hint.
inline bool requestProcessPid(const Json& request, const steamrequest::Policy& policy, DWORD& pid) {
    pid = 0;
    if (!policy.lightGamepadLease && !policy.sleep) return true;
    const auto witness = request.find(policy.lightGamepadLease ? "lightSessionProcess" : "sleepSessionProcess");
    if (witness == request.end()) return true;
    if (!witness->is_object()) return false;
    const auto field = witness->find("pid");
    if (field == witness->end()) return true;
    if (!field->is_number_integer()) return false;
    if (field->is_number_unsigned()) {
        const auto value = field->get<std::uint64_t>();
        if (value > 0xffffffffULL) return false;
        pid = static_cast<DWORD>(value);
    } else {
        const auto value = field->get<std::int64_t>();
        if (value < 0 || value > 0xffffffffLL) return false;
        pid = static_cast<DWORD>(value);
    }
    return true;
}
inline std::timed_mutex transportMutex;
enum class TransportHolder : std::uint8_t {
    Unknown, Mode, Menu, Sleep, Decky, Read, Action, Documents, DocumentWorker,
    SleepSessionInspect, SleepInspect, SleepReconcile, SleepInputResume
};
inline const char* transportHolderName(TransportHolder holder) noexcept {
    switch(holder){
    case TransportHolder::Mode:return "mode-read";
    case TransportHolder::Menu:return "menu";
    case TransportHolder::Sleep:
    case TransportHolder::SleepSessionInspect:
    case TransportHolder::SleepInspect:
    case TransportHolder::SleepReconcile:
    case TransportHolder::SleepInputResume:return "sleep";
    case TransportHolder::Decky:return "decky";
    case TransportHolder::Read:return "other-read";
    case TransportHolder::Action:return "other-action";
    case TransportHolder::Documents:return "documents-native";
    case TransportHolder::DocumentWorker:return "documents-worker";
    default:return "unknown";
    }
}
inline const char* transportSleepOperationName(TransportHolder holder) noexcept {
    switch(holder){
    case TransportHolder::SleepSessionInspect:return "sleep.session.inspect";
    case TransportHolder::SleepInspect:return "sleep.inspect";
    case TransportHolder::SleepReconcile:return "sleep.reconcile";
    case TransportHolder::SleepInputResume:return "sleep.input-resume";
    default:return nullptr;
    }
}
inline TransportHolder classifyTransportHolder(std::string_view operation, const steamrequest::Policy& policy) noexcept {
    if(!policy.known)return TransportHolder::Unknown;
    if(operation=="ui.mode.get")return TransportHolder::Mode;
    if(policy.menu)return TransportHolder::Menu;
    if(policy.sleep){
        // Keep the operation in the same atomic sample, never the request payload.
        if(operation=="sleep.session.inspect")return TransportHolder::SleepSessionInspect;
        if(operation=="sleep.inspect")return TransportHolder::SleepInspect;
        if(operation=="sleep.reconcile")return TransportHolder::SleepReconcile;
        if(operation=="sleep.input-resume")return TransportHolder::SleepInputResume;
        return TransportHolder::Sleep;
    }
    if(operation.starts_with("decky."))return TransportHolder::Decky;
    return policy.coordinatedEffect?TransportHolder::Action:TransportHolder::Read;
}
struct TransportHolderSample {
    TransportHolder holder = TransportHolder::Unknown;
    DWORD heldMs = 0;
    ULONGLONG observedTick = 0;
    ULONGLONG startedTick = 0;
};
// Observation only. Publish and clear under the existing lock; the waiter
// samples a single packed value without acquiring or extending that lock.
class TransportHoldObservation {
    static inline std::atomic<std::uint64_t> stamp_{0};
    std::uint64_t published_ = 0;
public:
    TransportHoldObservation(const std::unique_lock<std::timed_mutex>& lock, TransportHolder holder) noexcept {
        if(!lock.owns_lock()||lock.mutex()!=&transportMutex||holder==TransportHolder::Unknown)return;
        const auto tick=GetTickCount64();
        if(tick>(UINT64_MAX>>8))return;
        published_=(tick<<8)|static_cast<std::uint8_t>(holder);
        stamp_.store(published_,std::memory_order_relaxed);
    }
    TransportHoldObservation(const TransportHoldObservation&)=delete;
    TransportHoldObservation& operator=(const TransportHoldObservation&)=delete;
    ~TransportHoldObservation(){if(published_)stamp_.store(0,std::memory_order_relaxed);}
    ULONGLONG heldTick() const noexcept {return published_>>8;}
    static TransportHolderSample sample() noexcept {
        const auto stamp=stamp_.load(std::memory_order_relaxed);
        if(!stamp)return {};
        const auto now=GetTickCount64(), tick=stamp>>8;
        if(now<tick||stamp!=stamp_.load(std::memory_order_relaxed))return {};
        return {static_cast<TransportHolder>(stamp&0xff),static_cast<DWORD>((std::min)(now-tick,ULONGLONG{60000})),now,tick};
    }
};
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
// A held verified parent prevents an expected PID from becoming another Steam
// instance during queue/discovery/dispatch. No registry read or process scan.
class SteamProcessLease {
    HANDLE process_ = nullptr;
    bool required_ = false, bound_ = false;
    DWORD pid_ = 0; std::wstring root_;
public:
    SteamProcessLease() = default;
    SteamProcessLease(const SteamProcessLease&) = delete;
    SteamProcessLease& operator=(const SteamProcessLease&) = delete;
    ~SteamProcessLease() { if (process_) CloseHandle(process_); }
    bool bind(const steamsession::Session& session) {
        required_ = true; bound_ = false;
        if (process_) return false;
        if (!session.pid || !session.created || session.root.empty()) return false;
        ULONGLONG birth = 0;
        process_ = steamsession::verifiedProcess(session.pid, session.root, &birth);
        bound_ = process_ && birth == session.created;
        if (bound_) { pid_ = session.pid; root_ = normalizedPath(session.root); }
        return bound_ && current();
    }
    bool boundTo(const std::wstring& root, DWORD pid) const {
        return required_ && bound_ && pid && pid == pid_ && normalizedPath(root) == root_ && current();
    }
    std::wstring currentImage(const std::wstring& root, DWORD pid) const {
        if (!boundTo(root, pid)) return {};
        wchar_t image[32768]; DWORD size = static_cast<DWORD>(std::size(image));
        if (!QueryFullProcessImageNameW(process_, 0, image, &size) || !current()) return {};
        return normalizedPath(std::wstring(image, size));
    }
    bool current() const noexcept { return !required_ || (bound_ && process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT); }
};
inline bool listenerProcessBound(const std::wstring& steam, const std::wstring& image, const std::wstring& parentImage, DWORD parentPid, DWORD expectedSteamPid) {
    const auto root = normalizedPath(steam), child = normalizedPath(image);
    return child.compare(0,root.size()+1,root+L"\\") == 0 && std::filesystem::path(child).filename() == L"steamwebhelper.exe" &&
        normalizedPath(parentImage) == root+L"\\steam.exe" && (!expectedSteamPid || parentPid == expectedSteamPid);
}
// Only one document request holds this endpoint object. Images are still
// queried in child-then-parent order on every inventory check; only repeated
// OpenProcess/CloseHandle is avoided. Exit/unknown never rebinds a replacement.
class ListenerProcessLease {
    HANDLE process_ = nullptr;
    const SteamProcessLease* parent_ = nullptr;
    DWORD pid_ = 0, parentPid_ = 0;
    ULONGLONG birth_ = 0;
    bool attempted_ = false, bound_ = false;
    std::wstring root_;
public:
    ListenerProcessLease() = default;
    ListenerProcessLease(const ListenerProcessLease&) = delete;
    ListenerProcessLease& operator=(const ListenerProcessLease&) = delete;
    ~ListenerProcessLease() { if (process_) CloseHandle(process_); }
    bool current() const noexcept {
        return bound_ && parent_ && parent_->current() && process_ && birth_ &&
            WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    bool bind(const steamsession::Session& session, const Endpoint& endpoint, const SteamProcessLease& parent) {
        if (attempted_) return false;
        attempted_ = true; pid_ = endpoint.pid; parentPid_ = session.pid;
        root_ = normalizedPath(session.root); parent_ = &parent;
        if (!pid_ || !endpoint.port || !parent.boundTo(root_, parentPid_)) return false;
        process_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid_);
        if (!process_) return false;
        FILETIME birth{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process_, &birth, &exit, &kernel, &user)) return false;
        birth_ = (static_cast<ULONGLONG>(birth.dwHighDateTime) << 32) | birth.dwLowDateTime;
        bound_ = birth_ != 0;
        return verify(root_, pid_, parentPid_, parentPid_);
    }
    bool applies(DWORD pid) const noexcept { return attempted_ && pid == pid_; }
    bool verify(const std::wstring& root, DWORD pid, DWORD parentPid, DWORD expectedSteamPid) const {
        if (!current() || pid != pid_ || parentPid != parentPid_ || expectedSteamPid != parentPid_ ||
            normalizedPath(root) != root_ || !parent_->boundTo(root_, parentPid_)) return false;
        wchar_t image[32768]; DWORD size = static_cast<DWORD>(std::size(image));
        if (!QueryFullProcessImageNameW(process_, 0, image, &size)) return false;
        const std::wstring child(image, size);
        const auto parentImage = parent_->currentImage(root_, parentPid_);
        return listenerProcessBound(root_, child, parentImage, parentPid, expectedSteamPid) && current();
    }
};
// A fresh topology hint, not a reusable identity lease. The native owner alone
// performs Basic and fallback acquisition. Every listener still validates both
// current process images; an incomplete fallback supplies no parent authority.
inline std::map<DWORD,DWORD> listenerParents(
    ymcc::gameInventory::Query query = ymcc::gameInventory::resolveQuery(), DWORD expectedSteamPid = 0) {
    listenersource::checkpoint();
    std::map<DWORD,DWORD> parents;
    const auto record = [&](DWORD pid, DWORD parent) {
        if (!expectedSteamPid || parent == expectedSteamPid) parents[pid] = parent;
        else if (!parents.empty()) parents.erase(pid); // Preserve the last row for a duplicate PID.
    };
    {
        using Basic = ymcc::gameInventory::BasicSnapshot;
        Basic basic(query, Basic::Projection::TopologyOnly);
        listenersource::checkpoint();
        if (const auto* topology = basic.topology()) {
            for (const auto& entry : *topology) record(entry.pid, entry.parentPid);
            return parents;
        }
    }
    // Preserve the one-shot/reentrant fallback, through the same producer.
    // Failed prefixes cannot establish a parent relation or authorize a port.
    listenersource::checkpoint();
    const auto fallback = ymcc::processInventory::read(nullptr,
        ymcc::processInventory::currentTime, 0);
    listenersource::checkpoint();
    if (!fallback || fallback->status != ymcc::processInventory::Status::Complete)
        throw std::runtime_error("live-debug-unavailable");
    for (const auto& entry : fallback->rows) record(entry.pid, entry.parentPid);
    return parents;
}
inline std::vector<Endpoint> listeners(const std::wstring& steam, DWORD expectedSteamPid = 0, const ListenerProcessLease* held = nullptr) {
    if(listenersource::delegatedRole.load(std::memory_order_acquire)){
        if(held)throw std::runtime_error("live-listener-proxy-invalid");
        return listenersource::read(normalizedPath(steam),expectedSteamPid);
    }
    listenersource::checkpoint();
    // Only enumerate Steam's own listeners, not arbitrary localhost ports.
    const auto root = normalizedPath(steam);
    const auto parents = listenerParents(gameInventory::resolveQuery(), expectedSteamPid);
    std::map<DWORD,bool> verified;
    const auto trusted = [&](DWORD pid) {
        listenersource::checkpoint();
        const auto known = verified.find(pid);
        if (known != verified.end()) return known->second;
        const auto parent = parents.find(pid);
        // A fresh topology mismatch only rejects; matching still needs both
        // current images in the original child-then-parent observation order.
        if (parent == parents.end() || (expectedSteamPid && parent->second != expectedSteamPid)) {
            verified[pid] = false;
            return false;
        }
        if (held && held->applies(pid)) {
            const bool valid = held->verify(root, pid, parent->second, expectedSteamPid);
            verified[pid] = valid;
            return valid;
        }
        const auto image = processImage(pid);
        listenersource::checkpoint();
        const auto parentImage = processImage(parent->second);
        listenersource::checkpoint();
        const bool valid = listenerProcessBound(root,image,parentImage,parent->second,expectedSteamPid);
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
    // Both address families must be completely observed before any endpoint
    // can be authorized. A failed or truncated sibling table is not absence.
    const auto tableBytes=[](ULONG family, std::size_t header, std::size_t rowSize) {
        listenersource::checkpoint();
        DWORD size=0;
        const auto initial=GetExtendedTcpTable(nullptr,&size,FALSE,family,TCP_TABLE_OWNER_PID_LISTENER,0);
        listenersource::checkpoint();
        if(initial!=ERROR_INSUFFICIENT_BUFFER || size<header || size>16*1024*1024)
            throw std::runtime_error("live-debug-unavailable");
        std::vector<unsigned char> bytes(size);
        const auto capacity=size;
        listenersource::checkpoint();
        const auto filled=GetExtendedTcpTable(bytes.data(),&size,FALSE,family,TCP_TABLE_OWNER_PID_LISTENER,0);
        listenersource::checkpoint();
        if(filled!=NO_ERROR ||
           size<header || size>capacity) throw std::runtime_error("live-debug-unavailable");
        DWORD count=0;std::memcpy(&count,bytes.data(),sizeof(count));
        if(count>(size-header)/rowSize) throw std::runtime_error("live-debug-unavailable");
        return bytes;
    };
    const auto ipv4=tableBytes(2,FIELD_OFFSET(MIB_TCPTABLE_OWNER_PID,table),sizeof(MIB_TCPROW_OWNER_PID));
    const auto ipv6=tableBytes(23,FIELD_OFFSET(MIB_TCP6TABLE_OWNER_PID,table),sizeof(MIB_TCP6ROW_OWNER_PID));
    const auto* table4=reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(ipv4.data());
    for(DWORD i=0;i<table4->dwNumEntries;++i){
        const auto& row=table4->table[i];
        if(row.dwLocalAddr==0x0100007f) add(row.dwOwningPid,row.dwLocalPort,L"127.0.0.1");
        else if(row.dwLocalAddr==0&&trusted(row.dwOwningPid))networkExposed.emplace(row.dwOwningPid,decodePort(row.dwLocalPort));
    }
    const auto* table6=reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(ipv6.data());
    for(DWORD i=0;i<table6->dwNumEntries;++i){
        const auto& row=table6->table[i];
        const bool loopback=row.ucLocalAddr[15]==1&&std::all_of(row.ucLocalAddr,row.ucLocalAddr+15,[](unsigned char b){return b==0;});
        if(loopback)add(row.dwOwningPid,row.dwLocalPort,L"::1");
        else if(std::all_of(row.ucLocalAddr,row.ucLocalAddr+16,[](unsigned char b){return b==0;})&&trusted(row.dwOwningPid))
            networkExposed.emplace(row.dwOwningPid,decodePort(row.dwLocalPort));
    }
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& endpoint) {
        return networkExposed.count({endpoint.pid, endpoint.port}) != 0;
    }), result.end());
    listenersource::checkpoint();
    if (result.empty()) throw std::runtime_error(networkExposed.empty() ? "live-debug-unavailable" : "live-debug-not-loopback");
    if (result.size() > 16) throw std::runtime_error("live-context-ambiguous");
    return result;
}
inline bool stillOwned(const std::wstring& steam, const Endpoint& endpoint, DWORD expectedSteamPid = 0, const ListenerProcessLease* held = nullptr) {
    const auto current = listeners(steam, expectedSteamPid, held);
    return std::find(current.begin(), current.end(), endpoint) != current.end();
}
inline std::wstring widenAscii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
inline void requireStatus(HINTERNET request, DWORD expected, Json* failure = nullptr, steamfailure::SourceStage stage = steamfailure::SourceStage::Discovery) {
    DWORD code = 0, bytes = sizeof(code);
    const bool queried = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &bytes, nullptr) != FALSE;
    if (!queried || code != expected) {
        if (failure) *failure = steamfailure::native(queried ? steamfailure::Domain::Http : steamfailure::Domain::WinHttp, queried ? code : GetLastError(), stage);
        throw std::runtime_error("live-transport-failed");
    }
}
inline std::string getTargets(HINTERNET connection, ULONGLONG deadline = 0, Json* failure = nullptr) {
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr)) {
        if (failure) *failure = steamfailure::native(steamfailure::Domain::WinHttp, GetLastError(), steamfailure::SourceStage::Discovery);
        throw std::runtime_error("live-debug-unavailable");
    }
    requireStatus(request.h, 200, failure);
    std::string result;
    char buffer[8192]; DWORD read = 0;
    for (;;) {
        if (deadline) {
            const auto now = GetTickCount64();
            if (now >= deadline) throw std::runtime_error("live-ui-read-timeout");
            const auto slice = std::max<DWORD>(1, std::min<DWORD>(500, static_cast<DWORD>(deadline - now) / 4));
            WinHttpSetTimeouts(request.h, slice, slice, slice, slice);
        }
        if (!WinHttpReadData(request.h, buffer, sizeof(buffer), &read)) {
            if (failure) *failure = steamfailure::native(steamfailure::Domain::WinHttp, GetLastError(), steamfailure::SourceStage::Discovery);
            throw std::runtime_error("live-debug-unavailable");
        }
        if (!read) break;
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
    explicit BoundedSocket(HINTERNET socket, DWORD timeoutMs = 16000) : h(socket), watchdog([this, timeoutMs] {
        std::unique_lock<std::mutex> lock(mx);
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return finished; }) && h) {
            WinHttpCloseHandle(h); h = nullptr;
        }
    }) {}
    ~BoundedSocket() {
        { std::lock_guard<std::mutex> lock(mx); finished = true; }
        cv.notify_all(); watchdog.join();
        if (h) WinHttpCloseHandle(h);
    }
};
// Validate the common fixed envelope before any consumer's typed .value().
// Optional success fields stay optional; unknown payload fields remain with
// their original consumer. This never grants action or retry authority.
inline bool replyEnvelopeValid(const Json& value) {
    if (!value.is_object()) return false;
    const auto ok = value.find("ok");
    if (ok == value.end() || !ok->is_boolean()) return false;
    for (const auto* key : {"mutated", "requestDispatched"}) {
        const auto field = value.find(key);
        if (field != value.end() && !field->is_boolean()) return false;
    }
    const auto reason = value.find("reason");
    if (reason != value.end() && !reason->is_string()) return false;
    const auto account = value.find("observedAccount");
    if (account != value.end()) {
        if (!account->is_number_integer() ||
            (account->is_number_unsigned() ? account->get<std::uint64_t>() == 0 || account->get<std::uint64_t>() > 0xffffffffULL :
                account->get<std::int64_t>() <= 0 || account->get<std::int64_t>() > 0xffffffffLL)) return false;
    }
    return true;
}
enum class ModeTransportWait { Immediate, SharedObserver };
struct ModeTransportObservation {
    enum class Wait { None, Acquired, Busy, Cancelled };
    Wait wait = Wait::None;
    DWORD waitMs = 0;
    bool rendererBusy = false;
    TransportHolderSample holderAtBusy;
    unsigned sleepInspectExtraSlices = 0;
};
inline void appendModeTransportHolder(Json& detail, const ModeTransportObservation& observation) {
    if(observation.wait!=ModeTransportObservation::Wait::Busy)return;
    const auto& sample=observation.holderAtBusy;
    detail["modeTransportHolderAtBusy"]=transportHolderName(sample.holder);
    if(sample.holder!=TransportHolder::Unknown){
        detail["modeTransportObservedHeldMs"]=sample.heldMs;
        if(sample.observedTick)detail["modeTransportHolderSampleTick"]=sample.observedTick;
    }
    if(const auto* operation=transportSleepOperationName(sample.holder))detail["modeTransportSleepOperationAtBusy"]=operation;
}
inline const char* modeTransportWaitName(ModeTransportObservation::Wait wait) noexcept {
    using Wait = ModeTransportObservation::Wait;
    switch(wait){case Wait::Acquired:return "acquired";case Wait::Busy:return "busy";case Wait::Cancelled:return "cancelled";default:return nullptr;}
}
inline bool modeRendererReportedBusy(const Json& reply) noexcept {
    // Only called at the accepted renderer reply boundary, never for native failures.
    try {
        if(!reply.is_object())return false;
        const auto ok=reply.find("ok"),mutated=reply.find("mutated"),reason=reply.find("reason"),stage=reply.find("stage");
        return ok!=reply.end()&&ok->is_boolean()&&!ok->get<bool>()&&
            mutated!=reply.end()&&mutated->is_boolean()&&!mutated->get<bool>()&&
            reason!=reply.end()&&reason->is_string()&&reason->get_ref<const std::string&>()=="live-busy"&&
            stage!=reply.end()&&stage->is_string()&&stage->get_ref<const std::string&>()=="preflight";
    } catch(...) { return false; }
}
inline Json run(const std::wstring& steam, const Json& request, const std::function<bool()>& admitAction = {}, DWORD shortReadMs = 0,
    const std::function<bool()>& ownerCurrent = {}, const steamsession::Session* expectedSession = nullptr,
    steamcommand::Disposition* localCommandDisposition = nullptr, ModeTransportWait modeWait = ModeTransportWait::Immediate,
    ModeTransportObservation* modeObservation = nullptr) {
    if(localCommandDisposition)*localCommandDisposition=steamcommand::Disposition::Unavailable;
    // Interactive short reads do not queue. Only the public mode owner's
    // in-flight read can absorb brief local contention within its own budget.
    const auto field = request.is_object() ? request.find("operation") : request.end();
    if (!request.is_object() || field == request.end() || !field->is_string())
        return {{"ok", false}, {"reason", "invalid-live-operation"}, {"mutated", false}, {"requestDispatched", false}};
    const auto readOperation = field->get<std::string>();
    const auto policy = steamrequest::classify(readOperation);
    if (!policy.known)
        return {{"ok", false}, {"reason", "invalid-live-operation"}, {"mutated", false}, {"requestDispatched", false}};
    // Copy caller identity before callbacks or waiting; never borrow mutable
    // consumer storage through the queue. A present-but-invalid identity fails.
    const auto expected = expectedSession ? std::optional<steamsession::Session>(*expectedSession) : std::nullopt;
    if (expected && (!expected->pid || !expected->created || expected->root.empty() ||
        normalizedPath(expected->root) != normalizedPath(steam)))
        return {{"ok",false},{"reason","steam-session-changed"},{"mutated",false},{"requestDispatched",false}};
    // ownerCurrent is explicitly pure and repeatable. Legacy admitAction may
    // reserve/consume recovery authority and stays at the final dispatch gate.
    // Only the pure predicate may reject obsolete work before discovery.
    const auto current = [&]() noexcept { try { return !ownerCurrent || ownerCurrent(); } catch (...) { return false; } };
    const auto cancelled = [] { return Json{{"ok", false}, {"reason", "live-action-cancelled"}, {"mutated", false},
        {"stage", "native-live-admission"}, {"requestDispatched", false}}; };
    if (!current()) return cancelled();
    const bool runningLinkRead = isRunningLinkRead(request);
    const bool lightGamepadLease = policy.lightGamepadLease;
    const bool shortRead = shortReadMs && (policy.shortRead || runningLinkRead);
    const bool briefModeWait = modeWait == ModeTransportWait::SharedObserver && shortRead &&
        readOperation == "ui.mode.get" && expected && ownerCurrent;
    auto* const modeFacts = briefModeWait ? modeObservation : nullptr;
    const auto modeDeadline = [&]() -> ULONGLONG {
        if (!briefModeWait) return 0;
        const auto end = GetTickCount64() + (std::min)(shortReadMs, DWORD{4000});
        return listenersource::operationEnd ? (std::min)(end, listenersource::operationEnd) : end;
    }();
    const auto modeObsolete = [&]() noexcept {
        if (!modeDeadline) return false;
        try {
            return GetTickCount64() >= modeDeadline ||
                (listenersource::operationCurrent && (!*listenersource::operationCurrent || !(*listenersource::operationCurrent)()));
        } catch (...) { return true; }
    };
    std::optional<steamcommand::Scope> operationScope;
    if(!policy.lightGamepadLease&&(policy.coordinatedEffect||menuGameMutation(request,readOperation))&&!steamcommand::remote){
        if(!expected)return cancelled();
        operationScope.emplace(steamcommand::Target{expected->pid,expected->created,expected->root});
        if(!*operationScope){
            if(localCommandDisposition&&operationScope->failure()==steamcommand::Disposition::Busy)
                *localCommandDisposition=steamcommand::Disposition::Busy;
            return {{"ok",false},{"reason","steam-command-busy"},{"mutated",false},{"requestDispatched",false}};
        }
    }
    std::unique_lock<std::timed_mutex> serial(transportMutex, std::defer_lock);
    if (!lightGamepadLease) {
        if (shortRead || policy.tryLock) {
            if (!serial.try_lock()) {
                if (briefModeWait) {
                    constexpr ULONGLONG contentionMs = 250;
                    if (!current() || modeObsolete()) return cancelled();
                    const auto started = GetTickCount64();
                    if (started >= modeDeadline) return cancelled();
                    (void)serial.try_lock_for(std::chrono::milliseconds((std::min)(contentionMs, modeDeadline - started)));
                    bool revoked = !current() || modeObsolete();
                    TransportHolderSample busy;
                    unsigned extraSlices = 0;
                    if (!revoked && !serial.owns_lock()) {
                        busy = TransportHoldObservation::sample();
                        // An observed sleep inspection can finish on our original
                        // read slot. No new read or retry is scheduled; a changed
                        // or unknown holder does not renew this lock wait.
                        const auto inspectStart = busy.startedTick;
                        const auto lockDeadline = (std::min)(modeDeadline, started + 3 * contentionMs);
                        while (extraSlices < 2 && busy.holder == TransportHolder::SleepInspect && busy.startedTick == inspectStart) {
                            const auto now = GetTickCount64();
                            if (now >= lockDeadline) { revoked = now >= modeDeadline; break; }
                            const auto remainingMs = lockDeadline - now;
                            ++extraSlices;
                            (void)serial.try_lock_for(std::chrono::milliseconds((std::min)(contentionMs, remainingMs)));
                            revoked = !current() || modeObsolete();
                            if (revoked || serial.owns_lock()) break;
                            busy = TransportHoldObservation::sample();
                            // Do not open another slice after waiting out the
                            // last budget, even if the coarse clock lags it.
                            if (remainingMs <= contentionMs) { revoked = lockDeadline == modeDeadline; break; }
                        }
                    }
                    if (modeFacts) {
                        using Wait = ModeTransportObservation::Wait;
                        modeFacts->waitMs = static_cast<DWORD>((std::min)(GetTickCount64() - started, ULONGLONG{60000}));
                        modeFacts->wait = revoked ? Wait::Cancelled : serial.owns_lock() ? Wait::Acquired : Wait::Busy;
                        modeFacts->sleepInspectExtraSlices = extraSlices;
                        if(modeFacts->wait==Wait::Busy)modeFacts->holderAtBusy=busy;
                    }
                    if (revoked) return cancelled();
                }
                if (!serial.owns_lock()) return {{"ok", false}, {"reason", "live-busy"}, {"mutated", false}};
            }
        } else {
            // Only an already queued request waits in finite slices. No idle
            // worker or timer is created; successful free-lock ordering stays.
            for (;;) {
                DWORD waitMs = 50;
                if (listenersource::operationEnd) {
                    const auto now = GetTickCount64();
                    if (now >= listenersource::operationEnd) return cancelled();
                    waitMs = static_cast<DWORD>((std::min)(static_cast<ULONGLONG>(waitMs), listenersource::operationEnd - now));
                }
                if (serial.try_lock_for(std::chrono::milliseconds(waitMs))) {
                    if (listenersource::operationEnd && GetTickCount64() >= listenersource::operationEnd) return cancelled();
                    break;
                }
                if (!current()) return cancelled();
            }
        }
    }
    const TransportHoldObservation holder(serial,classifyTransportHolder(readOperation,policy));
    if (!current() || modeObsolete()) return cancelled();
    const ULONGLONG deadline = modeDeadline ? modeDeadline : shortRead ? GetTickCount64() + std::min<DWORD>(shortReadMs, 4000) : 0;
    listenersource::Deadline listenerDeadline(deadline?deadline:GetTickCount64()+16000);
    const auto remaining = [&]() -> DWORD {
        const auto now = GetTickCount64();
        if (deadline && now >= deadline) throw std::runtime_error("live-ui-read-timeout");
        return deadline ? static_cast<DWORD>(deadline - now) : 16000;
    };
    const auto shortTimeouts = [&](HINTERNET handle) {
        if (shortRead) {
            const auto slice = std::max<DWORD>(1, std::min<DWORD>(500, remaining() / 4));
            WinHttpSetTimeouts(handle, slice, slice, slice, slice);
        }
    };
    const auto operation = request.value("operation", std::string{});
    DWORD requestSteamPid = 0;
    if (!requestProcessPid(request,policy,requestSteamPid))
        return {{"ok",false},{"reason","steam-session-changed"},{"mutated",false},{"requestDispatched",false}};
    const DWORD expectedSteamPid = expected ? expected->pid : requestSteamPid;
    if (expected && requestSteamPid && requestSteamPid != expected->pid)
        return {{"ok",false},{"reason","steam-session-changed"},{"mutated",false},{"requestDispatched",false}};
    SteamProcessLease process;
    bool sent = false;
    Json failure,settledNativeReturn;
    try {
        if (expected && !process.bind(*expected)) throw std::runtime_error("steam-session-changed");
        if (!current()) return cancelled();
        // Keep the caller's pure revocation visible inside the existing
        // process/TCP discovery checkpoints, and retain any inherited scope.
        const auto* inheritedCurrent = listenersource::operationCurrent;
        listenersource::Current acquisitionCurrent([&] {
            return current() && (!inheritedCurrent || (*inheritedCurrent && (*inheritedCurrent)()));
        });
        const auto candidates = listeners(steam, expectedSteamPid);
        HttpHandle session(WinHttpOpen(L"YMCC-SteamLive/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        WinHttpSetTimeouts(session.h, 1000, 1000, 1500, 1500);
        shortTimeouts(session.h);
        std::string discoveryError="live-context-unavailable";
        Json candidateFailure;
        const auto started=GetTickCount64();
        const auto selected=steamdiscovery::select<std::string>(steamdiscovery::Kind::Live,candidates,
            [&]{if(shortRead)remaining();if(GetTickCount64()-started>5000)throw std::runtime_error("live-discovery-timeout");
                if(!current()||!process.current())throw std::runtime_error("live-action-cancelled");},
            [](const auto&){return true;},
            [&](const auto& candidate)->std::optional<std::string>{
                candidateFailure=Json{};shortTimeouts(session.h);
                HttpHandle probe(WinHttpConnect(session.h,candidate.host.c_str(),candidate.port,0));
                return targetPath(Json::parse(getTargets(probe.h,deadline,&candidateFailure)),candidate);
            },
            [&](const std::exception& error,bool hasSelection){
                if(!hasSelection&&!candidateFailure.is_null())failure=std::move(candidateFailure);
                if(std::string_view(error.what())=="live-context-unavailable")discoveryError=error.what();
            });
        if(!selected.endpoint)throw std::runtime_error(discoveryError);
        const auto endpoint=*selected.endpoint;
        const auto& path=*selected.payload;
        failure = Json{}; // A rejected discovery candidate is not this endpoint's failure.
        if (!process.current() || !stillOwned(steam, endpoint, expectedSteamPid)) throw std::runtime_error("live-debug-owner-mismatch");
        WinHttpSetTimeouts(session.h, 1500, 1500, 2000, 3000);
        shortTimeouts(session.h);
        HttpHandle connection(WinHttpConnect(session.h, endpoint.host.c_str(), endpoint.port, 0));
        HttpHandle upgrade(WinHttpOpenRequest(connection.h, L"GET", widenAscii(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
        DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(upgrade.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));
        if (!WinHttpSetOption(upgrade.h, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
            !WinHttpSendRequest(upgrade.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(upgrade.h, nullptr)) {
            failure = steamfailure::native(steamfailure::Domain::WinHttp, GetLastError(), steamfailure::SourceStage::Upgrade);
            throw std::runtime_error("live-transport-failed");
        }
        requireStatus(upgrade.h, 101, &failure, steamfailure::SourceStage::Upgrade);
        const auto socketBudget = remaining();
        HINTERNET socket = WinHttpWebSocketCompleteUpgrade(upgrade.h, 0);
        if (!socket) {
            failure = steamfailure::native(steamfailure::Domain::WinHttp, GetLastError(), steamfailure::SourceStage::Upgrade);
            throw std::runtime_error("live-transport-failed");
        }
        HttpHandle pendingSocket(socket);
        BoundedSocket bounded(socket, socketBudget);
        pendingSocket.h = nullptr; // The fully constructed watchdog now owns this socket.
        if (!process.current() || !stillOwned(steam, endpoint, expectedSteamPid)) throw std::runtime_error("live-debug-owner-mismatch");
        std::string message;
        {
            std::string expression = std::string(lightGamepadLease ? kLightGamepadScript : operation == "ui.mode.get" ? kUiModeScript : kScript) + "(" + request.dump() + ")";
            Json::object_t params;
            params["expression"] = std::move(expression);
            params["returnByValue"] = true;
            params["awaitPromise"] = true;
            params["timeout"] = shortRead ? remaining() : 14000;
            Json::object_t packet;
            packet["id"] = 1;
            packet["method"] = "Runtime.evaluate";
            packet["params"] = std::move(params);
            message = Json(std::move(packet)).dump();
        }
        std::optional<steamcommand::Dispatch> coordinatedDispatch;
        if(!policy.lightGamepadLease&&(policy.coordinatedEffect||menuGameMutation(request,readOperation))){
            coordinatedDispatch.emplace(steamcommand::Effect::Mutation);
            if(!*coordinatedDispatch){
                if(localCommandDisposition&&!steamcommand::remote)*localCommandDisposition=steamcommand::Disposition::Busy;
                return {{"ok",false},{"reason","steam-command-busy"},{"mutated",false},{"requestDispatched",false}};
            }
        }
        // Recheck event/config/power admission after discovery and socket setup.
        // A cancelled action must not be sent merely because it entered the queue earlier.
        if (!current() || !process.current() || (admitAction && !admitAction()) || !process.current() || !current()) return {{"ok", false}, {"reason", "live-action-cancelled"}, {"mutated", false},
            {"stage", "native-live-pre-dispatch"}, {"requestDispatched", false}};
        sent = true;
        const auto sendError = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(message.data()), static_cast<DWORD>(message.size()));
        if (sendError != NO_ERROR) {
            failure = steamfailure::native(steamfailure::Domain::WinHttp, sendError, steamfailure::SourceStage::Send);
            throw std::runtime_error("live-transport-uncertain");
        }
        std::string response;
        unsigned messages = 0, fragments = 0;
        for (;;) {
            if (shortRead && (++fragments > 256 || GetTickCount64() >= deadline)) throw std::runtime_error("live-ui-read-timeout");
            char buffer[16384]; DWORD read = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            const auto receiveError = WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &read, &kind);
            if (receiveError != NO_ERROR || kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                failure = receiveError != NO_ERROR ? steamfailure::native(steamfailure::Domain::WinHttp, receiveError, steamfailure::SourceStage::Receive) :
                    steamfailure::normalize({{"schema",1},{"domain","websocket-close"},{"sourceStage","receive"}});
                if (receiveError == NO_ERROR) {
                    USHORT closeStatus = 0;
                    BYTE closeReason[WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH];
                    DWORD closeReasonLength = 0;
                    if (WinHttpWebSocketQueryCloseStatus(socket, &closeStatus, closeReason, sizeof(closeReason), &closeReasonLength) == NO_ERROR)
                        failure = steamfailure::native(steamfailure::Domain::WebSocketClose, closeStatus, steamfailure::SourceStage::Receive);
                }
                throw std::runtime_error("live-transport-uncertain");
            }
            if (kind != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                throw std::runtime_error("live-response-invalid");
            response.append(buffer, read);
            if (response.size() > 1024 * 1024) throw std::runtime_error("live-response-invalid");
            if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
            const auto packet = Json::parse(response); response.clear();
            if (++messages > 128) throw std::runtime_error("live-response-invalid");
            if (packet.value("id", 0) != 1) continue;
            if (packet.contains("error") || !packet.contains("result") || packet["result"].contains("exceptionDetails")) {
                failure = steamfailure::cdp(packet);
                throw std::runtime_error("live-api-error");
            }
            const auto& remote = packet["result"]["result"];
            if (!remote.contains("value") || !remote["value"].is_object()) throw std::runtime_error("live-response-invalid");
            // A settled reply from an exited/revoked original instance cannot
            // become a fresh success receipt. Do not repeat finalAdmit or replay.
            if (!current() || !process.current()) {
                // The original native result is diagnostic-only even when a
                // request is no longer current. Never acknowledge or replay it.
                if(operation=="sleep.input-resume"){
                    try{settledNativeReturn=steamreturnevidence::bounded(remote["value"]);}catch(...){}
                }
                // A late caught failure is evidence from this original request,
                // never a valid result or permission to retry. Keep safe facts
                // before rejecting it; successful/malformed values add none.
                try {
                    const auto& value = remote["value"];
                    const auto ok = value.find("ok"), sourceFailure = value.find("steamFailure");
                    if (ok != value.end() && ok->is_boolean() && !ok->get<bool>() &&
                        sourceFailure != value.end() && sourceFailure->is_object())
                        failure = steamfailure::normalize(*sourceFailure);
                } catch (...) {} // Diagnostics cannot change original rejection.
                throw std::runtime_error("steam-session-changed");
            }
            if (!replyEnvelopeValid(remote["value"])) throw std::runtime_error("live-response-invalid");
            if(coordinatedDispatch)coordinatedDispatch->complete(remote["value"].value("ok",false));
            if(modeFacts)modeFacts->rendererBusy=modeRendererReportedBusy(remote["value"]);
            return remote["value"];
        }
    } catch (const std::exception& e) {
        const bool uncertainMutation=sent&&(menuGameMutation(request,readOperation)||!shortRead||lightGamepadLease||readOperation=="menu.qam.retry");
        Json result={{"ok", false}, {"reason", uncertainMutation ? "live-transport-uncertain" : e.what()}, {"mutated", uncertainMutation}, {"requestDispatched",sent}};
        if (!failure.is_null()) result["steamFailure"] = std::move(failure);
        if(!settledNativeReturn.is_null()){
            result["nativeReturn"]=std::move(settledNativeReturn);
            result["nativeReturnEvidenceVersion"]="steam-native-return-evidence-v1";
        }
        return result;
    }
}
// Main-process egress borrows the existing source owner through transport
// return. Authenticated delegated children retain the original run entry.
template<class Source>
inline Json runOwned(Source& source, const std::wstring& steam, const Json& request,
    const std::function<bool()>& admitAction = {}, DWORD shortReadMs = 0,
    const std::function<bool()>& ownerCurrent = {}, const steamsession::Session* expectedSession = nullptr,
    bool borrowCommandReservation = false, steamcommand::Disposition* localCommandDisposition = nullptr,
    ModeTransportWait modeWait = ModeTransportWait::Immediate, ModeTransportObservation* modeObservation = nullptr) {
    if(localCommandDisposition)*localCommandDisposition=steamcommand::Disposition::Unavailable;
    const auto cancelled = [] { return Json{{"ok", false}, {"reason", "live-action-cancelled"}, {"mutated", false},
        {"stage", "native-live-admission"}, {"requestDispatched", false}}; };
    const auto reading = source.sourceRead();
    if (!reading) return cancelled();
    const auto view = reading.view();
    const auto current = [&]() noexcept {
        try { return view.current() && (!ownerCurrent || ownerCurrent()); }
        catch (...) { return false; }
    };
    if (!current()) return cancelled();
    std::optional<steamcommand::Scope> ingress;
    if(request.is_object()){
        const auto operation=request.find("operation");
        if(operation!=request.end()&&operation->is_string()&&
            steamrequest::classify(operation->get_ref<const std::string&>()).known&&
            !steamrequest::classify(operation->get_ref<const std::string&>()).lightGamepadLease&&
            (steamrequest::classify(operation->get_ref<const std::string&>()).coordinatedEffect||
             menuGameMutation(request,operation->get_ref<const std::string&>()))){
            if(!expectedSession||!expectedSession->pid||!expectedSession->created||expectedSession->root.empty())return cancelled();
            ingress.emplace(steamcommand::Target{expectedSession->pid,expectedSession->created,expectedSession->root},steamcommand::Plan::Single,borrowCommandReservation);
            if(!*ingress){
                if(localCommandDisposition&&ingress->failure()==steamcommand::Disposition::Busy)
                    *localCommandDisposition=steamcommand::Disposition::Busy;
                return {{"ok",false},{"reason","steam-command-busy"},{"mutated",false},{"requestDispatched",false}};
            }
        }
    }
    return run(steam, request, admitAction, shortReadMs, current, expectedSession, localCommandDisposition, modeWait, modeObservation);
}
inline bool canDefer(const Json& result) {
    // Deferral can replay a saved write intent. Missing/malformed fields are
    // not evidence that the previous request failed without a possible write.
    if (!result.is_object()) return false;
    const auto ok = result.find("ok"), mutated = result.find("mutated"), why = result.find("reason");
    if (ok == result.end() || !ok->is_boolean() || ok->get<bool>() ||
        mutated == result.end() || !mutated->is_boolean() || mutated->get<bool>() ||
        why == result.end() || !why->is_string()) return false;
    const auto& reason = why->get_ref<const std::string&>();
    return reason == "live-read-unavailable" || reason == "live-busy" || reason == "steam-session-starting" || reason == "steam-session-changed" || reason == "steam-account-changed" || reason == "live-debug-unavailable" || reason == "live-context-unavailable" || reason == "live-api-unavailable" || reason == "live-controller-not-connected";
}
}
