#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
namespace ymcc::msi::slotEvidence {
// Diagnostic allocation ceiling only; the shared control getter is untouched.
inline constexpr uint32_t maxPathChars=32768;
using PathQuery=std::function<uint32_t(uint32_t,wchar_t*,uint32_t*)>;
using ReverseQuery=std::function<uint32_t(const std::wstring&,uint8_t*)>;
struct Receipt {
    const char* status="not-attempted";
    bool sizeAttempted=false,readAttempted=false,reverseAttempted=false,pathValid=false;
    // Attempted does not imply that the API returned a status/output value.
    bool sizeReturned=false,readReturned=false,reverseReturned=false;
    uint32_t sizeCode=0,readCode=0,reverseCode=0,requestedCount=0,returnedCount=0;
    uint8_t reverseIndex=0xff;
    std::wstring path;
};
inline Receipt query(uint32_t slot,bool sharedGate,const PathQuery& pathQuery,const ReverseQuery& reverseQuery) {
    Receipt r;
    if(slot>3){r.status="slot-out-of-range";return r;}
    if(!sharedGate){r.status="shared-availability-gate-false";return r;}
    if(!pathQuery){r.status="path-export-unavailable";return r;}
    try {
        r.sizeAttempted=true;r.sizeCode=pathQuery(slot,nullptr,&r.requestedCount);r.sizeReturned=true;
        if(r.sizeCode!=0){r.status="size-query-error";return r;}
        if(!r.requestedCount){r.status="zero-required-count";return r;}
        if(r.requestedCount>maxPathChars){r.status="diagnostic-count-ceiling";return r;}
        // The reference GetDevicePath count includes NUL. Do not manufacture a
        // terminator with a zero-filled buffer or accept the extra guard cell.
        // A differing loaded DLL contract is evidence, never admission proof.
        std::vector<wchar_t> buffer(r.requestedCount+1,L'\xffff');r.returnedCount=r.requestedCount;
        r.readAttempted=true;r.readCode=pathQuery(slot,buffer.data(),&r.returnedCount);r.readReturned=true;
        if(r.readCode!=0){r.status="path-query-error";return r;}
        if(!r.returnedCount||r.returnedCount>r.requestedCount){r.status="returned-count-invalid";return r;}
        const auto end=std::find(buffer.begin(),buffer.begin()+r.returnedCount,L'\0');
        if(end==buffer.begin()+r.returnedCount){r.status="unterminated-within-returned-count";return r;}
        r.path.assign(buffer.begin(),end);
        if(r.path.empty()){r.status="empty-path";return r;}
        r.pathValid=true;
        if(!reverseQuery){r.status="reverse-export-unavailable";return r;}
        r.reverseAttempted=true;r.reverseCode=reverseQuery(r.path,&r.reverseIndex);r.reverseReturned=true;
        if(r.reverseCode!=0){r.status="reverse-query-error";return r;}
        if(r.reverseIndex>3){r.status="reverse-index-out-of-range";return r;}
        r.status=r.reverseIndex==slot?"path-round-trip-match":"path-round-trip-mismatch";
        return r;
    }catch(...){r.status="diagnostic-query-exception";r.pathValid=false;r.path.clear();return r;}
}
}
