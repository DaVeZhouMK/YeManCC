#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// GP-PREREQ-2: one child per application session, outside the input/driver lifecycle.
// No device operations, background thread, restart, repair loop or installer termination here.
namespace ymcc {
class GamepadDriverSetup {
public:
    enum State { NotStarted=0, Pending=1, Ready=2, Failed=3, RebootRequired=4, Slow=5, InstallerMissing=6, InstallerInvalid=7 };
    static constexpr State ResultState(DWORD code) {
        return code==0 ? Ready :
            (code==3010 || code==1641) ? RebootRequired :
            code==12 ? InstallerMissing :
            code==13 ? InstallerInvalid : Failed;
    }
    // Environment preparation is advisory; target admission belongs to the actual backend.
    static constexpr bool HasWarning(State s) { return s!=NotStarted && s!=Ready; }
    ~GamepadDriverSetup() { if (process_) CloseHandle(process_); }
    State state() const { return state_.load(std::memory_order_acquire); }
    DWORD result() const { return result_.load(std::memory_order_acquire); }
    bool warnsTargets() const { return HasWarning(state()); }
    bool active() const { const auto s=state(); return s==Pending || s==Slow; }
    void start(const std::wstring& root, const std::wstring& report) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state()!=NotStarted) return;
        const std::wstring executable=root+L"\\PowerControl\\redist\\HIDMaestroSetup.exe";
        std::wstring command=quote(executable)+L" --prepare --owner-pid "+std::to_wstring(GetCurrentProcessId())+
            L" --install-root "+quote(root)+L" --report "+quote(report);
        std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(L'\0');
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi{};
        started_=GetTickCount64();
        if (!CreateProcessW(executable.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                nullptr,(root+L"\\PowerControl\\redist").c_str(),&si,&pi)) {
            result_.store(GetLastError(),std::memory_order_release);state_.store(Failed,std::memory_order_release);return;
        }
        CloseHandle(pi.hThread);process_=pi.hProcess;state_.store(Pending,std::memory_order_release);
    }
    bool poll() {
        if (!active()) return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if (!process_) return false;
        DWORD wait=WaitForSingleObject(process_,0);
        if (wait==WAIT_OBJECT_0 || wait==WAIT_FAILED) {
            DWORD code=ERROR_PROCESS_ABORTED;
            if (wait==WAIT_OBJECT_0 && !GetExitCodeProcess(process_,&code)) code=GetLastError();
            if (wait==WAIT_FAILED) code=GetLastError();
            CloseHandle(process_);process_=nullptr;
            result_.store(code,std::memory_order_release);state_.store(ResultState(code),std::memory_order_release);return true;
        }
        if (state()==Pending && GetTickCount64()-started_>=180000) {
            // Report the slow preparation and keep observing this SAME child; target attempts remain allowed.
            // An MSI transaction is never killed and another installer is never launched.
            state_.store(Slow,std::memory_order_release);return true;
        }
        return false;
    }
private:
    static std::wstring quote(const std::wstring& value) {
        std::wstring out=L"\"";unsigned slashes=0;
        for (wchar_t c:value) {
            if (c==L'\\') {++slashes;continue;}
            if (c==L'\"') {out.append(slashes*2+1,L'\\');out+=c;}
            else {out.append(slashes,L'\\');out+=c;}
            slashes=0;
        }
        out.append(slashes*2,L'\\');out+=L'\"';return out;
    }
    std::mutex mutex_;
    std::atomic<State> state_{NotStarted};
    std::atomic<DWORD> result_{0};
    HANDLE process_=nullptr;
    ULONGLONG started_=0;
};
static_assert(!GamepadDriverSetup::HasWarning(GamepadDriverSetup::NotStarted));
static_assert(!GamepadDriverSetup::HasWarning(GamepadDriverSetup::Ready));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Pending));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Slow));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Failed));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::RebootRequired));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::InstallerMissing));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::InstallerInvalid));
static_assert(GamepadDriverSetup::ResultState(3010)==GamepadDriverSetup::RebootRequired);
static_assert(GamepadDriverSetup::ResultState(12)==GamepadDriverSetup::InstallerMissing);
static_assert(GamepadDriverSetup::ResultState(13)==GamepadDriverSetup::InstallerInvalid);
static_assert(GamepadDriverSetup::ResultState(1603)==GamepadDriverSetup::Failed);
}
