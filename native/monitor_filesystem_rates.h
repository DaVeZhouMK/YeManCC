#pragma once
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winperf.h>
#include <cmath>
#pragma comment(lib,"pdh.lib")
namespace ymcc::monitorfs {
struct Rates { double readMBps=-1,writeMBps=-1;long long ts=0; };
inline bool rawOk(const PDH_RAW_COUNTER& r) {return (r.CStatus==PDH_CSTATUS_VALID_DATA||r.CStatus==PDH_CSTATUS_NEW_DATA)&&r.FirstValue>=0&&r.SecondValue>0;}
inline double bulkRate(const PDH_RAW_COUNTER& before,const PDH_RAW_COUNTER& after,DWORD type,long long frequency) {
    if(type!=PERF_COUNTER_BULK_COUNT||frequency<=0||!rawOk(before)||!rawOk(after)||after.FirstValue<before.FirstValue||after.SecondValue<=before.SecondValue)return -1;
    const auto dt=after.SecondValue-before.SecondValue;
    if(static_cast<long double>(dt)/frequency>6)return -1;
    const long double rate=static_cast<long double>(after.FirstValue-before.FirstValue)*frequency/dt/1e6;
    return std::isfinite(rate)&&rate>=0&&rate<=1e6?static_cast<double>(rate):-1;
}
// Owned exclusively by nativeMonitorLoop's existing 1Hz top-demand lane.
// Query/counters reused; no WMI, thread, worker, process scan or display-side IO.
class Reader {
    PDH_HQUERY query_=nullptr;PDH_HCOUNTER read_=nullptr,write_=nullptr;
    PDH_RAW_COUNTER priorRead_{},priorWrite_{};bool baseline_=false;
    ULONGLONG retryAt_=0,lastTick_=0;long long readFrequency_=0,writeFrequency_=0;
    void close(){if(query_)PdhCloseQuery(query_);query_=nullptr;read_=write_=nullptr;baseline_=false;lastTick_=0;readFrequency_=writeFrequency_=0;}
    bool open(){
        if(PdhOpenQueryW(nullptr,0,&query_)!=ERROR_SUCCESS)return false;
        if(PdhAddEnglishCounterW(query_,L"\\FileSystem Disk Activity(_Total)\\FileSystem Bytes Read",0,&read_)!=ERROR_SUCCESS||
           PdhAddEnglishCounterW(query_,L"\\FileSystem Disk Activity(_Total)\\FileSystem Bytes Written",0,&write_)!=ERROR_SUCCESS){close();return false;}
        return true;
    }
public:
    ~Reader(){close();}Reader()=default;Reader(const Reader&)=delete;Reader&operator=(const Reader&)=delete;
    void reset(){close();retryAt_=0;}
    Rates sample(ULONGLONG tick,long long epoch){
        Rates result;if(!query_){if(tick<retryAt_)return result;if(!open()){retryAt_=tick+10000;return result;}}
        if(PdhCollectQueryData(query_)!=ERROR_SUCCESS){close();retryAt_=tick+10000;return result;}
        // Dynamic providers expose a zero time base until first collection.
        // Initialize once AFTER collect, not in open; no extra sample/read loop.
        if(readFrequency_<=0||writeFrequency_<=0){LONGLONG rf=0,wf=0;
            if(PdhGetCounterTimeBase(read_,&rf)!=ERROR_SUCCESS||PdhGetCounterTimeBase(write_,&wf)!=ERROR_SUCCESS||rf<=0||wf<=0){close();retryAt_=tick+10000;return result;}
            readFrequency_=rf;writeFrequency_=wf;
        }
        PDH_RAW_COUNTER read{},write{};DWORD readType=0,writeType=0;
        const bool r=PdhGetRawCounterValue(read_,&readType,&read)==ERROR_SUCCESS&&rawOk(read);
        const bool w=PdhGetRawCounterValue(write_,&writeType,&write)==ERROR_SUCCESS&&rawOk(write);
        if(baseline_&&lastTick_>0&&tick>lastTick_&&tick-lastTick_<6000){
            if(r)result.readMBps=bulkRate(priorRead_,read,readType,readFrequency_);
            if(w)result.writeMBps=bulkRate(priorWrite_,write,writeType,writeFrequency_);
        }
        priorRead_=r?read:PDH_RAW_COUNTER{};priorWrite_=w?write:PDH_RAW_COUNTER{};baseline_=true;lastTick_=tick;
        if(result.readMBps>=0||result.writeMBps>=0)result.ts=epoch;
        return result;
    }
};
}
