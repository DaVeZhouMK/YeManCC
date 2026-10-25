#pragma once
#include "monitor_hwinfo_config.h"
#include <array>
#include <optional>

namespace ymcc::hwinfo {
struct Request {
    bool restart=false,elevated=false,recoverOnly=false;
    unsigned period=0;
    std::wstring executable,powerControl;
};
struct Result {
    int code=0;bool needsElevation=false;DWORD win32=0;
    std::string detail="ready",sensorPolicy;
    bool elevationAttempted=false;
};
struct Proof {
    std::uint64_t poll=0;
    std::array<std::uint64_t,11> layout{};
    bool operator==(const Proof&)const=default;
    bool continues(const Proof& before)const{return layout==before.layout&&poll>before.poll;}
};
// The same transaction is executed by the application IPC pool and, only when
// needed, its own short-lived elevated command mode. Ops owns every OS handle.
// This is demand-driven recovery, never another steady sensor collector.
template<class Ops> Result transaction(Ops& ops,const Request& request){
    Result result;
    const auto checkpoint=[&]{if constexpr(requires{ops.interrupted();}){if(ops.interrupted())throw Error(15,"Recovery invalidated before the next mutation");}};
    try{
        checkpoint();
        if(!ops.acquire(request.elevated?10000:0)){result.code=11;result.detail="recovery-busy";return result;}
        struct Release{Ops&ops;~Release(){ops.release();}} release{ops};
        checkpoint();
        if((!request.restart||request.recoverOnly)&&ops.healthy()){
            checkpoint();
            if(request.period){result.code=12;result.detail="period-deferred-until-restart";return result;}
            ops.applyAffinity();result.detail="already-healthy";return result;
        }
        const auto path=ops.resolve(request.executable);
        if(path.empty())throw Error(2,"HWiNFO64.exe was not found");
        ops.prepare(path,request); // Validate policy/config before stopping.
        checkpoint();ops.stop();
        checkpoint();ops.configure(path,request); // Re-read/merge after producer exit.
        checkpoint();result.sensorPolicy=ops.sensorBudget(path);
        ops.assertStopped();
        checkpoint();ops.launch(path);
        std::optional<Proof> previous;bool sawProof=false;
        for(unsigned i=0;i<30;++i){
            checkpoint();
            if(ops.exited())throw Error(6,"HWiNFO64.exe exited before continuing production was confirmed");
            const auto proof=ops.proof(request.period);
            checkpoint();
            if(proof){sawProof=true;if(previous&&proof->continues(*previous)){result.detail="continuing-producer-confirmed";return result;}previous=proof;}
            else previous.reset();
            if(!ops.wait(1000))throw Error(15,"Recovery observation interrupted; producer was not terminated");
        }
        if(sawProof)throw Error(14,"Continuing HWiNFO producer updates were not confirmed within 30 seconds");
        if(request.period)throw Error(13,"Shared memory and requested HWiNFO polling period were not confirmed within 30 seconds");
        throw Error(7,"Fresh HWiNFO shared memory was not confirmed within 30 seconds");
    }catch(const Error&e){result.code=e.code;result.detail=e.what();result.needsElevation=e.elevation;result.win32=e.win32;}
    catch(const std::exception&e){result.code=1;result.detail=e.what();}
    return result;
}
}
