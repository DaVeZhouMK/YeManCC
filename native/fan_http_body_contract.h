// Fan-only EOF receipt checks. WinHTTP retains responsibility for transfer framing.
#pragma once
#include <cstdint>
#include <limits>
#include <string_view>
namespace ymcc::fan_http {
inline std::string_view trim(std::string_view s) {
    while(!s.empty()&&(s.front()==' '||s.front()=='\t'||s.front()=='\r'))s.remove_prefix(1);
    while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'))s.remove_suffix(1);
    return s;
}
inline bool equalAscii(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();i++){char c=a[i];if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');if(c!=b[i])return false;}
    return true;
}
inline bool responseBodyComplete(std::string_view method,unsigned long status,std::string_view headers,size_t bytes) {
    if(equalAscii(method,"head")||(status>=100&&status<200)||status==204||status==304)return bytes==0;
    bool declared=false,transferEncoding=false;uint64_t expected=0;
    while(!headers.empty()){
        const auto newline=headers.find('\n');const auto line=trim(headers.substr(0,newline));
        if(newline==std::string_view::npos)headers={};else headers.remove_prefix(newline+1);
        const auto colon=line.find(':');if(colon==std::string_view::npos)continue;
        const auto name=trim(line.substr(0,colon)),value=trim(line.substr(colon+1));
        if(equalAscii(name,"transfer-encoding")){transferEncoding=true;continue;}
        if(!equalAscii(name,"content-length"))continue;
        if(value.empty())return false;
        uint64_t length=0;
        for(const unsigned char ch:value){
            if(ch<'0'||ch>'9')return false;
            const auto digit=static_cast<uint64_t>(ch-'0');
            if(length>((std::numeric_limits<uint64_t>::max)()-digit)/10)return false;
            length=length*10+digit;
        }
        if(declared&&length!=expected)return false;
        declared=true;expected=length;
    }
    // Absent length retains existing chunked/EOF compatibility. A declared
    // length must match actual bytes; conflicting framing cannot prove a receipt.
    return !declared||(!transferEncoding&&expected==bytes);
}
}
