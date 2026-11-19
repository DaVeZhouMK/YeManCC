#pragma once
#include "steam_game_qualified_role_read.h"
#include "steam_native_record_win32.h"
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace ymcc::steamgamefacts {
class MainRunningGroupRoleIssuer;
// Completed, read-only native roles from the public source's actual typed read.
// A consumer cannot construct this object from a graph, JSON or supplied P/F.
// current() checks held scope/objects and the original owning request. The tuple
// belongs to this completed read; current() does not perform a fresh native read
// or confer Engine healthy-reference, publication, action or replay authority.
class RunningLinkNativeRoleObservation final {
    friend class MainRunningGroupRoleIssuer;
    const std::shared_ptr<ProcessGroupLease> group_;
    const std::shared_ptr<ymcc::steamnative::ReadScope> scope_;
    const ymcc::steamrecordwin::TypedNativeRoleObservation native_;
    const std::function<bool()> requestCurrent_;
    mutable std::atomic<bool> revoked_{false};
    RunningLinkNativeRoleObservation(std::shared_ptr<ProcessGroupLease> group,
        std::shared_ptr<ymcc::steamnative::ReadScope> scope,
        ymcc::steamrecordwin::TypedNativeRoleObservation native,std::function<bool()> requestCurrent)
        :group_(std::move(group)),scope_(std::move(scope)),native_(std::move(native)),
         requestCurrent_(std::move(requestCurrent)){}
public:
    RunningLinkNativeRoleObservation(const RunningLinkNativeRoleObservation&)=delete;
    RunningLinkNativeRoleObservation& operator=(const RunningLinkNativeRoleObservation&)=delete;
    void cancel()const noexcept {revoked_.store(true,std::memory_order_release);}
    bool current()const noexcept {
        try{
            if(revoked_.load(std::memory_order_acquire)||!group_||!scope_||!requestCurrent_||
                !requestCurrent_()||!group_->nativeReadScopeCurrent(*scope_)||!requestCurrent_()){
                cancel();return false;
            }
            return !revoked_.load(std::memory_order_acquire);
        }catch(...){cancel();return false;}
    }
    const auto& processGroup()const noexcept{return group_;}
    const auto& selectedGame()const noexcept{return group_->selectedGame();}
    const auto& steam()const noexcept{return group_->steam();}
    std::uint64_t account64()const noexcept{return native_.record().query().account64;}
    std::uint32_t appid()const noexcept{return group_->source().appid;}
    std::uint64_t fullGameId()const noexcept{return native_.record().query().gameId64;}
    std::uint32_t primaryPid()const noexcept{return native_.record().nativeRoles()->primaryLivenessPid;}
    std::uint32_t foregroundPid()const noexcept{return native_.record().nativeRoles()->foregroundComparisonPid;}
    bool nativePrimaryExact()const noexcept{return primaryPid()==selectedGame().pid;}
    bool nativeForegroundExact()const noexcept{return foregroundPid()==selectedGame().pid;}
};
}
