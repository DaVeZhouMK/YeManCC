#pragma once
#include "controller_hid_source.h"
#include <cstdint>
#include <string>

namespace ymcc::isolationhid {
struct Permit {
    std::uint64_t power=0,edge=0;
    std::wstring root,preferredPath;
    std::uint64_t suspendSerial=0;
};
struct Scene {
    bool xboxFse=false,originalIsolation=false,physicalSource=false,virtualOff=false;
    bool powerReady=false,stopping=false,higherPriority=false;
    std::uint64_t power=0,edge=0,suspendSerial=0;
};
inline bool admitted(const Permit& permit,const Scene& scene) noexcept {
    return scene.xboxFse && scene.originalIsolation && scene.physicalSource && scene.virtualOff &&
        scene.powerReady && !scene.stopping && !scene.higherPriority &&
        permit.power && permit.edge && !permit.root.empty() &&
        permit.power==scene.power && permit.edge==scene.edge && permit.suspendSerial==scene.suspendSerial;
}
struct Key {
    std::uint64_t power=0,edge=0,topology=0,identity=0,occupant=0;
    unsigned slot=4;
    std::wstring root,preferredPath,slotPath;
    std::uint64_t wakeSuspend=0,wakeNotice=0,wakeIntent=0;
    bool operator==(const Key&) const=default;
};
enum class ReadResult {Waiting,Frame,Failed};
enum class DrainResult {Waiting,Drained,Failed};
inline constexpr unsigned maxBindAttempts=4;
template<class Port> class Reader {
    Port port_;
    Key key_{};
    controllerhid::Frame frame_{};
    std::uint64_t sequence_=0,observedTick_=0,retryAt_=0;
    unsigned bindAttempts_=0;
    bool haveKey_=false,haveFrame_=false,awaitingFresh_=false,discardPending_=false,cancelIssued_=false;
public:
    Port& port() noexcept {return port_;}
    const Port& port() const noexcept {return port_;}
    bool awaitingFresh() const noexcept {return awaitingFresh_;}
    bool idle() const noexcept {
        return !haveKey_ && !haveFrame_ && !awaitingFresh_ && !discardPending_ &&
            !cancelIssued_ && port_.returned();
    }
    void pause() noexcept {
        haveFrame_=false;frame_={};observedTick_=0;
        awaitingFresh_=true;discardPending_=port_.pending();
    }
    void withdraw() {
        haveKey_=haveFrame_=false;key_={};frame_={};observedTick_=0;
        awaitingFresh_=discardPending_=cancelIssued_=false;
        retryAt_=0;bindAttempts_=0;port_.close();
    }
    bool retire() {
        withdraw();port_.serviceClosing();return port_.returned();
    }
    bool read(const Key* key,std::uint64_t now,controllerhid::Frame& frame,
        std::uint64_t& sequence,std::uint64_t& observedTick) {
        if(!key || !key->power || !key->edge || !key->topology || !key->identity || !key->occupant ||
            key->slot>=4 || key->root.empty() || key->slotPath.empty()) {
            withdraw();port_.serviceClosing();return false;
        }
        if(!haveKey_ || !(key_==*key)) {
            withdraw();key_=*key;haveKey_=true;
        }
        port_.serviceClosing();
        if(!port_.bound() && port_.canBind() && bindAttempts_<maxBindAttempts && now>=retryAt_) {
            ++bindAttempts_;retryAt_=now+250;
            if(!port_.bind(key_,bindAttempts_))port_.close();
        }
        if(!port_.bound())return false;
        if(discardPending_) {
            const auto drained=port_.discardPending(!cancelIssued_);
            cancelIssued_=true;
            if(drained==DrainResult::Waiting)return false;
            discardPending_=cancelIssued_=false;
            if(drained==DrainResult::Failed) {
                awaitingFresh_=false;port_.close();return false;
            }
        }
        if(!port_.pending() && !port_.begin()) {
            port_.close();
            awaitingFresh_=haveFrame_=false;frame_={};observedTick_=0;return false;
        }
        controllerhid::Frame incoming;
        const auto result=port_.poll(incoming);
        if(result==ReadResult::Failed) {
            awaitingFresh_=haveFrame_=false;frame_={};observedTick_=0;port_.close();return false;
        }
        if(result==ReadResult::Frame) {
            frame_=incoming;haveFrame_=true;awaitingFresh_=false;observedTick_=now;++sequence_;
        }
        if(!port_.pending() && !port_.begin()) {
            port_.close();
            const bool completed=result==ReadResult::Frame && haveFrame_;
            if(completed){frame=frame_;sequence=sequence_;observedTick=observedTick_;}
            awaitingFresh_=haveFrame_=false;frame_={};observedTick_=0;return completed;
        }
        if(!haveFrame_)return false;
        frame=frame_;sequence=sequence_;observedTick=observedTick_;return true;
    }
};
}
