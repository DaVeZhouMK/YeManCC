#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
#include "native_foreground_source_policy.h"
#include "controller_persona.h"
using json=nlohmann::json;
@@PRODUCTION_BLOCKS@@
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const auto catalog=json::parse(std::ifstream(argv[1]));
    unsigned checks=0;json failures=json::array(),rows=json::array();
    const auto check=[&](bool condition,const std::string& label){++checks;if(!condition)failures.push_back(label);};
    for(const auto& model:catalog){
        const std::string name=model.at("name");
        unsigned modelChecks=checks;
        for(const auto* persona:{"steamdeck","dualsense-edge","elite"}){
            check(ymcc::controllerPersona::currentOutput(persona),name+" current persona "+persona);
            const auto neutral=focusIsolationNeutralContent();
            const auto frame=focusIsolationAssembleFrame(neutral);
            for(const auto* field:{"buttons","backButtons","lx","ly","rx","ry","lt","rt","ltRaw","rtRaw"})
                check(frame.at(field)==0,name+" "+persona+" neutral "+field);
            check(!frame.at("imu").at("admitted").get<bool>(),name+" motion neutral");
            check(!frame.contains("touchpads") && !frame.contains("screenButtons"),name+" touch and screen neutral");
            for(const auto& field:frame.at("raw").items())check(field.value()==0,name+" raw neutral "+field.key());
        }
        unsigned admittedMachineCases=0;
        for(const auto& identity:model.at("identities")){
            const auto profile=ymcc::nativeForeground::machineProfile(identity.at("manufacturer"),identity.at("product"));
            const std::string family=model.at("family");
            const auto expectedProfile=family=="MSI"?ymcc::nativeForeground::Profile::Msi:family=="ASUS" && name!="ASUS"?ymcc::nativeForeground::Profile::Rog:family=="GPD"?ymcc::nativeForeground::Profile::Gpd:ymcc::nativeForeground::Profile::Unsupported;
            check(profile==expectedProfile,name+" HC factory identity native profile");
            if(profile!=ymcc::nativeForeground::Profile::Unsupported)++admittedMachineCases;
            for(const auto& tuple:model.at("gameCandidates")){
                if(!tuple.contains("vendor") || !tuple.contains("product"))continue;
                const unsigned vendor=tuple.at("vendor"),product=tuple.at("product");
                for(const auto& capability:std::vector<std::pair<unsigned,unsigned>>{{1,4},{1,5},{1,6},{1,2},{12,1},{32,1},{0xff00,1},{0xffa0,1},{0xfff0,64}}){
                    const bool device=(family=="MSI" && vendor==0x0db0 && product==0x1901) ||
                        (family=="ASUS" && vendor==0x0b05 && (product==0x1abe || product==0x1b4c)) ||
                        (family=="GPD" && vendor==0x045e && product==0x028e);
                    const bool expected=device && capability.first==1 && (capability.second==4 || capability.second==5);
                    check(ymcc::nativeForeground::gameInputAdmitted(profile,true,false,vendor,product,capability.first,capability.second)==expected,name+" source game capability");
                    check(!ymcc::nativeForeground::gameInputAdmitted(profile,false,false,vendor,product,capability.first,capability.second),name+" different root denied");
                    check(!ymcc::nativeForeground::gameInputAdmitted(profile,true,true,vendor,product,capability.first,capability.second),name+" virtual source denied");
                }
            }
        }
        for(const auto& filter:model.at("filters")){
            const unsigned page=filter.at("page"),usage=filter.at("usage");
            bool game=false;
            const auto role=hidHideClassifyInterfaceRole(page,usage,"HID\\AUXILIARY\\CATALOG",&game);
            const bool expectedGame=page==1 && (usage==4 || usage==5);
            check(game==expectedGame,name+" declared auxiliary HID role");
            if(page>=0xff00)check(role==HidHideIfaceRole::VendorControl,name+" vendor is control only");
        }
        for(const auto& capability:std::vector<std::pair<unsigned,unsigned>>{{1,4},{1,5},{1,6},{1,2},{12,1},{32,1},{0xff00,1}}){
            bool game=true;
            check(hidHideClassifyInterfaceRole(capability.first,capability.second,"HID\\HIDMAESTRO\\VIRTUAL",&game)==HidHideIfaceRole::Virtual && !game,name+" virtual exclusion");
        }
        rows.push_back({{"name",name},{"instantiable",model.at("instantiable")},{"family",model.at("family")},{"runtimePresent",model.at("runtimePresent")},{"identityCases",model.at("identities").size()},{"nativeMachineCases",admittedMachineCases},{"gameCandidates",model.at("gameCandidates").size()},{"checks",checks-modelChecks},{"physicalAcceptance",false}});
    }
    for(const auto* persona:{"dualshock4","dualsense","xbox360","unknown"})check(!ymcc::controllerPersona::currentOutput(persona),std::string("retired output ")+persona);
    json result={{"passed",failures.empty()},{"checks",checks},{"failures",failures},{"types",rows.size()},{"rows",rows},{"productionFunctionsExtracted",true},{"hardwareAccess",false},{"productMainExecuted",false}};
    std::ofstream(argv[2])<<result.dump(2)<<'\n';std::cout<<json({{"passed",failures.empty()},{"checks",checks},{"types",rows.size()}}).dump()<<'\n';return failures.empty()?0:1;
}
