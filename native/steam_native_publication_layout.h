#pragma once
#include "steam_compatibility_core.h"
#include <array>
#include <optional>
#include <string_view>
namespace ymcc::steamNativePublication {
using Layout=ymcc::steamcompat::Layout;
struct NativeProfile {
 const char* clientSha256;
 const char* uiSha256;
 std::uint64_t getterRva;
 std::uint64_t publisherRva;
 std::uint64_t publisherSelfOffset=0x3800;
 std::array<unsigned char,10> publisherEntry{0x48,0x8b,0xc4,0x48,0x81,0xec,0x88,0x01,0x00,0x00};
};
// Calibration labels and legacy independent exact-pair selection. The structural
// route still requires its complete disk/loaded contracts before using this data.
inline constexpr std::array<NativeProfile,3> kProfiles{{
 {"e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e","1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252",0xcfdc0,0x4e27e0},
 {"253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc","cd9226db4e6c28942df70c22c0475d91fdd62cba5f100baae2d65af57cabb610",0xcfdc0,0x4e27f0},
 {"ebf46f84606e4dc49ebd551f6621f7ca5c19927d572a49f0aa4925ce9414096c","5fdf0f7972ed7f6acb3949ec6dcd8a424e607a044a3e992bd3ffe8ebcf4d96f7",0xcfed0,0x4f8240,0x3870,{0x48,0x8b,0xc4,0x48,0x81,0xec,0x78,0x01,0x00,0x00}}
}};
inline std::optional<Layout> knownClientLayout(std::string_view hash) noexcept {
 for(std::size_t i=0;i<kProfiles.size();++i)if(hash==kProfiles[i].clientSha256)return i==0?Layout::Legacy:i==1?Layout::Public:Layout::OwnerScoped;
 return std::nullopt;
}
inline std::optional<Layout> knownUiLayout(std::string_view hash) noexcept {
 for(std::size_t i=0;i<kProfiles.size();++i)if(hash==kProfiles[i].uiSha256)return i==0?Layout::Legacy:i==1?Layout::Public:Layout::OwnerScoped;
 return std::nullopt;
}
inline std::optional<NativeProfile> profileForHashes(std::string_view client,std::string_view ui) noexcept {
 for(const auto& p:kProfiles)if(client==p.clientSha256&&ui==p.uiSha256)return p;
 return std::nullopt;
}
inline std::optional<NativeProfile> profileForCompatibility(Layout client,Layout ui) noexcept {
 if(!ymcc::steamcompat::validLayout(client)||!ymcc::steamcompat::validLayout(ui))return std::nullopt;
 if(client==Layout::OwnerScoped||ui==Layout::OwnerScoped){if(client!=ui)return std::nullopt;return kProfiles[2];}
 auto p=kProfiles[client==Layout::Legacy?0:1];p.uiSha256=kProfiles[ui==Layout::Legacy?0:1].uiSha256;return p;
}
} // namespace ymcc::steamNativePublication
