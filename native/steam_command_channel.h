#pragma once
// Typed action rendezvous over the already authenticated single-reader pipe.
// It has no executable/URL/target input and does not consume business authority.
#include "decky_listener_proxy.h"
#include "steam_command_coordinator.h"
namespace ymcc::steamcommand {
class ChildServer {
    std::string id_,generation_;
    std::shared_ptr<deckybinding::listenerproxy::Sequence> sequence_;
    Ticket ticket_;
    Plan plan_;
public:
    ChildServer(std::string id,std::string generation,std::shared_ptr<deckybinding::listenerproxy::Sequence> sequence,Ticket ticket,Plan plan)
        :id_(std::move(id)),generation_(std::move(generation)),sequence_(std::move(sequence)),ticket_(std::move(ticket)),plan_(plan){}
    static bool isQuery(const nlohmann::json& value){return value.is_object()&&value.value("schema",std::string{})==deckybinding::listenerproxy::commandSchema;}
    nlohmann::json serve(const nlohmann::json& query,const std::function<bool()>& current,ULONGLONG end){
        namespace proxy=deckybinding::listenerproxy;
        proxy::commandEnvelope(query,id_,generation_,"query");const auto& d=query.at("detail");
        const auto sequence=proxy::decimal(d.at("sequence"));sequence_->take(sequence);
        const auto deadline=(std::min)(proxy::decimal(d.at("deadline")),end);
        proxy::require(d.at("stage").is_number_unsigned()&&d.at("stage").get<unsigned long long>()<=1&&
            d.at("effect").is_number_unsigned()&&d.at("effect").get<unsigned long long>()<=static_cast<unsigned>(Effect::Mutation));
        const auto stage=d.at("stage").get<unsigned>();const auto effect=static_cast<Effect>(d.at("effect").get<unsigned>());
        const auto phase=d.at("phase").get<std::string>();const bool returned=phase=="returned";
        proxy::require((returned&&d.size()==6&&d.at("accepted").is_boolean())||(!returned&&phase=="enter"&&d.size()==5));
        const bool allowed=plan_==Plan::Reopen?(stage==0?effect==Effect::Desktop:effect==Effect::BigPicture):stage==0&&effect==Effect::Mutation;
        bool ok=false;
        if(ticket_&&allowed&&proxy::left(deadline)){
            if(returned)ok=ticket_.returned(stage,d.at("accepted").get<bool>());
            else if(current&&current()&&ticket_.current())ok=ticket_.grant(effect,stage);
        }
        return proxy::commandPacket(id_,generation_,"reply",{{"sequence",std::to_string(sequence)},{"stage",stage},{"ok",ok}});
    }
};
// Declared before the real Owner so retirement runs after Owner's destructor has
// either returned resources or published the exact retained generation.
class ChildRetirement {
    Ticket ticket_;
    std::uint64_t generation_=0;
    bool attached_=false,destroyed_=false;
public:
    explicit ChildRetirement(Ticket ticket):ticket_(std::move(ticket)){}
    bool attach(std::uint64_t generation){
        generation_=generation;
        if(!ticket_)return true;
        attached_=ticket_.attachChild(generation);return attached_;
    }
    void confirmed(bool destroyed)noexcept{destroyed_=destroyed;}
    ~ChildRetirement(){
        if(!attached_)return;
        try{
            if(destroyed_)ticket_.childRetired(generation_,true);
            else ticket_.childRetired(generation_,false,[generation=generation_]{
                // Accurate preexisting retained states only, one original
                // zero-wait pass on demand via the existing fact executor.
                deckybinding::Owner::retryRetainedCleanup(0);
                const auto retained=deckybinding::Owner::retainedFailures();
                return std::none_of(retained.begin(),retained.end(),[&](const auto& state){return state.generation==generation;});
            });
        }catch(...){try{ticket_.childRetired(generation_,false);}catch(...){}}
    }
};
}
