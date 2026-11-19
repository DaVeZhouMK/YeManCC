#pragma once
#include "steam_session_observer.h"
#include "steam_game_process_facts.h"
#include "steam_native_power_access.h"
#include "sleep_power_observation.h"
#include <atomic>
#include <memory>
#include <utility>

namespace ymcc::steamnative {
// Chosen by the original event policy; neither value attests native login.
enum class ReadPurpose { ObserveOriginal, PrepareMutation };
struct ObservationPowerAccess {
    sleeppower::Reader read=nullptr;
    sleeppower::RevocationMirrors::Registration (*observe)(const sleeppower::SharedPowerV1&)noexcept=nullptr;
};
// Read-only qualification lifetime. Registry account fields remain candidates.
// Actual CUser/account64/GameID/record/primary proof stays with the Native reader;
// this scope is neither NativeQualified provenance nor a final publisher ticket.
class ReadScope {
    friend std::shared_ptr<ReadScope> acquireReadScope(steamsession::Observer&,
        const std::shared_ptr<const steamsession::IdentitySnapshot>&,steamsource::NativePowerAccess,
        ReadPurpose,ObservationPowerAccess)noexcept;
    // Issued only by the existing factory, for this exact object. A caller's
    // ReadScope subclass or copied expected() DTO cannot supply this origin.
    const steamsession::Observer* publisher_=nullptr;
    const ReadScope* issuedObject_=nullptr;
    void bindPublisher(const steamsession::Observer& publisher)noexcept{
        publisher_=&publisher;issuedObject_=this;
    }
protected:
    ReadScope()=default;
public:
    ReadScope(const ReadScope&)=delete;
    ReadScope& operator=(const ReadScope&)=delete;
    ReadScope(ReadScope&&)=delete;
    ReadScope& operator=(ReadScope&&)=delete;
    virtual ~ReadScope()=default;
    // In-process source association only; no account/login/action authority.
    bool issuedFor(const steamsession::Observer& publisher,
        const steamsession::IdentitySnapshot& held)const noexcept{
        if(publisher_!=&publisher||issuedObject_!=this)return false;
        const auto& receipt=expected();
        return held.processEpoch&&held.accountEpoch&&receipt.processEpoch==held.processEpoch&&
            receipt.accountEpoch==held.accountEpoch&&receipt.session==held.session&&
            receipt.presence==held.presence&&receipt.rootReadError==held.rootReadError&&
            receipt.registeredPid==held.registeredPid&&receipt.registeredPidReadError==held.registeredPidReadError&&
            receipt.accountReadError==held.accountReadError&&receipt.windowsSessionKnown&&held.windowsSessionKnown&&
            receipt.windowsSession==held.windowsSession&&receipt.windowsSessionError==ERROR_SUCCESS&&
            held.windowsSessionError==ERROR_SUCCESS;
    }
    virtual bool current()noexcept=0;
    virtual const steamsession::IdentitySnapshot& expected()const noexcept=0;
    virtual const sleeppower::SharedPowerV1& power()const noexcept=0;
    // Both handles are borrowed. Keep this scope through all actor/debug/remote
    // cleanup; do not ResetEvent or CloseHandle either original event.
    virtual HANDLE identityCancellationEvent()const noexcept=0;
    virtual HANDLE powerCancellationEvent()const noexcept=0;
    virtual void cancel()noexcept=0;
};
namespace detail {
// Context for the original non-mutating observation path. No Ready stamp or
// power event is minted; all pre-existing phase/generation/config/game gates
// remain with that path. It cannot stand in for a PrepareMutation capture.
class HeldObservationScope final:public ReadScope {
    const std::shared_ptr<steamsession::NativeReadDelegation> identity_;
    const steamgamefacts::detail::PinnedProcess steam_;
    const sleeppower::SharedPowerV1 unavailablePower_{},observedPower_;
    const ObservationPowerAccess access_;
    sleeppower::RevocationMirrors::Registration registration_;
    std::atomic<bool> cancelled_{false};
public:
    HeldObservationScope(std::shared_ptr<steamsession::NativeReadDelegation> identity,steamgamefacts::detail::PinnedProcess steam,
        ObservationPowerAccess access,sleeppower::SharedPowerV1 power,sleeppower::RevocationMirrors::Registration registration)
        :identity_(std::move(identity)),steam_(std::move(steam)),observedPower_(power),access_(access),registration_(std::move(registration)){}
    ~HeldObservationScope()override{cancel();}
    const steamsession::IdentitySnapshot& expected()const noexcept override{return identity_->expected();}
    const sleeppower::SharedPowerV1& power()const noexcept override{return unavailablePower_;}
    HANDLE identityCancellationEvent()const noexcept override{return identity_->cancellationEvent();}
    HANDLE powerCancellationEvent()const noexcept override{return registration_.event();}
    void cancel()noexcept override{cancelled_.store(true,std::memory_order_release);identity_->cancel();registration_.cancel();}
    bool current()noexcept override{
        if(cancelled_.load(std::memory_order_acquire))return false;
        if(!identity_->current()||registration_.cancelled()||!steamgamefacts::detail::alive(steam_.handle.value)||
            !sleeppower::powerObservationCurrent(access_.read,observedPower_)||!identity_->current()||registration_.cancelled()){
            cancel();return false;
        }
        return !cancelled_.load(std::memory_order_acquire);
    }
};
class HeldReadScope final:public ReadScope {
    const std::shared_ptr<steamsession::NativeReadDelegation> identity_;
    const steamgamefacts::detail::PinnedProcess steam_;
    const steamsource::NativePowerAccess access_;
    const sleeppower::SharedPowerV1 power_;
    sleeppower::RevocationMirrors::Registration registration_;
    std::atomic<bool> cancelled_{false};
public:
    HeldReadScope(std::shared_ptr<steamsession::NativeReadDelegation> identity,
        steamgamefacts::detail::PinnedProcess steam,steamsource::NativePowerAccess access,
        sleeppower::SharedPowerV1 power,sleeppower::RevocationMirrors::Registration registration)
        :identity_(std::move(identity)),steam_(std::move(steam)),access_(access),power_(power),registration_(std::move(registration)){}
    ~HeldReadScope()override{cancel();}
    const steamsession::IdentitySnapshot& expected()const noexcept override{return identity_->expected();}
    const sleeppower::SharedPowerV1& power()const noexcept override{return power_;}
    HANDLE identityCancellationEvent()const noexcept override{return identity_->cancellationEvent();}
    HANDLE powerCancellationEvent()const noexcept override{return registration_.event();}
    void cancel()noexcept override {
        cancelled_.store(true,std::memory_order_release);
        identity_->cancel();registration_.cancel();
    }
    bool current()noexcept override {
        if(cancelled_.load(std::memory_order_acquire))return false;
        if(!identity_->current()||registration_.cancelled()||!steamgamefacts::detail::alive(steam_.handle.value)||
            !access_.current||!access_.current(power_)||!identity_->current()||registration_.cancelled()||
            !steamgamefacts::detail::alive(steam_.handle.value)){
            cancel();return false;
        }
        return !cancelled_.load(std::memory_order_acquire);
    }
};
}
inline std::shared_ptr<ReadScope> acquireReadScope(steamsession::Observer& observer,
    const std::shared_ptr<const steamsession::IdentitySnapshot>& expected,steamsource::NativePowerAccess access,
    ReadPurpose purpose=ReadPurpose::PrepareMutation,ObservationPowerAccess observationAccess={})noexcept {
    try {
        if(!expected||(purpose!=ReadPurpose::ObserveOriginal&&purpose!=ReadPurpose::PrepareMutation))return {};
        if(purpose==ReadPurpose::ObserveOriginal){
            sleeppower::SharedPowerV1 observed;
            if(!observationAccess.read||!observationAccess.observe||!sleeppower::capturePowerObservation(observationAccess.read,observed))return {};
            auto identity=observer.delegateNativeRead(expected);if(!identity||!identity->current())return {};
            const auto& receipt=identity->expected();
            auto steam=steamgamefacts::detail::pin({receipt.session.pid,receipt.session.created,receipt.session.root+L"\\steam.exe"});
            if(!steam||steam->windowsSession!=receipt.windowsSession||!identity->current())return {};
            auto registration=observationAccess.observe(observed);
            if(!registration||registration.cancelled()||!sleeppower::powerObservationCurrent(observationAccess.read,observed))return {};
            auto scope=std::make_shared<detail::HeldObservationScope>(std::move(identity),std::move(*steam),observationAccess,observed,std::move(registration));
            scope->bindPublisher(observer);
            return scope->current()?std::shared_ptr<ReadScope>(std::move(scope)):std::shared_ptr<ReadScope>{};
        }
        if(!access.capture||!access.current||!access.observe)return {};
        sleeppower::SharedPowerV1 power;
        if(!access.capture(power)||!sleeppower::powerAdmitted(power)||!access.current(power))return {};
        auto identity=observer.delegateNativeRead(expected);
        if(!identity||!identity->current())return {};
        const auto& receipt=identity->expected();
        auto steam=steamgamefacts::detail::pin({receipt.session.pid,receipt.session.created,receipt.session.root+L"\\steam.exe"});
        if(!steam||steam->windowsSession!=receipt.windowsSession||!identity->current()||!access.current(power))return {};
        auto registration=access.observe(power);
        if(!registration||registration.cancelled()||!identity->current()||!access.current(power))return {};
        auto scope=std::make_shared<detail::HeldReadScope>(std::move(identity),std::move(*steam),access,power,std::move(registration));
        scope->bindPublisher(observer);
        return scope->current()?std::shared_ptr<ReadScope>(std::move(scope)):std::shared_ptr<ReadScope>{};
    }catch(...){return {};}
}
}
