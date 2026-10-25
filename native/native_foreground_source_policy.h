#pragma once
#include "msi_native_foreground_isolation.h"
#include <algorithm>
#include <initializer_list>
#include <string>
namespace ymcc::nativeForeground {
enum class Profile { Unsupported, Msi, Rog, Gpd };
inline std::string upper(std::string value) {
    for(auto& character:value)if(character>='a' && character<='z')character=char(character-'a'+'A');
    return value;
}
inline bool modelCharacter(char character) {
    return (character>='A' && character<='Z') || (character>='0' && character<='9') || character=='-' || character=='_';
}
inline bool hasModel(const std::string& value,std::initializer_list<const char*> models) {
    const auto text=upper(value);
    for(const auto* model:models){
        const std::string token=model;
        for(auto offset=text.find(token);offset!=std::string::npos;offset=text.find(token,offset+1)){
            const auto end=offset+token.size();
            if((offset==0 || !modelCharacter(text[offset-1])) && (end==text.size() || !modelCharacter(text[end])))return true;
        }
    }
    return false;
}
inline Profile machineProfile(const std::string& manufacturer,const std::string& board) {
    const auto vendor=upper(manufacturer);
    if(vendor.find("MICRO-STAR")!=std::string::npos && hasModel(board,{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}))return Profile::Msi;
    if(vendor.find("ASUSTEK")!=std::string::npos && hasModel(board,{"RC71L","RC72LA","RC73XA","RC73YA"}))return Profile::Rog;
    if(vendor.find("GPD")!=std::string::npos && hasModel(board,{"WIN2","G1618-03","G1618-04","G1618-05","G1617-01","G1617-02","G1619-03","G1619-04","G1619-05"}))return Profile::Gpd;
    return Profile::Unsupported;
}
inline const char* name(Profile profile) {
    switch(profile){case Profile::Msi:return "msi";case Profile::Rog:return "rog";case Profile::Gpd:return "gpd";default:return "unsupported";}
}
inline bool deviceAdmitted(Profile profile,unsigned vendor,unsigned product) {
    if(profile==Profile::Msi)return vendor==0x0db0 && product==0x1901;
    if(profile==Profile::Rog)return vendor==0x0b05 && (product==0x1abe || product==0x1b4c);
    if(profile==Profile::Gpd)return vendor==0x045e && product==0x028e;
    return false;
}
inline unsigned hexadecimal(const std::string& value,std::size_t offset) {
    unsigned result=0;
    for(std::size_t index=offset;index<offset+4;++index){
        const auto character=value[index];
        if(!((character>='0' && character<='9') || (character>='A' && character<='F')))return 0x10000;
        result=result*16+unsigned(character<='9'?character-'0':character-'A'+10);
    }
    return result;
}
inline bool rootCandidate(Profile profile,const std::string& root) {
    const auto normalized=ymcc::msi::nativeForegroundUpperInstance(root);
    if(normalized.size()<23 || normalized.compare(0,8,"USB\\VID_")!=0 || normalized.compare(12,5,"&PID_")!=0 ||
        normalized[21]!='\\' || normalized.find('\\',22)!=std::string::npos ||
        normalized.find("HIDMAESTRO")!=std::string::npos || normalized.find("VIGEM")!=std::string::npos)return false;
    return deviceAdmitted(profile,hexadecimal(normalized,8),hexadecimal(normalized,17));
}
inline bool gameInputAdmitted(Profile profile,bool sameRoot,bool virtualIdentity,unsigned vendor,unsigned product,unsigned page,unsigned usage) {
    return sameRoot && !virtualIdentity && deviceAdmitted(profile,vendor,product) && page==1 && (usage==4 || usage==5);
}
// Recognition is not permission to stop a devnode. 2026-10-23 ROGX
// morning logs deferred this HID source; admitting refresh in the afternoon
// immediately preceded loss of all XInput. Keep identity for read-only and
// virtual HidHide planning, but quarantine destructive native refresh on this
// product until a hardware-verified return protocol exists. No OEM mode write.
inline bool gameNodeRefreshMutationAllowed(Profile profile,const std::string& root,const std::string& instance) {
    return !(profile==Profile::Rog && upper(root).compare(0,22,"USB\\VID_0B05&PID_1B4C\\")==0 &&
        upper(instance).compare(0,4,"HID\\")==0);
}
inline bool xusbTargetAdmitted(Profile profile,const std::string& root,const ymcc::msi::NativeForegroundXusbInterface& item) {
    if(!item.activeInterface || !item.ancestorMatched || item.virtualDevice || !rootCandidate(profile,root) ||
        !rootCandidate(profile,item.ancestorRoot) || upper(root)!=upper(item.ancestorRoot))return false;
    const auto target=ymcc::msi::nativeForegroundUpperInstance(item.instanceId),source=upper(root);
    const bool busMatches=target.compare(0,4,"USB\\")==0 ||
        (profile==Profile::Rog && target.compare(0,4,"HID\\")==0);
    if(target.size()<23 || !busMatches || target.compare(4,17,source,4,17)!=0 || (target[21]!='&' && target[21]!='\\') ||
        target.find("HIDMAESTRO")!=std::string::npos || target.find("VIGEM")!=std::string::npos)return false;
    const auto separator=target.find('\\',4);
    return separator!=std::string::npos && separator+1<target.size() && target.find('\\',separator+1)==std::string::npos;
}
}
