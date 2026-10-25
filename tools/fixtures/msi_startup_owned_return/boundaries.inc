// Explicit inert boundaries for the integrated startup/return fixture.
static constexpr DWORD FILE_FLAG_OVERLAPPED=0x40000000;
enum class PowerLifecycle {Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_msiDinputSourceValid{true},g_msiDinputRouteConfirmed{false},g_msiMode1TerminalAttempted{false};
static std::atomic<unsigned long long> g_msiFeedbackSamplePower{9},g_msiFeedbackSampleMode{4},g_msiFeedbackSampleTopology{8};
struct HidHidePhysicalIdentity {bool resolved=true;unsigned vendorId=0x0db0,productId=0x1902;std::string baseContainerDeviceInstanceId;};
struct HidHideMsiMainSourceRecord {bool valid=true;unsigned generation=1,vendorId=0x0db0,productId=0x1902;std::string usbInstance,interfacePath;};
struct HidHideVisibilitySnapshot {bool valid=true,cloaking=true;std::vector<std::wstring> hidden,applications;};
struct StartupJournal {bool active=false,applicationAdded=false,ownedApplicationResponsibilityUnknown=false,cloakChanged=false;
    HidHideVisibilitySnapshot before{false,true,{},{}};std::string transactionId;HidHidePhysicalIdentity physical;
    std::vector<HidHidePhysicalIdentity> physicalSources;std::vector<std::wstring> hiddenByUs,ownedApplications;};
static StartupJournal g_hidHideVisibilityJournal;
static bool g_fakeOldOwnerDead=true,g_fakeStoreOk=true,g_fakeResolveOk=true,g_fakeVisibilityOk=true,g_fakeUnhideOk=true;
static bool g_fakeLocalClearOk=true;
static unsigned g_fakeInitialWrites=0,g_fakeStoreCalls=0,g_fakeUnhides=0,g_fakeSchedules=0;
static HidHideMsiMainSourceRecord g_fakeSource;
static std::vector<HidHidePhysicalIdentity> g_fakePhysical;
static std::vector<std::wstring> g_fakeHidden;
static MsiClawCommandOutcome g_fakeInitialOutcome;
static std::function<void()> g_fakeStoreHook,g_fakeIdentityHook,g_fakePhysicalQueryHook;
static std::string g_fakeDurableText;
static bool hidHideContainsPath(const std::vector<std::wstring>& paths,const std::string& value){
    return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return hidHideUpper(W2U(path))==hidHideUpper(value);});
}
static std::wstring msiCrashRecoveryPath(){return L"offline-only-controller-return.json";}
static std::string msiCrashCurrentOwner(){return std::to_string(GetCurrentProcessId())+":"+msiCrashProcessCreation(GetCurrentProcess());}
static bool msiCrashOldOwnerDead(const msiRecovery::Record& record){
    return (record.ownerPid!=GetCurrentProcessId() || record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess())) && g_fakeOldOwnerDead;
}
static void msiCrashLoadLocked(){} // Fixtures seed a decoded, already-loaded valid record.
static void msiCrashNote(const char* phase,const char* reason,bool persisted=false){
    appendNativeLifecycleLog("msi-crash-return",{{"phase",phase},{"reason",reason},{"persisted",persisted}});
}
static bool sgWriteFileAtomic(const std::wstring&,const std::string& text){
    ++g_fakeStoreCalls;
    if(g_fakeStoreHook){auto hook=std::move(g_fakeStoreHook);g_fakeStoreHook={};hook();}
    if(!g_fakeStoreOk)return false;
    const auto decoded=msiRecovery::decodeRecord(text);
    if(decoded.state!=msiRecovery::DecodeState::Valid)throw std::runtime_error("production persisted an invalid record");
    if(!decoded.record.modeReturnPending && g_msiCrashRecord.modeReturnPending)++g_count.durableDischarges;
    g_fakeDurableText=text;
    return true;
}
static bool msiDinputMainSourceRecordSnapshot(HidHideMsiMainSourceRecord& source){source=g_fakeSource;return g_msiDinputSourceValid.load() && source.valid;}
static bool msiCrashResolveDinputIdentity(const HidHideMsiMainSourceRecord& source,HidHidePhysicalIdentity& physical){
    physical={};physical.baseContainerDeviceInstanceId=source.usbInstance;
    if(g_fakeIdentityHook){auto hook=std::move(g_fakeIdentityHook);g_fakeIdentityHook={};hook();}
    return g_fakeResolveOk;
}
static bool hidHideGetPhysicalIdentities(std::vector<HidHidePhysicalIdentity>& sources){sources=g_fakePhysical;if(g_fakePhysicalQueryHook){auto hook=std::move(g_fakePhysicalQueryHook);g_fakePhysicalQueryHook={};hook();}return true;}
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot& after){after={};after.hidden=g_fakeHidden;return g_fakeVisibilityOk;}
static void msiCrashRecoveryAfterOwnedRestore(const HidHideVisibilitySnapshot&,bool);
static bool hidHideRestorePhysicalTransaction(const char*){
    ++g_fakeUnhides;
    if(g_fakeUnhideOk)for(const auto& path:g_hidHideVisibilityJournal.hiddenByUs)
        g_fakeHidden.erase(std::remove(g_fakeHidden.begin(),g_fakeHidden.end(),path),g_fakeHidden.end());
    HidHideVisibilitySnapshot after;const bool readback=hidHideCaptureVisibilitySnapshot(after);
    msiCrashRecoveryAfterOwnedRestore(after,readback);return readback && g_fakeUnhideOk;
}
static void msiDinputReleasePad(const char*){}
static void msiDinputClearOwnedState(const char*){if(g_fakeLocalClearOk)g_msiDinputSourceValid=false;}
static void ymccResolveMachineIdentityOnce(){}
static bool msiControllerTraceEnabled(){return false;}
static void msiControllerTraceLog(const char*,const json&,bool){}
static void msiTerminalReturnSchedule(const char*,const MsiClawCommandOutcome&){++g_fakeSchedules;g_msiTerminalReturnPending=true;}
static MsiClawCommandOutcome msiClawSwitchMode(BYTE mode){
    MsiClawCommandOutcome outcome=g_fakeInitialOutcome;outcome.diag=g_commandDiag;
    if(!g_commandAvailable){outcome={};outcome.write.state=MsiClawIoState::NoDevice;return outcome;}
    if(!msiCrashRecoveryBeforeModeWrite(mode,outcome.diag,false)){outcome={};return outcome;}
    if(outcome.write.attempted)++g_fakeInitialWrites;
    return outcome;
}
