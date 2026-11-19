#pragma once
#include <windows.h>
#include <atomic>
#include <iostream>
#include "json.hpp"
namespace loaderCloseProbe {
inline std::string mode;
inline std::atomic<unsigned> pausedReads{0},pausedAttempts{0},connectedReads{0};
inline void observeHealth(bool paused,bool connected,unsigned attempts) noexcept {
    if(mode=="paused"&&paused){++pausedReads;pausedAttempts=attempts;}
    if(mode=="paused"&&!paused&&connected)++connectedReads;
}
inline std::atomic<HANDLE> held{nullptr};
inline std::atomic<unsigned> protections{0},attempts{0},refusals{0};
inline std::atomic<DWORD> error{0};
inline std::atomic<unsigned> jobIngresses{0};
inline void protect(HANDLE& value) {
    if(mode=="joint-live")++jobIngresses;
    if(mode!="final"&&mode!="reentry"&&mode!="paused"&&mode!="joint"&&mode!="joint-live")return;
    if(protections.load()!=0)return;
    if(mode=="joint"||mode=="joint-live"){
    HANDLE restricted=nullptr;
    const DWORD access=JOB_OBJECT_ASSIGN_PROCESS|JOB_OBJECT_SET_ATTRIBUTES|JOB_OBJECT_QUERY|SYNCHRONIZE;
    if(!DuplicateHandle(GetCurrentProcess(),value,GetCurrentProcess(),&restricted,access,FALSE,0))
        throw std::runtime_error("joint-native-job-restrict-failed");
    if(!CloseHandle(value)){const DWORD saved=GetLastError();CloseHandle(restricted);SetLastError(saved);throw std::runtime_error("joint-native-original-unprotected-close-failed");}
    value=restricted;
    }
    if(!SetHandleInformation(value,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE))
        throw std::runtime_error("loader-final-close-real-protect-failed");
    held=value;++protections;
}
inline BOOL close(HANDLE value) noexcept {
    const BOOL result=CloseHandle(value);const DWORD observed=result?ERROR_SUCCESS:GetLastError();
    if(value==held.load()){++attempts;if(!result)++refusals;error=observed;}
    SetLastError(observed);return result;
}
inline std::atomic<unsigned> terminateCalls{0},terminateRefusals{0},fallbackCalls{0},fallbackRefusals{0};
inline std::atomic<DWORD> terminateError{0},fallbackError{0};
inline BOOL terminate(HANDLE value,UINT code) noexcept {
    const BOOL result=TerminateJobObject(value,code);const DWORD observed=result?ERROR_SUCCESS:GetLastError();
    if(value==held.load()){++terminateCalls;if(!result)++terminateRefusals;terminateError=observed;}
    SetLastError(observed);return result;
}
inline BOOL fallbackClose(HANDLE value) noexcept {
    const BOOL result=CloseHandle(value);const DWORD observed=result?ERROR_SUCCESS:GetLastError();
    if(value==held.load()){++fallbackCalls;if(!result)++fallbackRefusals;fallbackError=observed;}
    SetLastError(observed);return result;
}
inline nlohmann::json retainedFacts() {
    std::lock_guard lock(ymcc::deckybinding::detail::retainedMutex);
    unsigned matched=0,handles=0;bool destroyed=true;
    for(auto* state=ymcc::deckybinding::detail::retained;state;state=state->retainedNext){
        std::lock_guard stateLock(state->mutex);
        if(state->job.value!=held.load())continue;
        const auto value=ymcc::deckybinding::detail::snapshotLocked(*state);
        ++matched;handles+=value.handles;destroyed=destroyed&&value.fullyDestroyed;
    }
    return {{"matched",matched},{"handles",handles},{"fullyDestroyed",destroyed}};
}
inline void allowOriginalClose(){
    const auto original=held.exchange(nullptr);
    if(!original||!SetHandleInformation(original,HANDLE_FLAG_PROTECT_FROM_CLOSE,0))throw std::runtime_error("fixture-original-unprotect-failed");
    // Ownership stays with product State. Never close/reuse the raw value here.
}
inline nlohmann::json facts() {
    DWORD flags=0;const HANDLE value=held.load();const BOOL readable=value&&GetHandleInformation(value,&flags);
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    const BOOL queried=readable&&QueryInformationJobObject(value,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr);
    return {{"terminateCalls",terminateCalls.load()},{"terminateRefusals",terminateRefusals.load()},{"terminateError",terminateError.load()},
        {"fallbackCalls",fallbackCalls.load()},{"fallbackRefusals",fallbackRefusals.load()},{"fallbackError",fallbackError.load()},
        {"protections",protections.load()},{"nativeCloseAttempts",attempts.load()},{"nativeCloseRefusals",refusals.load()},
        {"nativeCloseError",error.load()},{"originalHandleStillValid",readable!=FALSE},
        {"originalHandleProtected",readable&&(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE)!=0},
        {"jobCountKnown",queried!=FALSE},{"jobActiveProcesses",queried?info.ActiveProcesses:MAXDWORD}};
}
struct Release {
    ~Release() noexcept {
        const auto value=held.exchange(nullptr);DWORD flags=0;
        const BOOL valid=value&&GetHandleInformation(value,&flags);
        const BOOL released=valid&&SetHandleInformation(value,HANDLE_FLAG_PROTECT_FROM_CLOSE,0)&&CloseHandle(value);
        try{std::cout<<"LOADER_CLOSE_PROBE_RELEASE "<<nlohmann::json{{"scope","fixture-after-original-measurement"},
            {"originalHandleValid",valid!=FALSE},{"exactOriginalUnprotectedAndClosed",released!=FALSE},
            {"nativeRefusals",refusals.load()}}.dump()<<'\n'<<std::flush;}catch(...){}
    }
};
}

