#pragma once
#include "controller_hid_source.h"
#include "controller_hid_source_lane.h"

// Included after background_controller_ui_runtime.h. Reuses its original
// physical identity observation and the single ordinary Raw Input sink.
namespace ymccCommonHid {
using Epoch=ymcc::commonhid::Epoch;
using Sample=ymcc::commonhid::Sample;
static ymcc::commonhid::Lane lane;
static std::uint64_t sequence=0;
static std::uint64_t registrationGeneration=1,dispatchGeneration=0;
static std::atomic<bool> uiRetirementPending{false};
static ULONGLONG registrationFloor=0,epochStarted=0;
static Epoch cacheEpoch;
static HANDLE boundDevice=nullptr;
enum class InspectionStage : unsigned {
    Unobserved, DeviceInfoRead, NotGamepadCollection, DeviceNameSize, DeviceNameRead,
    SourceBindingUnproven, XinputCapabilitiesUnavailable, XinputSubtypeUnsupported,
    PreparsedSize, PreparsedRead, HidCapsRead, HidCapsBounds, ButtonCapsRead,
    ValueCapsRead, DescriptorUnsupported, DataCapacity, Ready
};
static const char* inspectionStageName(InspectionStage s) noexcept {
    switch(s) {
    case InspectionStage::DeviceInfoRead:return "device-info-read";
    case InspectionStage::NotGamepadCollection:return "not-gamepad-collection";
    case InspectionStage::DeviceNameSize:return "device-name-size";
    case InspectionStage::DeviceNameRead:return "device-name-read";
    case InspectionStage::SourceBindingUnproven:return "source-binding-unproven";
    case InspectionStage::XinputCapabilitiesUnavailable:return "xinput-capabilities-unavailable";
    case InspectionStage::XinputSubtypeUnsupported:return "xinput-subtype-unsupported";
    case InspectionStage::PreparsedSize:return "preparsed-size";
    case InspectionStage::PreparsedRead:return "preparsed-read";
    case InspectionStage::HidCapsRead:return "hid-caps-read";
    case InspectionStage::HidCapsBounds:return "hid-caps-bounds";
    case InspectionStage::ButtonCapsRead:return "button-caps-read";
    case InspectionStage::ValueCapsRead:return "value-caps-read";
    case InspectionStage::DescriptorUnsupported:return "descriptor-unsupported";
    case InspectionStage::DataCapacity:return "data-capacity";
    case InspectionStage::Ready:return "ready";
    case InspectionStage::Unobserved:return "unobserved";
    }
    return "unknown";
}
struct Device {
    bool ready=false;
    // Observation only, driven by the original report/2s inspection retry.
    InspectionStage inspectionStage=InspectionStage::Unobserved;
    ULONGLONG inspectionDiagnosticTick=0;
    ULONGLONG retryAt=0;
    RID_DEVICE_INFO info{};
    HIDP_CAPS caps{};
    ULONG maxData=0;
    std::vector<BYTE> preparsed;
    std::vector<ymcc::controllerhid::ValueCaps> values;
    std::vector<ymcc::controllerhid::ButtonCaps> buttons;
    ymcc::controllerhid::Descriptor descriptor() const noexcept {
        return {info.hid.dwVendorId,info.hid.dwProductId,caps.UsagePage,caps.Usage,
            caps.InputReportByteLength,caps.NumberInputDataIndices,caps.NumberLinkCollectionNodes,
            caps.NumberInputValueCaps,caps.NumberInputButtonCaps,values,buttons};
    }
};
static std::unordered_map<HANDLE,Device> devices;
// Bounded observations of the existing Raw Input route. These counters never
// admit a source, read a device, or request recovery. Fields are independent
// atomic observations, not a transactional pipeline or hardware-delivery proof.
enum class PipelineStage : unsigned {
    HidHeader, HeaderUnavailable, NegativeFiltered, Capture, PacketBounds,
    ContextUnavailable, SessionInactive, OtherSource, DeviceCacheFull,
    ContextChanged, InspectRejected, QueueOrLengthRejected, ParseRejected,
    Observed, RetiredBatch, Exception, RawMessage, SinkMessage, MainMessage, KeyboardHeader, MouseHeader, Count
};
static constexpr const char* pipelineNames[]={
    "hidHeaders","headerUnavailable","negativeFiltered","captureCalls","packetBoundsRejected",
    "contextUnavailable","sessionInactive","otherSource","deviceCacheFull",
    "contextChanged","inspectionRejected","queueOrLengthRejected","parseRejected",
    "reportsObserved","retiredBatch","captureException",
    "rawMessages","sinkMessages","mainMessages","keyboardHeaders","mouseHeaders"};
static std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(PipelineStage::Count)> pipelineCounts{};
static std::atomic<std::uint64_t> pipelineVersion{0},pipelineLastObservedSequence{0};
static void observePipeline(PipelineStage stage,std::uint64_t observedSequence=0) noexcept {
    const DWORD savedError=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{savedError};
    try {
        if(!domainLogEnabled("virtual"))return;
        pipelineCounts[static_cast<unsigned>(stage)].fetch_add(1,std::memory_order_relaxed);
        if(stage==PipelineStage::Observed)pipelineLastObservedSequence.store(observedSequence,std::memory_order_relaxed);
        pipelineVersion.fetch_add(1,std::memory_order_relaxed);
    }catch(...) { /* Observation cannot replace the original input result. */ }
}
static json pipelineObservation() {
    json result=json::object();
    for(unsigned i=0;i<static_cast<unsigned>(PipelineStage::Count);++i)
        result[pipelineNames[i]]=pipelineCounts[i].load(std::memory_order_relaxed);
    result["lastObservedSequence"]=pipelineLastObservedSequence.load(std::memory_order_relaxed);
    result["snapshotAtomic"]=false;
    result["scope"]="current-process diagnostic-enabled observations only; no device absence or delivery proof";
    return result;
}
// Physical reading is independent of virtual output and the overlay switch. No new
// controller connection, Host incarnation or source admission is invented.
// Per-read facts from the original short-circuit evaluation. An absent bit
// means that condition was not evaluated, rather than false or unavailable.
struct EligibilityEvidence {
    unsigned evaluatedMask=0,valueMask=0;
};
static bool eligible(EligibilityEvidence* evidence=nullptr) {
    const auto observed=[evidence](unsigned bit,bool value) {
        if(evidence){evidence->evaluatedMask|=bit;if(value)evidence->valueMask|=bit;}
        return value;
    };
    return observed(1u,g_realStickTest.enabled) &&
        observed(2u,!g_externalPriorityVirtualSuspended.load(std::memory_order_acquire)) &&
        observed(4u,!physicalReleaseWindowActive()) && observed(8u,ymccBackgroundUi::fseActive());
}
static bool context(Epoch& e,std::wstring* path=nullptr) {
    ymcc::backgroundui::Epoch current;
    if(!ymccBackgroundUi::context(current,path))return false;
    e={current.power,current.topology,current.identity,current.episode,current.slot};
    return true;
}
// Locate the selected source's actual XUSB node, even when the original slot
// names an alias HID interface. Never bind through an arbitrary USB hub or
// a shared/sentinel container GUID.
static bool sameXusbSource(const std::wstring& rawPath,const std::wstring& slotPath,bool requireXusb=false) {
    DEVINST selected=0,raw=0;std::wstring selectedId,rawId;
    if(!ymccBackgroundUi::interfaceNode(slotPath,selected,selectedId) ||
       !ymccBackgroundUi::interfaceNode(rawPath,raw,rawId))return false;
    // Different interface classes may expose the same selected PnP devnode.
    // Require both exact node and full instance identity; never VID/PID, a
    // shared container or a common USB parent. Other devices keep the old path.
    if(!requireXusb && selected==raw && _wcsicmp(selectedId.c_str(),rawId.c_str())==0)return true;
    std::wstring sourceId;
    for(unsigned depth=0;depth<8;++depth) {
        std::wstring service;
        if(ymccBackgroundUi::propertyString(selected,DEVPKEY_Device_Service,service) &&
           _wcsicmp(service.c_str(),L"xusb22")==0) {
            std::array<wchar_t,512> id{};
            if(CM_Get_Device_IDW(selected,id.data(),static_cast<ULONG>(id.size()),0)!=CR_SUCCESS)return false;
            sourceId=id.data();break;
        }
        DEVINST parent=0;
        if(CM_Get_Parent(&parent,selected,0)!=CR_SUCCESS || parent==selected)return false;
        selected=parent;
    }
    if(sourceId.empty())return false;
    for(unsigned depth=0;depth<8;++depth) {
        std::array<wchar_t,512> id{};
        if(CM_Get_Device_IDW(raw,id.data(),static_cast<ULONG>(id.size()),0)!=CR_SUCCESS)return false;
        if(_wcsicmp(id.data(),sourceId.c_str())==0)return true;
        DEVINST parent=0;
        if(CM_Get_Parent(&parent,raw,0)!=CR_SUCCESS || parent==raw)return false;
        raw=parent;
    }
    return false;
}
#include "controller_isolation_hid_runtime.h"

// Rejection is not a device fault or a recovery request. A compared HID may
// belong to another device; only a successful exact ancestor match proves it.
static Device rejectedInspection(HANDLE handle,InspectionStage stage,const Device& observed,
    unsigned slot,bool infoKnown,bool ancestorMatched,bool capsKnown,
    bool xinputCallKnown,DWORD xinputResult,const XINPUT_CAPABILITIES& xcaps) {
    const DWORD savedError=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{savedError};
    Device rejected;rejected.inspectionStage=stage;
    try {
        const auto prior=devices.find(handle);
        if(prior!=devices.end() && prior->second.inspectionStage==stage)
            rejected.inspectionDiagnosticTick=prior->second.inspectionDiagnosticTick;
        if(!domainLogEnabled("virtual"))return rejected;
        const auto now=GetTickCount64();
        if(rejected.inspectionDiagnosticTick && now>=rejected.inspectionDiagnosticTick &&
           now-rejected.inspectionDiagnosticTick<10000)return rejected;
        // Count attempts, including failed diagnostic allocation/writes, so a
        // bad logger cannot turn the unchanged inspect retry into a log storm.
        rejected.inspectionDiagnosticTick=now;
        const bool hidInfoKnown=infoKnown && observed.info.dwType==RIM_TYPEHID;
        json row={{"schemaVersion",1},{"diagnosticOnly",true},{"status","not-qualified"},
            {"stage",inspectionStageName(stage)},{"candidateDeviceHandle",reinterpret_cast<std::uintptr_t>(handle)},
            {"candidateComparedToSlot",slot},{"deviceInfoKnown",infoKnown},
            {"selectedSourceMatchProven",ancestorMatched},{"capsKnown",capsKnown},
            {"xinputCapabilitiesReturnKnown",xinputCallKnown},
            {"xinputCapabilitiesReturn",xinputCallKnown?json(xinputResult):json(nullptr)},
            {"xinputType",xinputCallKnown&&xinputResult==ERROR_SUCCESS?json(xcaps.Type):json(nullptr)},
            {"xinputSubType",xinputCallKnown&&xinputResult==ERROR_SUCCESS?json(xcaps.SubType):json(nullptr)},
            {"observedTick",now},{"diagnosticRepeatMs",10000},
            {"admissionProven",false},{"repairRequested",false},{"gameConsumptionProven",false},
            {"scope","candidate comparison at original inspection; not selected-source failure, device absence, or extra device read"}};
        if(hidInfoKnown)row["candidateHid"]={{"vendor",observed.info.hid.dwVendorId},{"product",observed.info.hid.dwProductId},
            {"usagePage",observed.info.hid.usUsagePage},{"usage",observed.info.hid.usUsage}};
        if(capsKnown) {
            row["caps"]={{"reportBytes",observed.caps.InputReportByteLength},{"dataIndices",observed.caps.NumberInputDataIndices},
                {"linkCollections",observed.caps.NumberLinkCollectionNodes},{"declaredValueCaps",observed.caps.NumberInputValueCaps},
                {"declaredButtonCaps",observed.caps.NumberInputButtonCaps},{"capturedValueCaps",observed.values.size()},
                {"capturedButtonCaps",observed.buttons.size()},{"maxData",observed.maxData}};
            row["values"]=json::array();
            for(const auto& v:observed.values)row["values"].push_back({{"page",v.page},{"usageFirst",v.usageFirst},{"usageLast",v.usageLast},
                {"dataFirst",v.dataFirst},{"dataLast",v.dataLast},{"linkCollection",v.linkCollection},{"linkUsagePage",v.linkUsagePage},
                {"linkUsage",v.linkUsage},{"reportId",v.reportId},{"bitSize",v.bitSize},{"reportCount",v.reportCount},{"bitField",v.bitField},
                {"logicalMin",v.logicalMin},{"logicalMax",v.logicalMax},{"isRange",v.isRange},{"isAbsolute",v.isAbsolute},
                {"isAlias",v.isAlias},{"hasNull",v.hasNull}});
            row["buttons"]=json::array();
            for(const auto& v:observed.buttons)row["buttons"].push_back({{"page",v.page},{"usageFirst",v.usageFirst},{"usageLast",v.usageLast},
                {"dataFirst",v.dataFirst},{"dataLast",v.dataLast},{"linkCollection",v.linkCollection},{"linkUsagePage",v.linkUsagePage},
                {"linkUsage",v.linkUsage},{"reportId",v.reportId},{"bitField",v.bitField},{"isRange",v.isRange},
                {"isAbsolute",v.isAbsolute},{"isAlias",v.isAlias}});
        }
        // Reuse the already running diagnostic executor; never add file I/O
        // or wait for a logger while inspecting or holding the reader mutex.
        ymcc::inputHost::diagnostics::Scope diagnosticScope;
        appendNativeLifecycleLog("gamepad-common-hid-inspection",row);
    }catch(...) { /* Observation never changes the original rejection outcome. */ }
    return rejected;
}
static Device inspect(HANDLE handle,const std::wstring& slotPath,unsigned slot) {
    Device d;d.info.cbSize=sizeof(d.info);UINT bytes=sizeof(d.info);
    bool infoKnown=false,ancestorMatched=false,capsKnown=false,xinputCallKnown=false;
    DWORD xinputResult=ERROR_SUCCESS;XINPUT_CAPABILITIES xcaps{};
    const auto reject=[&](InspectionStage stage) {
        auto observation=rejectedInspection(handle,stage,d,slot,infoKnown,ancestorMatched,capsKnown,xinputCallKnown,xinputResult,xcaps);
        // Match the original early return-d and later return-empty branches.
        // Move, rather than copy, so diagnostics add no cache allocation.
        if(stage<InspectionStage::PreparsedRead) {
            d.inspectionStage=observation.inspectionStage;d.inspectionDiagnosticTick=observation.inspectionDiagnosticTick;
            return std::move(d);
        }
        return observation;
    };
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICEINFO,&d.info,&bytes)==~0u)return reject(InspectionStage::DeviceInfoRead);
    infoKnown=true;
    if(d.info.dwType!=RIM_TYPEHID || d.info.hid.usUsagePage!=1 || d.info.hid.usUsage!=5)return reject(InspectionStage::NotGamepadCollection);
    UINT chars=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&chars)==~0u || !chars || chars>32768)return reject(InspectionStage::DeviceNameSize);
    std::vector<wchar_t> path(static_cast<std::size_t>(chars)+1,L'\0');
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,path.data(),&chars)==~0u)return reject(InspectionStage::DeviceNameRead);
    if(!sameXusbSource(path.data(),slotPath))return reject(InspectionStage::SourceBindingUnproven);
    ancestorMatched=true;
    if(!g_ymccXInputGetCapabilities14 || slot>=4)return reject(InspectionStage::XinputCapabilitiesUnavailable);
    xinputResult=g_ymccXInputGetCapabilities14(slot,XINPUT_FLAG_GAMEPAD,&xcaps);xinputCallKnown=true;
    if(xinputResult!=ERROR_SUCCESS)return reject(InspectionStage::XinputCapabilitiesUnavailable);
    if(xcaps.Type!=XINPUT_DEVTYPE_GAMEPAD || xcaps.SubType!=XINPUT_DEVSUBTYPE_GAMEPAD)return reject(InspectionStage::XinputSubtypeUnsupported);
    bytes=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,nullptr,&bytes)==~0u || !bytes || bytes>65536)return reject(InspectionStage::PreparsedSize);
    d.preparsed.resize(bytes);
    if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,d.preparsed.data(),&bytes)==~0u)return reject(InspectionStage::PreparsedRead);
    const auto prep=reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data());
    if(HidP_GetCaps(prep,&d.caps)!=HIDP_STATUS_SUCCESS)return reject(InspectionStage::HidCapsRead);
    capsKnown=true;
    if(!d.caps.NumberInputButtonCaps || d.caps.NumberInputButtonCaps>8 || !d.caps.NumberInputValueCaps || d.caps.NumberInputValueCaps>16 ||
       !d.caps.InputReportByteLength || d.caps.InputReportByteLength>256)return reject(InspectionStage::HidCapsBounds);
    std::array<HIDP_BUTTON_CAPS,8> bc{};USHORT count=d.caps.NumberInputButtonCaps;
    if(HidP_GetButtonCaps(HidP_Input,bc.data(),&count,prep)!=HIDP_STATUS_SUCCESS || count!=d.caps.NumberInputButtonCaps)return reject(InspectionStage::ButtonCapsRead);
    for(unsigned i=0;i<count;++i) {
        const auto& v=bc[i];
        d.buttons.push_back({v.UsagePage,v.IsRange?v.Range.UsageMin:v.NotRange.Usage,
            v.IsRange?v.Range.UsageMax:v.NotRange.Usage,v.IsRange?v.Range.DataIndexMin:v.NotRange.DataIndex,
            v.IsRange?v.Range.DataIndexMax:v.NotRange.DataIndex,v.LinkCollection,v.LinkUsagePage,v.LinkUsage,
            v.ReportID,v.BitField,v.IsRange!=FALSE,v.IsAbsolute!=FALSE,v.IsAlias!=FALSE});
    }
    std::array<HIDP_VALUE_CAPS,16> vc{};count=d.caps.NumberInputValueCaps;
    if(HidP_GetValueCaps(HidP_Input,vc.data(),&count,prep)!=HIDP_STATUS_SUCCESS || count!=d.caps.NumberInputValueCaps)return reject(InspectionStage::ValueCapsRead);
    for(unsigned i=0;i<count;++i) {
        const auto& v=vc[i];
        d.values.push_back({v.UsagePage,v.IsRange?v.Range.UsageMin:v.NotRange.Usage,
            v.IsRange?v.Range.UsageMax:v.NotRange.Usage,v.IsRange?v.Range.DataIndexMin:v.NotRange.DataIndex,
            v.IsRange?v.Range.DataIndexMax:v.NotRange.DataIndex,v.LinkCollection,v.LinkUsagePage,v.LinkUsage,
            v.ReportID,v.BitSize,v.ReportCount,v.BitField,v.LogicalMin,v.LogicalMax,
            v.IsRange!=FALSE,v.IsAbsolute!=FALSE,v.IsAlias!=FALSE,v.HasNull!=FALSE});
    }
    if(!ymcc::controllerhid::qualified(d.descriptor()))return reject(InspectionStage::DescriptorUnsupported);
    d.maxData=HidP_MaxDataListLength(HidP_Input,prep);
    d.ready=d.maxData>=d.caps.NumberInputDataIndices && d.maxData<=64;
    if(d.ready)d.inspectionStage=InspectionStage::Ready;
    else {
        const auto observation=reject(InspectionStage::DataCapacity);
        d.inspectionStage=observation.inspectionStage;d.inspectionDiagnosticTick=observation.inspectionDiagnosticTick;
    }
    return d;
}
static void invalidate() {
    isolationDirect::withdraw();
    // Caller owns the original reader mutex, including registration/removal.
    lane.clear();devices.clear();boundDevice=nullptr;cacheEpoch={};epochStarted=0;
    ++registrationGeneration;++dispatchGeneration;
    registrationFloor=GetTickCount64();
}
static void rejectSelected(HANDLE device,bool retireBatch=true) {
    if(device && device==boundDevice) {
        lane.clear();ymccBackgroundUi::lane.clear();
        if(retireBatch)++dispatchGeneration;
    }
}
enum class ReadObservation {Unobserved, OriginalSourceNotApplicable, ContextUnavailable, SessionInactive, AwaitingHid, OriginalRetained, Selected};
static const char* readObservationName(ReadObservation r) noexcept {
    switch(r) {
    case ReadObservation::OriginalSourceNotApplicable:return "original-source-not-applicable";
    case ReadObservation::ContextUnavailable:return "physical-context-unavailable";
    case ReadObservation::SessionInactive:return "session-inactive";
    case ReadObservation::AwaitingHid:return "awaiting-qualified-current-hid";
    case ReadObservation::OriginalRetained:return "original-source-retained";
    case ReadObservation::Selected:return "selected";
    case ReadObservation::Unobserved:return "unobserved";
    }
    return "unknown";
}
static void observeRead(ReadObservation reason,const Epoch& e,bool contextKnown,bool allowed,bool used,bool samplePresent,
    const EligibilityEvidence* eligibility=nullptr) noexcept {
    // Called under the existing read mutex. Every admitted diagnostic reads
    // the bounded process registration snapshot after the original rate limit.
    static ReadObservation lastReason=ReadObservation::Unobserved;static ULONGLONG lastTick=0;
    static std::uint64_t lastPipelineVersion=0;
    static unsigned lastEvaluatedMask=0,lastValueMask=0;
    const auto currentPipelineVersion=pipelineVersion.load(std::memory_order_relaxed);
    const unsigned evaluatedMask=eligibility?eligibility->evaluatedMask:0u;
    const unsigned valueMask=eligibility?eligibility->valueMask:0u;
    const bool changed=reason!=lastReason || evaluatedMask!=lastEvaluatedMask || valueMask!=lastValueMask;
    if(!changed && (!allowed || used) && currentPipelineVersion==lastPipelineVersion)return;
    const DWORD savedError=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{savedError};
    try {
        if(!domainLogEnabled("virtual"))return;
        const auto now=GetTickCount64();const auto interval=changed?1000ULL:10000ULL;
        if(lastTick && now>=lastTick && now-lastTick<interval)return;
        lastTick=now;lastReason=reason;lastPipelineVersion=currentPipelineVersion; // Bound diagnostic failures as well.
        lastEvaluatedMask=evaluatedMask;lastValueMask=valueMask;
        ymcc::inputHost::diagnostics::Scope diagnosticScope;
        const auto condition=[eligibility](unsigned bit)->json {
            if(!eligibility || !(eligibility->evaluatedMask&bit))return nullptr;
            return (eligibility->valueMask&bit)!=0;
        };
        json observation={{"schemaVersion",1},{"diagnosticOnly",true},
            {"reason",readObservationName(reason)},{"selected",used},{"contextKnown",contextKnown},
            {"applicableSession",allowed},{"currentHidSamplePresent",samplePresent},
            {"pipeline",pipelineObservation()},
            {"isolationSharedReader",{{"bound",isolationDirect::reader.port().bound()},
                {"pending",isolationDirect::reader.port().pending()},{"reportSequence",isolationDirect::reportSequence},
                {"lastError",isolationDirect::reader.port().lastError},
                {"scope","actual completed shared HID reads; separate from Raw Input pipeline counters"}}},
            {"comparedSlot",e.slot},{"power",e.power},{"topology",e.topology},{"identity",e.identity},
            {"observedTick",now},{"minimumReasonChangeMs",1000},{"waitingRepeatMs",10000},
            {"absenceIsControllerFault",false},{"repairRequested",false},{"gameConsumptionProven",false},
            {"scope","original read-path observation; bounded read-only process registration; no input authority or recovery action"},
            {"eligibility",{{"evaluatedMask",eligibility?eligibility->evaluatedMask:0u},
                {"captureEnabled",condition(1u)},{"externalPriorityPermits",condition(2u)},
                {"physicalReleasePermits",condition(4u)},{"fseActive",condition(8u)},
                {"scope","this read's original short-circuit conditions; null means not evaluated; no extra platform query"}}}};
        observation["registration"]=gamepadRawInputRegistrationEvidence();
        appendNativeLifecycleLog("gamepad-common-hid-observation",observation);
    }catch(...) { /* Diagnostic failure never replaces the input read. */ }
}
} // namespace ymccCommonHid

namespace ymccCommonHid {
// Called under the existing read mutex. Epoch alone cannot distinguish a
// cancelled sleep query or same-generation second suspend from old Raw data.
static constexpr std::uint64_t commonRawMaxQueueAgeMs=250;
static bool win5WakeRawAllowed(std::uint64_t queuedTick=~std::uint64_t{0}) {
    const auto host=g_win5WakeReaderHost.load(std::memory_order_acquire);
    const auto scopedHost=host && host==inputHostEpochToken() && g_realStickTest.virtualTargetEnabled?host:0;
    static std::uint64_t priorHost=0,priorSerial=0,priorBoundary=0,floorTick=0;
    if(!scopedHost && !priorHost){
        if(!floorTick)return true;
        // Finish rejecting only messages queued before the previous handoff.
        // New Raw reports need not wait for the old queue to age out.
        const auto now=GetTickCount64();
        if(now>=floorTick && now-floorTick>commonRawMaxQueueAgeMs){floorTick=0;return true;}
        return queuedTick>floorTick;
    }
    const auto serial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    const auto boundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    if(scopedHost!=priorHost || serial!=priorSerial || boundary!=priorBoundary){
        priorHost=scopedHost;priorSerial=serial;priorBoundary=boundary;floorTick=GetTickCount64();
        lane.clear();++dispatchGeneration;
        isolationDirect::backgroundRetirementPending.store(true,std::memory_order_release);
    }
    return !g_sleepPowerDispatch.pendingSuspend() && queuedTick>floorTick;
}

static unsigned applyWin5CachedRawSticks(const Epoch& source,const XINPUT_GAMEPAD& native,Sample* raw,bool allowed) {
    // Reuse the admitted FSE source; this never grants a new reader or changes a retained wake.
    const auto host=raw && allowed && g_realStickTest.virtualTargetEnabled &&
        g_machineIdentity.family==YmccFamily::Gpd && g_machineIdentity.deviceClass=="HandheldCompanion.Devices.GPDWin5" &&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        g_inputHostMatchedSteamDeck.load(std::memory_order_acquire) ? inputHostEpochToken() : 0;
    static ymcc::commonhid::CachedRawStickHandoff handoff;
    return handoff.apply(host && host!=g_win5WakeReaderHost.load(std::memory_order_acquire)?host:0,source,native,raw);
}
}

static bool commonControllerHidRead(XINPUT_GAMEPAD& pad,CommonControllerHidRead& receipt,bool sourceEligible,bool publishUi) {
    using namespace ymccCommonHid;
    Epoch e;Sample sample;
    const bool contextKnown=sourceEligible && context(e);
    EligibilityEvidence eligibility;
    const bool allowed=contextKnown && eligible(&eligibility);
    const bool cold=isolationDirect::coldScene();
    // Do not borrow this reader from another physical source or arm it merely
    // because an idle pad read is neutral. Current XInput activity wins.
    const bool xinputActive=cold && allowed && (ymcc::commonhid::basicActive(pad) || pad.bLeftTrigger || pad.bRightTrigger);
    static Epoch coldXinputWitness;
    if(cold && allowed && coldXinputWitness!=e)coldXinputWitness={};
    if(xinputActive)coldXinputWitness=e;
    const bool xinputProven=cold && allowed && coldXinputWitness==e;
    const auto now=cold?GetTickCount64():0;
    const bool rawFirst=cold && allowed && !xinputActive && win5WakeRawAllowed() &&
        lane.readColdPrimary(e,pad,now,isolationDirect::key.coldIntent && isolationDirect::lane.selected(),sample);
    if(allowed)isolationDirect::observe(publishUi,e,rawFirst || xinputProven);
    else isolationDirect::withdraw();
    const bool rawAllowed=win5WakeRawAllowed();
    const bool samplesAllowed=isolationDirect::sampleReadAllowed(allowed);
    const bool wakeFallback=isolationDirect::key.wakeSuspend!=0;
    const bool directUsed=!rawFirst && (wakeFallback || isolationDirect::key.coldIntent
        ? isolationDirect::lane.readWakeFallback(e,samplesAllowed,pad,GetTickCount64(),sample)
        : isolationDirect::lane.read(e,samplesAllowed,pad,sample,false));
    const bool used=rawFirst || directUsed || (!cold && lane.read(e,rawAllowed && (wakeFallback?allowed:samplesAllowed),pad,sample));
    const auto nativeStickMask=applyWin5CachedRawSticks(e,pad,used && !directUsed && !wakeFallback?&sample:nullptr,allowed);
    const auto observation=!sourceEligible?ReadObservation::OriginalSourceNotApplicable:
        !contextKnown?ReadObservation::ContextUnavailable:!allowed?ReadObservation::SessionInactive:
        used?ReadObservation::Selected:!lane.hasSample()?ReadObservation::AwaitingHid:ReadObservation::OriginalRetained;
    observeRead(observation,e,contextKnown,allowed,used,lane.hasSample(),&eligibility);
    if(used) {
        pad=inputCanonicalizeGamepad(sample.pad);
        receipt={sample.sequence,sample.observedTick,sample.independentTriggers,sample.completeBasic,sample.combinedTriggerRaw,sample.isolationSharedRead,nativeStickMask};
    }
    static bool previous=false;
    static bool previousShared=false;
    static std::uint64_t witnessedPower=0,witnessedEdge=0,witnessedWakeSuspend=0,witnessedWakeNotice=0;
    static bool previousWake=false;
    if(previous && !previousWake && !used)uiRetirementPending.store(true,std::memory_order_release);
    const auto edge=directUsed?isolationDirect::key.edge:0;
    const auto wakeSuspend=directUsed?isolationDirect::key.wakeSuspend:0;
    const auto wakeNotice=directUsed?isolationDirect::key.wakeNotice:0;
    if(edge && (witnessedPower!=e.power || witnessedEdge!=edge ||
                witnessedWakeSuspend!=wakeSuspend || witnessedWakeNotice!=wakeNotice)) {
        // Count the admitted edge (or the exact public wake), not frames.
        witnessedPower=e.power;witnessedEdge=edge;
        witnessedWakeSuspend=wakeSuspend;witnessedWakeNotice=wakeNotice;
        try {appendNativeLifecycleLog("gamepad-common-hid-shared-read-first-selection",{{"schemaVersion",1},
            {"power",e.power},{"edge",edge},{"topology",e.topology},{"identity",e.identity},
            {"readerRole",wakeFallback?"win5-kept-sd-wake":isolationDirect::key.coldIntent?"cold-fse":"original-isolation"},
            {"wakeSuspendSerial",wakeSuspend},{"wakeNotificationSequence",wakeNotice},
            {"occupant",e.occupant},{"slot",e.slot},{"reportSequence",sample.sequence},
            {"reportObservedTick",sample.observedTick},{"gameConsumptionProven",false}});}catch(...){ }
    }else if(previous!=used || (previousShared!=directUsed && domainLogEnabled("virtual"))) {
        try {appendNativeLifecycleLog("gamepad-common-hid-source",{{"source",used?"xinput-hid":"original"},
            {"selected",used},{"slot",e.slot},{"reportSequence",used?sample.sequence:0},
            {"reportObservedTick",used?sample.observedTick:0},{"triggerSource",used&&sample.independentTriggers?"hid-independent":"original-xinput"},
            {"transport",used&&sample.isolationSharedRead?"isolation-shared-hid-read":"raw-input"},
            {"readerRole",directUsed&&isolationDirect::key.coldIntent?"cold-fse":directUsed&&wakeFallback?"win5-kept-sd-wake":"existing"},
            {"power",e.power},{"topology",e.topology},{"identity",e.identity},
            {"scope","same-physical-source-data;original-mapping-isolation-and-host-owner"},
            {"gameConsumptionProven",false}});}catch(...){}
    }
    previous=used;
    previousShared=directUsed;
    previousWake=wakeFallback && directUsed;
    return used;
}

static bool commonControllerHidUiDrainPending() {
    std::lock_guard lock(g_gamepadReadMx);
    using namespace ymccCommonHid;
    if(!isolationDirect::haveKey || !isolationDirect::reader.port().ready())return false;
    ymcc::isolationhid::Key current;
    if(isolationDirect::key.wakeSuspend){
        Epoch source;bool reuse=false;
        return eligible() && context(source) && isolationDirect::currentWin5Wake(source,current,reuse) && reuse;
    }
    if(isolationDirect::key.coldIntent){
        Epoch source;bool reuse=false;
        return eligible() && context(source) && isolationDirect::currentColdFse(source,current,reuse) && reuse;
    }
    return isolationDirect::current(current) && current==isolationDirect::key;
}

static bool commonControllerHidRetire() {
    std::unique_lock lock(g_gamepadReadMx,std::try_to_lock);
    if(!lock.owns_lock())return false;
    ymccCommonHid::isolationDirect::withdraw();
    return ymccCommonHid::isolationDirect::reader.retire();
}

// Sole UI semantic owner consumes this handoff after its original read. BUS
// retirement publishes an atomic fact; it never writes UI-owned plain flags.
static void commonControllerHidUiHandoff() noexcept {
    if(ymccCommonHid::isolationDirect::backgroundRetirementPending.exchange(false,std::memory_order_acq_rel))
        ymccBackgroundUi::lane.clear();
    if(ymccCommonHid::uiRetirementPending.exchange(false,std::memory_order_acq_rel)) {
        g_inputReleaseRequired.store(true,std::memory_order_release);
        g_gamepadAxisNeutralAdmissionRequired=true;
    }
}

// Capture before the old UI short circuit. Each packet is decoded completely
// before callbacks; every report edge uses the original semantic evaluator once.
static bool commonControllerHidCapture(RAWINPUT* raw,UINT packetBytes) {
    using namespace ymccCommonHid;
    if(!raw || packetBytes<sizeof(RAWINPUTHEADER) || raw->header.dwType!=RIM_TYPEHID)return false;
    const HANDLE deviceHandle=raw->header.hDevice;
    observePipeline(PipelineStage::Capture);
    bool claimed=false;
    std::uint64_t packetRegistration=0,packetDispatch=0;
    try {
        constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
        if(packetBytes<offset || raw->header.dwSize!=packetBytes) {
            observePipeline(PipelineStage::PacketBounds);
            std::lock_guard lock(g_gamepadReadMx);rejectSelected(deviceHandle);return false;
        }
        const auto bytes=raw->data.hid.dwSizeHid,count=raw->data.hid.dwCount;
        if(!bytes || bytes>256 ||
           !count || count>256 || count>(packetBytes-offset)/bytes) {
            observePipeline(PipelineStage::PacketBounds);
            std::lock_guard lock(g_gamepadReadMx);rejectSelected(deviceHandle);return false;
        }
        const auto now=GetTickCount64();
        const DWORD queueAge=static_cast<DWORD>(now)-static_cast<DWORD>(GetMessageTime());
        const auto queuedTick=queueAge<=now?now-queueAge:0;
        Epoch e;
        {std::lock_guard lock(g_gamepadReadMx);
            // A retired packet is handled here; do not replay through the
            // older UI Raw path even if the direct port is still returning.
            if(!win5WakeRawAllowed(queuedTick)){observePipeline(PipelineStage::RetiredBatch);return true;}
            if(!context(e)){observePipeline(PipelineStage::ContextUnavailable);lane.clear();return false;}
            if(!eligible()){observePipeline(PipelineStage::SessionInactive);lane.clear();return false;}}
        {std::lock_guard lock(g_gamepadReadMx);if(!(cacheEpoch==e)) {
            const bool replaced=cacheEpoch.power!=0;
            lane.clear();devices.clear();boundDevice=nullptr;cacheEpoch=e;
            epochStarted=replaced?now:registrationFloor;
        }}
        if(boundDevice && deviceHandle!=boundDevice){observePipeline(PipelineStage::OtherSource);return false;}
        auto it=devices.find(deviceHandle);
        if(it==devices.end() || (!it->second.ready && now>=it->second.retryAt)) {
            if(it==devices.end() && devices.size()>=8) {
                const auto unused=std::find_if(devices.begin(),devices.end(),[](const auto& v){return !v.second.ready;});
                if(unused==devices.end()){observePipeline(PipelineStage::DeviceCacheFull);return false;}
                devices.erase(unused);
            }
            std::wstring path;Epoch current;
            {std::lock_guard lock(g_gamepadReadMx);if(!context(current,&path) || !(current==e)){observePipeline(PipelineStage::ContextChanged);return false;}}
            Device candidate=inspect(deviceHandle,path,e.slot);
            candidate.retryAt=now+2000;
            it=devices.insert_or_assign(deviceHandle,std::move(candidate)).first;
            if(it->second.ready) {
                // Device diagnostics must not discard a valid source report.
                try {appendNativeLifecycleLog("gamepad-common-hid-device",{
                    {"qualified",true},{"source","xinput-hid"},{"slot",e.slot},{"slotPath",W2U(path)},
                    {"reportBytes",it->second.caps.InputReportByteLength},{"binding","exact-selected-devnode-or-xusb22-ancestor"},
                    {"triggerSupport","combined-z-partial;native-lt-rt-preserved"},{"gameConsumptionProven",false}});}catch(...){}
            }
        }
        auto& device=it->second;
        if(!device.ready){observePipeline(PipelineStage::InspectRejected);return false;}
        if(bytes!=device.caps.InputReportByteLength || queueAge>commonRawMaxQueueAgeMs || now<epochStarted || queueAge>now-epochStarted) {
            observePipeline(PipelineStage::QueueOrLengthRejected);
            std::lock_guard lock(g_gamepadReadMx);rejectSelected(deviceHandle);return false;
        }
        std::array<HIDP_DATA,64> data{};
        std::array<ymcc::controllerhid::DataPoint,64> points{};
        std::array<ymcc::controllerhid::Frame,256> frames{};
        std::array<bool,256> valid{};
        {std::lock_guard lock(g_gamepadReadMx);
            if(!win5WakeRawAllowed(queuedTick)){observePipeline(PipelineStage::RetiredBatch);return true;}
            packetRegistration=registrationGeneration;packetDispatch=++dispatchGeneration;}
        for(DWORD report=0;report<count;++report) {
            ULONG n=device.maxData;
            auto p=reinterpret_cast<PCHAR>(raw->data.hid.bRawData+report*bytes);
            const bool parsed=HidP_GetData(HidP_Input,data.data(),&n,
                reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data()),p,bytes)==HIDP_STATUS_SUCCESS && n<=64;
            if(parsed)for(ULONG i=0;i<n;++i)points[i]={data[i].DataIndex,data[i].RawValue,data[i].On!=FALSE};
            valid[report]=parsed && ymcc::controllerhid::decoded(ymcc::controllerhid::decode(device.descriptor(),
                {bytes,static_cast<unsigned char>(p[0]),{points.data(),n}},frames[report]));
            if(!valid[report])observePipeline(PipelineStage::ParseRejected);
        }
        // No device-cache reference survives the first original evaluator call.
        claimed=true;
        for(DWORD report=0;report<count;++report) {
            {std::lock_guard lock(g_gamepadReadMx);Epoch current;
                // A same-epoch registration or a newer reentrant packet also
                // retires the old batch suffix, without replay through UI.
                if(!win5WakeRawAllowed(queuedTick) || packetRegistration!=registrationGeneration || packetDispatch!=dispatchGeneration){observePipeline(PipelineStage::RetiredBatch);return true;}
                if(valid[report] && context(current) && current==e && eligible()) {
                    if(!boundDevice)boundDevice=deviceHandle;
                    if(boundDevice==deviceHandle) {
                        lane.observe(e,{frames[report].pad,++sequence,now,frames[report].independentTriggers,frames[report].completeBasic,frames[report].combinedTriggerRaw});
                        observePipeline(PipelineStage::Observed,sequence);
                        const ymcc::backgroundui::Epoch ui{e.power,e.topology,e.identity,e.occupant,e.slot};
                        ymccBackgroundUi::lane.observe(ui,frames[report].pad);
                    }
                } else {rejectSelected(deviceHandle,false);}
            }
            gamepadEval();
        }
        return true;
    }catch(...) {
        std::lock_guard lock(g_gamepadReadMx);
        observePipeline(PipelineStage::Exception);
        // An older semantic callback may have pumped a newer packet before
        // failing. It owns no cleanup of the replacement packet's state.
        if(!claimed || (packetRegistration==registrationGeneration && packetDispatch==dispatchGeneration)) {
            rejectSelected(deviceHandle);
            if(claimed)uiRetirementPending.store(true,std::memory_order_release);
        }
        return claimed; // A callback failure must not replay already consumed reports.
    }
}
