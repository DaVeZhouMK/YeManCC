#pragma once
// One lazily-created worker, shared by all keyboard rules. Waiting never occupies
// gamepadSerial or the UI. Each rule has its own bounded execution slot.
// 1 = owned down, 2 = released but awaiting a neutral OS sample. This keeps
// injected output from becoming the next keyboard shortcut's physical input.
static std::array<std::atomic<unsigned char>,256> g_keyboardSequenceInputFence{};
static bool nativeKeyboardSequenceOwnsKey(WORD key) {
    if(key>=g_keyboardSequenceInputFence.size())return false;
    const auto state=g_keyboardSequenceInputFence[key].load(std::memory_order_acquire);
    if(state==2&&(GetAsyncKeyState(key)&0x8000)==0){unsigned char expected=2;g_keyboardSequenceInputFence[key].compare_exchange_strong(expected,0,std::memory_order_acq_rel);}
    return state!=0;
}
class NativeKeyboardSequenceOwner {
    struct Slot {ymcc::keyboardSequence::Engine engine;std::function<bool()> current;};
    std::mutex mutex_;std::condition_variable cv_;std::thread worker_;
    std::map<std::string,Slot> slots_;std::map<std::pair<WORD,bool>,unsigned> owners_;bool stopping_=false;
    bool releaseBlocked()const {return std::any_of(slots_.begin(),slots_.end(),[](const auto& item){return item.second.engine.releaseBlocked();});}
    bool ownsVirtualKey(WORD vk)const {return std::any_of(owners_.begin(),owners_.end(),[vk](const auto& item){return item.first.first==vk;});}
    bool send(ymcc::keyboardSequence::Key key,bool up) {
        const auto identity=std::make_pair(static_cast<WORD>(key.vk),key.extended);
        auto found=owners_.find(identity);
        if(up&&found!=owners_.end()&&found->second>1){--found->second;return true;}
        if(!up&&found!=owners_.end()){++found->second;return true;}
        INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=key.vk;
        input.ki.dwFlags=nativeKeyboardShortcutInputFlags(key.vk)|(key.extended?KEYEVENTF_EXTENDEDKEY:0)|(up?KEYEVENTF_KEYUP:0);
        if(!up)g_keyboardSequenceInputFence[key.vk].store(1,std::memory_order_release);
        if(keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId::NativeKeyboardShortcut,1,&input)!=1){if(!up)g_keyboardSequenceInputFence[key.vk].store(ownsVirtualKey(key.vk)?1:0,std::memory_order_release);return false;}
        if(up){owners_.erase(identity);g_keyboardSequenceInputFence[key.vk].store(ownsVirtualKey(key.vk)?1:2,std::memory_order_release);}
        else owners_[identity]=1;return true;
    }
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);ULONGLONG stopAt=0;
        for(;;) {
            const auto now=GetTickCount64();if(stopping_&&!stopAt)stopAt=now;
            for(auto it=slots_.begin();it!=slots_.end();) {
                auto& slot=it->second;bool allowed=false;
                try {allowed=!stopping_&&!releaseBlocked()&&slot.current&&slot.current();}catch(...){}
                slot.engine.tick(now,allowed,[&](auto key,bool up){return send(key,up);},[&](auto key){return !ownsVirtualKey(key.vk)&&(GetAsyncKeyState(key.vk)&0x8000)!=0;},[]{return GetTickCount64();});
                if(!slot.engine.active()&&!slot.engine.pending())it=slots_.erase(it);else ++it;
            }
            if(stopping_&&(slots_.empty()||now-stopAt>=1000)) {
                if(!slots_.empty())appendNativeLifecycleLog("oem-shortcut-edge-delivery",{{"reason","keyboard-release-failed"},{"status","release-failed"},{"pendingRules",slots_.size()}});
                return;
            }
            if(slots_.empty())cv_.wait(lock,[&]{return stopping_||!slots_.empty();});
            else cv_.wait_for(lock,std::chrono::milliseconds(20));
        }
    }
public:
    ~NativeKeyboardSequenceOwner(){stop();}
    bool submit(const std::string& id,const ymcc::keyboardSequence::Plan& plan,std::function<bool()> current) {
        std::lock_guard<std::mutex> lock(mutex_);if(stopping_||releaseBlocked())return false;
        auto it=slots_.find(id);if(it==slots_.end()){if(slots_.size()>=16)return false;it=slots_.emplace(id,Slot{}).first;}
        if(!it->second.engine.start(id,plan,GetTickCount64()))return false;
        it->second.current=std::move(current);
        if(!worker_.joinable())worker_=std::thread([this]{run();});
        cv_.notify_one();return true;
    }
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
        cv_.notify_one();if(worker_.joinable())worker_.join();
    }
};
static NativeKeyboardSequenceOwner g_nativeKeyboardSequenceOwner;
static void nativeKeyboardSequenceStop(){g_nativeKeyboardSequenceOwner.stop();}
static bool nativeKeyboardSequenceDispatch(const NativeKeyboardShortcutRule& rule,uint64_t sourceEpoch,
    uint64_t wmiGeneration,uint64_t wmiSequence,bool keyboardWmiFallback,OemProducerTokens producerTokens) {
    const auto epoch=g_oemShortcutEdgeEpoch.load(std::memory_order_acquire);const auto power=currentPowerGeneration();
    const auto revision=ymSettingsFields("input",{"revision"}).value("revision",0ll);
    const auto window=GetForegroundWindow();const auto plan=rule.keyboardPlan;const auto id=rule.outputId;
    auto current=[epoch,power,revision,window,sourceEpoch,wmiGeneration,keyboardWmiFallback,producerTokens,lastSettingsCheck=GetTickCount64()]() mutable {
        const auto now=GetTickCount64();if(now-lastSettingsCheck>=400){lastSettingsCheck=now;if(ymSettingsFields("input",{"revision"}).value("revision",0ll)!=revision)return false;}
        return !g_exitRequested.load(std::memory_order_acquire)&&!g_shortcutRecordingActive.load(std::memory_order_acquire)&&
            g_inputReady.load(std::memory_order_acquire)&&sgVirtualWriterAllowedNow()&&!radialHasInputOwner()&&
            currentPowerGeneration()==power&&g_oemShortcutEdgeEpoch.load(std::memory_order_acquire)==epoch&&
            GetForegroundWindow()==window&&oemProducerTokensCurrent(producerTokens)&&
            (sourceEpoch==kOemShortcutUnboundEpoch||sourceEpoch==epoch)&&
            (!wmiGeneration||msiWmiGenerationCurrent(wmiGeneration))&&(!keyboardWmiFallback||!msiWmiShortcutAuthoritative());
    };
    return nativeYmccShortcutSubmit([id,plan,current]() mutable {if(current())g_nativeKeyboardSequenceOwner.submit(id,plan,current);},
        SerialJobKind::ExternalInjection,sourceEpoch,wmiGeneration,wmiSequence,keyboardWmiFallback,producerTokens);
}
