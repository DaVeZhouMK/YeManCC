#pragma once
#include "steam_command_coordinator.h"
#include <windows.h>
#include <mutex>

namespace ymcc::steamcommand {
// One existing Native worker's dedicated availability hint. This event is
// separate from its original observation Hint and every cancellation event.
// The existing Control must retain this object BEFORE open, throughout every
// borrowed wait, and after a refused close. Close only after worker.join().
// No destructor or successful signal constitutes resource-return evidence.
class AvailabilityEvent final:public AvailabilitySignal {
public:
    struct Status {
        bool opened=false,closing=false;
        DWORD openError=ERROR_SUCCESS,signalError=ERROR_SUCCESS;
        DWORD resetError=ERROR_SUCCESS,closeError=ERROR_SUCCESS;
    };
private:
    mutable std::mutex mutex_;
    HANDLE event_=nullptr;
    Status status_;
public:
    AvailabilityEvent()=default; // Allocation and publication precede OS creation.
    AvailabilityEvent(const AvailabilityEvent&)=delete;
    AvailabilityEvent& operator=(const AvailabilityEvent&)=delete;
    ~AvailabilityEvent()override{(void)close();} // Final teardown fallback only.
    bool open()noexcept {
        std::lock_guard lock(mutex_);
        if(status_.closing)return false; // Never replace an old retained resource.
        if(event_)return true;
        event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        status_.openError=event_?ERROR_SUCCESS:GetLastError();
        status_.opened=event_!=nullptr;
        return status_.opened;
    }
    void notify()noexcept override {
        std::lock_guard lock(mutex_);
        if(status_.closing||!event_)return;
        if(!SetEvent(event_))status_.signalError=GetLastError();
    }
    // Only the original worker resets, immediately BEFORE promote. Original
    // power/identity/business cancellation events are never reset here.
    bool reset()noexcept {
        std::lock_guard lock(mutex_);
        if(status_.closing||!event_)return false;
        if(!ResetEvent(event_)){status_.resetError=GetLastError();return false;}
        return status_.signalError==ERROR_SUCCESS;
    }
    // Borrowed until the same existing worker has joined. The Control remains
    // the owner; a caller must not duplicate, replace or close this handle.
    HANDLE event()const noexcept {
        std::lock_guard lock(mutex_);return status_.closing?nullptr:event_;
    }
    Status status()const noexcept {
        std::lock_guard lock(mutex_);return status_;
    }
    bool close()noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return false; // Owner retains and retries, no wait.
        status_.closing=true;
        if(!event_)return true;
        if(!CloseHandle(event_)){status_.closeError=GetLastError();return false;}
        event_=nullptr;status_.opened=false;status_.closeError=ERROR_SUCCESS;
        return true;
    }
};
}
