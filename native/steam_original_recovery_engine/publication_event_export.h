#pragma once
#include <windows.h>
#include <cstdint>
#include <array>
#include <functional>
namespace running_trial {
// Host-only resource transport. It produces no power/account fact or authority.
// Caller pins the original target and commits the original ledger first.
class PublicationEventExport final {
    HANDLE target_=nullptr;
    std::array<HANDLE,3> remote_{};
    bool attempted_=false,uncertain_=false;
public:
    PublicationEventExport()=default;
    PublicationEventExport(const PublicationEventExport&)=delete;
    PublicationEventExport& operator=(const PublicationEventExport&)=delete;
    ~PublicationEventExport(){if(target_)CloseHandle(target_);}
    using Current=std::function<bool()>;
    bool begin(HANDLE target,HANDLE power,HANDLE identity,const Current& current={})noexcept {
        const auto admitted=[&]()noexcept{try{return !current||current();}catch(...){return false;}};
        if(attempted_)return false;attempted_=true;
        if(!target||!power||!identity||power==INVALID_HANDLE_VALUE||identity==INVALID_HANDLE_VALUE||power==identity)return false;
        if(!admitted())return false;
        if(!DuplicateHandle(GetCurrentProcess(),target,GetCurrentProcess(),&target_,
                PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return false;
        if(!admitted())return false;
        if(!GetProcessId(target_)||WaitForSingleObject(target_,0)!=WAIT_TIMEOUT)return false;
        if(!admitted())return false;
        if(!DuplicateHandle(GetCurrentProcess(),power,target_,&remote_[0],SYNCHRONIZE,FALSE,0))return false;
        if(!admitted())return false;
        if(!DuplicateHandle(GetCurrentProcess(),identity,target_,&remote_[1],SYNCHRONIZE,FALSE,0))return false;
        return admitted();
    }
    // Host exports three cancellation events only after its original commit.
    // begin retains the original two-event implementation and single attempt.
    bool beginThree(HANDLE target,HANDLE first,HANDLE second,HANDLE third,const Current& current={})noexcept {
        if(!third||third==INVALID_HANDLE_VALUE||third==first||third==second){attempted_=true;return false;}
        if(!begin(target,first,second,current))return false;
        const auto admitted=[&]()noexcept{try{return !current||current();}catch(...){return false;}};
        if(!admitted())return false;
        if(!DuplicateHandle(GetCurrentProcess(),third,target_,&remote_[2],SYNCHRONIZE,FALSE,0))return false;
        return admitted();
    }
    std::uint64_t third()const noexcept{return reinterpret_cast<std::uintptr_t>(remote_[2]);}
    std::uint64_t power()const noexcept{return reinterpret_cast<std::uintptr_t>(remote_[0]);}
    std::uint64_t identity()const noexcept{return reinterpret_cast<std::uintptr_t>(remote_[1]);}
    bool pending()const noexcept{
        if(uncertain_)return true;
        for(const auto slot:remote_)if(slot)return true;
        return false;
    }
    bool forgetIfExited()noexcept {
        if(!target_||WaitForSingleObject(target_,0)!=WAIT_OBJECT_0)return false;
        remote_={};uncertain_=false;return true;
    }
    // Only after the caller proves no management call can still read any
    // borrowed transport slots. Receiver owns its separate local copies.
    bool releaseKnown()noexcept {
        if(!pending())return true;
        if(forgetIfExited())return true;
        if(uncertain_||!target_)return false;
        for(auto& slot:remote_){
            if(!slot)continue;
            const HANDLE original=slot;slot=nullptr;
            HANDLE local=nullptr;
            const auto copied=DuplicateHandle(target_,original,GetCurrentProcess(),&local,0,FALSE,
                DUPLICATE_CLOSE_SOURCE|DUPLICATE_SAME_ACCESS);
            // CLOSE_SOURCE may have closed the original even on failure. Never
            // retry that number: it might now name another target object.
            if(!copied){uncertain_=true;return false;}
            if(!CloseHandle(local)){uncertain_=true;return false;}
        }
        return !pending();
    }
};
}
