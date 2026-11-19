// Private directed extension of the original C++ fixture; NOT_RUN.
// run uses memory environment ports. dispatchFinalOnce uses controlled resume
// leaves, never WindowsPorts::spawnPrimary/ResumeThread, a process, or power IO.
struct BootSelectedMemory : Memory {
    std::function<void(const char*)> boundary;
    unsigned authorizationReads=0;
    void hit(const char* stage){if(boundary)boundary(stage);}
    bool bootAuthorized(){++authorizationReads;return Memory::bootAuthorized();}
    bool lock(){const bool value=Memory::lock();hit("lock");return value;}
    bool readLedger(Ledger& value){const bool okay=Memory::readLedger(value);hit("ledger");return okay;}
    bool saveLedger(const Ledger& value){const bool okay=Memory::saveLedger(value);hit("save");return okay;}
    auto steamExecutable(){auto value=Memory::steamExecutable();hit("executable");return value;}
    Presence snapshot(const std::vector<std::wstring>& addons){auto value=Memory::snapshot(addons);hit(snapshots==1?"first-snapshot":"final-snapshot");return value;}
};

int main(int argc,char** argv){
    if(argc!=2||std::string(argv[1])!="--boot-selected")return 2;
    nlohmann::json cases=nlohmann::json::array();unsigned assertions=0;
    const auto need=[&](bool value,const char* why){++assertions;if(!value)throw std::runtime_error(why);};
    const auto check=[&](const char* name,const auto& fn){
        try{fn();cases.push_back({{"name",name},{"passed",true}});}
        catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}
    };
    const std::function<bool()> healthy=[]{return true;};
    check("boot selected requires selection and current before IO",[&]{
        for(int missing=0;missing!=3;++missing){BootSelectedMemory f;
            const auto r=run(f,Origin::BootSelected,{},missing==1?f.executable:L"",missing==0?healthy:std::function<bool()>{});
            need(!r.ok&&r.reason=="steam-boot-selected-context-required","missing authority accepted");
            need(!f.lockCalls&&!f.spawns&&!f.saves&&!f.authorizationReads,"invalid source performed IO");
        }
    });
    check("real gate-aware port cannot omit its final boot gate",[&]{
        struct GateAware : BootSelectedMemory { bool hasFinalDispatchGate()const{return false;} } f;unsigned calls=0;
        const auto r=run(f,Origin::BootSelected,{},f.executable,[&]{++calls;return true;});
        need(!r.ok&&r.reason=="steam-boot-selected-final-gate-required","missing final gate accepted");
        need(!calls&&!f.lockCalls&&!f.spawns&&!f.saves,"missing final gate reached IO");
    });
    check("unknown origin refuses before current or environment",[&]{BootSelectedMemory f;unsigned calls=0;
        const auto r=run(f,static_cast<Origin>(999),{},f.executable,[&]{++calls;return true;});
        need(!r.ok&&r.reason=="steam-launch-origin-invalid","unknown origin accepted");need(!calls&&!f.lockCalls&&!f.spawns,"invalid origin effects");
    });
    check("boot selected is plain no addons no manual fallback or logon grant",[&]{
        for(bool consumed:{false,true}){BootSelectedMemory f;f.authorized=false;f.ledger.bootConsumed=consumed;unsigned overlay=0;
            const auto r=run(f,Origin::BootSelected,[&]{++overlay;},f.executable,healthy);
            need(r.ok&&r.dispatched&&f.steamStarts==1&&f.spawns==1,"boot selected not dispatched once");
            need(f.arguments.empty()&&!f.primaryCalls&&!f.fallbackCalls&&!f.addonReads&&!overlay,"manual flags or cascade borrowed");
            need(f.ledger.bootConsumed==consumed&&!f.authorizationReads&&!f.locked,"logon grant consumed or lock retained");
        }
    });
    check("boot selected mismatch refuses without substituting installation",[&]{BootSelectedMemory f;
        const auto r=run(f,Origin::BootSelected,{},L"y:\\steam.exe",healthy);
        need(!r.ok&&r.reason=="steam-selected-executable-changed","selection substituted");need(!f.spawns&&!f.saves&&!f.addonReads&&!f.locked,"mismatch effects");
    });
    check("boot selected spelling retains existing slash and case equality",[&]{BootSelectedMemory f;
        need(run(f,Origin::BootSelected,{},L"X:/StEaM.ExE",healthy).dispatched,"equivalent path refused");need(f.arguments.empty(),"selected flags changed");
    });
    check("boot selected current false or throw refuses before IO",[&]{for(bool throws:{false,true}){BootSelectedMemory f;
        const auto r=run(f,Origin::BootSelected,{},f.executable,[&]()->bool{if(throws)throw std::runtime_error("current unavailable");return false;});
        need(!r.ok&&r.reason=="steam-launch-request-expired","revoked source accepted");need(!f.lockCalls&&!f.saves&&!f.spawns,"entry revocation effects");
    }});
    for(const char* phase:{"lock","ledger","first-snapshot","executable","save","final-snapshot"}){
        const std::string name=std::string("boot selected revocation after ")+phase;
        check(name.c_str(),[&]{BootSelectedMemory f;bool live=true;f.boundary=[&](const char* seen){if(std::string(seen)==phase)live=false;};
            const auto r=run(f,Origin::BootSelected,{},f.executable,[&]{return live;});
            need(!r.ok&&r.reason=="steam-launch-request-expired","late revocation ignored");
            need(!f.spawns&&!f.fallbackCalls&&!f.addonReads&&!f.locked&&!f.ledger.bootConsumed,"revocation dispatched or consumed grant");
            const bool afterSave=std::string(phase)=="save"||std::string(phase)=="final-snapshot";
            need(f.saves==(afterSave?1:0),"receipt boundary changed");
        });
    }
    check("boot selected last current checkpoint precedes spawn",[&]{BootSelectedMemory f;unsigned terminalChecks=0;
        const auto r=run(f,Origin::BootSelected,{},f.executable,[&]{return f.snapshots!=2||++terminalChecks!=2;});
        need(!r.ok&&r.reason=="steam-launch-request-expired"&&terminalChecks==2,"last checkpoint missing");need(!f.spawns&&!f.locked,"spawn after cancel");
    });
    check("boot selected recent dispatch and clock rollback remain fenced",[&]{
        for(uint64_t at:{uint64_t(100000),uint64_t(60001),uint64_t(100001)}){BootSelectedMemory f;f.ledger={true,true,at};
            const auto r=run(f,Origin::BootSelected,{},f.executable,healthy);
            need(!r.ok&&r.reason=="steam-launch-confirming","automatic fence opened");need(!f.spawns&&!f.saves&&f.ledger.bootConsumed&&!f.locked,"fence mutated receipt");
        }
    });
    check("boot selected exact 40 second boundary permits plain dispatch",[&]{BootSelectedMemory f;f.ledger={true,true,60000};
        need(run(f,Origin::BootSelected,{},f.executable,healthy).dispatched,"40s boundary refused");need(f.arguments.empty()&&f.ledger.bootConsumed,"boundary borrowed logon flags");
    });
    check("failed or cancelled automatic receipt still fences next request",[&]{for(bool cancelled:{false,true}){BootSelectedMemory f;f.spawnOk=false;bool live=true;
        if(cancelled)f.boundary=[&](const char* phase){if(std::string(phase)=="save")live=false;};
        need(!run(f,Origin::BootSelected,{},f.executable,[&]{return live;}).ok,"failure accepted");
        BootSelectedMemory next;next.ledger=f.ledger;const auto r=run(next,Origin::BootSelected,{},next.executable,healthy);
        need(!r.ok&&r.reason=="steam-launch-confirming"&&!next.spawns,"failed receipt replayed");need(!f.fallbackCalls&&!f.addonReads,"automatic fallback or cascade");
    }});
    check("boot selected running unknown and final presence never dispatch",[&]{for(int presence:{1,-1}){for(bool final:{false,true}){BootSelectedMemory f;
        if(final)f.finalPresence=presence;else f.presence=presence;
        const auto r=run(f,Origin::BootSelected,{},f.executable,healthy);
        need(!r.dispatched&&!f.spawns&&!f.fallbackCalls&&!f.addonReads,"presence or unknown reopened Steam");
        need(!f.locked&&!f.ledger.bootConsumed,"presence consumed logon grant");
    }}});
    check("default Manual and Logon retain flags addons and grant policy",[&]{for(auto origin:{Origin::Manual,Origin::Logon}){Memory f;unsigned overlay=0;
        const auto r=run(f,origin,[&]{++overlay;});need(r.dispatched&&f.arguments==startupArguments&&f.spawns==3&&f.addonReads==1,"old default semantics changed");
        need(f.ledger.bootConsumed==(origin==Origin::Logon)&&overlay==(origin==Origin::Logon?1u:0u),"old grant or overlay changed");
    }});
    check("manual remains free of receipt timing ban and retains fallback",[&]{for(uint64_t at:{uint64_t(99999),uint64_t(200000)}){Memory f;f.ledger={true,true,at};f.spawnOk=false;
        need(run(f,Origin::Manual,{},f.executable).dispatched,"manual timed ban added");need(f.primaryCalls==1&&f.fallbackCalls==1&&f.arguments.empty()&&f.ledger.bootConsumed,"manual fallback changed");
    }});
    check("default logon still refuses recent dispatch and consumed grant",[&]{Memory recent;recent.ledger={false,true,99999};
        need(!run(recent,Origin::Logon).ok&&!recent.spawns&&recent.ledger.bootConsumed,"logon timing policy changed");
        Memory consumed;consumed.ledger.bootConsumed=true;need(run(consumed,Origin::Logon).ok&&!consumed.snapshots&&!consumed.spawns,"consumed logon replayed");
    });
    check("empty final gate invokes identical leaf exactly once",[&]{for(bool okay:{false,true}){unsigned calls=0;
        const auto r=dispatchFinalOnce({},[&]{++calls;return okay;});need(r.invoked&&r.resumed==okay&&!r.threw&&calls==1,"default final flow changed");
    }});
    check("gate denial or lying acceptance cannot dispatch an uncalled leaf",[&]{for(bool reported:{false,true}){unsigned calls=0;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>&){return reported;},[&]{++calls;return true;});
        need(!r.invoked&&!r.resumed&&!r.threw&&!calls,"gate bool invented dispatch");
    }});
    check("gate acceptance preserves a successful controlled leaf",[&]{unsigned calls=0;
        const auto r=dispatchFinalOnce([](const std::function<bool()>& resume){return resume();},[&]{++calls;return true;});
        need(r.invoked&&r.resumed&&!r.threw&&calls==1,"accepted leaf not dispatched");
    });
    check("gate may call leaf at most once regardless of repeated requests",[&]{for(bool okay:{false,true}){unsigned calls=0;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>& resume){const bool first=resume();const bool second=resume();need(first==okay&&!second,"second leaf call accepted");return second;},[&]{++calls;return okay;});
        need(r.invoked&&r.resumed==okay&&!r.threw&&calls==1,"gate repeated actual leaf");
    }});
    check("gate false or throw after success preserves actual dispatched terminal",[&]{for(bool throws:{false,true}){unsigned calls=0,cleanup=0;bool held=false;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>& resume)->bool{
            struct Release{bool& held;~Release(){held=false;}} release{held};held=true;
            need(resume(),"controlled resume failed");if(throws)throw std::runtime_error("after dispatch");return false;
        },[&]{need(held,"resume escaped gate");++calls;return true;});
        need(!held,"gate scope retained");if(!r.resumed)++cleanup;
        need(r.invoked&&r.resumed&&r.threw==throws&&calls==1&&!cleanup,"running child misclassified or cleaned");
    }});
    check("gate throw before leaf unwinds before controlled cleanup",[&]{bool held=false;unsigned calls=0,cleanup=0;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>&)->bool{struct Release{bool& held;~Release(){held=false;}} release{held};held=true;throw std::runtime_error("denied");},[&]{++calls;return true;});
        need(!held,"cleanup entered with gate held");if(!r.resumed)++cleanup;
        need(!r.invoked&&!r.resumed&&r.threw&&!calls&&cleanup==1,"throw invented dispatch");
    });
    check("fresh absence stays inside gated leaf after controlled settings work",[&]{bool held=false,absent=true;unsigned kernelLeaves=0;std::vector<std::string> order;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>& resume){
            struct Release{bool& held;std::vector<std::string>& order;~Release(){held=false;order.push_back("unlock");}} release{held,order};held=true;order.push_back("settings");absent=false;return resume();
        },[&]{need(held,"absence escaped gate");order.push_back("absence");if(!absent)return false;++kernelLeaves;return true;});
        need(!held,"cleanup before unlock");if(!r.resumed)order.push_back("cleanup");
        need(r.invoked&&!r.resumed&&!kernelLeaves,"stale pre-gate absence authorized leaf");
        need(order==std::vector<std::string>{"settings","absence","unlock","cleanup"},"gate/absence/cleanup order changed");
    });
    check("controlled leaf error is captured before gate changes error slot",[&]{unsigned liveError=0,capturedError=0,calls=0;
        const auto r=dispatchFinalOnce([&](const std::function<bool()>& resume){(void)resume();liveError=123;return true;},[&]{++calls;liveError=5;capturedError=liveError;return false;});
        need(r.invoked&&!r.resumed&&calls==1&&capturedError==5&&liveError==123,"leaf failure overwritten by gate acceptance");
    });
    check("gate throw after controlled leaf failure preserves failure and captured error",[&]{unsigned calls=0,capturedError=0;
        const auto r=dispatchFinalOnce([](const std::function<bool()>& resume)->bool{(void)resume();throw std::runtime_error("after failed leaf");},[&]{++calls;capturedError=5;return false;});
        need(r.invoked&&!r.resumed&&r.threw&&calls==1&&capturedError==5,"gate exception replaced leaf failure");
    });
    check("post-gate throwing diagnostic preserves scalar leaf terminal and saved error",[&]{
        // Controlled leaves only: no Windows spawn, Kernel call, or real error slot.
        for(int terminal:{0,1,2}){bool held=false;unsigned calls=0,cleanup=0;const char* stage=nullptr;DWORD savedError=0;
            const auto r=dispatchFinalOnce([&](const std::function<bool()>& resume){
                struct Release{bool& held;~Release(){held=false;}} release{held};held=true;return resume();
            },[&]{
                if(terminal==0){stage="steam-absence-unverified";savedError=ERROR_BUSY;return false;}
                ++calls;const bool resumed=terminal==2;
                savedError=resumed?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
                stage=resumed?"resumed":"resume-thread";
                return resumed;
            });
            need(!held,"diagnostic still held gate");
            const DWORD diagnosticError=savedError;
            try {throw std::bad_alloc();}catch(...) {}
            if(!r.resumed)++cleanup;
            need(r.invoked&&!r.threw&&r.resumed==(terminal==2),"diagnostic changed dispatch terminal");
            need(calls==(terminal==0?0u:1u)&&cleanup==(terminal==2?0u:1u),"diagnostic cleaned running child or repeated leaf");
            need(stage&&std::string(stage)==(terminal==0?"steam-absence-unverified":terminal==1?"resume-thread":"resumed"),"scalar stage changed");
            need(diagnosticError==(terminal==0?ERROR_BUSY:terminal==1?ERROR_ACCESS_DENIED:ERROR_SUCCESS),"saved error overwritten");
        }
    });
    unsigned failed=0;for(const auto& row:cases)if(!row["passed"].get<bool>())++failed;
    std::cout<<nlohmann::json{{"selection","boot-selected"},{"cases",cases},{"assertions",assertions},{"failed",failed},
        {"scope","run-memory-ports-and-final-helper-controlled-leaves"},{"actualKernelResumeCalls",0},{"actualSteamActions",0},
        {"nativePowerAuthorityExercised",false},{"windowsSpawnPrimaryExecuted",false},{"defaultGroupExecuted",false}}.dump()<<'\n';
    return failed?1:0;
}
