#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include "screen_special_actions.h"
#include <iostream>
using namespace ymcc::screenpads;
static void pump() {
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
}
static std::vector<StandaloneAction> dispatched;
static bool deferAction=false,acceptAction=true;
static UINT_PTR deferredGeneration=0;
static bool recordAction(StandaloneAction action,UINT_PTR generation) {
    dispatched.push_back(action);deferredGeneration=generation;
    if(acceptAction && !deferAction)standaloneFinish(generation,StandaloneStatus::Accepted);
    return acceptAction;
}
static UINT batchAccepted=4;
static std::vector<std::vector<INPUT>> inputBatches;
static UINT WINAPI recordInput(UINT count,LPINPUT inputs,int size) {
    if(size!=sizeof(INPUT))return 0;
    inputBatches.emplace_back(inputs,inputs+count);
    return inputBatches.size()==1?batchAccepted:count;
}
struct MicrophoneFixture {
    BOOL muted=FALSE;bool rejectRead=false,rejectSet=false,ignoreSet=false;int reads=0,writes=0;
    HRESULT GetMute(BOOL* value){++reads;if(rejectRead)return E_FAIL;*value=muted;return S_OK;}
    HRESULT SetMute(BOOL value,LPCGUID){++writes;if(rejectSet)return E_FAIL;if(!ignoreSet)muted=value;return S_OK;}
};
int main(int argc,char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Json result=selfTest();auto& cases=result["cases"];
    auto check=[&](const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    Config cfg=profileDefault(0);dryRun=true;setContext("disabled",false);
    initialize(GetModuleHandleW(nullptr),nullptr,cfg,0);refresh();pump();
    check("off-default-has-no-special-windows",!buttonsVisible());
    for(auto mode:{StandaloneSpecialMode::SteamDeck,StandaloneSpecialMode::PS5}) {
        const std::string name=standaloneSpecialName(mode);
        cfg.standaloneSpecialMode=mode;configure(cfg,0);pump();
        check(name+"-pair-uses-real-visible-input-and-artwork-windows",buttonWindows[1] && buttonWindows[2] &&
            IsWindowVisible(buttonWindows[1]) && IsWindowVisible(buttonWindows[2]) &&
            IsWindowVisible(buttonOutlines[1]) && IsWindowVisible(buttonOutlines[2]));
        check(name+"-pair-has-white-alpha-artwork",buttonUploads[1].ready && buttonUploads[2].ready &&
            buttonUploads[1].hasIcon && buttonUploads[2].hasIcon && buttonUploads[1].whiteIcon && buttonUploads[2].whiteIcon);
        check(name+"-artwork-resolves-matching-preset",buttonGlyph(standaloneSpecialProfile(cfg),1)==(mode==StandaloneSpecialMode::SteamDeck?glyphs::steam:glyphs::ps) &&
            buttonGlyph(standaloneSpecialProfile(cfg),2)==(mode==StandaloneSpecialMode::SteamDeck?glyphs::dots:glyphs::mute));
        bool noRear=true;for(int i=3;i<7;i++)noRear=noRear && (!buttonWindows[i] || !IsWindowVisible(buttonWindows[i]));
        check(name+"-separate-rear-keys-remain-off",noRear);
        RECT r{};GetWindowRect(buttonWindows[1],&r);buttonEvent(1,100,0,(r.right-r.left)/2.f,(r.bottom-r.top)/2.f,r.right-r.left,r.bottom-r.top);
        check(name+"-disabled-target-remains-disabled-with-no-BUS-report",activeProfile.load()==0 && !controlTargetEnabled.load() && !available.load() && !psAvailable.load() && !snapshot().present && buttonSnapshot()==0);
        check(name+"-readback-preserves-selection",state()["standaloneSpecialMode"]==name && profileState(cfg,0)["standaloneSpecialMode"]==name);
        Config other=profileDefault(3);setContext("elite",true);configure(other,3);pump();
        check(name+"-switch-profile-clears-held-special",screenButtonMask.load()==0);
        setContext("disabled",false);configure(cfg,0);pump();
        cfg.standaloneSpecialMode=StandaloneSpecialMode::Off;configure(cfg,0);pump();
        check(name+"-select-off-hides-both-pairs-and-clears-contact",!buttonsVisible() && screenButtonMask.load()==0 && !buttonContacts[1] && !buttonContacts[2]);
    }
    standaloneSubmit=recordAction;
    auto click=[&](int index,UINT32 id=301) {buttonEvent(index,id,0,20,20,80,60);buttonEvent(index,id,2,20,20,80,60);};
    static_assert(kStandaloneResultMessage!=kCursorSyncMessage);
    for(auto mode:{StandaloneSpecialMode::SteamDeck,StandaloneSpecialMode::PS5}) {
        cfg=profileDefault(0);cfg.standaloneSpecialMode=mode;setContext("disabled",false);configure(cfg,0);pump();
        const std::string name=standaloneSpecialName(mode);
        for(int index:{1,2}) {
            const auto before=dispatched.size();
            buttonEvent(index,300+index,0,20,20,80,60);
            buttonEvent(index,300+index,1,21,21,80,60);
            check(name+"-down-and-move-do-not-trigger-action-"+std::to_string(index),dispatched.size()==before && screenButtonMask.load()==0 && buttonSnapshot()==0);
            buttonEvent(index,300+index,2,20,20,80,60);
            const auto expected=index==1?StandaloneAction::SteamMainMenu:mode==StandaloneSpecialMode::SteamDeck?StandaloneAction::SteamQuickAccess:StandaloneAction::MicrophoneMute;
            check(name+"-release-dispatches-correct-direct-action-"+std::to_string(index),dispatched.size()==before+1 && dispatched.back()==expected && standalonePending.load()==0 && state()["standaloneSpecialStatus"]=="accepted");
            buttonEvent(index,300+index,2,20,20,80,60);
            check(name+"-duplicate-up-does-not-repeat-"+std::to_string(index),dispatched.size()==before+1);
            buttonEvent(index,400+index,0,20,20,80,60);buttonEvent(index,400+index,2,20,20,80,60,true);
            check(name+"-cancel-does-not-dispatch-"+std::to_string(index),dispatched.size()==before+1 && !buttonContacts[index]);
            buttonEvent(index,500+index,0,20,20,80,60);buttonEvent(index,500+index,1,100,100,80,60);buttonEvent(index,500+index,2,100,100,80,60);
            check(name+"-release-outside-does-not-dispatch-"+std::to_string(index),dispatched.size()==before+1);
            buttonEvent(index,600+index,0,20,20,80,60);buttonEvent(index,700+index,2,20,20,80,60);
            check(name+"-other-finger-up-ignored-"+std::to_string(index),dispatched.size()==before+1 && buttonContacts[index]==600+index);
            buttonEvent(index,600+index,2,20,20,80,60);
            check(name+"-correct-finger-up-dispatches-once-"+std::to_string(index),dispatched.size()==before+2);
            buttonEvent(index,800+index,0,20,20,80,60);SendMessageW(buttonWindows[index],WM_CANCELMODE,0,0);buttonEvent(index,800+index,2,20,20,80,60);
            check(name+"-window-cancel-clears-contact-without-action-"+std::to_string(index),dispatched.size()==before+2 && !buttonContacts[index]);
        }
        const auto before=dispatched.size();deferAction=true;click(1);const auto stale=deferredGeneration;
        click(1);click(2);
        check(name+"-pending-request-does-not-queue-toggle-storm",dispatched.size()==before+1 && standalonePending.load()==stale);
        cfg.standaloneSpecialMode=StandaloneSpecialMode::Off;configure(cfg,0);pump();standaloneFinish(stale,StandaloneStatus::Accepted);
        check(name+"-off-invalidates-queued-action-and-late-receipt",!standaloneCurrent(stale) && standalonePending.load()==0 && standaloneStatus.load()==StandaloneStatus::Idle);
        cfg.standaloneSpecialMode=mode;configure(cfg,0);click(1);const auto profileStale=deferredGeneration;
        setContext("steamdeck",true);standaloneFinish(profileStale,StandaloneStatus::Accepted);
        check(name+"-profile-change-blocks-queued-action-before-refresh",!standaloneCurrent(profileStale) && standalonePending.load()==0 && standaloneStatus.load()==StandaloneStatus::Idle);
        setContext("disabled",false);configure(cfg,0);pump();deferAction=false;
        acceptAction=false;click(1);acceptAction=true;
        check(name+"-queue-failure-visible-without-virtual-output",state()["standaloneSpecialStatus"]=="action-queue-unavailable" && state()["ok"]==true && buttonSnapshot()==0 && !snapshot().present);
        click(1);check(name+"-next-success-clears-previous-action-error",standaloneStatus.load()==StandaloneStatus::Accepted);
    }
    for(const char* persona:{"steamdeck","dualsense-edge"}) {
        const int profile=profileIndex(persona);setContext(persona,true);configure(profileDefault(profile),profile);pump();
        for(int index:{1,2}) {
            const auto before=dispatched.size();buttonEvent(index,901,0,20,20,80,60);
            check(std::string(persona)+"-native-held-report-preserved-"+std::to_string(index),buttonSnapshot()==(1u<<(index-1)));
            buttonEvent(index,901,2,20,20,80,60);
            check(std::string(persona)+"-native-release-never-dispatches-standalone-"+std::to_string(index),buttonSnapshot()==0 && dispatched.size()==before);
        }
    }
    for(bool quick:{false,true})for(UINT accepted=0;accepted<=4;accepted++) {
        inputBatches.clear();batchAccepted=accepted;
        const bool ok=ymcc::screenactions::sendMenuBatch(quick,kInjectedTag,recordInput);
        const auto& batch=inputBatches.front();
        bool edges=batch.size()==4 && batch[0].ki.wVk==VK_LCONTROL && batch[1].ki.wVk==(quick?'2':'1') &&
            batch[2].ki.wVk==batch[1].ki.wVk && batch[3].ki.wVk==VK_LCONTROL && !(batch[0].ki.dwFlags&KEYEVENTF_KEYUP) &&
            !(batch[1].ki.dwFlags&KEYEVENTF_KEYUP) && (batch[2].ki.dwFlags&KEYEVENTF_KEYUP) && (batch[3].ki.dwFlags&KEYEVENTF_KEYUP);
        for(const auto& input:batch)edges=edges && input.type==INPUT_KEYBOARD && input.ki.dwExtraInfo==kInjectedTag;
        const bool partial=accepted>0 && accepted<4;
        bool cleanup=inputBatches.size()==(partial?2u:1u);
        if(partial)cleanup=cleanup && inputBatches[1].size()==2 && (inputBatches[1][0].ki.dwFlags&KEYEVENTF_KEYUP) && (inputBatches[1][1].ki.dwFlags&KEYEVENTF_KEYUP);
        check(std::string(quick?"quick":"main")+"-tagged-complete-edges-and-partial-cleanup-"+std::to_string(accepted),edges && cleanup && ok==(accepted==4));
    }
    MicrophoneFixture mic;
    check("microphone-toggle-on-verified-by-readback",ymcc::screenactions::toggleMicrophoneEndpoint(&mic) && mic.muted && mic.writes==1 && mic.reads==2);
    check("microphone-toggle-off-verified-by-readback",ymcc::screenactions::toggleMicrophoneEndpoint(&mic) && !mic.muted && mic.writes==2 && mic.reads==4);
    MicrophoneFixture cancelled;check("microphone-cancel-after-read-never-writes",!ymcc::screenactions::toggleMicrophoneEndpoint(&cancelled,[]{return false;}) && cancelled.reads==1 && cancelled.writes==0);
    MicrophoneFixture noRead;noRead.rejectRead=true;check("microphone-read-failure-never-writes",!ymcc::screenactions::toggleMicrophoneEndpoint(&noRead) && noRead.writes==0);
    MicrophoneFixture noSet;noSet.rejectSet=true;check("microphone-write-failure-not-success",!ymcc::screenactions::toggleMicrophoneEndpoint(&noSet) && noSet.writes==1);
    MicrophoneFixture clamp;clamp.ignoreSet=true;check("microphone-rejected-value-not-success",!ymcc::screenactions::toggleMicrophoneEndpoint(&clamp) && clamp.writes==1);
    check("missing-microphone-not-success",!ymcc::screenactions::toggleMicrophoneEndpoint<MicrophoneFixture>(nullptr));
    standaloneSubmit=nullptr;buttonsRelease();
    check("standalone-dispatch-receipts-without-virtual-target",standaloneRequests.load()>0 && standaloneCompleted.load()>0 && injectedCount==0);
    check("visual-fixture-injects-no-system-input",dryRun && injectedCount==0 && inputError==0 && windowError==0);
    shutdown();pump();
    const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    check("fixture-releases-all-GDI-resources",gdiAfter<=gdiBefore+2);
    bool ok=true;for(const auto& c:cases){ok=ok && c["ok"].get<bool>();if(!c["ok"].get<bool>())std::cout<<c["name"]<<"\n";}
    result["ok"]=ok;result["virtualTargetStarted"]=false;result["systemInputInjected"]=false;
    result["realSteamOrPSConsumptionVerified"]=false;result["gdiBefore"]=gdiBefore;result["gdiAfter"]=gdiAfter;
    const char* path=argc>1?argv[1]:"standalone-special-selftest.json";
    std::ofstream out(path);out<<result.dump(2);std::cout<<(ok?"PASS ":"FAIL ")<<cases.size()<<" standalone/core cases\n";return ok?0:1;
}
