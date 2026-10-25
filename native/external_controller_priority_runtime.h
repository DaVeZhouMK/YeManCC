// Included after the native controller lifecycle implementations.
#include "external_controller_priority_policy.h"
#include "external_controller_priority_ledger.h"
#include <usbioctl.h>
#include <usbiodef.h>

struct ExternalPriorityDevice { HidHidePhysicalIdentity source; std::string location; bool internal=false, external=false; };
struct ExternalPriorityRuntime {
    bool loaded=false, storeInvalid=false, enabled=false, active=false, returning=false, takingOver=false, releasePending=false;
    ULONGLONG takeoverSince=0, failedSourceAbsentSince=0;
    std::string lastAttemptRoot,lastAttemptLocation,lastDiscoverySignature;
    bool builtinHidden=false, externalPresent=false;
    std::string state="off", reason="policy-disabled", externalRoot, builtinRoot, builtinLocation;
    ymcc::externalpad::VisibilityDebt debt;
    ymcc::externalpad::Arbitration arbitration;
    ULONGLONG nextTick=0, retryAt=0, auditAt=0, discoveryAt=0;
    ULONGLONG ledgerReadRetryAt=0;
    DWORD ledgerReadRetryDelayMs=1000;
    unsigned discoveryTopology=0;
    std::vector<ExternalPriorityDevice> discovered;
    ymcc::externalpad::Candidate admittedExternal;
    uint64_t power=0;
    HANDLE cycleProcess=nullptr;
    ULONGLONG cycleStarted=0;
    bool cycleRestoring=false, cycleReturnVerified=false;
    unsigned returnCycleAttempts=0;
    uint64_t lastEnabledRevision=0;
};
static void gamepadResetNativeState(bool requireRelease);
static std::mutex g_externalPriorityMx;
static ExternalPriorityRuntime g_externalPriority;
static std::atomic<bool> g_externalPriorityPolicyEnabled{false};
static std::wstring externalPriorityDebtPath(){return app_data_dir()+L"\\external-controller-priority-return-v1.json";}
static bool externalPriorityHasDebt(const ymcc::externalpad::VisibilityDebt& d){return !d.hiddenPaths.empty() || !d.applications.empty() || d.cloakChanged || d.refreshPending;}
static bool externalPriorityDesired(){
    const auto input=ymSettingsSection("input");
    const auto ownership=input.find("ownership");
    if(ownership==input.end() || !ownership->is_object())return false;
    const auto flag=ownership->find("externalControllerPriority");
    return flag!=ownership->end() && flag->is_boolean() && flag->get<bool>();
}
static void externalPriorityStatus(const char* state,const std::string& reason){
    auto& r=g_externalPriority;
    if(r.state==state && r.reason==reason)return;
    r.state=state;r.reason=reason;g_externalPriorityRevision.fetch_add(1,std::memory_order_release);
    json hidden=json::array();for(const auto& p:r.debt.hiddenPaths)hidden.push_back(W2U(p));
    appendNativeLifecycleLog("external-controller-priority",{{"state",state},{"reason",reason},
        {"enabled",r.enabled},{"externalRoot",r.externalRoot},{"builtinRoot",r.builtinRoot},
        {"builtinLocation",r.builtinLocation},{"ownedHiddenPaths",hidden},
        {"virtualSuspended",g_externalPriorityVirtualSuspended.load()},
        {"externalPresent",r.externalPresent},{"builtinHidden",r.builtinHidden},
        {"powerGeneration",currentPowerGeneration()},{"hostEpoch",inputHostEpochToken()},
        {"requestedPersona",inputHostConfiguredPersona()},{"directExternalPassthrough",true},
        {"steamConsumptionProven",false},{"owner","existing-input-lifecycle-worker"}});
}
static json externalControllerPrioritySnapshot(){
    std::unique_lock<std::mutex> lock(g_externalPriorityMx,std::try_to_lock);
    if(!lock.owns_lock())return {{"enabled",g_externalPriorityPolicyEnabled.load(std::memory_order_acquire)},{"state","transitioning"},{"reason","lifecycle-busy"},
        {"virtualSuspended",g_externalPriorityVirtualSuspended.load()},{"externalPresent",false},{"builtinHidden",false}};
    const auto& r=g_externalPriority;
    return {{"enabled",r.enabled},{"state",r.state},{"reason",r.reason},
        {"externalPresent",r.externalPresent},{"builtinHidden",r.builtinHidden},
        {"virtualSuspended",g_externalPriorityVirtualSuspended.load()}};
}
// A USB root on a non-user-connectable port is internal. USB bus/VID/slot0
// alone never establish this. Unsupported IOCTL is unknown, not internal.
static int externalPriorityUsbHopKind(const std::string& root){
    DEVINST node=0,parent=0;auto wide=U2W(root);
    if(CM_Locate_DevNodeW(&node,wide.data(),CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS ||
        CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)return -1;
    ULONG port=0,bytes=sizeof(port),type=0;
    if(CM_Get_DevNode_Registry_PropertyW(node,CM_DRP_ADDRESS,&type,&port,&bytes,0)!=CR_SUCCESS || !port)return -1;
    wchar_t parentId[MAX_DEVICE_ID_LEN]{};
    if(CM_Get_Device_IDW(parent,parentId,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS)return -1;
    HDEVINFO set=SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return -1;
    int result=-1;
    for(DWORD i=0;;++i){
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&GUID_DEVINTERFACE_USB_HUB,i,&iface))break;
        DWORD count=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&count,nullptr);
        if(count<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))continue;
        std::vector<BYTE> storage(count);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA dev{};dev.cbSize=sizeof(dev);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,count,nullptr,&dev))continue;
        const auto id=hidHideDeviceInstanceId(set,dev);
        if(hidHideUpper(id)!=hidHideUpper(W2U(parentId)))continue;
        HANDLE hub=CreateFileW(detail->DevicePath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(hub==INVALID_HANDLE_VALUE)break;
        std::vector<BYTE> info(sizeof(USB_PORT_CONNECTOR_PROPERTIES)+4096);
        auto* props=reinterpret_cast<USB_PORT_CONNECTOR_PROPERTIES*>(info.data());props->ConnectionIndex=port;
        DWORD returned=0;
        if(DeviceIoControl(hub,IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES,props,(DWORD)info.size(),props,(DWORD)info.size(),&returned,nullptr) &&
            returned>=sizeof(USB_PORT_CONNECTOR_PROPERTIES))result=props->UsbPortProperties.PortIsUserConnectable?1:0;
        CloseHandle(hub);break;
    }
    SetupDiDestroyDeviceInfoList(set);return result;
}
// Internal behind an internal hub must also have an internal upstream port.
// A soldered controller inside a removable USB dock is still external.
static int externalPriorityUsbPortKind(const std::string& root){
    std::string current=root;bool observed=false;
    for(unsigned depth=0;depth<16;++depth){
        const auto upper=hidHideUpper(current);
        if(upper.rfind("USB\\ROOT_HUB",0)==0)return observed?0:-1;
        if(upper.rfind("USB\\VID_",0)!=0)return -1;
        const int kind=externalPriorityUsbHopKind(current);if(kind!=0)return kind;
        observed=true;DEVINST node=0,parent=0;auto wide=U2W(current);
        if(CM_Locate_DevNodeW(&node,wide.data(),CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS ||
            CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)return -1;
        wchar_t id[MAX_DEVICE_ID_LEN]{};if(CM_Get_Device_IDW(parent,id,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS)return -1;
        current=W2U(id);
    }
    return -1;
}
static bool externalPriorityKnownHandheld(){
    return g_machineIdentity.resolved && g_machineIdentity.family!=YmccFamily::Unknown &&
        !g_machineIdentity.deviceClass.empty() && !g_machineIdentity.inputHidKey.empty();
}
static bool externalPriorityMsiBuiltin(const HidHidePhysicalIdentity& c){
    return msiClawKnownBoardAuthority() && c.vendorId==0x0DB0 && (c.productId==0x1901 || c.productId==0x1902) &&
        hidHideUpper(c.baseContainerDeviceInstanceId).rfind("USB\\VID_0DB0&PID_",0)==0;
}
static bool externalPrioritySlot(const HidHidePhysicalIdentity& c,unsigned& slot,unsigned* topologyOut=nullptr,uint64_t* powerOut=nullptr){
    const auto sampledTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto sampledPower=currentPowerGeneration();
    if(g_powerLifecycle.load()!=PowerLifecycle::Ready)return false;
    slot=4;if(c.baseContainerDevicePath.empty() || !ymccOpenXInputAvailable() || !g_ymccOpenXInputGetUserIndex)return false;
    BYTE index=255;
    if(g_ymccOpenXInputGetUserIndex(U2W(c.baseContainerDevicePath).c_str(),&index)!=0 || index>=4)return false;
    XINPUT_STATE state{};if(ymccXInputGetState14(index,&state)!=ERROR_SUCCESS)return false;
    // Reverse binding after the read guards a topology reorder during sampling.
    wchar_t path[4096]{};DWORD count=4096;
    if(!g_ymccOpenXInputGetDevicePath || g_ymccOpenXInputGetDevicePath(index,path,&count)!=0 || count>=4096 ||
        hidHideUpper(W2U(path))!=hidHideUpper(c.baseContainerDevicePath))return false;
    if(g_powerLifecycle.load()!=PowerLifecycle::Ready || sampledPower!=currentPowerGeneration() ||
        sampledTopology!=g_xinputTopologyEpoch.load(std::memory_order_acquire))return false;
    slot=index;if(topologyOut)*topologyOut=sampledTopology;if(powerOut)*powerOut=sampledPower;return true;
}
// gamepadReadState owns this same mutex while choosing the canonical slot.
// Publish the entire tuple under it; four independent atomics alone can mix
// a new mask with old context during same-generation slot reordering.
static bool externalPriorityPublishSource(const ymcc::externalpad::Candidate& c,ULONGLONG now){
    std::lock_guard<std::mutex> sampleLock(g_gamepadReadMx);
    if(c.topology!=g_xinputTopologyEpoch.load(std::memory_order_acquire) ||
        c.power!=currentPowerGeneration() || g_powerLifecycle.load()!=PowerLifecycle::Ready)return false;
    g_externalPrioritySourceMask=1u<<c.slot;
    g_externalPrioritySourceDeadline=now+1200;
    g_externalPrioritySourceTopology=c.topology;
    g_externalPrioritySourcePower=c.power;
    return true;
}
static bool externalPriorityFreshCandidate(const ymcc::externalpad::Candidate& c){
    std::vector<HidHidePhysicalIdentity> devices;
    if(!hidHideResolvePhysicalIdentities(devices))return false;
    for(const auto& d:devices)if(hidHideUpper(d.baseContainerDeviceInstanceId)==c.root){
        unsigned slot=4;const auto locations=msiForegroundIsolationLocations(d.baseContainerDeviceInstanceId);
        if(!externalPrioritySlot(d,slot) || slot!=c.slot || locations.empty() ||
            hidHideUpper(W2U(locations.front()))!=c.location)return false;
        HidHideVisibilitySnapshot visibility;
        if(!hidHideCaptureVisibilitySnapshotViaCli(visibility) || visibility.inverseRaw.find("--inv-off")==std::string::npos)return false;
        for(const auto& p:hidHidePhysicalTargets(d))if(hidHideContainsPath(visibility.hidden,p))return false;
        return true;
    }
    return false;
}
static bool externalPriorityCommit(const ymcc::externalpad::VisibilityDebt& next){
    std::string error;
    if(!ymcc::externalpad::saveDebt(externalPriorityDebtPath(),next,error)){externalPriorityStatus("degraded","ownership-save-failed:"+error);return false;}
    g_externalPriority.debt=next;return true;
}
static bool externalPriorityCommand(const std::wstring& verb,const std::wstring& path={}){
    const bool restoring=verb==L"--dev-unhide" || verb==L"--app-unreg" || verb==L"--cloak-off";
    if(!hardwareWriteAllowed() || (!restoring && (!externalPriorityDesired() || inputHostExitFenceRaised() ||
        g_powerLifecycle.load()!=PowerLifecycle::Ready || g_externalPriority.power!=currentPowerGeneration())))return false;
    if(path.find(L'"')!=std::wstring::npos || path.find(L'\n')!=std::wstring::npos || path.find(L'\r')!=std::wstring::npos)return false;
    DWORD code=1;return hidHideRun(verb+(path.empty()?L"":L" \""+path+L"\""),nullptr,&code) && code==ERROR_SUCCESS;
}
// Starts one existing HC/Nefarius port refresh without creating a virtual Host.
// Polling stays on the existing lifecycle owner; an unresolved child keeps duty.
static bool externalPriorityCycle(const std::string& root,const std::string& location,bool restoring){
    auto& r=g_externalPriority;
    if(r.cycleProcess || root.empty() || location.empty() || root.find('"')!=std::string::npos || location.find('"')!=std::string::npos)return false;
    const auto exe=inputHostExecutablePath();
    auto command=L"\""+exe+L"\" --external-controller-priority-cycle \""+U2W(root)+L"\" --location \""+U2W(location)+
        L"\" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --once";
    std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
    const auto dir=fspath::path(exe).parent_path().wstring();
    if(!ymcc::processEfficiency::createProcess(exe.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi))return false;
    CloseHandle(pi.hThread);r.cycleProcess=pi.hProcess;r.cycleRestoring=restoring;r.cycleStarted=GetTickCount64();return true;
}
static bool externalPriorityRestore(){
    auto& r=g_externalPriority;HidHideVisibilitySnapshot before;
    if(!externalPriorityHasDebt(r.debt))return true;
    if(r.debt.refreshPending && r.debt.hiddenPaths.empty() && r.debt.applications.empty() &&
        !r.debt.cloakChanged && !r.cycleReturnVerified && r.returnCycleAttempts>=2)return false;
    if(!hidHideCaptureVisibilitySnapshotViaCli(before))return false;
    bool ok=true;
    for(const auto& p:r.debt.hiddenPaths)if(hidHideContainsPath(before.hidden,W2U(p)))ok=externalPriorityCommand(L"--dev-unhide",p)&&ok;
    for(const auto& p:r.debt.applications)if(hidHideContainsPath(before.applications,W2U(p)))ok=externalPriorityCommand(L"--app-unreg",p)&&ok;
    HidHideVisibilitySnapshot after;if(!hidHideCaptureVisibilitySnapshotViaCli(after))return false;
    for(const auto& p:r.debt.hiddenPaths)ok=!hidHideContainsPath(after.hidden,W2U(p))&&ok;
    for(const auto& p:r.debt.applications)ok=!hidHideContainsPath(after.applications,W2U(p))&&ok;
    if(!ok)return false;
    if(r.debt.cloakChanged && after.hidden.empty()){
        if(!externalPriorityCommand(L"--cloak-off") || !hidHideCaptureVisibilitySnapshotViaCli(after) || after.cloaking)return false;
    } // foreign hides retain the current global cloak; never disable their protection
    r.builtinHidden=false;r.active=false;
    // Configuration returned is separate from stale consumer handles. Keep the
    // durable refresh duty until the exact-root refresh has a terminal receipt.
    auto returned=r.debt;returned.hiddenPaths.clear();returned.applications.clear();returned.cloakChanged=false;
    if(!externalPriorityCommit(returned))return false;
    if(returned.refreshPending && !r.cycleReturnVerified){
        if(r.cycleProcess)return false;
        if(r.returnCycleAttempts>=2)return false;
        const auto locations=msiForegroundIsolationLocations(returned.builtinRoot);
        if(locations.empty() || hidHideUpper(W2U(locations.front()))!=hidHideUpper(returned.locationPath))return false;
        ++r.returnCycleAttempts;
        externalPriorityCycle(returned.builtinRoot,returned.locationPath,true);return false;
    }
    ymcc::externalpad::VisibilityDebt clean;
    return externalPriorityCommit(clean);
}
static void externalPriorityEnd(const char* reason){
    auto& r=g_externalPriority;r.returning=true;r.active=false;r.takingOver=false;
    g_externalPrioritySourceMask=0;g_externalPrioritySourceDeadline=0;
    externalPriorityStatus("returning",reason);
}
static bool externalPriorityHide(const HidHidePhysicalIdentity& builtin){
    auto& r=g_externalPriority;HidHideVisibilitySnapshot before;
    if(!hidHideCaptureVisibilitySnapshotViaCli(before) || !before.valid || before.inverseRaw.find("--inv-off")==std::string::npos)return false;
    auto next=r.debt;next.builtinRoot=builtin.baseContainerDeviceInstanceId;
    // Pre-existing hides are not ours and cannot be borrowed across an OFF return.
    for(const auto& path:hidHidePhysicalTargets(builtin))if(hidHideContainsPath(before.hidden,path))return false;
    const auto module=hidHideCurrentModulePath();if(module.empty())return false;
    if(!hidHideContainsPath(before.applications,W2U(module)))next.applications.push_back(module);
    for(const auto& path:hidHidePhysicalTargets(builtin))next.hiddenPaths.push_back(U2W(path));
    next.cloakChanged=!before.cloaking;
    next.refreshPending=true;next.locationPath=r.builtinLocation;
    if(!externalPriorityCommit(next))return false; // write-ahead of every actual mutation
    bool ok=true;
    for(const auto& app:next.applications)ok=externalPriorityCommand(L"--app-reg",app)&&ok;
    if(!before.cloaking)ok=externalPriorityCommand(L"--cloak-on")&&ok;
    for(const auto& path:next.hiddenPaths)ok=externalPriorityCommand(L"--dev-hide",path)&&ok;
    HidHideVisibilitySnapshot after;
    ok=hidHideCaptureVisibilitySnapshotViaCli(after)&&ok;
    for(const auto& p:next.hiddenPaths)ok=hidHideContainsPath(after.hidden,W2U(p))&&ok;
    ok=ok && after.valid && after.cloaking && after.inverseRaw.find("--inv-off")!=std::string::npos &&
        hidHideContainsPath(after.applications,W2U(module));
    if(!ok)return false;
    r.builtinHidden=true;r.builtinRoot=builtin.baseContainerDeviceInstanceId;
    return externalPriorityCycle(r.builtinRoot,r.builtinLocation,false);
}
static void externalControllerPriorityMaintenanceImpl(){
    std::unique_lock<std::mutex> lock(g_externalPriorityMx,std::try_to_lock);if(!lock.owns_lock())return;
    auto& r=g_externalPriority;const auto now=GetTickCount64();if(now<r.nextTick)return;r.nextTick=now+500;
    const bool enabledNow=externalPriorityDesired();
    g_externalPriorityPolicyEnabled.store(enabledNow,std::memory_order_release);
    if(r.enabled!=enabledNow){r.enabled=enabledNow;r.returnCycleAttempts=0;r.lastAttemptRoot.clear();r.lastAttemptLocation.clear();r.arbitration.reset();g_externalPriorityRevision.fetch_add(1,std::memory_order_release);}
    if(r.power!=currentPowerGeneration()){r.power=currentPowerGeneration();r.returnCycleAttempts=0;r.auditAt=0;r.discoveryAt=0;r.lastAttemptRoot.clear();r.lastAttemptLocation.clear();}
    if(r.storeInvalid)return; // Invalid ownership records are never repaired or cleared here.
    if(!r.loaded){
        if(now<r.ledgerReadRetryAt)return;
        std::string error;
        ymcc::externalpad::VisibilityDebt candidate;
        ymcc::externalpad::DebtLoadFailure failure;
        if(!ymcc::externalpad::loadDebt(externalPriorityDebtPath(),candidate,error,failure)){
            // Unknown debt continues to block publication, including with policy OFF.
            // Only a successful read may establish ownership; never save/restore a guess.
            g_externalPriorityVirtualSuspended=true;
            if(failure==ymcc::externalpad::DebtLoadFailure::RetryableIo){
                r.ledgerReadRetryAt=now+r.ledgerReadRetryDelayMs;
                r.ledgerReadRetryDelayMs=r.ledgerReadRetryDelayMs>=15000?30000:r.ledgerReadRetryDelayMs*2;
                externalPriorityStatus("degraded","ownership-read-retry:"+error);
            }else{
                r.storeInvalid=true;
                externalPriorityStatus("degraded","ownership-read-failed:"+error);
            }
            return;
        }
        const bool readRecovered=r.ledgerReadRetryAt!=0;
        r.debt=std::move(candidate);r.loaded=true;
        r.ledgerReadRetryAt=0;r.ledgerReadRetryDelayMs=1000;
        if(externalPriorityHasDebt(r.debt)){
            g_externalPriorityVirtualSuspended=true;externalPriorityEnd("startup-owned-return");
        }else if(readRecovered){
            // Even an empty recovery passes the existing power/write/return gates
            // before the existing returning branch releases publication.
            externalPriorityEnd("startup-ledger-read-recovered");
        }
    }
    if(inputHostExitFenceRaised()){if(externalPriorityHasDebt(r.debt))externalPriorityEnd("app-exit");else return;}
    if(g_powerLifecycle.load()!=PowerLifecycle::Ready || !g_inputReady.load()){
        // Sleep doesn't destroy the preference or manufacture an unplug.
        if(r.active)externalPriorityStatus("suspended","power-not-ready");return;
    }
    if(!hardwareWriteAllowed()){externalPriorityStatus("waiting","hardware-write-gate-closed");return;}
    if(r.cycleProcess){
        const auto wait=WaitForSingleObject(r.cycleProcess,0);
        if(wait!=WAIT_OBJECT_0){
            const bool overdue=now-r.cycleStarted>=8000;
            if(!r.enabled || inputHostExitFenceRaised() || (!r.cycleRestoring && overdue))
                externalPriorityEnd("pending-port-refresh-owned-return-required");
            // Return our configuration while retaining this exact child and its
            // refresh duty. Restore cannot launch another cycle while it exists.
            if(r.returning && !r.releasePending && now>=r.retryAt){
                r.retryAt=now+3000;externalPriorityRestore();
            }
            if(overdue)externalPriorityStatus("degraded","port-refresh-receipt-pending-duty-retained");
            return;
        }
        DWORD code=1;GetExitCodeProcess(r.cycleProcess,&code);CloseHandle(r.cycleProcess);r.cycleProcess=nullptr;
        if(!r.cycleRestoring && code!=0)externalPriorityEnd(("builtin-port-refresh-failed:"+std::to_string(code)).c_str());
        else if(!r.cycleRestoring){
            if(r.returning || !r.enabled || !externalPriorityFreshCandidate(r.admittedExternal))externalPriorityEnd("external-unavailable-after-builtin-refresh");
            else {r.active=true;r.auditAt=0;externalPriorityStatus("active","external-ready-builtin-hidden-virtual-removed");}
        }
        else if(code!=0)externalPriorityStatus("degraded","return-port-refresh-failed:"+std::to_string(code));
        else r.cycleReturnVerified=true;
    }
    if(r.returning){
        if(r.releasePending){
            if(inputHostHasLocalLifecycleState() || g_inputHostReleaseQuarantined){
                externalPriorityStatus("degraded","virtual-release-still-unverified");return;
            }
            r.releasePending=false;
        }
        if(now<r.retryAt)return;r.retryAt=now+3000;
        if(!externalPriorityRestore()){externalPriorityStatus("degraded","owned-visibility-return-unverified");return;}
        if(r.cycleProcess)return;
        r.returning=false;r.arbitration.reset();r.retryAt=now+3000;
        g_externalPriorityVirtualSuspended=false;g_externalPrioritySourceMask=0;
        {std::lock_guard<std::mutex> bus(g_inputCaptureBusTickMx);inputCaptureRefreshRealStickSettings(true);}
        gamepadResetNativeState(true);
        // Original source identity admission resumes; no external identity is installed into it.
        xinputSlotsRequestIdentityReprobe(0xFu,"external-priority-return");
        msiNativeForegroundUiEvent(true);
        externalPriorityStatus(r.enabled?"waiting":"off",r.enabled?"waiting-external-controller":"policy-disabled");return;
    }
    if(!r.enabled){
        if(r.active || g_externalPriorityVirtualSuspended.load())externalPriorityEnd("policy-disabled");
        else externalPriorityStatus("off","policy-disabled");return;
    }
    if(!externalPriorityKnownHandheld()){externalPriorityStatus("waiting","builtin-capability-unavailable");return;}
    if(now<r.retryAt)return;
    std::vector<HidHidePhysicalIdentity> devices;
    const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    if(now>=r.discoveryAt || r.discoveryTopology!=topology){
        r.discoveryAt=now+2000;r.discoveryTopology=topology;r.discovered.clear();
        if(hidHideResolvePhysicalIdentities(devices))for(const auto& c:devices){
            if(!c.resolved || hidHideIsVirtualIdentity(c.deviceInstanceId,c.baseContainerDeviceInstanceId))continue;
            const auto root=hidHideUpper(c.baseContainerDeviceInstanceId);
            const auto locations=msiForegroundIsolationLocations(c.baseContainerDeviceInstanceId);
            if(locations.empty())continue;
            const int port=root.rfind("USB\\",0)==0?externalPriorityUsbPortKind(c.baseContainerDeviceInstanceId):-1;
            const bool internal=externalPriorityMsiBuiltin(c) || port==0;
            r.discovered.push_back({c,hidHideUpper(W2U(locations.front())),internal,!internal && (port==1 || root.rfind("BTH",0)==0)});
        }
    }
    json observed=json::array();
    for(const auto& d:r.discovered)observed.push_back({{"root",d.source.baseContainerDeviceInstanceId},
        {"instance",d.source.deviceInstanceId},{"location",d.location},{"vid",d.source.vendorId},{"pid",d.source.productId},
        {"role",d.internal?"builtin":(d.external?"external":"unknown")}});
    const auto signature=observed.dump();
    if(signature!=r.lastDiscoverySignature){
        r.lastDiscoverySignature=signature;
        appendNativeLifecycleLog("external-controller-priority-topology",{{"devices",observed},
            {"deviceClass",g_machineIdentity.deviceClass},{"builtinAuthority","known-board-plus-exact-MSI-family-or-entire-internal-USB-port-chain"},
            {"slotAndBrandAloneAccepted",false},{"powerGeneration",currentPowerGeneration()}});
    }
    std::vector<HidHidePhysicalIdentity> builtins;std::vector<ymcc::externalpad::Candidate> externals;
    for(const auto& d:r.discovered){
        if(d.internal){builtins.push_back(d.source);continue;}
        unsigned slot=4,readTopology=0;uint64_t readPower=0;
        if(d.external && externalPrioritySlot(d.source,slot,&readTopology,&readPower))
            externals.push_back({hidHideUpper(d.source.baseContainerDeviceInstanceId),d.location,slot,true,true,true,readTopology,readPower});
    }
    const bool failedStillPresent=std::any_of(externals.begin(),externals.end(),[&](const auto& c){return c.root==r.lastAttemptRoot && c.location==r.lastAttemptLocation;});
    if(!r.lastAttemptRoot.empty() && !failedStillPresent){
        if(!r.failedSourceAbsentSince)r.failedSourceAbsentSince=now;
        if(now-r.failedSourceAbsentSince>=2000){r.lastAttemptRoot.clear();r.lastAttemptLocation.clear();}
    }else r.failedSourceAbsentSince=0;
    const auto decision=r.arbitration.observe(true,true,currentPowerGeneration(),now,externals);
    r.externalPresent=decision.available;
    if(decision.release){if(r.active || g_externalPriorityVirtualSuspended.load())externalPriorityEnd("external-disconnected");return;}
    if(r.takingOver && now-r.takeoverSince>=8000){externalPriorityEnd("builtin-return-source-timeout");return;}
    if(!decision.available){
        if(r.active)externalPriorityStatus("holding","external-source-grace");
        else if(!r.takingOver)externalPriorityStatus("waiting",externals.empty()?"waiting-verified-external-xinput":"confirming-external-controller");
        return;
    }
    const bool selectedChanged=r.admittedExternal.root!=decision.selected.root ||
        r.admittedExternal.location!=decision.selected.location || r.admittedExternal.slot!=decision.selected.slot;
    if((!r.active || selectedChanged) && !externalPriorityFreshCandidate(decision.selected)){
        if(r.active)externalPriorityEnd("external-read-or-visibility-unverified");else externalPriorityStatus("waiting","external-read-or-visibility-unverified");return;
    }
    // The sample retains the context observed around its APIs. Never attach a
    // newer context to an old slot after a blocking read or visibility check.
    if(decision.selected.topology!=g_xinputTopologyEpoch.load(std::memory_order_acquire) ||
        decision.selected.power!=currentPowerGeneration() || g_powerLifecycle.load()!=PowerLifecycle::Ready){
        g_externalPrioritySourceDeadline=0;return;
    }
    if(r.externalRoot!=decision.selected.root){gamepadResetNativeState(true);g_externalPriorityRevision.fetch_add(1,std::memory_order_release);}
    if(r.admittedExternal.root!=decision.selected.root || r.admittedExternal.location!=decision.selected.location || r.admittedExternal.slot!=decision.selected.slot){
        appendNativeLifecycleLog("external-controller-priority-source",{{"root",decision.selected.root},
            {"location",decision.selected.location},{"slot",decision.selected.slot},{"physicalIdentityVerified",true},
            {"readSucceeded",true},{"sampleMayBeNeutral",true},{"topologyGeneration",g_xinputTopologyEpoch.load()},
            {"powerGeneration",currentPowerGeneration()},{"steamConsumptionProven",false}});
    }
    r.externalRoot=decision.selected.root;r.admittedExternal=decision.selected;
    if(!externalPriorityPublishSource(decision.selected,now))return;
    if(r.active){
        if(now>=r.auditAt){
            r.auditAt=now+5000;HidHideVisibilitySnapshot snap;
            bool valid=hidHideCaptureVisibilitySnapshotViaCli(snap)&&snap.cloaking&&snap.inverseRaw.find("--inv-off")!=std::string::npos;
            for(const auto& p:r.debt.hiddenPaths)valid=hidHideContainsPath(snap.hidden,W2U(p))&&valid;
            bool externalVisible=false;
            for(const auto& d:r.discovered)if(hidHideUpper(d.source.baseContainerDeviceInstanceId)==r.externalRoot){
                externalVisible=true;
                for(const auto& p:hidHidePhysicalTargets(d.source))externalVisible=!hidHideContainsPath(snap.hidden,p)&&externalVisible;
            }
            if(!valid || !externalVisible){externalPriorityEnd(!externalVisible?"external-visibility-readback-changed":"builtin-suppression-readback-changed");return;}
        }
        externalPriorityStatus("active","external-ready-builtin-hidden-virtual-removed");return;
    }
    if(!r.takingOver){
        if(r.lastAttemptRoot==decision.selected.root && r.lastAttemptLocation==decision.selected.location){
            externalPriorityStatus("degraded","takeover-attempt-held-until-reconnect-policy-toggle-or-power");return;
        }
        if(builtins.size()!=1){externalPriorityStatus("waiting","builtin-identity-unknown-or-ambiguous");return;}
        const auto selectedBuiltin=builtins.front();
        const auto locations=msiForegroundIsolationLocations(selectedBuiltin.baseContainerDeviceInstanceId);
        if(locations.empty() || msiForegroundIsolationBorrowBusy()){externalPriorityStatus("waiting","builtin-visibility-owner-busy");return;}
        r.builtinRoot=selectedBuiltin.baseContainerDeviceInstanceId;r.builtinLocation=W2U(locations.front());
        r.lastAttemptRoot=decision.selected.root;r.lastAttemptLocation=decision.selected.location;
        r.takingOver=true;r.takeoverSince=now;
        g_externalPriorityVirtualSuspended=true;externalPriorityStatus("taking-over","verified-external-takeover");
        // Freeze effective output first, then canonical neutral/release/owned return.
        {std::lock_guard<std::mutex> bus(g_inputCaptureBusTickMx);inputCaptureRefreshRealStickSettings(true);}
        gamepadResetNativeState(true);
        if(inputHostHasLocalLifecycleState())inputHostStop("coordinator-stop");
        r.takeoverSince=GetTickCount64(); // separate endpoint/source arrival budget after stop
        if(inputHostHasLocalLifecycleState() || g_inputHostReleaseQuarantined || inputHostExitFenceRaised()){
            r.releasePending=inputHostHasLocalLifecycleState() || g_inputHostReleaseQuarantined;
            externalPriorityEnd("virtual-release-unverified");return;
        }
    }
    // MSI normal stop can switch PID1902 ->1901. Re-resolve the *same USB port*,
    // never the old mode2 instance, and never apply MSI writes to another family.
    devices.clear();hidHideResolvePhysicalIdentities(devices);builtins.clear();
    for(const auto& c:devices){
        const auto current=msiForegroundIsolationLocations(c.baseContainerDeviceInstanceId);
        if(!c.resolved || current.empty() || hidHideUpper(W2U(current.front()))!=hidHideUpper(r.builtinLocation))continue;
        if(hidHideUpper(c.baseContainerDeviceInstanceId)==hidHideUpper(r.builtinRoot) || externalPriorityMsiBuiltin(c))builtins.push_back(c);
    }
    if(builtins.empty()){
        externalPriorityStatus("taking-over","waiting-same-port-builtin-source-after-release");return;
    }
    r.takingOver=false;r.cycleReturnVerified=false;r.returnCycleAttempts=0;
    if(builtins.size()!=1 || !externalPriorityDesired() || !externalPriorityFreshCandidate(decision.selected) ||
        g_powerLifecycle.load()!=PowerLifecycle::Ready || !externalPriorityHide(builtins.front())){
        externalPriorityEnd("takeover-failed-returning-own-paths");return;
    }
    r.nextTick=0;externalPriorityStatus("taking-over","waiting-builtin-port-refresh");
}
static void externalControllerPriorityMaintenance() noexcept {
    try { externalControllerPriorityMaintenanceImpl(); }
    catch (...) {
        // Settings mutex/file/provider failures must never tear down the shared
        // capture worker, especially with the external policy disabled.
        try {
            std::lock_guard<std::mutex> lock(g_externalPriorityMx);
            auto& r=g_externalPriority;r.nextTick=GetTickCount64()+3000;
            g_externalPrioritySourceDeadline=0;
            if(r.takingOver){
                g_externalPriorityVirtualSuspended=true;
                externalPriorityEnd("maintenance-provider-fault-owned-return-required");
            } else externalPriorityStatus("degraded","maintenance-provider-fault-state-retained");
        } catch (...) { /* diagnostic failures cannot terminate common capture */ }
    }
}
static void externalControllerPriorityShutdown(){
    std::lock_guard<std::mutex> lock(g_externalPriorityMx);auto& r=g_externalPriority;
    g_externalPrioritySourceMask=0;
    if(r.cycleProcess){
        if(WaitForSingleObject(r.cycleProcess,5000)!=WAIT_OBJECT_0){
            externalPriorityEnd("exit-cycle-pending-duty-retained");
            if(!r.releasePending)externalPriorityRestore();
            externalPriorityStatus("degraded","exit-cycle-pending-duty-retained");return;
        }
        DWORD code=1;const bool read=GetExitCodeProcess(r.cycleProcess,&code)!=FALSE;
        if(r.cycleRestoring && read && code==0)r.cycleReturnVerified=true;
        CloseHandle(r.cycleProcess);r.cycleProcess=nullptr;
    }
    if(externalPriorityHasDebt(r.debt)){
        r.returning=true;if(!externalPriorityRestore())externalPriorityStatus("degraded","exit-return-unverified-duty-retained");
        else externalPriorityStatus("off","exit-owned-configuration-returned");
    }
}
