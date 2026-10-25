#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>
#include <optional>
#include <type_traits>
#include "one_shot_dispatch.h"
#include "../steam_compatibility_core.h"
namespace running_trial {
// Legacy qualification selects only exact known hash pairs and original typed
// loaded guards. Semi-static qualification uses independent content layouts and
// resolved entries. Packet ABI 3 carries the choice; dispatcher ABI stays 1.
struct NativeProfile {
 const char* clientSha256;
 const char* uiSha256;
 std::uint64_t getterRva;
 std::uint64_t publisherRva;
};
inline std::optional<NativeProfile> profileForHashes(std::string_view client, std::string_view ui) noexcept {
 constexpr NativeProfile profiles[] {
  {"e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e",
   "1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252", 0xcfdc0, 0x4e27e0},
  {"253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc",
   "cd9226db4e6c28942df70c22c0475d91fdd62cba5f100baae2d65af57cabb610", 0xcfdc0, 0x4e27f0}
 };
 for (const auto& profile : profiles)
  if (client == profile.clientSha256 && ui == profile.uiSha256) return profile;
 return std::nullopt;
}
inline std::optional<NativeProfile> profileForCompatibility(ymcc::steamcompat::Layout client,ymcc::steamcompat::Layout ui) noexcept {
 using Layout=ymcc::steamcompat::Layout;
 if(!ymcc::steamcompat::validLayout(client)||!ymcc::steamcompat::validLayout(ui))return std::nullopt;
 return NativeProfile{
  client==Layout::Legacy ? "e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e" : "253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc",
  ui==Layout::Legacy ? "1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252" : "cd9226db4e6c28942df70c22c0475d91fdd62cba5f100baae2d65af57cabb610",
  0xcfdc0, client==Layout::Legacy ? 0x4e27e0ULL : 0x4e27f0ULL};
}
// Packet ABI is independent of one_shot_dispatch.h's unchanged dispatcher ABI.
inline constexpr std::uint32_t abi=3;
enum class QualificationRoute:std::uint32_t { LegacyDynamic=1, SemiStatic=2 };
inline constexpr bool validQualificationRoute(QualificationRoute route) noexcept {
 return route==QualificationRoute::LegacyDynamic||route==QualificationRoute::SemiStatic;
}
inline constexpr const char* qualificationRouteName(QualificationRoute route) noexcept {
 return route==QualificationRoute::LegacyDynamic?"legacy-dynamic":route==QualificationRoute::SemiStatic?"semi-static":"unsupported";
}
// Full type2 GameID only. The high word is the exact nonzero shortcut AppID;
// the low word must contain type 2 and no native-AppID or reserved bits.
inline constexpr std::uint32_t shortcutAppId(std::uint64_t gameId) noexcept {
 return static_cast<std::uint32_t>(gameId)==(2u<<24)?static_cast<std::uint32_t>(gameId>>32):0;
}
inline bool validSteamPath(std::wstring_view path) {
 if(path.size()<12||path.size()>=520||!((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))||path[1]!=L':'||path[2]!=L'\\'||path.find(L'\0')!=path.npos)return false;
 const auto last=path.find_last_of(L'\\');
 const auto leaf=path.substr(last+1);
 if(leaf.size()!=9)return false;
 constexpr std::wstring_view expected=L"steam.exe";
 for(std::size_t i=0;i<leaf.size();++i)if(std::towlower(leaf[i])!=expected[i])return false;
 for(std::size_t begin=3;begin<path.size();){
  const auto end=path.find(L'\\',begin),limit=end==path.npos?path.size():end;
  const auto part=path.substr(begin,limit-begin);
  if(part.empty()||part==L"."||part==L".."||part.find_first_of(L"/:*?\"<>|")!=part.npos)return false;
  if(end==path.npos)break;begin=end+1;
 }
 return true;
}
inline std::wstring steamModulePath(std::wstring_view steamPath,std::wstring_view module) {
 if(!validSteamPath(steamPath)||(module!=L"steamclient64.dll"&&module!=L"SteamUI.dll"))return {};
 std::wstring result(steamPath.substr(0,steamPath.find_last_of(L'\\')+1));result.append(module);return result;
}
enum class Op:std::uint32_t{Prepare=1,Arm,Status,Disarm,Cleanup};
struct Telemetry{volatile LONG stage=0;volatile LONG guardBefore=0,guardAfter=0;volatile LONG publisherCalls=0;volatile LONG publisherReturned=0;volatile LONG callbackResult=0;DWORD sourceThread=0;DWORD flagsBefore=0,flagsAfter=0;DWORD primaryBefore=0,primaryAfter=0;DWORD foregroundBefore=0,foregroundAfter=0;DWORD rejectCode=0;};
struct AuxiliaryIdentity{DWORD pid=0,session=0;std::uint64_t birth=0;wchar_t path[520]{};};
struct SavedHealthyProof{char pinNonce[37]{};char ownerPin[65]{},recordPin[65]{},uiObjectPin[65]{};std::uint32_t healthyFlags=0;};
struct Packet{
 std::uint32_t size=sizeof(Packet),version=abi;Op op=Op::Prepare;std::uint32_t returnCode=0;
 QualificationRoute qualificationRoute=QualificationRoute::SemiStatic;
 osd::Nonce nonce{};std::uint64_t account=0,gameId=0,gameBirth=0,steamBirth=0,threadBirth=0,deadlineTick=0;
 DWORD gamePid=0,steamPid=0,session=0,threadId=0;wchar_t gamePath[520]{},steamPath[520]{};
 std::uint32_t noPublish=0,cleanupProof=0; AuxiliaryIdentity ymcc{},cef{}; SavedHealthyProof healthyProof{};
 osd::PrepareResult prepared{};osd::Status status{};std::uint64_t telemetryAddress=0;Telemetry telemetry{};
};
template<std::size_t N> inline bool terminatedPath(const wchar_t (&path)[N]) {
 return path[0]!=L'\0'&&std::find(std::begin(path),std::end(path),L'\0')!=std::end(path);
}
inline bool validPacketPaths(const Packet& packet) {
 return terminatedPath(packet.steamPath)&&validSteamPath(packet.steamPath)&&terminatedPath(packet.gamePath)&&terminatedPath(packet.ymcc.path)&&terminatedPath(packet.cef.path);
}
static_assert(sizeof(wchar_t)==2&&std::is_standard_layout_v<Packet>);
static_assert(sizeof(QualificationRoute)==4&&offsetof(Packet,qualificationRoute)==16);
static_assert(offsetof(Packet,steamPath)==offsetof(Packet,gamePath)+520*sizeof(wchar_t));
}
