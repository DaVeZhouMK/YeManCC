#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
static void pump() {
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
}
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
