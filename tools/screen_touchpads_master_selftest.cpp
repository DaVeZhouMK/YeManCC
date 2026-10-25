#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    dryRun=true; // Production state machines only. No initialize(), HWNDs, or system input.
    Json cases=Json::array();bool passed=true;
    auto check=[&](const char* name,bool ok){passed=passed&&ok;cases.push_back({{"name",name},{"ok",ok}});};
    ProfileBank bank;Config parsed;
    check("legacy-profiles-default-master-on",bank.slots[0].masterEnabled&&bank.slots[1].masterEnabled&&bank.slots[2].masterEnabled&&bank.slots[3].masterEnabled);
    const Json legacy={{"profiles",{{"steamdeck",{{"layout","dual"},{"leftMode","wasd"},{"rightMode","mouse"},{"specialMask",2},{"rearMask",5}}}}}};
    const ProfileBank migrated(legacy);
    check("legacy-values-preserved",migrated.slots[1].masterEnabled&&migrated.slots[1].left==Mode::Wasd&&migrated.slots[1].right==Mode::Mouse&&migrated.slots[1].specialMask==2&&migrated.slots[1].rearMask==5);
    Config deck=migrated.slots[1];deck.scale=135;deck.summonPosition=SummonPosition::Left;
    const Json before=configJson(deck);
    check("master-off-strict-parse",parseConfig({{"masterEnabled",false}},deck,parsed)&&!parsed.masterEnabled);
    Json expected=before;expected["masterEnabled"]=false;
    check("off-preserves-all-configured-values",configJson(parsed)==expected);
    for(const auto& invalid:Json::array({1,"false",nullptr,Json::object()}))check("nonboolean-master-rejected",!parseConfig({{"masterEnabled",invalid}},deck,parsed));
    parseConfig({{"masterEnabled",false}},deck,parsed);bank.slots[1]=parsed;
    const ProfileBank reloaded(bank.json());
    check("persona-bank-roundtrip-keeps-independent-master",!reloaded.slots[1].masterEnabled&&reloaded.slots[2].masterEnabled&&reloaded.slots[3].masterEnabled&&configJson(reloaded.slots[1])==configJson(parsed));
    Config restored;parseConfig({{"masterEnabled",true}},reloaded.slots[1],restored);
    check("reenable-restores-identical-settings",configJson(restored)==before);
    Core keyCore;keyCore.config=deck;
    check("keyboard-contact-before-off-is-held",keyCore.event(0,1,0,180,20,200,200,1).keys!=0);
    keyCore.config.masterEnabled=false;
    check("off-does-not-publish-held-keys-or-feedback",keyCore.current().keys==0&&keyCore.current().hapticContacts==0&&keyCore.event(0,2,0,30,30,200,200,2).keys==0);
    Core nativeCore;nativeCore.config=profileDefault(1);nativeCore.event(0,7,0,30,30,200,200,1);
    check("native-contact-before-off-is-present",nativeCore.snapshot(1).left.down);
    nativeCore.config.masterEnabled=false;
    check("off-does-not-publish-native-contacts",!nativeCore.snapshot(2).present);
    activeProfile.store(1);appliedProfile.store(1);controlTargetEnabled.store(true);
    deck.specialMask=3;deck.rearMask=15;
    check("configured-buttons-admitted-before-off",buttonPermitted(deck,0)&&buttonPermitted(deck,1)&&buttonPermitted(deck,3));
    deck.masterEnabled=false;
    bool blocked=true;for(int i=0;i<7;++i)blocked=blocked&&!buttonPermitted(deck,i);
    check("master-blocks-summon-special-and-rear-buttons",blocked);
    buttonEventAtHit(deck,1,3,0,true);
    check("master-blocks-button-input",screenButtonMask.load()==0&&buttonContacts[1]==0);
    Config live=profileDefault(1);live.left=Mode::Wasd;live.right=Mode::Mouse;configure(live,1);
    core.event(0,1,0,190,10,200,200,1);buttonEventAtHit(live,1,4,0,true);
    live.masterEnabled=false;const bool reset=configure(live,1);
    check("configure-off-releases-existing-contact-and-button",reset&&!core.pads[0].down&&screenButtonMask.load()==0&&buttonContacts[1]==0);
    check("configure-off-closes-runtime-admission",!enabled.load()&&!nativeMode.load()&&!getConfig().masterEnabled&&getConfig().enabled);
    check("off-retains-no-autohide-timer",autoHideVisibleMask(live)==0&&autoHide.timer==0);
    live.masterEnabled=true;configure(live,1);
    check("reenable-does-not-replay-held-input",enabled.load()&&!core.pads[0].down&&core.current().keys==0&&buttonSnapshot()==0);
    Config sony=profileDefault(2);sony.masterEnabled=false;configure(sony,2);
    check("off-closes-native-mapping-atomic",!enabled.load()&&!nativeMode.load());
    bool noWindows=true;for(HWND h:windows)noWindows=noWindows&&!h;for(HWND h:buttonWindows)noWindows=noWindows&&!h;
    check("no-real-window-or-owner-started",noWindows&&!initialized);
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);
    out<<Json{{"ok",passed},{"cases",cases},{"systemInputInjected",false},{"hardwareWrites",0},{"windowsCreated",false},{"nativeRuntimeStarted",false}}.dump(2);
    std::cout<<"SCREEN_TOUCHPAD_MASTER_OK="<<passed<<" checks="<<cases.size()<<"\n";
    return passed&&out.good()?0:3;
}
