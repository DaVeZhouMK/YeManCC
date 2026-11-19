// Private state/env lifetime around the ORIGINAL MSI case. No new test or owner.
#pragma once
static bool fixtureMsiJointChildrenReturned();
static void fixtureMsiJointObserveState(bool);
namespace fixtureMsiJointState {
static bool active=false,environmentChanged=false,hadEnvironment=false;
static std::wstring previousEnvironment;
static void restoreEnvironment(){
    if(environmentChanged&&!SetEnvironmentVariableW(L"YMCC_FIXTURE_MSI_VISIBILITY_STATE",
        hadEnvironment?previousEnvironment.c_str():nullptr))
        throw std::logic_error("MSI-joint-state-environment-restore-failed");
    environmentChanged=false;
}
}
static void fixtureMsiJointPrepareState(){
    if(fixtureMsiJointState::active||fixtureMsiJointEnabled||
       !msiPolicyReturnIo::scopeEnabled||!fixtureTargetReturnCaseSerial)
        throw std::logic_error("MSI-joint-state-requires-fresh-declared-case");
    const auto* selectedBoundary=std::getenv("YMCC_FIXTURE_MSI_POLICY_BOUNDARY");
    if(selectedBoundary&&*selectedBoundary&&!fixtureMsiJointBoundaryReady(selectedBoundary))
        throw std::logic_error("MSI-joint-non-seam-cancellation-input-not-yet-admitted");
    if(!g_cmds.contains("settings.write"))reg_fs();
    const auto root=fspath::absolute(fspath::path(g_yemanccDataDir)).lexically_normal();
    if(!root.is_absolute()||!fspath::is_directory(root)||
       (GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::logic_error("MSI-joint-state-private-root-unproven");
    const auto builtin=msiPolicyReturnIo::identity(true);
    std::vector<std::wstring> devices={U2W(builtin.sourcePath),U2W(builtin.deviceInstanceId),
        U2W(builtin.baseContainerDeviceInstanceId),U2W(builtin.baseContainerDevicePath),
        LR"(USB\VID_0DB0&PID_1902\MSI_POLICY_RETURN_BUILTIN)",
        LR"(HID\VID_0DB0&PID_1902&MI_00&COL01\MSI_POLICY_RETURN_BUILTIN)",
        LR"(\\?\HID#VID_0DB0&PID_1902&MI_00&COL01#MSI_POLICY_RETURN_BUILTIN#{4D1E55B2-F16F-11CF-88CB-001111000030})",
        LR"(\\?\USB#VID_0DB0&PID_1902#MSI_POLICY_RETURN_BUILTIN#{EC87F1E3-C13B-4100-B5F7-8B84D54260CB})"};
    const auto file=(root/(L"msi-visibility-"+std::to_wstring(fixtureTargetReturnCaseSerial)+L".json")).wstring();
    msiJointVisibility::configure(devices,hidHideExemptApplicationPaths(),file);
    fixtureMsiJointState::active=true;
    SetLastError(ERROR_SUCCESS);
    const DWORD needed=GetEnvironmentVariableW(L"YMCC_FIXTURE_MSI_VISIBILITY_STATE",nullptr,0);
    const DWORD error=GetLastError();
    if(!needed&&error!=ERROR_ENVVAR_NOT_FOUND&&error!=ERROR_SUCCESS)
        throw std::logic_error("MSI-joint-state-environment-read-failed");
    fixtureMsiJointState::hadEnvironment=needed!=0;
    fixtureMsiJointState::previousEnvironment.clear();
    if(needed){
        std::vector<wchar_t> value(needed);
        const DWORD count=GetEnvironmentVariableW(L"YMCC_FIXTURE_MSI_VISIBILITY_STATE",value.data(),needed);
        if(count>=needed)throw std::logic_error("MSI-joint-state-environment-changed-during-read");
        fixtureMsiJointState::previousEnvironment.assign(value.data(),count);
    }
    if(!SetEnvironmentVariableW(L"YMCC_FIXTURE_MSI_VISIBILITY_STATE",file.c_str()))
        throw std::logic_error("MSI-joint-state-environment-bind-failed");
    fixtureMsiJointState::environmentChanged=true;
    if(!fixtureMsiJointVisibilityBridge::input.declared)
        fixtureMsiJointBindVisibilityCycleBridge(FixtureMsiJointPresence::Present,FixtureMsiJointPresence::Present);
    // These are explicit synthetic CM inputs for both declared control roots.
    // Native XInput availability still depends on the actual MODE1 report file.
}
class FixtureMsiJointStateScope {
    bool enabled_;int exceptions_;
public:
    explicit FixtureMsiJointStateScope(bool enabled):enabled_(enabled),exceptions_(std::uncaught_exceptions()){}
    ~FixtureMsiJointStateScope()noexcept(false){
        if(!enabled_||!fixtureMsiJointState::active)return;
        try{
            if(g_inputHostProcess||!fixtureMsiJointChildrenReturned()||g_msiDinputReturnOwed.load()||
               fixtureMsiJointFeedbackDebt.load(std::memory_order_acquire))
                throw std::logic_error("MSI-joint-state-scope-retains-unreturned-child-or-duty");
            msiJointVisibility::retire();
            fixtureMsiJointObserveState(true);
            fixtureMsiJointState::restoreEnvironment();fixtureMsiJointState::active=false;
        }catch(...){
            if(std::uncaught_exceptions()==exceptions_)throw;
            // Keep the original exception and stable file/environment for a
            // still-owned child. No fabricated cleanup or discarded state.
            try{fixtureMsiJointObserveState(false);}catch(...){}
        }
    }
    FixtureMsiJointStateScope(const FixtureMsiJointStateScope&)=delete;
    FixtureMsiJointStateScope& operator=(const FixtureMsiJointStateScope&)=delete;
};
