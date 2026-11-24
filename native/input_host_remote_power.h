#pragma once
#include <memory>
#include <string>
#include "input_host_shared_power.h"
#include "sleep_power_remote_transport.h"

namespace ymcc::inputHost::sharedPower {
// Original Host transaction owner only. This is a two-entry transport cache
// over the original Scope, not another power authority or recovery budget.
class RemoteOffers final {
    struct Entry {
        std::string run,nonce,header;
        std::uint64_t epoch=0;
        HANDLE originalConsumer=nullptr; // Compared only while the original Host owner holds it.
        sleeppower::remote::ProcessHold peer;
        std::shared_ptr<Scope> source;
        std::shared_ptr<sleeppower::remote::NativeScope> remote;
        bool acknowledged=false;
        ~Entry(){if(remote)remote->cancel();}
    };
    std::unique_ptr<Entry> lifecycle_,frame_;
    static void retire(std::unique_ptr<Entry>& entry,sleeppower::remote::NativeProvider* provider) noexcept {
        if(!entry)return;
        if(entry->remote){
            entry->remote->cancel();
            if(provider)(void)provider->withdrawUnclaimed(entry->remote->binding().scope,entry->peer.get());
        }
        // A claimed importer remains in the original provider ledger and owns
        // its registration until its actual borrowers settle or process exits.
        entry.reset();
    }
public:
    struct Ticket {
        std::shared_ptr<sleeppower::remote::NativeScope> remote;
        sleeppower::remote::Purpose purpose=sleeppower::remote::Purpose::Observation;
    };
    bool prepare(sleeppower::remote::NativeProvider& provider,
                 sleeppower::remote::NativeRemoteTransport& transport,
                 const std::shared_ptr<Scope>& source,HANDLE heldConsumer,
                 const std::string& run,std::uint64_t epoch,const std::string& nonce,
                 sleeppower::remote::Purpose purpose,const std::string*& header,Ticket& ticket) noexcept {
        using namespace sleeppower::remote;
        header=nullptr;ticket={};
        if(!source||!source->current()||!heldConsumer||run.empty()||!epoch||nonce.empty()||!inputPurpose(purpose)||
           (purpose==Purpose::InputFrame&&!inputAdmitted(source->power(),source->keepTarget())))return false;
        auto& slot=purpose==Purpose::InputFrame?frame_:lifecycle_;
        try{
            if(slot&&slot->source==source&&slot->originalConsumer==heldConsumer&&
               slot->run==run&&slot->epoch==epoch&&slot->nonce==nonce&&
               WaitForSingleObject(slot->peer.get(),0)==WAIT_TIMEOUT&&
               WaitForSingleObject(slot->remote->cancellationEvent(),0)==WAIT_TIMEOUT){
                ticket={slot->remote,purpose};if(!slot->acknowledged)header=&slot->header;return true;
            }
            retire(slot,&provider);
            auto next=std::make_unique<Entry>();next->run=run;next->epoch=epoch;next->nonce=nonce;
            next->originalConsumer=heldConsumer;next->source=source;
            if(!next->peer.bind(heldConsumer))return false;
            ProcessIdentityV1 peer;
            if(!processIdentity(next->peer.get(),peer))return false;
            RemoteOfferV1 offer;
            next->remote=transport.capture(next->peer.get(),{epoch,peer.birth100ns},purpose,
                [source,purpose](Purpose requested){return requested==purpose&&source->current()&&
                    (purpose!=Purpose::InputFrame||inputAdmitted(source->power(),source->keepTarget()));},offer,
                [source](HANDLE event)->std::shared_ptr<void>{return source->linkCancellationEvent(event);});
            if(!next->remote)return false;
            if(!encodeRemotePowerOfferHeaderV1(offer,next->header)||!source->current()){
                (void)provider.withdrawUnclaimed(next->remote->binding().scope,next->peer.get());return false;
            }
            ticket={next->remote,purpose};slot=std::move(next);header=&slot->header;return true;
        }catch(...){return false;}
    }
    void accepted(const Ticket& ticket) noexcept {
        auto& slot=ticket.purpose==sleeppower::remote::Purpose::InputFrame?frame_:lifecycle_;
        if(slot&&slot->remote==ticket.remote)slot->acknowledged=true;
    }
    // Exact serial reply consumer only. Never let a late receipt retire the
    // newer offer, cancel the original power source, or forgive claimed debt.
    bool importRejected(const Ticket& ticket,sleeppower::remote::NativeProvider& provider) noexcept {
        if(!ticket.remote||!sleeppower::remote::inputPurpose(ticket.purpose))return false;
        auto& slot=ticket.purpose==sleeppower::remote::Purpose::InputFrame?frame_:lifecycle_;
        if(!slot||slot->remote!=ticket.remote)return false;
        retire(slot,&provider);return true;
    }
    void reset(sleeppower::remote::NativeProvider* provider=nullptr) noexcept {
        retire(lifecycle_,provider);retire(frame_,provider);
    }
};
}
