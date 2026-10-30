#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_target_return_record.h"
#ifdef _WIN32
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <vector>

// A one-shot observation of exactly one PID/birth pair. No recovery authority,
// persistent I/O, termination or device operations. An ambiguous handle probe
// may use one bounded read-only process census, never a background poll.
namespace ymcc::targetReturn::process {
enum class State { Alive, Dead, Unknown };
enum class Reason {
    InvalidIdentity, OpenFailed, PidQueryFailed, PidMismatch, BirthQueryFailed,
    BirthMissing, PidReused, MatchingRunning, MatchingExited, PidNotFound,
    WaitFailed, UnexpectedWait, CensusPidNotFound, CensusPidReused,
    CensusMatchingRunning, CensusUnavailable, CensusQueryFailed, CensusTooLarge,
    CensusMalformed, CensusSelfUnproven
};
struct Probe {
    State state = State::Unknown;
    Reason reason = Reason::InvalidIdentity;
    DWORD win32Error = ERROR_SUCCESS;
    DWORD directWin32Error = ERROR_SUCCESS;
    unsigned censusAttempts = 0;
    DWORD censusBytes = 0;
    DWORD censusNtStatus = 0;
    std::uint32_t censusRows = 0;
};
inline const char* stateName(State state) noexcept {
    switch(state) {
    case State::Alive: return "alive";
    case State::Dead: return "dead";
    default: return "unknown";
    }
}
inline const char* reasonName(Reason reason) noexcept {
    switch(reason) {
    case Reason::InvalidIdentity: return "invalid-identity";
    case Reason::OpenFailed: return "open-failed";
    case Reason::PidQueryFailed: return "pid-query-failed";
    case Reason::PidMismatch: return "pid-mismatch";
    case Reason::BirthQueryFailed: return "birth-query-failed";
    case Reason::BirthMissing: return "birth-missing";
    case Reason::PidReused: return "pid-reused";
    case Reason::MatchingRunning: return "matching-running";
    case Reason::MatchingExited: return "matching-exited";
    case Reason::PidNotFound: return "pid-not-found";
    case Reason::WaitFailed: return "wait-failed";
    case Reason::UnexpectedWait: return "unexpected-wait";
    case Reason::CensusPidNotFound: return "census-pid-not-found";
    case Reason::CensusPidReused: return "census-pid-reused";
    case Reason::CensusMatchingRunning: return "census-matching-running";
    case Reason::CensusUnavailable: return "census-api-unavailable";
    case Reason::CensusQueryFailed: return "census-query-failed";
    case Reason::CensusTooLarge: return "census-budget-exceeded";
    case Reason::CensusMalformed: return "census-malformed";
    case Reason::CensusSelfUnproven: return "census-self-unproven";
    default: return "unknown-reason";
    }
}
namespace leafDetail {
inline std::uint64_t fileTimeValue(const FILETIME& time) noexcept {
    return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
struct OwnedHandle {
    HANDLE value;
    explicit OwnedHandle(HANDLE handle) noexcept : value(handle) {}
    ~OwnedHandle() { if(value) CloseHandle(value); }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
};
}

// The caller retains ownership and must keep the borrowed handle open for the
// whole call. A process pseudo handle is accepted; an event is not a process.
inline std::optional<ProcessIdentity> capture(HANDLE borrowedProcess, DWORD* error = nullptr) {
    if(error) *error = ERROR_SUCCESS;
    const DWORD pid = GetProcessId(borrowedProcess);
    if(!pid) {
        const DWORD failure = GetLastError();
        if(error) *error = failure;
        return std::nullopt;
    }
    FILETIME birth{}, exit{}, kernel{}, user{};
    if(!GetProcessTimes(borrowedProcess, &birth, &exit, &kernel, &user)) {
        const DWORD failure = GetLastError();
        if(error) *error = failure;
        return std::nullopt;
    }
    const auto value = leafDetail::fileTimeValue(birth);
    if(!value) {
        if(error) *error = ERROR_INVALID_DATA;
        return std::nullopt;
    }
    return ProcessIdentity{pid, std::to_string(value)};
}

// A successful Alive observation can go stale immediately. Dead refers ONLY to
// this recorded identity, never its descendants, CLI, Host or virtual target.
inline Probe probeRecordedOwnerHandle(const ProcessIdentity& recorded) {
    if(!ymcc::targetReturn::detail::process(recorded))
        return {State::Unknown, Reason::InvalidIdentity, ERROR_INVALID_DATA};
    const HANDLE opened = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, recorded.pid);
    if(!opened) {
        const DWORD failure = GetLastError();
        if(failure == ERROR_INVALID_PARAMETER)
            return {State::Dead, Reason::PidNotFound, failure};
        return {State::Unknown, Reason::OpenFailed, failure};
    }
    leafDetail::OwnedHandle owned(opened);
    const DWORD actualPid = GetProcessId(opened);
    if(!actualPid) return {State::Unknown, Reason::PidQueryFailed, GetLastError()};
    if(actualPid != recorded.pid)
        return {State::Unknown, Reason::PidMismatch, ERROR_INVALID_DATA};
    FILETIME birth{}, exit{}, kernel{}, user{};
    if(!GetProcessTimes(opened, &birth, &exit, &kernel, &user))
        return {State::Unknown, Reason::BirthQueryFailed, GetLastError()};
    const auto value = leafDetail::fileTimeValue(birth);
    if(!value) return {State::Unknown, Reason::BirthMissing, ERROR_INVALID_DATA};
    if(std::to_string(value) != recorded.creationTime)
        return {State::Dead, Reason::PidReused, ERROR_SUCCESS};
    const DWORD waited = WaitForSingleObject(opened, 0);
    if(waited == WAIT_TIMEOUT) return {State::Alive, Reason::MatchingRunning, ERROR_SUCCESS};
    if(waited == WAIT_OBJECT_0) return {State::Dead, Reason::MatchingExited, ERROR_SUCCESS};
    if(waited == WAIT_FAILED) return {State::Unknown, Reason::WaitFailed, GetLastError()};
    return {State::Unknown, Reason::UnexpectedWait, ERROR_INVALID_DATA};
}
namespace leafDetail {
// SystemProcessInformation prefix only (class 5). The complete chain and our
// own PID/birth must agree with GetProcessTimes before any row is authority.
// A changed/partial ABI remains Unknown, including a missing target PID.
struct NativeUnicodeString { USHORT length, maximumLength; PWSTR buffer; };
struct NativeProcessPrefix {
    ULONG nextEntryOffset, threadCount;
    BYTE reserved[24];
    LARGE_INTEGER creation, user, kernel;
    NativeUnicodeString imageName;
    LONG basePriority;
    HANDLE uniqueProcessId;
};
static_assert(offsetof(NativeProcessPrefix, creation)==32, "process creation prefix ABI");
static_assert(offsetof(NativeProcessPrefix, uniqueProcessId)==(sizeof(void*)==8?80:68), "process PID prefix ABI");
using QuerySystemProcesses=LONG(NTAPI*)(ULONG,PVOID,ULONG,PULONG);
inline Probe censusRecordedOwner(const ProcessIdentity& recorded, const Probe& direct) {
    Probe result{State::Unknown,Reason::CensusUnavailable,ERROR_NOT_SUPPORTED};
    result.directWin32Error=direct.win32Error;
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto query=module?reinterpret_cast<QuerySystemProcesses>(GetProcAddress(module,"NtQuerySystemInformation")):nullptr;
    if(!query)return result;
    DWORD selfError=ERROR_SUCCESS;
    const auto self=capture(GetCurrentProcess(),&selfError);
    if(!self || self->pid!=GetCurrentProcessId()) {
        result.reason=Reason::CensusSelfUnproven;result.win32Error=selfError?selfError:ERROR_INVALID_DATA;return result;
    }
    constexpr ULONG maxBytes=8*1024*1024, initialBytes=64*1024;
    constexpr unsigned maxAttempts=3;
    ULONG capacity=initialBytes;
    try {
        std::vector<BYTE> bytes;
        for(unsigned attempt=1;attempt<=maxAttempts;++attempt) {
            result.censusAttempts=attempt;
            bytes.resize(capacity);
            ULONG returned=0;
            const LONG status=query(5,bytes.data(),capacity,&returned);
            result.censusNtStatus=static_cast<DWORD>(status);
            if(status==static_cast<LONG>(0xc0000004u) || status==static_cast<LONG>(0xc0000023u)) {
                result.censusBytes=returned;
                if(attempt==maxAttempts || returned>maxBytes || capacity==maxBytes) {
                    result.reason=Reason::CensusTooLarge;result.win32Error=ERROR_INSUFFICIENT_BUFFER;return result;
                }
                const auto desired=(std::max)(std::uint64_t(returned)+4096,std::uint64_t(capacity)*2);
                if(desired>maxBytes) {result.reason=Reason::CensusTooLarge;result.win32Error=ERROR_INSUFFICIENT_BUFFER;return result;}
                capacity=static_cast<ULONG>(desired);continue;
            }
            if(status<0) {result.reason=Reason::CensusQueryFailed;result.win32Error=ERROR_GEN_FAILURE;return result;}
            result.censusBytes=returned;
            if(returned<sizeof(NativeProcessPrefix) || returned>capacity) {
                result.reason=Reason::CensusMalformed;result.win32Error=ERROR_INVALID_DATA;return result;
            }
            std::size_t offset=0;bool selfSeen=false,targetSeen=false,targetMatches=false;
            std::set<std::uintptr_t> seen;
            for(;;) {
                if(offset>returned || returned-offset<sizeof(NativeProcessPrefix)) {
                    result.reason=Reason::CensusMalformed;result.win32Error=ERROR_INVALID_DATA;return result;
                }
                NativeProcessPrefix row{};std::memcpy(&row,bytes.data()+offset,sizeof(row));
                ++result.censusRows;
                const auto pid=reinterpret_cast<std::uintptr_t>(row.uniqueProcessId);
                if(pid>(std::numeric_limits<std::uint32_t>::max)() ||
                    !seen.insert(pid).second || row.creation.QuadPart<0) {
                    result.reason=Reason::CensusMalformed;result.win32Error=ERROR_INVALID_DATA;return result;
                }
                const auto birth=std::to_string(static_cast<std::uint64_t>(row.creation.QuadPart));
                if(pid==self->pid) {
                    if(birth!=self->creationTime) {result.reason=Reason::CensusSelfUnproven;result.win32Error=ERROR_INVALID_DATA;return result;}
                    selfSeen=true;
                }
                if(pid==recorded.pid) {
                    if(row.creation.QuadPart==0) {result.reason=Reason::CensusMalformed;result.win32Error=ERROR_INVALID_DATA;return result;}
                    targetSeen=true;targetMatches=birth==recorded.creationTime;
                }
                if(row.nextEntryOffset==0)break;
                if(row.nextEntryOffset<sizeof(row) || row.nextEntryOffset%sizeof(void*) ||
                    row.nextEntryOffset>returned-offset || returned-offset-row.nextEntryOffset<sizeof(row)) {
                    result.reason=Reason::CensusMalformed;result.win32Error=ERROR_INVALID_DATA;return result;
                }
                offset+=row.nextEntryOffset;
            }
            if(!selfSeen) {result.reason=Reason::CensusSelfUnproven;result.win32Error=ERROR_INVALID_DATA;return result;}
            result.win32Error=ERROR_SUCCESS;
            result.state=targetSeen&&targetMatches?State::Alive:State::Dead;
            result.reason=!targetSeen?Reason::CensusPidNotFound:targetMatches?Reason::CensusMatchingRunning:Reason::CensusPidReused;
            return result;
        }
    } catch(...) {result.reason=Reason::CensusQueryFailed;result.win32Error=ERROR_NOT_ENOUGH_MEMORY;}
    return result;
}
}
// VC-COLD-HOST-IDENTITY-20261026-R1. A protected/reused PID or a retained exited
// process object must not strand all future Start/OFF requests at Unknown.
// The census proves this exact writer identity only. The existing cold consumer
// must STILL prove all virtual targets absent, writers quiet and owned paths
// returned before it consumes historical debt. Never kill/adopt a process here.
inline Probe probeRecordedOwner(const ProcessIdentity& recorded) {
    const auto direct=probeRecordedOwnerHandle(recorded);
    if(direct.state!=State::Unknown || direct.reason==Reason::InvalidIdentity)return direct;
    return leafDetail::censusRecordedOwner(recorded,direct);
}
} // namespace ymcc::targetReturn::process
#endif
