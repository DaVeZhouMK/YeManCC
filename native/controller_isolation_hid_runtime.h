#pragma once

namespace isolationDirect {
static thread_local bool uiEvaluation=false;
struct UiReadScope {
    bool previous=uiEvaluation;
    UiReadScope() {uiEvaluation=true;}
    ~UiReadScope() {uiEvaluation=previous;}
};
struct Port {
    OemHidReadSession session;
    PHIDP_PREPARSED_DATA preparsed=nullptr;
    Device descriptor;
    DWORD lastError=ERROR_SUCCESS;
    struct BindFailure {
        std::string_view preferredStage,stage; // Static stage literals from bind.
        DWORD error=ERROR_SUCCESS;
        CONFIGRET configResult=CR_SUCCESS;
        bool operator==(const BindFailure&) const=default;
    };
    BindFailure previousBindFailure;
    bool haveBindFailure=false;
    bool returned() const noexcept {
        return session.device==INVALID_HANDLE_VALUE && !session.ovEvent && !session.readPending &&
            !session.closing && !preparsed;
    }
    void serviceClosing() {
        oemHidReadServiceClosing(session,"isolation-shared-hid");
        if(session.device==INVALID_HANDLE_VALUE && !session.ovEvent && !session.readPending && !session.closing) {
            if(preparsed && HidD_FreePreparsedData(preparsed))preparsed=nullptr;
            if(!preparsed)descriptor={};
        }
    }
    void close() {
        if(!returned())oemHidReadRequestClose(session,"isolation-shared-hid");
        serviceClosing();
    }
    bool bound() const noexcept {return session.bound && !session.closing;}
    bool canBind() {serviceClosing();return returned();}
    bool pending() const noexcept {return session.readPending;}
    bool ready() const noexcept {
        return bound() && pending() && session.ovEvent &&
            (WaitForSingleObject(session.ovEvent,0)==WAIT_OBJECT_0 || HasOverlappedIoCompleted(&session.ov));
    }
    ymcc::isolationhid::DrainResult discardPending(bool requestCancel) {
        using Result=ymcc::isolationhid::DrainResult;
        auto completion=oemHidReadTakeCompletion(session);
        if(!completion.terminal && !completion.observationFailed && session.readPending && requestCancel) {
            if(!CancelIoEx(session.device,&session.ov)) {
                lastError=GetLastError();
                if(lastError!=ERROR_NOT_FOUND)return Result::Failed;
            }
            completion=oemHidReadTakeCompletion(session);
        }
        if(completion.observationFailed)return Result::Failed;
        if(!completion.terminal)return session.readPending?Result::Waiting:Result::Drained;
        lastError=completion.error;
        return completion.success || completion.error==ERROR_OPERATION_ABORTED?Result::Drained:Result::Failed;
    }
    static bool samePhysicalSource(const ymcc::isolationhid::Key& key,const std::wstring& path) {
        DEVINST node=0;std::wstring instance;
        return !path.empty() && ymccBackgroundUi::interfaceNode(path,node,instance) &&
            sameXusbSource(path,key.slotPath,key.coldIntent!=0) &&
            ymcc::targetReturn::physicalPath::verify(W2U(key.root),W2U(instance)).proven();
    }
    enum class Probe {Other,Gamepad,Unknown};
    static Probe probe(const std::wstring& path,bool cold) noexcept {
        const auto handle=CreateFileW(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return Probe::Unknown;
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        PHIDP_PREPARSED_DATA data=nullptr;HIDP_CAPS caps{};
        const bool known=HidD_GetAttributes(handle,&attributes) && HidD_GetPreparsedData(handle,&data) &&
            HidP_GetCaps(data,&caps)==HIDP_STATUS_SUCCESS;
        const bool freed=!data || HidD_FreePreparsedData(data);
        if(!CloseHandle(handle) || !freed || !known)return Probe::Unknown;
        return (cold || (attributes.VendorID==0x045e && attributes.ProductID==0x028e)) && caps.UsagePage==1 && caps.Usage==5
            ?Probe::Gamepad:Probe::Other;
    }
    static bool findCurrentPath(const ymcc::isolationhid::Key& key,std::wstring& selected,
            const char*& stage,CONFIGRET& configResult) {
        selected.clear();
        DEVINST slotNode=0;std::wstring slotInstance;
        stage="slot-root-unproven";
        if(!ymccBackgroundUi::interfaceNode(key.slotPath,slotNode,slotInstance) ||
           !ymcc::targetReturn::physicalPath::verify(W2U(key.root),W2U(slotInstance)).proven())return false;
        auto root=key.root;
        std::array<DEVINST,32> nodes{};
        stage="root-unavailable";
        configResult=CM_Locate_DevNodeW(&nodes.front(),root.data(),CM_LOCATE_DEVNODE_NORMAL);
        if(configResult!=CR_SUCCESS || !nodes.front())return false;
        const auto started=GetTickCount64();
        unsigned nodeCount=1,pathCount=0;
        auto hidGuid=kYeManGuidDeviceInterfaceHid;
        for(unsigned index=0;index<nodeCount;++index) {
            stage="current-inventory-limit";
            if(GetTickCount64()-started>30)return false;
            std::array<wchar_t,512> instance{};
            stage="current-instance-unavailable";
            configResult=CM_Get_Device_IDW(nodes[index],instance.data(),static_cast<ULONG>(instance.size()),0);
            if(configResult!=CR_SUCCESS || !instance.front() || instance.back()!=L'\0' ||
               (index==0 && _wcsicmp(instance.data(),root.c_str())!=0))return false;
            std::array<wchar_t,2048> paths{};
            stage="current-interface-list-unavailable";
            configResult=CM_Get_Device_Interface_ListW(&hidGuid,instance.data(),paths.data(),
                static_cast<ULONG>(paths.size()),CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
            if(configResult!=CR_SUCCESS)return false;
            for(auto path=paths.begin();path!=paths.end() && *path;) {
                stage="current-inventory-limit";
                const auto end=std::find(path,paths.end(),L'\0');
                if(++pathCount>16 || end==paths.end() || end+1==paths.end() || GetTickCount64()-started>30)return false;
                const std::wstring candidate(path,end);path=end+1;
                DEVINST listedNode=0;std::wstring listedInstance;
                stage="current-interface-node-changed";
                if(!ymccBackgroundUi::interfaceNode(candidate,listedNode,listedInstance) || listedNode!=nodes[index] ||
                   _wcsicmp(listedInstance.c_str(),instance.data())!=0)return false;
                if(!sameXusbSource(candidate,key.slotPath,key.coldIntent!=0))continue;
                stage="current-interface-probe-unavailable";
                const auto suitability=probe(candidate,key.coldIntent!=0);
                if(suitability==Probe::Unknown)return false;
                if(suitability!=Probe::Gamepad)continue;
                stage="current-game-interface-ambiguous";
                if(!selected.empty() && _wcsicmp(selected.c_str(),candidate.c_str())!=0)return false;
                selected=candidate;
            }
            DEVINST child=0;
            stage="current-device-tree-unavailable";
            configResult=CM_Get_Child(&child,nodes[index],0);
            while(configResult==CR_SUCCESS) {
                stage="current-inventory-limit";
                if(!child || nodeCount==nodes.size() || GetTickCount64()-started>30 ||
                   std::find(nodes.begin(),nodes.begin()+nodeCount,child)!=nodes.begin()+nodeCount)return false;
                nodes[nodeCount++]=child;
                const auto sibling=child;
                stage="current-device-tree-unavailable";
                configResult=CM_Get_Sibling(&child,sibling,0);
            }
            if(configResult!=CR_NO_SUCH_DEVNODE)return false;
        }
        configResult=CR_SUCCESS;
        stage="no-current-game-interface";
        return !selected.empty();
    }
    bool bind(const ymcc::isolationhid::Key& key,unsigned attempt=1) {
        lastError=ERROR_INVALID_DATA;
        if(attempt==1)haveBindFailure=false;
        struct BindReceipt {
            Port& port;const ymcc::isolationhid::Key& key;
            unsigned attempt;
            std::wstring selected;
            const char* preferredStage="not-attempted";
            const char* stage="retirement";
            CONFIGRET configResult=CR_SUCCESS;
            ~BindReceipt() noexcept {
                const bool bound=port.bound();
                const BindFailure failure{preferredStage,stage,port.lastError,configResult};
                const bool limitReached=!bound && attempt==ymcc::isolationhid::maxBindAttempts;
                const char* boundary=bound?(port.haveBindFailure?"bound-after-failure":key.wakeSuspend?"bound-win5-wake":key.coldIntent?"bound-cold-fse":nullptr):
                    limitReached?"bind-attempt-limit":!port.haveBindFailure?"first-failure":
                    port.previousBindFailure!=failure?"failure-changed":nullptr;
                port.haveBindFailure=!bound;
                if(!bound)port.previousBindFailure=failure;
                if(!boundary && !domainLogEnabled("virtual"))return;
                try {appendNativeLifecycleLog(boundary?"native-isolation-shared-hid-bind-boundary":"native-isolation-shared-hid-bind",{{"bound",bound},
                    {"attempt",attempt},{"attemptLimit",ymcc::isolationhid::maxBindAttempts},
                    {"boundary",boundary?json(boundary):json(nullptr)},
                    {"error",port.lastError},{"power",key.power},{"edge",key.edge},{"topology",key.topology},
                    {"identity",key.identity},{"occupant",key.occupant},{"slot",key.slot},
                    {"readerRole",key.wakeSuspend?"win5-kept-sd-wake":key.coldIntent?"cold-fse":"original-isolation"},
                    {"wakeSuspendSerial",key.wakeSuspend},{"wakeNotificationSequence",key.wakeNotice},
                    {"path",W2U(selected.empty()?key.preferredPath:selected)},{"preferredPath",W2U(key.preferredPath)},
                    {"root",W2U(key.root)},{"preferredStage",preferredStage},{"stage",stage},{"configRet",configResult},
                    {"access","shared-read-only-overlapped"},{"inputDeliveryProven",false}});}catch(...){}
            }
        } receipt{*this,key,attempt};
        try {
            if(!canBind())return false;
            if(key.coldIntent) {
                receipt.stage="current-xinput-gamepad-subtype";
                XINPUT_CAPABILITIES caps{};
                if(!g_ymccXInputGetCapabilities14 || key.slot>=4 ||
                   g_ymccXInputGetCapabilities14(key.slot,XINPUT_FLAG_GAMEPAD,&caps)!=ERROR_SUCCESS ||
                   caps.Type!=XINPUT_DEVTYPE_GAMEPAD || caps.SubType!=XINPUT_DEVSUBTYPE_GAMEPAD)return false;
            }
            if(bindPath(key,key.preferredPath,receipt.preferredStage)) {
                receipt.stage="bound-preferred";return true;
            }
            close();
            if(!returned() || receipt.preferredStage==std::string_view("read-event"))return false;
            if(!findCurrentPath(key,receipt.selected,receipt.stage,receipt.configResult))return false;
            if(_wcsicmp(receipt.selected.c_str(),key.preferredPath.c_str())==0) {
                receipt.stage="preferred-still-current";return false;
            }
            if(!bindPath(key,receipt.selected,receipt.stage))return false;
            receipt.stage="bound-current";return true;
        } catch(...) {
            lastError=ERROR_NOT_ENOUGH_MEMORY;receipt.stage="bind-exception";
            try {close();} catch(...) {}
            return false;
        }
    }
    bool bindPath(const ymcc::isolationhid::Key& key,const std::wstring& path,const char*& stage) {
        lastError=ERROR_INVALID_DATA;stage="interface-ancestry";
        if(!samePhysicalSource(key,path))return false;
        stage="interface-open";
        session.device=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(session.device==INVALID_HANDLE_VALUE){lastError=GetLastError();return false;}
        stage="read-event";
        session.ovEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!session.ovEvent){lastError=GetLastError();return false;}
        stage="hid-caps";
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        if(!HidD_GetAttributes(session.device,&attributes) || !HidD_GetPreparsedData(session.device,&preparsed) ||
            HidP_GetCaps(preparsed,&descriptor.caps)!=HIDP_STATUS_SUCCESS)return false;
        auto& caps=descriptor.caps;
        if(caps.UsagePage!=1 || caps.Usage!=5 || !caps.InputReportByteLength || caps.InputReportByteLength>256 ||
            !caps.NumberInputButtonCaps || caps.NumberInputButtonCaps>8 ||
            !caps.NumberInputValueCaps || caps.NumberInputValueCaps>16)return false;
        descriptor.info.hid.dwVendorId=attributes.VendorID;descriptor.info.hid.dwProductId=attributes.ProductID;
        stage="hid-button-caps";
        std::array<HIDP_BUTTON_CAPS,8> buttonCaps{};USHORT count=caps.NumberInputButtonCaps;
        if(HidP_GetButtonCaps(HidP_Input,buttonCaps.data(),&count,preparsed)!=HIDP_STATUS_SUCCESS ||
            count!=caps.NumberInputButtonCaps)return false;
        for(unsigned index=0;index<count;++index) {
            const auto& value=buttonCaps[index];
            descriptor.buttons.push_back({value.UsagePage,value.IsRange?value.Range.UsageMin:value.NotRange.Usage,
                value.IsRange?value.Range.UsageMax:value.NotRange.Usage,value.IsRange?value.Range.DataIndexMin:value.NotRange.DataIndex,
                value.IsRange?value.Range.DataIndexMax:value.NotRange.DataIndex,value.LinkCollection,value.LinkUsagePage,value.LinkUsage,
                value.ReportID,value.BitField,value.IsRange!=FALSE,value.IsAbsolute!=FALSE,value.IsAlias!=FALSE});
        }
        stage="hid-value-caps";
        std::array<HIDP_VALUE_CAPS,16> valueCaps{};count=caps.NumberInputValueCaps;
        if(HidP_GetValueCaps(HidP_Input,valueCaps.data(),&count,preparsed)!=HIDP_STATUS_SUCCESS ||
            count!=caps.NumberInputValueCaps)return false;
        for(unsigned index=0;index<count;++index) {
            const auto& value=valueCaps[index];
            descriptor.values.push_back({value.UsagePage,value.IsRange?value.Range.UsageMin:value.NotRange.Usage,
                value.IsRange?value.Range.UsageMax:value.NotRange.Usage,value.IsRange?value.Range.DataIndexMin:value.NotRange.DataIndex,
                value.IsRange?value.Range.DataIndexMax:value.NotRange.DataIndex,value.LinkCollection,value.LinkUsagePage,value.LinkUsage,
                value.ReportID,value.BitSize,value.ReportCount,value.BitField,value.LogicalMin,value.LogicalMax,
                value.IsRange!=FALSE,value.IsAbsolute!=FALSE,value.IsAlias!=FALSE,value.HasNull!=FALSE});
        }
        stage="hid-report-layout";
        descriptor.maxData=HidP_MaxDataListLength(HidP_Input,preparsed);
        if(!ymcc::controllerhid::qualified(descriptor.descriptor()) ||
            descriptor.maxData<caps.NumberInputDataIndices || descriptor.maxData>64)return false;
        session.buf.assign(caps.InputReportByteLength,0);session.bound=true;
        ++session.actionGeneration;lastError=ERROR_SUCCESS;stage="bound";
        return true;
    }
    ymcc::isolationhid::ReadResult poll(ymcc::controllerhid::Frame& frame) {
        const auto completion=oemHidReadTakeCompletion(session);
        if(!completion.terminal)return completion.observationFailed?
            ymcc::isolationhid::ReadResult::Failed:ymcc::isolationhid::ReadResult::Waiting;
        if(!completion.success || completion.readGeneration!=session.actionGeneration ||
            completion.bytes!=descriptor.caps.InputReportByteLength) {
            lastError=completion.success?ERROR_INVALID_DATA:completion.error;
            return ymcc::isolationhid::ReadResult::Failed;
        }
        std::array<HIDP_DATA,64> data{};ULONG count=descriptor.maxData;
        if(HidP_GetData(HidP_Input,data.data(),&count,preparsed,reinterpret_cast<PCHAR>(session.buf.data()),
            completion.bytes)!=HIDP_STATUS_SUCCESS || count>data.size())return ymcc::isolationhid::ReadResult::Failed;
        std::array<ymcc::controllerhid::DataPoint,64> points{};
        for(ULONG index=0;index<count;++index)points[index]={data[index].DataIndex,data[index].RawValue,data[index].On!=FALSE};
        return ymcc::controllerhid::decoded(ymcc::controllerhid::decode(descriptor.descriptor(),
            {completion.bytes,session.buf[0],{points.data(),count}},frame))?
            ymcc::isolationhid::ReadResult::Frame:ymcc::isolationhid::ReadResult::Failed;
    }
    bool begin() {
        if(!oemHidReadCanBegin(session) || !ResetEvent(session.ovEvent))return false;
        ZeroMemory(&session.ov,sizeof(session.ov));session.ov.hEvent=session.ovEvent;
        session.readActionGeneration=session.actionGeneration;
        const bool completed=ReadFile(session.device,session.buf.data(),static_cast<DWORD>(session.buf.size()),nullptr,&session.ov)!=FALSE;
        lastError=completed?ERROR_SUCCESS:GetLastError();
        session.readPending=completed || lastError==ERROR_IO_PENDING;
        return session.readPending;
    }
};
static auto& reader=*new ymcc::isolationhid::Reader<Port>;
static ymcc::commonhid::Lane lane;
static ymcc::isolationhid::Key key;
static bool haveKey=false;
static std::uint64_t reportSequence=0;
static std::atomic<bool> backgroundRetirementPending{false};
static void retireSample() {
    if(lane.hasSample() && !key.wakeSuspend && !key.coldIntent) {
        ymccCommonHid::lane.clear();
        uiRetirementPending.store(true,std::memory_order_release);
        backgroundRetirementPending.store(true,std::memory_order_release);
    }
    lane.clear();
}
static std::uint64_t readSleepBoundary=0;
static void withdraw() {
    // Keep servicing cancellation and handle-return debt after the key is gone.
    if(!haveKey && !lane.hasSample() && !reportSequence && !readSleepBoundary && reader.idle())return;
    if(reader.awaitingFresh() && !key.wakeSuspend && !key.coldIntent)ymccCommonHid::lane.clear();
    retireSample();haveKey=false;key={};reportSequence=0;readSleepBoundary=0;reader.withdraw();
}
// The retained virtual target still owns all output. Borrow only physical data
// from this same source and public wake, using the existing single read port.
static bool win5WakeDemandReadable(std::uint64_t demand) noexcept {
    return demand && !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        demand==g_inputHostRecoveryIntent.revision();
}
static bool currentWin5Wake(const Epoch& source,ymcc::isolationhid::Key& next,bool& reuse) {
    reuse=false;
    const auto host=g_win5WakeReaderHost.load(std::memory_order_acquire);
    if(!host || host!=inputHostEpochToken() || !g_realStickTest.virtualTargetEnabled ||
       !g_inputReady.load(std::memory_order_acquire) || g_inputCaptureStop.load(std::memory_order_acquire) ||
       g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) || inputHostExitFenceRaised() ||
       !g_xinputPhysicalSourceLocked)return false;
    const auto demand=g_win5WakeReaderDemand.load(std::memory_order_acquire);
    if(!win5WakeDemandReadable(demand))return false;
    ymcc::sleeppower::SharedPowerV1 wakePower;
    if(!captureSharedPowerLifecycle(wakePower) || !wakePower.suspendSerial ||
       wakePower.generation!=g_wakeKeptTargetGeneration.load(std::memory_order_acquire) ||
       wakePower.suspendSerial!=g_wakeKeptTargetSuspendSerial.load(std::memory_order_acquire) ||
       source.power!=wakePower.generation || host!=g_win5WakeReaderHost.load(std::memory_order_acquire))return false;
    if(haveKey && key.power==source.power && key.edge==host && key.topology==source.topology &&
       key.identity==source.identity && key.occupant==source.occupant && key.slot==source.slot &&
       key.wakeSuspend==wakePower.suspendSerial && key.wakeNotice==wakePower.observedNotificationSequence && key.wakeIntent==demand){
        reuse=true;return true;
    }
    Epoch verified;std::wstring slotPath;
    if(!context(verified,&slotPath) || verified!=source || slotPath.empty())return false;
    std::unique_lock<std::mutex> physical(g_hidHidePhysicalIdentityMx,std::try_to_lock);
    if(!physical.owns_lock())return false;
    const auto& selected=g_hidHidePhysicalIdentity;
    if(!selected.resolved || selected.vendorId!=0x045e || selected.productId!=0x028e ||
       selected.baseContainerDeviceInstanceId.empty() || selected.sourcePath.empty())return false;
    next={source.power,host,source.topology,source.identity,source.occupant,source.slot,
        U2W(selected.baseContainerDeviceInstanceId),U2W(selected.sourcePath),std::move(slotPath),
        wakePower.suspendSerial,wakePower.observedNotificationSequence,demand};
    return true;
}
static bool coldScene() {
    std::uint64_t intent=0,host=0;
    if(!coldFseReaderCurrent(currentPowerGeneration(),intent,host))return false;
    static bool entered=false,departed=false;
    if(departed)return false;
    if(!ymccBackgroundUi::fseActive()){
        if(entered)departed=true;
        return false;
    }
    entered=true;
    return true;
}
static bool currentColdFse(const Epoch& source,ymcc::isolationhid::Key& next,bool& reuse) {
    reuse=false;
    std::uint64_t intent=0,host=0;
    if(!coldScene() || !coldFseReaderCurrent(source.power,intent,host) || !g_realStickTest.virtualTargetEnabled ||
       !g_inputReady.load(std::memory_order_acquire) || g_inputCaptureStop.load(std::memory_order_acquire) ||
       g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) || inputHostExitFenceRaised() ||
       !g_xinputPhysicalSourceLocked)return false;
    if(haveKey && key.coldIntent==intent && key.edge==host && key.power==source.power &&
       key.topology==source.topology && key.identity==source.identity && key.occupant==source.occupant && key.slot==source.slot){
        reuse=true;return true;
    }
    Epoch verified;std::wstring slotPath;
    if(!context(verified,&slotPath) || verified!=source || slotPath.empty())return false;
    std::unique_lock<std::mutex> physical(g_hidHidePhysicalIdentityMx,std::try_to_lock);
    if(!physical.owns_lock())return false;
    const auto& selected=g_hidHidePhysicalIdentity;
    if(!selected.resolved || selected.baseContainerDeviceInstanceId.empty() || selected.sourcePath.empty())return false;
    next={source.power,host,source.topology,source.identity,source.occupant,source.slot,
        U2W(selected.baseContainerDeviceInstanceId),U2W(selected.sourcePath),std::move(slotPath),0,0,0,intent};
    return true;
}
static bool current(ymcc::isolationhid::Key& next,bool retainPending=false) {
    const auto permit=g_controllerIsolationHidPermit.load(std::memory_order_acquire);
    if(!permit)return false;
    Epoch epoch;std::wstring slotPath;
    const bool physical=context(epoch,&slotPath);
    ymcc::isolationhid::Scene scene;
    scene.xboxFse=ymccBackgroundUi::fseActive();
    scene.originalIsolation=g_controllerFrontendIsolationEnabled.load(std::memory_order_acquire) &&
        g_msiNativeForegroundObserverActive.load(std::memory_order_acquire) && msiNativeForegroundUiEligible();
    scene.physicalSource=physical;
    scene.virtualOff=inputHostConfiguredPersona()=="disabled" && !g_realStickTest.virtualTargetEnabled;
    scene.powerReady=g_inputReady.load(std::memory_order_acquire) &&
        !g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) &&
        (retainPending ? g_sleepPowerDispatch.pendingSuspend() && !g_aiFanMockSession.isolated &&
            g_hardwareWriteGate.load(std::memory_order_acquire) &&
            g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready : hardwareWriteAllowed());
    scene.stopping=g_exitRequested.load(std::memory_order_acquire) || g_inputCaptureStop.load(std::memory_order_acquire) || inputHostExitFenceRaised();
    scene.higherPriority=msiNativeForegroundVirtualWorkHasPriority() ||
        g_msiNativeForegroundVirtualYieldPending.load(std::memory_order_acquire);
    scene.power=currentPowerGeneration();scene.edge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    scene.suspendSerial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    if(epoch.power!=scene.power || !ymcc::isolationhid::admitted(*permit,scene))return false;
    next={epoch.power,permit->edge,epoch.topology,epoch.identity,epoch.occupant,epoch.slot,
        permit->root,permit->preferredPath,std::move(slotPath)};
    return true;
}
static void pauseReadAtBoundary(std::uint64_t boundary) {
    retireSample();
    if(!key.wakeSuspend && !key.coldIntent)ymccCommonHid::lane.clear();
    readSleepBoundary=boundary;
    if(reader.port().bound())reader.pause();
}
static bool pausePending() {
    if(!haveKey || !reader.port().bound() || !g_sleepPowerDispatch.pendingSuspend())return false;
    ymcc::isolationhid::Key next;
    if(!current(next,true) || !(key==next))return false;
    pauseReadAtBoundary(g_sleepPowerDispatch.observedSleepBoundarySequence());
    return true;
}
static bool sampleReadAllowed(bool allowed) {
    if(!allowed || !haveKey)return allowed;
    const bool pending=g_sleepPowerDispatch.pendingSuspend();
    const auto boundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    if(readSleepBoundary!=boundary){pauseReadAtBoundary(boundary);return false;}
    if(pending || reader.awaitingFresh())return false;
    if(key.coldIntent){
        std::uint64_t intent=0,host=0;
        return coldScene() && coldFseReaderCurrent(key.power,intent,host) && intent==key.coldIntent && host==key.edge;
    }
    if(key.wakeSuspend)return currentPowerGeneration()==key.power &&
        g_sharedPowerSuspendSerial.load(std::memory_order_acquire)==key.wakeSuspend &&
        g_sleepPowerDispatch.observedSequence()==key.wakeNotice &&
        g_win5WakeReaderHost.load(std::memory_order_acquire)==key.edge &&
        inputHostEpochToken()==key.edge && win5WakeDemandReadable(key.wakeIntent);
    return true;
}
static void observe(bool ui,const Epoch& source,bool coldPrimarySelected=false) {
    const auto boundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    ymcc::isolationhid::Key next;
    bool reuse=false;
    if(!currentWin5Wake(source,next,reuse) &&
       (coldPrimarySelected || !currentColdFse(source,next,reuse)) && !current(next)){
        if(!pausePending()){
            withdraw();if(!reader.port().returned())reader.port().serviceClosing();
        }
        return;
    }
    if(!reuse && (!haveKey || !(key==next))){
        withdraw();key=std::move(next);haveKey=true;readSleepBoundary=boundary;
    }else if(readSleepBoundary!=boundary)pauseReadAtBoundary(boundary);
    if(!ui || !uiEvaluation)return;
    if(g_sleepPowerDispatch.pendingSuspend() || readSleepBoundary!=g_sleepPowerDispatch.observedSleepBoundarySequence())return;
    if(key.wakeSuspend && !win5WakeDemandReadable(key.wakeIntent))return;
    if(reader.awaitingFresh() && !key.wakeSuspend && !key.coldIntent)ymccCommonHid::lane.clear();
    ymcc::controllerhid::Frame frame;std::uint64_t observedSequence=0,observedTick=0;
    const bool observed=reader.read(&key,GetTickCount64(),frame,observedSequence,observedTick);
    if(observed) {
        ymcc::isolationhid::Key after;
        bool stillCurrent=false;
        if(key.wakeSuspend){
            Epoch currentSource;
            bool sameKey=false;
            stillCurrent=eligible() && context(currentSource) &&
                currentWin5Wake(currentSource,after,sameKey) && sameKey;
        }else if(key.coldIntent) {
            Epoch currentSource;
            bool sameKey=false;
            stillCurrent=eligible() && context(currentSource) &&
                currentColdFse(currentSource,after,sameKey) && sameKey;
        }else stillCurrent=current(after) && key==after;
        if(!stillCurrent){
            if(!pausePending())withdraw();
            return;
        }
    }
    const auto afterBoundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    if(readSleepBoundary!=afterBoundary){pauseReadAtBoundary(afterBoundary);return;}
    if(!observed){retireSample();return;}
    if(observedSequence==reportSequence)return;
    if(key.wakeSuspend && !reportSequence){
        try{appendNativeLifecycleLog("gamepad-win5-kept-sd-wake-first-hid-report",{
            {"power",key.power},{"hostEpoch",key.edge},{"suspendSerial",key.wakeSuspend},
            {"notificationSequence",key.wakeNotice},{"reportSequence",observedSequence},
            {"observedTick",observedTick},{"completeBasic",frame.completeBasic},
            {"basicActive",ymcc::commonhid::basicActive(frame.pad)},{"gameConsumptionProven",false}});}catch(...){}
    }
    if(key.coldIntent && !reportSequence){
        try{appendNativeLifecycleLog("gamepad-cold-fse-first-hid-report",{
            {"power",key.power},{"hostEpoch",key.edge},{"reportSequence",observedSequence},
            {"observedTick",observedTick},{"completeBasic",frame.completeBasic},
            {"basicActive",ymcc::commonhid::basicActive(frame.pad)},{"gameConsumptionProven",false}});}catch(...){}
    }
    reportSequence=observedSequence;
    lane.observe({key.power,key.topology,key.identity,key.occupant,key.slot},
        {frame.pad,++sequence,observedTick,frame.independentTriggers,frame.completeBasic,frame.combinedTriggerRaw,true});
}
}
