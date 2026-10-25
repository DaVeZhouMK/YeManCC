#pragma once
// Native MSI XInput rear compatibility only. SD/managed-DInput code is not a
// consumer of this plan. At most three source-wait attempts per eligible entry.
#include <cstdint>
#include <mutex>
#include <string>
namespace ymcc::msi::nativeRear {
// 2026-10-12 14:33 actual G3E failure: the ordinary writer did not produce
// observed rear keys. Keep unverified persistent writes OUT of default builds.
#ifdef YMCC_MSI_NATIVE_REAR_KEYBOARD_EXPERIMENT
inline constexpr bool buildWritesEnabled=true;
#else
inline constexpr bool buildWritesEnabled=false;
#endif
struct Token {uint64_t epoch=0,power=0;};
struct Stamp {Token token{};bool written=false;};
struct Facts {bool knownBoard=false,requested=false,powerReady=false,exit=false,captureStopped=false,
    virtualEnabled=false,managedDinput=false,returnOwed=false,keyboardHookActive=false,msiProfile=false;uint64_t power=0;};
inline bool allowed(const Facts& f){return f.knownBoard&&f.requested&&f.powerReady&&!f.exit&&!f.captureStopped&&
    !f.virtualEnabled&&!f.managedDinput&&!f.returnOwed&&f.keyboardHookActive&&f.msiProfile&&f.power!=0;}
inline bool writeAdmissionAllowed(const Facts& f,bool enabled=buildWritesEnabled){return enabled&&allowed(f);}
enum class Result { Prepared, SourceWait, Failed, Cancelled };
class Plan {
    std::mutex mx_;bool active_=false,queued_=false,terminal_=false,written_=false;Token token_{};
    uint64_t deadline_=0,next_=0;unsigned attempts_=0;
public:
    bool request(bool wanted,uint64_t power,uint64_t now,Token& out){
        std::lock_guard<std::mutex> lock(mx_);
        if(!wanted||!power){if(active_){active_=false;queued_=terminal_=written_=false;++token_.epoch;}return false;}
        if(!active_||token_.power!=power){active_=true;queued_=terminal_=written_=false;++token_.epoch;token_.power=power;attempts_=0;next_=now;deadline_=now<=UINT64_MAX-12000?now+12000:now;}
        if(terminal_||queued_||attempts_>=3||now<next_||now>deadline_){if(now>deadline_)terminal_=true;return false;}
        out=token_;queued_=true;++attempts_;return true;
    }
    bool current(Token t,uint64_t now=0){std::lock_guard<std::mutex> lock(mx_);return active_&&queued_&&!terminal_&&t.epoch!=0&&t.epoch==token_.epoch&&t.power==token_.power&&(!now||now<=deadline_);}
    void complete(Token t,Result result,uint64_t now){
        std::lock_guard<std::mutex> lock(mx_);if(!active_||t.epoch!=token_.epoch||t.power!=token_.power)return;
        queued_=false;written_=result==Result::Prepared;if(result==Result::SourceWait&&attempts_<3&&now<=deadline_&&now<=UINT64_MAX-1500)next_=now+1500;else terminal_=true;
    }
    Stamp stamp(){std::lock_guard<std::mutex> lock(mx_);return active_?Stamp{token_,written_}:Stamp{};}
    unsigned attempts(){std::lock_guard<std::mutex> lock(mx_);return attempts_;}
};
inline std::string reason(Token t){return "msi-native-rear-keyboard:"+std::to_string(t.epoch)+":"+std::to_string(t.power);}
inline bool parse(const std::string& s,Token& t){
    static const std::string prefix="msi-native-rear-keyboard:";if(s.rfind(prefix,0)!=0)return false;
    const auto rest=s.substr(prefix.size());const auto colon=rest.find(':');if(colon==std::string::npos||colon==0||colon+1==rest.size())return false;
    const auto a=rest.substr(0,colon),b=rest.substr(colon+1);if(a.size()>20||b.size()>20||a.find_first_not_of("0123456789")!=std::string::npos||b.find_first_not_of("0123456789")!=std::string::npos)return false;
    try{t.epoch=std::stoull(a);t.power=std::stoull(b);}catch(...){return false;}return t.epoch!=0&&t.power!=0;
}
} // namespace ymcc::msi::nativeRear