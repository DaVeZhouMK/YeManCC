#include "sleep_kernel_cycle.h"

// Shared Windows evidence owner. These observations never admit power actions.
static ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;
static std::atomic<ULONGLONG> g_sgKernelSystemWake{0};
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
        return {at,generation,reason,span,inProgress!=values.end()&&inProgress->get<bool>()};
    }catch(...){return {};}
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
    const auto start=GetTickCount64();
    EVT_HANDLE query=nullptr,events[kernelCycleReadLimit]{};DWORD count=0;
    const auto close=[&]{
        for(auto& event:events)if(event){if(!EvtClose(event))out.error=GetLastError();event=nullptr;}
        if(query){if(!EvtClose(query))out.error=GetLastError();query=nullptr;}
    };
    try{
        if(!current())return {{},ERROR_CANCELLED};
        query=EvtQuery(nullptr,L"System",L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=506 or EventID=507 or EventID=107)]]",
                       EvtQueryChannelPath|EvtQueryReverseDirection);
        if(!query)return {{},GetLastError()};
        if(!EvtNext(query,kernelCycleReadLimit,events,0,0,&count)){out.error=GetLastError();close();return out;}
        if(count>kernelCycleReadLimit){close();return {{},ERROR_INVALID_DATA};}
        ReverseKernelCycle evidence;
        std::vector<wchar_t> buffer(32768,L'\0');
        for(DWORD index=0;index<count;++index){
            if(!current()||GetTickCount64()-start>1000){out.error=ERROR_CANCELLED;break;}
            DWORD used=0,properties=0;
            if(!EvtRender(nullptr,events[index],EvtRenderEventXml,static_cast<DWORD>(buffer.size()*sizeof(wchar_t)),
                          buffer.data(),&used,&properties)){out.error=GetLastError();break;}
            if(used<sizeof(wchar_t)||used>buffer.size()*sizeof(wchar_t)||buffer[(used/sizeof(wchar_t))-1]!=L'\0'){
                out.error=ERROR_INVALID_DATA;break;
            }
            const auto xml=W2U(buffer.data());
            const auto at=sgKernelPowerEventFileTime(xml);
            const auto id=sgKernelPowerEventId(xml),reason=sgKernelPowerReason(xml);
            if(!at||at>sgNowFileTime()){out.error=ERROR_INVALID_DATA;break;}
            if(id==107){out.fact.result=KernelCycleResult::SystemWake;break;}
            if(id==507){
                auto exit=sgKernelSleepExit(ymcc::sleep_residency::makeEvidence(xml),at,power.generation,reason);
                // Keep the original time and reason even when sleep counters are unavailable.
                evidence.exit(exit.sessionUs ? exit : KernelCycleSample{at,power.generation,reason});
                continue;
            }
            if(id!=506){out.error=ERROR_INVALID_DATA;break;}
            // A newer entry, even if not eligible, ends this read's cycle.
            out.fact=evidence.original({at,power.generation,reason},observed,closed,SG_S0_ENTRY_FAILURE_WINDOW_MS);
            break;
        }
        close();
        if(!current()||GetTickCount64()-start>1000)out.error=ERROR_CANCELLED;
    }catch(...){out.error=ERROR_INVALID_DATA;close();}
    return out;
}
