// Included only by the original settings fixture's explicit owned-apply scope.
// Real getter/ApplyOwned and Observer publication are generated from frozen source.
// Boot current, localconfig choice and transport are controlled leaves.
#include <mutex>
static std::mutex g_steamSettingsMx;
static json steamMonitorApplyOwned(const json&,const std::function<bool()>&);
struct OwnedMonitorState {
    bool current=true,isolated=false,throwSet=false,throwGet=false;
    unsigned ownerCalls=0,expireAtOwnerCall=0,gateCalls=0,gateRepeats=8,attempts=0,discoveryReads=0;bool setOwner=false,getOwner=false;
    json client=json::object(),patch={{"position",4}};
    std::string setMode="ok",getMode="ok";
    std::function<void()> beforeGate,afterSet,afterGet,beforeSetOwner,beforeGetOwner;
    std::vector<std::string> operations;
};
static OwnedMonitorState owned;
static bool monitorOwnedCurrent(){++owned.ownerCalls;if(owned.expireAtOwnerCall&&owned.ownerCalls>=owned.expireAtOwnerCall)owned.current=false;return owned.current;}
static json monitorOwnedTransport(DWORD user,const json& request,const std::function<bool()>& gate,const std::function<bool()>& pureOwner){
    const bool isSet=request.value("operation",std::string{})=="settings.set";
    (isSet?owned.setOwner:owned.getOwner)=bool(pureOwner);
    auto& ownerHook=isSet?owned.beforeSetOwner:owned.beforeGetOwner;
    if(ownerHook){auto hook=std::move(ownerHook);ownerHook={};hook();}
    if(pureOwner&&!pureOwner())return {{"ok",false},{"mutated",false},{"reason","live-action-cancelled"}};
    ++owned.discoveryReads; // Controlled original public discovery after pure owner.
    ++owned.attempts;
    for(unsigned i=0;i<owned.gateRepeats;++i){
        if(owned.beforeGate){const auto hook=std::move(owned.beforeGate);owned.beforeGate={};hook();}
        ++owned.gateCalls;if(gate&&!gate())return {{"ok",false},{"mutated",false},{"reason","live-action-cancelled"}};
    }
    if(user!=memory.account)return {{"ok",false},{"mutated",false},{"reason","steam-account-changed"}};
    const auto op=request.value("operation",std::string{});owned.operations.push_back(op);
    if(op=="settings.set"){
        ++memory.setCalls;
        if(owned.throwSet)throw std::runtime_error("controlled-set-transport-exception");
        if(owned.setMode=="malformed")return {{"ok","true"}};
        if(owned.setMode=="uncertain")return {{"ok",false},{"mutated",true},{"reason","live-transport-uncertain"}};
        owned.client.update(request.at("values"));
        if(owned.afterSet){const auto hook=std::move(owned.afterSet);owned.afterSet={};hook();}
        if(owned.setMode=="mismatch")return {{"ok",true},{"values",json::object()}};
        return {{"ok",true},{"via","live"},{"values",request.at("values")}};
    }
    if(op!="settings.get")throw std::runtime_error("unexpected-owned-monitor-operation");
    ++memory.liveReads;
    if(owned.throwGet)throw std::runtime_error("controlled-get-transport-exception");
    json values=json::object();for(const auto& k:request.at("keys"))values[k.get<std::string>()]=owned.client.at(k.get<std::string>());
    if(owned.afterGet){const auto hook=std::move(owned.afterGet);owned.afterGet={};hook();}
    if(owned.getMode=="malformed")return {{"ok",1},{"values",values}};
    if(owned.getMode=="mismatch")values["overlay_fps_counter_corner"]=5;
    if(owned.getMode=="invalid")values["overlay_fps_counter_corner"]=7;
    if(owned.getMode=="failed")return {{"ok",false},{"reason","live-debug-unavailable"}};
    return {{"ok",true},{"via","live"},{"values",values}};
}
static int monitorOwnedApplyFixture(int argc,char**argv){
    const bool earlyControl=argc==4&&std::string(argv[3])=="--early-owner-control";
    const bool earlyOnly=argc==4&&std::string(argv[3])=="--early-owner-boundaries";
    const bool control=argc==4&&std::string(argv[3])=="--birth-reuse-control";
    const bool repair=argc==4&&std::string(argv[3])=="--registered-pid-repair";
    if((argc!=3&&!control&&!repair&&!earlyControl&&!earlyOnly)||!argv[1][0]||std::string(argv[2])!="--monitor-owned-shared")return 2;
    const HANDLE out=CreateFileA(argv[1],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE)return 2;
    struct Owner{HANDLE h;~Owner(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}}owner{out};DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
    json raw={{"selection","monitor-owned-shared"},{"actualApplyOwnedFunction",true},{"actualObserverPublication",true},{"controlledBootConfigAndTransportLeaves",true},{"defaultSuiteExecuted",false},{"realSteamOperations",0},{"cases",json::array()},{"caseIds",json::array()}};
    json* active=nullptr;unsigned checks=0;bool failed=false;
    const auto check=[&](bool ok,const char* label){++checks;(*active)["checks"].push_back({{"name",label},{"passed",ok}});if(!ok)throw std::runtime_error(label);};
    const auto reset=[&]{g_steamSettingsObserver.stop();memory={};owned={};g_aiFanMockSession.isolated=false;memory.saved={{"pending",true},{"preserve","sentinel"}};json original=json::object();for(const auto& f:ymcc::steamsettings::fields)original[f.name]=f.integer?json(static_cast<int>(f.fallback)):json(f.fallback);owned.client=ymcc::steamsettings::clientPatch(original,-1);publish();};
    const auto run=[&](const char* id,const std::function<void()>& setup,unsigned sets,unsigned gets,const char* reason,bool healthy,bool expired=false,bool absentOwner=false){
        if(failed||(earlyOnly&&std::string(id)!="stable-owned-set-and-one-readback"&&std::string(id)!="repeated-existing-admissions-share-one-identity"&&std::string(id)!="same-pid-birth-after-set"&&std::string(id)!="birth-before-owned-set-discovery"&&std::string(id)!="account-before-owned-get-discovery"))return;reset();setup();const auto saved=memory.saved;raw["caseIds"].push_back(id);raw["cases"].push_back({{"id",id},{"passed",false},{"checks",json::array()}});active=&raw["cases"].back();
        try{
            json receipt;bool ownerExpired=false;
            try{receipt=steamMonitorApplyOwned(owned.patch,absentOwner?std::function<bool()>{}:std::function<bool()>{monitorOwnedCurrent});}
            catch(const std::runtime_error&e){if(std::string(e.what())!="BOOT_STEAM_MONITOR_REQUEST_EXPIRED")throw;ownerExpired=true;}
            (*active)["receipt"]=receipt;(*active)["ownerExpired"]=ownerExpired;
            check(ownerExpired==expired,"original-boot-owner-expiry-contract");
            if(!expired){check(receipt.value("applicationConfirmed",false)==healthy&&receipt.value("liveConfirmed",false)==healthy,"exact-owned-live-confirmation");if(reason)check(receipt.value("reason",std::string{})==reason,"exact-refusal-reason");}
            check(memory.setCalls==sets&&memory.liveReads==gets,"one-set-and-bounded-readback-no-replay");
            check(memory.reg==0&&memory.presenceReads==0&&memory.privateConfig==0,"owned-consumer-no-private-pid-registry-or-presence-discovery");
            check(memory.writes==0&&memory.saved==saved,"owned-live-apply-does-not-write-or-commit-pending");
            if(healthy){check(receipt.value("position",-1)==4&&receipt.value("pending",true)==false,"matching-readback-required");check(owned.operations==std::vector<std::string>{"settings.set","settings.get"},"original-set-then-get-order");}
            (*active)["passed"]=true;
        }catch(const std::exception&e){(*active)["error"]=e.what();failed=true;}
        (*active)["calls"]={{"controlledDiscovery",owned.discoveryReads},{"setOwner",owned.setOwner},{"getOwner",owned.getOwner},{"set",memory.setCalls},{"get",memory.liveReads},{"reg",memory.reg},{"presence",memory.presenceReads},{"privateConfig",memory.privateConfig},{"config",memory.configReads},{"gates",owned.gateCalls},{"owner",owned.ownerCalls},{"attempts",owned.attempts}};
    };
    if(earlyControl){
        run("birth-before-owned-set-discovery",[]{owned.beforeSetOwner=[]{++memory.birth;publish();};},0,0,"steam-session-changed",false);
        if(!failed){active=&raw["cases"].back();try{check(owned.discoveryReads==0&&owned.setOwner,"revoked-owned-set-zero-discovery");}catch(const std::exception&e){(*active)["error"]=e.what();(*active)["passed"]=false;failed=true;}}
    }
    else if(repair){
        run("zero-registered-pid-with-verified-process",[]{memory.registered=0;publish();},0,0,"steam-session-starting",false);
        run("registered-pid-read-failure-with-verified-process",[]{memory.registeredError=ERROR_ACCESS_DENIED;publish();},0,0,"steam-session-starting",false);
        run("registered-nonzero-healthy-apply",[]{},1,1,nullptr,true);
        run("registered-nonzero-different-pid-preserves-old-policy",[]{++memory.registered;publish();},1,1,nullptr,true);
    }
    else if(control){run("same-pid-birth-after-set",[]{owned.afterSet=[]{++memory.birth;publish();};},1,0,"steam-session-changed",false);}
    else{
        run("stable-owned-set-and-one-readback",[]{},1,1,nullptr,true);
        run("birth-before-owned-set-discovery",[]{owned.beforeSetOwner=[]{++memory.birth;publish();};},0,0,"steam-session-changed",false);
        if(!failed){active=&raw["cases"].back();try{check(owned.discoveryReads==0&&owned.setOwner,"revoked-owned-set-zero-discovery");}catch(const std::exception&e){(*active)["error"]=e.what();(*active)["passed"]=false;failed=true;}}
        run("account-before-owned-get-discovery",[]{owned.beforeGetOwner=[]{++memory.account;publish();};},1,0,"steam-session-changed",false);
        if(!failed){active=&raw["cases"].back();try{check(owned.discoveryReads==1&&owned.setOwner&&owned.getOwner,"revoked-owned-get-no-second-discovery-or-set-replay");}catch(const std::exception&e){(*active)["error"]=e.what();(*active)["passed"]=false;failed=true;}}
        run("repeated-existing-admissions-share-one-identity",[]{owned.gateRepeats=64;},1,1,nullptr,true);
        run("same-pid-birth-after-set",[]{owned.afterSet=[]{++memory.birth;publish();};},1,0,"steam-session-changed",false);
        run("identity-a-b-a-after-set",[]{owned.afterSet=[]{++memory.account;publish();--memory.account;publish();};},1,0,"steam-session-changed",false);
        run("account-after-set",[]{owned.afterSet=[]{++memory.account;publish();};},1,0,"steam-session-changed",false);
        run("root-after-set",[]{owned.afterSet=[]{memory.root+=L"-other";publish();};},1,0,"steam-session-changed",false);
        run("registered-pid-after-set",[]{owned.afterSet=[]{++memory.registered;publish();};},1,0,"steam-session-changed",false);
        run("same-pid-birth-after-get",[]{owned.afterGet=[]{++memory.birth;publish();};},1,1,"steam-session-changed",false);
        run("revoked-before-set-admission",[]{owned.beforeGate=[]{++memory.birth;publish();};},0,0,"steam-session-changed",false);
        run("revoked-during-config-selection",[]{memory.configHook=[]{++memory.birth;publish();};},0,0,"steam-session-changed",false);
        run("file-target-replaced-after-set",[]{owned.afterSet=[]{memory.configSuffix=L"-replaced";};},1,0,"steam-session-changed",false);
        run("owner-expired-at-entry",[]{owned.current=false;},0,0,nullptr,false,true);
        run("owner-expired-after-lock",[]{owned.expireAtOwnerCall=2;},0,0,nullptr,false,true);
        run("owner-expired-during-config",[]{memory.configHook=[]{owned.current=false;};},0,0,nullptr,false,true);
        run("owner-expired-after-set",[]{owned.afterSet=[]{owned.current=false;};},1,0,nullptr,false,true);
        run("owner-expired-after-get",[]{owned.afterGet=[]{owned.current=false;};},1,1,nullptr,false,true);
        run("absent-owner-cannot-apply",[]{},0,0,nullptr,false,true,true);
        run("offline-never-writes",[]{memory.presence=0;publish();},0,0,"steam-not-running",false);
        run("unknown-not-offline",[]{memory.presence=-1;publish();},0,0,"steam-session-starting",false);
        run("missing-public-receipt",[]{g_steamSettingsObserver.stop();},0,0,"steam-session-starting",false);
        run("zero-birth-rejected",[]{memory.birth=0;publish();},0,0,"steam-session-starting",false);
        run("zero-pid-rejected",[]{memory.pid=0;publish();},0,0,"steam-session-starting",false);
        run("root-failure-rejected",[]{memory.rootError=ERROR_ACCESS_DENIED;publish();},0,0,"localconfig-not-found",false);
        run("missing-localconfig-rejected",[]{memory.configFound=false;},0,0,"localconfig-not-found",false);
        run("set-uncertain-no-get-or-retry",[]{owned.setMode="uncertain";},1,0,"live-transport-uncertain",false);
        run("set-malformed-no-get",[]{owned.setMode="malformed";},1,0,"invalid-steam-monitor-receipt",false);
        run("set-mismatched-values-no-get",[]{owned.setMode="mismatch";},1,0,"invalid-steam-monitor-receipt",false);
        run("set-exception-no-replay",[]{owned.throwSet=true;},1,0,"invalid-steam-monitor-receipt",false);
        run("get-exception-no-replay",[]{owned.throwGet=true;},1,1,"invalid-steam-monitor-receipt",false);
        run("get-malformed-unconfirmed",[]{owned.getMode="malformed";},1,1,"live-readback-failed",false);
        run("get-mismatch-unconfirmed",[]{owned.getMode="mismatch";},1,1,"live-readback-failed",false);
        run("get-invalid-values-unconfirmed",[]{owned.getMode="invalid";},1,1,"live-readback-failed",false);
        run("get-failed-unconfirmed",[]{owned.getMode="failed";},1,1,"live-readback-failed",false);
        run("isolated-no-query",[]{g_aiFanMockSession.isolated=true;},0,0,"isolated-session",false);
        run("invalid-patch-no-query",[]{owned.patch={{"position",7}};},0,0,"invalid-monitor-settings",false);
    }
    g_steamSettingsObserver.stop();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;raw["beforeHandles"]=before;raw["afterHandles"]=after;raw["ok"]=!failed&&before==after;raw["checks"]=checks;
    const auto bytes=raw.dump(2);DWORD written=0;if(!WriteFile(owner.h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(owner.h))return 3;if(!CloseHandle(owner.h))return 3;owner.h=INVALID_HANDLE_VALUE;std::cout<<json{{"ok",raw["ok"]},{"cases",raw["caseIds"].size()},{"checks",checks}}.dump()<<'\n';return raw["ok"]==true?0:1;
}
