#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <functional>
#include <vector>
// A last-stage, touchpad-owned input apartment. The normal UI-thread path
// remains unchanged. Its native loop sleeps until an input/command arrives.
namespace ymcc::screeninput {
enum class Phase { Down, Move, Up, Cancel };
struct Event { int surface=0;UINT32 id=0;POINTER_INPUT_TYPE type=PT_POINTER;Phase phase=Phase::Cancel;float x=0,y=0;bool cancelled=false;DWORD sourceTime=0;bool timeKnown=false; };
using Sink=void (*)(HWND,const Event&,void*);
inline constexpr UINT kChannelInputMessage=WM_APP+521;
class InputChannel {
    using Controller=winrt::Windows::UI::Core::CoreIndependentInputSourceController;
    using Source=winrt::Windows::UI::Core::CoreIndependentInputSource;
    using Dispatcher=winrt::Windows::UI::Core::CoreDispatcher;
    inline static std::atomic<UINT_PTR> nextCookie_{1};
public:
    struct Input {
        const UINT_PTR cookie=nextCookie_.fetch_add(1);HWND target=nullptr;int surface=0;
        std::atomic<const char*> attachStage{"attach-owned-command"};
        bool attachCommandCompleted=false; // UI-side observation of this command's original wait.
        std::atomic<bool> syncCommandFailed{false}; // Evidence belongs to this exact source.
        std::atomic<bool> admitted{true},ready{false},consumerClosed{false};std::atomic<UINT64> epoch{1};
        // UI reads events, worker alone owns the COM fields below.
        std::mutex mutex;std::array<Event,64> events{};unsigned head=0,count=0;bool posted=false,overflow=false;
        Controller controller{nullptr};Source source{nullptr};std::array<winrt::event_token,4> tokens{};
        winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget desktopTarget{nullptr};
        winrt::Windows::UI::Composition::SpriteVisual ownedVisual{nullptr};
        void fence() noexcept {
            admitted.store(false,std::memory_order_release);std::lock_guard<std::mutex> lock(mutex);++epoch;head=count=0;posted=overflow=false;
        }
        void publish(Event event,UINT64 expectedEpoch) noexcept {
            bool notify=false;{
                std::lock_guard<std::mutex> lock(mutex);if(!admitted.load()||expectedEpoch!=epoch.load())return;
                if(count&&event.phase==Phase::Move){auto& last=events[(head+count-1)%events.size()];if(last.phase==Phase::Move&&last.id==event.id){last=event;return;}}
                if(count==events.size()){head=count=0;overflow=true;}
                if(!overflow){events[(head+count)%events.size()]=event;++count;}
                if(!posted){posted=true;notify=true;}
            }
            // A full posted-message queue must not lose the final UP. Sent
            // notifications are asynchronous and bypass the posted quota.
            // No mailbox lock may be held when the UI can enter its drain.
            if(notify&&admitted.load()&&expectedEpoch==epoch.load()&&!PostMessageW(target,kChannelInputMessage,cookie,0)){
                {std::lock_guard<std::mutex> lock(mutex);if(!admitted.load()||expectedEpoch!=epoch.load())return;overflow=true;head=count=0;}
                if(!SendNotifyMessageW(target,kChannelInputMessage,cookie,0)){
                    ready=false;DWORD_PTR result=0;
                    SendMessageTimeoutW(target,kChannelInputMessage,cookie,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&result);
                }
            }
        }
        bool retire() noexcept {
            // The input apartment can end without a UI-side close. Fence all
            // old edges and use the existing loss notification to release the
            // UI's held gesture before this worker discards its source.
            fence();ready.store(false,std::memory_order_release);
            // An explicit UI close already withdrew this receiver and cancels
            // its contacts. It needs no loss notice on a destroyed child HWND.
            // This says nothing about COM cleanup, which is still verified below.
            if(consumerClosed.load(std::memory_order_acquire))return true;
            {std::lock_guard<std::mutex> lock(mutex);overflow=true;posted=true;}
            if(!target)return consumerClosed.load(std::memory_order_acquire);
            return PostMessageW(target,kChannelInputMessage,cookie,0)!=FALSE||
                SendNotifyMessageW(target,kChannelInputMessage,cookie,0)!=FALSE||
                consumerClosed.load(std::memory_order_acquire);
        }
        unsigned take(std::array<Event,64>& out,bool& lost) noexcept {
            std::lock_guard<std::mutex> lock(mutex);lost=overflow;overflow=false;posted=false;
            const auto n=count;for(unsigned i=0;i<n;++i)out[i]=events[(head+i)%events.size()];head=count=0;return n;
        }
    };
public:
    struct CleanupFailure {HRESULT error=S_OK;const char* stage="none";int surface=-1;bool stopping=false,complete=false;};
private:
    struct Command {
        std::function<void()> run;HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic<bool> cancelled{false};HRESULT error=S_OK;
        explicit Command(std::function<void()> fn):run(std::move(fn)){}~Command(){if(done)CloseHandle(done);}
    };
    struct State {
        HDESK desktop=nullptr;HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic<HWND> window{nullptr};std::atomic<bool> stopping{false},closed{false},startupPublished{false};std::atomic<HRESULT> error{S_OK};
        // One worker publishes the first cleanup failure; the HRESULT release
        // store publishes its fixed stage/surface. No healthy-path polling.
        std::atomic<HRESULT> cleanupError{S_OK};std::atomic<const char*> cleanupStage{"none"};std::atomic<int> cleanupSurface{-1};
        std::mutex mutex;std::array<std::shared_ptr<Command>,16> commands{};
        // Worker-owned slots grow only on attachment; Input objects and their
        // event mailboxes retain stable addresses through vector growth.
        std::vector<std::shared_ptr<Input>> inputs;std::atomic<size_t> inputSlotCount{0};
        winrt::Windows::System::DispatcherQueueController queue{nullptr};
        winrt::Windows::UI::Composition::Compositor compositor{nullptr};Controller seed{nullptr};Dispatcher dispatcher{nullptr};winrt::Windows::Foundation::IAsyncAction queueShutdown{nullptr};
        winrt::Windows::Foundation::AsyncActionCompletedHandler queueCompletion{nullptr};
        ~State(){if(ready)CloseHandle(ready);}
    };
    std::shared_ptr<State> state_;HANDLE thread_=nullptr;DWORD threadId_=0;
    // A live dispatcher can reject attachments while its thread remains healthy.
    // A successful visible owner refresh or actual State retirement settles it.
    std::atomic<HRESULT> attachmentError_{S_OK};
    // UI-owner-only weak evidence, bounded by the same current source slots.
    // Count stays zero on the healthy path; no idle scan or extra source probe.
    std::vector<std::weak_ptr<Input>> failedSyncInputs_;
    size_t failedSyncInputCount_=0;
    void rememberFailedSyncInput(const std::shared_ptr<Input>& input) noexcept {
        size_t reusable=failedSyncInputs_.size();
        for(size_t index=0;index<failedSyncInputCount_;++index){
            const auto previous=failedSyncInputs_[index].lock();
            if(previous==input)return; // Find identity before reusing an earlier slot.
            if(reusable==failedSyncInputs_.size()&&(!previous||previous->consumerClosed.load(std::memory_order_acquire)||
               !previous->syncCommandFailed.load(std::memory_order_acquire)))reusable=index;
        }
        if(reusable<failedSyncInputs_.size())failedSyncInputs_[reusable]=input;
        else if(failedSyncInputCount_<failedSyncInputs_.size())failedSyncInputs_[failedSyncInputCount_++]=input;
    }
    inline static constexpr UINT kCommand=WM_APP+522,kStop=WM_APP+523;
    static HRESULT currentCleanupException() noexcept {
        try{throw;}catch(const winrt::hresult_error& ex){return ex.code();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
    }
    static void rememberCleanupFailure(State& state,HRESULT error,const char* stage,int surface=-1) noexcept {
        if(SUCCEEDED(state.cleanupError.load(std::memory_order_relaxed))){
            state.cleanupStage.store(stage,std::memory_order_relaxed);state.cleanupSurface.store(surface,std::memory_order_relaxed);
            state.cleanupError.store(FAILED(error)?error:E_FAIL,std::memory_order_release);
        }
    }
    static void closeInput(Input& input,const char*& stage) {
        input.admitted.store(false,std::memory_order_release);input.ready.store(false,std::memory_order_release);
        if(input.source){
            stage="source-event-revocation";
            if(input.tokens[0].value)input.source.PointerPressed(input.tokens[0]);
            if(input.tokens[1].value)input.source.PointerMoved(input.tokens[1]);
            if(input.tokens[2].value)input.source.PointerReleased(input.tokens[2]);
            if(input.tokens[3].value)input.source.PointerCaptureLost(input.tokens[3]);
            input.tokens={};stage="source-capture-query";const bool captured=input.source.HasCapture();
            if(captured){stage="source-capture-release";input.source.ReleasePointerCapture();}
        }
        // Close both owned endpoints directly; a closed visual can reject an
        // auxiliary Root(nullptr) before these definitive Close calls run.
        stage="input-controller-close";if(input.controller)input.controller.Close();input.source=nullptr;input.controller=nullptr;
        stage="input-target-close";if(input.desktopTarget)input.desktopTarget.Close();input.desktopTarget=nullptr;input.ownedVisual=nullptr;
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) noexcept {
        if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);return TRUE;}
        auto* state=(State*)GetWindowLongPtrW(h,GWLP_USERDATA);
        if(state&&m==kCommand){
            std::array<std::shared_ptr<Command>,16> commands;{std::lock_guard<std::mutex> lock(state->mutex);commands.swap(state->commands);}
            for(auto& command:commands)if(command){
                if(command->cancelled.load()||state->stopping.load())command->error=E_ABORT;
                else try{command->run();}catch(const winrt::hresult_error& ex){command->error=ex.code();}catch(...){command->error=E_FAIL;}
                SetEvent(command->done);
            }return 0;
        }
        if(state&&m==kStop){state->stopping.store(true);try{state->dispatcher.StopProcessEvents();}catch(...){state->error=E_FAIL;}return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    static DWORD WINAPI entry(void* pointer) noexcept {
        std::unique_ptr<std::shared_ptr<State>> argument((std::shared_ptr<State>*)pointer);const auto state=*argument;argument.reset();bool apartment=false;
        try{
            if(state->desktop&&!SetThreadDesktop(state->desktop))winrt::throw_last_error();
            winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
            // Prepare the only C++ completion delegate before a queue/source
            // exists. Cleanup must not depend on allocating this handler.
            const auto completionThread=GetCurrentThreadId();
            state->queueCompletion=[completionThread](auto const&,auto){PostThreadMessageW(completionThread,WM_APP+524,0,0);};
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
            winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(state->queue)));
            state->compositor=winrt::Windows::UI::Composition::Compositor();auto visual=state->compositor.CreateSpriteVisual();visual.Size({1.f,1.f});
            state->seed=Controller::CreateForVisual(visual);state->seed.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);
            state->seed.IsTransparentForUncontrolledInput(true);state->dispatcher=state->seed.Source().Dispatcher();
            WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=proc;cls.lpszClassName=L"YeManTouchpadInputApartment";
            if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)winrt::throw_last_error();
            HWND h=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,state.get());if(!h)winrt::throw_last_error();state->startupPublished.store(true,std::memory_order_release);state->window=h;SetEvent(state->ready);
            if(!state->stopping.load())state->dispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessUntilQuit);
        }catch(const winrt::hresult_error& ex){state->error=ex.code();}catch(const std::bad_alloc&){state->error=E_OUTOFMEMORY;}catch(...){state->error=E_FAIL;}
        state->stopping.store(true);SetEvent(state->ready);
        bool cleaned=true;
        for(auto& input:state->inputs)if(input){
            if(!input->retire()){
                const DWORD failure=GetLastError();cleaned=false;rememberCleanupFailure(*state,HRESULT_FROM_WIN32(failure?failure:ERROR_INVALID_WINDOW_HANDLE),"source-loss-notification",input->surface);
                if(SUCCEEDED(state->error.load()))state->error=HRESULT_FROM_WIN32(failure?failure:ERROR_INVALID_WINDOW_HANDLE);
            }
            const char* closeStage="source-close";
            bool released=false;for(int attempt=0;attempt<3&&!released;++attempt)try{closeInput(*input,closeStage);released=true;}catch(...){cleaned=false;rememberCleanupFailure(*state,currentCleanupException(),closeStage,input->surface);}
            // Release the final COM references on their original apartment even
            // after an explicit Close error. Keep that failure sticky.
            input->source=nullptr;input->controller=nullptr;input->desktopTarget=nullptr;input->ownedVisual=nullptr;input.reset();
        }
        try{if(state->seed)state->seed.Close();}catch(...){cleaned=false;rememberCleanupFailure(*state,currentCleanupException(),"seed-controller-close");}state->seed=nullptr;state->dispatcher=nullptr;state->compositor=nullptr;
        HWND h=state->window.exchange(nullptr);if(h&&!DestroyWindow(h)){const DWORD failure=GetLastError();cleaned=false;rememberCleanupFailure(*state,HRESULT_FROM_WIN32(failure?failure:ERROR_INVALID_WINDOW_HANDLE),"worker-window-destroy");}
        const char* queueStage="queue-shutdown-start";
        try{if(state->queue){state->queueShutdown=state->queue.ShutdownQueueAsync();
            // Failed completion notification registration does not cancel the
            // original shutdown action. Retain its error and verify that same
            // action on its owning STA before releasing the queue references.
            try{state->queueShutdown.Completed(state->queueCompletion);}
            catch(const winrt::hresult_error& ex){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,ex.code());rememberCleanupFailure(*state,ex.code(),"queue-completion-registration");}
            catch(const std::bad_alloc&){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,E_OUTOFMEMORY);rememberCleanupFailure(*state,E_OUTOFMEMORY,"queue-completion-registration");}
            catch(...){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,E_FAIL);rememberCleanupFailure(*state,E_FAIL,"queue-completion-registration");}
            // The UI join remains bounded. If Windows has not completed its
            // queue shutdown, retain this owning STA rather than falsely
            // certifying retirement or releasing the action on another thread.
            queueStage="queue-shutdown-status";auto shutdownStatus=state->queueShutdown.Status();
            while(shutdownStatus==winrt::Windows::Foundation::AsyncStatus::Started){
                MsgWaitForMultipleObjectsEx(0,nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                shutdownStatus=state->queueShutdown.Status();
            }
            // Completed proves the queue has shut down. A failure reading that
            // terminal result must retain its error without stranding already
            // retired queue references after this owning apartment exits.
            if(shutdownStatus!=winrt::Windows::Foundation::AsyncStatus::Completed){cleaned=false;rememberCleanupFailure(*state,shutdownStatus==winrt::Windows::Foundation::AsyncStatus::Canceled?E_ABORT:E_FAIL,"queue-not-completed");}
            else try{state->queueShutdown.GetResults();}
            catch(const winrt::hresult_error& ex){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,ex.code());rememberCleanupFailure(*state,ex.code(),"queue-results");}
            catch(const std::bad_alloc&){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,E_OUTOFMEMORY);rememberCleanupFailure(*state,E_OUTOFMEMORY,"queue-results");}
            catch(...){HRESULT expected=S_OK;state->error.compare_exchange_strong(expected,E_FAIL);rememberCleanupFailure(*state,E_FAIL,"queue-results");}
            state->queueShutdown=nullptr;state->queue=nullptr;}}
        catch(...){cleaned=false;rememberCleanupFailure(*state,currentCleanupException(),queueStage);}
        state->queueCompletion=nullptr; // Release this owned delegate reference on its original STA.
        {std::lock_guard<std::mutex> lock(state->mutex);for(auto& command:state->commands)if(command){command->error=E_ABORT;SetEvent(command->done);command.reset();}}
        state->closed.store(cleaned);if(apartment)winrt::uninit_apartment();return 0;
    }
    bool notify(UINT message,HRESULT& error) noexcept {
        const HWND target=state_?state_->window.load():nullptr;
        if(!target){error=RO_E_CLOSED;return false;}
        if(PostMessageW(target,message,0,0)||SendNotifyMessageW(target,message,0,0))return true;
        const DWORD failure=GetLastError();error=HRESULT_FROM_WIN32(failure?failure:ERROR_INVALID_WINDOW_HANDLE);return false;
    }
    void requestStop() noexcept {
        if(!state_)return;state_->stopping=true;HRESULT error=S_OK;
        // No HWND means startup has not published one or cleanup owns it.
        // Never post a thread message accidentally through a null HWND.
        if(state_->window.load()&&!notify(kStop,error))state_->error=error;
    }
    bool invoke(std::function<void()> action,HRESULT& error,bool* completed=nullptr) noexcept {
        if(completed)*completed=false;
        try{
            if(!state_||state_->stopping.load()||!thread_||WaitForSingleObject(thread_,0)!=WAIT_TIMEOUT){error=RO_E_CLOSED;return false;}
            auto command=std::make_shared<Command>(std::move(action));if(!command->done){error=HRESULT_FROM_WIN32(GetLastError());return false;}
            bool inserted=false;{std::lock_guard<std::mutex> lock(state_->mutex);for(auto& slot:state_->commands)if(!slot){slot=command;inserted=true;break;}}
            if(!inserted){error=HRESULT_FROM_WIN32(ERROR_BUSY);return false;}
            if(!notify(kCommand,error)){command->cancelled=true;return false;}
            if(WaitForSingleObject(command->done,750)!=WAIT_OBJECT_0){command->cancelled=true;requestStop();error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);return false;}
            if(completed)*completed=true;
            error=command->error;return SUCCEEDED(error);
        }catch(...){error=E_OUTOFMEMORY;return false;}
    }
public:
    InputChannel()=default;InputChannel(const InputChannel&)=delete;InputChannel& operator=(const InputChannel&)=delete;
    ~InputChannel(){shutdown();}
    bool closedForSettings() const noexcept {
        // Only an explicit selection may replace a completely retired worker.
        // Live, still closing, and failed cleanup attempts retain their owner.
        return thread_&&state_&&state_->stopping.load(std::memory_order_acquire)&&
            state_->closed.load(std::memory_order_acquire)&&
            (state_->startupPublished.load(std::memory_order_acquire)||FAILED(state_->error.load()))&&
            WaitForSingleObject(thread_,0)==WAIT_OBJECT_0;
    }
    bool start(HRESULT& error) noexcept {
        if(thread_){error=state_?state_->error.load():E_FAIL;return state_&&!state_->stopping.load()&&WaitForSingleObject(thread_,0)==WAIT_TIMEOUT;}
        try{state_=std::make_shared<State>();state_->desktop=GetThreadDesktop(GetCurrentThreadId());if(!state_->ready){error=HRESULT_FROM_WIN32(GetLastError());return false;}
            auto arg=std::make_unique<std::shared_ptr<State>>(state_);thread_=CreateThread(nullptr,0,entry,arg.get(),0,&threadId_);if(!thread_){error=HRESULT_FROM_WIN32(GetLastError());return false;}arg.release();
            if(WaitForSingleObject(state_->ready,1000)!=WAIT_OBJECT_0){error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);requestStop();return false;}
            error=state_->error;return state_->window.load()&&!state_->stopping.load()&&SUCCEEDED(error);
        }catch(...){error=E_OUTOFMEMORY;return false;}
    }
    bool attachmentFailed()const noexcept{return FAILED(attachmentError_.load(std::memory_order_relaxed));}
    void acknowledgeAttachments()noexcept{attachmentError_.store(S_OK,std::memory_order_relaxed);}
    bool attach(const winrt::Windows::UI::Composition::Visual& visual,HWND target,int surface,std::shared_ptr<Input>& out,HRESULT& error,bool ownComposition=false) noexcept {
        try{const auto state=state_;
            // UI-owner preparation precedes the worker command. This call can
            // admit at most one new source slot. Failure recording stays fixed
            // within this prepared storage, including the last active source.
            if(state){const size_t slots=state->inputSlotCount.load(std::memory_order_acquire);
                if(slots>=failedSyncInputs_.max_size())throw std::bad_alloc();
                if(failedSyncInputs_.size()<slots+1)failedSyncInputs_.resize(slots+1);}
            auto input=std::make_shared<Input>();input->target=target;input->surface=surface;out=input;
            bool completed=false;
            const bool attached=invoke([state,input,visual,ownComposition]{
                input->attachStage.store("attach-owned-input-slot",std::memory_order_relaxed);
                auto slot=std::find(state->inputs.begin(),state->inputs.end(),nullptr);
                if(slot==state->inputs.end()){state->inputs.push_back(input);state->inputSlotCount.store(state->inputs.size(),std::memory_order_release);}
                else *slot=input;
                auto bindingVisual=visual;
                if(ownComposition){
                    input->attachStage.store("attach-owned-client-rect",std::memory_order_relaxed);
                    RECT rect{};if(!GetClientRect(input->target,&rect))winrt::throw_last_error();
                    input->attachStage.store("attach-owned-compositor-interop",std::memory_order_relaxed);
                    auto interop=state->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
                    input->attachStage.store("attach-owned-desktop-target",std::memory_order_relaxed);
                    winrt::check_hresult(interop->CreateDesktopWindowTarget(input->target,TRUE,(ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(input->desktopTarget)));
                    input->attachStage.store("attach-owned-visual",std::memory_order_relaxed);
                    input->ownedVisual=state->compositor.CreateSpriteVisual();input->ownedVisual.Size({(float)rect.right,(float)rect.bottom});input->ownedVisual.Brush(state->compositor.CreateColorBrush({0,0,0,0}));
                    input->attachStage.store("attach-owned-target-root",std::memory_order_relaxed);
                    input->desktopTarget.Root(input->ownedVisual);bindingVisual=input->ownedVisual;
                }
                input->attachStage.store("attach-owned-input-controller",std::memory_order_relaxed);
                input->controller=Controller::CreateForVisual(bindingVisual);
                input->attachStage.store("attach-owned-controlled-input",std::memory_order_relaxed);
                input->controller.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);
                input->attachStage.store("attach-owned-input-transparency",std::memory_order_relaxed);
                input->controller.IsTransparentForUncontrolledInput(true);
                input->attachStage.store("attach-owned-input-source",std::memory_order_relaxed);input->source=input->controller.Source();
                const auto consume=[weak=std::weak_ptr<Input>(input)](auto const&,auto const& args,Phase phase){try{args.Handled(true);auto input=weak.lock();if(!input||!input->admitted.load())return;const auto epoch=input->epoch.load();auto point=args.CurrentPoint();auto kind=point.PointerDevice().PointerDeviceType();const POINTER_INPUT_TYPE type=kind==winrt::Windows::Devices::Input::PointerDeviceType::Touch?PT_TOUCH:kind==winrt::Windows::Devices::Input::PointerDeviceType::Pen?PT_PEN:PT_POINTER;if(type==PT_POINTER)return;const auto p=point.Position();if(!std::isfinite(p.X)||!std::isfinite(p.Y))return;input->publish(Event{input->surface,point.PointerId(),type,phase,p.X,p.Y,point.Properties().IsCanceled()||phase==Phase::Cancel,(DWORD)(point.Timestamp()/1000),true},epoch);}catch(...){}};
                input->attachStage.store("attach-owned-input-events",std::memory_order_relaxed);
                input->tokens[0]=input->source.PointerPressed([consume](auto const& s,auto const& a){consume(s,a,Phase::Down);});
                input->tokens[1]=input->source.PointerMoved([consume](auto const& s,auto const& a){consume(s,a,Phase::Move);});
                input->tokens[2]=input->source.PointerReleased([consume](auto const& s,auto const& a){consume(s,a,Phase::Up);});
                input->tokens[3]=input->source.PointerCaptureLost([consume](auto const& s,auto const& a){consume(s,a,Phase::Cancel);});
                input->attachStage.store("attach-owned-process-events",std::memory_order_relaxed);
                input->source.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);input->ready=true;
            },error,&completed);
            if(!attached&&completed)input->attachCommandCompleted=true;
            if(!attached&&FAILED(error))attachmentError_.store(error,std::memory_order_relaxed);return attached;
        }catch(...){error=E_OUTOFMEMORY;attachmentError_.store(error,std::memory_order_relaxed);return false;}
    }
    bool sourceSyncFailedForSettings() const noexcept {
        for(size_t index=0;index<failedSyncInputCount_;++index){const auto input=failedSyncInputs_[index].lock();
            if(input&&!input->consumerClosed.load(std::memory_order_acquire)&&input->syncCommandFailed.load(std::memory_order_acquire))return true;
        }
        return false;
    }
    bool sync(const std::shared_ptr<Input>& input,HRESULT& error,SIZE size={}) noexcept {
        bool completed=false;const bool synced=invoke([input,size]{if(!input||!input->ready.load()||!input->source)winrt::throw_hresult(RO_E_CLOSED);
            if(input->ownedVisual&&size.cx>0&&size.cy>0)input->ownedVisual.Size({(float)size.cx,(float)size.cy});
            auto dispatcher=input->source.Dispatcher();dispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
            input->admitted.store(true,std::memory_order_release);
        },error,&completed);
        if(input&&completed&&!input->consumerClosed.load(std::memory_order_acquire)){
            if(synced)input->syncCommandFailed.store(false,std::memory_order_release);
            else if(FAILED(error)&&error!=E_ABORT){input->syncCommandFailed.store(true,std::memory_order_release);rememberFailedSyncInput(input);}
        }
        return synced;
    }
    bool cancel(const std::shared_ptr<Input>& input,HRESULT& error) noexcept {
        return invoke([input]{if(input&&input->source){if(input->source.HasCapture())input->source.ReleasePointerCapture();input->source.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);}},error);
    }
    bool close(const std::shared_ptr<Input>& input,HRESULT& error) noexcept {
        if(!input)return true;input->admitted=false;const auto state=state_;
        if(state&&state->closed.load())return true;
        return invoke([state,input]{const char* stage="source-close";closeInput(*input,stage);for(auto& slot:state->inputs)if(slot==input)slot.reset();},error);
    }
    bool shutdown() noexcept {
        if(!thread_)return !state_||state_->closed.load();requestStop();
        if(GetCurrentThreadId()==threadId_||WaitForSingleObject(thread_,1500)!=WAIT_OBJECT_0)return false;
        if(!state_->closed.load())return false;
        if(!CloseHandle(thread_))return false;thread_=nullptr;threadId_=0;state_.reset();for(size_t index=0;index<failedSyncInputCount_;++index)failedSyncInputs_[index].reset();failedSyncInputCount_=0;attachmentError_.store(S_OK,std::memory_order_relaxed);return true;
    }
    bool healthy()const noexcept{return thread_&&state_&&!state_->stopping.load()&&WaitForSingleObject(thread_,0)==WAIT_TIMEOUT;}
    HRESULT lastError()const noexcept{return state_?state_->error.load(std::memory_order_acquire):S_OK;}
    CleanupFailure cleanupFailure()const noexcept {
        CleanupFailure result;if(!state_)return result;result.error=state_->cleanupError.load(std::memory_order_acquire);
        if(FAILED(result.error)){result.stage=state_->cleanupStage.load(std::memory_order_relaxed);result.surface=state_->cleanupSurface.load(std::memory_order_relaxed);
            result.stopping=state_->stopping.load(std::memory_order_acquire);result.complete=state_->closed.load(std::memory_order_acquire);}
        return result;
    }
    DWORD threadId()const noexcept{return threadId_;}
};
} // namespace ymcc::screeninput
