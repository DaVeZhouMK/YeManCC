#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using json=nlohmann::json;
#ifdef YMCC_MENU_POPUP_CONTINUATION_NATIVE_TAIL
static constexpr bool menuNativeOriginalTail=true;
#else
static constexpr bool menuNativeOriginalTail=false;
#endif
struct MenuNativeState {
    bool power=true,process=true,session=true,host=true,foreground=true,firstComplete=false;
    bool throwCurrent=false,throwSecond=false;std::uint64_t tick=100;
    unsigned firstCalls=0,keyCount=0,secondCalls=0,secondDispatched=0;
    json first,second,lastSecond;
    std::function<void(MenuNativeState&)> afterFirst,beforeSecond,afterSecond,onCurrent;
};
static MenuNativeState* menuNativeState=nullptr;
static ULONGLONG menuNativeTick(){return menuNativeState->tick;}
static json steamLiveRequest(DWORD,const json& request,const std::function<bool()>& admitted,
    const std::function<bool()>& ownerCurrent={}){
    auto& s=*menuNativeState;
    if(menuNativeOriginalTail&&!ownerCurrent)throw std::runtime_error("pure-feature-owner-not-forwarded");
    const auto current=[&]{return !ownerCurrent||ownerCurrent();};
    if(request.value("operation",json())==json("menu.button")){
        ++s.firstCalls;if(!current()||!admitted()||!current())return {{"ok",false},{"mutated",false},{"requestDispatched",false}};
        ++s.keyCount;s.firstComplete=true;if(s.afterFirst)s.afterFirst(s);return s.first;
    }
    ++s.secondCalls;s.lastSecond=request;if(s.beforeSecond)s.beforeSecond(s);
    if(!current()||!admitted()||!current())return {{"ok",false},{"mutated",false},{"requestDispatched",false}};
    ++s.secondDispatched;if(s.throwSecond)throw std::runtime_error("controlled-send-exception");
    if(s.afterSecond)s.afterSecond(s);return s.second;
}
struct MenuNativeLease {bool current()const{return menuNativeState->host;}};
struct MenuNativeHost {std::shared_ptr<MenuNativeLease> hostLease=std::make_shared<MenuNativeLease>();};
#define GetTickCount64 menuNativeTick
static json menuNativeExercise(json request,MenuNativeState& state){
    menuNativeState=&state;DWORD account=42;std::optional<MenuNativeHost> host{MenuNativeHost{}};
    const auto current=[&]{if(state.firstComplete&&state.onCurrent){const auto callback=std::move(state.onCurrent);state.onCurrent={};callback(state);}if(state.firstComplete&&state.throwCurrent)throw std::runtime_error("controlled-current-exception");return state.power&&state.process&&state.session&&state.foreground;};
    const auto cancelled=[] {return json{{"ok",false},{"reason","controlled-native-cancelled"},{"mutated",false}};};
    const auto finishWithContext=[](json value){return value;};
#ifdef YMCC_MENU_POPUP_CONTINUATION_NATIVE_TAIL
#ifdef YMCC_MENU_POPUP_CONTINUATION_BEFORE
#include "before-tail.inc"
#else
#include "candidate-tail.inc"
#endif
#else
    const auto admitted=[&]{return current()&&host->hostLease->current();};
    const auto started=menuNativeTick();auto initial=steamLiveRequest(account,request,admitted);
    return ymcc::menupopup::continueAfterButton(request,std::move(initial),started,admitted,
        [&](const json& continuation,const std::function<bool()>& gate){return steamLiveRequest(account,continuation,gate);},
        []{return menuNativeTick();});
#endif
}
#undef GetTickCount64
static int menuNativeContinuationFixture(int argc,char**argv){
    const bool selected=argc==5&&std::string(argv[3])=="--case";
    if((argc!=3&&!selected)||!argv[1][0]||std::string(argv[2])!="--menu-popup-continuation")return 2;
    const std::string selectedCase=selected?argv[4]:"";
    if(selected&&(selectedCase!="opened-pending-one-followup"&&selectedCase!="cancelled-at-second-dispatch"))return 2;
    const HANDLE output=CreateFileA(argv[1],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(output==INVALID_HANDLE_VALUE)return 2;
    struct Owner{HANDLE h;~Owner(){if(h!=INVALID_HANDLE_VALUE)::CloseHandle(h);}}owner{output};
    DWORD handlesBefore=0,handlesAfter=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesBefore))return 3;
    const json identity={{"version",1},{"appid",4064044731u},{"gameid64","17454939209159671808"},{"account",42u},
        {"gamePid",500u},{"gameCreated","123456789"},{"steamPid",600u},{"steamCreated","223456789"},
        {"hostPid",700u},{"hostCreated","323456789"},{"browserId",12u},{"registeredAppid",3958459u},{"registeredGameid64","4064044731"}};
    const json request={{"operation","menu.button"},{"menu","quick-access"},{"menuGame",identity}};
    const json pending={{"schema",1},{"scope","held-active-qam-popup-original-activation"},{"status","pending-browser-task"},
        {"callbackSubmitted",false},{"inputCapabilityGranted",false},{"physicalInputProven",false},{"ticket","Qam:1-abc._"}};
    const json completed={{"schema",1},{"scope","held-active-qam-popup-original-activation"},{"status","original-popup-tree-activation-submitted"},
        {"callbackSubmitted",true},{"inputCapabilityGranted",false},{"physicalInputProven",false},{"activationPhase","native-followup"},{"treeId","QuickAccess-NA"}};
    const json original={{"ok",true},{"keyDelivered",true},{"runtimeAccepted",true},{"before",0},{"menu",2},{"mutated",true},
        {"originalMarker","keep-exact"},{"menuTarget",{{"other",17},{"windowFocus",{{"other",19},{"menuTreeActivation",pending}}}}}};
    json raw={{"selection","native-menu-popup-continuation"},{"cases",json::array()},{"caseIds",json::array()},
        {"actualNativeFinalSuffix",menuNativeOriginalTail},{"defaultSuiteExecuted",false},{"realSteamOperations",0},{"systemInputInjected",false},{"physicalInputProven",false}};
    unsigned checks=0;bool failed=false;json* active=nullptr;
    const auto check=[&](bool ok,const char* name){++checks;(*active)["checks"].push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);};
    const auto leaf=[](const json& value)->const json&{return value.at("menuTarget").at("windowFocus").at("menuTreeActivation");};
    const auto prepare=[&]{MenuNativeState s;s.first=original;s.second={{"ok",true},{"mutated",true},{"menuPopupActivation",completed}};return s;};
    const auto run=[&](const char* id,const std::function<void(MenuNativeState&)>& setup,unsigned expectedCalls,unsigned expectedDispatched,const char* expectedStatus){
        if(failed||(!selectedCase.empty()&&selectedCase!=id))return;raw["caseIds"].push_back(id);raw["cases"].push_back({{"id",id},{"checks",json::array()},{"passed",false}});active=&raw["cases"].back();auto state=prepare();setup(state);
        try{
            const auto result=menuNativeExercise(request,state);(*active)["receipt"]=result;
            check(state.firstCalls==1&&state.keyCount==1,"exactly-one-original-button");
            check(state.secondCalls==expectedCalls&&state.secondDispatched==expectedDispatched,"exact-native-followup-counts-no-retry");
            check(leaf(result).value("status",std::string{})==expectedStatus,"exact-activation-result-classification");
            auto expected=state.first;expected["menuTarget"]["windowFocus"]["menuTreeActivation"]=leaf(result);
            check(result==expected,"only-activation-leaf-changes-original-key-and-state-preserved");
            if(expectedCalls){check(state.lastSecond.size()==4&&state.lastSecond.at("operation")=="menu.qam.observe"&&state.lastSecond.at("menu")=="quick-access","second-call-is-observe-only-not-another-button");check(state.lastSecond.at("menuGame")==identity&&state.lastSecond.at("menuGamePopupActivation")==pending.at("ticket"),"exact-original-thirteen-field-identity-and-ticket");}
            if(std::string(expectedStatus)=="followup-unconfirmed")check(leaf(result).at("inputCapabilityGranted")==false&&leaf(result).at("physicalInputProven")==false&&leaf(result).at("callbackSubmitted")== (expectedCalls?json(nullptr):json(false)),"uncertain-followup-never-claims-input-or-callback-ack");
            (*active)["passed"]=true;
        }catch(const std::exception& e){(*active)["error"]=e.what();failed=true;}
        (*active)["firstCalls"]=state.firstCalls;(*active)["keyCount"]=state.keyCount;(*active)["secondCalls"]=state.secondCalls;(*active)["secondDispatched"]=state.secondDispatched;menuNativeState=nullptr;
    };
    run("opened-pending-one-followup",[](auto&){},1,1,"original-popup-tree-activation-submitted");
    run("already-activated-no-followup",[&](auto&s){s.first["menuTarget"]["windowFocus"]["menuTreeActivation"]=completed;},0,0,"original-popup-tree-activation-submitted");
    run("closing-no-followup",[](auto&s){s.first["before"]=2;s.first["menu"]=0;},0,0,"pending-browser-task");
    run("unknown-before-no-followup",[](auto&s){s.first["before"]=-1;},0,0,"pending-browser-task");
    run("failed-first-no-followup",[](auto&s){s.first["ok"]=false;},0,0,"pending-browser-task");
    run("invalid-ticket-no-followup",[](auto&s){s.first["menuTarget"]["windowFocus"]["menuTreeActivation"]["ticket"]="../private";},0,0,"pending-browser-task");
    run("power-revoked-after-first",[](auto&s){s.afterFirst=[](auto&v){v.power=false;};},0,0,"followup-unconfirmed");
    run("process-revoked-after-first",[](auto&s){s.afterFirst=[](auto&v){v.process=false;};},0,0,"followup-unconfirmed");
    run("session-revoked-after-first",[](auto&s){s.afterFirst=[](auto&v){v.session=false;};},0,0,"followup-unconfirmed");
    run("host-revoked-after-first",[](auto&s){s.afterFirst=[](auto&v){v.host=false;};},0,0,"followup-unconfirmed");
    run("foreground-changed-after-first",[](auto&s){s.afterFirst=[](auto&v){v.foreground=false;};},0,0,"followup-unconfirmed");
    run("original-four-seconds-exhausted",[](auto&s){s.afterFirst=[](auto&v){v.tick+=4000;};},0,0,"followup-unconfirmed");
    run("native-clock-backward",[](auto&s){s.afterFirst=[](auto&v){v.tick=99;};},0,0,"followup-unconfirmed");
    run("current-callback-crosses-deadline",[](auto&s){s.afterFirst=[](auto&v){v.tick+=3999;};s.onCurrent=[](auto&v){++v.tick;};},0,0,"followup-unconfirmed");
    run("cancelled-at-second-dispatch",[](auto&s){s.beforeSecond=[](auto&v){v.power=false;};},1,0,"followup-unconfirmed");
    run("owner-ends-after-second",[](auto&s){s.afterSecond=[](auto&v){v.session=false;};},1,1,"followup-unconfirmed");
    run("uncertain-second-not-retried",[](auto&s){s.second={{"ok",false},{"mutated",true},{"requestDispatched",true}};},1,1,"followup-unconfirmed");
    run("malformed-second-leaf",[](auto&s){s.second["menuPopupActivation"]=json::array();},1,1,"followup-unconfirmed");
    run("throwing-second-not-retried",[](auto&s){s.throwSecond=true;},1,1,"followup-unconfirmed");
    run("throwing-current-stops-suffix",[](auto&s){s.afterFirst=[](auto&v){v.throwCurrent=true;};},0,0,"followup-unconfirmed");
    run("second-cannot-grant-physical-input",[](auto&s){s.second["menuPopupActivation"]["physicalInputProven"]=true;},1,1,"followup-unconfirmed");
    run("same-budget-post-reply-expiry",[](auto&s){s.afterSecond=[](auto&v){v.tick+=4000;};},1,1,"followup-unconfirmed");
    if(!GetProcessHandleCount(GetCurrentProcess(),&handlesAfter))return 3;
    raw["selectedCase"]=selectedCase;raw["pureOwnerForwardingRequired"]=menuNativeOriginalTail;
    raw["beforeHandles"]=handlesBefore;raw["afterHandles"]=handlesAfter;raw["checkCount"]=checks;raw["ok"]=!failed&&handlesBefore==handlesAfter;raw["helperProcesses"]=0;
    const auto bytes=raw.dump(2);DWORD written=0;if(!WriteFile(owner.h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(owner.h))return 3;
    if(!CloseHandle(owner.h))return 3;owner.h=INVALID_HANDLE_VALUE;std::cout<<json{{"ok",raw["ok"]},{"cases",raw["caseIds"].size()},{"checks",checks}}.dump()<<'\n';return raw["ok"]==true?0:1;
}
