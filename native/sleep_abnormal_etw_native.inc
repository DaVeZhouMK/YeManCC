// Episode-owned real-time Kernel-Power source. No ETL, global channel writes,
// provider takeover, detached consumer, or background activity between episodes.
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <array>
#include <cstring>
#include <cwchar>
#include <vector>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"tdh.lib")
static constexpr GUID SG_ABNORMAL_KERNEL_PROVIDER{0x331c3b3a,0x2005,0x44c2,{0xac,0x5e,0x77,0x22,0x0c,0x37,0xd6,0xb4}};
struct SgAbnormalEtwState {
    struct Properties {EVENT_TRACE_PROPERTIES value{};WCHAR name[128]{};} properties;
    struct Schema {EVENT_DESCRIPTOR descriptor{};USHORT headerFlags=0;bool verified=false;};
    std::array<Schema,5> schemas{}; //161v0-v3 and522v0, only this episode.
    TRACEHANDLE session=0;
    std::atomic<TRACEHANDLE> consumer{INVALID_PROCESSTRACE_HANDLE};
    HANDLE worker=nullptr;
    ULONGLONG origin=0;
    std::atomic<bool> stopping{false},ready{false};
};
static SgAbnormalEtwState g_sgAbnormalEtw;
static void sgAbnormalEtwPublish(unsigned,const std::string&,ULONGLONG,ULONGLONG);
static void sgAbnormalEtwGap(const char* reason,DWORD error) {
    g_sgAbnormalDropped.store(true,std::memory_order_release);
    // REQUIRED low-frequency failure evidence, once per source. The UI stop
    // receipt retains the gap; no callback event/raw payload journal spam.
    if(g_sgAbnormalEtw.ready.exchange(false,std::memory_order_acq_rel)) {
        sgRecordFact("abnormal-sleep-live-source-gap",{{"source","episode-etw"},{"reason",reason},{"error",error},{"assessment","UNKNOWN"},{"mandatoryEvidence",true}});
        if(g_hwnd&&!g_sgAbnormalPending.load(std::memory_order_acquire))PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,0);
    }
}
static bool sgAbnormalEtwSchemaProperty(const std::vector<BYTE>& storage,const wchar_t* name,USHORT type) {
    if(storage.size()<sizeof(TRACE_EVENT_INFO))return false;
    const auto info=reinterpret_cast<const TRACE_EVENT_INFO*>(storage.data());
    const auto count=info->TopLevelPropertyCount;
    if(count>64||offsetof(TRACE_EVENT_INFO,EventPropertyInfoArray)+count*sizeof(EVENT_PROPERTY_INFO)>storage.size())return false;
    unsigned matches=0;
    for(ULONG i=0;i<count;++i) {
        const auto& property=info->EventPropertyInfoArray[i];
        const auto offset=property.NameOffset;
        if(offset%sizeof(wchar_t)||offset>=storage.size()||storage.size()-offset<sizeof(wchar_t))return false;
        const auto text=reinterpret_cast<const wchar_t*>(storage.data()+offset);
        const auto available=(storage.size()-offset)/sizeof(wchar_t);
        if(std::find(text,text+available,L'\0')==text+available)return false;
        if(std::wcscmp(text,name))continue;
        if(++matches!=1||property.Flags!=0||property.nonStructType.InType!=type||property.count!=1)return false;
    }
    return matches==1;
}
static bool sgAbnormalEtwProperty(EVENT_RECORD* event,const wchar_t* name,USHORT type,ULONGLONG& value) {
    // Required for EVERY event, including cached schemas: no field-size or
    // payload-value caching, and never reuse health or action evidence.
    PROPERTY_DATA_DESCRIPTOR descriptor{};descriptor.PropertyName=reinterpret_cast<ULONGLONG>(name);descriptor.ArrayIndex=ULONG_MAX;
    ULONG size=0;
    const ULONG expected=type==TDH_INTYPE_UINT64?8UL:4UL;
    if(TdhGetPropertySize(event,0,nullptr,1,&descriptor,&size)!=ERROR_SUCCESS||size!=expected)return false;
    value=0;
    return TdhGetProperty(event,0,nullptr,1,&descriptor,size,reinterpret_cast<PBYTE>(&value))==ERROR_SUCCESS;
}
static VOID WINAPI sgAbnormalEtwEvent(EVENT_RECORD* event) {
    auto& source=g_sgAbnormalEtw;
    if(!event||event->UserContext!=&source||
        source.origin!=g_sgAbnormalOrigin.load(std::memory_order_acquire)||
        !IsEqualGUID(event->EventHeader.ProviderId,SG_ABNORMAL_KERNEL_PROVIDER))return;
    const auto id=event->EventHeader.EventDescriptor.Id;
    if(id!=161&&id!=522&&id!=554)return;
    if(id==554)return; // action accounting is neither health nor permission.
    if((id==161&&event->EventHeader.EventDescriptor.Version>3)||(id==522&&event->EventHeader.EventDescriptor.Version!=0)) {sgAbnormalEtwGap("schema-version-unverified",ERROR_REVISION_MISMATCH);return;}
    if(event->EventHeader.TimeStamp.QuadPart<=0){sgAbnormalEtwGap("invalid-event-time",ERROR_INVALID_DATA);return;}
    try {
        auto& schema=source.schemas[id==161?event->EventHeader.EventDescriptor.Version:4];
        const bool cached=schema.verified&&schema.headerFlags==event->EventHeader.Flags&&
            std::memcmp(&schema.descriptor,&event->EventHeader.EventDescriptor,sizeof(EVENT_DESCRIPTOR))==0;
        std::vector<BYTE> storage;
        if(!cached) {
            schema.verified=false;
            ULONG size=0;
            if(TdhGetEventInformation(event,0,nullptr,nullptr,&size)!=ERROR_INSUFFICIENT_BUFFER||size<sizeof(TRACE_EVENT_INFO)||size>65536) {
                sgAbnormalEtwGap("schema-unavailable",ERROR_INVALID_DATA);return;
            }
            storage.resize(size);
            if(TdhGetEventInformation(event,0,nullptr,reinterpret_cast<PTRACE_EVENT_INFO>(storage.data()),&size)!=ERROR_SUCCESS||size>storage.size()||size<sizeof(TRACE_EVENT_INFO)) {
                sgAbnormalEtwGap("schema-unreadable",ERROR_INVALID_DATA);return;
            }
            const auto metadata=reinterpret_cast<const TRACE_EVENT_INFO*>(storage.data());
            if(metadata->DecodingSource!=DecodingSourceXMLFile||!IsEqualGUID(metadata->ProviderGuid,SG_ABNORMAL_KERNEL_PROVIDER)||
                std::memcmp(&metadata->EventDescriptor,&event->EventHeader.EventDescriptor,sizeof(EVENT_DESCRIPTOR))!=0) {
                sgAbnormalEtwGap("schema-event-mismatch",ERROR_INVALID_DATA);return;
            }
        }
        const wchar_t* keys161[]={L"DurationThisPeriodMs",L"ResiliencyPhaseNonActivatedNoDripsMs",L"NonActivatedCpuTimeMs"};
        const wchar_t* keys522[]={L"HwDripsTotalTimeValid",L"DripsTotalTimeThisPeriodUs",L"HwDripsTotalTimeThisPeriodUs"};
        const char* names161[]={"DurationThisPeriodMs","ResiliencyPhaseNonActivatedNoDripsMs","NonActivatedCpuTimeMs"};
        const char* names522[]={"HwDripsTotalTimeValid","DripsTotalTimeThisPeriodUs","HwDripsTotalTimeThisPeriodUs"};
        std::string xml="<Event><EventData>";
        for(unsigned i=0;i<3;++i) {
            ULONGLONG value=0;const auto type=static_cast<USHORT>(id==161?TDH_INTYPE_UINT32:i==0?TDH_INTYPE_BOOLEAN:TDH_INTYPE_UINT64);
            if((!cached&&!sgAbnormalEtwSchemaProperty(storage,id==161?keys161[i]:keys522[i],type))||
                !sgAbnormalEtwProperty(event,id==161?keys161[i]:keys522[i],type,value)||(id==522&&i==0&&value>1)) {
                sgAbnormalEtwGap("schema-property-mismatch",ERROR_INVALID_DATA);return;
            }
            xml+="<Data Name=\"";xml+=id==161?names161[i]:names522[i];xml+="\">";xml+=std::to_string(value);xml+="</Data>";
        }
        if(!cached) {
            // Cache only a verified manifest schema identity, never offsets,
            // payloads, loss counters or dispatch permission. Descriptor/flags
            // changes force a fresh TDH verification; new episode clears all5.
            schema.descriptor=event->EventHeader.EventDescriptor;schema.headerFlags=event->EventHeader.Flags;schema.verified=true;
        }
        xml+="</EventData></Event>";
        sgAbnormalEtwPublish(id,xml,static_cast<ULONGLONG>(event->EventHeader.TimeStamp.QuadPart),source.origin);
    } catch(...) {sgAbnormalEtwGap("event-decode-failed",ERROR_NOT_ENOUGH_MEMORY);}
}
static ULONG WINAPI sgAbnormalEtwBuffer(EVENT_TRACE_LOGFILEW* logfile) {
    if(!logfile||logfile->Context!=&g_sgAbnormalEtw)return FALSE;
    if(logfile->EventsLost)sgAbnormalEtwGap("events-lost",ERROR_EVT_QUERY_RESULT_STALE);
    // CloseTrace/STOP drains queued buffers; do not abort the final health witness.
    return TRUE;
}
static DWORD WINAPI sgAbnormalEtwProcess(void*) {
    TRACEHANDLE trace=g_sgAbnormalEtw.consumer.load(std::memory_order_acquire);
    const auto error=ProcessTrace(&trace,1,nullptr,nullptr);
    if(!g_sgAbnormalEtw.stopping.load(std::memory_order_acquire))sgAbnormalEtwGap("consumer-ended",error);
    return error;
}
static bool sgAbnormalEtwStop() {
    auto& source=g_sgAbnormalEtw;
    if(!source.session&&source.consumer.load()==INVALID_PROCESSTRACE_HANDLE&&!source.worker)return true;
    source.stopping.store(true,std::memory_order_release);source.ready.store(false,std::memory_order_release);
    bool ok=true;DWORD error=ERROR_SUCCESS;
    if(source.session) {
        const auto status=ControlTraceW(source.session,nullptr,&source.properties.value,EVENT_TRACE_CONTROL_STOP);
        if(status==ERROR_SUCCESS||status==ERROR_WMI_INSTANCE_NOT_FOUND)source.session=0;
        else {ok=false;error=status;}
        if(source.properties.value.EventsLost||source.properties.value.LogBuffersLost||source.properties.value.RealTimeBuffersLost)
            g_sgAbnormalDropped.store(true,std::memory_order_release);
    }
    const auto trace=source.consumer.load(std::memory_order_acquire);
    if(trace!=INVALID_PROCESSTRACE_HANDLE) {
        const auto status=CloseTrace(trace);
        if(status==ERROR_SUCCESS||status==ERROR_CTX_CLOSE_PENDING||status==ERROR_INVALID_HANDLE)
            source.consumer.store(INVALID_PROCESSTRACE_HANDLE,std::memory_order_release);
        else {ok=false;error=status;}
    }
    if(source.worker) {
        if(WaitForSingleObject(source.worker,1500)==WAIT_OBJECT_0&&CloseHandle(source.worker))source.worker=nullptr;
        else {ok=false;error=ERROR_TIMEOUT;}
    }
    if(!ok)g_sgAbnormalDropped.store(true,std::memory_order_release);
    sgRecordFact("abnormal-sleep-live-source-stop",{{"source","episode-etw"},{"closed",ok},{"error",error},
        {"eventsLost",source.properties.value.EventsLost},{"logBuffersLost",source.properties.value.LogBuffersLost},{"realTimeBuffersLost",source.properties.value.RealTimeBuffersLost},
        {"assessment",ok&&!g_sgAbnormalDropped.load()?"closed":"UNKNOWN"},{"mandatoryEvidence",true}});
    return ok;
}
static bool sgAbnormalEtwStart(ULONGLONG origin) {
    auto& source=g_sgAbnormalEtw;
    if(source.session||source.worker||source.consumer.load()!=INVALID_PROCESSTRACE_HANDLE)return false;
    source.properties={};source.schemas={};source.origin=origin;source.stopping.store(false);source.ready.store(false);
    const auto name=L"YMCC-AbnormalSleep-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(origin);
    if(name.size()>=std::size(source.properties.name))return false;
    std::copy(name.begin(),name.end(),source.properties.name);
    auto& properties=source.properties.value;
    properties.Wnode.BufferSize=sizeof(source.properties);properties.Wnode.Flags=WNODE_FLAG_TRACED_GUID;
    properties.Wnode.ClientContext=2; // System FILETIME; ProcessTrace does not use RAW_TIMESTAMP.
    properties.BufferSize=64;properties.MinimumBuffers=2;properties.MaximumBuffers=4;properties.FlushTimer=1;
    properties.LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_INDEPENDENT_SESSION_MODE|EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
    properties.LoggerNameOffset=offsetof(SgAbnormalEtwState::Properties,name);properties.LogFileNameOffset=0;
    TRACEHANDLE session=0;DWORD error=StartTraceW(&session,source.properties.name,&properties);
    if(error==ERROR_SUCCESS) {
        source.session=session;
        struct IdFilter {BOOLEAN include=TRUE;UCHAR reserved=0;USHORT count=3;USHORT ids[3]={161,522,554};} ids;
        EVENT_FILTER_DESCRIPTOR filter{};filter.Ptr=reinterpret_cast<ULONGLONG>(&ids);filter.Size=sizeof(ids);filter.Type=EVENT_FILTER_TYPE_EVENT_ID;
        ENABLE_TRACE_PARAMETERS parameters{};parameters.Version=ENABLE_TRACE_PARAMETERS_VERSION_2;parameters.EnableFilterDesc=&filter;parameters.FilterDescCount=1;
        error=EnableTraceEx2(source.session,&SG_ABNORMAL_KERNEL_PROVIDER,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_INFORMATION,0x404,0,0,&parameters);
        if(error==ERROR_SUCCESS) {
            EVENT_TRACE_LOGFILEW logfile{};logfile.LoggerName=source.properties.name;
            logfile.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;
            logfile.EventRecordCallback=sgAbnormalEtwEvent;logfile.BufferCallback=sgAbnormalEtwBuffer;logfile.Context=&source;
            const auto trace=OpenTraceW(&logfile);source.consumer.store(trace,std::memory_order_release);
            if(trace==INVALID_PROCESSTRACE_HANDLE)error=GetLastError();
            else {
                source.ready.store(true,std::memory_order_release);
                source.worker=CreateThread(nullptr,0,sgAbnormalEtwProcess,nullptr,0,nullptr);
                if(!source.worker){error=GetLastError();source.ready.store(false);}
            }
        }
    }
    if(error!=ERROR_SUCCESS) {
        sgRecordFact("abnormal-sleep-live-source-start",{{"source","episode-etw"},{"ready",false},{"error",error},{"assessment","UNKNOWN"},{"mandatoryEvidence",true}});
        sgAbnormalEtwStop();return false;
    }
    sgRecordFact("abnormal-sleep-live-source-start",{{"source","episode-etw"},{"ready",true},{"eventIds",{161,522,554}},
        {"episode",sgFormatFactFileTime(origin)},{"etl",false},{"persistent",false},{"mandatoryEvidence",true}});
    return source.ready.load(std::memory_order_acquire)&&!g_sgAbnormalDropped.load(std::memory_order_acquire);
}
static bool sgAbnormalEtwHealthy() {
    auto& source=g_sgAbnormalEtw;
    if(!source.session)return false;
    if(!source.ready.load(std::memory_order_acquire))return false;
    const auto status=ControlTraceW(source.session,nullptr,&source.properties.value,EVENT_TRACE_CONTROL_QUERY);
    if(status!=ERROR_SUCCESS||source.properties.value.EventsLost||source.properties.value.LogBuffersLost||source.properties.value.RealTimeBuffersLost) {
        sgAbnormalEtwGap("source-stats-unavailable-or-lost",status);return false;
    }
    return true;
}
