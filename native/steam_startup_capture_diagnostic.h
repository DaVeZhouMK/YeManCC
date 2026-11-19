#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
namespace ymcc::steamgamefacts::startupdiagnostic {
enum class Stage:unsigned { Observer,Cache,Main,Parent,Retain,Join,Count };
struct Receipt { const char* first=nullptr;const char* last=nullptr;std::uint64_t count=0,firstTick=0,lastTick=0; };
struct Snapshot { DWORD pid=0;bool present=false,readBusy=false;std::uint64_t evictions=0,dropped=0;std::array<Receipt,static_cast<unsigned>(Stage::Count)> stages{}; };
// Diagnostic PID slots only: no HANDLE, path, birth, GameID or admission right.
// Bounded writes occur only at existing CREATE/fresh-source boundaries. Nothing
// schedules work or writes a log here. Snapshot loss is explicit and harmless.
class Ledger final {
    std::mutex mutex_;std::array<Snapshot,64> slots_{};std::size_t next_=0;std::uint64_t evictions_=0;std::atomic<std::uint64_t> dropped_{0};
public:
    void note(DWORD pid,Stage stage,const char* reason) noexcept {
        const DWORD error=GetLastError();
        try { if(pid&&stage<Stage::Count&&reason){std::unique_lock lock(mutex_,std::try_to_lock);if(lock.owns_lock()){
            Snapshot* selected=nullptr;for(auto& slot:slots_)if(slot.present&&slot.pid==pid){selected=&slot;break;}
            if(!selected){selected=&slots_[next_];next_=(next_+1)%slots_.size();if(selected->present)++evictions_;*selected={};selected->pid=pid;selected->present=true;}
            auto& row=selected->stages[static_cast<unsigned>(stage)];const auto tick=GetTickCount64();if(!row.first){row.first=reason;row.firstTick=tick;}row.last=reason;row.lastTick=tick;if(row.count!=UINT64_MAX)++row.count;
        }else dropped_.fetch_add(1,std::memory_order_relaxed);}}catch(...){dropped_.fetch_add(1,std::memory_order_relaxed);}SetLastError(error);
    }
    Snapshot snapshot(DWORD pid) noexcept {
        const DWORD error=GetLastError();Snapshot result;result.pid=pid;
        try {std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())result.readBusy=true;else{result.evictions=evictions_;for(const auto& slot:slots_)if(slot.present&&slot.pid==pid){result=slot;result.evictions=evictions_;break;}}}catch(...){result.readBusy=true;}
        result.dropped=dropped_.load(std::memory_order_relaxed);SetLastError(error);return result;
    }
};
inline Ledger& ledger() noexcept {static Ledger value;return value;}
struct Scope final {DWORD pid;Stage stage;const char* reason;Scope(DWORD p,Stage s,const char* r)noexcept:pid(p),stage(s),reason(r){}~Scope(){ledger().note(pid,stage,reason);}void reached(const char* value)noexcept{reason=value;}};
}
