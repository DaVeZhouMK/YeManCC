#pragma once
#include <array>
#include <cstdint>
namespace ymcc::msi::rearProtocol {
// Historical names select GetM12 payloads. They do NOT denote disjoint
// hardware memory banks: modern XInput flag data overlaps DInput Index/Flag.
enum class Route { XInputKeyboard, DInputButtons };
// Exact supported firmware table, HC 06c0b954 ClawA1M deviceVersions.
// CG3EM/A2VM/BZ2EM inherit it. A family label does not authorize an unknown FW.
inline bool knownFirmware(uint16_t fw) {
    switch(fw){case 0x163:case 0x166:case 0x167:case 0x211:case 0x217:case 0x219:case 0x308:case 0x411:return true;default:return false;}
}
struct Reports {
    bool valid=false;
    Route route=Route::XInputKeyboard;
    std::array<uint8_t,64> m1{},m2{},sync{};
};
inline Reports reports(uint16_t fw,Route route) {
    Reports r;r.route=route;
    if(!knownFirmware(fw) || (route!=Route::XInputKeyboard && route!=Route::DInputButtons))return r;
    const bool keyboard=route==Route::XInputKeyboard,legacy=fw==0x163||fw==0x211;
    const auto make=[&](bool m1){
        std::array<uint8_t,64> p{};
        p[0]=0x0f;p[3]=0x3c;p[4]=0x21;p[5]=0x01;p[6]=m1?0:1;
        p[7]=uint8_t((m1?(legacy?0x7a:0xba):(legacy?0x1f:0x63))+(keyboard?1:0));
        if(keyboard){p[8]=0x07;p[9]=0x04;p[10]=0;p[11]=m1?0x7a:0x7d;p[12]=p[13]=p[14]=p[15]=0xff;}
        else {p[8]=0x02;p[9]=0x01;p[10]=0;}
        return p;
    };
    r.m1=make(true);r.m2=make(false);r.sync[0]=0x0f;r.sync[3]=0x3c;r.sync[4]=0x22;r.valid=true;return r;
}
// Action de-duplication and OS keyboard suppression are different contracts.
// Only an identified MSI profile may retain configured keyboard suppression
// while WMI owns action delivery. Other profiles preserve the previous rule.
inline uint64_t keyboardConsumeMask(uint64_t mask,bool knownMsiProfile,bool wmiAuthority,uint64_t frontBits) {
    return wmiAuthority&&!knownMsiProfile?mask&~frontBits:mask;
}
struct NativeGateFacts {
    bool knownBoard=false,inputEnabled=false,powerReady=false,exitRequested=false;
    bool virtualTargetEnabled=false,managedDinputArmed=false,returnOwed=false;
    uint64_t requestPower=0,currentPower=0;
    bool rearMappingRequested=false; // configured rear rule or explicit recording
};
inline bool nativePreparationAllowed(const NativeGateFacts& f) {
    return f.knownBoard&&f.rearMappingRequested&&f.inputEnabled&&f.powerReady&&!f.exitRequested&&
        !f.virtualTargetEnabled&&!f.managedDinputArmed&&!f.returnOwed&&
        f.requestPower!=0&&f.requestPower==f.currentPower;
}
struct NativePreparation {
    bool enabled=false,requested=false;
    bool shouldQueue(bool wanted){
        if(!wanted){enabled=false;requested=false;return false;}
        if(!enabled){enabled=true;requested=false;}
        return !requested;
    }
    void queueAccepted(bool accepted){if(accepted)requested=true;}
};
} // namespace ymcc::msi::rearProtocol
