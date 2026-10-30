#pragma once
#include <set>
#include <string>

namespace ymcc::gpuschedule {
// Match the exact CPU SKU, never a chassis brand, GPU number or 3950X prefix.
inline bool isAmd395(const std::string& processor) {
    std::set<std::string> tokens;std::string token;
    const auto flush=[&]{if(!token.empty()){tokens.insert(token);token.clear();}};
    for(const unsigned char raw:processor) {
        const unsigned char c=raw>='a'&&raw<='z'?static_cast<unsigned char>(raw-'a'+'A'):raw;
        if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))token+=static_cast<char>(c);
        else flush();
    }
    flush();
    return tokens.count("AMD")&&tokens.count("RYZEN")&&tokens.count("AI")&&
        tokens.count("MAX")&&tokens.count("395");
}
} // namespace ymcc::gpuschedule
