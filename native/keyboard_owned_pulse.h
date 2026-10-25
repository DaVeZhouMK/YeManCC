#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

// Owns only balanced, reverse-release virtual-key pulses from one YMCC producer.
// No platform input calls, clocks, timers, threads, logging or allocation here.
// The sender is synchronous/nonthrowing; its accepted-count prefix is an
// explicit bookkeeping assumption, NOT proof of OS or downstream key state.
namespace ymcc::keyboardPulse {
inline constexpr std::size_t capacity=4;
struct SendReceipt {UINT inserted=0;DWORD error=0;};
enum class Status { Sent, ShortSend, DebtBlocked, Busy, InvalidBatch };
struct Result {
    Status status=Status::InvalidBatch;
    bool originalIssued=false;
    UINT requested=0,inserted=0,repairInserted=0,apiCalls=0;
    DWORD originalError=0;
    std::size_t pendingReleases=0;
};
struct Snapshot {
    bool known=false,pending=false;
    std::array<INPUT,capacity> releases{};
    std::size_t releaseCount=0;
    std::uint64_t pulses=0,shortSends=0,repairCalls=0,blockedPulses=0;
    std::uint64_t lastFailureTick=0,retryAt=0;
    DWORD lastErrorHint=0;
    std::uint32_t pendingSource=0;
};
namespace detail {struct TestAccess;}
class Owner {
    friend struct detail::TestAccess;
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    struct Lease {std::atomic_flag& flag;~Lease()noexcept{flag.clear(std::memory_order_release);}};
    bool tryEnter()noexcept{return !busy_.test_and_set(std::memory_order_acquire);}
    std::array<INPUT,capacity> releases_{};
    std::size_t count_=0;
    std::atomic<bool> pending_{false};
    std::atomic<std::uint64_t> retryAt_{0};
    std::uint64_t pulses_=0,shortSends_=0,repairCalls_=0,blockedPulses_=0,lastFailureTick_=0;
    unsigned failures_=0;
    DWORD lastErrorHint_=0;
    std::uint32_t source_=0;
    static bool plan(const INPUT* inputs,UINT count,std::array<INPUT,capacity>& upForDown)noexcept {
        if(!inputs||count<2||count>capacity)return false;
        std::array<UINT,capacity> opened{};std::size_t n=0;
        for(UINT i=0;i<count;++i){const auto& p=inputs[i];
            if(p.type!=INPUT_KEYBOARD||!p.ki.wVk||p.ki.wVk>255||(p.ki.dwFlags&(KEYEVENTF_SCANCODE|KEYEVENTF_UNICODE)))return false;
            if((p.ki.dwFlags&KEYEVENTF_KEYUP)==0){
                // One key cannot be owned by nested pulses in this tiny batch.
                for(std::size_t k=0;k<n;++k)if(inputs[opened[k]].ki.wVk==p.ki.wVk)return false;
                opened[n++]=i;
            }else{
                if(!n||inputs[opened[n-1]].ki.wVk!=p.ki.wVk)return false;
                upForDown[opened[--n]]=p;
            }
        }
        return n==0;
    }
    void acceptOriginal(const INPUT* inputs,UINT accepted,const std::array<INPUT,capacity>& upForDown)noexcept {
        for(UINT i=0;i<accepted;++i){const auto& p=inputs[i];
            if((p.ki.dwFlags&KEYEVENTF_KEYUP)==0)releases_[count_++]=upForDown[i];
            else for(std::size_t k=0;k<count_;++k)if(releases_[k].ki.wVk==p.ki.wVk){
                for(std::size_t j=k+1;j<count_;++j)releases_[j-1]=releases_[j];--count_;break;
            }
        }
        pending_.store(count_!=0,std::memory_order_release);
    }
    void schedule(std::uint64_t now)noexcept {
        if(!count_){failures_=0;retryAt_.store(0,std::memory_order_release);return;}
        const auto delay=std::uint64_t(250u)<<((std::min)(failures_,3u));
        if(failures_<3)++failures_;
        retryAt_.store(now<=UINT64_MAX-delay?now+delay:UINT64_MAX,std::memory_order_release);
    }
    template<class Sender,class Observer> UINT repairLocked(std::uint64_t now,Sender& sender,Observer& observer,UINT& calls)noexcept {
        if(!count_)return 0;
        // Copy so count advancement never changes the observed outgoing batch.
        std::array<INPUT,capacity> outgoing{};const auto requested=static_cast<UINT>(count_);
        for(std::size_t i=0;i<count_;++i)outgoing[i]=releases_[count_-1-i];
        const auto receipt=sender(outgoing.data(),requested);++calls;++repairCalls_;
        const UINT accepted=(std::min)(receipt.inserted,requested);
        count_-=accepted; // Repairs use reverse press order, matching the original pulse.
        if(receipt.inserted!=requested){lastFailureTick_=now;lastErrorHint_=receipt.error;}
        pending_.store(count_!=0,std::memory_order_release);schedule(now);
        observer(outgoing.data(),requested,receipt,true,source_);
        return accepted;
    }
public:
    Owner()noexcept=default;
    Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
    bool pending()const noexcept{return pending_.load(std::memory_order_acquire);}
    template<class Sender,class Observer> Result submit(const INPUT* inputs,UINT requested,std::uint64_t now,Sender sender,Observer observer,std::uint32_t source=0) {
        static_assert(noexcept(sender(inputs,requested)));static_assert(noexcept(observer(inputs,requested,SendReceipt{},false,std::uint32_t{})));
        Result result;result.requested=requested;
        std::array<INPUT,capacity> ups{};if(!plan(inputs,requested,ups))return result;
        if(!tryEnter()){result.status=Status::Busy;return result;}
        const Lease lease{busy_};
        if(count_){
            // A new explicit pulse may attempt old owned releases immediately.
            result.repairInserted+=repairLocked(now,sender,observer,result.apiCalls);
            if(count_){++blockedPulses_;result.status=Status::DebtBlocked;result.pendingReleases=count_;return result;}
        }
        ++pulses_;source_=source;const auto receipt=sender(inputs,requested);++result.apiCalls;
        result.originalIssued=true;result.inserted=receipt.inserted;result.originalError=receipt.error;
        acceptOriginal(inputs,(std::min)(receipt.inserted,requested),ups);
        observer(inputs,requested,receipt,false,source_);
        if(receipt.inserted==requested){result.status=Status::Sent;failures_=0;retryAt_.store(0,std::memory_order_release);}
        else{
            ++shortSends_;lastFailureTick_=now;lastErrorHint_=receipt.error;result.status=Status::ShortSend;
            result.repairInserted+=repairLocked(now,sender,observer,result.apiCalls);
        }
        result.pendingReleases=count_;return result;
    }
    template<class Sender,class Observer> Result maintain(std::uint64_t now,bool force,Sender sender,Observer observer) {
        static_assert(noexcept(sender(static_cast<const INPUT*>(nullptr),UINT{})));
        static_assert(noexcept(observer(static_cast<const INPUT*>(nullptr),UINT{},SendReceipt{},true,std::uint32_t{})));
        Result result;result.status=Status::Sent;
        if(!pending()||(!force&&now<retryAt_.load(std::memory_order_acquire)))return result;
        if(!tryEnter()){result.status=Status::Busy;return result;}
        const Lease lease{busy_};
        if(!count_||(!force&&now<retryAt_.load(std::memory_order_relaxed)))return result;
        result.repairInserted=repairLocked(now,sender,observer,result.apiCalls);
        result.pendingReleases=count_;if(count_)result.status=Status::DebtBlocked;return result;
    }
    Snapshot snapshot() {
        Snapshot s;s.pending=pending();if(!tryEnter())return s;const Lease lease{busy_};
        s.known=true;s.pending=count_!=0;s.releases=releases_;s.releaseCount=count_;s.pulses=pulses_;s.shortSends=shortSends_;
        s.repairCalls=repairCalls_;s.blockedPulses=blockedPulses_;s.lastFailureTick=lastFailureTick_;s.retryAt=retryAt_.load();s.lastErrorHint=lastErrorHint_;s.pendingSource=source_;return s;
    }
};
} // namespace ymcc::keyboardPulse
