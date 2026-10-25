#include "../native/fan_activity_identity.h"
#include <iostream>
#include <string>
using json = nlohmann::json;
int main() {
 int checks=0,failed=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
 using ymcc::fan_activity::deviceClassFromReply;
 for(const auto* type:{"HandheldCompanion.Devices.MSI.ClawA1M","HandheldCompanion.Devices.MSI.ClawA2VM","HandheldCompanion.Devices.MSI.ClawBZ2EM","HandheldCompanion.Devices.MSI.ClawCG3EM","HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.GPD.GPDWin5"}){
  check(deviceClassFromReply(json{{"deviceClass",type}})==type,"legacy declared deviceClass");
  check(deviceClassFromReply(json{{"state","Ready"},{"factoryType",type}})==type,"actual GET state factoryType");
  check(deviceClassFromReply(json{{"ok",true},{"state",{{"state","Ready"},{"factoryType",type}}}})==type,"actual enable wrapper factoryType");
  check(deviceClassFromReply(json{{"deviceClass",type},{"state",{{"factoryType",type}}}})==type,"matching declarations remain exact");
 }
 for(const auto& input:{json(),json::array(),json::object(),json{{"state","Ready"}},json{{"factoryType","MSI"}},json{{"factoryType",123}},json{{"deviceClass",false}},json{{"deviceClass","HandheldCompanion.Devices.ROGAlly"},{"state",{{"factoryType","HandheldCompanion.Devices.MSI.ClawCG3EM"}}}},json{{"factoryType",std::string(300,'x')}}})
  check(deviceClassFromReply(input).empty(),"unknown/malformed/conflicting device is not accepted");
 check(deviceClassFromReply(json{{"deviceClass",nullptr},{"state",{{"factoryType","HandheldCompanion.Devices.MSI.ClawCG3EM"}}}})=="HandheldCompanion.Devices.MSI.ClawCG3EM","nullable absent declaration permits exact state identity");
 std::cout<<"checks="<<checks<<" failed="<<failed<<" hardwareCalls=0 networkCalls=0\n";return failed?1:0;
}
