#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Clock/sender/physical-state ports are supplied by the existing native owner.
// No sleeps or OS calls in this state machine. Only release debts survive cancel.
namespace ymcc::keyboardSequence {
struct Key { unsigned short vk=0; bool extended=false; };
struct Step { std::vector<Key> keys; uint64_t duration=80; };
struct Plan { std::vector<Step> steps; bool toggle=false; unsigned repeats=1; uint64_t gap=150; };
template<class Json,class Resolve> bool parse(const Json& params,const std::string& action,Resolve resolve,Plan& output) {
    Plan plan;
    auto chord=[&](const Json& keys,Step& step) {
        if(!keys.is_array()||keys.empty()||keys.size()>4)return false;
        std::vector<std::string> codes;
        for(const auto& k:keys) {
            if(!k.is_object()||!k.contains("actionId")||!k["actionId"].is_string()||!k.contains("code")||!k["code"].is_string())return false;
            unsigned short vk=0;bool shift=false;const auto id=k["actionId"].template get<std::string>(),code=k["code"].template get<std::string>();
            if(std::find(codes.begin(),codes.end(),code)!=codes.end())return false;codes.push_back(code);
            if(id.rfind("keyboard.",0)!=0||!resolve(id,code,vk,shift))return false;
            auto append=[&](Key key){if(std::none_of(step.keys.begin(),step.keys.end(),[&](Key old){return old.vk==key.vk&&old.extended==key.extended;}))step.keys.push_back(key);};
            if(shift)append({0xA0,false});
            append({vk,code=="NumpadEnter"});
        }
        if(step.keys.size()>4)return false;
        // Ctrl/Shift/Alt/Win precede ordinary keys regardless of selection order.
        const auto modifier=[](Key k){return k.vk==0x10||k.vk==0x11||k.vk==0x12||k.vk==0x5b||k.vk==0x5c||(k.vk>=0xa0&&k.vk<=0xa5);};
        std::stable_partition(step.keys.begin(),step.keys.end(),modifier);return true;
    };
    auto integer=[](const Json& obj,const char* name,uint64_t lo,uint64_t hi,uint64_t& value){
        if(!obj.contains(name)||!obj[name].is_number_integer())return false;
        const auto n=obj[name].template get<int64_t>();if(n<static_cast<int64_t>(lo)||n>static_cast<int64_t>(hi))return false;value=static_cast<uint64_t>(n);return true;
    };
    try {
        if(!params.is_object())return false;
        if(!params.contains("keySequence")) {
            if(!params.contains("key")||!params["key"].is_string())return false;
            Json keys=Json::array({{{"actionId",action},{"code",params["key"]}}});Step step;
            if(!chord(keys,step))return false;plan.steps.push_back(step);
        } else {
            if(!params["keySequence"].is_string()||params["keySequence"].template get_ref<const std::string&>().size()>16384)return false;
            const auto sequence=Json::parse(params["keySequence"].template get<std::string>());
            uint64_t version=0,repeats=0,gap=0;
            if(!integer(sequence,"version",1,1,version)||!integer(sequence,"repeatCount",1,100,repeats)||!integer(sequence,"intervalMs",100,60000,gap)||!sequence.contains("mode")||!sequence["mode"].is_string())return false;
            const auto mode=sequence["mode"].template get<std::string>();if(mode!="once"&&mode!="toggle")return false;
            plan.toggle=mode=="toggle";plan.repeats=static_cast<unsigned>(repeats);plan.gap=gap;
            if(!sequence.contains("steps")||!sequence["steps"].is_array()||sequence["steps"].empty()||sequence["steps"].size()>32)return false;
            bool hasKey=false;
            for(const auto& raw:sequence["steps"]) {
                if(!raw.is_object())return false;Step step;
                if(raw.contains("delayMs")){if(raw.contains("keys")||!integer(raw,"delayMs",10,60000,step.duration))return false;}
                else {if(!raw.contains("keys")||!integer(raw,"holdMs",40,2000,step.duration)||!chord(raw["keys"],step))return false;hasKey=true;}
                plan.steps.push_back(std::move(step));
            }
            if(!hasKey)return false;
        }
    }catch(...){return false;}
    output=std::move(plan);return true;
}
class Engine {
    Plan plan_;std::string id_;size_t step_=0;unsigned cycle_=0;uint64_t due_=0;
    bool active_=false,waiting_=false,releaseBlocked_=false;std::vector<Key> held_;
public:
    bool active()const{return active_;}bool pending()const{return !held_.empty();}
    bool releaseBlocked()const{return releaseBlocked_;}
    const std::string& id()const{return id_;}
    bool start(const std::string& id,const Plan& plan,uint64_t now) {
        if(active_&&id==id_&&plan.toggle){active_=false;due_=now;return true;}
        if(active_||pending()||plan.steps.empty())return false;
        plan_=plan;id_=id;held_.reserve(4);step_=0;cycle_=0;due_=now;active_=true;waiting_=false;releaseBlocked_=false;return true;
    }
    void cancel(uint64_t now){active_=false;due_=now;}
    template<class Send,class IsDown> void tick(uint64_t now,bool allowed,Send send,IsDown isDown) {tick(now,allowed,send,isDown,[now]{return now;});}
    template<class Send,class IsDown,class Clock> void tick(uint64_t now,bool allowed,Send send,IsDown isDown,Clock clock) {
        if(active_&&!allowed)cancel(now);
        if(now<due_)return;
        // Never drop a failed key-up. Retry only these owned releases, no downs.
        if(!held_.empty()) {
            while(!held_.empty()) {if(!send(held_.back(),true)){active_=false;releaseBlocked_=true;due_=clock()+250;return;}held_.pop_back();}
        }
        releaseBlocked_=false;
        if(!active_)return;
        if(waiting_){const bool wasKey=!plan_.steps[step_].keys.empty();waiting_=false;++step_;
            if(wasKey&&step_<plan_.steps.size()&&!plan_.steps[step_].keys.empty()){due_=clock()+40;return;}}
        if(step_==plan_.steps.size()) {
            ++cycle_;
            if(!plan_.toggle&&cycle_>=plan_.repeats){active_=false;return;}
            step_=0;due_=clock()+plan_.gap;return;
        }
        const auto& step=plan_.steps[step_];
        for(const auto key:step.keys) {
            if(isDown(key))continue; // Never claim/release an already-held physical key.
            if(!send(key,false)){cancel(clock());tick(clock(),false,send,isDown,clock);return;}
            held_.push_back(key);
        }
        waiting_=true;due_=clock()+step.duration;
    }
};
} // namespace ymcc::keyboardSequence
