#pragma once
#include <cstdint>
namespace ymcc::msi {
// An INPUTLOST episode may reacquire the SAME verified binding. It never owns
// output permission: only a new valid sample can republish that permission.
struct AcquireRecovery {
    static constexpr unsigned budget=8;
    static constexpr std::uint64_t intervalMs=250;
    bool active=false;unsigned attempts=0;std::uint64_t nextDue=0,power=0,topology=0,mode=0;
    void begin(std::uint64_t now,std::uint64_t generation,std::uint64_t tree=0,std::uint64_t transaction=0)noexcept {
        if(active)return;active=true;attempts=0;nextDue=now;power=generation;topology=tree;mode=transaction;
    }
    bool stale(std::uint64_t generation,std::uint64_t tree=0,std::uint64_t transaction=0)const noexcept{
        return active&&(power!=generation||topology!=tree||mode!=transaction);}
    bool exhausted()const noexcept{return active&&attempts>=budget;}
    bool due(std::uint64_t now,std::uint64_t generation,std::uint64_t tree=0,std::uint64_t transaction=0)const noexcept {
        return active&&!stale(generation,tree,transaction)&&!exhausted()&&now>=nextDue;
    }
    void attempted(std::uint64_t now)noexcept{++attempts;nextDue=now+intervalMs;}
    void reset()noexcept{*this={};}
};
} // namespace ymcc::msi
