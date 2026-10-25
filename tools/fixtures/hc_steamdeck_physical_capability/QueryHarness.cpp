#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include "json.hpp"
using json=nlohmann::json;
// @@PRODUCTION@@
int main(){
  g_machineIdentity.family=YmccFamily::ValveSteamDeck;
  g_machineIdentity.deviceClass="HandheldCompanion.Devices.SteamDeck";
  auto deck=actualQuery(json::object());
  const bool directGateSpecial=oemKeyCaptureImplemented(g_machineIdentity.family,g_machineIdentity.deviceClass,"special");
  const bool directGateSpecial2=oemKeyCaptureImplemented(g_machineIdentity.family,g_machineIdentity.deviceClass,"special2");
  auto deckLog=loggedKeys;
  diagnosticLogEnabled=false;
  auto deckWithoutLog=actualQuery(json::object());
  diagnosticLogEnabled=true;
  g_machineIdentity.family=YmccFamily::AsusRogAlly;
  g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";
  auto legacy=actualQuery(json::object());
  std::cout<<json{{"deck",deck},{"directGateSpecial",directGateSpecial},{"directGateSpecial2",directGateSpecial2},{"deckWithoutLog",deckWithoutLog},{"loggedKeys",deckLog},{"legacyControl",legacy}}.dump()<<std::endl;
}

