#include "steam_original_recovery_config.h"
#include <iostream>
#include <stdexcept>
using J=nlohmann::json;
int main(){try{unsigned checks=0;const auto need=[&](bool value,const std::string& name="original-configuration"){++checks;if(!value)throw std::runtime_error(name+" (check "+std::to_string(checks)+")");};
    const auto config=ymcc::steamOriginal::configuration;
    // Exact read-only local runtime/enabled.json sample captured on 2026-11-02.
    // No runtime path is opened by this consumer; configuration receives a copy.
    const J legacy=J::parse(R"json({
      "targetGameId":"10260559608980439040",
      "profile":{
        "clientSha256":"e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e",
        "uiSha256":"1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252"
      },
      "schema":"ymcc.steam-original-recovery-enabled.v1",
      "enabled":true,
      "targetAppId":2388972698,
      "authorized":"user-20261021-ymcc-only-no-assistant-sleep"
    })json");
    // Keep the two semantic counterexamples bounded: each fails before the old suite.
    need(config(legacy,true,false).at("enabled")==true,"legacy-v1-real-runtime-enabled");
    {auto off=legacy;off["enabled"]=false;
        need(config(off,true,false).at("enabled")==false,"legacy-v1-explicit-off-remains-off");}
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
    for(const auto schema:J::array({"ymcc.steam-original-recovery-enabled.v99",2,nullptr,J::object()})){
        c=config(J{{"schema",schema},{"enabled",true}},true,false);need(c.at("enabled")==false);
    }
    c=config(J{{"schema","ymcc.steam-original-recovery-enabled.v2"},{"enabled",true},{"extension",{{"keep",7}}}},true,false);
    need(c.at("enabled")==true&&c.at("extension").at("keep")==7);
    for(const auto id:J::array({"18446744073709551616","10307630032478011393","945360",17,nullptr})){
        c=config(J{{"enabled",true},{"targetMode","fixed"},{"targetGameId",id}},true,false);need(c.at("enabled")==false);
    }
    c=config(J{{"enabled",true},{"targetMode","fixed"},{"targetGameId","10307630032478011392"},{"targetAppId",2388972698u}},true,false);need(c.at("enabled")==false);
    c=config(J{{"enabled",true},{"targetMode","fixed"}},true,false);need(c.at("enabled")==false);
    const std::string schemaV2="ymcc.steam-original-recovery-enabled.v2";
    const auto legacyResult=[&](J input,const std::string& name){
        const J before=input;
        const J actual=config(input,true,false);
        J expected=input;expected["schema"]=schemaV2;expected["source"]="legacy-v1-compatible";
        if(!expected.contains("targetMode"))expected["targetMode"]="generic";
        need(actual==expected,name+"-normalized-output-and-extensions");
        need(input==before,name+"-input-unchanged");
        return actual;
    };
    c=legacyResult(legacy,"legacy-v1-real-runtime");
    need(c.at("targetMode")=="generic","legacy-v1-historical-pin-and-profile-do-not-select-fixed");
    need(c.at("schema")==schemaV2&&c.at("source")=="legacy-v1-compatible","legacy-v1-normalization-provenance");
    need(config(c,true,false)==c,"legacy-v1-normalization-idempotent");
    {auto off=legacy;off["enabled"]=false;legacyResult(off,"legacy-v1-off");}
    {auto tagged=legacy;tagged["source"]="historical-authorization";
        tagged["extension"]={{"keep",7},{"nested",J::array({false,nullptr,"keep"})}};
        legacyResult(tagged,"legacy-v1-source-replaced-extensions-preserved");}
    {auto generic=legacy;generic["targetMode"]="generic";generic["targetGameId"]=nullptr;
        generic["targetAppId"]="stale-appid";generic["profile"]=J::array({"stale-profile"});
        legacyResult(generic,"legacy-v1-explicit-generic-does-not-authorize-stale-fields");}
    {auto generic=legacy;generic.erase("targetAppId");
        need(!legacyResult(generic,"legacy-v1-generic-no-appid").contains("targetAppId"),
             "legacy-v1-generic-does-not-derive-appid-from-old-pin");}

    const auto rejected=[&](const J& input,bool exists,bool lookupError,const std::string& name){
        const J before=input;const J actual=config(input,exists,lookupError);
        need(actual.at("enabled")==false&&actual.at("targetMode")=="generic"&&
             actual.at("source")=="invalid-local-override",name);
        need(input==before,name+"-input-unchanged");
    };
    for(const bool exists:{false,true}){
        rejected(legacy,exists,true,"legacy-v1-lookup-error-exists-"+std::to_string(exists));
    }
    for(const auto& unreadable:J::array({nullptr,"unreadable",true,7,J::array(),J::object()})){
        rejected(unreadable,true,false,"present-unreadable-root-"+unreadable.dump());
    }
    {auto missing=legacy;missing.erase("enabled");rejected(missing,true,false,"legacy-v1-missing-enabled");}
    for(const auto& bad:J::array({"true",1,nullptr,J::object(),J::array()})){
        auto invalid=legacy;invalid["enabled"]=bad;
        rejected(invalid,true,false,"legacy-v1-invalid-enabled-"+bad.dump());
    }
    for(const auto& schema:J::array({"ymcc.steam-original-recovery-enabled.v0",
          "ymcc.steam-original-recovery-enabled.v3","ymcc.steam-original-recovery-enabled.v99",
          "ymcc.steam-original-recovery-enabled.v1 ","YMCC.steam-original-recovery-enabled.v1",
          "ymcc.steam-original-recovery-enabled.v1.0","",true,2,2.0,nullptr,J::object(),J::array()})){
        auto invalid=legacy;invalid["schema"]=schema;
        rejected(invalid,true,false,"unknown-or-invalid-schema-"+schema.dump());
    }
    for(const auto& mode:J::array({"other",true,1,nullptr,J::object(),J::array()})){
        auto invalid=legacy;invalid["targetMode"]=mode;
        rejected(invalid,true,false,"legacy-v1-invalid-target-mode-"+mode.dump());
    }

    // Use the same explicit fixed contract for legacy v1, v2 and no schema.
    const auto fullId=std::string("10307630032478011392");
    const auto appid=static_cast<std::uint32_t>(std::stoull(fullId)>>32);
    for(const auto& schema:J::array({"ymcc.steam-original-recovery-enabled.v1",schemaV2,nullptr})){
        auto fixed=legacy;fixed["targetMode"]="fixed";fixed["targetGameId"]=fullId;fixed["targetAppId"]=appid;
        if(schema.is_null())fixed.erase("schema");else fixed["schema"]=schema;
        const auto label=std::string("fixed-")+(schema.is_null()?"no-schema":schema.get<std::string>());
        const auto actual=config(fixed,true,false);
        need(actual.at("enabled")==true&&actual.at("targetMode")=="fixed"&&
             actual.at("targetGameId")==fullId&&actual.at("targetAppId")==appid,label+"-valid-type2-consistent-appid");
        need(actual.at("profile")==legacy.at("profile")&&actual.at("authorized")==legacy.at("authorized"),
             label+"-historical-fields-remain-extensions");
        if(schema=="ymcc.steam-original-recovery-enabled.v1"){
            legacyResult(fixed,label+"-normalized");
        }else{
            need(actual.contains("schema")==fixed.contains("schema"),label+"-schema-presence-unchanged");
            if(fixed.contains("schema"))need(actual.at("schema")==schema,label+"-schema-unchanged");
            need(actual.at("source")=="local-override",label+"-source-rule-unchanged");
        }
        {auto derived=fixed;derived.erase("targetAppId");
            const auto normalized=config(derived,true,false);
            need(normalized.at("enabled")==true&&normalized.at("targetMode")=="fixed"&&
                 normalized.at("targetAppId")==appid,label+"-derives-appid-from-type2-full-id");}
        {auto off=fixed;off["enabled"]=false;const auto normalized=config(off,true,false);
            need(normalized.at("enabled")==false&&normalized.at("targetMode")=="fixed",label+"-valid-off-remains-off");
            if(schema=="ymcc.steam-original-recovery-enabled.v1")legacyResult(off,label+"-off-normalized");}
        for(const auto& pin:J::array({"", "010307630032478011392", "18446744073709551616",
              "10307630032478011393","10307630032461234176","33554432","945360",
              "-10307630032478011392","10307630032478011392x",17,nullptr,J::object(),J::array()})){
            auto invalid=fixed;invalid["targetGameId"]=pin;
            rejected(invalid,true,false,label+"-invalid-full-id-"+pin.dump());
        }
        {auto missing=fixed;missing.erase("targetGameId");rejected(missing,true,false,label+"-missing-full-id");}
        for(const auto& pin:J::array({2388972698u,-1,0,4294967296ULL,"2409245359",true,1.0,nullptr,J::object(),J::array()})){
            auto invalid=fixed;invalid["targetAppId"]=pin;
            rejected(invalid,true,false,label+"-invalid-or-mismatched-appid-"+pin.dump());
        }
    }
    for(const bool withSchema:{false,true}){
        J generic={{"enabled",true},{"targetGameId","stale-pin"},{"profile",legacy.at("profile")},
                   {"authorized",legacy.at("authorized")},{"extension",{{"keep",7}}}};
        if(withSchema)generic["schema"]=schemaV2;
        J expected=generic;expected["targetMode"]="generic";expected["source"]="local-override";
        need(config(generic,true,false)==expected,withSchema?"v2-generic-rule-unchanged":"no-schema-generic-rule-unchanged");
        generic["enabled"]=false;expected["enabled"]=false;
        need(config(generic,true,false)==expected,withSchema?"v2-off-rule-unchanged":"no-schema-off-rule-unchanged");
        rejected(generic,true,true,withSchema?"v2-off-lookup-error":"no-schema-off-lookup-error");
    }
    std::cout<<"{\"checks\":"<<checks<<",\"passed\":true,\"processActions\":0,\"sleepActions\":0}\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
