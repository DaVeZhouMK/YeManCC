#pragma once
// Ordinary-mode only, separate from the D4462D53 SD preparation writer/cache.
// No SwitchMode, target create, HidHide, OEM service or registry operation.
// The experimental keyboard payload touches SHARED profile fields; no SD isolation claim.
static ymcc::msi::nativeRear::Plan g_msiNativeRearPlan;
static ymcc::msi::nativeRear::Facts msiNativeRearFacts(){
    return {msiClawKnownBoardAuthority(),g_msiNativeRearDemand.load(std::memory_order_acquire)||g_shortcutRecordingActive.load(std::memory_order_acquire),
        hardwareWriteAllowed(),g_exitRequested.load(std::memory_order_acquire),g_inputCaptureStop.load(std::memory_order_acquire),
        g_realStickTest.virtualTargetEnabled,g_msiDinputRouteDinputArmed.load(std::memory_order_acquire),g_msiDinputReturnOwed.load(std::memory_order_acquire),
        (g_oemKeyHooksActive.load(std::memory_order_acquire)&&g_oemBackHook!=nullptr),g_oemBackProfileId.load(std::memory_order_acquire)==static_cast<int>(OemChordProfileId::Msi),currentPowerGeneration()};
}
static bool msiNativeRearTokenCurrent(ymcc::msi::nativeRear::Token token){
    const auto f=msiNativeRearFacts();return ymcc::msi::nativeRear::writeAdmissionAllowed(f)&&token.power==f.power&&g_msiNativeRearPlan.current(token,GetTickCount64());
}
static bool msiNativeRearIntentCurrent(const std::string& reason){
    ymcc::msi::nativeRear::Token token;return ymcc::msi::nativeRear::parse(reason,token)&&msiNativeRearTokenCurrent(token);
}
struct MsiNativeRearEndpoint {HANDLE handle=INVALID_HANDLE_VALUE;uint16_t firmware=0;unsigned candidates=0,openFailures=0;DWORD enumerationError=0;std::wstring path;};
static MsiNativeRearEndpoint msiNativeRearOpenXInput(){
    MsiNativeRearEndpoint out;
    const GUID& guid=kYeManGuidDeviceInterfaceHid;
    HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE){out.enumerationError=GetLastError();return out;}
    for(DWORD index=0;;++index){
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&guid,index,&iface)){out.enumerationError=GetLastError();break;}
        if(index>=512){out.enumerationError=ERROR_MORE_DATA;break;}
        DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);if(!bytes)continue;
        std::vector<BYTE> buffer(bytes);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA data{};data.cbSize=sizeof(data);if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,&data))continue;
        if(!msiPathMentionsVendor(detail->DevicePath))continue;
        HANDLE handle=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,nullptr);
        if(handle==INVALID_HANDLE_VALUE){++out.openFailures;continue;}
        HIDD_ATTRIBUTES attrs{};attrs.Size=sizeof(attrs);PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS caps{};
        const bool attrOk=HidD_GetAttributes(handle,&attrs)!=FALSE;
        const bool prepOk=attrOk&&attrs.VendorID==kMsiClawVendorId&&attrs.ProductID==kMsiClawPidXInput&&HidD_GetPreparsedData(handle,&prep)!=FALSE;
        const bool capsOk=prepOk&&HidP_GetCaps(prep,&caps)==HIDP_STATUS_SUCCESS;
        if(prep)HidD_FreePreparsedData(prep);
        const bool match=capsOk&&caps.UsagePage==0xFFA0&&caps.Usage==0x0001&&caps.OutputReportByteLength>=64;
        if(match){++out.candidates;if(out.candidates==1){out.handle=handle;out.firmware=attrs.VersionNumber;out.path=detail->DevicePath;handle=INVALID_HANDLE_VALUE;}}
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(set);
    // Never write a guessed first match when external identical pads/partial
    // inventory prevent unique identity. DInput1902 is never a native candidate.
    if(out.candidates!=1||out.enumerationError!=ERROR_NO_MORE_ITEMS){if(out.handle!=INVALID_HANDLE_VALUE)CloseHandle(out.handle);out.handle=INVALID_HANDLE_VALUE;}
    return out;
}
static ymcc::msi::nativeRear::Result msiNativeRearPrepareKeyboard(ymcc::msi::nativeRear::Token token){
    using Result=ymcc::msi::nativeRear::Result;
    if(!msiNativeRearTokenCurrent(token))return Result::Cancelled;
    auto endpoint=msiNativeRearOpenXInput();
    if(endpoint.handle==INVALID_HANDLE_VALUE){
        appendNativeLifecycleLog("msi-native-rear-prepare",{{"state","source-unavailable"},{"epoch",token.epoch},{"requestPower",token.power},
            {"xinputCandidates",endpoint.candidates},{"openFailures",endpoint.openFailures},{"enumerationError",endpoint.enumerationError},
            {"attempt",g_msiNativeRearPlan.attempts()},{"maxSourceWaitAttempts",3},{"hardwareWriteIssued",false},{"sdRouteChanged",false}});
        return endpoint.candidates==0?Result::SourceWait:Result::Failed;
    }
    HANDLE handle=endpoint.handle;
    const auto reports=ymcc::msi::rearProtocol::reports(endpoint.firmware,ymcc::msi::rearProtocol::Route::XInputKeyboard);
    const auto modeGeneration=g_msiFeedbackModeGeneration.load(std::memory_order_acquire);
    const auto current=[&]{return msiNativeRearTokenCurrent(token)&&g_msiFeedbackModeGeneration.load(std::memory_order_acquire)==modeGeneration;};
    if(!reports.valid||!current()){
        CloseHandle(handle);
        appendNativeLifecycleLog("msi-native-rear-prepare",{{"state",reports.valid?"cancelled":"unknown-firmware-refused"},
            {"firmware",endpoint.firmware},{"epoch",token.epoch},{"hardwareWriteIssued",false},{"sdRouteChanged",false}});
        return reports.valid?Result::Cancelled:Result::Failed;
    }
    std::array<bool,3> ok{};std::array<DWORD,3> errors{},written{};bool anyWrite=false,retained=false;
    const auto write=[&](const std::array<uint8_t,64>& packet,unsigned slot){
        if(retained||!current()){errors[slot]=ERROR_OPERATION_ABORTED;return false;}
        anyWrite=true;const auto result=msiClawCommandWriteBounded(handle,packet.data(),packet.size());
        retained=result.handleRetained;written[slot]=result.written;errors[slot]=result.lastError;
        if(retained||result.timedOut||!result.ok){if(!errors[slot])errors[slot]=result.timedOut?ERROR_TIMEOUT:ERROR_WRITE_FAULT;return false;}return true;
    };
    ok[0]=write(reports.m1,0);
    if(ok[0]){Sleep(500);ok[1]=write(reports.m2,1);}else errors[1]=ERROR_OPERATION_ABORTED;
    if(ok[0]&&ok[1]){Sleep(500);ok[2]=write(reports.sync,2);}else errors[2]=ERROR_OPERATION_ABORTED;
    const bool prepared=ok[0]&&ok[1]&&ok[2]&&current();
    if(!retained)CloseHandle(handle); // pending overlapped buffers/handle stay with existing command owner
    appendNativeLifecycleLog("msi-native-rear-prepare",{{"state",prepared?"keyboard-bank-written":"write-failed-or-cancelled"},
        {"epoch",token.epoch},{"requestPower",token.power},{"requestModeGeneration",modeGeneration},{"firmware",endpoint.firmware},
        {"commandInterfacePath",W2U(endpoint.path)},{"commandPid",kMsiClawPidXInput},{"mappingRoute","native-xinput-f11-f14-only"},
        {"m1",ok[0]},{"m2",ok[1]},{"sync",ok[2]},{"m1Error",errors[0]},{"m2Error",errors[1]},{"syncError",errors[2]},
        {"m1Bytes",written[0]},{"m2Bytes",written[1]},{"syncBytes",written[2]},{"handleRetainedByCommandOwner",retained},
        {"hardwareWriteIssued",anyWrite},{"persistentSyncIssued",ok[2]},{"originalMappingRead",false},{"originalMappingRestoreEstablished",false},
        {"mappingVerified",false},{"modeWriteIssued",false},{"targetCreateRequested",false},{"dinputBankWriteIssued",false},{"sdRouteChanged",false},
        {"bankNamesAreDisjointHardwareRegions",false},{"dinputMappingUnaffectedVerified",false},
        {"sharedProfileFieldsMayAffectOtherModes",true},
        {"note","observed keyboard press/release and configured YMCC action still required"}});
    return prepared?Result::Prepared:Result::Failed; // never keep retrying a partial firmware write
}
static void msiNativeRearHandleWork(const std::string& reason){
    ymcc::msi::nativeRear::Token token;if(!ymcc::msi::nativeRear::parse(reason,token))return;
    const auto result=msiNativeRearPrepareKeyboard(token);g_msiNativeRearPlan.complete(token,result,GetTickCount64());
}
static void msiNativeRearBusPump(){
    if(!msiClawKnownBoardAuthority())return;
    const auto facts=msiNativeRearFacts();ymcc::msi::nativeRear::Token token;
    if(g_msiNativeRearPlan.request(ymcc::msi::nativeRear::writeAdmissionAllowed(facts),facts.power,GetTickCount64(),token)){
        const auto reason=ymcc::msi::nativeRear::reason(token);
        if(!inputHostQueueLifecycleWork(reason.c_str()))g_msiNativeRearPlan.complete(token,ymcc::msi::nativeRear::Result::SourceWait,GetTickCount64());
    }
}
// Existing UI physical-edge delivery only. Four first-edge rows per native
// preparation episode; never log inside the LL hook or capture arbitrary keys.
static void msiNativeRearKeyboardEdgeNote(uint64_t mask){
    const auto facts=msiNativeRearFacts();if(!ymcc::msi::nativeRear::allowed(facts))return;
    const auto stamp=g_msiNativeRearPlan.stamp();if(!stamp.token.epoch||stamp.token.power!=facts.power)return;
    static uint64_t lastEpoch=0,lastPower=0;static unsigned previous=0,logged=0;
    if(lastEpoch!=stamp.token.epoch||lastPower!=stamp.token.power){lastEpoch=stamp.token.epoch;lastPower=stamp.token.power;previous=logged=0;}
    const unsigned now=((mask&kOemBitM1)?1u:0u)|((mask&kOemBitM2)?2u:0u);
    for(unsigned i=0;i<2;++i){const unsigned bit=1u<<i;if((now&bit)==(previous&bit))continue;
        const bool down=(now&bit)!=0;const unsigned once=1u<<(i*2+(down?0:1));if(logged&once)continue;logged|=once;
        appendNativeLifecycleLog("msi-native-rear-keyboard-edge",{{"source","native-xinput-keyboard-compat"},
            {"button",i==0?"m1":"m2"},{"transition",down?"press":"release"},
            {"virtualKey",i==0?0x7A:0x7D},{"mappingEpoch",stamp.token.epoch},{"mappingPower",stamp.token.power},
            {"mappingWriteReceiptPrepared",stamp.written},{"observedKeyboardSignal",true},
            {"physicalDeviceIdentityVerified",false},{"virtualTargetEnabled",false},{"sdRouteChanged",false},
            {"verificationScope","physical keyboard edge through existing MSI OEM profile; not firmware readback"}});
    }
    previous=now;
}
