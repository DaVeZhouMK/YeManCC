#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_target_return_record.h"
#ifdef _WIN32
#include <windows.h>

// A one-shot observation of exactly one PID/birth pair. No recovery authority,
// process enumeration, retry, persistent I/O, termination or device operations.
namespace ymcc::targetReturn::process {
enum class State { Alive, Dead, Unknown };
enum class Reason {
    InvalidIdentity, OpenFailed, PidQueryFailed, PidMismatch, BirthQueryFailed,
    BirthMissing, PidReused, MatchingRunning, MatchingExited, PidNotFound,
    WaitFailed, UnexpectedWait
};
struct Probe {
    State state = State::Unknown;
    Reason reason = Reason::InvalidIdentity;
    DWORD win32Error = ERROR_SUCCESS;
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
inline Probe probeRecordedOwner(const ProcessIdentity& recorded) {
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
} // namespace ymcc::targetReturn::process
#endif
