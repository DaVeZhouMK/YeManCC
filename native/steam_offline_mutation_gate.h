#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Cooperating YMCC Steam file writers and cold starts use the original
// shortcut-writer mutex. This grants exclusion only, never identity/power or
// recovery permission. Writers retain their original validation and rollback.
namespace ymcc::steamoffline {
inline constexpr wchar_t mutationMutexName[]=L"Local\\YeManSteamArtworkLabShortcutWriter";
class Guard final {
    HANDLE handle_=nullptr;
    bool owned_=false;
    DWORD wait_=WAIT_FAILED,error_=ERROR_SUCCESS;
public:
    explicit Guard(DWORD timeoutMs=0) noexcept {
        if(timeoutMs>30000){error_=ERROR_INVALID_PARAMETER;return;}
        handle_=CreateMutexW(nullptr,FALSE,mutationMutexName);
        if(!handle_){error_=GetLastError();return;}
        wait_=WaitForSingleObject(handle_,timeoutMs);
        owned_=wait_==WAIT_OBJECT_0||wait_==WAIT_ABANDONED;
        if(!owned_)error_=wait_==WAIT_TIMEOUT?ERROR_BUSY:wait_==WAIT_FAILED?GetLastError():ERROR_INVALID_DATA;
    }
    Guard(const Guard&)=delete;
    Guard& operator=(const Guard&)=delete;
    ~Guard() noexcept {
        if(owned_)ReleaseMutex(handle_);
        if(handle_)CloseHandle(handle_);
    }
    explicit operator bool()const noexcept{return owned_;}
    DWORD error()const noexcept{return error_;}
    bool abandoned()const noexcept{return owned_&&wait_==WAIT_ABANDONED;}
};
}
