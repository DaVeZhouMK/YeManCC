#pragma once
// Owner-thread click deadlines; Steam work uses the existing serial worker.
// No waiting on UI/BUS, no timer between gestures, no virtual report mutation.
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
namespace ymcc::qamfollowup {
using Json=nlohmann::json;
inline constexpr UINT_PTR kTimerId=0x594D5141;
inline constexpr ULONGLONG kWindowMs=3000,kIntervalMs=750;
struct Ticket {unsigned long long generation=0;ULONGLONG started=0,startedAt=0,power=0;bool deck=false;std::string id;};
inline std::atomic<unsigned long long> generation{1};
inline std::atomic<bool> pending{false},done{true};
inline std::mutex mutex;
inline Ticket current;
inline HWND timerOwner=nullptr;
inline bool (*admitted)(const Ticket&)=nullptr;
inline bool (*submit)(const Ticket&,const char*,bool)=nullptr;
inline unsigned long long startedCount=0,finishedCount=0; // owner-thread counters
inline ULONGLONG unixMilliseconds() noexcept {FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return (n.QuadPart-116444736000000000ULL)/10000;}
inline Ticket snapshot(){std::lock_guard<std::mutex> lock(mutex);return current;}
inline bool valid(const Ticket& t) {
    const auto now=GetTickCount64();
    return t.generation==generation.load(std::memory_order_acquire)&&!done.load(std::memory_order_acquire)&&
        now>=t.started&&now-t.started<kWindowMs&&admitted&&admitted(t);
}
inline Json request(const Ticket& t){return {{"id",t.id},{"startedAt",t.startedAt}};}
inline void cancel() {
    {std::lock_guard<std::mutex> lock(mutex);++generation;done.store(true,std::memory_order_release);pending.store(false,std::memory_order_release);}
    if(timerOwner)KillTimer(timerOwner,kTimerId);timerOwner=nullptr;
}
inline void abandon(const Ticket& t) {
    std::lock_guard<std::mutex> lock(mutex);
    if(t.generation!=generation.load(std::memory_order_acquire))return;
    done.store(true,std::memory_order_release);pending.store(false,std::memory_order_release);
}
inline void complete(const Ticket& t,const Json& result) {
    std::lock_guard<std::mutex> lock(mutex);
    if(t.generation!=generation.load(std::memory_order_acquire))return;
    // A malformed remote acknowledgment must not throw through the original
    // action's finish() or leave standalonePending latched. Unknown means stop.
    bool keep=false;
    if(result.is_object()) {
        const auto ok=result.find("ok"),recovery=result.find("qamRecovery");
        const auto reason=result.find("reason"),mutated=result.find("mutated");
        if(ok!=result.end()&&ok->is_boolean()) {
            if(!ok->get<bool>()&&recovery==result.end()&&reason!=result.end()&&reason->is_string()&&
               reason->get_ref<const std::string&>()=="live-busy"&&
               mutated!=result.end()&&mutated->is_boolean()&&!mutated->get<bool>())keep=true;
            else if(ok->get<bool>()&&recovery!=result.end()&&recovery->is_object()) {
                const auto terminal=recovery->find("terminal"),status=recovery->find("status");
                if(terminal!=recovery->end()&&terminal->is_boolean()&&!terminal->get<bool>()&&
                   status!=recovery->end()&&status->is_string()) {
                    const auto& value=status->get_ref<const std::string&>();
                    keep=value=="waiting-receiver"||value=="awaiting-menu"||value=="waiting-slot"||
                         value=="steam-suppressed"||value=="waiting-spacing"||value=="supplement-sent";
                }
            }
        }
    }
    if(!keep)done.store(true,std::memory_order_release);
    pending.store(false,std::memory_order_release);
}
inline void launch(const Ticket& t,const char* operation,bool supplement) {
    if(!valid(t))return;
    {std::lock_guard<std::mutex> lock(mutex);
     if(t.generation!=generation.load()||done.load()||pending.load())return;
     pending.store(true,std::memory_order_release);}
    if(!submit||!submit(t,operation,supplement)){
        std::lock_guard<std::mutex> lock(mutex);
        if(t.generation==generation.load())pending.store(false,std::memory_order_release);
    }
}
inline void CALLBACK timerProc(HWND window,UINT,UINT_PTR id,DWORD) {
    if(window!=timerOwner||id!=kTimerId)return;
    const auto t=snapshot();
    if(!valid(t)){cancel();++finishedCount;return;}
    launch(t,"menu.qam.retry",true);
}
inline void begin(HWND owner,bool deck,ULONGLONG power) {
    cancel();Ticket t;t.generation=generation.load();t.started=GetTickCount64();t.startedAt=unixMilliseconds();t.deck=deck;t.power=power;
    t.id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(t.generation)+"-"+std::to_string(t.started);
    {std::lock_guard<std::mutex> lock(mutex);current=t;}
    done.store(false,std::memory_order_release);++startedCount;
    if(!owner||!SetTimer(owner,kTimerId,(UINT)kIntervalMs,timerProc)){cancel();return;}
    timerOwner=owner;
    if(deck)launch(t,"menu.qam.observe",false); // Never send a second normal SD key.
}
} // namespace ymcc::qamfollowup
