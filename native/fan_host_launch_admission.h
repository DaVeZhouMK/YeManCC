#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>

// FAN-951: every native FanHost launch, including concurrent renderers, uses
// this machine gate and checks all paths/versions before CreateProcessW.
class FanHostLaunchAdmission {
    HANDLE gate_ = nullptr;
    bool owned_ = false;
    DWORD blocker_ = 0;
    std::string failure_;
public:
    FanHostLaunchAdmission() {
        gate_ = CreateMutexW(nullptr, FALSE, L"Global\\YeManFanHost-launch-v1");
        if (!gate_) { failure_ = "launch-gate-unavailable"; return; }
        const DWORD wait = WaitForSingleObject(gate_, 0);
        owned_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!owned_) { failure_ = "launch-already-in-flight"; return; }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) { failure_ = "inventory-unavailable"; return; }
        PROCESSENTRY32W entry{sizeof(entry)};
        if (!Process32FirstW(snapshot, &entry)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) failure_ = "inventory-unavailable";
        } else {
            do {
                if (_wcsicmp(entry.szExeFile, L"YeManFanHost.exe") != 0) continue;
                HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
                const bool exited = process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
                if (process) CloseHandle(process);
                if (exited) continue;
                // An inaccessible process is a blocker, never an absent owner.
                blocker_ = entry.th32ProcessID;
                failure_ = "resident-host-requires-exact-exit";
                break;
            } while (Process32NextW(snapshot, &entry));
            if (failure_.empty() && GetLastError() != ERROR_NO_MORE_FILES)
                failure_ = "inventory-unavailable";
        }
        CloseHandle(snapshot);
    }
    ~FanHostLaunchAdmission() {
        if (owned_) ReleaseMutex(gate_);
        if (gate_) CloseHandle(gate_);
    }
    FanHostLaunchAdmission(const FanHostLaunchAdmission&) = delete;
    FanHostLaunchAdmission& operator=(const FanHostLaunchAdmission&) = delete;
    bool allowed() const { return owned_ && failure_.empty(); }
    DWORD blockerPid() const { return blocker_; }
    const std::string& failure() const { return failure_; }
};
