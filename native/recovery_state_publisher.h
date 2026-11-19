#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace ymcc::recovery {
// Owns only the existing recovery publisher's resources, never power authority.
class StatePublisher final {
    struct Run final {
        HANDLE mapping=nullptr, stopEvent=nullptr;
        void* view=nullptr;
        void (*publish)(void*)=nullptr;
        DWORD heartbeat=0;
        bool (*attach)(void*) noexcept=nullptr;
        void (*retire)(void*) noexcept=nullptr;
        std::atomic<bool> retired{false};
        void retireOnce() noexcept {if(!retired.exchange(true,std::memory_order_acq_rel)&&retire)retire(view);}
        std::atomic<bool> stopping{false};
        std::mutex publicationMx;
        ~Run() noexcept {
            retireOnce();
            if(view) UnmapViewOfFile(view);
            if(stopEvent) CloseHandle(stopEvent);
            if(mapping) CloseHandle(mapping);
        }
        bool publishOnce() noexcept {
            std::lock_guard<std::mutex> lock(publicationMx);
            if(stopping.load(std::memory_order_acquire)) return false;
            try { publish(view); return true; }
            catch(...) {
                return false; // A later existing heartbeat may retry a transient observation failure.
            }
        }
    };
    mutable std::mutex lifecycleMx;
    std::shared_ptr<Run> current;
    std::thread worker;
    bool reap(DWORD wait) {
        if(!worker.joinable()) { current.reset(); return true; }
        const auto handle=reinterpret_cast<HANDLE>(worker.native_handle());
        if(!handle || WaitForSingleObject(handle,wait)!=WAIT_OBJECT_0) return false;
        worker.join();
        current.reset();
        return true;
    }
public:
    StatePublisher()=default;
    StatePublisher(const StatePublisher&)=delete;
    StatePublisher& operator=(const StatePublisher&)=delete;
    ~StatePublisher() noexcept {
        (void)stop(0); // Do not repeat the main exit path's bounded wait.
        // Process exit only: the detached worker's shared Run retains every
        // resource it may still access. It never dereferences this owner.
        if(worker.joinable()) worker.detach();
    }
    bool start(const wchar_t* name,DWORD bytes,DWORD heartbeat,void (*publish)(void*),bool (*attach)(void*) noexcept=nullptr,void (*retire)(void*) noexcept=nullptr) noexcept {
        std::lock_guard<std::mutex> lock(lifecycleMx);
        if(current && !current->stopping.load(std::memory_order_acquire)) return true;
        if(!reap(0) || !name || !bytes || !heartbeat || !publish) return false;
        try {
            auto run=std::make_shared<Run>();
            run->publish=publish;run->heartbeat=heartbeat;run->attach=attach;run->retire=retire;
            run->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,bytes,name);
            if(!run->mapping) return false;
            run->view=MapViewOfFile(run->mapping,FILE_MAP_ALL_ACCESS,0,0,bytes);
            if(!run->view) return false;
            run->stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(!run->stopEvent) return false;
            ZeroMemory(run->view,bytes);
            if(run->attach&&!run->attach(run->view))return false;
            if(!run->publishOnce()) return false;
            std::thread next([run] {
                for(;;) {
                    (void)run->publishOnce();
                    if(run->stopping.load(std::memory_order_acquire)) break;
                    if(WaitForSingleObject(run->stopEvent,run->heartbeat)!=WAIT_TIMEOUT) break;
                }
                run->stopping.store(true,std::memory_order_release);
            });
            worker=std::move(next);
            current=std::move(run);
            return true;
        } catch(...) { return false; } // Includes a failed std::thread construction.
    }
    bool stop(DWORD wait) noexcept {
        std::lock_guard<std::mutex> lock(lifecycleMx);
        if(current) {
            current->stopping.store(true,std::memory_order_release);
            current->retireOnce();
            SetEvent(current->stopEvent);
        }
        return reap(wait); // Timeout retains both the joinable thread and its Run.
    }
    void publishOnce() noexcept {
        std::shared_ptr<Run> run;
        { std::lock_guard<std::mutex> lock(lifecycleMx);run=current; }
        if(run) (void)run->publishOnce();
    }
    bool running() const noexcept {
        std::lock_guard<std::mutex> lock(lifecycleMx);
        return current && !current->stopping.load(std::memory_order_acquire);
    }
    void* view() const noexcept {
        std::lock_guard<std::mutex> lock(lifecycleMx);
        return current?current->view:nullptr;
    }
};
}
