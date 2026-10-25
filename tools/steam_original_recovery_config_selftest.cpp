#include "steam_original_recovery_config.h"
#include <iostream>
#include <stdexcept>
using J=nlohmann::json;
int main(){unsigned checks=0;const auto need=[&](bool value){++checks;if(!value)throw std::runtime_error("configuration assertion");};
    const auto config=ymcc::steamOriginal::configuration;
    auto c=config(nullptr,false,false);need(c.at("enabled")==true&&c.at("targetMode")=="generic");
    c=config(J{{"enabled",false}},true,false);need(c.at("enabled")==false);
    c=config(nullptr,true,false);need(c.at("enabled")==false);
    c=config(J{{"enabled",true}},false,true);need(c.at("enabled")==false);
    for(const auto bad:J::array({"yes",1,nullptr,J::object(),J::array()})){
        c=config(J{{"enabled",bad}},true,false);need(c.at("enabled")==false);
    }
    for(const auto bad:J::array({"other",true,1,nullptr,J::array()})){
        c=config(J{{"enabled",true},{"targetMode",bad}},true,false);need(c.at("enabled")==false);
    }
    c=config(J{{"enabled",true},{"targetGameId","10260559608980439040"},{"targetAppId",2388972698u}},true,false);
    need(c.at("enabled")==true&&c.at("targetMode")=="generic"); // Historical pin is not a silent portable lock.
    c=config(J{{"enabled",true},{"targetMode","fixed"},{"targetGameId","10307630032478011392"}},true,false);
    need(c.at("targetMode")=="fixed");
    c=config(J{{"enabled",true},{"source",J::array()}},true,false);need(c.at("source")=="local-override");
    std::cout<<"{\"checks\":"<<checks<<",\"passed\":true,\"processActions\":0,\"sleepActions\":0}\n";
}
