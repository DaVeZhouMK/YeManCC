#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <iostream>
#include <fstream>
#include <optional>
#include "json.hpp"
#include "steam_monitor_settings.h"
#include "steam_session_observer.fixture.h"
using json=nlohmann::json;
static ymcc::steamsession::Observer g_steamSettingsObserver;
static struct{bool isolated=false;}g_aiFanMockSession;
struct MonitorReadState {
    std::wstring root=L"Q:\\Steam";DWORD pid=900,account=42,registered=900;ULONGLONG birth=100;
    int presence=1;LSTATUS rootError=0,accountError=0,registeredError=0;unsigned reg=0,presenceReads=0,privateConfig=0,configReads=0,liveReads=0,setCalls=0,writes=0,refreshes=0,discoveryReads=0;
    json saved,reply;std::function<void()> liveHook,configHook,beforeOwnerHook;bool earlyOwnerProvided=false;bool hookAtSecondConfig=false,throwLive=false;
    std::wstring configSuffix;bool configFound=true;
};
static MonitorReadState memory;
static void publish(){
    ymcc::steamsession::IdentitySnapshot f;f.session={memory.root,memory.pid,memory.account,memory.birth};f.registryRoot=memory.root;
    f.presence=memory.presence;f.registeredPid=memory.registered;f.rootReadError=memory.rootError;f.accountReadError=memory.accountError;f.registeredPidReadError=memory.registeredError;f.windowsSessionKnown=true;f.windowsSession=7;f.windowsSessionError=0;
    ymcc::steamsession::IdentityPublicationFixture::publish(g_steamSettingsObserver,f);
}
static json steamMonitorStateLocked(){return memory.saved;}
static bool sofSteamRunning(){++memory.presenceReads;return memory.presence!=0;}
static bool sofFindLocalConfigAt(const std::wstring& root,DWORD candidate,std::wstring* path,std::string* account){
    ++memory.configReads;const auto selected=candidate?candidate:42u;*path=root+L"\\userdata\\"+std::to_wstring(selected)+L"\\config\\localconfig.vdf";*account=std::to_string(selected);*path+=memory.configSuffix;
    if(memory.configHook&&(!memory.hookAtSecondConfig||memory.configReads==2)){const auto callback=std::move(memory.configHook);memory.configHook={};callback();}
    return memory.configFound;
}
static bool sofFindLocalConfig(std::wstring* path,std::string* account){++memory.privateConfig;return sofFindLocalConfigAt(memory.root,memory.account,path,account);}
static bool sofRegDWORD(HKEY,const wchar_t*,const wchar_t*,DWORD* value){++memory.reg;*value=memory.registered;return true;}
#if defined(YMCC_STEAM_MONITOR_OWNED_APPLY_ONLY)
#include "steam_monitor_owned_apply_cases.inc"
#endif
static json steamLiveRequest(DWORD user,const json& request,const std::function<bool()>& gate={},const std::function<bool()>& currentOwner={}){
#if defined(YMCC_STEAM_MONITOR_OWNED_APPLY_ONLY)
    return monitorOwnedTransport(user,request,gate,currentOwner);
#else
    memory.earlyOwnerProvided=bool(currentOwner);
    if(memory.beforeOwnerHook){const auto callback=std::move(memory.beforeOwnerHook);memory.beforeOwnerHook={};callback();}
    if(currentOwner&&!currentOwner())return {{"ok",false},{"reason","live-action-cancelled"},{"mutated",false}};
    ++memory.discoveryReads; // Controlled stand-in for discovery after the original early owner gate.
    if(gate&&!gate())return {{"ok",false},{"reason","live-action-cancelled"},{"mutated",false}};
    if(request.value("operation",std::string{})!="settings.get"){++memory.setCalls;throw std::runtime_error("fixture-unexpected-set");}
    ++memory.liveReads;if(memory.liveHook){const auto callback=std::move(memory.liveHook);memory.liveHook={};callback();}
    if(memory.throwLive)throw std::runtime_error("fixture-transport-unavailable");return memory.reply;
#endif
}
#ifdef YMCC_MONITOR_LIVE_READ_BEFORE
#include "before-read.inc"
#else
#include "candidate-read.inc"
#endif
#if !defined(YMCC_STEAM_MONITOR_OWNED_APPLY_ONLY)
static int monitorLiveReadFixture(int argc,char**argv){
    const bool earlyControl=argc==4&&std::string(argv[3])=="--early-owner-control";
    const bool control=argc==4&&std::string(argv[3])=="--birth-reuse-control";
    if((argc!=3&&!control&&!earlyControl)||!argv[1][0]||std::string(argv[2])!="--monitor-live-shared")return 2;
    const HANDLE out=CreateFileA(argv[1],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE)return 2;
    struct Owner{HANDLE h;~Owner(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}}owner{out};DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
    json raw={{"selection","monitor-live-shared"},{"defaultSuiteExecuted",false},{"actualMonitorGetFunction",true},{"actualObserverPublication",true},{"controlledConfigAndTransportLeaves",true},{"cases",json::array()},{"caseIds",json::array()},{"realSteamOperations",0}};
    json* active=nullptr;unsigned checks=0;bool failed=false;
    const auto check=[&](bool ok,const char* label){++checks;(*active)["checks"].push_back({{"name",label},{"passed",ok}});if(!ok)throw std::runtime_error(label);};
    const auto reset=[&]{g_steamSettingsObserver.stop();memory={};g_aiFanMockSession.isolated=false;memory.saved={{"ok",true},{"available",true},{"via","file"},{"pending",true},{"applicationConfirmed",false},{"position",3},{"reason",""}};json values=json::object();for(const auto& f:ymcc::steamsettings::fields)values[f.client]=f.integer?json(static_cast<int>(f.fallback)):json(f.fallback);values["overlay_fps_counter_corner"]=6;memory.reply={{"ok",true},{"mutated",false},{"via","live"},{"values",values}};publish();};
    const auto run=[&](const char* id,const std::function<void()>& setup,unsigned expectedLive,const char* reason,bool healthy){
        if(failed)return;reset();setup();const auto original=memory.saved;raw["caseIds"].push_back(id);raw["cases"].push_back({{"id",id},{"checks",json::array()},{"passed",false}});active=&raw["cases"].back();
        try{const auto receipt=steamMonitorGetLocked();(*active)["receipt"]=receipt;
            check(receipt.value("liveConfirmed",false)==healthy,"exact-live-confirmation");
            if(reason)check(receipt.value("reason",std::string{})==reason,"exact-original-refusal-classification");
            check(memory.liveReads==expectedLive&&memory.setCalls==0&&memory.writes==0,"one-get-or-zero-no-set-write-retry");
            if(memory.beforeOwnerHook==nullptr&&memory.discoveryReads>0)check(memory.earlyOwnerProvided,"pure-outer-owner-forwarded-before-discovery");
            if(std::string(id).find("before-live-discovery")!=std::string::npos)check(memory.discoveryReads==0&&memory.earlyOwnerProvided,"revoked-outer-owner-zero-controlled-discovery");
            check(memory.reg==0&&memory.presenceReads==0&&memory.privateConfig==0,"shared-reader-performs-no-private-registry-or-presence-discovery");
            check(memory.saved==original,"get-preserves-saved-pending-intent");
            if(healthy)check(receipt.at("position")==6&&receipt.at("pending")==true&&receipt.at("applicationConfirmed")==false,"current-live-values-do-not-claim-pending-set-success");
            (*active)["passed"]=true;
        }catch(const std::exception&e){(*active)["error"]=e.what();failed=true;}
        (*active)["calls"]={{"live",memory.liveReads},{"reg",memory.reg},{"presence",memory.presenceReads},{"privateConfig",memory.privateConfig},{"localConfig",memory.configReads},{"controlledDiscovery",memory.discoveryReads},{"earlyOwnerProvided",memory.earlyOwnerProvided}};
    };
    if(earlyControl){run("same-pid-birth-replaced-before-live-discovery",[]{memory.beforeOwnerHook=[]{++memory.birth;publish();};},0,"steam-session-changed",false);}
    else if(control){run("same-pid-birth-replaced-during-get",[]{memory.liveHook=[]{++memory.birth;publish();};},1,"steam-session-changed",false);}
    else{
        run("stable-one-shared-live-get",[]{},1,nullptr,true);
        run("same-pid-birth-replaced-before-live-discovery",[]{memory.beforeOwnerHook=[]{++memory.birth;publish();};},0,"steam-session-changed",false);
        run("account-replaced-before-live-discovery",[]{memory.beforeOwnerHook=[]{memory.account=43;publish();};},0,"steam-account-changed",false);
        run("identity-a-b-a-before-live-discovery",[]{memory.beforeOwnerHook=[]{const auto original=memory.birth;++memory.birth;publish();memory.birth=original;publish();};},0,"steam-session-changed",false);
        run("same-pid-birth-replaced-during-get",[]{memory.liveHook=[]{++memory.birth;publish();};},1,"steam-session-changed",false);
        run("account-replaced-during-get",[]{memory.liveHook=[]{memory.account=43;publish();};},1,"steam-account-changed",false);
        run("identity-a-b-a-during-get",[]{memory.liveHook=[]{const auto original=memory.birth;++memory.birth;publish();memory.birth=original;publish();};},1,"steam-session-changed",false);
        run("root-replaced-during-get",[]{memory.liveHook=[]{memory.root=L"R:\\OtherSteam";publish();};},1,"steam-session-changed",false);
        run("registered-pid-binding-changed",[]{memory.liveHook=[]{++memory.registered;publish();};},1,"steam-account-changed",false);
        run("offline-only-file-state",[]{memory.presence=0;publish();},0,nullptr,false);
        run("unknown-presence-is-not-offline",[]{memory.presence=-1;publish();},0,"steam-session-starting",false);
        run("missing-public-receipt",[]{g_steamSettingsObserver.stop();},0,"steam-session-starting",false);
        run("zero-birth-live-rejected",[]{memory.birth=0;publish();},0,"steam-session-starting",false);
        run("zero-pid-live-rejected",[]{memory.pid=0;publish();},0,"steam-session-starting",false);
        run("root-read-failure-live-rejected",[]{memory.rootError=ERROR_ACCESS_DENIED;publish();},0,"localconfig-not-found",false);
        run("revoked-during-config-selection",[]{memory.configHook=[]{++memory.birth;publish();};},0,"steam-session-changed",false);
        run("revoked-during-final-config-selection",[]{memory.hookAtSecondConfig=true;memory.configHook=[]{++memory.birth;publish();};},1,"steam-session-changed",false);
        run("live-uncertain-does-not-retry",[]{memory.reply={{"ok",false},{"reason","live-transport-uncertain"},{"mutated",true}};},1,"live-transport-uncertain",false);
        run("live-values-invalid-remain-unconfirmed",[]{memory.reply["values"]["overlay_fps_counter_corner"]=7;},1,"live-readback-failed",false);
        run("live-envelope-invalid",[]{memory.reply["ok"]="true";},1,"invalid-steam-monitor-receipt",false);
        run("live-exception-no-fallback-authority",[]{memory.throwLive=true;},1,"invalid-steam-monitor-receipt",false);
        run("isolated-no-query",[]{g_aiFanMockSession.isolated=true;memory.saved["ok"]=false;memory.saved["reason"]="isolated-session";},0,"isolated-session",false);
        run("corrupt-saved-state-no-query",[]{memory.saved["ok"]=false;memory.saved["reason"]="invalid-steam-monitor-state";},0,"invalid-steam-monitor-state",false);
    }
    g_steamSettingsObserver.stop();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;raw["beforeHandles"]=before;raw["afterHandles"]=after;raw["ok"]=!failed&&before==after;raw["checks"]=checks;
    const auto bytes=raw.dump(2);DWORD written=0;if(!WriteFile(owner.h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(owner.h))return 3;if(!CloseHandle(owner.h))return 3;owner.h=INVALID_HANDLE_VALUE;std::cout<<json{{"ok",raw["ok"]},{"cases",raw["caseIds"].size()},{"checks",checks}}.dump()<<'\n';return raw["ok"]==true?0:1;
}
#endif // original read-only case body retained
int main(int argc,char**argv){
#if defined(YMCC_STEAM_MONITOR_OWNED_APPLY_ONLY)
    return monitorOwnedApplyFixture(argc,argv);
#else
    return monitorLiveReadFixture(argc,argv);
#endif
}
