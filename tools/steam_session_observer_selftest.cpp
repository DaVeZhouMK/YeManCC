#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_session_observer.h"
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
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--child") {
        HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, argv[2]);
        if (!h) return 2;
        WaitForSingleObject(h, 30000); CloseHandle(h); return 0; // Crash backstop; no polling.
    }
    try {
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
        expect(argc==2, "selftest output workspace supplied");
        const auto root = std::filesystem::absolute(argv[1]).wstring();
        const auto childPath = root + L"\\steam.exe";
        wchar_t current[32768]; GetModuleFileNameW(nullptr, current, static_cast<DWORD>(std::size(current)));
        expect(CopyFileW(current, childPath.c_str(), FALSE), "isolated synthetic Steam child copied inside test workspace");
        Registry registry(L"Software\\YeManCC-SteamSessionSelftest\\"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Child first(childPath);
        registry.root(root); registry.number(L"ActiveUser",42); registry.number(L"pid",first.pi.dwProcessId);
        ULONGLONG born=0;HANDLE verified=verifiedProcess(first.pi.dwProcessId,root,&born);
        expect(verified&&born, "process identity verifies test Steam executable and birth time");if(verified)CloseHandle(verified);
        verified=verifiedProcess(GetCurrentProcessId(),root,&born);
        expect(!verified, "unrelated PID cannot be adopted");if(verified)CloseHandle(verified);
        Observer observer(registry.path);
        expect(observer.steamPresence()==-1, "Steam presence unknown before observer starts");
        std::atomic<int> changes{0},retryCalls{0};
        expect(observer.start([&](bool changed){if(changed)++changes;else ++retryCalls;},[]{return false;}), "real asynchronous registry observer starts");
        expect(until([&]{return changes.load()==1;}), "initial session delivered exactly once");
        expect(observer.steamPresence()==1, "verified live session publishes cached Steam presence");
        const auto beforeCpu=cpuTime();Sleep(600);const auto idleCpu=cpuTime()-beforeCpu;
        std::cout << "IDLE_CPU_MS " << idleCpu/10000.0 << " over 600ms (isolated observer process)\n";
        expect(idleCpu<1000000 && changes==1 && retryCalls==0, "idle subscription has no work callbacks and negligible CPU");
        for(DWORD i=0;i<40;++i)registry.number(L"UnrelatedValue",i,false);
        Sleep(200);expect(changes==1&&retryCalls==0, "unrelated registry writes do not dispatch or reset work budget");
        Child second(childPath);registry.number(L"pid",second.pi.dwProcessId);
        expect(until([&]{return changes.load()==2;}), "new Steam PID is adopted by registry event");
        expect(observer.steamPresence()==1, "replacement session keeps live Steam presence");
        first.finish();Sleep(100);expect(changes==2, "old PID exit does not affect replacement session");
        second.finish();expect(until([&]{return changes.load()==3;}), "process exit handle signals without registry change or scan timer");
        registry.number(L"ActiveUser",0);expect(until([&]{return changes.load()==4;}), "temporary missing account delivers transition");
        registry.number(L"ActiveUser",42);expect(until([&]{return changes.load()==5;}), "account recovery is event-driven");
        RegDeleteTreeW(HKEY_CURRENT_USER,registry.path.c_str());
        expect(until([&]{return changes.load()>5;}), "deleted Steam key is observed");
        Sleep(100);const auto deletedCount=changes.load();
        registry.root(root);registry.number(L"ActiveUser",43);registry.number(L"pid",0);
        expect(until([&]{return changes.load()>deletedCount;}), "recreated key heals through ancestor subscription");
        Sleep(150);const auto stableCount=changes.load();Sleep(150);
        expect(changes==stableCount, "deleted/recreated key does not cause a notification spin");
        observer.stop();
        expect(observer.steamPresence()==-1, "stopped observer cannot publish stale Steam absence/presence");
        registry.number(L"ActiveUser",99);Sleep(100);
        expect(changes==stableCount, "stop closes waits and prevents further callbacks");observer.stop();
        expect(observer.start([&](bool changed){if(changed)++changes;else ++retryCalls;},[]{return true;}), "observer can restart after cleanup");
        expect(until([&]{return changes.load()==stableCount+1;}), "restart delivers one fresh session");
        expect(until([&]{return retryCalls.load()==4;}, 23000), "real pending observer performs only four bounded retries");
        const auto exhaustedCpu = cpuTime();Sleep(1100);const auto afterBudgetCpu=cpuTime()-exhaustedCpu;
        std::cout << "EXHAUSTED_PENDING_IDLE_CPU_MS " << afterBudgetCpu/10000.0 << " over 1100ms\n";
        expect(retryCalls==4&&changes==stableCount+1&&afterBudgetCpu<1000000, "exhausted pending request waits for events with no further callbacks or CPU work");observer.stop();
        std::cout << "STEAM_SESSION_OBSERVER_OK tests=" << tests << " actualSteamOperations=0\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED "<<e.what()<<'\n';return 1;}
}