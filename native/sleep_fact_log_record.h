#pragma once
// Record-creation metadata only. No device probes, listeners, timers or sleep decisions.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include "json.hpp"

namespace ymcc::sleepfactlog {
using Json=nlohmann::json;
inline std::uint64_t fileTimeValue(const FILETIME& value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime)<<32)|value.dwLowDateTime;
}
struct WriterIdentity {
    DWORD pid=0;
    std::uint64_t creation100ns=0;
};
inline const WriterIdentity& writerIdentity() noexcept {
    // Exactly one cached GetProcessTimes for this process; never retry on a control path.
    static const WriterIdentity identity=[]() noexcept {
        WriterIdentity result;result.pid=GetCurrentProcessId();
        FILETIME created{},exited{},kernel{},user{};
        if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
            result.creation100ns=fileTimeValue(created);
        return result;
    }();
    return identity;
}
inline std::string isoTime(const SYSTEMTIME& time,const std::string& suffix) {
    char text[40]{};
    std::snprintf(text,sizeof(text),"%04u-%02u-%02uT%02u:%02u:%02u.%03u",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return std::string(text)+suffix;
}
inline void decorateRecord(Json& record,std::uint64_t powerGeneration) {
    // Journal summaries can contain inherited pending/error entries. Preserve
    // the original details exactly and label their evidence limits outside them.
    if(record.contains("event")&&record["event"].is_string()){
        const auto& event=record["event"].get_ref<const std::string&>();
        if(event=="static-wake-scan-result"||event=="static-wake-restore-result")
            record["payloadSemantics"]={{"statusScope","retained-journal-state-and-reported-operation-errors"},
                {"itemCounts","cumulative-retained-state;not-writes-or-readbacks-in-this-operation"},
                {"lastErrorFreshness","unknown;may-be-an-inherited-entry-error"},
                {"lastScanEpochScope","journal-scan-start;not-per-item-observation-time"},
                {"currentItemReadbackVerified",nullptr},{"actualWritesThisOperation",nullptr},
                {"powercfgFailureCodeOrigin","wrapper-or-child-origin-not-recorded-in-legacy-error-string"}};
    }
    const auto& writer=writerIdentity();
    record["factSchemaVersion"]=2;
    record["writerPid"]=writer.pid;
    record["writerThreadId"]=GetCurrentThreadId();
    record["writerProcessCreation100ns"]=writer.creation100ns?Json(std::to_string(writer.creation100ns)):Json(nullptr);
    record["writerInstanceId"]=writer.creation100ns?
        Json("native:"+std::to_string(writer.pid)+":"+std::to_string(writer.creation100ns)):Json(nullptr);
    record["writerIdentityStatus"]=writer.creation100ns?"pid-and-process-creation-known":"pid-only;process-creation-unavailable";
    record["writerPowerGeneration"]=powerGeneration; // NOT details.generation or a kernel ScenarioInstanceId.
    record["observedTickMs"]=GetTickCount64();
    record["observationScope"]="journal-record-creation;not-kernel-event-time-or-action-completion";
    FILETIME utc{},local{};SYSTEMTIME utcParts{},localParts{};
    GetSystemTimeAsFileTime(&utc);
    const auto ticks=fileTimeValue(utc);
    constexpr std::uint64_t epoch=116444736000000000ULL;
    record["utcEpochMs"]=ticks>=epoch?Json((ticks-epoch)/10000):Json(nullptr);
    record["timestampUtc"]=FileTimeToSystemTime(&utc,&utcParts)?Json(isoTime(utcParts,"Z")):Json(nullptr);
    if(FileTimeToLocalFileTime(&utc,&local)&&FileTimeToSystemTime(&local,&localParts)){
        const auto localTicks=fileTimeValue(local);
        const auto difference=localTicks>=ticks?static_cast<std::int64_t>(localTicks-ticks):-static_cast<std::int64_t>(ticks-localTicks);
        const auto minutes=difference/600000000LL;
        const auto magnitude=minutes<0?-minutes:minutes;
        char zone[16]{};std::snprintf(zone,sizeof(zone),"%c%02lld:%02lld",minutes<0?'-':'+',magnitude/60,magnitude%60);
        record["timestampLocal"]=isoTime(localParts,zone);
        record["utcOffsetMinutes"]=minutes;
    }else{
        record["timestampLocal"]=nullptr;record["utcOffsetMinutes"]=nullptr;
    }
    record["clockScope"]="timestampUtc/utcEpochMs=record-creation-wall-clock;observedTickMs=Windows-boot-monotonic;time=legacy-local-label;details-time-is-original-event-evidence";
}
} // namespace ymcc::sleepfactlog
