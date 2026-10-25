#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ymcc {
using ShortcutActionJson = nlohmann::json;
inline bool shortcutUiAction(const std::string& id) {
    return id=="os.switchProgram" || id=="monitor.toggle" || id=="display.frameRateControl" || id=="fan.switchPreset" || id=="app.launchQuickApp";
}
inline bool shortcutUiParams(const std::string& id,const ShortcutActionJson& raw,ShortcutActionJson& result) {
    if(!shortcutUiAction(id)||!raw.is_object())return false;
    const auto text=[&](const char* key,const char* fallback)->std::string {
        const auto at=raw.find(key);return at==raw.end()?std::string(fallback):at->is_string()?at->get<std::string>():std::string();
    };
    result=ShortcutActionJson::object();
    if(id=="os.switchProgram")return true;
    if(id=="monitor.toggle") {
        const auto operation=text("operation","toggle");if(operation!="toggle"&&operation!="show"&&operation!="hide")return false;
        result["operation"]=operation;return true;
    }
    if(id=="display.frameRateControl") {
        const auto operation=text("operation","toggle"),fps=text("fps","60");
        static const std::vector<std::string> choices={"15","20","25","30","40","45","50","60","72","90","120","144","165","200","240","300"};
        if((operation!="toggle"&&operation!="set")||std::find(choices.begin(),choices.end(),fps)==choices.end())return false;
        result={{"operation",operation},{"fps",fps}};return true;
    }
    if(id=="fan.switchPreset") {
        const auto preset=text("preset","next");if(preset!="next"&&preset!="soft"&&preset!="balanced"&&preset!="aggressive")return false;
        result["preset"]=preset;return true;
    }
    const auto app=text("appPath","");if(app.empty()||app.size()>8192)return false;
    for(unsigned char c:app)if(c<32)return false;
    result["appPath"]=app;return true;
}
inline bool shortcutFeedbackState(const std::string& state) {
    return state=="applied"||state=="saved"||state=="pending"||state=="failed";
}
inline bool shortcutFeedbackText(const ShortcutActionJson& value,size_t max) {
    if(!value.is_string())return false;const auto text=value.get<std::string>();if(text.empty()||text.size()>max)return false;
    for(unsigned char c:text)if(c<32)return false;return true;
}

// At most 16 pending one-shot requests. No worker, hook or timer. Only the
// original serial dispatcher issues requests; claims and results recheck its guard.
class ShortcutActionMailbox {
    struct Entry { ShortcutActionJson wire; uint64_t deadline=0; std::function<bool()> current; bool claimed=false; };
    std::mutex mutex_;
    std::unordered_map<std::string,Entry> pending_;
    uint64_t sequence_=0;
    std::string session_;
public:
    explicit ShortcutActionMailbox(std::string session):session_(std::move(session)){}
private:
    ShortcutActionJson issueNormalized(const std::string& action,ShortcutActionJson params,uint64_t generation,uint64_t revision,uint64_t tick,int64_t unixMs,std::function<bool()> current) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto it=pending_.begin();it!=pending_.end();)if(it->second.deadline<=tick)it=pending_.erase(it);else ++it;
        if(pending_.size()>=16)return {};
        const std::string id=session_+":"+std::to_string(++sequence_);
        ShortcutActionJson wire={{"schemaVersion",1},{"id",id},{"actionId",action},{"params",std::move(params)},
            {"powerGeneration",generation},{"inputRevision",revision},{"issuedAtUnixMs",unixMs},{"expiresAtUnixMs",unixMs+30000}};
        pending_.emplace(id,Entry{wire,tick+30000,std::move(current),false});return wire;
    }
public:
    ShortcutActionJson issue(const std::string& action,const ShortcutActionJson& raw,uint64_t generation,uint64_t revision,uint64_t tick,int64_t unixMs,std::function<bool()> current) {
        ShortcutActionJson params;if(!shortcutUiParams(action,raw,params))return {};
        return issueNormalized(action,std::move(params),generation,revision,tick,unixMs,std::move(current));
    }
    ShortcutActionJson issueResultTicket(const std::string& action,uint64_t generation,uint64_t revision,uint64_t tick,int64_t unixMs,std::function<bool()> current) {
        if(action!="power.tdpAdjust")return {};
        return issueNormalized(action,ShortcutActionJson::object(),generation,revision,tick,unixMs,std::move(current));
    }
    ShortcutActionJson claim(const std::string& id,uint64_t tick) {
        std::function<bool()> current;
        {std::lock_guard<std::mutex> lock(mutex_);const auto at=pending_.find(id);if(at==pending_.end()||at->second.deadline<=tick)return {};current=at->second.current;}
        if(!current||!current())return {};
        std::lock_guard<std::mutex> lock(mutex_);const auto at=pending_.find(id);if(at==pending_.end()||at->second.deadline<=tick)return {};
        at->second.claimed=true;return at->second.wire;
    }
    ShortcutActionJson complete(const std::string& id,const ShortcutActionJson& outcome,uint64_t tick,int64_t unixMs) {
        if(!outcome.is_object()||!outcome.contains("state")||!outcome["state"].is_string()||!shortcutFeedbackState(outcome["state"].get<std::string>())||
            !outcome.contains("title")||!shortcutFeedbackText(outcome["title"],192)||!outcome.contains("message")||!shortcutFeedbackText(outcome["message"],768))return {};
        Entry entry;
        {std::lock_guard<std::mutex> lock(mutex_);const auto at=pending_.find(id);if(at==pending_.end()||!at->second.claimed||at->second.deadline<=tick)return {};
            entry=at->second;pending_.erase(at);}
        if(!entry.current||!entry.current())return {};
        const auto state=outcome["state"].get<std::string>();
        ShortcutActionJson feedback={{"schemaVersion",1},{"id",id+":result"},{"actionId",entry.wire["actionId"]},{"origin","controller-shortcut"},
            {"state",state},{"title",outcome["title"]},{"message",outcome["message"]},{"occurredAtUnixMs",unixMs},
            {"expiresAtUnixMs",unixMs+(state=="failed"?5000:2500)},{"powerGeneration",entry.wire["powerGeneration"]},
            {"presentation",{{"fullscreen",true},{"leftMonitor",true}}}};
        if(outcome.contains("value")&&outcome["value"].is_object()&&outcome["value"].contains("text")&&shortcutFeedbackText(outcome["value"]["text"],288)) {
            const auto& raw=outcome["value"];ShortcutActionJson value={{"text",raw["text"]}};
            if(raw.contains("number")&&raw["number"].is_number())value["number"]=raw["number"];
            if(raw.contains("unit")&&shortcutFeedbackText(raw["unit"],48))value["unit"]=raw["unit"];
            feedback["value"]=std::move(value);
        }
        return feedback;
    }
    size_t size(){std::lock_guard<std::mutex> lock(mutex_);return pending_.size();}
};
}
