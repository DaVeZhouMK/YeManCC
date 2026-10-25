#pragma once
#include "msi_usb_name_origin_decode.h"
#include <set>
namespace ymcc::msi::usbName {
// Additional collection-completeness gate; structural descriptor decoding alone
// does not prove that every declared interface/name index was present.
inline json configurationCoverage(const std::vector<uint8_t>& header,const json& parsed,unsigned currentInterface=0){
    json row={{"valid",false},{"error",""},{"headerMatches",false},{"currentInterface",currentInterface},{"currentInterfacePresent",false}};
    auto fail=[&](const char* reason){row["error"]=reason;return row;};
    if(header.size()!=9||header[0]!=9||header[1]!=2)return fail("configuration-query-header-invalid");
    if(!parsed.value("valid",false)||!parsed.contains("rawBytes")||!parsed["rawBytes"].is_array()||parsed["rawBytes"].size()<9)return fail("full-configuration-not-valid");
    for(size_t i=0;i<9;++i)if(parsed["rawBytes"][i]!=header[i])return fail("full-query-header-differs");
    row["headerMatches"]=true;
    if(!parsed.contains("interfaces")||!parsed["interfaces"].is_array())return fail("interface-table-unavailable");
    std::set<unsigned> numbers;std::set<std::pair<unsigned,unsigned>> alternateIdentities;
    for(const auto& iface:parsed["interfaces"]){
        const auto number=iface.at("number").get<unsigned>(),alternate=iface.at("alternateSetting").get<unsigned>();
        if(number>255||alternate>255||!alternateIdentities.insert({number,alternate}).second)return fail("interface-identity-invalid-or-duplicate");
        numbers.insert(number);
    }
    const unsigned declared=parsed.value("numInterfaces",0u);row["declaredInterfaceCount"]=declared;row["distinctInterfaceCount"]=numbers.size();row["interfaceNumbers"]=numbers;
    row["currentInterfacePresent"]=numbers.count(currentInterface)!=0;
    if(!declared||numbers.size()!=declared)return fail("declared-interface-set-incomplete");
    if(!numbers.count(currentInterface))return fail("current-interface-not-present");
    if(!parsed.contains("iads")||!parsed["iads"].is_array())return fail("iad-table-unavailable");
    for(const auto& iad:parsed["iads"]){const auto first=iad.at("firstInterface").get<unsigned>(),count=iad.at("interfaceCount").get<unsigned>();
        if(!count||first>255||count>256-first)return fail("iad-interface-range-invalid");
        for(unsigned i=0;i<count;++i)if(!numbers.count(first+i))return fail("iad-interface-range-incomplete");
    }
    row["valid"]=true;return row;
}
}
