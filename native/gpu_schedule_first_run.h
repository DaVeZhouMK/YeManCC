#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace ymcc::gpuschedule {
// Exact processor family and SKU; never infer from a chassis or Radeon suffix.
inline bool isAmdAiMaxDefaultOff(const std::string& processor) {
    std::vector<std::string> tokens;std::string token;
    const auto flush=[&]{if(!token.empty()){tokens.push_back(token);token.clear();}};
    for(const unsigned char raw:processor) {
        const unsigned char c=raw>='a'&&raw<='z'?static_cast<unsigned char>(raw-'a'+'A'):raw;
        if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))token+=static_cast<char>(c);
        else flush();
    }
    flush();
    if(std::find(tokens.begin(),tokens.end(),"AMD")==tokens.end())return false;
    for(size_t i=0;i<tokens.size();++i) {
        if(tokens[i]!="RYZEN")continue;
        size_t next=i+1;
        if(next<tokens.size()&&(tokens[next]=="TM"||tokens[next]=="R"))++next;
        if(next+2>=tokens.size()||tokens[next]!="AI"||tokens[next+1]!="MAX")continue;
        next+=2;
        if(tokens[next]=="PRO"&&++next>=tokens.size())continue;
        const auto& sku=tokens[next];
        return sku=="385"||sku=="388"||sku=="395";
    }
    return false;
}
} // namespace ymcc::gpuschedule
