#pragma once
// Coordination only. This does not establish identity, consume permission, or
// acknowledge a Steam effect. The original source/process/power/admit gates stay
// at each actual sink. Only the normal Native parent installs a Provider.
#include "steam_command_disposition.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace ymcc::steamcommand {
enum class Effect : std::uint8_t { Desktop, BigPicture, Stop, Home, Focus, Mutation };
enum class Plan : std::uint8_t { Single, Reopen };
struct Target {
    std::uint32_t pid=0;
    std::uint64_t birth=0;
    std::wstring root;
    bool operator==(const Target&)const=default;
    bool valid()const noexcept{return pid&&birth&&!root.empty();}
};
struct State;
struct Operation {
    const Target target;
    const Plan plan;
    const bool shellActivation;
    bool parent=true,sealed=false,entered=false,granted=false;
    unsigned next=0,children=0;
    std::uint64_t childGeneration=0;
    std::function<bool()> retiredChild;
    Operation(Target value,Plan kind):target(std::move(value)),plan(kind),shellActivation(false){}
private:
    struct ShellTag {};
    friend class Coordinator;
public:
    // Opaque explicit Shell intent: no live-client identity or action grant.
    explicit Operation(ShellTag):target{},plan(Plan::Single),shellActivation(true){}
};
// A wake is only a hint to recheck this exact intent. Implementations must
// signal a retained resource only, return promptly, and grant no authority.
class AvailabilitySignal {
public:
    virtual ~AvailabilitySignal()=default;
    virtual void notify() noexcept=0;
};
struct IntentState {
    const std::shared_ptr<Operation> operation;
    const std::shared_ptr<AvailabilitySignal> signal;
    bool requested=false,cancelled=false;
    IntentState(std::shared_ptr<Operation> value,std::shared_ptr<AvailabilitySignal> wake)
        :operation(std::move(value)),signal(std::move(wake)){}
};
// Call send only after the State lock is gone. The strong request keeps the
// signal resource alive through both signaling and the final capture release.
struct AvailabilityNotice {
    std::shared_ptr<IntentState> intent;
    void send() noexcept {
        auto held=std::move(intent);
        if(held&&held->signal)held->signal->notify();
    }
};
struct State {
    std::mutex mutex;
    std::shared_ptr<Operation> active;
    std::weak_ptr<IntentState> waiting; // One requested promotion; passive observation never occupies it.
    bool stopped=false,repairQueued=false;
};
class Ticket {
    friend class Coordinator;
    friend class Intent;
    std::shared_ptr<State> state_;
    std::shared_ptr<Operation> operation_;
    Ticket(std::shared_ptr<State> state,std::shared_ptr<Operation> operation)
        :state_(std::move(state)),operation_(std::move(operation)){}
    static void retire(State& state,const std::shared_ptr<Operation>& operation,AvailabilityNotice& notice)noexcept{
        if(state.active!=operation||operation->parent||operation->entered||operation->granted||operation->children||operation->retiredChild)return;
        state.active.reset();
        // Promotion shares the exact same lock/real retirement boundary as
        // reserve. A new ordinary request cannot steal an already promoted slot.
        notice.intent=state.waiting.lock();state.waiting.reset();
        if(notice.intent&&!state.stopped&&!notice.intent->cancelled&&
            notice.intent->operation->parent&&!notice.intent->operation->sealed)
            state.active=notice.intent->operation;
    }
    bool matchesLocked()const noexcept{return state_->active==operation_;}
    bool allowedLocked(Effect effect,unsigned stage)const noexcept{
        if(!matchesLocked()||state_->stopped||operation_->sealed||operation_->entered||operation_->granted||stage!=operation_->next)return false;
        if(operation_->shellActivation)return stage==0&&effect==Effect::Mutation;
        if(operation_->plan==Plan::Reopen)return stage<=1&&(stage==0?effect==Effect::Desktop:effect==Effect::BigPicture);
        return stage==0;
    }
public:
    Ticket()=default;
    explicit operator bool()const noexcept{return state_&&operation_;}
    bool same(const Ticket& other)const noexcept{return state_==other.state_&&operation_==other.operation_;}
    bool held()const noexcept{
        if(!*this)return false;
        std::lock_guard lock(state_->mutex);return matchesLocked()&&!state_->stopped;
    }
    bool current()const noexcept{
        if(!*this)return false;
        std::lock_guard lock(state_->mutex);
        return matchesLocked()&&!state_->stopped&&!operation_->sealed;
    }
    bool target(const Target& expected)const noexcept{return *this&&!operation_->shellActivation&&operation_->target==expected;}
    bool enter(Effect effect,unsigned stage)const noexcept{
        if(!*this)return false;
        std::lock_guard lock(state_->mutex);
        if(!allowedLocked(effect,stage))return false;
        operation_->entered=true;
        return true;
    }
    // A parent grant may already be delivered even when its write reports an
    // error. Keep this phase occupied until a typed return or exact child proof.
    bool grant(Effect effect,unsigned stage)const noexcept{
        if(!*this)return false;
        std::lock_guard lock(state_->mutex);
        if(!allowedLocked(effect,stage))return false;
        operation_->granted=true;
        return true;
    }
    bool returned(unsigned stage,bool accepted)const noexcept{
        if(!*this)return false;
        AvailabilityNotice notice;
        {
            std::lock_guard lock(state_->mutex);
            if(!matchesLocked()||stage!=operation_->next||(!operation_->entered&&!operation_->granted))return false;
            operation_->entered=false;operation_->granted=false;++operation_->next;
            if(!accepted||operation_->plan==Plan::Single||operation_->next>=2)operation_->sealed=true;
            retire(*state_,operation_,notice);
        }
        notice.send();return true;
    }
    void seal()const noexcept{
        if(!*this)return;
        std::lock_guard lock(state_->mutex);if(matchesLocked())operation_->sealed=true;
    }
    bool attachChild(std::uint64_t generation)const noexcept{
        if(!*this||!generation)return false;
        std::lock_guard lock(state_->mutex);
        if(!matchesLocked()||state_->stopped||operation_->sealed||operation_->shellActivation||operation_->children||operation_->retiredChild)return false;
        operation_->children=1;operation_->childGeneration=generation;return true;
    }
    // Called after the original Owner has completed destruction/retention. A
    // false proof stores an accurate old-child repair, never a timeout release.
    void childRetired(std::uint64_t generation,bool destroyed,std::function<bool()> repair={})const {
        if(!*this)return;
        AvailabilityNotice notice;
        {
            std::lock_guard lock(state_->mutex);
            if(!matchesLocked()||operation_->children!=1||operation_->childGeneration!=generation)return;
            operation_->children=0;
            if(destroyed){operation_->entered=false;operation_->granted=false;}
            else operation_->retiredChild=std::move(repair);
            if(!destroyed&&!operation_->retiredChild){
                // No proof/repair route is available: retain an explicit permanent
                // logical debt rather than clearing a possibly delivered grant.
                operation_->children=1;
            }
            retire(*state_,operation_,notice);
        }
        notice.send();
    }
    void parentReturned()const noexcept{
        if(!*this)return;
        AvailabilityNotice notice;
        {
            std::lock_guard lock(state_->mutex);
            if(!matchesLocked())return;
            operation_->parent=false;operation_->sealed=true;retire(*state_,operation_,notice);
        }
        notice.send();
    }
    unsigned nextStage()const noexcept{
        if(!*this)return 0;
        std::lock_guard lock(state_->mutex);return operation_->next;
    }
};
enum class IntentStatus : std::uint8_t { Unavailable, Observing, Waiting, Ready, Busy, Revoked };
// One exact request, independent of TLS and of other requests' tickets. This
// object holds coordination only; the caller retains identity/current/budgets.
class Intent final {
    friend class Coordinator;
    const std::shared_ptr<State> state_;
    const std::shared_ptr<IntentState> value_;
    Intent(std::shared_ptr<State> state,std::shared_ptr<IntentState> value)
        :state_(std::move(state)),value_(std::move(value)){}
public:
    Intent(const Intent&)=delete;
    ~Intent(){cancel();}
    bool matches(const Target& target)const noexcept{return value_&&value_->operation->target==target;}
    IntentStatus status()const noexcept {
        if(!state_||!value_)return IntentStatus::Unavailable;
        std::lock_guard lock(state_->mutex);
        if(state_->stopped||value_->cancelled)return IntentStatus::Revoked;
        if(state_->active==value_->operation)return IntentStatus::Ready;
        return value_->requested?IntentStatus::Waiting:IntentStatus::Observing;
    }
    // The caller clears its own wake before this call, never afterwards. A
    // retirement before/during/after registration cannot lose availability:
    // this either sees idle, owns the slot, or registers under the same mutex.
    IntentStatus promote()noexcept {
        if(!state_||!value_)return IntentStatus::Unavailable;
        std::shared_ptr<IntentState> previous; // Last signal owner drops after lock.
        std::lock_guard lock(state_->mutex);
        if(state_->stopped||value_->cancelled||!value_->operation->parent||value_->operation->sealed)return IntentStatus::Revoked;
        if(state_->active==value_->operation)return IntentStatus::Ready;
        if(!state_->active){value_->requested=true;state_->active=value_->operation;return IntentStatus::Ready;}
        previous=state_->waiting.lock();
        if(previous&&previous!=value_&&!previous->cancelled)return IntentStatus::Busy;
        value_->requested=true;state_->waiting=value_;return IntentStatus::Waiting;
    }
    Ticket ticket()const noexcept {
        if(!state_||!value_)return {};
        std::lock_guard lock(state_->mutex);
        return !state_->stopped&&!value_->cancelled&&state_->active==value_->operation
            ?Ticket(state_,value_->operation):Ticket{};
    }
    void cancel()noexcept {
        if(!state_||!value_)return;
        AvailabilityNotice next;
        std::shared_ptr<IntentState> previous;
        bool signal=false;
        {
            std::lock_guard lock(state_->mutex);
            if(value_->cancelled)return;
            value_->cancelled=true;signal=true;
            value_->operation->parent=false;value_->operation->sealed=true;
            previous=state_->waiting.lock();
            if(previous==value_)state_->waiting.reset();
            Ticket::retire(*state_,value_->operation,next);
        }
        if(signal&&value_->signal)value_->signal->notify();
        next.send();
    }
};
class Coordinator {
    const std::shared_ptr<State> state_=std::make_shared<State>();
public:
    using Post=std::function<bool(std::function<void()>,std::function<void()>)>;
private:
    Post recoveryPost_;
public:
    void configureRecoveryPost(Post post){recoveryPost_=std::move(post);} // composition-root initialization only
    Ticket reserve(Target target,Plan plan){
        if(!target.valid())return {};
        // Allocate the candidate before publishing it or holding the state lock.
        auto candidate=std::make_shared<Operation>(std::move(target),plan);
        std::unique_lock lock(state_->mutex,std::try_to_lock);
        if(!lock.owns_lock()||state_->stopped||state_->active)return {};
        state_->active=std::move(candidate);
        return Ticket(state_,state_->active);
    }
    Ticket reserveShellActivation(){
        // The composition root retains original explicit Shell/source/power
        // authority. This only shares the same global command retirement slot.
        auto candidate=std::make_shared<Operation>(Operation::ShellTag{});
        std::unique_lock lock(state_->mutex,std::try_to_lock);
        if(!lock.owns_lock()||state_->stopped||state_->active)return {};
        state_->active=std::move(candidate);
        return Ticket(state_,state_->active);
    }
    std::shared_ptr<Intent> prepareIntent(Target target,std::shared_ptr<AvailabilitySignal> signal){
        if(!target.valid()||!signal)return {};
        // All allocation precedes the original state lock and any admission.
        auto operation=std::make_shared<Operation>(std::move(target),Plan::Single);
        auto value=std::make_shared<IntentState>(std::move(operation),std::move(signal));
        auto intent=std::shared_ptr<Intent>(new Intent(state_,std::move(value)));
        {std::unique_lock lock(state_->mutex,std::try_to_lock);if(!lock.owns_lock()||state_->stopped)return {};}
        return intent; // Passive observation occupies no command slot or wait slot.
    }
    void stop()noexcept{
        AvailabilityNotice notice;
        {
            std::lock_guard lock(state_->mutex);state_->stopped=true;
            if(state_->active)state_->active->sealed=true;
            notice.intent=state_->waiting.lock();state_->waiting.reset();
            if(notice.intent)notice.intent->cancelled=true;
        }
        notice.send();
    }
    // Requested only by a subsequent original ingress, on the existing fact
    // executor. No repair or callback runs while the coordinator lock is held.
    void requestRetainedRepair(const Post& post){
        if(!post)return;
        Ticket debt;std::function<bool()> repair;
        {
            std::unique_lock lock(state_->mutex,std::try_to_lock);
            if(!lock.owns_lock()||state_->stopped||state_->repairQueued||!state_->active||state_->active->parent||!state_->active->retiredChild)return;
            debt=Ticket(state_,state_->active);repair=state_->active->retiredChild;state_->repairQueued=true;
        }
        // Capture retirement clears queued even when an admitted source task is
        // canceled before run/done. It grants no action and retains unproved debt.
        const auto finish=[state=state_,debt](bool repaired){
            AvailabilityNotice notice;
            {
                std::lock_guard lock(state->mutex);state->repairQueued=false;
                if(state->active!=debt.operation_||!repaired)return;
                debt.operation_->retiredChild={};debt.operation_->entered=false;debt.operation_->granted=false;
                Ticket::retire(*state,debt.operation_,notice);
            }
            notice.send();
        };
        struct Completion {
            std::function<void(bool)> finish;
            std::atomic<bool> repaired{false};
            explicit Completion(std::function<void(bool)> value):finish(std::move(value)){}
            ~Completion(){finish(repaired.load(std::memory_order_acquire));}
        };
        std::shared_ptr<Completion> completion;
        try{completion=std::make_shared<Completion>(finish);}
        catch(...){std::lock_guard lock(state_->mutex);state_->repairQueued=false;throw;}
        const auto run=[repair=std::move(repair),completion]{try{completion->repaired.store(repair(),std::memory_order_release);}catch(...){}};
        const auto done=[completion]{};
        try{(void)post(run,done);}catch(...){}
    }
    void requestRetainedRepair(){requestRetainedRepair(recoveryPost_);}
};
inline std::atomic<Coordinator*> provider{nullptr};
inline thread_local Ticket active;
inline std::shared_ptr<Intent> prepareIntent(const Target& target,std::shared_ptr<AvailabilitySignal> signal){
    // A nested independent request cannot silently borrow or wait on its own
    // enclosing action. The original explicitly borrowed Scope route remains.
    if(active)return {};
    auto* parent=provider.load(std::memory_order_acquire);
    return parent?parent->prepareIntent(target,std::move(signal)):nullptr;
}
class Reservation {
    Ticket ticket_;
    bool owns_;
public:
    Reservation(Ticket ticket,bool owns):ticket_(std::move(ticket)),owns_(owns){}
    ~Reservation(){if(owns_)ticket_.parentReturned();}
    Reservation(const Reservation&)=delete;
    const Ticket& ticket()const noexcept{return ticket_;}
};
inline std::shared_ptr<Reservation> reserve(const Target& target,Plan plan=Plan::Single,bool borrow=false){
    if(borrow&&active){
        if(!active.target(target)||!active.held())return {};
        return std::make_shared<Reservation>(active,false);
    }
    auto* parent=provider.load(std::memory_order_acquire);if(!parent)return {};
    auto ticket=parent->reserve(target,plan);if(!ticket){parent->requestRetainedRepair();return {};}
    try{return std::make_shared<Reservation>(ticket,true);}
    catch(...){ticket.parentReturned();throw;}
}
class Use {
    Ticket previous_;
public:
    explicit Use(const Ticket& ticket):previous_(active){if(ticket)active=ticket;}
    ~Use(){active=previous_;}
    Use(const Use&)=delete;
};
class Provider {
    Coordinator* value_;
public:
    explicit Provider(Coordinator& value):value_(&value){
        Coordinator* expected=nullptr;
        if(!provider.compare_exchange_strong(expected,value_))value_=nullptr;
    }
    ~Provider(){if(value_){value_->stop();auto* expected=value_;provider.compare_exchange_strong(expected,nullptr);}}
    explicit operator bool()const noexcept{return value_!=nullptr;}
};
// Stack-local logical ticket only. No SourceRead, raw Observer or caller
// predicate is placed in TLS, native callback parents, or a remote lease.
class Scope {
    Ticket ticket_,previous_;
    bool owns_=false;
    Disposition failure_=Disposition::Unavailable;
public:
    explicit Scope(const Target& target,Plan plan=Plan::Single,bool borrow=true):previous_(active){
        if(borrow&&previous_){
            if(previous_.target(target)&&previous_.held())ticket_=previous_;
            else failure_=Disposition::Revoked;
        }else if(auto* parent=provider.load(std::memory_order_acquire)){
            ticket_=parent->reserve(target,plan);owns_=bool(ticket_);
            if(!ticket_){failure_=Disposition::Busy;parent->requestRetainedRepair();}
        }
        if(ticket_)active=ticket_;
    }
    ~Scope(){active=previous_;if(owns_)ticket_.parentReturned();}
    Scope(const Scope&)=delete;
    explicit operator bool()const noexcept{return bool(ticket_);}
    Disposition failure()const noexcept{return failure_;} // Consult only if !*this; reservation never means Submitted.
    const Ticket& ticket()const noexcept{return ticket_;}
};
using Remote=std::function<bool(Effect,unsigned,bool,bool)>;
inline thread_local const Remote* remote=nullptr;
class RemoteScope {
    const Remote callback_;
    const Remote* previous_;
public:
    explicit RemoteScope(Remote callback):callback_(std::move(callback)),previous_(remote){remote=&callback_;}
    ~RemoteScope(){remote=previous_;}
};
// Independent explicit Shell activation only; it never borrows a targeted or
// remote operation and cannot issue target/child proof. Normal Native owns it.
class ShellScope {
    Ticket ticket_,previous_;
    Disposition failure_=Disposition::Unavailable;
public:
    ShellScope():previous_(active){
        if(previous_||remote){failure_=Disposition::Revoked;return;}
        if(auto* parent=provider.load(std::memory_order_acquire)){
            ticket_=parent->reserveShellActivation();
            if(!ticket_){failure_=Disposition::Busy;parent->requestRetainedRepair();}
        }
        if(ticket_)active=ticket_;
    }
    ~ShellScope(){active=previous_;if(ticket_)ticket_.parentReturned();}
    ShellScope(const ShellScope&)=delete;
    explicit operator bool()const noexcept{return bool(ticket_);}
    Disposition failure()const noexcept{return failure_;}
    const Ticket& ticket()const noexcept{return ticket_;}
};
// Enter immediately before the original final admit/Shell/send. The original
// request keeps checking actual process/owner/current after this coordination.
class Dispatch {
    Ticket ticket_;
    const Remote* remote_=nullptr;
    Effect effect_;
    unsigned stage_=0;
    bool entered_=false,returned_=false;
public:
    explicit Dispatch(Effect effect):ticket_(active),remote_(remote),effect_(effect){
        if(remote_){try{entered_=(*remote_)(effect_,0,false,false);}catch(...){}}
        else if(ticket_){stage_=ticket_.nextStage();entered_=ticket_.enter(effect_,stage_);}
    }
    ~Dispatch(){if(entered_&&!returned_)complete(false);}
    explicit operator bool()const noexcept{return entered_;}
    void complete(bool accepted)noexcept{
        if(!entered_||returned_)return;returned_=true;
        if(remote_){try{(void)(*remote_)(effect_,0,true,accepted);}catch(...){}}
        else (void)ticket_.returned(stage_,accepted);
    }
};
}
