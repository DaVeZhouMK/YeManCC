#include "../native/steam_desktop_binding_evidence.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using ymcc::steamdeck::bindingevidence::summarize;
using Json=nlohmann::json;
static int checks=0,cases=0;
static Json failures=Json::array();
static void expect(bool condition,const std::string& name) { ++checks; if (!condition) failures.push_back(name); }
static std::string root(const std::string& body) {
    return "\"controller_mappings\" { \"version\" \"3\" \"controller_type\" \"controller_neptune\" "+body+" }";
}
static std::string input(const std::string& bindings,const std::string& name="left_bumper",const std::string& activator="Full_Press") {
    return "\""+name+"\" { activators { \""+activator+"\" { bindings { "+bindings+" } } } }";
}
static std::string group(const std::string& id,const std::string& inputs) {
    return "group { id \""+id+"\" mode switches inputs { "+inputs+" } } ";
}
static std::string preset(const std::string& id,const std::string& refs,const std::string& extra="") {
    return "preset { id \""+id+"\" name PRIVATE_TITLE group_source_bindings { "+refs+" } "+extra+" } ";
}
static const std::string ctrl="binding \"key_press LEFT_CONTROL\" ";
static const std::string alt="binding \"key_press LEFT_ALT\" ";
static bool match(const Json& j,unsigned id,const char* in,const char* mod) {
    for (const auto& m:j["matches"]) if(m["groupId"]==id && m["input"]==in && m["modifier"]==mod) return true;
    return false;
}
static bool reference(const Json& j,unsigned id,unsigned gid,const char* state="active",const char* relation="stored-group") {
    for (const auto& p:j["presets"]) if(p["id"]==id)
        for(const auto& r:p["groupSourceBindings"]) if(r["groupId"]==gid && r["storedState"]==state && r["groupStatus"]==relation) return true;
    return false;
}
static Json run(const std::string& label,const std::string& bytes) {
    ++cases; const auto saved=bytes; auto j=summarize(bytes);
    expect(bytes==saved,label+":input-unmodified");
    expect(j["active"]==false && j["activePresetKnown"]==false && j["producer"]==false && j["absenceProven"]==false,label+":no-authority-or-absence-proof");
    expect(j["storedLayoutOnly"]==true && j["schema"]==1,label+":stored-schema");
    expect(j["groups"].size()<=64 && j["presets"].size()<=16 && j["matches"].size()<=32,label+":array-bounds");
    std::size_t refs=0;for(const auto& p:j["presets"]) refs+=p["groupSourceBindings"].size();
    expect(refs<=64,label+":reference-bound");expect(j.dump().size()<=65536,label+":serialized-bound");
    return j;
}
int main(int argc,char** argv) {
 try {
    if(argc!=2) throw std::runtime_error("exact frozen fixture argument required");
    std::ifstream file(argv[1],std::ios::binary);if(!file)throw std::runtime_error("frozen fixture unavailable");
    const std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    auto actual=run("frozen-default",bytes);
    expect(actual["ok"]==true,"frozen-default:accepted");
    expect(match(actual,7,"left_bumper","leftCtrl"),"frozen-default:LB-Ctrl");
    expect(match(actual,7,"right_bumper","leftAlt"),"frozen-default:RB-Alt");
    expect(reference(actual,0,7),"frozen-default:desktop-switch-7");
    expect(reference(actual,1,16),"frozen-default:separate-gamepad-switch-16");
    expect(!match(actual,16,"left_bumper","leftCtrl")&&!match(actual,16,"right_bumper","leftAlt"),"frozen-default:no-gamepad-modifier-inference");
    expect(actual.dump().find("76561197969363440")==std::string::npos && actual.dump().find("Desktop Configuration")==std::string::npos,"frozen-default:metadata-excluded");
    {
      auto j=run("custom-no-sensitivity",root(group("7",input(ctrl)+input(alt,"right_bumper"))+preset("0","7 \"switch active\"")));
      expect(j["ok"]==true&&j["coverageComplete"]==true,"custom:independent-of-sensitivity-parser");
      expect(j["matches"].size()==2&&reference(j,0,7),"custom:literal-pair-and-association");
    }
    {
      auto j=run("repeat-binding-fields",root(group("2",input(ctrl+alt+"binding \"key_press RIGHT_CONTROL\" binding \"key_press RIGHT_ALT\""))));
      expect(j["matches"].size()==4&&j["counters"]["bindingsSeen"]==4,"repeated:all-four-literals");
      expect(match(j,2,"left_bumper","rightCtrl")&&match(j,2,"left_bumper","rightAlt"),"repeated:right-modifiers");
    }
    {
      auto j=run("command-grammar",root(group("1",input("binding \"  key_press\\tLEFT_CONTROL  \" binding \"key_press W\" binding \"key_press LEFT_SHIFT\""))));
      // Backslash-t is not a decoded tab in this VDF parser; it must not invent a key.
      expect(j["matches"].empty()&&j["counters"]["unsupportedBindingCommands"]==1,"grammar:no-escape-interpretation");
      expect(j["counters"]["nonModifierKeyPressBindings"]==2,"grammar:unrelated-keys-counted-only");
      auto k=run("literal-whitespace",root(group("1",input("binding \" \tkey_press\tLEFT_CONTROL\r\n \""))));
      expect(match(k,1,"left_bumper","leftCtrl"),"grammar:literal-whitespace");
      auto u=run("unknown-key-tokens",root(group("1",input("binding \"key_press LEFT_CTRL\" binding \"key_press PRIVATE_KEY\""))));
      expect(u["matches"].empty()&&u["counters"]["unsupportedBindingCommands"]==2&&u["counters"]["nonModifierKeyPressBindings"]==0,"grammar:unknown-tokens-unsupported");
    }
    {
      const std::string secret="PRIVATE_PERSONAL_89250578";
      std::string bad="binding \"macro key_press LEFT_CONTROL PRIVATE_MACRO\" binding \"key_press LEFT_ALT, PRIVATE_LABEL\" binding \"key_press LEFT_CONTROL; key_press LEFT_ALT\" binding \"KEY_PRESS LEFT_ALT\" binding \"key_press LEFT_CONTROL RIGHT_ALT\"";
      auto j=run("privacy-and-macros",root("title PRIVATE_TITLE description PRIVATE_DESCRIPTION creator 76561197969363440 url https://PRIVATE_URL settings { cookie PRIVATE_COOKIE } "+group("7",input(ctrl+bad,secret,"PRIVATE_ACTIVATOR"))+preset("0","7 \"PRIVATE_SOURCE PRIVATE_STATE\"")));
      expect(j["matches"].size()==1&&j["matches"][0]["input"]=="unknown"&&j["matches"][0]["activator"]=="unknown","privacy:fixed-name-whitelists");
      expect(j["counters"]["unsupportedBindingCommands"]==5,"privacy:unrecognized-commands-counted");
      for(const char* canary:{"PRIVATE_","76561197969363440","89250578","https://","key_press","macro","cookie","description"})
        expect(j.dump().find(canary)==std::string::npos,std::string("privacy:excluded-")+canary);
      expect(j["presets"][0]["groupSourceBindings"][0]["source"]=="unknown","privacy:source-whitelist");
    }
    {
      auto j=run("duplicate-groups",root(group("7",input(ctrl))+group("07",input(alt))+preset("0","7 \"switch active\"")));
      expect(j["groups"].empty()&&j["matches"].empty(),"duplicate-groups:no-arbitrary-winner");
      expect(j["counters"]["duplicateGroupIds"]==1&&j["counters"]["ambiguousGroups"]==2,"duplicate-groups:explicit-counts");
      expect(reference(j,0,7,"active","ambiguous"),"duplicate-groups:ambiguous-reference");
      auto k=run("duplicate-id-fields",root("group { id 7 id 8 inputs { "+input(ctrl)+" } }"));
      expect(k["groups"].empty()&&k["counters"]["duplicateIdFields"]==1,"duplicate-id-fields:reject");
    }
    {
      auto j=run("duplicate-presets",root(group("7",input(ctrl))+preset("0","7 \"switch active\"")+preset("00","7 \"switch inactive\"")));
      expect(j["presets"].empty()&&j["counters"]["ambiguousPresets"]==2,"duplicate-presets:no-arbitrary-winner");
      expect(j["matches"].size()==1,"duplicate-presets:stored-group-still-visible");
      auto k=run("duplicate-preset-references",root(group("7",input(ctrl))+preset("0","7 \"switch active\" 07 \"switch inactive\"")));
      expect(k["presets"][0]["groupSourceBindings"].empty()&&k["counters"]["ambiguousPresetReferences"]==2,"duplicate-refs:no-arbitrary-state");
    }
    {
      auto j=run("dangling-numeric-reference",root(preset("0","4294967295 \"switch inactive\" -1 \"switch active\" 4294967296 \"switch active\"")));
      expect(reference(j,0,4294967295u,"inactive","missing"),"dangling:numeric-max-preserved");
      expect(j["counters"]["presetReferencesOmitted"]==2&&j["counters"]["missingGroupReferences"]==1,"dangling:invalid-and-missing-counters");
      auto k=run("invalid-group-identifiers",root(group("-1",input(ctrl))+group("4294967296",input(ctrl))+group("",input(ctrl))));
      expect(k["groups"].empty()&&k["counters"]["invalidIds"]==3,"identifiers:reject-invalid");
    }
    for(const std::string& text:{root("")+root(""),std::string("controller_mappings {} private_root {}")}) {
      auto j=run("ambiguous-root",text);expect(j["ok"]==false&&j["status"]=="ambiguous-root","ambiguous-root:reject");
    }
    for(const std::string& text:{std::string(""),std::string("other_root {}"),std::string("controller_mappings scalar")}) {
      auto j=run("unsupported-root",text);expect(j["ok"]==false,"unsupported-root:reject");
    }
    for(const std::string& type:{"controller_generic","PRIVATE_TYPE_76561197969363440"}) {
      auto j=run("unsupported-controller",std::string("controller_mappings { version 3 controller_type ")+type+" }");
      expect(j["status"]=="unsupported-controller-type"&&j["counters"]["unsupportedControllerTypes"]==1,"controller:explicit-unsupported");
      expect(j.dump().find(type)==std::string::npos,"controller:private-type-not-exported");
    }
    {
      auto j=run("duplicate-controller-header",root("controller_type controller_neptune"));
      expect(j["ok"]==false,"controller:duplicate-header-rejected");
      for(const std::string& text:{std::string("controller_mappings { controller_type controller_neptune }"),std::string("controller_mappings { version 4 controller_type controller_neptune }"),root("version 3")}) {
        auto k=run("unknown-format",text);expect(k["ok"]==false&&k["counters"]["unsupportedFormatVersions"]==1,"format:unknown-not-absence");
      }
    }
    for(const std::string& text:{std::string("controller_mappings {"),root(group("7",input(ctrl))).substr(0,80),std::string("\"unterminated"),root("")+" }",root("")+" /* truncated"}) {
      auto j=run("malformed",text);expect(j["ok"]==false&&j["counters"]["parseFailures"]==1,"malformed:explicit-failure");
    }
    {
      auto j=run("oversize",std::string(2*1024*1024+1,' '));expect(j["status"]=="input-too-large","size:reject-over-limit");
      auto exact=root("");exact.resize(2*1024*1024,' ');auto k=run("exact-size",exact);expect(k["ok"]==true,"size:accept-exact-limit");
      std::string nested="leaf value ";for(int i=0;i<63;++i)nested="n { "+nested+" }";
      auto m=run("depth-boundary",root(nested));expect(m["ok"]==true,"depth:accept-64");
      auto n=run("depth-overflow",root("n { "+nested+" }"));expect(n["ok"]==false,"depth:reject-65");
      std::string many;for(int i=0;i<30000;++i)many+="x v ";auto q=run("node-overflow",root(many));expect(q["ok"]==false,"nodes:reject-over-limit");
    }
    {
      std::string groups;for(int i=0;i<65;++i)groups+=group(std::to_string(i),input(ctrl));
      auto j=run("group-and-match-caps",root(groups));
      expect(j["groups"].size()==64&&j["matches"].size()==32,"caps:groups-and-matches");
      expect(j["counters"]["groupsSeen"]==65&&j["counters"]["groupsOmitted"]==1&&j["counters"]["matchesOmitted"]==32,"caps:explicit-omissions");
      auto k=run("duplicate-beyond-group-cap",root(groups+group("0",input(alt))));
      expect(!match(k,0,"left_bumper","leftCtrl")&&k["counters"]["ambiguousGroups"]==2,"caps:late-duplicate-invalidates-first");
      std::string presets;for(int i=0;i<17;++i)presets+=preset(std::to_string(i),"7 \"switch active\"");
      auto m=run("preset-cap",root(group("7",input(ctrl))+presets));
      expect(m["presets"].size()==16&&m["counters"]["presetsOmitted"]==1,"caps:presets");
      std::string refs;for(int i=0;i<65;++i)refs+=std::to_string(i)+" \"switch active\" ";
      auto n=run("reference-cap",root(groups+preset("0",refs)));
      expect(n["presets"][0]["groupSourceBindings"].size()==64&&n["counters"]["presetReferencesOmitted"]==1,"caps:references");
      auto q=run("reference-global-cap",root(groups+preset("0",refs)+preset("1",refs)));
      expect(q["presets"][1]["groupSourceBindings"].empty()&&q["counters"]["presetReferencesOmitted"]==66,"caps:references-global");
    }
    {
      auto j=run("ambiguous-input-containers",root("group { id 7 inputs { "+input(ctrl)+" } inputs { "+input(alt)+" } }"));
      expect(j["matches"].empty()&&j["counters"]["ambiguousContainers"]==1,"containers:reject-ambiguous");
      auto k=run("malformed-binding-fields",root(group("7",input("binding { PRIVATE_NODE PRIVATE_VALUE } unrelated PRIVATE_TEXT"))));
      expect(k["matches"].empty()&&k["counters"]["unsupportedBindingFields"]==2,"fields:reject-objects-and-unrecognized");
      expect(k.dump().find("PRIVATE_")==std::string::npos,"fields:no-private-echo");
    }
    {
      auto j=run("disabled-only",root(group("7","left_bumper { disabled_activators { Full_Press { bindings { "+ctrl+" } } } activators {} }")));
      expect(j["matches"].empty()&&j["counters"]["disabledActivatorEntries"]==1,"disabled:not-enabled-binding");
      auto k=run("modeshift-presence",root(group("7",input(ctrl))+preset("0","7 \"switch active\"","group_source_modeshifts { 7 PRIVATE_MODE }")));
      expect(k["modeShiftPresent"]==true&&k["presets"][0]["modeShiftPresent"]==true&&k["counters"]["unsupportedModeShifts"]==1,"modeshift:presence-with-unsupported-counter");
      expect(k.dump().find("PRIVATE_MODE")==std::string::npos,"modeshift:no-private-echo");
      auto m=run("empty-modeshift",root(preset("0","","group_source_modeshifts {}")));
      expect(m["modeShiftPresent"]==true&&m["counters"]["modeShiftEntries"]==0,"modeshift:empty-present");
    }
    {
      auto j=run("invalid-utf8-metadata",root(std::string("description \"")+char(-1)+"\" "+group("7",input(ctrl,std::string(1,char(-1))))));
      expect(j["matches"].size()==1&&j["matches"][0]["input"]=="unknown","utf8:untrusted-name-never-serialized");
      const auto stable=root(group("7",input(ctrl))+preset("0","7 \"switch active\""));
      expect(summarize(stable)==summarize(stable),"pure:deterministic");
      auto k=run("no-bindings",root(""));expect(k["ok"]==true&&k["matches"].empty()&&k["absenceProven"]==false,"empty:not-absence-proof");
    }
    std::cout<<Json({{"pass",failures.empty()},{"checks",checks},{"cases",cases},{"failures",failures},
        {"frozenDefaultSummary",actual},{"actualSteamCalls",0},{"OSInputCalls",0},{"producer",false}}).dump(2)<<'\n';
    return failures.empty()?0:1;
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
}
