// Matrix consumes the production comparison template, not a copied algorithm.
#include "virtual_output_transition_status.h"
#include "json.hpp"
#include <functional>
#include <fstream>
#include <type_traits>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using json=nlohmann::json;
struct MatrixCase {std::string id;json left,right;bool expected;};
static void add(std::vector<MatrixCase>& cases,const std::string& id,const json& left,const json& right,bool expected){cases.push_back({id,left,right,expected});}
static void leaves(const json& value,const std::string& prefix,std::vector<std::string>& paths){
    if(value.is_object()){for(auto i=value.cbegin();i!=value.cend();++i)leaves(i.value(),prefix+"/"+i.key(),paths);}
    else paths.push_back(prefix);
}
static json differentValue(const json& value){
    if(value.is_boolean())return !value.get<bool>();
    if(value.is_number_unsigned())return value.get<std::uint64_t>()+std::uint64_t{1};
    if(value.is_number_integer())return value.get<std::int64_t>()+std::int64_t{1};
    if(value.is_number_float())return value.get<double>()+0.5;
    if(value.is_string())return value.get<std::string>()+"-changed84";
    return "changed84";
}
static std::vector<MatrixCase> makeCases(const json& base){
    std::vector<MatrixCase> cases;json changed=base;changed["startProtection"]["expiresAtUnixMs"]=std::uint64_t{1900000000123ULL};
    add(cases,"valid-active-expiry-only",base,changed,true);
    add(cases,"identical-valid-active",base,base,true);
    std::vector<std::string> paths;leaves(base,"",paths);
    for(const auto& p:paths){
        if(p=="/startProtection/expiresAtUnixMs")continue;
        json x=changed;const json::json_pointer ptr(p);x[ptr]=differentValue(base.at(ptr));
        add(cases,"control-value"+p,base,x,false);
        x=changed;x[ptr]=nullptr;add(cases,"control-null"+p,base,x,false);
        x=changed;const auto slash=p.find_last_of('/');const json::json_pointer parent(p.substr(0,slash));x.at(parent).erase(p.substr(slash+1));
        add(cases,"control-missing"+p,base,x,false);
        x=changed;x[ptr]=json::array({base.at(ptr)});add(cases,"control-shape-array"+p,base,x,false);
        if(base.at(ptr).is_number()){
            x=changed;x[ptr]=base.at(ptr).get<double>();add(cases,"control-number-type-float"+p,base,x,false);
            x=changed;if(base.at(ptr).is_number_unsigned())x[ptr]=base.at(ptr).get<std::int64_t>();else x[ptr]=base.at(ptr).get<std::uint64_t>();
            add(cases,"control-integer-storage-type"+p,base,x,false);
        }
    }
    add(cases,"unknown-root-added",base,[&]{json x=changed;x["futureControl"]=true;return x;}(),false);
    add(cases,"unknown-protection-added",base,[&]{json x=changed;x["startProtection"]["futureControl"]=true;return x;}(),false);
    for(const std::string where:{"", "/startProtection"}){
        const json::json_pointer p(where);json a=base,b=changed;
        a[p]["future"]={{"nested",json::array({1,2,3})},{"expiresAtUnixMs",123}};b[p]["future"]=a[p]["future"];
        add(cases,"unknown-equal-object"+where,a,b,true);
        b[p]["future"]["nested"][1]=9;add(cases,"unknown-nested-array-value"+where,a,b,false);
        b[p]["future"]=json::array({1,2,3});add(cases,"unknown-shape"+where,a,b,false);
        b[p]["future"]=a[p]["future"];b[p]["future"]["expiresAtUnixMs"]=124;
        add(cases,"unknown-nested-expiry-not-ignored"+where,a,b,false);
        b[p]["future"]=a[p]["future"];b[p]["future"]["nested"].push_back(4);
        add(cases,"unknown-array-length"+where,a,b,false);
        b[p]["future"]=a[p]["future"];b[p]["future"]["nested"]=json::array({3,2,1});
        add(cases,"unknown-array-order"+where,a,b,false);
        b[p]["future"]=a[p]["future"];b[p]["future"]["extra"]=nullptr;
        add(cases,"unknown-object-key-set"+where,a,b,false);
    }
    const std::vector<json> invalid={nullptr,false,true,0,-1,0.0,1.0,1.5,"1",json::array(),json::object()};
    for(const std::string key:{"locked","requestId","expiresAtUnixMs"}){
        for(std::size_t n=0;n<invalid.size();++n){
            // Boolean true is the one valid locked value; positive integer fields exclude booleans/floats.
            if(key=="locked"&&invalid[n].is_boolean()&&invalid[n].get<bool>())continue;
            json a=base,b=changed;b["startProtection"][key]=invalid[n];
            if(a==b){b["startProtection"]["expiresAtUnixMs"]=std::uint64_t{1900000000999ULL};}
            add(cases,"invalid-right-"+key+"-"+std::to_string(n),a,b,false);
            a["startProtection"][key]=invalid[n];
            if(key=="expiresAtUnixMs"){
                b["startProtection"]["expiresAtUnixMs"]=invalid[n].is_number()?json("different-invalid"):json(-2);
            }
            add(cases,"invalid-both-no-expiry-exemption-"+key+"-"+std::to_string(n),a,b,false);
        }
        json a=base,b=changed;b["startProtection"].erase(key);add(cases,"missing-right-"+key,a,b,false);
        a["startProtection"].erase(key);if(key=="expiresAtUnixMs")b["state"]="different";
        add(cases,"missing-both-no-expiry-exemption-"+key,a,b,false);
    }
    for(const std::string key:{"requestId","expiresAtUnixMs"}){
        for(const json& number:{json(std::uint64_t{1}),json(std::numeric_limits<std::uint64_t>::max()),json(std::int64_t{1}),json(std::numeric_limits<std::int64_t>::max())}){
            json a=base,b=changed;a["startProtection"][key]=number;b["startProtection"][key]=number;
            if(key=="expiresAtUnixMs")b["startProtection"][key]=number.is_number_unsigned()?json(number.get<std::uint64_t>()==1?std::uint64_t{2}:std::uint64_t{1}):json(number.get<std::int64_t>()==1?std::int64_t{2}:std::int64_t{1});
            add(cases,"positive-boundary-"+key+"-"+std::to_string(static_cast<int>(number.type()))+"-"+number.dump(),a,b,true);
        }
    }
    for(const json& malformed:{json(nullptr),json::array(),json("bad"),json(true)}){
        json b=changed;b["startProtection"]=malformed;add(cases,"protection-shape-"+malformed.dump(),base,b,false);
        json a=base;a["startProtection"]=malformed;add(cases,"same-bad-shape-no-exemption-"+malformed.dump(),a,b,true);
        b["state"]="changed";add(cases,"same-bad-shape-other-control-"+malformed.dump(),a,b,false);
    }
    json missing=base;missing.erase("startProtection");add(cases,"missing-protection",base,missing,false);
    json sameMissing=missing;sameMissing["state"]="changed";add(cases,"both-missing-protection-other-control",missing,sameMissing,false);
    add(cases,"identical-missing-protection-strict-whole-json-only",missing,missing,true);
    for(const std::string phase:{"success","failure","idle","expired","cancelled"}){
        json a=base,b=changed;a["startProtection"]["locked"]=false;b["startProtection"]["locked"]=false;
        a["startProtection"]["phase"]=phase;b["startProtection"]["phase"]=phase;
        add(cases,"unlocked-expiry-not-exempt-"+phase,a,b,false);
        a["startProtection"]["expiresAtUnixMs"]=0;b["startProtection"]["expiresAtUnixMs"]=0;
        add(cases,"identical-unlocked-zero-expiry-"+phase,a,b,true);
    }
    add(cases,"array-identical",json::array({1,2}),json::array({1,2}),true);
    add(cases,"array-order",json::array({1,2}),json::array({2,1}),false);
    add(cases,"scalar-number-type",json(1),json(1.0),false);
    return cases;
}
template<class Json>
static json checkCases(const std::vector<MatrixCase>& cases,const char* kind){
    json receipts=json::array();unsigned checks=0;
    for(const auto& c:cases){
        const Json left=Json::parse(c.left.dump()),right=Json::parse(c.right.dump());
        // Preserve integer/float storage for the ordinary json instantiation below by using the source objects.
        const Json a=[&](){if constexpr(std::is_same_v<Json,json>)return c.left;else return left;}();
        const Json b=[&](){if constexpr(std::is_same_v<Json,json>)return c.right;else return right;}();
        const auto beforeA=a.dump(),beforeB=b.dump();
        const bool actual=ymcc::virtualOutputPublicationEquivalent(a,b),reverse=ymcc::virtualOutputPublicationEquivalent(b,a);
        // ordered_json parse canonicalizes positive signed values to unsigned; its expected type-only cases are separately instantiated below.
        const bool storageOnly=c.id.rfind("control-integer-storage-type",0)==0;
        const bool expected=std::is_same_v<Json,json>?c.expected:(storageOnly?true:c.expected);
        checks+=4;
        const bool passed=actual==expected&&reverse==expected&&a.dump()==beforeA&&b.dump()==beforeB;
        receipts.push_back({{"id",c.id},{"expected",expected},{"actual",actual},{"reverse",reverse},{"passed",passed}});
        if(!passed)throw std::runtime_error(std::string(kind)+" matrix failed "+c.id+" expected="+std::to_string(expected)+" actual="+std::to_string(actual));
    }
    return {{"jsonType",kind},{"cases",cases.size()},{"checks",checks},{"receipts",receipts}};
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Expected actual status-producer receipt path");
        std::ifstream stream(argv[1]);json raw;stream>>raw;
        const json base=raw.at("presentationReceipts").at(0).at("receipt");
        // Only the transport-age and identity inputs vary in the matrix. The
        // remaining output facts come from the actual producer fixture receipt.
        json active=base;active["startProtection"]={{"locked",true},{"requestId",1ULL},{"expiresAtUnixMs",1900000000000ULL},{"phase","verifying"},{"remainingMs",20000},{"verificationRemainingMs",9000},{"candidateOutcome","success"},{"persona","elite"}};
        const auto cases=makeCases(active);
        const json ordinary=checkCases<json>(cases,"nlohmann::json");
        const json ordered=checkCases<nlohmann::ordered_json>(cases,"nlohmann::ordered_json");
        std::cout<<json({{"actualProductionHeader",true},{"passed",true},{"results",json::array({ordinary,ordered})}}).dump(2)<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
