#pragma once
#include "rog_fse_experiment_policy.h"
#include "physical_source_reader_bridge.h"
#include <cfgmgr32.h>

// VC-FSE-READER-20261026-R1. Experimental SOURCE adapter. It never creates,
// stops or owns a virtual target. Capability-library identity and the existing
// pinned physical XInput source stay authoritative. Marker absent = old route.
// Full independent triggers are retained; no combined-Z DirectInput inference.
static std::mutex g_physicalReaderContextMx;
static std::unique_ptr<ymcc::physicalreader::Bridge> g_physicalReaderBridge;
static ymcc::physicalreader::Context g_physicalReaderContext;
static unsigned g_physicalReaderIdentityEpoch=0;
static bool physicalSourceReaderCandidateEnabled() noexcept {
#if !YMCC_ROG_FSE_EXPERIMENTS
    return false; // An old marker cannot arm a normal mainline build.
#else
    static const bool enabled=[]() noexcept {
        wchar_t module[32768]{};
        const DWORD n=GetModuleFileNameW(nullptr,module,static_cast<DWORD>(std::size(module)));
        if(!n || n>=std::size(module))return false;
        const auto marker=std::filesystem::path(module).parent_path()/L"virtual-input-fse-reader.flag";
        const DWORD attr=GetFileAttributesW(marker.c_str());
        return attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY);
    }();
    return enabled; // Experimental build only: remove marker while closed, restart.
#endif
}
static void physicalSourceReaderStop() noexcept {
    std::unique_ptr<ymcc::physicalreader::Bridge> retired;
    {std::lock_guard<std::mutex> lock(g_physicalReaderContextMx);
        g_physicalReaderContext={};g_physicalReaderIdentityEpoch=0;
        retired=std::move(g_physicalReaderBridge);}
    if(retired)retired->stop();
}
static void physicalSourceReaderTick() noexcept {
    if(!physicalSourceReaderCandidateEnabled())return;
    try {
        using ymcc::nativeForeground::Profile;
        ymcc::physicalreader::Context next;
        unsigned identityEpoch=0;
        std::string reason="inactive";
        std::wstring observedSlotPath,selectedRootPath,selectedHidPath,resolvedHidAlias;
        std::string pathBinding="unproven";bool hidAliasQueried=false;CONFIGRET hidAliasResult=CR_SUCCESS;
        const bool basic=nativeForegroundProfile()==Profile::Rog && g_realStickTest.enabled &&
            g_realStickTest.virtualTargetEnabled && !g_exitRequested.load(std::memory_order_acquire) &&
            g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
            !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) && !physicalReleaseWindowActive();
        bool fseKnown=false,fseActive=false;
        if(basic) {
            const auto platform=ymcc::bootmode::readPlatform();
            fseKnown=platform.known && platform.activeKnown;fseActive=fseKnown && platform.active;
            reason=fseKnown?(fseActive?"physical-source-pending":"outside-fse"):"fse-unknown";
            if(fseActive) {
                unsigned long long epoch=0;
                {std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);
                    if(!host.owns_lock())return;
                    if(g_inputHostPrepared && !g_inputHostTransportFault && inputHostProcessAliveLocked())epoch=g_inputHostEpoch;}
                if(epoch) {
                    DWORD slot=4;
                    {std::unique_lock<std::mutex> sampling(g_gamepadReadMx,std::try_to_lock);
                        if(!sampling.owns_lock())return;
                        const DWORD candidate=g_xinputPrimarySlot;
                        if(candidate<4 && g_xinputPhysicalSourceLocked &&
                            (g_xinputPhysicalSourceMask&(1u<<candidate)) &&
                            (g_xinputConnectedMask&(1u<<candidate)) &&
                            !(g_xinputVirtualEndpointMask&(1u<<candidate)) &&
                            !(g_xinputSlotUnknownMask&(1u<<candidate)))slot=candidate;
                        identityEpoch=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);}
                    if(slot<4) {
                        HidHidePhysicalIdentity physical;
                        {std::unique_lock<std::mutex> identity(g_hidHidePhysicalIdentityMx,std::try_to_lock);
                            if(!identity.owns_lock())return;physical=g_hidHidePhysicalIdentity;}
                        const auto slotPath=ymccOpenXInputGetDevicePath(slot);
                        const auto rootPath=U2W(physical.baseContainerDevicePath);
                        const auto hidPath=U2W(physical.sourcePath);
                        observedSlotPath=slotPath;selectedRootPath=rootPath;selectedHidPath=hidPath;
                        bool exact=false;
                        if(physical.resolved && !slotPath.empty() &&
                           !hidHideIsVirtualIdentity(W2U(slotPath),physical.baseContainerDeviceInstanceId)) {
                            exact=(!rootPath.empty() && _wcsicmp(rootPath.c_str(),slotPath.c_str())==0) ||
                                  (!hidPath.empty() && _wcsicmp(hidPath.c_str(),slotPath.c_str())==0);
                            if(exact)pathBinding="exact-interface-path";
                            else if(!hidPath.empty()) {
                                // R2 field evidence: XInput and HID publish distinct interface GUIDs.
                                // Ask Windows for the same-devnode/reference-string HID alias;
                                // never infer identity by VID/PID, slot or stripping the GUID.
                                GUID hidInterface={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};
                                std::array<wchar_t,4096> alias{};ULONG characters=static_cast<ULONG>(alias.size());
                                hidAliasQueried=true;
                                hidAliasResult=CM_Get_Device_Interface_AliasW(slotPath.c_str(),&hidInterface,alias.data(),&characters,0);
                                if(hidAliasResult==CR_SUCCESS && characters>0 && characters<=alias.size()) {
                                    const auto limit=alias.begin()+characters;
                                    const auto end=std::find(alias.begin(),limit,L'\0');
                                    if(end!=limit) {
                                        resolvedHidAlias.assign(alias.begin(),end);
                                        exact=!resolvedHidAlias.empty() && _wcsicmp(resolvedHidAlias.c_str(),hidPath.c_str())==0;
                                        if(exact)pathBinding="windows-interface-alias";
                                    }
                                }
                            }
                        }
                        if(exact) {
                            wchar_t module[32768]{};const DWORD n=GetModuleFileNameW(nullptr,module,static_cast<DWORD>(std::size(module)));
                            FILETIME birth{},exit{},kernel{},user{};
                            if(n && n<std::size(module) && GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)) {
                                next.desired=true;next.parentPid=GetCurrentProcessId();
                                next.parentBirth=(static_cast<std::uint64_t>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
                                next.power=currentPowerGeneration();next.topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
                                next.epoch=epoch;next.slot=slot;next.helperExe=inputHostExecutablePath();
                                next.dll=(std::filesystem::path(module).parent_path()/L"xinput1_4.dll").wstring();
                                next.devicePath=slotPath;reason="requested";
                            }
                        } else reason="physical-path-not-proven";
                    }
                } else reason="host-not-ready";
            }
        }
        std::string state="inactive";
        {std::unique_lock<std::mutex> lock(g_physicalReaderContextMx,std::try_to_lock);
            if(!lock.owns_lock())return;
            if(next.desired && !g_physicalReaderBridge)g_physicalReaderBridge=std::make_unique<ymcc::physicalreader::Bridge>();
            if(g_physicalReaderBridge) {g_physicalReaderBridge->update(next);state=g_physicalReaderBridge->state();}
            g_physicalReaderContext=next;g_physicalReaderIdentityEpoch=identityEpoch;}
        static std::string last;
        const std::string key=state+"|"+reason+"|"+std::to_string(next.power)+"|"+std::to_string(next.topology)+"|"+std::to_string(next.epoch);
        if(last!=key) {
            last=key;
            appendNativeLifecycleLog("input-physical-reader-state",{{"policy","VC-FSE-READER-20261026-R1"},
                {"state",state},{"reason",reason},{"candidateMarker",true},{"desired",next.desired},
                {"fseKnown",fseKnown},{"fseActive",fseActive},{"slot",next.slot},
                {"sourcePowerGeneration",next.power},{"sourceTopologyGeneration",next.topology},
                {"sourceHostEpoch",next.epoch},{"sourceIdentityEpoch",identityEpoch},
                {"devicePath",W2U(next.devicePath)},{"observedNativePath",W2U(observedSlotPath)},
                {"selectedRootPath",W2U(selectedRootPath)},{"selectedHidPath",W2U(selectedHidPath)},
                {"pathBinding",pathBinding},{"hidAliasQueried",hidAliasQueried},
                {"resolvedHidAlias",W2U(resolvedHidAlias)},{"hidAliasResult",static_cast<unsigned long>(hidAliasResult)},
                {"nativePathPinned",next.desired},{"outputProtocolChanged",false},
                {"physicalDeviceBinding","exact-OpenXInput-device-path-before-and-after-read"},
                {"gameConsumptionProven",false}});
        }
    }catch(...) { /* A candidate read failure cannot escape into the BUS. */ }
}
// Called only by the existing source owner with g_gamepadReadMx held. This
// does not consume historical/other-persona/other-source reader frames.
static bool physicalSourceReaderSample(ymcc::physicalreader::Sample& sample) noexcept {
    if(!physicalSourceReaderCandidateEnabled() || g_exitRequested.load(std::memory_order_acquire) ||
       !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
       nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Rog ||
       g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
       g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) || physicalReleaseWindowActive())return false;
    try {
        std::unique_lock<std::mutex> lock(g_physicalReaderContextMx,std::try_to_lock);
        if(!lock.owns_lock() || !g_physicalReaderBridge)return false;
        const auto& c=g_physicalReaderContext;
        if(!c.desired || c.power!=currentPowerGeneration() || c.topology!=g_xinputTopologyEpoch.load(std::memory_order_acquire) ||
           c.epoch!=g_inputHostEpochMirror.load(std::memory_order_acquire) ||
           g_physicalReaderIdentityEpoch!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) ||
           c.slot>=4 || c.slot!=g_xinputPrimarySlot || !g_xinputPhysicalSourceLocked ||
           !(g_xinputPhysicalSourceMask&(1u<<c.slot)) || !(g_xinputConnectedMask&(1u<<c.slot)) ||
           (g_xinputVirtualEndpointMask&(1u<<c.slot)) || (g_xinputSlotUnknownMask&(1u<<c.slot)))return false;
        return g_physicalReaderBridge->read(c,GetTickCount64(),sample);
    }catch(...) {return false;}
}
