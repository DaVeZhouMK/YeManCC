#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ymcc::msi {
// Extra syntactic restriction, NOT source-identity proof. Caller still needs
// current PnP resolution, gaming capabilities, unique source and location.
// Mirror the one-shot's full USB device root contract BEFORE any hide write.
inline bool nativeForegroundRootCandidate(const std::string& root) {
    constexpr char prefix[]="USB\\VID_0DB0&PID_1901\\";
    constexpr std::size_t prefixLen=sizeof(prefix)-1;
    if(root.size()<=prefixLen || root.size()>=512)return false;
    std::string upper;upper.reserve(root.size());
    for(unsigned char c:root){
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
             c=='\\'||c=='&'||c=='_'||c=='-'||c=='{'||c=='}'))return false;
        upper.push_back(c>='a'&&c<='z'?char(c-'a'+'A'):char(c));
    }
    return upper.compare(0,prefixLen,prefix)==0 && upper.find('\\',prefixLen)==std::string::npos &&
        upper.find("&MI_")==std::string::npos && upper.find("VIGEM")==std::string::npos &&
        upper.find("HIDMAESTRO")==std::string::npos;
}


// XUSB is a separate GAME interface, not an IG_ name heuristic or a general
// descendant permission. The runtime must enumerate an active XUSB interface
// and prove its actual devnode ancestry reaches the pinned physical USB root.
struct NativeForegroundXusbInterface {
    bool activeInterface=false,ancestorMatched=false,virtualDevice=false;
    std::string instanceId,ancestorRoot;
};
struct NativeForegroundXusbInventory {
    bool complete=false;
    std::string reason;
    std::vector<NativeForegroundXusbInterface> interfaces;
};

inline std::string nativeForegroundUpperInstance(const std::string& text) {
    if(text.empty() || text.size()>=512)return {};
    std::string upper;upper.reserve(text.size());
    for(unsigned char c:text){
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
             c=='\\'||c=='&'||c=='_'||c=='-'||c=='{'||c=='}'))return {};
        upper.push_back(c>='a'&&c<='z'?char(c-'a'+'A'):char(c));
    }
    return upper;
}
inline bool nativeForegroundXusbTargetAdmitted(const std::string& sourceRoot,
        const NativeForegroundXusbInterface& item) {
    if(!item.activeInterface || !item.ancestorMatched || item.virtualDevice ||
       !nativeForegroundRootCandidate(sourceRoot) || !nativeForegroundRootCandidate(item.ancestorRoot))return false;
    const auto root=nativeForegroundUpperInstance(sourceRoot),parent=nativeForegroundUpperInstance(item.ancestorRoot);
    if(root.empty() || root!=parent)return false;
    const auto id=nativeForegroundUpperInstance(item.instanceId);
    constexpr char prefix[]="USB\\VID_0DB0&PID_1901";
    constexpr auto prefixLen=sizeof(prefix)-1;
    if(id.size()<=prefixLen || id.compare(0,prefixLen,prefix)!=0 ||
       (id[prefixLen]!='&' && id[prefixLen]!='\\'))return false;
    const auto separator=id.find('\\',4);
    return separator!=std::string::npos && separator+1<id.size() &&
        id.find('\\',separator+1)==std::string::npos &&
        id.find("VIGEM")==std::string::npos && id.find("HIDMAESTRO")==std::string::npos;
}

// Native-only UI isolation has no virtual target, mode write or Host epoch.
// It borrows visibility ONLY for the existing foreground boundary and must
// give it back at every focus/power/config transition, including failed hide.
struct NativeForegroundFacts {
    bool knownBoard=false,nativeChoice=false,virtualEnabled=false,hostAbsent=false;
    bool powerReady=false,uiOwned=false,exitFence=false,borrowBusy=false;
    bool currentUniqueSource=false,gameInput=false,virtualSource=false;
    bool readerExempt=false,inverseOff=false;
    uint64_t power=0,currentPower=0,edge=0,currentEdge=0;
};
inline constexpr bool kNativeForegroundHidHideBorrowEnabled = true;
inline bool nativeOffShouldSkipResidentCloak(bool knownMsiBoard, bool virtualEnabled, bool nativeChoice) noexcept {
    return knownMsiBoard && !virtualEnabled && nativeChoice;
}
inline bool nativeForegroundMayEnter(const NativeForegroundFacts& f) {
    return kNativeForegroundHidHideBorrowEnabled && f.knownBoard && f.nativeChoice &&
        !f.virtualEnabled && f.hostAbsent && f.powerReady && f.uiOwned &&
        !f.exitFence && !f.borrowBusy && f.power && f.edge &&
        f.power==f.currentPower && f.edge==f.currentEdge;
}
inline bool nativeForegroundMayHide(const NativeForegroundFacts& f) {
    return nativeForegroundMayEnter(f) && f.currentUniqueSource && f.gameInput &&
        !f.virtualSource && f.readerExempt && f.inverseOff;
}
}
