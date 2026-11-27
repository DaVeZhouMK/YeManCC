#include "sleep_kernel_cycle.h"

// Shared Windows evidence owner. These observations never admit power actions.
static ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;
static std::atomic<ULONGLONG> g_sgKernelSystemWake{0};
static ymcc::sleeppower::KernelCycleIdentity sgKernelEntryIdentity(const std::string& xml) noexcept {
    try {
        const auto data=ymcc::sleep_residency::readEventData(xml);
        const auto id=[&](const char* key)->std::optional<std::uint64_t>{
            const auto found=data.values.find(key);
            if(found==data.values.end()||data.duplicates.count(key))return {};
            return ymcc::sleep_residency::unsignedValue(found->second);
        };
        return {id("BootId"),id("ScenarioInstanceIdV2")};
    }catch(...){return {};}
}
static ymcc::sleeppower::KernelCycleIdentity sgKernelExitIdentity(const json& evidence) noexcept {
    try {
        const auto values=evidence.find("values");
        if(values==evidence.end()||!values->is_object())return {};
        const auto id=[&](const char* key)->std::optional<std::uint64_t>{
            const auto found=values->find(key);
            if(found==values->end()||!found->is_number_unsigned())return {};
            return found->get<std::uint64_t>();
        };
        // The continuing 507 can name both its own segment and the successor.
        return {id("BootId"),id("ScenarioInstanceIdV2"),id("ScenarioInstanceId")};
    }catch(...){return {};}
}
static ymcc::sleeppower::KernelCycleSample sgKernelSleepExit(const json& evidence,ULONGLONG at,
                                                            ULONGLONG generation,int reason) noexcept {
    try {
        const auto& values=evidence.at("values");
        const auto& entered=values.at("SleepEntered");
        const auto& sleep=values.at("SleepDurationInUs");
        const auto& total=values.at("DurationInUs");
        if(!entered.is_boolean()||!entered.get<bool>()||!sleep.is_number_unsigned()||!total.is_number_unsigned())return {};
        const auto inProgress=values.find("IsCsSessionInProgressOnExit");
        if((inProgress!=values.end()&&!inProgress->is_boolean())||
           (inProgress==values.end()&&evidence.at("raw").contains("IsCsSessionInProgressOnExit")))return {};
        const auto slept=sleep.get<ULONGLONG>(),span=total.get<ULONGLONG>();
        if(!slept||span<slept)return {};
        // A continuing phase can prove entry without authorizing interactive recovery.
        return {at,generation,reason,span,inProgress!=values.end()&&inProgress->get<bool>(),false,slept};
    }catch(...){return {};}
}
template<class Current,class Visit>
static DWORD sgVisitOriginalKernelCycle(Current current,Visit visit) {
    using namespace ymcc::sleeppower;
    const auto start=GetTickCount64();
    EVT_HANDLE query=nullptr,events[kernelCycleReadLimit]{};DWORD count=0;
    DWORD error=ERROR_SUCCESS;
    const auto close=[&]{
        for(auto& event:events)if(event){if(!EvtClose(event))error=GetLastError();event=nullptr;}
        if(query){if(!EvtClose(query))error=GetLastError();query=nullptr;}
    };
    try{
        if(!current())return ERROR_CANCELLED;
        query=EvtQuery(nullptr,L"System",L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=506 or EventID=507 or EventID=107)]]",
                       EvtQueryChannelPath|EvtQueryReverseDirection);
        if(!query)return GetLastError();
        if(!EvtNext(query,kernelCycleReadLimit,events,0,0,&count)){error=GetLastError();close();return error;}
        if(count>kernelCycleReadLimit){close();return ERROR_INVALID_DATA;}
        std::vector<wchar_t> buffer(32768,L'\0');
        for(DWORD index=0;index<count;++index){
            if(!current()||GetTickCount64()-start>1000){error=ERROR_CANCELLED;break;}
            DWORD used=0,properties=0;
            if(!EvtRender(nullptr,events[index],EvtRenderEventXml,static_cast<DWORD>(buffer.size()*sizeof(wchar_t)),
                          buffer.data(),&used,&properties)){error=GetLastError();break;}
            if(used<sizeof(wchar_t)||used%sizeof(wchar_t)||used>buffer.size()*sizeof(wchar_t)||buffer[(used/sizeof(wchar_t))-1]!=L'\0'){
                error=ERROR_INVALID_DATA;break;
            }
            auto xml=W2U(buffer.data());
            const auto at=sgKernelPowerEventFileTime(xml);
            const auto id=sgKernelPowerEventId(xml),reason=sgKernelPowerReason(xml);
            if(!at||at>sgNowFileTime()||(id!=506&&id!=507&&id!=107)){error=ERROR_INVALID_DATA;break;}
            if(!visit(xml,at,id,reason))break;
        }
        close();
        if(!current()||GetTickCount64()-start>1000)error=ERROR_CANCELLED;
    }catch(...){error=ERROR_INVALID_DATA;close();}
    return error;
}

struct SgOriginalKernelWakeReadback {
    ymcc::sleeppower::KernelWakeRecord fact;
    std::string xml;
    DWORD error=ERROR_SUCCESS;
};
template<class Current,class Target>
static SgOriginalKernelWakeReadback sgReadOriginalKernelWake(ULONGLONG entry,ULONGLONG boundary,Current current,Target target) {
    using namespace ymcc::sleeppower;
    SgOriginalKernelWakeReadback out;
    if(!entry){out.error=ERROR_INVALID_PARAMETER;return out;}
    KernelWakeReadback evidence(entry,boundary);
    out.error=sgVisitOriginalKernelCycle(current,[&](std::string& xml,ULONGLONG at,int id,int reason){
        KernelWakeRecord event{at,id,reason};
        if(id==107)event.target=target(xml,"TargetState");
        if(id==507&&at>entry&&at>boundary&&reason>=0&&reason!=20){
            const auto residency=ymcc::sleep_residency::makeEvidence(xml);
            // Unknown accounting cannot promote an ordinary exit recovered
            // from history. The explicit manual reason is independently valid.
            event.completed=reason==1||ymcc::sleep_residency::sessionInProgressOnExit(residency)==false;
            if(event.completed&&(reason==7||reason==8)){
                const auto exit=sgKernelSleepExit(residency,at,0,reason);
                event.completed=completedKernelCycle({entry},exit,SG_S0_ENTRY_FAILURE_WINDOW_MS);
            }
        }
        if(evidence.observe(event))out.xml=std::move(xml);
        return true; // Later entries are barriers even if records arrived out of timestamp order.
    });
    if(!out.error)out.fact=evidence.result(sgNowFileTime());
    if(!out.fact.at)out.xml.clear();
    return out;
}

struct SgOriginalKernelCycleReadback {
    ymcc::sleeppower::OriginalKernelCycle fact;
    DWORD error=ERROR_SUCCESS;
};
static SgOriginalKernelCycleReadback sgReadOriginalKernelCycle(const ymcc::sleeppower::SharedPowerV1& power,
                                                               ULONGLONG observed,ULONGLONG closed) {
    using namespace ymcc::sleeppower;
    SgOriginalKernelCycleReadback out;
    const auto current=[&]{return !g_exitRequested.load(std::memory_order_acquire)&&
        powerObservationCurrent(&readSharedSystemPower,power);};
    ReverseKernelCycle evidence;
    out.error=sgVisitOriginalKernelCycle(current,[&](const std::string& xml,ULONGLONG at,int id,int reason){
        if(id==107){out.fact.result=KernelCycleResult::SystemWake;return false;}
        if(id==507){
            auto exit=sgKernelSleepExit(ymcc::sleep_residency::makeEvidence(xml),at,power.generation,reason);
            evidence.exit(exit.sessionUs ? exit : KernelCycleSample{at,power.generation,reason});
            return true;
        }
        // A newer entry, even if not eligible, ends this read's cycle.
        out.fact=evidence.original({at,power.generation,reason},observed,closed,SG_S0_ENTRY_FAILURE_WINDOW_MS);
        return false;
    });
    return out;
}
