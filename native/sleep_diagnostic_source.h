#pragma once
#include <string>
namespace ymcc::sleepdiagnostic {
// Mandatory low-cost source preparation. A missing source is UNKNOWN, never sleep success.
struct Journal { bool owned=false; };
enum class Load { Missing, Valid, Invalid };
struct Result { bool ok=false,ready=false,owned=false;std::string error; };
template<class Ports> Result reconcile(Ports& ports,bool wanted) {
    Journal journal;const auto loaded=ports.load(journal);
    if(loaded==Load::Invalid)return {false,false,false,"journal-invalid"};
    bool current=false;
    if(!ports.read(current))return {false,false,journal.owned,"channel-read-failed"};
    if(!ports.allowed(wanted))return {false,current,journal.owned,"request-superseded"};
    if(wanted) {
        if(current)return {true,true,journal.owned,{}};
        const bool created=!journal.owned;
        if(created) { journal.owned=true;if(!ports.save(journal))return {false,false,false,"baseline-write-failed"}; }
        // Query again after durable preparation; do not acquire another owner's intervening ON.
        if(!ports.read(current))return {false,false,true,"prepared-channel-read-failed"};
        if(current&&created) {
            if(!ports.clear())return {false,true,true,"unapplied-baseline-clear-failed"};
            return {true,true,false,{}};
        }
        if(!ports.allowed(wanted))return {false,current,true,"request-superseded"};
        if(!current&&!ports.write(true))return {false,false,true,"channel-enable-failed"};
        if(!ports.read(current)||!current)return {false,false,true,"channel-enable-unconfirmed"};
        return {true,true,true,{}};
    }
    if(!journal.owned)return {true,current,false,{}};
    // Only the false -> true transition journaled by YMCC may be restored.
    if(current) {
        if(!ports.allowed(wanted))return {false,true,true,"request-superseded"};
        if(!ports.write(false))return {false,true,true,"channel-restore-failed"};
        if(!ports.read(current)||current)return {false,current,true,"channel-restore-unconfirmed"};
    }
    if(!ports.clear())return {false,current,true,"restore-journal-clear-failed"};
    return {true,current,false,{}};
}
}
