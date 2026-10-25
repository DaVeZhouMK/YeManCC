#pragma once
#include <windows.h>
#include <powrprof.h>
#include <string>
namespace ymcc::idle {
// Local GUID storage keeps this shared read-only utility independent of uuid.lib.
inline constexpr GUID videoSub={0x7516b95f,0xf776,0x4464,{0x8c,0x53,0x06,0x16,0x7f,0x40,0xcc,0x99}};
inline constexpr GUID videoOff={0x3c0bc021,0xc8a8,0x4e07,{0xa9,0x73,0x6b,0x14,0xcb,0xcb,0x2b,0x7e}};
inline constexpr GUID videoDim={0x17aaa29b,0x8b43,0x4b94,{0xaa,0xfe,0x35,0xf6,0x4d,0xaa,0xf1,0xee}};
inline constexpr GUID sessionDisplay={0x2b84c20e,0xad23,0x4ddf,{0x93,0xdb,0x05,0xff,0xbd,0x7e,0xfc,0xa5}};
struct Environment {
    bool lastInputOk=false,idleValid=false,sourceKnown=false;
    DWORD lastInputTick=0,idleMs=0,lastInputError=0;
    int acLineStatus=-1;
    DWORD activePlanStatus=ERROR_NOT_READY,acOffStatus=ERROR_NOT_READY,dcOffStatus=ERROR_NOT_READY;
    DWORD acDimStatus=ERROR_NOT_READY,dcDimStatus=ERROR_NOT_READY;
    DWORD acOffSec=0,dcOffSec=0,acDimSec=0,dcDimSec=0;
    GUID plan{};
    LONG executionStatus=ERROR_NOT_READY;DWORD executionFlags=0;
};
inline Environment readEnvironment() noexcept {
    Environment e;
    LASTINPUTINFO input{sizeof(input)};
    e.lastInputOk=GetLastInputInfo(&input)!=FALSE;
    if(e.lastInputOk){e.lastInputTick=input.dwTime;e.idleMs=DWORD(GetTickCount()-input.dwTime);
        // Do not interpret a future/inconsistent tick as weeks of inactivity.
        e.idleValid=e.idleMs<0x80000000u;
    }else e.lastInputError=GetLastError();
    SYSTEM_POWER_STATUS power{};
    if(GetSystemPowerStatus(&power)){e.acLineStatus=power.ACLineStatus;e.sourceKnown=power.ACLineStatus==0||power.ACLineStatus==1;}
    GUID* active=nullptr;e.activePlanStatus=PowerGetActiveScheme(nullptr,&active);
    if(e.activePlanStatus==ERROR_SUCCESS&&active){
        e.plan=*active;
        e.acOffStatus=PowerReadACValueIndex(nullptr,active,&videoSub,&videoOff,&e.acOffSec);
        e.dcOffStatus=PowerReadDCValueIndex(nullptr,active,&videoSub,&videoOff,&e.dcOffSec);
        e.acDimStatus=PowerReadACValueIndex(nullptr,active,&videoSub,&videoDim,&e.acDimSec);
        e.dcDimStatus=PowerReadDCValueIndex(nullptr,active,&videoSub,&videoDim,&e.dcDimSec);
    }
    if(active)LocalFree(active);
    // Query only: NEVER SetThreadExecutionState or clear another request.
    e.executionStatus=CallNtPowerInformation(SystemExecutionState,nullptr,0,&e.executionFlags,sizeof(e.executionFlags));
    return e;
}
inline bool selectedTimeout(const Environment& e,bool dim,DWORD& seconds) {
    if(!e.sourceKnown||e.activePlanStatus!=ERROR_SUCCESS)return false;
    const bool ac=e.acLineStatus==1;
    const DWORD status=dim?(ac?e.acDimStatus:e.dcDimStatus):(ac?e.acOffStatus:e.dcOffStatus);
    seconds=dim?(ac?e.acDimSec:e.dcDimSec):(ac?e.acOffSec:e.dcOffSec);
    return status==ERROR_SUCCESS;
}
} // namespace ymcc::idle
