// Compiles the actual runtime include; every event/timer/power boundary is inert.
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cwchar>
#include <atomic>
#include <memory>
#include <deque>
#include "sleep_abnormal_resleep.h"
#include "sleep_request_fallback.h"
#include "sleep_retry_budget.h"
constexpr UINT SG_ABNORMAL_TIMER_ID=0xA212,WM_SG_ABNORMAL_EVIDENCE=WM_APP+825;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static bool g_guardEnabled=true,g_sgRetryEntryFailure=true,g_sgResleepEnabled=true;
static std::atomic<bool> g_exitRequested{false};
static std::atomic<ULONGLONG> g_sgLastPowerButtonWakeEventFileTime{0};
enum class PowerLifecycle{Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspended};
enum class SgRetryKind{None,EntryFailure,UnexpectedWake};
struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
static std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
static ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
static constexpr ULONGLONG testEpoch=134372000000000000ULL;
static ULONGLONG testTick=100000,testFileTime=testEpoch,testDue=0;
static ULONGLONG testEtwQueryDelayMs=0,testEtwQueryDelayAt=0,testPrivilegeDelayMs=0,testStopDelayMs=0,testCloseDelayMs=0,testJoinDelayMs=0;
static unsigned testEtwClockMode=0;
static bool testAdapterFinalQuery=false;
static unsigned testSubscriptions=0,testCloses=0,testSleepCalls=0,testPowerOffCalls=0,testTimerWrites=0;
static bool testChannel=true,testAccepted=true,testPowerOffAccepted=true,testRetryBusy=false,testPrivilege=true,testWakeDuringPrivilege=false;
static bool testSuspendOnHibernate=false,testTimerAvailable=true,testSuccessDuringPrivilege=false,testReplaceMarkerDuringRequest=false;
static DWORD testError=0;
static std::deque<MSG> testMessages;
static std::string testXml;
static bool testPostAvailable=true;
struct SgSystemSleepRequestResult{bool accepted=false;DWORD requestError=0,shutdownPrivilegeError=0;ymcc::sleeprequest::Mode target=ymcc::sleeprequest::Mode::None;};
static void sgAbnormalSuspendBoundary();
static bool sgAbnormalDispatchReady();
static void sgAbnormalPostCompleted(const std::string&,ULONGLONG);
static bool sgPrepareSleepPrivilege(SgSystemSleepRequestResult& result){
    testTick+=testPrivilegeDelayMs;testFileTime+=testPrivilegeDelayMs*10000;
    if(testWakeDuringPrivilege)g_sgLastPowerButtonWakeEventFileTime=testFileTime;
    if(testSuccessDuringPrivilege)sgAbnormalPostCompleted("<Event><EventData><Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data></EventData></Event>",testFileTime);
    if(!testPrivilege)result.shutdownPrivilegeError=ERROR_ACCESS_DENIED;
    return testPrivilege;
}
static SgSystemSleepRequestResult sgRequestSystemSleep(ymcc::sleeprequest::Mode first,ymcc::sleeprequest::Mode,bool staged){
    check(staged,"stage uses shared power adapter");
    if(testAdapterFinalQuery&&!sgAbnormalDispatchReady())return {false,ERROR_CANCELLED,0,first};
    ++testSleepCalls;g_sgAutomaticSleepTarget=first;
    if(testSuspendOnHibernate&&first==ymcc::sleeprequest::Mode::Hibernate)sgAbnormalSuspendBoundary();
    if(testReplaceMarkerDuringRequest)g_sgInternalSleepRequest={SgRetryKind::UnexpectedWake,testTick+1,testFileTime+1};
    return {testAccepted,testAccepted?0UL:ERROR_NOT_SUPPORTED,0,first};
}
struct TestFanBoundary{void retryBoundary(ULONGLONG,ULONGLONG){}};
static TestFanBoundary g_fanPhysicalWake;
static void sgMarkInternalSleepRequest(SgRetryKind kind){g_sgInternalSleepRequest={kind,testTick,testFileTime};}
static void sgClearInternalSleepRequest(const char*){g_sgInternalSleepRequest={};}
static bool sgSleepRetryActive(){return testRetryBusy;}
static bool sgModernStandbyRepairSupported(){return true;}
static ULONGLONG sgNowFileTime(){return testFileTime;}
static int sgKernelPowerEventId(const std::string& xml){return std::stoi(xml.substr(xml.find("<EventID>")+9));}
static ULONGLONG testGetTick(){return testTick;}
static BOOL testKillTimer(HWND,UINT_PTR){testDue=0;return TRUE;}
static UINT_PTR testSetTimer(HWND,UINT_PTR id,UINT ms,TIMERPROC){++testTimerWrites;if(!testTimerAvailable)return 0;testDue=testTick+ms;return id;}
static BOOL testPost(HWND h,UINT id,WPARAM w,LPARAM l){if(!testPostAvailable)return FALSE;MSG m{};m.hwnd=h;m.message=id;m.wParam=w;m.lParam=l;testMessages.push_back(m);return TRUE;}
static BOOL testPeek(LPMSG m,HWND,UINT,UINT,UINT){if(testMessages.empty())return FALSE;*m=testMessages.front();testMessages.pop_front();return TRUE;}
static BOOL testPowerOff(UINT flags,DWORD){check(flags==EWX_POWEROFF,"normal shutdown never forces applications");++testPowerOffCalls;return testPowerOffAccepted;}
static void testSetError(DWORD v){testError=v;}
static DWORD testGetError(){return testError;}

// All ETW/TDH/thread boundaries are inert. No OS source or worker is started.
static EVENT_TRACE_LOGFILEW testEtwLog{};
static DWORD testEtwStartError=ERROR_SUCCESS,testEtwEnableError=ERROR_SUCCESS,testEtwStatsError=ERROR_SUCCESS,testEtwStopError=ERROR_SUCCESS;
static bool testEtwOpen=true,testEtwThread=true,testEtwJoin=true,testEtwSchema=true,testEtwProperty=true;
static ULONG testEtwLoss=0;static unsigned testEtwStartCalls=0,testEtwStops=0,testEtwQueries=0,testEtwThreads=0,testEtwJoins=0,testEtwPropertyCalls=0;
static unsigned testEtwInfoCalls=0,testEtwSizeCalls=0;
static USHORT testEtwRecordFlags=0,testEtwRecordTask=0;
static ULONG testEtwPropertySizeOverride=0;
static bool testUnknownDuringEtwQuery=false;
static bool testEtwWrongType=false,testWakeDuringEtwQuery=false,testLossDuringStop=false,testSuccessDuringEtwQuery=false,testGenerationDuringEtwQuery=false;
static unsigned testEtwWakeAtQuery=0,testEtwLossAtQuery=0,testEtwLossKind=0;static unsigned testEtwDuplicate=0;
static unsigned testHealthyDrainAt=0;
static void testDrainHealthy(unsigned point);
static void testEtwDeliverQueued(BYTE version);
static DWORD testPid(){return 731;}
static ULONG testStartTrace(PTRACEHANDLE handle,LPCWSTR name,PEVENT_TRACE_PROPERTIES properties){
    ++testEtwStartCalls;
    check(name&&std::wstring(name).find(L"YMCC-AbnormalSleep-731-")==0,"ETW owns unique episode name without attaching another session");
    check(properties->LogFileNameOffset==0&&(properties->LogFileMode&EVENT_TRACE_REAL_TIME_MODE)&&!(properties->LogFileMode&EVENT_TRACE_FILE_MODE_SEQUENTIAL),"ETW is real-time only with no ETL path");
    check(properties->Wnode.ClientContext==2&&properties->MaximumBuffers<=4,"ETW uses original system FILETIME and bounded buffers");
    check((properties->LogFileMode&EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING)!=0,"ETW does not reserve a buffer pair per processor");
    if(!testChannel)return ERROR_ACCESS_DENIED;
    if(testEtwStartError)return testEtwStartError;
    ++testSubscriptions;*handle=41;return ERROR_SUCCESS;
}
static ULONG testEnableTrace(TRACEHANDLE handle,LPCGUID provider,ULONG control,UCHAR level,ULONGLONG any,ULONGLONG all,ULONG timeout,PENABLE_TRACE_PARAMETERS parameters){
    const GUID expected{0x331c3b3a,0x2005,0x44c2,{0xac,0x5e,0x77,0x22,0x0c,0x37,0xd6,0xb4}};
    check(handle==41&&provider&&IsEqualGUID(*provider,expected)&&control==EVENT_CONTROL_CODE_ENABLE_PROVIDER&&level==TRACE_LEVEL_INFORMATION&&any==0x404&&!all&&!timeout,"ETW enables only Kernel-Power diagnostic keywords");
    check(parameters&&parameters->Version==ENABLE_TRACE_PARAMETERS_VERSION_2&&parameters->FilterDescCount==1&&parameters->EnableFilterDesc->Type==EVENT_FILTER_TYPE_EVENT_ID,"ETW requires supported ID filter without broad fallback");
    const auto filter=reinterpret_cast<const EVENT_FILTER_EVENT_ID*>(parameters->EnableFilterDesc->Ptr);
    check(filter->FilterIn&&filter->Count==3&&filter->Events[0]==161&&filter->Events[1]==522&&filter->Events[2]==554,"ETW provider event filter is exactly161/522/554");
    return testEtwEnableError;
}
static TRACEHANDLE testOpenTrace(PEVENT_TRACE_LOGFILEW logfile){
    check(logfile&&!logfile->LogFileName&&logfile->ProcessTraceMode==(PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD),"consumer converts event timestamps and never reads a log file");
    testEtwLog=*logfile;return testEtwOpen?42:INVALID_PROCESSTRACE_HANDLE;
}
static ULONG testProcessTrace(PTRACEHANDLE,ULONG,const FILETIME*,const FILETIME*){return ERROR_SUCCESS;}
static ULONG testControlTrace(TRACEHANDLE handle,LPCWSTR name,PEVENT_TRACE_PROPERTIES properties,ULONG control){
    check(handle==41&&!name,"ETW control uses only the owned session handle");
    if(control==EVENT_TRACE_CONTROL_STOP){testTick+=testStopDelayMs;testFileTime+=testStopDelayMs*10000;++testEtwStops;++testCloses;if(testLossDuringStop)properties->EventsLost=1;
        testDrainHealthy(1);
        return testEtwStopError;}
    check(control==EVENT_TRACE_CONTROL_QUERY,"ETW source query never starts or rewrites a session");++testEtwQueries;
    if(!testEtwQueryDelayAt||testEtwQueryDelayAt==testEtwQueries){if(testEtwClockMode!=2)testTick+=testEtwQueryDelayMs;if(testEtwClockMode!=1)testFileTime+=testEtwQueryDelayMs*10000;}
    if(testWakeDuringEtwQuery||testEtwWakeAtQuery==testEtwQueries)g_sgLastPowerButtonWakeEventFileTime=testFileTime;
    if(testSuccessDuringEtwQuery)sgAbnormalPostCompleted("<Event><EventData><Data Name=\"DurationInUs\">1000000</Data><Data Name=\"DripsResidencyInUs\">1000</Data><Data Name=\"HwDripsResidencyInUs\">900</Data><Data Name=\"SleepEntered\">true</Data></EventData></Event>",testFileTime);
    if(testGenerationDuringEtwQuery)++testPowerGeneration;
    if(testUnknownDuringEtwQuery) {testXml="<Event><System><EventID>161</EventID></System><EventData><Data Name=\"DurationThisPeriodMs\">1</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">0</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data></EventData></Event>";testEtwDeliverQueued(0);}
    properties->EventsLost=properties->LogBuffersLost=properties->RealTimeBuffersLost=0;
    const auto loss=testEtwLoss||(testEtwLossAtQuery==testEtwQueries);
    if(testEtwLossKind==0)properties->EventsLost=loss;else if(testEtwLossKind==1)properties->LogBuffersLost=loss;else properties->RealTimeBuffersLost=loss;return testEtwStatsError;
}
static ULONG testCloseTrace(TRACEHANDLE trace){testTick+=testCloseDelayMs;testFileTime+=testCloseDelayMs*10000;check(trace==42,"consumer closes only owned trace");++testCloses;testDrainHealthy(2);return ERROR_SUCCESS;}
static HANDLE testCreateThread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD){++testEtwThreads;return testEtwThread?reinterpret_cast<HANDLE>(43):nullptr;}
static DWORD testWait(HANDLE handle,DWORD ms){testTick+=testJoinDelayMs;testFileTime+=testJoinDelayMs*10000;check(handle==reinterpret_cast<HANDLE>(43)&&ms<=1500,"ETW consumer join is bounded");++testEtwJoins;testDrainHealthy(3);return testEtwJoin?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL testCloseHandle(HANDLE handle){check(handle==reinterpret_cast<HANDLE>(43),"only owned consumer worker handle closes");return TRUE;}
static ULONG testEventInformation(PEVENT_RECORD event,ULONG,PTDH_CONTEXT,PTRACE_EVENT_INFO info,PULONG size){
    ++testEtwInfoCalls;
    if(!testEtwSchema)return ERROR_NOT_FOUND;
    const wchar_t* names161[]={L"DurationThisPeriodMs",L"ResiliencyPhaseNonActivatedNoDripsMs",L"NonActivatedCpuTimeMs"};
    const wchar_t* names522[]={L"HwDripsTotalTimeValid",L"DripsTotalTimeThisPeriodUs",L"HwDripsTotalTimeThisPeriodUs"};
    const auto names=event->EventHeader.EventDescriptor.Id==161?names161:names522;
    ULONG required=static_cast<ULONG>(offsetof(TRACE_EVENT_INFO,EventPropertyInfoArray)+3*sizeof(EVENT_PROPERTY_INFO));
    for(unsigned i=0;i<3;++i)required+=static_cast<ULONG>((std::wcslen(names[i])+1)*sizeof(wchar_t));
    if(!info||*size<required){*size=required;return ERROR_INSUFFICIENT_BUFFER;}
    std::memset(info,0,required);info->ProviderGuid=event->EventHeader.ProviderId;info->EventDescriptor=event->EventHeader.EventDescriptor;info->PropertyCount=info->TopLevelPropertyCount=3;
    ULONG offset=static_cast<ULONG>(offsetof(TRACE_EVENT_INFO,EventPropertyInfoArray)+3*sizeof(EVENT_PROPERTY_INFO));
    for(unsigned i=0;i<3;++i){auto& property=info->EventPropertyInfoArray[i];property.NameOffset=offset;property.count=1;
        property.nonStructType.InType=static_cast<USHORT>(event->EventHeader.EventDescriptor.Id==161?TDH_INTYPE_UINT32:i==0?TDH_INTYPE_BOOLEAN:TDH_INTYPE_UINT64);
        if(testEtwWrongType&&i==0)property.nonStructType.InType=TDH_INTYPE_ANSISTRING;
        const auto bytes=(std::wcslen(names[i])+1)*sizeof(wchar_t);std::memcpy(reinterpret_cast<BYTE*>(info)+offset,names[i],bytes);offset+=static_cast<ULONG>(bytes);
    }
    if(testEtwDuplicate)info->EventPropertyInfoArray[1].NameOffset=info->EventPropertyInfoArray[0].NameOffset;
    *size=required;return ERROR_SUCCESS;
}
static std::optional<ULONGLONG> testEtwValue(PPROPERTY_DATA_DESCRIPTOR property){
    const auto name=reinterpret_cast<const wchar_t*>(property->PropertyName);std::wstring wide(name);std::string key;for(const auto c:wide)key.push_back(static_cast<char>(c));
    const auto data=ymcc::sleep_residency::readEventData(testXml);const auto value=data.values.find(key);
    if(value==data.values.end()||data.duplicates.count(key))return {};
    if(value->second=="true")return 1;if(value->second=="false")return 0;
    return ymcc::sleep_residency::unsignedValue(value->second);
}
static ULONG testPropertySize(PEVENT_RECORD event,ULONG,PTDH_CONTEXT,ULONG count,PPROPERTY_DATA_DESCRIPTOR property,PULONG size){
    ++testEtwSizeCalls;
    if(!testEtwProperty||count!=1||!testEtwValue(property))return ERROR_NOT_FOUND;
    *size=event->EventHeader.EventDescriptor.Id==161||std::wcscmp(reinterpret_cast<const wchar_t*>(property->PropertyName),L"HwDripsTotalTimeValid")==0?4:8;if(testEtwPropertySizeOverride)*size=testEtwPropertySizeOverride;return ERROR_SUCCESS;
}
static ULONG testProperty(PEVENT_RECORD,ULONG,PTDH_CONTEXT,ULONG,PPROPERTY_DATA_DESCRIPTOR property,ULONG size,PBYTE buffer){
    ++testEtwPropertyCalls;const auto value=testEtwValue(property);if(!value||size>sizeof(*value))return ERROR_INVALID_DATA;std::memcpy(buffer,&*value,size);return ERROR_SUCCESS;
}
#define StartTraceW testStartTrace
#define EnableTraceEx2 testEnableTrace
#define OpenTraceW testOpenTrace
#define ProcessTrace testProcessTrace
#define ControlTraceW testControlTrace
#define CloseTrace testCloseTrace
#define CreateThread testCreateThread
#define WaitForSingleObject testWait
#define CloseHandle testCloseHandle
#define GetCurrentProcessId testPid
#define TdhGetEventInformation testEventInformation
#define TdhGetPropertySize testPropertySize
#define TdhGetProperty testProperty

#define GetTickCount64 testGetTick
#define KillTimer testKillTimer
#define SetTimer testSetTimer
#define PostMessageW testPost
#define PeekMessageW testPeek
#define ExitWindowsEx testPowerOff
#define SetLastError testSetError
#define GetLastError testGetError
#include "sleep_abnormal_resleep_native.inc"
#undef StartTraceW
#undef EnableTraceEx2
#undef OpenTraceW
#undef ProcessTrace
#undef ControlTraceW
#undef CloseTrace
#undef CreateThread
#undef WaitForSingleObject
#undef CloseHandle
#undef GetCurrentProcessId
#undef TdhGetEventInformation
#undef TdhGetPropertySize
#undef TdhGetProperty
#undef GetTickCount64
#undef KillTimer
#undef SetTimer
#undef PostMessageW
#undef PeekMessageW
#undef ExitWindowsEx
#undef SetLastError
#undef GetLastError
static void testAt(ULONGLONG ms){testTick=100000+ms;testFileTime=testEpoch+ms*10000;}
static void testPump(){while(!testMessages.empty()){const auto m=testMessages.front();testMessages.pop_front();sgAbnormalHandleEvent(m.lParam);}}
static void testReset(){
    runtimeFactObserver=nullptr;
    testEtwQueryDelayMs=testEtwQueryDelayAt=testPrivilegeDelayMs=testStopDelayMs=testCloseDelayMs=testJoinDelayMs=0;testEtwInfoCalls=testEtwSizeCalls=0;
    testEtwClockMode=0;testAdapterFinalQuery=testUnknownDuringEtwQuery=false;testEtwRecordFlags=testEtwRecordTask=0;testEtwPropertySizeOverride=0;
    testEtwJoin=true;testEtwStopError=ERROR_SUCCESS;sgAbnormalStop("fixture-reset");g_sgAbnormalLastHumanIntent=g_sgAbnormalLastOwnedRequest=0;g_sgAbnormalHumanIntent=0;g_sgResleepEnabled=true;g_sgAbnormalSuccessOrigin=0;g_sgAbnormalDropped=false;testAt(0);testDue=0;testSubscriptions=testCloses=testSleepCalls=testPowerOffCalls=testTimerWrites=0;
    testEtwLog={};testEtwStartError=testEtwEnableError=testEtwStatsError=testEtwStopError=ERROR_SUCCESS;testEtwLoss=0;testEtwStartCalls=testEtwStops=testEtwQueries=testEtwThreads=testEtwJoins=testEtwPropertyCalls=0;testEtwOpen=testEtwThread=testEtwJoin=testEtwSchema=testEtwProperty=true;testEtwWrongType=testWakeDuringEtwQuery=testLossDuringStop=testSuccessDuringEtwQuery=testGenerationDuringEtwQuery=false;testEtwWakeAtQuery=testEtwLossAtQuery=testEtwLossKind=0;testPowerGeneration=771;testEtwDuplicate=0;
    testTimerAvailable=testChannel=testAccepted=testPowerOffAccepted=testPrivilege=true;testRetryBusy=testWakeDuringPrivilege=testSuspendOnHibernate=testSuccessDuringPrivilege=testReplaceMarkerDuringRequest=false;g_sgInternalSleepRequest={};
    g_sgLastPowerButtonWakeEventFileTime=0;g_guardEnabled=g_sgRetryEntryFailure=true;g_exitRequested=false;
    g_powerLifecycle=PowerLifecycle::Suspended;g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
    g_sgAutomaticSleepBudget.confirmedUserWake();testError=0;captured.clear();sgAbnormalDisplay(0);
}
static void testEtwDeliverQueued(BYTE version=0){
    EVENT_RECORD event{};event.EventHeader.ProviderId=SG_ABNORMAL_KERNEL_PROVIDER;event.EventHeader.EventDescriptor.Id=static_cast<USHORT>(sgKernelPowerEventId(testXml));event.EventHeader.EventDescriptor.Version=version;event.EventHeader.EventDescriptor.Task=testEtwRecordTask;event.EventHeader.Flags=testEtwRecordFlags;event.EventHeader.TimeStamp.QuadPart=static_cast<LONGLONG>(testFileTime);event.UserContext=&g_sgAbnormalEtw;sgAbnormalEtwEvent(&event);
}
static std::string testEvent(unsigned id,const std::string& fields){return "<Event><System><EventID>"+std::to_string(id)+"</EventID></System><EventData>"+fields+"</EventData></Event>";}
static void testDrainHealthy(unsigned point){
    if(testHealthyDrainAt!=point)return;testHealthyDrainAt=0;
    testXml=testEvent(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8</Data>");
    testEtwDeliverQueued(0);
}
static void testDeliver(unsigned id,const std::string& fields){testXml=testEvent(id,fields);testEtwDeliverQueued();testPump();}
static void testBad(ULONGLONG end,ULONGLONG period){
    testAt(end);testDeliver(161,"<Data Name=\"DurationThisPeriodMs\">"+std::to_string(period)+"</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">"+std::to_string(period)+"</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
}
// EXPLICIT HISTORICAL SCOPE: not a production Arm/Owned consumer. Reproduce
// the original entry identity/guards and execute the SAME real StartPhase and
// source admission. No permissive fixture flag or separate model engine.
static void testArmHistoricalStaged(int reason,ULONGLONG eventTime) {
    if(reason!=1||!g_guardEnabled||!g_sgRetryEntryFailure||g_exitRequested.load()||!sgModernStandbyRepairSupported())return;
    if(eventTime<=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)||eventTime<=g_sgAbnormalLastHumanIntent)return;
    g_sgAbnormalLastHumanIntent=eventTime;
    sgAbnormalStartPhase(eventTime,eventTime,false);
}
static bool testObserveHistoricalOwnedRetry(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime) {
    if(!originalHumanIntent||requestFileTime<originalHumanIntent||!g_guardEnabled||!g_sgRetryEntryFailure||
        !g_sgResleepEnabled||g_exitRequested.load()||!sgModernStandbyRepairSupported()||
        g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=originalHumanIntent||
        !g_sgAutomaticSleepBudget.requests()||g_sgInternalSleepRequest.kind!=SgRetryKind::UnexpectedWake||
        g_sgInternalSleepRequest.dispatchFileTime!=requestFileTime)return false;
    if(requestFileTime<=g_sgAbnormalLastOwnedRequest)
        return g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==requestFileTime&&g_sgAbnormalEpisode.originalHumanIntent==originalHumanIntent;
    g_sgAbnormalLastOwnedRequest=requestFileTime;
    g_sgAbnormalLastHumanIntent=(std::max)(g_sgAbnormalLastHumanIntent,originalHumanIntent);
    return sgAbnormalStartPhase(originalHumanIntent,requestFileTime,true);
}

static unsigned abnormalRuntimeTests(const json& fixture){
    const auto initialChecks=sleepEvidenceChecks;
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
    ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testEpoch-1);
    sgAbnormalTick();check(g_sgLastPowerButtonWakeEventFileTime==testFileTime&&!g_sgAbnormalEpisode.active()&&!testSleepCalls,
        "out-of-order old button wake cannot overwrite a newer manual veto");
    testReset();check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"untriggered flow has no timer or subscription");
    testArmHistoricalStaged(3,testEpoch);check(!testSubscriptions&&!testDue,"menu sleep cannot start power-button-only flow");
    g_sgRetryEntryFailure=false;testArmHistoricalStaged(1,testEpoch);check(!testSubscriptions,"disabled option cannot arm");
    testReset();testChannel=false;testArmHistoricalStaged(1,testEpoch);check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"unavailable source stays unknown without enabling channel");
    testReset();testArmHistoricalStaged(1,testEpoch);const auto due=testDue;testArmHistoricalStaged(1,testEpoch);
    check(testSubscriptions==1&&testDue==due,"duplicate button506 never resets origin or subscribes twice");
    testAt(30000);sgAbnormalTick();check(!testSleepCalls&&!testPowerOffCalls,"missing evidence at30seconds does not authorize sleep");
    testAt(45000);sgAbnormalTick();check(!testSleepCalls&&g_sgAbnormalEpisode.next==1,"missed30second stage is not replayed");
    testAt(900000);sgAbnormalTick();check(!g_sgAbnormalEpisode.active()&&!testPowerOffCalls&&!testDue,"late resume cannot execute expired shutdown");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);
    check(testSleepCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"known bad original period dispatches one30second sleep");
    sgAbnormalTick();check(testSleepCalls==1,"same stage cannot dispatch twice");
    testAt(31000);g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgAbnormalTick();
    check(!g_sgAbnormalEpisode.active()&&!testDue&&testCloses==2,"callback-side power button cancels and closes live source");
    testAt(360000);sgAbnormalTick();check(testSleepCalls==1&&!testPowerOffCalls,"old timers after user wake cannot sleep or power off");
    // A new physical intent is allowed; no interception of the user's Windows request.
    testAt(370000);testArmHistoricalStaged(1,testFileTime);check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==testFileTime,"fresh manual sleep is not blocked by old episode");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(20000);
    const auto originalTick=g_sgAbnormalEpisode.startTick;sgAbnormalInternalEntry();
    check(g_sgAbnormalEpisode.origin==testEpoch&&g_sgAbnormalEpisode.startTick==originalTick,"internal suspend retains original stage origin");
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data>"),testFileTime);testPump();
    check(!g_sgAbnormalEpisode.active()&&!testDue,"completed Windows low-power success cancels all future stages");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(20000);
    testDeliver(554,"<Data Name=\"DfxTransitionCount\">1</Data><Data Name=\"Ps4TransitionCount\">1</Data>");
    check(g_sgAbnormalEpisode.active(),"native action counts alone do not claim success");
    testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9000</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8000</Data>");
    check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSleepCalls,"valid Windows effective-low-power evidence ends every remaining action");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(20000);sgAbnormalDisplay(1);sgAbnormalTick();
    check(g_sgAbnormalEpisode.active()&&!testSleepCalls,"display change is not classified as manual power-button wake");
    sgAbnormalStopAsync();check(!sgAbnormalPermitted(),"OFF revokes dispatch immediately before UI cancellation");testPump();check(!testDue&&!g_sgAbnormalEtw.session,"OFF releases source and timer");
    for(const bool accepted:{false,true}){
        testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);
        testAccepted=false;testBad(330000,30000);check(testSleepCalls==3&&g_sgAutomaticSleepBudget.requests()==3,"two resleeps and S4 share existing three-call budget");
        testPowerOffAccepted=accepted;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
        check(testPowerOffCalls==1&&!g_sgAbnormalOrigin.load()&&!testDue&&!g_sgAbnormalEtw.session,"shutdown success or failure is terminal with no source or timer");
        sgAbnormalTick();testBad(361000,1000);check(testPowerOffCalls==1,"terminal shutdown never attempts twice");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testBad(330000,30000);
    testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls,"accepted-but-unconfirmed S4 never authorizes shutdown");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testSuspendOnHibernate=true;testBad(330000,30000);
    check(!g_sgAbnormalEpisode.active()&&!testDue,"Windows hibernate suspend boundary ends pending shutdown");
    testReset();testArmHistoricalStaged(1,testEpoch);g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();testBad(30000,30000);
    check(!testSleepCalls&&!g_sgAbnormalEpisode.active(),"other repairs consume the same budget without refill");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testWakeDuringPrivilege=true;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!g_sgAbnormalOrigin.load(),"wake racing shutdown privilege wins with no second attempt");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    testDeliver(161,"<Data Name=\"DurationThisPeriodMs\">1000</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">9000</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
    check(!testSleepCalls,"cumulative or contradictory watchdog values never become a live failure period");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testEpoch+30000*10000ULL,161},testFileTime);
    testAt(36000);sgAbnormalTick();check(!testSleepCalls,"stale positive evidence cannot authorize action");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);testXml=testEvent(161,"<Data Name=\"DurationThisPeriodMs\">30000</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">30000</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
    for(int i=0;i<20;++i)testEtwDeliverQueued();
    check(g_sgAbnormalPending<=8&&testMessages.size()<=8,"source burst keeps a bounded event queue");sgAbnormalStop("test");check(g_sgAbnormalPending==0,"cancellation drains all owned queued observations");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(230000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch+200000*10000ULL,testFileTime,161},testFileTime);
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data>"),testEpoch+210000*10000ULL);
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testDue,"late success within original intent cancels newer bad evidence and due stage");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testSuccessDuringPrivilege=true;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load(),"Windows success during shutdown preparation vetoes final API and ends flow");
    testReset();testArmHistoricalStaged(1,testEpoch);testAccepted=false;testBad(30000,30000);
    check(g_sgInternalSleepRequest.kind==SgRetryKind::None,"rejected automatic request clears only its own marker before next manual sleep");
    testReset();testArmHistoricalStaged(1,testEpoch);testAccepted=false;testReplaceMarkerDuringRequest=true;testBad(30000,30000);
    check(g_sgInternalSleepRequest.kind==SgRetryKind::UnexpectedWake,"rejected staged request preserves a newer repair owner's marker");
    testReset();testTimerAvailable=false;testArmHistoricalStaged(1,testEpoch);
    check(!g_sgAbnormalEpisode.active()&&!g_sgAbnormalEtw.session,"timer allocation failure releases subscription and episode");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testPrivilege=false;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load()&&!g_sgAbnormalEtw.session,"shutdown privilege denial ends flow with no second attempt");
    // A successful OS event must veto a due timer before the UI queue is pumped.
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    testXml=testEvent(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8</Data>");
    testEtwDeliverQueued();
    check(!sgAbnormalPermitted(),"callback-side Windows success immediately revokes dispatch before UI queue");
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testPowerOffCalls&&!testDue,"queued Windows success beats due timer");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    testEtwLog.EventsLost=1;sgAbnormalEtwBuffer(&testEtwLog);
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testDue,"subscription error immediately vetoes due timer");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,1);
    check(!testSleepCalls,"momentary no-DRIPS sample cannot authorize a thirty-second retry");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Unknown,0,testFileTime,161},testFileTime);
    sgAbnormalTick();check(!testSleepCalls,"contradictory observation at identical timestamp erases bad evidence");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    g_sgAbnormalDropped=true;sgAbnormalTick();check(!testSleepCalls&&!testDue,"queue gap vetoes due action before event handling");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);testFileTime+=6000*10000ULL;
    sgAbnormalTick();check(!g_sgAbnormalEpisode.active()&&!testDue,"wall-clock drift terminates rather than replaying deadlines");
    for(const auto& c:fixture.at("cases"))if(c.contains("fieldClassification")){
        testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);sgAbnormalTick();
        check(!testSleepCalls,"ROG completed507 never becomes live30second failure evidence");
        testAt(static_cast<ULONGLONG>(c.at("historicalReplay").at("powerButtonWakeElapsedMs").get<double>()+1));g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgAbnormalTick();
        testAt(360000);sgAbnormalTick();check(!testPowerOffCalls&&!testSleepCalls,"ROG historical active wake cancels230/330/360 without invented live data");
    }
    for(const unsigned stage:{0U,1U,2U,3U}){
        testReset();testArmHistoricalStaged(1,testEpoch);
        if(stage>0)testBad(30000,30000);
        if(stage>1)testBad(230000,30000);
        if(stage>2){testAccepted=false;testBad(330000,30000);testBad(340000,10000);testBad(350000,10000);}
        const auto previousSleepCalls=testSleepCalls,previousPowerOffCalls=testPowerOffCalls;
        const auto end=ymcc::abnormalsleep::Episode::deadlines[stage];testAt(end);
        ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
        testBad(end,stage==3?10000:30000);
        check(!g_sgAbnormalEpisode.active()&&!testDue&&!g_sgAbnormalEtw.session,"every30/230/330/360stage terminates on callback-side human wake");
        testAt(900000);sgAbnormalTick();
        check(testSleepCalls==previousSleepCalls&&testPowerOffCalls==previousPowerOffCalls,"stale timers cannot dispatch any power action after human wake");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testBad(340000,10000);testBad(350000,10000);
    runtimeFactObserver=[](const char* event,const json& details){
        if(std::string(event)=="abnormal-sleep-finished"&&details.value("trigger",std::string{})=="normal-poweroff-attempt")
            ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
    };
    testBad(360000,10000);
    check(!testPowerOffCalls&&!g_sgAbnormalEpisode.active()&&!testDue,"human wake during final shutdown cleanup logging vetoes the only shutdown attempt");
    sgAbnormalTick();check(!testPowerOffCalls,"shutdown veto stays terminal without retry");

    // An authorized parent request already owns one call in the shared budget.
    const auto ownRetry=[](ULONGLONG offset){
        testAt(offset);check(g_sgAutomaticSleepBudget.consume(),"parent owned retry consumes shared budget before observation");
        sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
        return testObserveHistoricalOwnedRetry(testEpoch,testFileTime);
    };
    const auto health507=[](ULONGLONG duration){return testEvent(507,"<Data Name=\"DurationInUs\">"+std::to_string(duration)+"</Data><Data Name=\"DripsResidencyInUs\">1000</Data><Data Name=\"HwDripsResidencyInUs\">900</Data><Data Name=\"SleepEntered\">true</Data>");};
    testReset();testChannel=false;
    check(!sgAbnormalSourceReady()&&!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"read-only disabled source probe changes no episode or listener");
    testChannel=true;check(!sgAbnormalSourceReady()&&!testDue&&!testSubscriptions,"idle source status never starts a session or pretends a prepared channel is ready");
    testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);
    check(testSleepCalls==2&&g_sgAbnormalEpisode.next==2,"log2 episode ETW preserves original thirty and230second stages");
    testReset();testChannel=false;testArmHistoricalStaged(1,testEpoch);testChannel=true;
    check(ownRetry(2038),"log1 authorized UnexpectedWake establishes a fresh bounded observation");
    check(g_sgAutomaticSleepBudget.requests()==1,"owned phase cannot refill original shared budget");
    const auto requestTime=testFileTime,phaseDue=testDue;
    sgAbnormalOwnedRetryResult(testEpoch,requestTime,true);sgAbnormalInternalEntry();
    check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.originalHumanIntent==testEpoch&&g_sgAbnormalEpisode.origin==requestTime&&testDue==phaseDue,
        "owned accepted request and PBT retain observation without claiming low-power success");
    check(testObserveHistoricalOwnedRetry(testEpoch,requestTime)&&testDue==phaseDue&&testSubscriptions==1,"duplicate owned request cannot reset phase deadlines or subscribe twice");
    testAt(5000);sgAbnormalPostCompleted(health507(10000000),testFileTime);
    check(sgAbnormalPermitted(),"old507 spanning prior session cannot veto a new owned retry before UI queue");testPump();
    check(g_sgAbnormalEpisode.active(),"old507 delivered after new request cannot terminate the new phase");
    testBad(32038,30000);
    check(testSleepCalls==1&&g_sgAutomaticSleepBudget.requests()==2,"log1 second black-screen activity needs no second507 to reach original thirtysecond retry");
    testBad(232038,30000);check(testSleepCalls==2&&g_sgAutomaticSleepBudget.requests()==3,"owned retry and two staged sleeps share total budget three");
    testBad(332038,30000);check(testSleepCalls==2&&!g_sgAbnormalEpisode.active()&&!testPowerOffCalls,"new owned phase never replenishes shared budget for S4");
    testReset();check(ownRetry(2038),"owned retry missing-evidence control arms");testAt(32038);sgAbnormalTick();
    check(!testSleepCalls&&g_sgAutomaticSleepBudget.requests()==1,"owned retry with missing161 cannot blindly sleep at thirtyseconds");
    testAt(442038);sgAbnormalTick();check(!testDue&&!g_sgAbnormalEtw.session,"owned observation expires with no resident timer or source");
    check(!testObserveHistoricalOwnedRetry(testEpoch,testEpoch+2038*10000ULL),"expired owned request cannot resurrect on duplicate late delivery");
    testReset();testChannel=false;check(!ownRetry(2038)&&!testDue&&!g_sgAbnormalEpisode.active(),"owned retry without reliable source remains unknown and releases observation");
    testReset();testAt(2038);sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
    check(!testObserveHistoricalOwnedRetry(testEpoch,testFileTime)&&!testDue,"unconsumed parent budget cannot establish an owned observation");
    g_sgAutomaticSleepBudget.consume();sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);
    check(!testObserveHistoricalOwnedRetry(testEpoch,testFileTime),"wrong retry owner cannot establish a fresh phase");
    for(const bool oldWake:{false,true}){
        testReset();check(ownRetry(2038),"owned manual veto control arms");
        testAt(32038);g_sgLastPowerButtonWakeEventFileTime=oldWake?testEpoch+1000*10000ULL:testFileTime;
        testBad(32038,30000);check(!testSleepCalls&&!testDue&&!g_sgAbnormalEpisode.active(),"manual wake after original intent wins even when timestamp precedes owned request");
    }
    testReset();check(ownRetry(2038),"owned health control arms");testAt(30000);
    sgAbnormalPostCompleted(health507(10000000),testFileTime);
    check(!sgAbnormalPermitted(),"new phase507 health immediately revokes dispatch before queue");testPump();
    check(!testDue&&!g_sgAbnormalEtw.session&&!g_sgAbnormalEpisode.active(),"new phase507 health closes all observation resources");
    sgAbnormalOwnedRetryResult(testEpoch,testEpoch+2038*10000ULL,true);check(!testDue,"late accepted result cannot resurrect a healthy terminal phase");
    testReset();check(ownRetry(2038),"owned rejected result control arms");
    sgAbnormalOwnedRetryResult(testEpoch,testFileTime,false);check(!testDue&&!g_sgAbnormalEpisode.active()&&g_sgAutomaticSleepBudget.requests()==1,"owned API rejection terminates observation without refunding budget");
    testReset();check(ownRetry(2038),"owned OFF control arms");g_sgRetryEntryFailure=false;sgAbnormalStopAsync();
    sgAbnormalOwnedRetryResult(testEpoch,testEpoch+2038*10000ULL,true);testPump();check(!testDue&&!testSleepCalls&&!g_sgAbnormalEtw.session,"OFF and late API result cannot reactivate owned observation");
    testReset();check(ownRetry(2038),"owned resleep setting control arms");g_sgResleepEnabled=false;testBad(32038,30000);
    check(!testSleepCalls&&!testDue,"owned resleep OFF immediately vetoes staged actions");
    testReset();check(ownRetry(2038),"owned stale result control arms");const auto previousRequest=testFileTime;
    check(ownRetry(4000),"second authorized retry starts new phase using the same shared budget");const auto newerDue=testDue;
    sgAbnormalOwnedRetryResult(testEpoch,previousRequest,false);
    check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==testFileTime&&testDue==newerDue,"late prior result cannot cancel newer owned phase");
    testReset();check(ownRetry(2038),"owned late data control arms");testAt(100000);
    sgAbnormalPostCompleted(health507(50000000),testEpoch+31038*10000ULL);testPump();
    check(g_sgAbnormalEpisode.active(),"late prior-session507 remains ignored even when delivered long after phase start");
    sgAbnormalPostCompleted(health507(1000000),testEpoch+31038*10000ULL);
    check(!sgAbnormalPermitted(),"late but phase-contained507 healthy result is terminal");testPump();
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">352691540</Data><Data Name=\"ActiveResidencyInUs\">352691540</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>"),testFileTime);
    testPump();check(!testSleepCalls,"completed zero507 never promotes missing current interval to an action");


    const auto factCount=[](const char* name){return static_cast<unsigned>(std::count_if(captured.begin(),captured.end(),[&](const auto& row){return row.first==name;}));};
    testReset();check(!sgAbnormalSourceReady()&&!testEtwStartCalls&&!testEtwThreads,"idle source status has no ETW session, source thread or channel write");
    testArmHistoricalStaged(1,testEpoch);check(sgAbnormalSourceReady()&&testEtwThreads==1,"source ready requires one actual mocked episode session and consumer");
    testAt(370001);sgAbnormalTick();check(!sgAbnormalSourceReady()&&!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker&&!testDue,"no-data expiry closes episode source and worker without timers");
    for(const DWORD error:{ERROR_ACCESS_DENIED,ERROR_ALREADY_EXISTS,ERROR_NOT_SUPPORTED}){
        testReset();testEtwStartError=error;testArmHistoricalStaged(1,testEpoch);
        check(!g_sgAbnormalEpisode.active()&&!testDue&&!testEtwStops&&!testEtwThreads,"start failure never attaches, stops or takes over another ETW session");
        check(factCount("abnormal-sleep-source-unavailable")==1,"source startup UNKNOWN is retained in ordinary evidence");
    }
    testReset();testEtwEnableError=ERROR_NOT_SUPPORTED;testArmHistoricalStaged(1,testEpoch);
    check(!g_sgAbnormalEpisode.active()&&testEtwStartCalls==1&&testEtwStops==1&&!testEtwThreads,"unsupported ID filter releases owned session without broad logging fallback");
    testReset();testEtwOpen=false;testArmHistoricalStaged(1,testEpoch);check(!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker&&testEtwStops==1,"OpenTrace failure releases only owned session");
    testReset();testEtwThread=false;testArmHistoricalStaged(1,testEpoch);check(!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker&&g_sgAbnormalEtw.consumer.load()==INVALID_PROCESSTRACE_HANDLE,"consumer thread creation failure closes trace and session");
    for(const unsigned version:{0U,1U,2U,3U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
        testXml=testEvent(161,"<Data Name=\"DurationThisPeriodMs\">30000</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">30000</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
        testEtwDeliverQueued(static_cast<BYTE>(version));testPump();check(testSleepCalls==1&&testEtwPropertyCalls==3,"161v0-v3 typed metadata passes exact current-period evidence into original consumer");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(10000);testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9000</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8000</Data>");
    check(g_sgAbnormalSuccessOrigin==testEpoch&&g_sgAbnormalSuccessEventId==522&&!g_sgAbnormalDropped&&!testDue,"522Boolean+UInt64 schema yields a health witness rather than an UNKNOWN source failure");
    check(captured.back().first=="abnormal-sleep-finished"&&captured.back().second["reason"]=="windows-effective-low-power"&&captured.back().second["healthyEventId"]==522,"ordinary stop receipt retains actual Windows health identity");
    for(const unsigned badSchema:{0U,1U,2U,3U,4U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);testEtwSchema=badSchema!=0;testEtwWrongType=badSchema==1;testEtwProperty=badSchema!=2;testEtwDuplicate=badSchema==3;
        testXml=testEvent(161,"<Data Name=\"DurationThisPeriodMs\">30000</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">30000</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
        testEtwDeliverQueued(badSchema==4?4:0);testPump();
        check(!testSleepCalls&&!testDue&&!sgAbnormalSourceReady()&&g_sgAbnormalDropped,"missing, changed, duplicate or unverified version metadata is UNKNOWN and terminal");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(10000);testDeliver(999,"<Data Name=\"DurationThisPeriodMs\">30000</Data>");
    check(g_sgAbnormalEpisode.active()&&!testEtwPropertyCalls&&!testMessages.size(),"unrequested event IDs never reach TDH decoder or UI queue");
    testReset();testArmHistoricalStaged(1,testEpoch);sgAbnormalEtwProcess(nullptr);testPump();
    check(!testDue&&!g_sgAbnormalEpisode.active()&&!g_sgAbnormalEtw.worker,"unexpected consumer completion immediately ends flow UNKNOWN");
    for(const unsigned lossKind:{0U,1U,2U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testEtwLoss=1;testEtwLossKind=lossKind;testBad(30000,30000);
        check(!testSleepCalls&&!testDue&&!g_sgAbnormalEpisode.active(),"all ETW lost-event and buffer counters veto due power requests");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testEtwStatsError=ERROR_ACCESS_DENIED;testBad(30000,30000);
    check(!testSleepCalls&&!testDue,"unreadable live loss counters are UNKNOWN, never permission");
    for(const unsigned query:{1U,2U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testEtwWakeAtQuery=query;testBad(30000,30000);
        check(!testSleepCalls&&!testDue&&!g_sgAbnormalEpisode.active(),"human wake during either final blocking source query vetoes actual sleep adapter");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testSuccessDuringEtwQuery=true;testBad(30000,30000);
    check(!testSleepCalls&&!testDue&&g_sgAbnormalSuccessOrigin==testEpoch,"Windows health arriving inside blocking source query ends phase before adapter");
    testReset();testArmHistoricalStaged(1,testEpoch);testGenerationDuringEtwQuery=true;testBad(30000,30000);
    check(!testSleepCalls&&!testDue,"generation changed during source query cannot dispatch a stale action");
    for(const unsigned stopRace:{0U,1U,2U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
        testBad(340000,10000);testBad(350000,10000);
        if(stopRace==0)testEtwWakeAtQuery=testEtwQueries+2;else if(stopRace==1)testEtwLossAtQuery=testEtwQueries+2;else testLossDuringStop=true;
        testBad(360000,10000);
        check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load(),"post-privilege manual wake, final lost counters or shutdown cleanup loss veto the only poweroff attempt");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testEtwJoin=false;sgAbnormalStop("join-timeout");
    check(g_sgAbnormalEtw.worker&&!g_sgAbnormalEtw.session&&g_sgAbnormalEtw.consumer.load()==INVALID_PROCESSTRACE_HANDLE&&g_sgAbnormalDropped,"join timeout retains owned worker context without detaching or freeing state");
    testAt(1000);testArmHistoricalStaged(1,testFileTime);check(!g_sgAbnormalEpisode.active()&&testEtwStartCalls==1,"new phase cannot reuse state while an old owned worker is unconfirmed");
    testEtwJoin=true;sgAbnormalStop("confirmed-worker-exit");check(!g_sgAbnormalEtw.worker,"owned worker is released only after join confirmation");
    testReset();testArmHistoricalStaged(1,testEpoch);testEtwStopError=ERROR_ACCESS_DENIED;sgAbnormalStop("stop-denied");
    check(g_sgAbnormalEtw.session==41&&!g_sgAbnormalEtw.worker&&g_sgAbnormalDropped,"failed StopTrace retains session ownership while consumer is closed");
    testAt(1000);testArmHistoricalStaged(1,testFileTime);check(!g_sgAbnormalEpisode.active()&&testEtwStartCalls==1,"unreleased owned session prevents starting or taking over another session");
    testEtwStopError=ERROR_SUCCESS;sgAbnormalStop("confirmed-session-release");check(!g_sgAbnormalEtw.session,"later cleanup can confirm only the previously owned session");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(1000);
    for(unsigned i=0;i<100;++i){testAt(1000+i);testDeliver(161,"<Data Name=\"DurationThisPeriodMs\">1</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">1</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");}
    check(factCount("abnormal-sleep-evidence-state")==1,"ordinary evidence aggregates event bursts with at most one state record per thirtyseconds");
    testAt(30000);for(unsigned i=0;i<100;++i)sgAbnormalTick();
    check(factCount("abnormal-sleep-deadline")==1,"deadline evaluations are bounded and deduplicated even with repeated due callbacks");
    sgAbnormalStopAsync();testPump();check(factCount("abnormal-sleep-finished")==1,"asynchronous OFF preserves one ordinary terminal receipt");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);
    const auto requestFact=std::find_if(captured.begin(),captured.end(),[](const auto& row){return row.first=="abnormal-sleep-request";});
    check(requestFact!=captured.end()&&requestFact->second["evidence"]["continuousBadMs"]==30000&&requestFact->second["evidence"]["fresh"]==true,"ordinary action receipt preserves the exact continuous interval used for dispatch");

    for(const unsigned drainPoint:{1U,2U,3U}){
        testReset();testArmHistoricalStaged(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
        testBad(340000,10000);testBad(350000,10000);testHealthyDrainAt=drainPoint;testBad(360000,10000);
        check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load(),"Windows healthy ETW delivered during terminal source drain vetoes shutdown");
        check(!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker&&g_sgAbnormalPending.load()==0,"healthy terminal drain releases owned resources and queued records");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    testPostAvailable=false;sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DripsResidencyInUs\">0</Data>"),testFileTime);testPostAvailable=true;
    sgAbnormalTick();check(!testSleepCalls&&!testPowerOffCalls&&!testDue,"completed event queue loss revokes dispatch with a required source gap");

    testReset();testArmHistoricalStaged(1,testEpoch);
    for(unsigned i=0;i<100;++i)testBad(1000+i,1);
    std::cout<<"ABNORMAL_CPU_COUNTERS "<<json{{"events",100},{"metadataCalls",testEtwInfoCalls},{"propertySizeCalls",testEtwSizeCalls},{"propertyReadCalls",testEtwPropertyCalls},{"statQueries",testEtwQueries},{"timerWrites",testTimerWrites}}.dump()<<'\n';
    testAt(30000);const auto timerWritesBefore=testTimerWrites;for(unsigned i=0;i<100;++i)testBad(30000,1);
    std::cout<<"ABNORMAL_TIMER_COUNTERS "<<json{{"dueCallbacks",100},{"timerWrites",testTimerWrites-timerWritesBefore},{"statQueries",testEtwQueries}}.dump()<<'\n';
    bool noStaleQueryRequests=true;
    for(const bool shutdown:{false,true}) {
        testReset();testArmHistoricalStaged(1,testEpoch);
        if(shutdown){testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);testBad(340000,10000);testBad(350000,10000);}
        const auto priorSleep=testSleepCalls;testEtwQueryDelayMs=11001;testEtwQueryDelayAt=testEtwQueries+1;
        testBad(shutdown?360000:30000,shutdown?10000:30000);
        const bool safe=testSleepCalls==priorSleep&&!testPowerOffCalls;noStaleQueryRequests=noStaleQueryRequests&&safe;
        std::cout<<"ABNORMAL_QUERY_DELAY_REPRO "<<json{{"shutdown",shutdown},{"delayMs",11001},{"sleepCalls",testSleepCalls},{"priorSleepCalls",priorSleep},{"powerOffCalls",testPowerOffCalls},{"elapsedMs",testTick-100000},{"safe",safe}}.dump()<<'\n';
    }
    check(noStaleQueryRequests,"all final blocking source queries recheck freshness and dispatch deadline before power actions");


    const auto prepareStage=[](unsigned stage) {
        testReset();testArmHistoricalStaged(1,testEpoch);
        if(stage>0)testBad(30000,30000);
        if(stage>1)testBad(230000,30000);
        if(stage>2){testAccepted=false;testBad(330000,30000);testBad(340000,10000);testBad(350000,10000);}
    };
    for(const unsigned stage:{0U,1U,2U,3U})for(const unsigned query:{1U,2U})for(const ULONGLONG delay:{5001ULL,11001ULL}) {
        prepareStage(stage);const auto previousCalls=testSleepCalls;testEtwQueryDelayMs=delay;testEtwQueryDelayAt=testEtwQueries+query;
        testBad(ymcc::abnormalsleep::Episode::deadlines[stage],stage==3?10000:30000);
        check(testSleepCalls==previousCalls&&!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load(),
            "every30/230/330/360stage rejects stale evidence after either blocking source QUERY");
    }
    for(const ULONGLONG delay:{5000ULL,5001ULL}) {
        prepareStage(0);testEtwQueryDelayMs=delay;testEtwQueryDelayAt=1;testBad(30000,30000);
        check(testSleepCalls==(delay==5000?1U:0U),"blocking QUERY preserves inclusive5000ms freshness without extending it");
    }
    for(const unsigned clock:{1U,2U}) {
        prepareStage(0);testEtwClockMode=clock;testEtwQueryDelayMs=6000;testEtwQueryDelayAt=1;testBad(30000,30000);
        check(!testSleepCalls&&!testDue,"wall or monotonic clock divergence during QUERY terminates dispatch");
    }
    for(const unsigned stage:{0U,1U,2U}) {
        prepareStage(stage);const auto previousCalls=testSleepCalls;testAdapterFinalQuery=true;
        testEtwQueryDelayMs=5001;testEtwQueryDelayAt=testEtwQueries+3;
        testBad(ymcc::abnormalsleep::Episode::deadlines[stage],30000);
        check(testSleepCalls==previousCalls&&!testDue,"real-adapter final QUERY hook also rejects evidence that aged after module preparation");
    }
    for(const unsigned drain:{1U,2U,3U,4U}) {
        prepareStage(3);
        if(drain==1)testStopDelayMs=5001;else if(drain==2)testCloseDelayMs=5001;else if(drain==3)testJoinDelayMs=5001;
        else runtimeFactObserver=[](const char* name,const json&){if(std::string(name)=="abnormal-sleep-finished"){testTick+=5001;testFileTime+=5001*10000;}};
        testBad(360000,10000);
        check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker,
            "shutdown rechecks original evidence after StopTrace CloseTrace join and required journal drain");
        const auto powerResult=std::find_if(captured.begin(),captured.end(),[](const auto& row){return row.first=="abnormal-sleep-poweroff-result";});
        check(powerResult!=captured.end()&&powerResult->second["error"]==ERROR_CANCELLED&&powerResult->second["finalReason"]=="unknown-or-stale",
            "terminal stale drain keeps an ordinary explicit final-reason receipt");
    }
    prepareStage(3);testPrivilegeDelayMs=5001;testBad(360000,10000);
    check(!testPowerOffCalls&&!testDue,"privilege delay cannot preserve an old shutdown permission");
    prepareStage(0);runtimeFactObserver=[](const char* name,const json&){if(std::string(name)=="abnormal-sleep-request"){testTick+=5001;testFileTime+=5001*10000;}};
    testBad(30000,30000);check(!testSleepCalls&&!testDue,"required request journal delay still needs fresh evidence before the power adapter");
    prepareStage(0);testUnknownDuringEtwQuery=true;testBad(30000,30000);
    check(!testSleepCalls&&!testDue&&!testMessages.size(),"new UNKNOWN pending during QUERY vetoes old Bad before UI consumption");
    for(const unsigned stage:{0U,1U,2U,3U}) {
        ymcc::abnormalsleep::Episode policy;
        check(policy.arm(testEpoch,100000,testEpoch,771),"dispatch boundary policy arms original phase");policy.hibernateRejected=true;
        const auto deadline=ymcc::abnormalsleep::Episode::deadlines[stage];const auto end=deadline+10000;
        policy.badBegin=testEpoch+(end-30000)*10000;policy.badEnd=testEpoch+end*10000;
        check(policy.dispatch(stage,100000+end,policy.badEnd).action!=ymcc::abnormalsleep::Action::None,"each dispatch window includes its exact10000ms boundary");
        policy.badBegin+=10000;policy.badEnd+=10000;
        check(policy.dispatch(stage,100000+end+1,policy.badEnd).action==ymcc::abnormalsleep::Action::None,
            "fresh new data cannot extend any30/230/330/360dispatch window by one millisecond");
    }
    testReset();testArmHistoricalStaged(1,testEpoch);
    for(unsigned i=0;i<100;++i)testBad(1000+i,1);
    check(testEtwInfoCalls==2&&testEtwSizeCalls==300&&testEtwPropertyCalls==300,
        "100same-schema events verify metadata once but retain all300current size and value reads");
    testEtwProperty=false;testBad(1200,1);
    check(g_sgAbnormalDropped&&!testDue,"cached metadata never bypasses unreadable current event payload");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(1000,1);testEtwPropertySizeOverride=8;testBad(1100,1);
    check(g_sgAbnormalDropped&&!testDue&&testEtwInfoCalls==2,"cached metadata still rejects a changed current field size");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(1000,1);testEtwRecordTask=1;testBad(1100,1);testEtwRecordFlags=EVENT_HEADER_FLAG_32_BIT_HEADER;testBad(1200,1);
    check(testEtwInfoCalls==6&&!g_sgAbnormalDropped,"full descriptor or header flag changes force fresh TDH verification");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(1000,1);testEtwRecordTask=1;testEtwSchema=false;testBad(1100,1);
    check(g_sgAbnormalDropped&&!testDue,"changed descriptor with unavailable schema is UNKNOWN even after another descriptor succeeded");
    testReset();testArmHistoricalStaged(1,testEpoch);testBad(1000,1);sgAbnormalStop("new-schema-episode");testAt(2000);testArmHistoricalStaged(1,testFileTime);testBad(3000,1);
    check(testEtwInfoCalls==4&&testEtwStartCalls==2,"new owned episode revalidates metadata instead of inheriting cached schema");
    testReset();testArmHistoricalStaged(1,testEpoch);
    for(const BYTE version:std::array<BYTE,4>{0,1,2,3}){
        testAt(1000);testXml=testEvent(161,"<Data Name=\"DurationThisPeriodMs\">1</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">1</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");testEtwDeliverQueued(version);testPump();testEtwDeliverQueued(version);testPump();
    }
    check(testEtwInfoCalls==8&&testEtwSizeCalls==24&&g_sgAbnormalEtw.schemas.size()==5,"four supported161versions have bounded independent schema identities");
    testReset();testArmHistoricalStaged(1,testEpoch);testAt(1000);
    for(unsigned i=0;i<100;++i)testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">0</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">0</Data>");
    check(testEtwInfoCalls==2&&testEtwPropertyCalls==300&&g_sgAbnormalEpisode.active(),"522schema cache never turns zero or cumulative observations into health");
    testAt(2000);testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8</Data>");
    check(!testDue&&g_sgAbnormalSuccessEventId==522,"cached522schema consumes new healthy values and immediately ends the phase");
    prepareStage(3);testAt(355000);testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">0</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">0</Data>");
    testHealthyDrainAt=1;testBad(360000,30000);
    check(!testPowerOffCalls&&!testDue&&g_sgAbnormalSuccessEventId==522,"already cached522schema remains active during terminal healthy drain veto");
    testReset();testArmHistoricalStaged(1,testEpoch);const auto writesAtArm=testTimerWrites;
    for(unsigned i=0;i<100;++i)testBad(1000+i,1);
    check(testTimerWrites==writesAtArm&&testEtwQueries==0,"predue evidence events neither rearm timer nor query source statistics");
    testAt(30000);for(unsigned i=0;i<100;++i)sgAbnormalTick(true);
    check(testTimerWrites==writesAtArm+1,"100waiting deadline callbacks share one existing absolute timer target");
    while(testDue){const auto nextDue=testDue;testAt(nextDue-100000);sgAbnormalTick();}
    check(testTimerWrites==8&&!g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker,
        "no-evidence episode uses only eight original deadline and window timers then releases its source");


    testReset();testArmHistoricalStaged(1,testEpoch);testAt(1000);const auto liveTarget=testDue;
    testKillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);sgAbnormalTick();
    check(testDue==liveTarget&&testDue!=0,"old or early WM_TIMER reinstalls the current target after main killed the actual timer");
    testAt(30000);testKillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);sgAbnormalTick();
    check(testDue==140001,"actual deadline timer advances to its bounded evidence window after main one-shot delivery");
    const auto newWindow=testDue;testAt(30001);testKillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);sgAbnormalTick();
    check(testDue==newWindow,"stale WM_TIMER cannot erase a newly installed future dispatch-window timer");

    testReset();return sleepEvidenceChecks-initialChecks;
}

// REAL PRODUCTION SCOPE. No historical StartPhase path is reachable from the
// two production hooks. Retain policy facts but create no observation resources.
static unsigned abnormalConservativeProductionTests() {
    const auto initialChecks=sleepEvidenceChecks;
    const auto assertIdle=[](const char* label) {
        check(!g_sgAbnormalEpisode.active()&&!g_sgAbnormalOrigin.load()&&
            !g_sgAbnormalEtw.session&&!g_sgAbnormalEtw.worker&&
            g_sgAbnormalEtw.consumer.load()==INVALID_PROCESSTRACE_HANDLE&&
            !testDue&&!testTimerWrites&&!testEtwStartCalls&&!testEtwThreads&&!testEtwQueries&&
            !testEtwInfoCalls&&!testEtwSizeCalls&&!testEtwPropertyCalls&&
            !testSleepCalls&&!testPowerOffCalls&&testMessages.empty(),label);
    };
    const auto factCount=[](){return static_cast<unsigned>(std::count_if(captured.begin(),captured.end(),
        [](const auto& row){return row.first=="abnormal-sleep-conservative-policy";}));};
    const auto assertPolicy=[](unsigned expected,const char* trigger) {
        const auto policies=static_cast<unsigned>(std::count_if(captured.begin(),captured.end(),
            [](const auto& row){return row.first=="abnormal-sleep-conservative-policy";}));
        check(policies==expected,"production policy fact is bounded to once per original human intent");
        if(!expected)return;
        const auto row=std::find_if(captured.rbegin(),captured.rend(),
            [](const auto& value){return value.first=="abnormal-sleep-conservative-policy";});
        const auto& fact=row->second;
        check(fact.at("policy")=="conservative-windows-events"&&fact.at("drips")=="drips-observation-only"&&
            fact.at("highPower")=="highPower-deferred"&&fact.at("automaticHighPowerRepair")==false&&
            fact.at("mandatoryEvidence")==true&&fact.at("detailLogsRequired")==false&&fact.at("trigger")==trigger,
            "ordinary production receipt explicitly states the conservative observation-only deferred policy");
    };
    // Known Bad does not grant the deferred high-power controller a production entry.
    for(const bool badEvidence:{false,true}) {
        testReset();sgAbnormalArm(1,testEpoch);assertPolicy(1,"power-button-506");
        assertIdle("real production Arm is receipt-only with zero ETW worker timer or power start");
        const auto latestMarker=g_sgInternalSleepRequest;
        for(const ULONGLONG at:{30000ULL,40001ULL,230000ULL,240001ULL,330000ULL,340001ULL,360000ULL,370001ULL}) {
            testAt(at);
            if(badEvidence) {
                testBad(at,30000);
                const auto event=ymcc::abnormalsleep::Observation{ymcc::abnormalsleep::Quality::Bad,testFileTime-30000*10000ULL,testFileTime,161};
                g_sgAbnormalEpisode.observe(event,testFileTime);
            }
            sgAbnormalInternalEntry();sgAbnormalSuspendBoundary();sgAbnormalTick();
            assertIdle("no DRIPS or even live Bad at30/230/330/360cannot start a deferred production repair");
        }
        check(g_sgAutomaticSleepBudget.requests()==0&&g_sgInternalSleepRequest.kind==latestMarker.kind&&
            g_sgInternalSleepRequest.dispatchFileTime==latestMarker.dispatchFileTime,
            "production Arm and deferred deadlines never consume or reset shared budget or legacy markers");
        assertPolicy(1,"power-button-506");
        check(!sgAbnormalSourceReady()&&!sgAbnormalDispatchReady(),"real production idle readiness never advertises a staged source or permit");
    }
    // Owned FALSE is informational: leave the parent's existing transaction intact.
    for(const unsigned alreadyConsumed:{1U,2U,3U})for(const bool accepted:{false,true}) {
        testReset();for(unsigned i=0;i<alreadyConsumed;++i)check(g_sgAutomaticSleepBudget.consume(),"parent independently spends its original shared request budget");
        testAt(2038);sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);const auto marker=g_sgInternalSleepRequest;
        testRetryBusy=true;const auto originalPhase=g_powerLifecycle.load();
        check(!sgAbnormalObserveOwnedRetry(testEpoch,testFileTime),"real production Owned hook always returns false without vetoing or replacing the legacy transaction");
        assertPolicy(1,"legacy-owned-retry");
        sgAbnormalOwnedRetryResult(testEpoch,marker.dispatchFileTime,accepted);
        check(g_sgAutomaticSleepBudget.requests()==alreadyConsumed&&g_sgInternalSleepRequest.kind==marker.kind&&
            g_sgInternalSleepRequest.dispatchTick==marker.dispatchTick&&g_sgInternalSleepRequest.dispatchFileTime==marker.dispatchFileTime&&
            testRetryBusy&&g_powerLifecycle.load()==originalPhase,
            "production false and accepted or rejected result preserve budget marker and pending legacy ownership");
        assertIdle("real Owned hook and result never start sources timers or substitute power APIs");
        for(unsigned duplicate=0;duplicate<100;++duplicate)
            check(!sgAbnormalObserveOwnedRetry(testEpoch,marker.dispatchFileTime),"duplicate production Owned remains false with no hidden phase");
        testAt(3038);sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
        check(!sgAbnormalObserveOwnedRetry(testEpoch,testFileTime),"a new legacy retry in the same human intent does not enable a staged phase");
        assertPolicy(1,"legacy-owned-retry");
        for(const ULONGLONG at:{32038ULL,232038ULL,332038ULL,362038ULL}) {
            testBad(at,30000);sgAbnormalTick();sgAbnormalOwnedRetryResult(testEpoch,marker.dispatchFileTime,accepted);
            assertIdle("real Owned with Bad evidence at every historical stage makes zero added power calls");
        }
        check(g_sgAutomaticSleepBudget.requests()==alreadyConsumed,"owned observations never refund or replenish the parent shared-three budget");
    }
    testReset();sgAbnormalArm(1,testEpoch);
    for(unsigned i=0;i<100;++i)sgAbnormalArm(1,testEpoch);
    testAt(2038);g_sgAutomaticSleepBudget.consume();sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
    check(!sgAbnormalObserveOwnedRetry(testEpoch,testFileTime),"Arm then Owned of one intent stays observation-only");
    assertPolicy(1,"power-button-506");
    testAt(3000);g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgAbnormalArm(1,testEpoch);
    check(!sgAbnormalObserveOwnedRetry(testEpoch,testEpoch+2038*10000ULL),"confirmed human wake vetoes a delayed production Owned notice");
    assertPolicy(1,"power-button-506");
    testAt(4000);sgAbnormalArm(1,testFileTime);assertPolicy(2,"power-button-506");
    testAt(5000);sgAbnormalArm(1,testEpoch+1000*10000ULL);assertPolicy(2,"power-button-506");
    check(g_sgAbnormalLastHumanIntent==testEpoch+4000*10000ULL,"out-of-order old production notifications cannot overwrite the current human watermark");
    assertIdle("manual wake and later manual sleep only add one current policy fact with zero resource creation");
    // Non-manual, stale, impossible and already-woken notices have no fact flood.
    for(const unsigned invalid:{0U,1U,2U,3U,4U,5U,6U,7U}) {
        testReset();
        if(invalid==1)testAt(10001);
        if(invalid==2)g_sgLastPowerButtonWakeEventFileTime=testEpoch;
        if(invalid==3)g_guardEnabled=false;
        if(invalid==4)g_sgRetryEntryFailure=false;
        if(invalid==5)g_exitRequested=true;
        const auto origin=invalid==6?testEpoch+10000:invalid==7?0:testEpoch;
        for(unsigned i=0;i<100;++i)sgAbnormalArm(invalid==0?3:1,origin);
        assertPolicy(0,"ignored");assertIdle("non-manual stale OFF future invalid and already-woken production notices neither log spam nor start resources");
        check(!g_sgAbnormalLastHumanIntent,"rejected production notices do not acquire the original human watermark");
    }
    testReset();testAt(10000);sgAbnormalArm(1,testEpoch);assertPolicy(1,"power-button-506");
    testReset();testAt(10001);sgAbnormalArm(1,testEpoch);testAt(10002);sgAbnormalArm(1,testFileTime);
    assertPolicy(1,"power-button-506");
    check(g_sgAbnormalLastHumanIntent==testFileTime,"stale notice cannot suppress a subsequent real current human intent");
    for(const unsigned invalid:{0U,1U,2U,3U,4U,5U,6U,7U}) {
        testReset();testAt(2038);g_sgAutomaticSleepBudget.consume();sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
        const auto request=g_sgInternalSleepRequest.dispatchFileTime;
        if(invalid==0)g_sgAutomaticSleepBudget.confirmedUserWake();
        if(invalid==1)g_sgInternalSleepRequest.kind=SgRetryKind::EntryFailure;
        if(invalid==2)++g_sgInternalSleepRequest.dispatchFileTime;
        if(invalid==3)g_sgResleepEnabled=false;
        if(invalid==4)g_sgLastPowerButtonWakeEventFileTime=testEpoch+1000*10000ULL;
        if(invalid==5)testAt(12039);
        const auto origin=invalid==6?request+1:invalid==7?0:testEpoch;
        const auto budget=g_sgAutomaticSleepBudget.requests();const auto requestKind=g_sgInternalSleepRequest.kind;
        for(unsigned i=0;i<100;++i)check(!sgAbnormalObserveOwnedRetry(origin,request),"invalid production owned notification always remains false");
        assertPolicy(0,"ignored");assertIdle("invalid owned notice has no policy spam or deferred engine resource creation");
        check(g_sgAutomaticSleepBudget.requests()==budget&&g_sgInternalSleepRequest.kind==requestKind&&!g_sgAbnormalLastOwnedRequest,
            "invalid owned notice cannot alter legacy budget marker or owned watermark");
    }
    testReset();testChannel=false;sgAbnormalArm(1,testEpoch);assertPolicy(1,"power-button-506");
    assertIdle("production policy receipt does not depend on live ETW availability");
    testAt(1000);sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">35210323</Data>"),testFileTime);testPump();
    assertIdle("completed507remains information and cannot establish a production staged engine");
    check(factCount()==1,"completed507does not duplicate the intent policy receipt");
    testReset();return sleepEvidenceChecks-initialChecks;
}
