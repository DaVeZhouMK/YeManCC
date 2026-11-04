#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cstdint>
namespace ymcc::gameApplications {
struct EmulatorProfile {
    std::string id;
    std::string name;
    std::string playnitePlatform;
    std::string platformHint;
    std::string platformLabel;
    int igdbPlatformId = 0;
    int steamArtworkAppId = 0;
    uint32_t colorA = 0;
    uint32_t colorB = 0;
    std::vector<std::string> executableNames;
    std::vector<std::string> imageExtensions;
    std::string playniteStartupArguments;
    bool executablePrefix = false;
    bool directoryTarget = false;
    bool scriptGameImport = false;
};

inline const std::vector<EmulatorProfile>& emulatorProfiles() {
    static const std::vector<EmulatorProfile> profiles = {
        {"yuzu", "yuzu", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0x2B8BD6, 0x151B2B,
         {"yuzu.exe"}, {"nso", "nro", "nca", "xci", "nsp"}, "\"{ImagePath}\""},
        {"eden", "Eden", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0x75C66B, 0x17251E,
         {"eden.exe"}, {"nso", "nro", "nca", "xci", "nsp"}, "-f -g \"{ImagePath}\""},
        {"ryujinx", "Ryujinx", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0xE64A4A, 0x27171B,
         {"ryujinx.exe", "ryujinx.ava.exe"}, {"xci", "nsp"}, "--fullscreen \"{ImagePath}\""},
        {"pcsx2", "PCSX2", "sony_playstation2", "ps2", "PlayStation 2", 8, 0, 0x376EB5, 0x151C31,
         {"pcsx2.exe", "pcsx2x64.exe", "pcsx2-qt.exe", "pcsx2x64-avx2.exe", "pcsx2-qtx64-avx2.exe", "pcsx2-qtx64.exe"},
         {"iso", "bin", "mdf", "nrg", "img", "gz", "cso", "chd", "m3u"},
         "\"{ImagePath}\" --nogui --fullboot --fullscreen"},
        {"ppsspp", "PPSSPP", "sony_psp", "psp", "PlayStation Portable", 38, 0, 0x28A8E0, 0x172331,
         {"ppssppwindows64.exe", "ppssppwindows.exe"}, {"iso", "cso", "pbp", "chd"},
         "\"{ImagePath}\" --pause-menu-exit --fullscreen"},
        {"rpcs3", "RPCS3", "sony_playstation3", "ps3", "PlayStation 3", 9, 0, 0xEFC24C, 0x231C16,
         {"rpcs3"}, {"iso"}, "\"{ImagePath}\"", true, true, true},
        {"retroarch", "RetroArch", "", "any", "Multi-system emulator", 0, 1118310, 0x4A4A4A, 0x111111,
         {"retroarch.exe"}, {"zip", "7z", "iso", "bin", "cue", "chd", "nes", "sfc", "smc", "gba", "gbc", "gb", "n64", "z64"},
         "-L \"{EmulatorDir}\\cores\\{Core}.dll\" \"{ImagePath}\""}
    };
    return profiles;
}


inline std::wstring base(std::wstring image){
 const auto slash=image.find_last_of(L"\\/");if(slash!=std::wstring::npos)image.erase(0,slash+1);
 std::transform(image.begin(),image.end(),image.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
 if(image.ends_with(L".exe"))image.resize(image.size()-4);return image;
}
inline bool patternMatches(std::wstring_view value,std::wstring_view pattern){
 std::size_t v=0,p=0,star=std::wstring::npos,retry=0;
 while(v<value.size()){
  if(p<pattern.size()&&(pattern[p]==L'?'||pattern[p]==value[v])){++v;++p;}
  else if(p<pattern.size()&&pattern[p]==L'*'){star=p++;retry=v;}
  else if(star!=std::wstring::npos){p=star+1;v=++retry;}else return false;
 }
 while(p<pattern.size()&&pattern[p]==L'*')++p;return p==pattern.size();
}
inline const std::vector<std::wstring>& gameLibraries(){
 static const std::vector<std::wstring> rules={
#include "game_library_blacklist_entries.h"
 };return rules;
}
inline const std::vector<std::wstring>& emulators(){
 static const auto rules=[](){std::vector<std::wstring> result;
  for(const auto& profile:emulatorProfiles())for(const auto& exe:profile.executableNames){
   auto rule=base(std::wstring(exe.begin(),exe.end()));if(profile.executablePrefix)rule+=L"*";result.push_back(std::move(rule));
  }
  // These VM/emulation hosts are already excluded by the game detector.
  for(const auto* rule:{L"vmware-vmx",L"virtualboxvm",L"qemu-system-*"})result.emplace_back(rule);
  return result;
 }();return rules;
}
inline bool matches(const std::wstring& image,const std::vector<std::wstring>& rules){
 const auto name=base(image);for(const auto& rule:rules)if(patternMatches(name,rule))return true;return false;
}
enum class Category{Ordinary,Emulator,GameLibrary};
inline Category classify(const std::wstring& image){
 if(matches(image,emulators()))return Category::Emulator;
 if(matches(image,gameLibraries()))return Category::GameLibrary;
 return Category::Ordinary;
}
} // namespace ymcc::gameApplications
