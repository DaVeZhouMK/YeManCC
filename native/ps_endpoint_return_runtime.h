#pragma once
// Included after the existing read-only owned-PS reader. Uses the same serialized
// Host/return owner; no timer, Start/retry authority or machine-capability policy.
static bool psRetiredFamilyAbsent() {
    // Without an accepted owned root, only complete absence of this immutable
    // output profile can prove return. Presence never invents ownership.
    HDEVINFO set=SetupDiGetClassDevsW(&kYeManGuidDeviceInterfaceHid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return false;
    struct Close {HDEVINFO value;~Close(){SetupDiDestroyDeviceInfoList(value);}} close{set};
    for(DWORD index=0;index<256;++index){
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&kYeManGuidDeviceInterfaceHid,index,&iface))return GetLastError()==ERROR_NO_MORE_ITEMS;
        DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);
        if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>65536)return false;
        std::vector<unsigned char> storage(bytes);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,&info))return false;
        const auto family=[](std::wstring id){for(auto& ch:id)ch=static_cast<wchar_t>(towupper(ch));
            return id.find(L"VID_054C")!=std::wstring::npos&&id.find(L"PID_0DF2")!=std::wstring::npos;};
        // Fixed output profile is negative evidence only. Device paths alone
        // may omit hardware IDs, so require a complete CM ancestor walk as well.
        if(family(detail->DevicePath))return false;
        DEVINST node=info.DevInst,visited[16]{};bool complete=false;
        for(unsigned depth=0;depth<16;++depth){
            for(unsigned i=0;i<depth;++i)if(visited[i]==node)return false;visited[depth]=node;
            wchar_t id[512]{};if(CM_Get_Device_IDW(node,id,512,0)!=CR_SUCCESS||!id[0])return false;
            if(family(id))return false;
            DEVINST parent=0;const auto result=CM_Get_Parent(&parent,node,0);
            if(result==CR_NO_SUCH_DEVNODE){complete=true;break;}
            if(result!=CR_SUCCESS)return false;node=parent;
        }
        if(!complete)return false;
    }
    return false; // incomplete/truncated census stays unknown
}
static bool psRetiredReturnProvenLocked() {
    if(!g_psEndpointOwnership.retirementRequired())return true;
    const auto debt=g_psEndpointOwnership.retirement();
    if(g_inputHostProcess||!debt.owner.epoch||debt.owner.epoch!=g_inputHostEpoch||debt.owner.runId!=g_inputHostRunId)return false;
    bool absent=false;const char* basis="profile-negative-census";const char* verdict="unknown";
    if(debt.target.valid){
        const auto probe=psWakeProbeOwnEndpoint(debt.target);basis="exact-retired-SDK-root";
        absent=probe.enumerationComplete&&!probe.queryError&&(probe.kind==PsWakeProbe::Absent||probe.kind==PsWakeProbe::Foreign);
        verdict=absent?"absent":probe.kind==PsWakeProbe::Present?"present":"unknown";
    }else {absent=psRetiredFamilyAbsent();verdict=absent?"absent":"unknown-or-profile-present";}
    // Diagnostics are edges on this existing owner, not a periodic scan log.
    // These fields never authorize return and never change its polling budget.
    static std::string lastRun;static std::uint64_t lastEpoch=0,checks=0;static const char* lastBasis="",*lastVerdict="";
    try{
        if(lastRun!=debt.owner.runId||lastEpoch!=debt.owner.epoch){lastRun=debt.owner.runId;lastEpoch=debt.owner.epoch;lastBasis="";lastVerdict="";checks=0;}
        ++checks;if(std::strcmp(lastBasis,basis)==0&&std::strcmp(lastVerdict,verdict)==0)return absent;
        lastBasis=basis;lastVerdict=verdict;
        appendNativeLifecycleLog("input-host-ps-return-proof",{{"runId",debt.owner.runId},{"hostEpoch",debt.owner.epoch},{"bindingGeneration",debt.target.generation},{"basis",basis},{"verdict",verdict},{"ownedRootAvailable",debt.target.valid},{"completeAbsence",absent},{"xinputPlaneSubstituted",false},{"provesGameInput",false},{"checksSincePreviousVerdict",checks}});checks=0;}catch(...){}
    return absent; // keep the exact debt until physical restore/hold also succeeds
}
