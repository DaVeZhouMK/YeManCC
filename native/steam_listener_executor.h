#pragma once
#include <windows.h>
#include <functional>
#include <mutex>
#include <cstdint>
#include <utility>

namespace ymcc::steamsession {
// One queued or acquiring task in the main process. No dedicated thread,
// polling, or second acquisition on restart. Calls and destruction are owned
// by Observer's lifecycle; a task must not destroy its own executor.
class ListenerExecutor final {
    std::mutex mutex_, retirement_;
    PTP_WORK work_ = nullptr;
    bool accepting_ = false, busy_ = false, retired_ = false;
    std::uint64_t generation_ = 0, submittedGeneration_ = 0;
    std::function<void()> read_, done_;
    static void CALLBACK invoke(PTP_CALLBACK_INSTANCE instance,void* context,PTP_WORK) noexcept {
        CallbackMayRunLong(instance);
        static_cast<ListenerExecutor*>(context)->run();
    }
    void run() noexcept {
        std::function<void()> read,done;
        std::uint64_t generation = 0;
        bool admitted = false;
        {
            std::lock_guard lock(mutex_);
            generation = submittedGeneration_;
            admitted=accepting_&&generation_==generation;
            read=std::move(read_);
            done=std::move(done_);
        }
        if(admitted&&read)try{read();}catch(...){}
        read={}; // Release acquisition captures before making the slot reusable.
        bool publish = false;
        {
            std::lock_guard lock(mutex_);
            busy_=false;
            publish=accepting_&&generation_==generation;
        }
        // Work::publish independently rechecks deadline/revocation. A callback
        // already admitted here may overlap stop; it must hold no borrowed owner.
        if(publish&&done)try{done();}catch(...){}
    }
public:
    ListenerExecutor()=default;
    ListenerExecutor(const ListenerExecutor&)=delete;
    ListenerExecutor& operator=(const ListenerExecutor&)=delete;
    ~ListenerExecutor(){finishForExit();}
    // Explicit process-exit retirement, called on the original background
    // cleanup owner after consumers stop. This seals restart permanently.
    // No time bound is inferred for an already-running synchronous OS call.
    void finishForExit(){
        std::function<void()> read,done;
        std::lock_guard retirement(retirement_);
        PTP_WORK work=nullptr;
        {
            std::lock_guard lock(mutex_);
            accepting_=false;retired_=true;++generation_;work=work_;
        }
        if(work){
            WaitForThreadpoolWorkCallbacks(work,TRUE);
            CloseThreadpoolWork(work);
        }
        {
            std::lock_guard lock(mutex_);
            work_=nullptr;busy_=false;
            read=std::move(read_);done=std::move(done_);
        }
        // Canceled queued captures are released outside both the source mutex
        // and the native wait, before the exit owner reports completion.
    }
    bool resume(){std::lock_guard lock(mutex_);if(retired_)return false;++generation_;accepting_=true;return true;}
    void suspend(){std::lock_guard lock(mutex_);accepting_=false;++generation_;}
    bool post(std::function<void()> read,std::function<void()> done={}) {
        if(!read)return false;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||!accepting_||retired_||busy_)return false;
        if(!work_){work_=CreateThreadpoolWork(invoke,this,nullptr);if(!work_)return false;}
        read_=std::move(read);done_=std::move(done);busy_=true;submittedGeneration_=generation_;
        SubmitThreadpoolWork(work_);return true;
    }
};
} // namespace ymcc::steamsession
