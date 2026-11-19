using namespace running_trial;
inline bool oldReaderValidRoleGroupShape(const Packet& packet) noexcept {
 const auto& wire=packet.roleGroup;
 if(wire.size!=sizeof wire||wire.version!=1||wire.enabled>1||wire.reserved)return false;
 if(!wire.enabled)return true;
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
 // Root/root never qualifies the new role action, even on a valid chain.
 if(wire.expectedPrimary==result.root.pid&&wire.expectedForeground==result.root.pid)return false;
 return (result.nativePrimaryExact||result.nativeForegroundExact)&&
        (result.state==ymcc::steamgameroles::State::ExactSelected||
         result.state==ymcc::steamgameroles::State::VerifiedGroupRoles);
}
