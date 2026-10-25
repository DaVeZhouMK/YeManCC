#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
#include "msi_slot_api_evidence.h"
using json=nlohmann::json;using namespace ymcc::msi::slotEvidence;
struct Case {
    std::string name;
    uint32_t slot=0,sizeCode=0,readCode=0,reverseCode=0,count=4,returned=4;
    uint8_t index=0;
    bool gate=true,hasPath=true,hasReverse=true,noWrite=false;
    bool throwSize=false,throwRead=false,throwReverse=false,nulAfterReturned=false;
    std::wstring text=L"MSI";
    const char* status="path-round-trip-match";
    unsigned expectedPath=2,expectedReverse=1;
};
int main(int argc,char**argv){if(argc!=2)return 2;
    json out={{"allPlatformApisStubbed",true},{"realHardwareAccess",false},{"cases",json::array()}};unsigned failures=0;
    auto run=[&](const Case&c){unsigned sizes=0,reads=0,reverses=0;bool callbackInputs=true;
        PathQuery p;if(c.hasPath)p=[&](uint32_t slot,wchar_t*buffer,uint32_t*count)->uint32_t{
            callbackInputs&=slot==c.slot&&count!=nullptr;
            if(!buffer){++sizes;callbackInputs&=*count==0;*count=c.count;if(c.throwSize)throw std::runtime_error("size");return c.sizeCode;}
            ++reads;callbackInputs&=*count==c.count;
            // No zero-filled sentinel may be mistaken for an API-written NUL.
            for(uint32_t i=0;i<c.count+1;++i)callbackInputs&=buffer[i]!=0;
            *count=c.returned;if(c.throwRead)throw std::runtime_error("read");
            if(!c.noWrite&&c.text.size()+1<=uint64_t(c.count)+1){std::copy(c.text.begin(),c.text.end(),buffer);buffer[c.text.size()]=L'\0';}
            if(c.nulAfterReturned&&c.returned<=c.count)buffer[c.returned]=L'\0';
            return c.readCode;
        };
        ReverseQuery rev;if(c.hasReverse)rev=[&](const std::wstring&path,uint8_t*index)->uint32_t{++reverses;callbackInputs&=index&&*index==0xff&&!path.empty();*index=c.index;if(c.throwReverse)throw std::runtime_error("reverse");return c.reverseCode;};
        const auto r=query(c.slot,c.gate,p,rev);
        bool pass=std::strcmp(r.status,c.status)==0&&sizes+reads==c.expectedPath&&reverses==c.expectedReverse&&sizes<=1&&reads<=1&&reverses<=1&&callbackInputs;
        pass&=r.sizeAttempted==(sizes==1)&&r.readAttempted==(reads==1)&&r.reverseAttempted==(reverses==1);
        pass&=r.sizeReturned==(sizes==1&&!c.throwSize)&&r.readReturned==(reads==1&&!c.throwRead)&&r.reverseReturned==(reverses==1&&!c.throwReverse);
        if(c.throwSize||c.throwRead||c.throwReverse)pass&=!r.pathValid&&r.path.empty();
        if(r.sizeReturned)pass&=r.sizeCode==c.sizeCode&&r.requestedCount==c.count;
        if(r.readReturned)pass&=r.readCode==c.readCode&&r.returnedCount==c.returned;
        if(r.reverseReturned)pass&=r.reverseCode==c.reverseCode&&r.reverseIndex==c.index;
        if(std::strcmp(c.status,"path-round-trip-match")==0)pass&=r.pathValid&&r.path==c.text&&r.reverseIndex==c.slot;
        if(!pass)++failures;
        out["cases"].push_back({{"case",c.name},{"passed",pass},{"status",r.status},{"expectedStatus",c.status},{"sizeCalls",sizes},{"readCalls",reads},{"reverseCalls",reverses},{"sizeReturned",r.sizeReturned},{"readReturned",r.readReturned},{"reverseReturned",r.reverseReturned},{"sizeCodeKnown",r.sizeReturned},{"sizeCode",r.sizeReturned?json(r.sizeCode):json(nullptr)},{"readCode",r.readReturned?json(r.readCode):json(nullptr)},{"reverseCode",r.reverseReturned?json(r.reverseCode):json(nullptr)}});
    };
    auto c=Case{"happy-path"};run(c);
    c=Case{"slot-out-of-range"};c.slot=4;c.status="slot-out-of-range";c.expectedPath=c.expectedReverse=0;run(c);
    c=Case{"slot-uintmax"};c.slot=UINT32_MAX;c.status="slot-out-of-range";c.expectedPath=c.expectedReverse=0;run(c);
    c=Case{"availability-false"};c.gate=false;c.status="shared-availability-gate-false";c.expectedPath=c.expectedReverse=0;run(c);
    c=Case{"path-export-missing"};c.hasPath=false;c.status="path-export-unavailable";c.expectedPath=c.expectedReverse=0;run(c);
    for(auto code:{1u,122u,1167u,UINT32_MAX}){c=Case{"size-error-"+std::to_string(code)};c.sizeCode=code;c.status="size-query-error";c.expectedPath=1;c.expectedReverse=0;run(c);}
    c=Case{"zero-size"};c.count=0;c.status="zero-required-count";c.expectedPath=1;c.expectedReverse=0;run(c);
    for(auto count:{maxPathChars+1,UINT32_MAX}){c=Case{"ceiling-"+std::to_string(count)};c.count=count;c.status="diagnostic-count-ceiling";c.expectedPath=1;c.expectedReverse=0;run(c);}
    c=Case{"exact-ceiling"};c.count=c.returned=maxPathChars;c.text.assign(maxPathChars-1,L'M');run(c);
    for(auto code:{1u,122u,1167u,UINT32_MAX}){c=Case{"read-error-"+std::to_string(code)};c.readCode=code;c.status="path-query-error";c.expectedReverse=0;run(c);}
    for(auto count:{0u,5u,UINT32_MAX}){c=Case{"invalid-returned-"+std::to_string(count)};c.returned=count;c.status="returned-count-invalid";c.expectedReverse=0;run(c);}
    c=Case{"return-shrinks-with-valid-nul"};c.count=6;run(c);
    c=Case{"no-api-writes"};c.noWrite=true;c.status="unterminated-within-returned-count";c.expectedReverse=0;run(c);
    c=Case{"no-nul"};c.text=L"MSIX";c.status="unterminated-within-returned-count";c.expectedReverse=0;run(c);
    c=Case{"nul-beyond-returned-count"};c.count=5;c.text=L"MSIX";c.nulAfterReturned=true;c.status="unterminated-within-returned-count";c.expectedReverse=0;run(c);
    c=Case{"nul-only"};c.count=c.returned=1;c.text=L"";c.status="empty-path";c.expectedReverse=0;run(c);
    c=Case{"empty-path"};c.text=L"";c.status="empty-path";c.expectedReverse=0;run(c);
    c=Case{"reverse-export-missing"};c.hasReverse=false;c.status="reverse-export-unavailable";c.expectedReverse=0;run(c);
    for(auto code:{1u,122u,1167u,UINT32_MAX}){c=Case{"reverse-error-"+std::to_string(code)};c.reverseCode=code;c.index=2;c.status="reverse-query-error";run(c);}
    for(auto index:{4u,0xffu}){c=Case{"reverse-invalid-"+std::to_string(index)};c.index=uint8_t(index);c.status="reverse-index-out-of-range";run(c);}
    for(uint32_t slot=0;slot<4;++slot)for(uint8_t index=0;index<4;++index){c=Case{"round-trip-"+std::to_string(slot)+"-"+std::to_string(index)};c.slot=slot;c.index=index;c.status=slot==index?"path-round-trip-match":"path-round-trip-mismatch";run(c);}
    c=Case{"size-throws-after-mutating-output"};c.throwSize=true;c.status="diagnostic-query-exception";c.expectedPath=1;c.expectedReverse=0;run(c);
    c=Case{"read-throws-after-mutating-output"};c.throwRead=true;c.status="diagnostic-query-exception";c.expectedReverse=0;run(c);
    c=Case{"reverse-throws-after-mutating-output"};c.throwReverse=true;c.status="diagnostic-query-exception";run(c);
    out["caseCount"]=out["cases"].size();out["failureCount"]=failures;out["passed"]=failures==0;out["noRetries"]=true;
    std::ofstream f(argv[1]);f<<out.dump(2);f.close();std::cout<<"MSI_SLOT_API_CALLBACK_TESTS "<<out["caseCount"]<<" failures="<<failures<<"\n";return failures?3:0;
}
