#ifdef YMCC_STEAM_OBSERVER_SOURCE_OWNER_ONLY
#include "fixtures/steam_source_owner_cases.inc"
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <atomic>
static std::atomic<unsigned> registryReads{0};
static LSTATUS WINAPI countedRegGetValueW(HKEY key,LPCWSTR sub,LPCWSTR value,DWORD flags,LPDWORD type,PVOID data,LPDWORD size) {
    ++registryReads; return RegGetValueW(key,sub,value,flags,type,data,size);
}
#define RegGetValueW countedRegGetValueW
#include "steam_session_observer.h"
#undef RegGetValueW

#include <shellapi.h>
// Instrument actual pin acquisition; every wrapper calls its original Win32 API.
static std::atomic<unsigned> nativeReadProcessQueries{0};
static HANDLE WINAPI nativeReadCountedOpenProcess(DWORD a,BOOL i,DWORD p){
    ++nativeReadProcessQueries;return ::OpenProcess(a,i,p);
}
static BOOL WINAPI nativeReadCountedProcessTimes(HANDLE h,LPFILETIME b,LPFILETIME e,LPFILETIME k,LPFILETIME u){
    ++nativeReadProcessQueries;return ::GetProcessTimes(h,b,e,k,u);
}
static BOOL WINAPI nativeReadCountedQueryImage(HANDLE h,DWORD f,LPWSTR n,PDWORD s){
    ++nativeReadProcessQueries;return ::QueryFullProcessImageNameW(h,f,n,s);
}
static BOOL WINAPI nativeReadCountedProcessSession(DWORD p,DWORD* s){
    ++nativeReadProcessQueries;return ::ProcessIdToSessionId(p,s);
}
#define OpenProcess nativeReadCountedOpenProcess
#define GetProcessTimes nativeReadCountedProcessTimes
#define QueryFullProcessImageNameW nativeReadCountedQueryImage
#define ProcessIdToSessionId nativeReadCountedProcessSession
#include "steam_native_read_scope.h"
#undef ProcessIdToSessionId
#undef QueryFullProcessImageNameW
#undef GetProcessTimes
#undef OpenProcess
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ymcc::steamsession;
static int tests = 0;
static void expect(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
    ++tests; std::cout << "PASS " << label << '\n';
}
template<class F> static bool until(F f, DWORD timeoutMs = 3000) {
    const auto end = GetTickCount64() + timeoutMs;
    while (!f()) { if (GetTickCount64() >= end) return false; Sleep(5); }
    return true;
}
struct Registry {
    std::wstring path;
    explicit Registry(std::wstring p) : path(std::move(p)) {}
    ~Registry() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
    void number(const wchar_t* name, DWORD value, bool active = true) {
        HKEY key = nullptr; const auto p = path + (active ? L"\\ActiveProcess" : L"");
        if (RegCreateKeyExW(HKEY_CURRENT_USER, p.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) throw std::runtime_error("test registry create");
        const auto r = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key); if (r) throw std::runtime_error("test registry value");
    }
    void root(const std::wstring& value) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) throw std::runtime_error("test registry root");
        const auto r = RegSetValueExW(key, L"SteamPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
        RegCloseKey(key); if (r) throw std::runtime_error("test root value");
    }
};
struct Child {
    HANDLE stop = nullptr; PROCESS_INFORMATION pi{};
    explicit Child(const std::wstring& exe) {
        const auto name = L"Local\\YMCCSteamObserverTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        stop = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        STARTUPINFOW si{sizeof(si)};
        std::wstring command = L"\"" + exe + L"\" --child \"" + name + L"\"";
        if (!stop || !CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) throw std::runtime_error("isolated child start");
        CloseHandle(pi.hThread); pi.hThread = nullptr;
    }
    void finish() { if (stop) SetEvent(stop); if (pi.hProcess) WaitForSingleObject(pi.hProcess, 3000); }
    ~Child() { finish(); if (pi.hProcess) CloseHandle(pi.hProcess); if (stop) CloseHandle(stop); }
};
static ULONGLONG cpuTime() {
    FILETIME b{},e{},k{},u{};
    GetProcessTimes(GetCurrentProcess(), &b,&e,&k,&u);
    return ((static_cast<ULONGLONG>(k.dwHighDateTime)<<32)|k.dwLowDateTime) + ((static_cast<ULONGLONG>(u.dwHighDateTime)<<32)|u.dwLowDateTime);
}
#include <functional>
#include <fstream>
#include <limits>
#include <mutex>
#include "json.hpp"
using json=nlohmann::json;
static const auto contextRegistryPath=L"Software\\YeManCC-TrackerProjectionSelftest\\"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
static Observer g_steamSettingsObserver(contextRegistryPath);
static std::atomic<bool> g_exitRequested{false};
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_gameRulesEpoch{8},generation{3};
static unsigned long long currentPowerGeneration(){return generation.load();}
struct NativeDetectedGame {
    DWORD pid = 0;
    std::wstring name;
    std::wstring title;
    std::wstring path;
    SIZE_T workingSet = 0;
    ULONGLONG processCreated = 0;
    bool whitelisted = false;
    bool customConfigured = false;
    std::wstring whitelistRule;
};
#include "game_steam_scope.h"
static std::string W2U(const wchar_t* w, int len = -1) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
static std::string W2U(const std::wstring& w) { return W2U(w.c_str(), (int)w.size()); }

static std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}



namespace SleepRecoveryApi {struct Tail {std::string bytes;json meta;};
static Tail readTail(const std::wstring& path,DWORD cap=2*1024*1024) {
    Tail t;t.meta={{"available",false},{"truncated",false},{"limitBytes",cap}};
    const DWORD attr=GetFileAttributesW(path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES || (attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        t.meta["reason"]="missing-or-not-regular-file";return t;
    }
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){t.meta["reason"]="read-denied";t.meta["win32Error"]=GetLastError();return t;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(h,&size)||size.QuadPart<0){CloseHandle(h);t.meta["reason"]="size-unavailable";return t;}
    const auto take=static_cast<DWORD>(std::min<LONGLONG>(size.QuadPart,cap));
    LARGE_INTEGER offset{};offset.QuadPart=size.QuadPart-take;
    DWORD got=0;t.bytes.resize(take);
    const bool ok=SetFilePointerEx(h,offset,nullptr,FILE_BEGIN)&&ReadFile(h,t.bytes.data(),take,&got,nullptr);
    const DWORD error=ok?0:GetLastError();CloseHandle(h);
    if(!ok){t.bytes.clear();t.meta["reason"]="read-failed";t.meta["win32Error"]=error;return t;}
    t.bytes.resize(got);t.meta["available"]=true;t.meta["originalBytes"]=size.QuadPart;
    t.meta["copiedBytes"]=got;t.meta["truncated"]=offset.QuadPart>0;
    return t;
}
}
#include "sleep_steam_capture.h"
namespace SleepSteamRecovery {
using namespace ymcc::sleepsteam;
struct Game {DWORD pid=0;ULONGLONG created=0;std::wstring path;};
struct Session {
    DWORD account=0,pid=0,registeredPid=0;ULONGLONG created=0;std::wstring root;
    std::string source="unverified",reason="steam-process-unverified",accountSource="unverified";
    bool accountPinned=false,liveAccountPinned=false,foreignRegistryAccountIgnored=false;
};
struct NativePrimaryRead {NativePrimaryProcessInventory inventory;json log;};
static json nativePrimaryJoin(const NativePrimaryRead& primary,const TrackedProcessInventory& tracker,std::uint32_t app,DWORD pid) {
    json result={{"source","steam-local-native-procid-diagnostic"},{"status","unknown"},{"originalAllowed",false},{"nativePrimaryVerified",false},{"expectedPid",pid},{"appid",app},{"log",primary.log}};
    unsigned members=0;bool originalTracked=false;for(const auto& row:tracker.rows)if(row.appid==app){++members;if(row.pid==pid)originalTracked=true;}
    result["trackedMemberCount"]=members;
    if(!tracker.complete||!primary.inventory.complete){result["status"]="unverifiable";result["reason"]=primary.inventory.reason;return result;}
    const auto found=primary.inventory.rows.find(app);
    if(found==primary.inventory.rows.end()){
        // Existing exact single-member authority is retained, explicitly without
        // claiming native-primary proof. Multiple members never use this fallback.
        result["status"]=members==1&&originalTracked?"unique-tracker-native-primary-unobserved":"unknown";
        result["reason"]=primary.inventory.reason.empty()?"steam-native-primary-not-observed":primary.inventory.reason;
        result["originalAllowed"]=members==1&&originalTracked;return result;
    }
    const auto& row=found->second;result["nativePrimaryPid"]=row.pid;result["at"]=row.at;result["lineInTail"]=row.line;result["label"]=row.label;
    if(row.removed){result["status"]="removed";return result;}
    if(!row.local){result["status"]="non-local-record";return result;}
    result["nativePrimaryVerified"]=true;result["status"]=row.pid==pid?"matched":"different-instance";result["originalAllowed"]=row.pid==pid&&originalTracked;return result;
}
static std::string localSecond(ULONGLONG created) {
    ULARGE_INTEGER raw{};raw.QuadPart=created;FILETIME utc{raw.LowPart,raw.HighPart},local{};SYSTEMTIME at{};
    if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&at))return {};
    char value[32]{};snprintf(value,sizeof(value),"%04u-%02u-%02u %02u:%02u:%02u",at.wYear,at.wMonth,at.wDay,at.wHour,at.wMinute,at.wSecond);return value;
}
}
static unsigned fixtureProjectionSourceReads=0,fixtureProjectionClockReads=0;
static unsigned fixtureFreshGameReads=0,fixtureFreshConsoleReads=0;
static std::function<void(const std::wstring&)> fixtureFreshReadHook;
static json sourceChecks=json::array();
static void sourceCheck(const char* name,bool pass){sourceChecks.push_back({{"name",name},{"pass",pass}});}
static json freshChecks=json::array();
static void freshCheck(const char* name,bool pass){freshChecks.push_back({{"name",name},{"pass",pass}});}
static std::atomic<bool> g_sleepRecoveryAutoEnabled{true};
static std::atomic<unsigned long long> g_sleepRecoveryAutoConfigEpoch{1};
static ULONGLONG sgNowFileTime() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul; ul.LowPart = ft.dwLowDateTime; ul.HighPart = ft.dwHighDateTime;
    return ul.QuadPart;
}
static std::function<void()> fixtureProjectionAfterRead;
#include "steam_game_tracker_facts.h"
namespace ymcc::steamgamefacts {
SteamLogSourceIdentity MainTrackerFactsAdapter::sourceKey(const SteamLogIdentity& expected){
    const auto actual=trackerSteamReceipt(g_steamSettingsObserver.identitySnapshot());
    if(!actual.present||!actual.rootKnown||!actual.windowsSessionKnown||!actual.pid||!actual.birth||!actual.processEpoch||
        actual.pid!=expected.pid||actual.birth!=expected.birth||
        ymcc::sleepsteam::pathKey(W2U(actual.root))!=ymcc::sleepsteam::pathKey(W2U(expected.root)))return {};
    return SteamLogSourceIdentity(new SteamLogSourceKey(actual));
}
bool MainTrackerFactsAdapter::sourceCurrent(const SteamLogSourceKey& key)noexcept{
    try{const auto now=trackerSteamReceipt(g_steamSettingsObserver.identitySnapshot());const auto& expected=key.facts();
        return now.present&&now.rootKnown&&now.windowsSessionKnown&&now.pid==expected.pid&&now.birth==expected.birth&&
            now.processEpoch==expected.processEpoch&&now.windowsSession==expected.windowsSession&&now.root==expected.root;
    }catch(...){return false;}
}
TrackerTailRead MainReadTailPort::readTail(const std::wstring& sourceKey,SteamLogKind kind) const {
    ++fixtureProjectionSourceReads;
    if(sourceKey.ends_with(L"gameprocess_log.txt"))++fixtureFreshGameReads;
    if(sourceKey.ends_with(L"console_log.txt"))++fixtureFreshConsoleReads;
    // The ONLY file-reading implementation for the public tracker producer.
    // The owner selects a fixed per-kind cap; exact source and strict completeness remain.
    auto tail = SleepRecoveryApi::readTail(sourceKey,static_cast<DWORD>(steamLogMaximumBytes(kind)));
    TrackerTailRead out;
    out.bytes = std::move(tail.bytes);
    const auto field = [&](const char* key) -> const json* {
        if (!tail.meta.is_object()) return nullptr;
        const auto value = tail.meta.find(key);
        return value == tail.meta.end() ? nullptr : &*value;
    };
    const auto flag = [&](const char* key, bool& known) {
        const auto* value = field(key); known = value && value->is_boolean();
        return known ? value->get<bool>() : false;
    };
    const auto number = [&](const char* key, bool& known) -> std::uint64_t {
        const auto* value = field(key); known = false;
        if (!value || !value->is_number_integer()) return 0;
        if (value->is_number_unsigned()) { known = true; return value->get<std::uint64_t>(); }
        const auto result = value->get<std::int64_t>();
        if (result < 0) return 0;
        known = true; return static_cast<std::uint64_t>(result);
    };
    out.available = flag("available",out.availableKnown);
    out.truncated = flag("truncated",out.truncatedKnown);
    out.originalBytes = number("originalBytes",out.originalBytesKnown);
    out.copiedBytes = number("copiedBytes",out.copiedBytesKnown);
    out.limitBytes = number("limitBytes",out.limitBytesKnown);
    const auto error = number("win32Error",out.win32ErrorKnown);
    if (error > (std::numeric_limits<std::uint32_t>::max)()) out.win32ErrorKnown = false;
    if (out.win32ErrorKnown) out.win32Error = static_cast<std::uint32_t>(error);
    const auto* reason = field("reason");
    using Reason = TrackerSourceObservation::ReadReason;
    if (!reason && out.availableKnown && out.available) out.reason = Reason::None;
    else if (reason && reason->is_string()) {
        const auto& value = reason->get_ref<const std::string&>();
        if (value == "missing-or-not-regular-file") out.reason = Reason::MissingOrNotRegular;
        else if (value == "read-denied") out.reason = Reason::ReadDenied;
        else if (value == "size-unavailable") out.reason = Reason::SizeUnavailable;
        else if (value == "read-failed") out.reason = Reason::ReadFailed;
    }
    if(fixtureProjectionAfterRead)fixtureProjectionAfterRead();
    if(fixtureFreshReadHook)fixtureFreshReadHook(sourceKey);
    return out;
}
std::string MainReadTailPort::selectedPathUtf8(const std::wstring& path) const {
    return W2U(path);
}
std::string MainReadTailPort::localSecond(std::uint64_t birth) const {
    return SleepSteamRecovery::localSecond(birth);
}
std::uint64_t MainReadTailPort::nowTick() const noexcept {
    ++fixtureProjectionClockReads;
    return GetTickCount64();
}
std::uint64_t MainReadTailPort::nowFileTime() const noexcept {
    return sgNowFileTime();
}
std::shared_ptr<const IdentitySnapshot> MainReadTailPort::identitySnapshot() const {
    // Memory-only shared producer. No registry read and no UI/CM account inference.
    return g_steamSettingsObserver.identitySnapshot();
}
TrackerObservationKey MainTrackerFactsAdapter::factKey(const ::NativeGameFactScope& selected,
    TrackerObservationPolicy policy, TrackerAccountDomain account) {
    // Unsupported until there is a real typed UI/CM proof source. Raw account
    // provenance enums and document strings cannot mint AccountBound facts.
    if (policy != TrackerObservationPolicy::ProcessOnlyObservation) return {};
    // Both global A and explicitly qualified foreground B use THEIR OWN closed
    // game scope. Do not read/replace global selection or use its generation.
    if (!nativeGameFactScopeCurrent(selected)) return {};
    const auto published = g_steamSettingsObserver.identitySnapshot();
    TrackerFactTuple fact;
    fact.policy = policy; fact.account = account;
    fact.selectedGame = {selected.pid,selected.birth,selected.path};
    fact.canonicalGamePath = ymcc::sleepsteam::pathKey(W2U(selected.path));
    fact.steam = trackerSteamReceipt(published);
    fact.canonicalSteamRoot = ymcc::sleepsteam::pathKey(W2U(fact.steam.root));
    fact.rulesEpoch = selected.rulesEpoch; fact.powerGeneration = selected.powerGeneration;
    if (!tracker_detail::validFact(fact)) return {};
    auto closed = TrackerObservationKey(new TrackerFactKey(std::move(fact),
        std::make_shared<const ::NativeGameFactScope>(selected)));
    auto shared = trackerOwner().adoptFact(std::move(closed));
    return shared && current(*shared) ? shared : TrackerObservationKey{};
}
bool MainTrackerFactsAdapter::current(const TrackerFactKey& key) noexcept {
    // Pure memory only. NEVER menu actionCurrent/allowed/foreground/OS/CDP.
    try {
    if (key.facts().policy != TrackerObservationPolicy::ProcessOnlyObservation) return false;
    if (!key.gameScope_ || !nativeGameFactScopeCurrent(*key.gameScope_)) return false;
    const auto now = trackerSteamReceipt(g_steamSettingsObserver.identitySnapshot());
    const auto& fact = key.facts();
    return now.present && now.rootKnown && now.windowsSessionKnown &&
        now.pid == fact.steam.pid && now.birth == fact.steam.birth &&
        now.processEpoch == fact.steam.processEpoch && now.accountEpoch == fact.steam.accountEpoch &&
        now.windowsSession == fact.steam.windowsSession &&
        now.root == fact.steam.root; // Same publisher receipt; no Win32 conversion in peek/current.
    } catch (...) { return false; }
}
}
static ymcc::steamgamefacts::TrackerObservationKey nativeSteamGameTrackerFactKey(
    const NativeGameFactScope& scope,
    ymcc::steamgamefacts::TrackerObservationPolicy policy = ymcc::steamgamefacts::TrackerObservationPolicy::ProcessOnlyObservation,
    ymcc::steamgamefacts::TrackerAccountDomain account = {}) {
    return ymcc::steamgamefacts::MainTrackerFactsAdapter::factKey(scope,policy,account);
}
static ymcc::steamgamefacts::TrackerPeekResult nativeSteamGameTrackerPeek(
    const ymcc::steamgamefacts::TrackerObservationKey& key,
    const ymcc::steamgamefacts::TrackerOwnerCurrent& pureOwnerCurrent) {
    return ymcc::steamgamefacts::trackerOwner().peek(key,pureOwnerCurrent);
}
static ymcc::steamgamefacts::TrackerDemandIssue nativeSteamGameTrackerBeginDemand(
    const ymcc::steamgamefacts::TrackerObservationKey& key,
    ymcc::steamgamefacts::TrackerRequestContext context,
    const ymcc::steamgamefacts::TrackerOwnerCurrent& pureOwnerCurrent) {
    return ymcc::steamgamefacts::trackerOwner().beginDemand(key,std::move(context),pureOwnerCurrent);
}
static ymcc::steamgamefacts::TrackerClaim nativeSteamGameTrackerClaim(
    ymcc::steamgamefacts::TrackerDemand demand,
    const ymcc::steamgamefacts::TrackerOwnerCurrent& pureOwnerCurrent,
    ymcc::steamgamefacts::TrackerReadMode mode) {
    return ymcc::steamgamefacts::trackerOwner().claim(std::move(demand),pureOwnerCurrent,mode);
}
static bool nativeSteamGameTrackerExecutionEligible(
    const ymcc::steamgamefacts::TrackerFlightToken& token) noexcept {
    return ymcc::steamgamefacts::trackerOwner().executionEligible(token);
}
static std::shared_ptr<const ymcc::steamgamefacts::TrackerSnapshot> nativeSteamGameTrackerObserve(
    ymcc::steamgamefacts::TrackerFlightToken token,
    std::shared_ptr<ymcc::steamgamefacts::ProcessLease> lease) {
    // Existing worker ONLY: ExecutionEligible(token) immediately BEFORE any
    // ProcessOwner.acquire/OpenProcess for token.factKey(). False: return the
    // token via worker release, with no acquire/read. Do not use the originating
    // subscriber's current callback to discard a shared token. observe retains
    // its separate begin/admitRead/final held-lease and common-current gates.
    return ymcc::steamgamefacts::trackerOwner().observe(std::move(token),std::move(lease));
}
static void nativeSteamGameTrackerClose(
    ymcc::steamgamefacts::TrackerDemand request,
    ymcc::steamgamefacts::TrackerRequestCloseReason reason) noexcept {
    ymcc::steamgamefacts::trackerOwner().close(std::move(request),reason);
}
static void nativeSteamGameTrackerReleaseExecution(
    ymcc::steamgamefacts::TrackerFlightToken token,
    ymcc::steamgamefacts::TrackerExecutionCloseReason reason) noexcept {
    ymcc::steamgamefacts::trackerOwner().releaseExecution(std::move(token),reason);
}

static std::shared_ptr<const ymcc::steamgamefacts::SteamLogSourceView> nativeSteamLogSource(
    const ymcc::steamgamefacts::SteamLogIdentity& expected,ymcc::steamgamefacts::SteamLogKind kind,
    ymcc::steamgamefacts::SteamLogReadPolicy policy,const ymcc::steamgamefacts::SteamLogReadAdmission& admit){
    using namespace ymcc::steamgamefacts;
    return trackerOwner().readLogSource(MainTrackerFactsAdapter::sourceKey(expected),kind,policy,admit);
}

// Fresh GameLink log facts keep their original per-call admission boundary.
// This executes on its existing worker; there is no alternate reader or queue.
static ymcc::steamgamefacts::TrackerFreshObservation nativeSteamGameTrackerReadFresh(
    const SleepSteamRecovery::Session& client,const SleepSteamRecovery::Game& game,
    ymcc::steamgamefacts::TrackerLogScope requiredScope,json* diagnostic=nullptr){
    using namespace ymcc::steamgamefacts;
    // Diagnostic only: no discovery/current callback and no exception may
    // affect the original read or its denial. Fixed keys/strings stay bounded.
    const auto note=[&](const char* stage,const char* reason,int claimStatus=-1)noexcept{
        if(!diagnostic)return;
        try{*diagnostic={{"stage",stage},{"reason",reason},{"claimStatus",claimStatus}};}catch(...){}
    };
    note("admission","not-completed");
    if(requiredScope!=TrackerLogScope::ProcessLifetime&&requiredScope!=TrackerLogScope::RunningListAndProcess){note("admission","unsupported-log-scope");return {};}
    const auto rules=g_gameRulesEpoch.load(std::memory_order_acquire),power=currentPowerGeneration();
    const auto config=g_sleepRecoveryAutoConfigEpoch.load(std::memory_order_acquire);
    const auto current=[&](){return !g_exitRequested.load(std::memory_order_acquire)&&
        g_sleepRecoveryAutoEnabled.load(std::memory_order_acquire)&&g_sleepRecoveryAutoConfigEpoch.load(std::memory_order_acquire)==config&&
        g_gameRulesEpoch.load(std::memory_order_acquire)==rules&&currentPowerGeneration()==power&&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready;};
    if(!current()){note("admission","owner-not-current");return {};}
    NativeDetectedGame selected;selected.pid=game.pid;selected.processCreated=game.created;selected.path=game.path;
    const auto scope=nativeGameFactScope(selected,rules,power);if(!scope){note("game-scope","game-fact-scope-unavailable");return {};}
    const auto key=nativeSteamGameTrackerFactKey(*scope);if(!key){note("fact-key","tracker-fact-key-unavailable");return {};}
    const auto& steam=key->facts().steam;
    if(steam.pid!=client.pid||steam.birth!=client.created||ymcc::sleepsteam::pathKey(W2U(steam.root))!=ymcc::sleepsteam::pathKey(W2U(client.root))){note("steam-scope","steam-instance-mismatch");return {};}
    auto& owner=trackerOwner();auto demand=owner.beginDemand(key,{},current);
    auto claim=owner.claim(std::move(demand.demand),current,TrackerReadMode::RequireNewSource);
    if(claim.status!=TrackerClaimStatus::Claimed||!owner.executionEligible(claim.token)||!current()){note("claim",claim.status==TrackerClaimStatus::FreshSourceBusy?"fresh-source-busy":"claim-not-executable-or-owner-retired",static_cast<int>(claim.status));return {};}
    auto lease=processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),
        {game.pid,game.created,game.path});
    if(!lease||!current()){note("process-lease",lease?"owner-retired-after-pin":"process-lease-unavailable");return {};}
    auto result=owner.observeFresh(std::move(claim.token),std::move(lease),current);
    if(!current())result.complete=false;
    note("observe-fresh",result.complete?"fresh-read-complete":"fresh-read-incomplete");
    return result;
}
static json projectionChecks=json::array();
static void projectionCheck(const char* name,bool pass){projectionChecks.push_back({{"name",name},{"pass",pass}});}
#include "steam_game_group_projection_cases.inc"
template<class Check> static void trackerSourceProjectionCases(Check check){
    using namespace ymcc::steamgamefacts;
    const std::string at="2026-11-13 10:00:00",earliest="2026-11-13 09:59:58",latest="2026-11-13 10:00:03",path="D:\\Game\\selected.exe";
    const std::uint32_t app=2399932135U;const auto full=(std::uint64_t(app)<<32)|0x02000000ULL;
    const auto add=[&](std::uint64_t id,unsigned pid,const std::string& exe){return "["+at+"] AppID "+std::to_string(id)+" adding PID "+std::to_string(pid)+" as a tracked process \""+exe+"\"";};
    std::vector<std::string> lines={add(full,123,path),add(full,456,"D:\\Game\\helper.exe")};
    auto source=tracker_detail::projectSource(19,lines,123,path,earliest,latest);
    check("same-source-revision-on-all-scope-projections",source&&source->sourceRevision==19&&source->processLifetime.binding.sourceRevision==19&&source->runningList.binding.sourceRevision==19&&source->processLifetime.inventory.sourceRevision==19&&source->runningList.inventory.sourceRevision==19);
    check("scope-labels-remain-explicit",source->processLifetime.binding.scope==TrackerLogScope::ProcessLifetime&&source->processLifetime.inventory.scope==TrackerLogScope::ProcessLifetime&&source->runningList.binding.scope==TrackerLogScope::RunningListAndProcess&&source->runningList.inventory.scope==TrackerLogScope::RunningListAndProcess);
    check("complete-membership-keeps-other-pid",source->runningList.inventory.complete&&source->runningList.inventory.members.size()==2&&source->processLifetime.inventory.members.size()==2&&source->runningList.inventory.members[1].pid==456);
    check("full-decimal-from-exact-selected-row",source->processLifetime.binding.status=="tracked"&&source->processLifetime.binding.fullGameIdKnown&&source->processLifetime.binding.fullGameId==full&&source->processLifetime.binding.gameIdRaw==std::to_string(full)&&source->processLifetime.binding.sourceLine==1&&source->processLifetime.binding.exactPath);
    lines.push_back("[2026-11-13 10:01:00] Remove "+std::to_string(full)+" from running list");
    auto removed=tracker_detail::projectSource(20,lines,123,path,earliest,latest);
    check("running-remove-is-not-process-death",removed->runningList.binding.status=="removed"&&removed->runningList.inventory.members.empty()&&removed->processLifetime.binding.status=="tracked"&&removed->processLifetime.inventory.members.size()==2);
    check("old-immutable-projection-not-rewritten",source->runningList.binding.status=="tracked"&&source->runningList.inventory.members.size()==2&&source->sourceRevision==19);
    lines.pop_back();lines.push_back("[2026-11-13 10:01:00] AppID "+std::to_string(full)+" no longer tracking PID 123, exit code 0");
    auto dropped=tracker_detail::projectSource(21,lines,123,path,earliest,latest);
    check("explicit-process-drop-invalidates-both-bindings",dropped->processLifetime.binding.status=="removed"&&dropped->runningList.binding.status=="removed");
    check("drop-preserves-complete-other-member-inventory",dropped->runningList.inventory.complete&&dropped->runningList.inventory.members.size()==1&&dropped->runningList.inventory.members[0].pid==456);
    lines.push_back("[2026-11-13 10:02:00] Client version: fixture-reset");
    auto reset=tracker_detail::projectSource(22,lines,123,path,earliest,latest);
    check("client-reset-invalidates-both-scopes",reset->runningList.binding.status=="unknown"&&reset->processLifetime.binding.status=="unknown"&&reset->runningList.inventory.members.empty()&&reset->processLifetime.inventory.members.empty());
    lines={add(full,123,path),add(730,123,path)};
    auto ambiguous=tracker_detail::projectSource(23,lines,123,path,earliest,latest);
    check("two-appids-for-selected-pid-remain-ambiguous",ambiguous->runningList.binding.status=="ambiguous"&&ambiguous->processLifetime.binding.status=="ambiguous"&&ambiguous->runningList.inventory.members.size()==2);
    lines={add(730,123,path)};auto native=tracker_detail::projectSource(24,lines,123,path,earliest,latest);
    check("native-appid-full-value-kept",native->processLifetime.binding.fullGameIdKnown&&native->processLifetime.binding.fullGameId==730);
    lines={add(app,123,path)};auto shortId=tracker_detail::projectSource(25,lines,123,path,earliest,latest);
    check("shortcut-short-id-not-upgraded-to-full-id",shortId->runningList.binding.status=="tracked"&&!shortId->runningList.binding.fullGameIdKnown&&shortId->runningList.binding.gameIdRaw==std::to_string(app));
    lines={"["+at+"] AppID -1895035161 adding PID 123 as a tracked process \""+path+"\""};
    auto signedId=tracker_detail::projectSource(26,lines,123,path,earliest,latest);
    check("signed-appid-parser-compatibility-without-full-id-guess",signedId->runningList.binding.status=="tracked"&&signedId->runningList.binding.appid==app&&!signedId->runningList.binding.fullGameIdKnown&&signedId->runningList.binding.gameIdRaw=="-1895035161");
    lines={add(full,123,path)};
    auto wrongPath=tracker_detail::projectSource(27,lines,123,"D:\\Game\\different.exe",earliest,latest);
    check("inventory-membership-cannot-replace-exact-exe-binding",wrongPath->runningList.binding.status=="unknown"&&wrongPath->runningList.inventory.members.size()==1);
    auto wrongTime=tracker_detail::projectSource(28,lines,123,path,"2026-11-13 10:00:01",latest);
    check("inventory-membership-cannot-replace-birth-window",wrongTime->processLifetime.binding.status=="unknown"&&wrongTime->processLifetime.inventory.members.size()==1);
    lines={std::string(ymcc::sleepsteam::trackerMaximumBytes+1,'x')};auto oversized=tracker_detail::projectSource(29,lines,123,path,earliest,latest);
    check("oversized-original-source-refused-by-both-original-parsers",!oversized->runningList.inventory.complete&&!oversized->processLifetime.inventory.complete&&oversized->runningList.binding.status=="incomplete"&&oversized->processLifetime.binding.status=="incomplete");
    check("zero-source-revision-cannot-produce-projections",!tracker_detail::projectSource(0,{},123,path,earliest,latest));
    lines.clear();for(unsigned i=0;i<33;++i)lines.push_back(add(730,100+i,"D:\\Game\\member.exe"));
    auto tooMany=tracker_detail::projectSource(30,lines,123,"D:\\Game\\member.exe",earliest,latest);
    check("original-32-live-member-limit-kept",!tooMany->runningList.inventory.complete&&!tooMany->processLifetime.inventory.complete&&tooMany->runningList.inventory.reason=="steam-tracker-live-process-limit");
    lines.assign(20001,"unrelated fixture row");lines.push_back(add(full,123,path));lines.push_back("[2026-11-13 10:01:00] Remove "+std::to_string(full)+" from running list");
    auto dense=tracker_detail::projectSource(31,lines,123,path,earliest,latest);
    check("dense-complete-source-late-remove-only-affects-running-view",dense->processLifetime.binding.status=="tracked"&&dense->processLifetime.binding.sourceLine==20002&&dense->runningList.binding.status=="removed"&&dense->runningList.inventory.complete&&dense->runningList.inventory.members.empty());
    lines.push_back("[2026-11-13 10:02:00] Client version: late-reset");auto denseReset=tracker_detail::projectSource(32,lines,123,path,earliest,latest);
    check("dense-source-final-client-reset-not-missed",denseReset->processLifetime.binding.status=="unknown"&&denseReset->runningList.binding.status=="unknown"&&denseReset->processLifetime.inventory.members.empty());
    trackerGameGroupProjectionCases(check);
}

static void trackerSourceRealCases(const std::wstring& root,const std::wstring& childPath,Registry& registry){
    using namespace ymcc::steamgamefacts;
    trackerSourceProjectionCases(projectionCheck);
    const auto gamePath=root+L"\\projection-game.exe",otherPath=root+L"\\projection-helper.exe";
    if(!CopyFileW(childPath.c_str(),gamePath.c_str(),TRUE)||!CopyFileW(childPath.c_str(),otherPath.c_str(),TRUE))throw std::runtime_error("projection child copies");
    Child steam(childPath);Sleep(15);Child gameChild(gamePath);Sleep(15);Child helper(otherPath);
    registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
    if(!g_steamSettingsObserver.start([](bool){},[]{return false;}))throw std::runtime_error("projection Observer start");
    struct StopObserver {~StopObserver(){g_steamSettingsObserver.stop();}} stopObserver;
    if(!until([&]{const auto r=g_steamSettingsObserver.identitySnapshot();return r&&r->presence==1&&r->session.pid==steam.pi.dwProcessId&&r->session.account==42&&r->windowsSessionKnown;}))throw std::runtime_error("projection actual Observer publication");
    const auto birth=[](const Child& child){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(child.pi.hProcess,&b,&e,&k,&u))throw std::runtime_error("projection birth");return (ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime;};
    NativeDetectedGame game;game.pid=gameChild.pi.dwProcessId;game.processCreated=birth(gameChild);game.path=gamePath;
    const auto scope=nativeGameFactScope(game,g_gameRulesEpoch.load(),currentPowerGeneration());if(!scope)throw std::runtime_error("projection original game scope");
    const auto key=nativeSteamGameTrackerFactKey(*scope);if(!key)throw std::runtime_error("projection actual shared fact key");
    const std::uint32_t app=2399932135U;const auto full=(std::uint64_t(app)<<32)|0x02000000ULL;
    const auto at=SleepSteamRecovery::localSecond(game.processCreated);
    const auto original="["+at+"] AppID "+std::to_string(full)+" adding PID "+std::to_string(game.pid)+" as a tracked process \""+W2U(game.path)+"\"\n";
    const auto other="["+at+"] AppID "+std::to_string(full)+" adding PID "+std::to_string(helper.pi.dwProcessId)+" as a tracked process \""+W2U(otherPath)+"\"\n";
    const auto file=root+L"\\logs\\gameprocess_log.txt";std::filesystem::create_directories(root+L"\\logs");
    const auto write=[&](const std::string& bytes){std::ofstream stream(std::filesystem::path(file),std::ios::binary|std::ios::trunc);stream<<bytes;if(!stream)throw std::runtime_error("projection fixture file write");};
    const auto fresh=[&](){
        auto issued=nativeSteamGameTrackerBeginDemand(key,{},[]{return true;});
        auto claim=nativeSteamGameTrackerClaim(std::move(issued.demand),[]{return true;},TrackerReadMode::RequireCurrentSource);
        if(!claim.token||!nativeSteamGameTrackerExecutionEligible(claim.token))throw std::runtime_error("projection execution token");
        const auto lease=processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),{game.pid,game.processCreated,game.path});
        if(!lease)throw std::runtime_error("projection real process lease");
        return nativeSteamGameTrackerObserve(std::move(claim.token),lease);
    };
    const auto before=fixtureProjectionSourceReads;write(original+other);auto first=fresh();
    projectionCheck("actual-owner-publishes-both-projections-from-one-read",first&&first->hasHistoricalBinding()&&first->parsedSource&&fixtureProjectionSourceReads==before+1&&first->parsedSource->sourceRevision==first->provenance.observationSequence&&first->parsedSource->runningList.inventory.members.size()==2);
    projectionCheck("actual-owner-group-matches-parsed-lifetime-scope-and-selected-metadata",first&&first->parsedSource&&first->groupObservation&&
        trackerGameGroupMetadataEqual(*first->groupObservation,first->parsedSource->processLifetime.group)&&
        first->groupObservation->status==TrackerGroupStatus::Observed&&first->groupObservation->scope==TrackerLogScope::ProcessLifetime&&
        first->groupObservation->sourceRevision==first->parsedSource->sourceRevision&&
        first->groupObservation->sourceRevision==first->parsedSource->processLifetime.group.sourceRevision&&
        first->parsedSource->runningList.group.scope==TrackerLogScope::RunningListAndProcess&&
        first->parsedSource->runningList.group.sourceRevision==first->groupObservation->sourceRevision&&
        first->groupObservation->selectedPid==game.pid&&first->groupObservation->selectedSourceLine==1&&
        first->groupObservation->fullGameId==full&&first->groupObservation->members.size()==2&&
        std::any_of(first->groupObservation->members.begin(),first->groupObservation->members.end(),[&](const auto& row){
            return row.pid==game.pid&&row.path==W2U(game.path)&&row.at==at&&row.sourceLine==1&&row.gameIdRaw==std::to_string(full);
        })&&std::any_of(first->groupObservation->members.begin(),first->groupObservation->members.end(),[&](const auto& row){
            return row.pid==helper.pi.dwProcessId&&row.path==W2U(otherPath)&&row.at==at&&row.sourceLine==2&&row.gameIdRaw==std::to_string(full);
        }));
    const auto cached=nativeSteamGameTrackerPeek(key,[]{return true;});
    projectionCheck("positive-memory-peek-withholds-both-parsed-source-views",cached.association&&!cached.association->parsedSource&&cached.observation&&!cached.observation->parsedSource&&!cached.currentSource&&!cached.processValidated);
    projectionCheck("cached-group-is-historical-metadata-without-current-source-or-process-proof",first&&first->parsedSource&&first->groupObservation&&cached.association&&cached.observation&&
        cached.association->groupObservation==first->groupObservation&&cached.observation->groupObservation==first->groupObservation&&
        cached.association->groupObservation->scope==TrackerLogScope::ProcessLifetime&&
        cached.association->groupObservation->sourceRevision==first->parsedSource->processLifetime.group.sourceRevision&&
        !cached.association->parsedSource&&!cached.observation->parsedSource&&!cached.association->processValidated&&
        !cached.observation->processValidated&&!cached.currentSource&&!cached.processValidated);
    const auto reads=fixtureProjectionSourceReads,clocks=fixtureProjectionClockReads;
    bool groupPeeksRemainHistorical=first&&first->groupObservation;
    for(unsigned i=0;i<1001;++i){
        const auto peek=nativeSteamGameTrackerPeek(key,[]{return true;});
        groupPeeksRemainHistorical=groupPeeksRemainHistorical&&peek.association&&peek.observation&&
            peek.association->groupObservation==first->groupObservation&&peek.observation->groupObservation==first->groupObservation&&
            !peek.association->parsedSource&&!peek.observation->parsedSource&&!peek.association->processValidated&&
            !peek.observation->processValidated&&!peek.currentSource&&!peek.processValidated;
    }
    projectionCheck("1001-memory-peeks-never-read-or-clock-new-source",fixtureProjectionSourceReads==reads&&fixtureProjectionClockReads==clocks);
    projectionCheck("1001-cached-group-peeks-retain-metadata-with-zero-additional-source-or-clock",groupPeeksRemainHistorical&&
        fixtureProjectionSourceReads==reads&&fixtureProjectionClockReads==clocks);
    write(original+other+"["+at+"] Remove "+std::to_string(full)+" from running list\n");auto removed=fresh();
    projectionCheck("actual-owner-keeps-historical-binding-separate-from-running-remove",removed&&removed->hasHistoricalBinding()&&removed->parsedSource&&removed->parsedSource->processLifetime.binding.status=="tracked"&&removed->parsedSource->runningList.binding.status=="removed"&&removed->parsedSource->runningList.inventory.members.empty()&&fixtureProjectionSourceReads==before+2);
    projectionCheck("next-real-read-gets-new-owner-source-revision",removed&&first&&removed->parsedSource&&first->parsedSource&&removed->parsedSource->sourceRevision!=first->parsedSource->sourceRevision);
    write(original+other+"["+at+"] AppID "+std::to_string(full)+" no longer tracking PID "+std::to_string(game.pid)+", exit code 0\n");auto dropped=fresh();
    projectionCheck("actual-negative-publication-preserves-full-other-member-observation",dropped&&dropped->status==TrackerStatus::ParserRemoved&&dropped->parsedSource&&dropped->parsedSource->processLifetime.inventory.members.size()==1&&dropped->parsedSource->processLifetime.inventory.members[0].pid==helper.pi.dwProcessId);
    const auto negative=nativeSteamGameTrackerPeek(key,[]{return true;});
    projectionCheck("negative-memory-peek-withholds-old-running-projection",!negative.association&&negative.observation&&negative.observation->status==TrackerStatus::ParserRemoved&&!negative.observation->parsedSource);
    write(std::string(ymcc::sleepsteam::trackerMaximumBytes+1,'x'));auto truncated=fresh();
    projectionCheck("truncated-source-never-publishes-derived-projections",truncated&&truncated->status==TrackerStatus::SourceTruncated&&!truncated->parsedSource&&!nativeSteamGameTrackerPeek(key,[]{return true;}).association);
    if(!DeleteFileW(file.c_str()))throw std::runtime_error("projection fixture file delete");auto unavailable=fresh();
    projectionCheck("unavailable-source-never-revives-derived-projections",unavailable&&unavailable->status==TrackerStatus::SourceUnavailable&&!unavailable->parsedSource&&!nativeSteamGameTrackerPeek(key,[]{return true;}).association);
    write(original+other);auto stable=fresh();fixtureProjectionAfterRead=[&](){gameChild.finish();};auto changed=fresh();fixtureProjectionAfterRead={};
    projectionCheck("changed-real-held-context-does-not-publish-new-projection",changed&&changed->status==TrackerStatus::ContextChanged&&!changed->parsedSource&&nativeSteamGameTrackerPeek(key,[]{return true;}).association);
    g_steamSettingsObserver.stop();helper.finish();steam.finish();
    projectionCheck("three-owned-children-signalled-and-source-owner-stopped",WaitForSingleObject(gameChild.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(helper.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0&&!g_steamSettingsObserver.identitySnapshot());
}


namespace SteamGameLink {
using namespace ymcc::sleepsteam;
using Game=SleepSteamRecovery::Game;using Client=SleepSteamRecovery::Session;
struct TrackerRead {std::string gameId;Binding binding;json primary=json::object(),log=json::object();bool complete=false;};
static TrackerRead tracking(const Client& client,const Game& game,TrackingScope scope=TrackingScope::ProcessLifetime) {
    TrackerRead result;
    const auto logScope=scope==TrackingScope::RunningListAndProcess
        ?ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess
        :ymcc::steamgamefacts::TrackerLogScope::ProcessLifetime;
    json freshDiagnostic;
    const auto fresh=nativeSteamGameTrackerReadFresh(client,game,logScope,&freshDiagnostic);

    // Keep the original gameprocess tail metadata, including rejected reads.
    if(fresh.tracker)
        result.log=ymcc::steamgamefacts::trackerSourceMetadata(fresh.tracker->observation);
    try{if(freshDiagnostic.is_object())result.log["sharedFresh"]=std::move(freshDiagnostic);}catch(...){}
    if(!fresh.tracker||!fresh.tracker->parsedSource)return result;

    const auto& parsed=*fresh.tracker->parsedSource;
    const auto& selected=logScope==ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess
        ?parsed.runningList:parsed.processLifetime;
    if(!selected.inventory.complete)return result;

    // Preserve every member: unique-primary depends on the full AppID/PID set.
    // nativePrimaryJoin does not consume the inventory's path or timestamp.
    ymcc::sleepsteam::TrackedProcessInventory inventory;
    inventory.complete=selected.inventory.complete;
    inventory.reason=selected.inventory.reason;
    inventory.rows.reserve(selected.inventory.members.size());
    for(const auto& member:selected.inventory.members)
        inventory.rows.push_back({member.appid,member.pid,{},{}});

    result.binding.status=selected.binding.status;
    result.binding.at=selected.binding.at;
    result.binding.appid=selected.binding.appid;
    result.binding.line=selected.binding.sourceLine;
    result.binding.exactPath=selected.binding.exactPath;
    if(result.binding.status=="tracked")result.gameId=selected.binding.gameIdRaw;

    // Parsed binding is an observation even when the shared transaction did
    // not finish. All consumers retain complete before any association/action.
    if(!fresh.complete||!fresh.nativePrimary)return result;

    // Reuse the original unique-tracker fallback and native verified verdicts.
    // Freshness and the console boundary belong to the shared facade.
    result.primary=SleepSteamRecovery::nativePrimaryJoin(
        {*fresh.nativePrimary,ymcc::steamgamefacts::trackerSourceMetadata(fresh.consoleObservation)},
        inventory,result.binding.appid,game.pid);
    result.complete=fresh.complete&&inventory.complete&&static_cast<bool>(fresh.nativePrimary);
    return result;
}
}

static void trackerFreshSourceCases(const std::wstring& root,const std::wstring& childPath,Registry& registry){
    using namespace ymcc::steamgamefacts;
    using namespace ymcc::sleepsteam;
    const auto gamePath=root+L"\\fresh-game.exe",helperPath=root+L"\\fresh-helper.exe";
    if(!CopyFileW(childPath.c_str(),gamePath.c_str(),TRUE)||!CopyFileW(childPath.c_str(),helperPath.c_str(),TRUE))throw std::runtime_error("fresh child copies");
    Child steam(childPath);Sleep(15);Child gameChild(gamePath);Sleep(15);Child helper(helperPath);
    registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
    if(!g_steamSettingsObserver.start([](bool){},[]{return false;}))throw std::runtime_error("fresh Observer start");
    struct StopObserver {~StopObserver(){g_steamSettingsObserver.stop();}} stopObserver;
    if(!until([&]{const auto r=g_steamSettingsObserver.identitySnapshot();return r&&r->presence==1&&r->session.pid==steam.pi.dwProcessId&&r->session.account==42&&r->windowsSessionKnown;}))throw std::runtime_error("fresh actual publication");
    const auto birth=[](const Child& child){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(child.pi.hProcess,&b,&e,&k,&u))throw std::runtime_error("fresh birth");return (ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime;};
    SleepSteamRecovery::Game game{gameChild.pi.dwProcessId,birth(gameChild),gamePath};
    SleepSteamRecovery::Session client;client.pid=steam.pi.dwProcessId;client.created=birth(steam);client.root=root;client.account=42;
    NativeDetectedGame selected;selected.pid=game.pid;selected.processCreated=game.created;selected.path=game.path;
    const auto scope=nativeGameFactScope(selected,g_gameRulesEpoch.load(),currentPowerGeneration());if(!scope)throw std::runtime_error("fresh game scope");
    const auto key=nativeSteamGameTrackerFactKey(*scope);if(!key)throw std::runtime_error("fresh public key");
    const std::uint32_t app=2399932135U;const auto full=(std::uint64_t(app)<<32)|0x02000000ULL;
    const auto at=SleepSteamRecovery::localSecond(game.created);
    const auto original="["+at+"] AppID "+std::to_string(full)+" adding PID "+std::to_string(game.pid)+" as a tracked process \""+W2U(game.path)+"\"\n";
    const auto other="["+at+"] AppID "+std::to_string(full)+" adding PID "+std::to_string(helper.pi.dwProcessId)+" as a tracked process \""+W2U(helperPath)+"\"\n";
    const auto console="["+at+"] Game process added : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pid)+", IP 0.0.0.0:0\n";
    const auto gameFile=root+L"\\logs\\gameprocess_log.txt",consoleFile=root+L"\\logs\\console_log.txt";std::filesystem::create_directories(root+L"\\logs");
    const auto write=[&](const std::wstring& file,const std::string& bytes){std::ofstream out(std::filesystem::path(file),std::ios::binary|std::ios::trunc);out<<bytes;if(!out)throw std::runtime_error("fresh fixture write");};
    const auto seed=[&](){write(gameFile,original+other);write(consoleFile,console);};seed();
    const auto current=[](){return true;};auto& owner=trackerOwner();
    const auto issue=[&](){auto ticket=owner.beginDemand(key,{},current);return owner.claim(std::move(ticket.demand),current,TrackerReadMode::RequireNewSource);};
    {
        auto old=issue();auto tooEarly=issue();
        freshCheck("new-source-request-does-not-join-older-unread-flight",old.token&&tooEarly.status==TrackerClaimStatus::FreshSourceBusy&&!tooEarly.token);
    }
    const auto facade=[&](TrackingScope requested=TrackingScope::RunningListAndProcess){return SteamGameLink::tracking(client,game,requested);};
    const auto before=fixtureFreshGameReads,consoleBefore=fixtureFreshConsoleReads;
    auto first=facade();
    freshCheck("actual-gamelink-consumes-one-common-game-and-console-read",first.complete&&first.binding.status=="tracked"&&first.gameId==std::to_string(full)&&first.primary.value("originalAllowed",false)&&first.primary.value("nativePrimaryVerified",false)&&first.primary.value("trackedMemberCount",0)==2&&fixtureFreshGameReads==before+1&&fixtureFreshConsoleReads==consoleBefore+1);
    freshCheck("source-metadata-preserves-original-complete-tail-fields",first.log.value("available",false)&&!first.log.value("truncated",true)&&first.log.value("originalBytes",0ULL)==original.size()+other.size());
    auto second=facade();
    freshCheck("second-post-pin-equivalent-call-requires-another-physical-pair",second.complete&&fixtureFreshGameReads==before+2&&fixtureFreshConsoleReads==consoleBefore+2);
    auto pure=nativeSteamGameTrackerPeek(key,current);const auto sourceCount=fixtureFreshGameReads,consoleCount=fixtureFreshConsoleReads;
    freshCheck("memory-peek-cannot-supply-fresh-source-or-console",pure.association&&!pure.association->parsedSource&&!pure.currentSource&&fixtureFreshGameReads==sourceCount&&fixtureFreshConsoleReads==consoleCount);
    write(gameFile,original+other+"["+at+"] Remove "+std::to_string(full)+" from running list\n");
    auto removed=facade(),lifetime=facade(TrackingScope::ProcessLifetime);
    freshCheck("gamelink-running-and-lifetime-scopes-stay-distinct",removed.complete&&removed.binding.status=="removed"&&removed.gameId.empty()&&!removed.primary.value("originalAllowed",true)&&lifetime.complete&&lifetime.binding.status=="tracked"&&lifetime.primary.value("originalAllowed",false));
    seed();bool busyDuringConsole=false;fixtureFreshReadHook=[&](const std::wstring& file){if(file.ends_with(L"console_log.txt")){auto inConsole=issue();busyDuringConsole=inConsole.status==TrackerClaimStatus::FreshSourceBusy&&!inConsole.token;}};
    auto held=facade();fixtureFreshReadHook={};
    freshCheck("original-source-token-remains-held-through-console-read",held.complete&&busyDuringConsole);
    if(!DeleteFileW(consoleFile.c_str()))throw std::runtime_error("fresh remove console");auto noPrimaryTwo=facade();
    freshCheck("missing-console-multiple-members-cannot-use-single-member-fallback",noPrimaryTwo.complete&&!noPrimaryTwo.primary.value("originalAllowed",true)&&!noPrimaryTwo.primary.value("nativePrimaryVerified",true));
    write(gameFile,original);auto noPrimarySingle=facade();
    freshCheck("original-single-member-fallback-remains-explicitly-unverified",noPrimarySingle.complete&&noPrimarySingle.primary.value("originalAllowed",false)&&!noPrimarySingle.primary.value("nativePrimaryVerified",true)&&noPrimarySingle.primary.value("trackedMemberCount",0)==1);
    write(consoleFile,std::string(ymcc::sleepsteam::trackerMaximumBytes+32,'x')+"\n"+console);auto largeConsole=facade();
    freshCheck("fresh-gamelink-accepts-complete-console-above-game-cap",largeConsole.complete&&largeConsole.primary.value("originalAllowed",false)&&largeConsole.primary.value("nativePrimaryVerified",false));
    write(consoleFile,std::string(ymcc::sleepsteam::nativePrimaryMaximumBytes+1,'x'));auto truncated=facade();
    freshCheck("truncated-console-cannot-downgrade-to-single-member-fallback",truncated.complete&&!truncated.primary.value("originalAllowed",true)&&!truncated.primary.value("nativePrimaryVerified",true)&&truncated.primary.value("status","")=="unverifiable");
    write(consoleFile,"["+at+"] Game process updated : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(helper.pi.dwProcessId)+", IP 0.0.0.0:0\n");auto different=facade();
    freshCheck("different-native-primary-stays-refused",different.complete&&different.primary.value("nativePrimaryVerified",false)&&!different.primary.value("originalAllowed",true)&&different.primary.value("status","")=="different-instance");
    write(consoleFile,"["+at+"] Game process added : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pid)+", IP 10.0.0.1:123\n");auto nonLocal=facade();
    freshCheck("nonlocal-primary-stays-refused",nonLocal.complete&&!nonLocal.primary.value("originalAllowed",true)&&nonLocal.primary.value("status","")=="non-local-record");
    write(consoleFile,"["+at+"] Game process removed: AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pid)+"\n");auto removedPrimary=facade();
    freshCheck("removed-primary-stays-refused",removedPrimary.complete&&!removedPrimary.primary.value("originalAllowed",true)&&removedPrimary.primary.value("status","")=="removed");
    write(consoleFile,"["+at+"] Game process added : AppID malformed ProcID 1\n");auto malformed=facade();
    freshCheck("malformed-primary-keeps-original-parser-refusal",malformed.complete&&!malformed.primary.value("originalAllowed",true)&&malformed.primary.value("status","")=="unverifiable");
    seed();const auto offReads=fixtureFreshGameReads;g_sleepRecoveryAutoEnabled=false;auto off=facade();g_sleepRecoveryAutoEnabled=true;
    freshCheck("off-before-fresh-call-has-zero-source-read",!off.complete&&fixtureFreshGameReads==offReads);
    auto wrong=client;wrong.created++;auto mismatch=SteamGameLink::tracking(wrong,game);
    freshCheck("wrong-steam-process-cannot-collect-another-source",!mismatch.complete&&fixtureFreshGameReads==offReads);
    bool invalidateConfig=true;fixtureFreshReadHook=[&](const std::wstring& file){if(invalidateConfig&&file.ends_with(L"console_log.txt")){invalidateConfig=false;++g_sleepRecoveryAutoConfigEpoch;}};
    auto configChanged=facade();fixtureFreshReadHook={};
    freshCheck("config-change-during-console-read-revokes-consumer-result",!configChanged.complete);
    seed();fixtureFreshReadHook=[&](const std::wstring& file){if(file.ends_with(L"console_log.txt"))gameChild.finish();};auto exited=facade();fixtureFreshReadHook={};
    freshCheck("game-exit-during-console-read-revokes-held-result",!exited.complete);
    g_steamSettingsObserver.stop();helper.finish();steam.finish();
    freshCheck("all-three-fixture-processes-signalled-before-resource-return",WaitForSingleObject(gameChild.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(helper.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0&&!g_steamSettingsObserver.identitySnapshot());
}

static void steamOnlyLogSourceCases(const std::wstring& root,const std::wstring& childPath,Registry& registry){
    using namespace ymcc::steamgamefacts;using namespace ymcc::sleepsteam;
    const auto gamePath=root+L"\\source-game.exe";if(!CopyFileW(childPath.c_str(),gamePath.c_str(),TRUE))throw std::runtime_error("source fixture child");
    Child steam(childPath);Sleep(15);Child game(gamePath);registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
    if(!g_steamSettingsObserver.start([](bool){},[]{return false;}))throw std::runtime_error("source Observer start");
    struct Stop {~Stop(){g_steamSettingsObserver.stop();}} stop;
    if(!until([&](){const auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->presence==1&&s->session.pid==steam.pi.dwProcessId&&s->session.account==42&&s->windowsSessionKnown;}))throw std::runtime_error("source real publication");
    const auto birth=[](const Child& child){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(child.pi.hProcess,&b,&e,&k,&u))throw std::runtime_error("source real birth");return (ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime;};
    const SteamLogIdentity expected{steam.pi.dwProcessId,birth(steam),root};const auto gameBirth=birth(game);
    const auto key=MainTrackerFactsAdapter::sourceKey(expected);sourceCheck("closed-source-key-from-real-common-steam-instance",bool(key));
    const auto file=root+L"\\logs\\gameprocess_log.txt",console=root+L"\\logs\\console_log.txt";std::filesystem::create_directories(root+L"\\logs");
    const auto write=[&](const std::wstring& path,const std::string& bytes){std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);out<<bytes;if(!out)throw std::runtime_error("source fixture file");};
    const auto at=SleepSteamRecovery::localSecond(gameBirth);const std::uint64_t full=(std::uint64_t(2399932135U)<<32)|0x02000000ULL;
    const auto add="["+at+"] AppID "+std::to_string(full)+" adding PID "+std::to_string(game.pi.dwProcessId)+" as a tracked process \""+W2U(gamePath)+"\"\n";
    write(file,add);write(console,"["+at+"] Game process added : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pi.dwProcessId)+", IP 0.0.0.0:0\n");
    const auto load=[&](SteamLogKind kind,SteamLogReadPolicy policy,const SteamLogReadAdmission& admit=[](){return true;}){return nativeSteamLogSource(expected,kind,policy,admit);};
    auto complete=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly);
    sourceCheck("complete-source-owns-full-original-inventory-and-binding",complete&&complete->complete&&!complete->diagnosticOnly&&complete->runningInventory.complete&&complete->runningInventory.rows.size()==1&&complete->binding(game.pi.dwProcessId,W2U(gamePath),at,at).status=="tracked");
    NativeDetectedGame selected;selected.pid=game.pi.dwProcessId;selected.processCreated=gameBirth;selected.path=gamePath;
    g_powerLifecycle=PowerLifecycle::Suspending;auto suspending=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly);
    sourceCheck("suspending-source-does-not-borrow-ready-only-game-scope",suspending->complete&&!nativeGameFactScope(selected,g_gameRulesEpoch.load(),currentPowerGeneration()));
    g_powerLifecycle=PowerLifecycle::Resuming;auto resuming=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly);
    sourceCheck("resuming-source-remains-read-only-and-separate-from-game-scope",resuming->complete&&!nativeGameFactScope(selected,g_gameRulesEpoch.load(),currentPowerGeneration()));g_powerLifecycle=PowerLifecycle::Ready;
    registry.number(L"ActiveUser",43);if(!until([&](){const auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->session.account==43;}))throw std::runtime_error("source candidate account event");
    sourceCheck("process-only-source-not-revoked-by-registry-account-candidate-noise",MainTrackerFactsAdapter::sourceCurrent(*key)&&load(SteamLogKind::GameProcess,SteamLogReadPolicy::Raw)->complete);
    registry.number(L"ActiveUser",42);if(!until([&](){const auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->session.account==42;}))throw std::runtime_error("source candidate return");
    const auto boundary=sgNowFileTime();write(console,"["+at+"] Game process added : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pi.dwProcessId)+", IP 0.0.0.0:0\n["+SleepSteamRecovery::localSecond(boundary+600000000ULL)+"] Game process updated : AppID "+std::to_string(full)+" late, ProcID 12345, IP 0.0.0.0:0\n");
    auto primary=load(SteamLogKind::Console,SteamLogReadPolicy::Raw);const auto beforeBoundary=primary->primary(SleepSteamRecovery::localSecond(expected.birth),SleepSteamRecovery::localSecond(boundary));
    sourceCheck("absolute-console-boundary-is-caller-owned-and-excludes-late-row",primary->complete&&beforeBoundary.complete&&beforeBoundary.rows.at(2399932135U).pid==game.pi.dwProcessId);
    sourceCheck("game-and-console-have-distinct-physical-source-revisions",complete->observation.sourceRevision&&primary->observation.sourceRevision>complete->observation.sourceRevision);
    const auto primaryRow="["+at+"] Game process added : AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pi.dwProcessId)+", IP 0.0.0.0:0\n";
    const auto parseConsole=[&](const std::string& bytes){write(console,bytes);return load(SteamLogKind::Console,SteamLogReadPolicy::Raw);};
    const auto parsePrimary=[&](const auto& value){return value->primary(SleepSteamRecovery::localSecond(expected.birth),SleepSteamRecovery::localSecond(sgNowFileTime()));};
    const std::string prefix(trackerMaximumBytes+32,'x');
    auto large=parseConsole(prefix+"\n"+primaryRow);auto largePrimary=parsePrimary(large);
    sourceCheck("complete-console-above-game-budget-keeps-full-prefix-and-primary",large->complete&&!large->observation.truncated&&large->observation.limitBytes==nativePrimaryMaximumBytes&&large->observation.originalBytes==prefix.size()+1+primaryRow.size()&&largePrimary.complete&&largePrimary.rows.at(2399932135U).pid==game.pi.dwProcessId);
    auto removed=parseConsole(primaryRow+prefix+"\n["+at+"] Game process removed: AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(game.pi.dwProcessId)+"\n");auto removedPrimary=parsePrimary(removed);
    sourceCheck("large-console-final-remove-overrides-earlier-primary",removed->complete&&removedPrimary.complete&&removedPrimary.rows.at(2399932135U).removed);
    auto different=parseConsole(primaryRow+prefix+"\n["+at+"] Game process updated : AppID "+std::to_string(full)+" fixture, ProcID 12345, IP 0.0.0.0:0\n");auto differentPrimary=parsePrimary(different);
    sourceCheck("large-console-final-different-primary-remains-visible",different->complete&&differentPrimary.complete&&differentPrimary.rows.at(2399932135U).pid==12345);
    auto malformed=parseConsole(primaryRow+prefix+"\n["+at+"] Game process updated : AppID broken\n");
    sourceCheck("large-console-malformed-late-primary-refuses-inventory",malformed->complete&&!parsePrimary(malformed).complete);
    auto exact=parseConsole(std::string(nativePrimaryMaximumBytes-primaryRow.size()-1,'x')+"\n"+primaryRow);
    sourceCheck("console-exact-fixed-cap-remains-complete",exact->complete&&exact->observation.returnedBytes==nativePrimaryMaximumBytes&&parsePrimary(exact).complete);
    auto beyond=parseConsole(std::string(nativePrimaryMaximumBytes+1,'x'));
    sourceCheck("console-over-fixed-cap-stays-truncated-unknown",!beyond->complete&&beyond->observation.truncated&&beyond->observation.returnedBytes==nativePrimaryMaximumBytes&&!parsePrimary(beyond).complete);

    write(file,std::string(trackerMaximumBytes+32,'x')+"\n"+add);auto strict=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly);auto diagnostic=load(SteamLogKind::GameProcess,SteamLogReadPolicy::TruncatedDiagnostic);
    sourceCheck("truncated-strict-source-never-supplies-complete-inventory",!strict->complete&&!strict->diagnosticOnly&&strict->lines.empty()&&!strict->runningInventory.complete&&strict->binding(game.pi.dwProcessId,W2U(gamePath),at,at).status=="unknown");
    sourceCheck("truncated-diagnostic-drops-only-first-residual-line",!diagnostic->complete&&diagnostic->diagnosticOnly&&diagnostic->observation.truncated&&diagnostic->binding(game.pi.dwProcessId,W2U(gamePath),at,at).status=="tracked"&&diagnostic->lines.size()==1);
    write(file,add);const auto before=fixtureFreshGameReads;auto denied=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,[](){return false;});
    sourceCheck("expired-original-admission-performs-no-source-read",!denied->complete&&!denied->observation.readAttempted&&fixtureFreshGameReads==before);
    unsigned checks=0;auto beforeRead=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,[&](){return ++checks<2;});
    sourceCheck("original-budget-can-expire-after-pin-without-reset-or-read",!beforeRead->complete&&checks==2&&!beforeRead->observation.readAttempted&&fixtureFreshGameReads==before);
    bool valid=true;fixtureFreshReadHook=[&](const std::wstring& p){if(p.ends_with(L"gameprocess_log.txt"))valid=false;};auto afterRead=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,[&](){return valid;});fixtureFreshReadHook={};
    sourceCheck("late-admission-change-keeps-observed-bytes-but-no-parsed-fact",!afterRead->complete&&afterRead->observation.readAttempted&&afterRead->observation.bytes&&afterRead->lines.empty()&&afterRead->observation.readReason==TrackerSourceObservation::ReadReason::ContextChanged);
    auto wrong=expected;wrong.birth++;const auto currentReads=fixtureFreshGameReads;auto refused=nativeSteamLogSource(wrong,SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,[](){return true;});
    sourceCheck("wrong-steam-birth-never-falls-back-to-another-root",!refused->complete&&!refused->observation.readAttempted&&fixtureFreshGameReads==currentReads);
    const auto currentClocks=fixtureProjectionClockReads;for(unsigned i=0;i<1001;++i)if(!MainTrackerFactsAdapter::sourceCurrent(*key))throw std::runtime_error("source memory current unexpectedly changed");
    sourceCheck("1001-source-current-observations-have-zero-file-and-clock-reads",fixtureFreshGameReads==currentReads&&fixtureProjectionClockReads==currentClocks);
    fixtureFreshReadHook=[&](const std::wstring& p){if(p.ends_with(L"gameprocess_log.txt"))steam.finish();};auto exited=load(SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly);fixtureFreshReadHook={};
    sourceCheck("actual-held-steam-exit-during-read-refuses-source-publication",!exited->complete&&exited->observation.readAttempted&&exited->lines.empty());
    g_steamSettingsObserver.stop();game.finish();
    sourceCheck("both-fixture-processes-signalled-and-original-observer-stopped",WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(game.pi.hProcess,0)==WAIT_OBJECT_0&&!g_steamSettingsObserver.identitySnapshot());
}

static void delegatedIdentityCases(const std::wstring& root,const std::wstring& childPath,Registry& registry){
        // Actual producer-issued revocations. No new timers or replacement registry owner.
        {
            Child delegatedChild(childPath);
            registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",delegatedChild.pi.dwProcessId);
            Observer delegatedObserver(registry.path);
            expect(delegatedObserver.start([](bool){},[]{return false;}),"delegation uses the existing event observer");
            expect(until([&]{const auto r=delegatedObserver.identitySnapshot();return r&&r->presence==1&&r->session.pid==delegatedChild.pi.dwProcessId&&r->session.account==42;}),"delegation fixture has actual published live process");
            const auto receipt=delegatedObserver.identitySnapshot();
            auto candidateLease=delegatedObserver.delegateIdentity(receipt,42,false);
            auto uiLease=delegatedObserver.delegateIdentity(receipt,123,true);
            expect(candidateLease&&uiLease&&candidateLease->current()&&uiLease->current(),"publisher issues distinct current candidate and UI request leases");
            expect(candidateLease->cancellationEvent()!=uiLease->cancellationEvent(),"independent requests never share cancellation events");
            expect(uiLease->matches(receipt->session.root,receipt->session.pid,receipt->session.created,123,receipt->windowsSession,true)&&
                !uiLease->matches(receipt->session.root,receipt->session.pid,receipt->session.created,42,receipt->windowsSession,true)&&
                !uiLease->matches(receipt->session.root,receipt->session.pid,receipt->session.created,123,receipt->windowsSession,false),"delegation binds resolved account and source policy exactly");
            const auto beforeReads=registryReads.load();
            for(unsigned i=0;i<1001;++i)if(!candidateLease->current()||!uiLease->current())throw std::runtime_error("delegation current unexpectedly changed");
            expect(registryReads.load()==beforeReads,"1001 delegated consumers perform zero registry discovery");
            expect(!delegatedObserver.delegateIdentity({},42,false)&&!delegatedObserver.delegateIdentity(receipt,0,true)&&
                !delegatedObserver.delegateIdentity(receipt,43,false),"missing zero and mismatched candidate identities cannot create a lease");
            auto invalid=std::make_shared<IdentitySnapshot>(*receipt);invalid->processEpoch++;
            expect(!delegatedObserver.delegateIdentity(invalid,42,false),"stale process epoch cannot be delegated");
            invalid=std::make_shared<IdentitySnapshot>(*receipt);invalid->session.created++;
            expect(!delegatedObserver.delegateIdentity(invalid,42,false),"unpublished process birth cannot be delegated");
            invalid=std::make_shared<IdentitySnapshot>(*receipt);invalid->windowsSessionKnown=false;
            expect(!delegatedObserver.delegateIdentity(invalid,42,false),"unknown Windows session cannot be delegated");
            registry.number(L"ActiveUser",43);
            expect(until([&]{const auto r=delegatedObserver.identitySnapshot();return r&&r->session.account==43;}),"same-process account transition is published by the original owner");
            expect(!candidateLease->current()&&WaitForSingleObject(candidateLease->cancellationEvent(),0)==WAIT_OBJECT_0,
                "account change signals the existing inherited cancellation capability");
            expect(uiLease->current(),"candidate-only account noise does not revoke independent current-UI proof");
            registry.number(L"ActiveUser",42);
            expect(until([&]{const auto r=delegatedObserver.identitySnapshot();return r&&r->session.account==42;}),"account candidate can return through the same event owner");
            expect(!candidateLease->current()&&!delegatedObserver.delegateIdentity(receipt,42,false),"account ABA neither revives an old lease nor admits an old candidate receipt");
            const auto renewed=delegatedObserver.identitySnapshot();auto currentLease=delegatedObserver.delegateIdentity(renewed,42,false);
            expect(currentLease&&currentLease->current(),"new current receipt can create a distinct request without reviving old work");
            HANDLE observed=nullptr;{
                auto released=delegatedObserver.delegateIdentity(renewed,42,false);
                expect(released&&DuplicateHandle(GetCurrentProcess(),released->cancellationEvent(),GetCurrentProcess(),&observed,SYNCHRONIZE,FALSE,0),"consumer can retain only an observation duplicate");
            }
            expect(WaitForSingleObject(observed,0)==WAIT_OBJECT_0,"dropping the request lease signals revocation before closing its handle");CloseHandle(observed);
            delegatedObserver.stop();
            expect(!uiLease->current()&&!currentLease->current()&&WaitForSingleObject(uiLease->cancellationEvent(),0)==WAIT_OBJECT_0,
                "producer stop revokes every live request before retiring publication");
            expect(!delegatedObserver.delegateIdentity(renewed,42,false),"stopped owner refuses a retained receipt");
            expect(delegatedObserver.start([](bool){},[]{return false;})&&until([&]{const auto r=delegatedObserver.identitySnapshot();return r&&r->presence==1;}),"same-process owner restart publishes a fresh epoch");
            expect(!delegatedObserver.delegateIdentity(renewed,42,false)&&!uiLease->current(),"owner restart cannot revive old request epochs");
            auto freshLease=delegatedObserver.delegateIdentity(delegatedObserver.identitySnapshot(),42,false);
            expect(freshLease&&freshLease->current(),"fresh restarted owner admits only a fresh request lease");
            delegatedChild.finish();
            expect(until([&]{return !freshLease->current();}),"actual held process exit revokes delegated requests without polling");
            delegatedObserver.stop();
        }
}
namespace SleepSteamRecovery {
static NativePrimaryRead readNativePrimary(const Session& client,ULONGLONG boundary,
    const ymcc::steamgamefacts::SteamLogReadAdmission& admit=[](){return true;}) {
    NativePrimaryRead result;
    const auto source=nativeSteamLogSource({client.pid,client.created,client.root},
        ymcc::steamgamefacts::SteamLogKind::Console,ymcc::steamgamefacts::SteamLogReadPolicy::Raw,admit);
    if(!source||!source->observation.availableKnown||!source->observation.readAttempted||
        source->observation.readReason==ymcc::steamgamefacts::TrackerSourceObservation::ReadReason::IdentityUnavailable||
        source->observation.readReason==ymcc::steamgamefacts::TrackerSourceObservation::ReadReason::ContextChanged||
        source->observation.readReason==ymcc::steamgamefacts::TrackerSourceObservation::ReadReason::InvalidRequest||
        source->observation.readReason==ymcc::steamgamefacts::TrackerSourceObservation::ReadReason::SequenceExhausted||
        source->observation.readReason==ymcc::steamgamefacts::TrackerSourceObservation::ReadReason::ReadException){result.inventory.complete=false;result.inventory.reason="steam-native-primary-source-unknown";
        result.log={{"available",false},{"reason","steam-native-primary-source-unknown"}};return result;}
    result.log=source->meta();
    if(!flag(result.log,"available")){result.inventory.reason="steam-native-primary-log-unavailable";return result;}
    if(flag(result.log,"truncated")){result.inventory.complete=false;result.inventory.reason="steam-native-primary-log-truncated";return result;}
    if(!source->complete||source->diagnosticOnly||!source->observation.bytes){
        result.inventory.complete=false;result.inventory.reason="steam-native-primary-source-unknown";return result;}
    result.inventory=nativePrimaryProcessInventoryText(*source->observation.bytes,localSecond(client.created),localSecond(boundary));return result;
}
}
#ifndef YMCC_STEAM_LOG_SOURCE_ONLY
#include "steam_native_observation_tracking.h"
#include "steam_session_observer_native_read_scope_cases.inc"
#endif
#include "steam_game_group_role_cases.inc"
int wmain(int argc, wchar_t** argv) {
    if(const auto result=SteamGameGroupRoleFixture::childMain(argc,argv))return *result;
    if (argc == 3 && std::wstring(argv[1]) == L"--child") {
        HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, argv[2]);
        if (!h) return 2;
        // Real top-level creation/show event, kept off-screen; never touch the real Steam install.
        const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"YMCC observer fixture",
            WS_POPUP, -32000, -32000, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) { CloseHandle(h); return 3; }
        ShowWindow(window, SW_SHOWNOACTIVATE);
        WaitForSingleObject(h, 30000); DestroyWindow(window); CloseHandle(h); return 0; // Crash backstop; no polling.
    }
    try {
        if(argc<2||!argv[1][0])throw std::runtime_error("empty observer output workspace");
        if(std::wstring_view(argv[1])==L"--native-read-scope")throw std::runtime_error("missing observer output workspace");
#ifdef YMCC_STEAM_LOG_SOURCE_ONLY
        if(argc!=3||(std::wstring_view(argv[2])!=L"--steam-log-source"&&std::wstring_view(argv[2])!=L"--tracker-fresh-gamelink"))
            throw std::runtime_error("invalid source-only observer selector");
#endif
#ifndef YMCC_STEAM_LOG_SOURCE_ONLY
        if(argc>=3&&(std::wstring_view(argv[2])==L"--native-read-scope"||std::wstring_view(argv[2])==L"--native-read-observation")){
            std::string filter;
            if(argc!=3){
                if(argc!=5||std::wstring_view(argv[3])!=L"--case"||!argv[4][0])
                    throw std::runtime_error("invalid native-read-scope case selector");
                filter=W2U(argv[4]);
                if(!NativeReadScopeFixture::knownCase(filter))
                    throw std::runtime_error("unknown native-read-scope case");
            }
            // Selection validation precedes filesystem/Registry/Child/Observer.
            const auto root=std::filesystem::absolute(argv[1]).wstring();std::filesystem::create_directories(root);
            wchar_t image[32768];const auto size=GetModuleFileNameW(nullptr,image,static_cast<DWORD>(std::size(image)));
            if(!size||size>=std::size(image))throw std::runtime_error("native scope fixture image");
            const auto child=root+L"\\steam.exe";
            if(!CopyFileW(image,child.c_str(),TRUE))throw std::runtime_error("native scope exclusive child copy");
            return NativeReadScopeFixture::run(root,child,filter,std::wstring_view(argv[2])==L"--native-read-observation");
        }
#endif
        const bool optInOnly=argc==4&&std::wstring_view(argv[2])==L"--tracker-process-group-roles"&&std::wstring_view(argv[3])==L"--fresh-opt-in-only";
        if(!optInOnly&&argc!=2&&(argc!=3||(std::wstring_view(argv[2])!=L"--delegation-only"&&std::wstring_view(argv[2])!=L"--tracker-source-projections"&&std::wstring_view(argv[2])!=L"--tracker-fresh-gamelink"&&std::wstring_view(argv[2])!=L"--tracker-process-group-roles"&&std::wstring_view(argv[2])!=L"--steam-log-source")))
            throw std::runtime_error("invalid observer selector");
        if(argc==3&&std::wstring_view(argv[2])==L"--steam-log-source"){
            const auto root=std::filesystem::absolute(argv[1]).wstring();std::filesystem::create_directories(root);
            wchar_t image[32768];GetModuleFileNameW(nullptr,image,static_cast<DWORD>(std::size(image)));
            const auto child=root+L"\\steam.exe";
            if(!CopyFileW(image,child.c_str(),TRUE))throw std::runtime_error("source selector child copy");
            Registry registry(contextRegistryPath);steamOnlyLogSourceCases(root,child,registry);
            unsigned failures=0;for(const auto& row:sourceChecks)if(!row.at("pass").get<bool>())++failures;
            std::cout<<"STEAM_LOG_SOURCE_JSON "<<json({{"selection","steam-log-source"},{"checks",sourceChecks},{"failures",failures},{"gameReads",fixtureFreshGameReads},{"consoleReads",fixtureFreshConsoleReads},{"actualHeldProcessLease",true},{"realSteamOperations",0}}).dump()<<std::endl;
            return failures?2:0;
        }
        if((argc==3||optInOnly)&&std::wstring_view(argv[2])==L"--tracker-process-group-roles"){
            const auto root=std::filesystem::absolute(argv[1]).wstring();std::filesystem::create_directories(root);
            wchar_t image[32768];const auto size=GetModuleFileNameW(nullptr,image,static_cast<DWORD>(std::size(image)));
            if(!size||size>=std::size(image))throw std::runtime_error("role selector image");
            const auto child=root+L"\\steam.exe";
            if(!CopyFileW(image,child.c_str(),TRUE))throw std::runtime_error("role selector exclusive child copy");
            Registry registry(contextRegistryPath);
            return SteamGameGroupRoleFixture::run(root,child,registry,optInOnly);
        }
        if(argc==3&&std::wstring_view(argv[2])==L"--tracker-fresh-gamelink"){
            const auto root=std::filesystem::absolute(argv[1]).wstring();std::filesystem::create_directories(root);
            wchar_t image[32768];GetModuleFileNameW(nullptr,image,static_cast<DWORD>(std::size(image)));
            const auto child=root+L"\\steam.exe";
            if(!CopyFileW(image,child.c_str(),TRUE))throw std::runtime_error("fresh selector child copy");
            Registry registry(contextRegistryPath);trackerFreshSourceCases(root,child,registry);
            unsigned failures=0;for(const auto& row:freshChecks)if(!row.at("pass").get<bool>())++failures;
            std::cout<<"TRACKER_FRESH_JSON "<<json({{"selection","tracker-fresh-gamelink"},{"checks",freshChecks},{"failures",failures},{"gameReads",fixtureFreshGameReads},{"consoleReads",fixtureFreshConsoleReads},{"actualHeldProcessLease",true},{"realSteamOperations",0}}).dump()<<std::endl;
            return failures?2:0;
        }
        if(argc==3&&std::wstring_view(argv[2])==L"--tracker-source-projections"){
            const auto root=std::filesystem::absolute(argv[1]).wstring();std::filesystem::create_directories(root);
            wchar_t image[32768];GetModuleFileNameW(nullptr,image,static_cast<DWORD>(std::size(image)));
            const auto child=root+L"\\steam.exe";
            if(!CopyFileW(image,child.c_str(),TRUE))throw std::runtime_error("projection selector child copy");
            Registry registry(contextRegistryPath);trackerSourceRealCases(root,child,registry);
            unsigned failures=0;for(const auto& row:projectionChecks)if(!row.at("pass").get<bool>())++failures;
            std::cout<<"TRACKER_SOURCE_JSON "<<json({{"selection","tracker-source-projections"},{"checks",projectionChecks},{"failures",failures},{"sourcePortReads",fixtureProjectionSourceReads},{"actualHeldProcessLease",true},{"realSteamOperations",0}}).dump()<<std::endl;
            return failures?2:0;
        }
        RetryWindow budget;
        expect(budget.timeout(0) == INFINITE, "idle policy has no recurring deadline");
        budget.begin(1000);
        for (const ULONGLONG offset : {1000ULL,3000ULL,8000ULL,20000ULL}) {
            expect(budget.timeout(1000+offset-1)==1 && !budget.take(1000+offset-1), "startup deadline cannot fire early");
            expect(budget.take(1000+offset), "one bounded startup retry consumed");
        }
        expect(budget.timeout(1000000)==INFINITE && !budget.take(1000000), "four attempts exhaust to infinite event wait");
        budget.begin(5000);expect(budget.take(60000)&&budget.timeout(60000)==INFINITE, "late IO or wake skips overdue retry burst");
        Session a{L"X:\\Steam",1,42,100},b=a;
        b.created=200;expect(!(a==b), "PID reuse with different process birth is a session change");
        expect(argc==2||(argc==3&&std::wstring_view(argv[2])==L"--delegation-only"), "selftest output workspace supplied");
        const auto root = std::filesystem::absolute(argv[1]).wstring();
        const auto childPath = root + L"\\steam.exe";
        wchar_t current[32768]; GetModuleFileNameW(nullptr, current, static_cast<DWORD>(std::size(current)));
        expect(CopyFileW(current, childPath.c_str(), FALSE), "isolated synthetic Steam child copied inside test workspace");
        Registry registry(L"Software\\YeManCC-SteamSessionSelftest\\"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        if(argc==3){delegatedIdentityCases(root,childPath,registry);std::cout<<"STEAM_IDENTITY_DELEGATION_OK tests="<<tests<<" actualSteamOperations=0\n";return 0;}
        Child first(childPath);
        registry.root(root); registry.number(L"ActiveUser",42); registry.number(L"pid",first.pi.dwProcessId);
        ULONGLONG born=0;HANDLE verified=verifiedProcess(first.pi.dwProcessId,root,&born);
        expect(verified&&born, "process identity verifies test Steam executable and birth time");if(verified)CloseHandle(verified);
        verified=verifiedProcess(GetCurrentProcessId(),root,&born);
        expect(!verified, "unrelated PID cannot be adopted");if(verified)CloseHandle(verified);
        Observer observer(registry.path);
        expect(observer.steamPresence()==-1, "Steam presence unknown before observer starts");
        expect(!observer.identitySnapshot(), "coherent identity is Unknown before startup");
        expect(observer.sessionSnapshot().pid==0, "shared addon snapshot cannot invent a session before startup");
        std::atomic<int> changes{0},retryCalls{0};
        expect(observer.start([&](bool changed){if(changed)++changes;else ++retryCalls;},[]{return false;}), "real asynchronous registry observer starts");
        expect(until([&]{return changes.load()==1;}), "initial session delivered exactly once");
        expect(observer.steamPresence()==1, "verified live session publishes cached Steam presence");
        const auto shared=observer.sessionSnapshot();
        expect(shared.pid==first.pi.dwProcessId && shared.created!=0 && _wcsicmp(shared.root.c_str(),root.c_str())==0, "sidebar and CSS guard share the same verified PID/birth snapshot");
        const auto initialIdentity=observer.identitySnapshot();
        expect(initialIdentity && initialIdentity->session==shared && initialIdentity->presence==1 &&
            initialIdentity->registryAccountCandidateKnown() && initialIdentity->registeredProcessMatches() &&
            initialIdentity->currentRegistryAccountKnown() && initialIdentity->windowsSessionKnown,
            "one immutable publication binds presence process registry source and Windows session");
        const auto readsBefore=registryReads.load();
        for(unsigned i=0;i<1001;++i) {
            const auto receipt=observer.identitySnapshot();
            if(receipt.get()!=initialIdentity.get()||!observer.sameIdentityEpoch(receipt))throw std::runtime_error("pure receipt consumption changed identity");
        }
        expect(registryReads.load()==readsBefore, "1001 identity consumers perform zero registry discovery");
        const auto initialRevision=initialIdentity->revision;
        expect(observer.requestIdentityRefresh() && until([&]{const auto r=observer.identitySnapshot();return r&&r->revision>initialRevision;}),
            "explicit refresh coalesces through the original producer");
        expect(observer.sameIdentityEpoch(initialIdentity) && initialIdentity->revision==initialRevision && changes==1 && retryCalls==0,
            "unchanged refresh keeps epochs immutable and does not refill a work budget");
        const auto beforeCpu=cpuTime();Sleep(600);const auto idleCpu=cpuTime()-beforeCpu;
        std::cout << "IDLE_CPU_MS " << idleCpu/10000.0 << " over 600ms (isolated observer process)\n";
        expect(idleCpu<1000000 && changes==1 && retryCalls==0, "idle subscription has no work callbacks and negligible CPU");
        for(DWORD i=0;i<40;++i)registry.number(L"UnrelatedValue",i,false);
        Sleep(200);expect(changes==1&&retryCalls==0, "unrelated registry writes do not dispatch or reset work budget");
        Child second(childPath);registry.number(L"pid",second.pi.dwProcessId);
        expect(until([&]{return changes.load()==2;}), "new Steam PID is adopted by registry event");
        expect(observer.steamPresence()==1, "replacement session keeps live Steam presence");
        const auto replacedIdentity=observer.identitySnapshot();
        expect(replacedIdentity && replacedIdentity->processEpoch>initialIdentity->processEpoch &&
            !observer.sameIdentityEpoch(initialIdentity) && initialIdentity->session.pid==first.pi.dwProcessId,
            "PID replacement revokes the old epoch without mutating held receipts");
        first.finish();Sleep(100);expect(changes==2, "old PID exit does not affect replacement session");
        second.finish();expect(until([&]{return changes.load()==3;}), "process exit handle signals without registry change or scan timer");
        registry.number(L"ActiveUser",0);expect(until([&]{return changes.load()==4;}), "temporary missing account delivers transition");
        const auto zeroIdentity=observer.identitySnapshot();
        expect(zeroIdentity && zeroIdentity->accountReadError==ERROR_SUCCESS && zeroIdentity->session.account==0 &&
            !zeroIdentity->registryAccountCandidateKnown() && !zeroIdentity->currentRegistryAccountKnown(),
            "a readable registry zero remains Unknown and is not a logout or login proof");
        registry.number(L"ActiveUser",42);expect(until([&]{return changes.load()==5;}), "account recovery is event-driven");
        const auto recoveredIdentity=observer.identitySnapshot();
        expect(recoveredIdentity && recoveredIdentity->accountEpoch>zeroIdentity->accountEpoch &&
            recoveredIdentity->processEpoch==zeroIdentity->processEpoch && recoveredIdentity->registryAccountCandidateKnown() &&
            !recoveredIdentity->currentRegistryAccountKnown(),
            "account-only recovery advances its own epoch and a stale positive account cannot prove a live session");
        RegDeleteTreeW(HKEY_CURRENT_USER,registry.path.c_str());
        expect(until([&]{return changes.load()>5;}), "deleted Steam key is observed");
        const auto missingIdentity=observer.identitySnapshot();
        expect(missingIdentity && missingIdentity->accountReadError!=ERROR_SUCCESS &&
            !missingIdentity->registryAccountCandidateKnown(), "registry failure is distinct from readable zero");
        Sleep(100);const auto deletedCount=changes.load();
        registry.root(root);registry.number(L"ActiveUser",43);registry.number(L"pid",0);
        expect(until([&]{return changes.load()>deletedCount;}), "recreated key heals through ancestor subscription");
        Sleep(150);const auto stableCount=changes.load();Sleep(150);
        expect(changes==stableCount, "deleted/recreated key does not cause a notification spin");
        const auto unchangedPid=regNumber(registry.path,L"pid");
        expect(unchangedPid==0, "restart regression keeps the ActiveProcess PID at zero");
        {
            Child restarted(childPath); // No registry write or manual beginWindow.
            expect(until([&]{return changes.load()==stableCount+1;}), "native Steam window creation wakes session discovery without a registry PID change");
            expect(observer.steamPresence()==1, "event-only restart adopts the verified Steam process");
            expect(regNumber(registry.path,L"pid")==unchangedPid, "observer never repairs or writes Steam registry state");
            restarted.finish();
            expect(until([&]{return changes.load()==stableCount+2;}), "event-adopted Steam process exit is observed via its owned read handle");
        }
        const auto postRestartCount=changes.load();
        observer.stop();
        expect(observer.steamPresence()==-1, "stopped observer cannot publish stale Steam absence/presence");
        expect(observer.sessionSnapshot().pid==0, "observer shutdown clears addon session identity");
        expect(!observer.identitySnapshot() && !observer.sameIdentityEpoch(initialIdentity),
            "teardown revokes receipt consumption even while another consumer holds its old bytes");
        registry.number(L"ActiveUser",99);Sleep(100);
        expect(changes==postRestartCount, "stop closes waits and prevents further callbacks");observer.stop();
        expect(observer.start([&](bool changed){if(changed)++changes;else ++retryCalls;},[]{return true;}), "observer can restart after cleanup");
        expect(until([&]{return changes.load()==postRestartCount+1;}), "restart delivers one fresh session");
        expect(until([&]{return retryCalls.load()==4;}, 23000), "real pending observer performs only four bounded retries");
        const auto exhaustedCpu = cpuTime();Sleep(1100);const auto afterBudgetCpu=cpuTime()-exhaustedCpu;
        std::cout << "EXHAUSTED_PENDING_IDLE_CPU_MS " << afterBudgetCpu/10000.0 << " over 1100ms\n";
        expect(retryCalls==4&&changes==postRestartCount+1&&afterBudgetCpu<1000000, "exhausted pending request waits for events with no further callbacks or CPU work");observer.stop();
        delegatedIdentityCases(root,childPath,registry);
        std::cout << "STEAM_SESSION_OBSERVER_OK tests=" << tests << " actualSteamOperations=0\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED "<<e.what()<<'\n';return 1;}
}

#endif // YMCC_STEAM_OBSERVER_SOURCE_OWNER_ONLY
