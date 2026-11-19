#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>
#include <optional>
#include <type_traits>
#include "one_shot_dispatch.h"
#include "publication_revocation.h"
#include "steam_game_group_publication_revocation.h"
#include "../steam_compatibility_core.h"
#include "../steam_native_publication_layout.h"
#include "../steam_game_role_contract.h"
namespace running_trial {
// One shared publication data source; the dispatcher ABI remains unchanged.
using NativeProfile=ymcc::steamNativePublication::NativeProfile;
using ymcc::steamNativePublication::profileForHashes;
using ymcc::steamNativePublication::profileForCompatibility;
using ymcc::steamNativePublication::knownClientLayout;
using ymcc::steamNativePublication::knownUiLayout;
// Packet ABI is independent of one_shot_dispatch.h's unchanged dispatcher ABI.
// ABI 6 is append-only at the source level, but ABI 5 assets/packets are rejected.
// Host, driver, embedded DLL and resource identities must be rebuilt together.
inline constexpr std::uint32_t abi=6;
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
// This wire describes an already authenticated host lease. It is NOT a log-source
// signature or permission: revisions and fullGameId are consistency fields only.
// The host must retain its authenticated in-process source lease and original
// Current/remote-export revocation gates until the dispatcher/child is returned.
struct RoleGroupWire {
 std::uint32_t size=sizeof(RoleGroupWire),version=1,enabled=0,reserved=0;
 std::uint64_t sourceRevision=0,consoleRevision=0,fullGameId=0;
 DWORD expectedPrimary=0,expectedForeground=0;
 ymcc::steamgameroles::Graph graph{};
 // Parallel to graph.members, including selected. The selected entry MUST be an
 // exact copy of Packet's game identity; it reuses the existing game HANDLE.
 std::array<AuxiliaryIdentity,ymcc::steamgameroles::kMaxMembers> identities{};
 // Receiver-table exports only, appended after every existing ABI 6 field.
 RoleCancellationWire cancellation{};
};
struct Packet{
 std::uint32_t size=sizeof(Packet),version=abi;Op op=Op::Prepare;std::uint32_t returnCode=0;
 QualificationRoute qualificationRoute=QualificationRoute::SemiStatic;
 osd::Nonce nonce{};std::uint64_t account=0,gameId=0,gameBirth=0,steamBirth=0,threadBirth=0,deadlineTick=0;
 DWORD gamePid=0,steamPid=0,session=0,threadId=0;wchar_t gamePath[520]{},steamPath[520]{};
 std::uint32_t noPublish=0,cleanupProof=0; AuxiliaryIdentity ymcc{},cef{}; SavedHealthyProof healthyProof{};
 PublicationRevocationWire revocation{};
 osd::PrepareResult prepared{};osd::Status status{};std::uint64_t telemetryAddress=0;Telemetry telemetry{};
 RoleGroupWire roleGroup{}; // ABI 6 tail; all preceding field meanings are retained.
};
template<std::size_t N> inline bool terminatedPath(const wchar_t (&path)[N]) {
 return path[0]!=L'\0'&&std::find(std::begin(path),std::end(path),L'\0')!=std::end(path);
}
inline bool validPacketPaths(const Packet& packet) {
 return terminatedPath(packet.steamPath)&&validSteamPath(packet.steamPath)&&terminatedPath(packet.gamePath)&&terminatedPath(packet.ymcc.path)&&terminatedPath(packet.cef.path);
}
// Structural/topology checks only. Actual process objects and parent facts are
// revalidated by the DLL; no caller-provided "verified" flag is consumed here.
inline bool emptyRoleGroupTopology(const RoleGroupWire& wire) noexcept {
 if(wire.sourceRevision||wire.consoleRevision||wire.fullGameId||wire.expectedPrimary||
    wire.expectedForeground||wire.graph.count||wire.graph.selectedPid)return false;
 for(const auto& member:wire.graph.members)
  if(member.pid||member.session||member.parentPid||member.birth)return false;
 for(const auto& identity:wire.identities){
  if(identity.pid||identity.session||identity.birth)return false;
  for(const auto ch:identity.path)if(ch)return false;
 }
 return true;
}
inline bool validRoleGroupShape(const Packet& packet) noexcept {
 const auto& wire=packet.roleGroup;
 if(wire.size!=sizeof wire||wire.enabled>1||wire.reserved)return false;
 if(wire.version==2)return !wire.enabled&&emptyRoleGroupTopology(wire)&&
     validOriginalCancellationWire(wire.cancellation);
 const bool launcher=wire.version==3;
 if(wire.version!=1&&!launcher)return false;
 if(!wire.enabled)return !launcher&&emptyRoleCancellationWire(wire.cancellation);
 const auto& graph=wire.graph;
 if(graph.count<2||graph.count>ymcc::steamgameroles::kMaxMembers||
    graph.selectedPid!=packet.gamePid||!wire.sourceRevision||!wire.consoleRevision||
    wire.fullGameId!=packet.gameId||!shortcutAppId(wire.fullGameId))return false;
 for(std::uint32_t i=0;i<graph.count;++i){
  const auto& member=graph.members[i];const auto& identity=wire.identities[i];
  if(member.pid!=identity.pid||member.birth!=identity.birth||member.session!=identity.session||
     identity.session!=packet.session||!terminatedPath(identity.path)||
     member.pid==packet.steamPid||member.pid==packet.ymcc.pid||member.pid==packet.cef.pid)return false;
  if(member.pid==packet.gamePid&&
     (identity.birth!=packet.gameBirth||!terminatedPath(packet.gamePath)||
      std::wstring_view(identity.path)!=std::wstring_view(packet.gamePath)))return false;
 }
 const auto result=ymcc::steamgameroles::inspect(graph,wire.expectedPrimary,wire.expectedForeground);
 // Version 3 names a distinct launcher-record role shape. It is not an
 // attestation: the receiver still reads the real native record and held objects.
 if(launcher)return ymcc::steamgameroles::directLauncherTuple(graph,wire.expectedPrimary,wire.expectedForeground);
 // Version 1 retains its original direct-selected role requirement.
 if(wire.expectedPrimary==result.root.pid&&wire.expectedForeground==result.root.pid)return false;
 return (result.nativePrimaryExact||result.nativeForegroundExact)&&
        (result.state==ymcc::steamgameroles::State::ExactSelected||
         result.state==ymcc::steamgameroles::State::VerifiedGroupRoles);
}
// Group v1/v3 shape is usable before host commit; receiver cancellation is
// still mandatory for both. Original-only v2 is always complete.
// Only full validation admits a receiver;
// the host fills these transport slots after its original durable commit.
inline bool validRoleGroupWire(const Packet& packet) noexcept {
 return validRoleGroupShape(packet)&&
        (!packet.roleGroup.enabled||validRoleCancellationWire(packet.roleGroup.cancellation));
}
// The shared Graph is a native C++ structure, not a portable IPC format. ABI 6
// deliberately fixes its Windows x64 layout; do not serialize it with packing.
static_assert(sizeof(void*)==8&&sizeof(AuxiliaryIdentity)==1056);
static_assert(sizeof(ymcc::steamgameroles::MemberIdentity)==24&&
              offsetof(ymcc::steamgameroles::MemberIdentity,birth)==16&&
              sizeof(ymcc::steamgameroles::Graph)==776);
static_assert(std::is_standard_layout_v<RoleGroupWire>&&std::is_trivially_copyable_v<RoleGroupWire>);
static_assert(offsetof(RoleGroupWire,graph)==48&&offsetof(RoleGroupWire,identities)==824&&
              offsetof(RoleGroupWire,cancellation)==34616&&sizeof(RoleGroupWire)==34648);
static_assert(offsetof(Packet,roleGroup)==offsetof(Packet,telemetry)+sizeof(Telemetry));
static_assert(sizeof(wchar_t)==2&&std::is_standard_layout_v<Packet>);
static_assert(sizeof(QualificationRoute)==4&&offsetof(Packet,qualificationRoute)==16);
static_assert(offsetof(Packet,steamPath)==offsetof(Packet,gamePath)+520*sizeof(wchar_t));
}
