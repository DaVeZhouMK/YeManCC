#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ymcc::hwinfo {
struct Error:std::runtime_error {
    int code; bool elevation; DWORD win32;
    Error(int c,const char* text,bool elevate=false,DWORD e=0):std::runtime_error(text),code(c),elevation(elevate),win32(e){}
};
inline void require(bool ok,const char* text){if(!ok)throw Error(8,text);}
inline bool periodAllowed(unsigned p){return p==0||p==1000||p==1500||p==2000||p==3000;}
inline std::wstring trim(std::wstring s){const auto a=s.find_first_not_of(L" \t\r\n\v\f");if(a==std::wstring::npos)return {};return s.substr(a,s.find_last_not_of(L" \t\r\n\v\f")-a+1);}
inline std::wstring lower(std::wstring s){if(s.empty())return s;const auto count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr,0);require(count>0,"INI key case mapping failed");std::wstring out(count,L'\0');require(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),static_cast<int>(s.size()),out.data(),count,nullptr,nullptr,0)==count,"INI key case mapping failed");return out;}
inline bool number(const std::wstring& raw,unsigned& n){auto s=trim(raw);if(s.empty())return false;std::uint64_t v=0;std::size_t i=s[0]==L'+'?1:0;if(i==s.size())return false;for(;i<s.size();++i){if(s[i]<L'0'||s[i]>L'9')return false;v=v*10+(s[i]-L'0');if(v>INT_MAX)return false;}n=static_cast<unsigned>(v);return true;}
struct Ini {
    enum Encoding{Ansi,Utf8,Utf16LE,Utf16BE} encoding=Ansi;
    std::string bytes;
    std::wstring text;
    std::vector<std::wstring> lines;
    std::size_t preamble=0;
};
inline bool validUtf16(const std::wstring&s){for(std::size_t i=0;i<s.size();++i){auto c=static_cast<unsigned>(s[i]);if(c>=0xd800&&c<=0xdbff){if(++i>=s.size()||s[i]<0xdc00||s[i]>0xdfff)return false;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;}
inline Ini decodeIni(std::string bytes){
    require(bytes.size()<=262144,"HWiNFO INI exceeds the 256 KiB configuration limit");
    Ini out;out.bytes=std::move(bytes);
    if(out.bytes.size()>=3&&out.bytes.compare(0,3,"\xef\xbb\xbf")==0){out.encoding=Ini::Utf8;out.preamble=3;}
    else if(out.bytes.size()>=2&&out.bytes.compare(0,2,"\xff\xfe")==0){out.encoding=Ini::Utf16LE;out.preamble=2;}
    else if(out.bytes.size()>=2&&out.bytes.compare(0,2,"\xfe\xff")==0){out.encoding=Ini::Utf16BE;out.preamble=2;}
    const auto size=out.bytes.size()-out.preamble;
    if(out.encoding==Ini::Utf16LE||out.encoding==Ini::Utf16BE){
        require(size%2==0,"HWiNFO INI has malformed UTF-16 bytes");out.text.reserve(size/2);
        for(std::size_t i=out.preamble;i<out.bytes.size();i+=2){const auto a=static_cast<unsigned char>(out.bytes[i]),b=static_cast<unsigned char>(out.bytes[i+1]);out.text.push_back(static_cast<wchar_t>(out.encoding==Ini::Utf16LE?a|(b<<8):(a<<8)|b));}
        require(validUtf16(out.text),"HWiNFO INI has invalid UTF-16 characters");
    }else if(size){
        const UINT cp=out.encoding==Ini::Utf8?CP_UTF8:CP_ACP;
        const auto count=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,out.bytes.data()+out.preamble,static_cast<int>(size),nullptr,0);
        require(count>0,"HWiNFO INI has invalid encoded characters");out.text.resize(count);
        require(MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,out.bytes.data()+out.preamble,static_cast<int>(size),out.text.data(),count)==count,"HWiNFO INI decoding failed");
    }
    require(out.text.find(L'\0')==std::wstring::npos,"HWiNFO INI contains an embedded zero");
    std::size_t at=0;for(std::size_t i=0;i<out.text.size();++i){if(out.text[i]!=L'\r'&&out.text[i]!=L'\n')continue;out.lines.push_back(out.text.substr(at,i-at));if(out.text[i]==L'\r'&&i+1<out.text.size()&&out.text[i+1]==L'\n')++i;at=i+1;require(out.lines.size()<=8192,"HWiNFO INI exceeds the line limit");}
    out.lines.push_back(out.text.substr(at));require(out.lines.size()<=8192,"HWiNFO INI exceeds the line limit");return out;
}
inline std::string encodeIni(const Ini& ini,const std::wstring& text){
    require(validUtf16(text),"Merged HWiNFO INI has invalid UTF-16");std::string out=ini.bytes.substr(0,ini.preamble);
    if(ini.encoding==Ini::Utf16LE||ini.encoding==Ini::Utf16BE){for(wchar_t c:text){const auto n=static_cast<unsigned>(c);out.push_back(static_cast<char>(ini.encoding==Ini::Utf16LE?n&255:n>>8));out.push_back(static_cast<char>(ini.encoding==Ini::Utf16LE?n>>8:n&255));}}
    else if(!text.empty()){
        const UINT cp=ini.encoding==Ini::Utf8?CP_UTF8:CP_ACP;const bool utf8=cp==CP_UTF8||GetACP()==CP_UTF8;BOOL substituted=FALSE;const DWORD flags=utf8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
        auto size=WideCharToMultiByte(cp,flags,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,utf8?nullptr:&substituted);
        require(size>0&&!substituted,"Merged INI cannot be represented in its original encoding");const auto pos=out.size();out.resize(pos+size);substituted=FALSE;
        require(WideCharToMultiByte(cp,flags,text.data(),static_cast<int>(text.size()),out.data()+pos,size,nullptr,utf8?nullptr:&substituted)==size&&!substituted,"Merged INI encoding failed");
    }
    require(out.size()<=262144,"Merged HWiNFO INI exceeds the configuration limit");return out;
}
struct Entry{std::wstring section,key,value,identity;std::size_t index;};
inline std::optional<std::wstring> sectionOf(const std::wstring& line){const auto s=trim(line);if(s.empty()||s.front()!=L'[')return {};const auto end=s.find(L']');if(end==std::wstring::npos||end==1)return {};const auto tail=trim(s.substr(end+1));if(!tail.empty()&&tail[0]!=L';'&&tail[0]!=L'#')return {};return trim(s.substr(1,end-1));}
inline std::vector<Entry> entries(const Ini& ini){
    std::vector<Entry> out;std::map<std::wstring,bool> sections,seen;std::wstring section;
    for(std::size_t i=0;i<ini.lines.size();++i){const auto& line=ini.lines[i];if(auto next=sectionOf(line)){section=*next;require(!sections[lower(section)],"HWiNFO INI contains a repeated section");sections[lower(section)]=true;continue;}
        auto s=trim(line);if(s.empty()||s[0]==L';'||s[0]==L'#')continue;const auto eq=line.find(L'=');if(eq==std::wstring::npos)continue;const auto key=trim(line.substr(0,eq));if(key.empty())continue;
        require(!section.empty(),"HWiNFO INI contains a value outside a section");const auto identity=lower(section)+L'\x1f'+lower(key);require(!seen[identity],"HWiNFO INI contains a duplicate section/key pair");seen[identity]=true;out.push_back({section,key,line.substr(eq+1),identity,i});
    }return out;
}
inline Ini managedIni(std::optional<bool> snapshotPolling={}){
    // User-supplied stable background template, embedded in the YMCC image.
    // Historical Average is not consumed by YMCC, FanHost or our RTSS templates.
    // Use the default incremental average; keep current/Presented metrics intact.
    // Enabling sharing does not alter HWiNFO licensing or its free timeout.
    std::string text="[Settings]\r\nSensorsOnly=1\r\nNegativeTempsOK=1\r\nMinimalizeSensors=1\r\nMinimalizeSensorsClose=1\r\nAutoUpdate=0\r\nShowFans0RPM=1\r\nMinimalizeMainWnd=1\r\nShowWelcomeAndProgress=0\r\nPreferAMDADL=1\r\nTheme=1\r\nSensorsFontFace=Segoe UI\r\nSensorsFontHeight=17\r\nSensorsSM=1\r\nWakeGPUs=0\r\nAC97CodecID=0\r\niMEsupport=0\r\nSVID_VR=0\r\nHighestIdeAddress=-1\r\nCSMI_SAS_Support=0\r\nSWSMI=0\r\nUseHPET=0\r\nBusClkPolling=0\r\nLPC=0\r\nEC=0\r\nSMART=0\r\nATAStats=0\r\nSupportCorsairAsetek=0\r\nDebugDirect=0\r\nSMBus=0\r\nGPUI2C=0\r\nEcScanRate=5000\r\nSmartScanRate=5000\r\nSensorInterval=3000\r\nSensorAveragingInterval=0\r\nPresentMon=1\r\nNvClockMeasuring=0\r\nUSBC_PD=0\r\n";
    if(snapshotPolling)text+=std::string("SnapshotPolling=")+(*snapshotPolling?"1":"0")+"\r\n";
    auto ini=decodeIni(text);ini.encoding=Ini::Utf8;return ini;
}
inline std::string mergeIni(const Ini& source,const std::optional<Ini>& existing,unsigned requested=0){
    require(periodAllowed(requested),"Unsupported HWiNFO polling interval");const auto desired=entries(source);const auto& target=existing?*existing:source;const auto current=entries(target);
    auto lines=target.lines;std::map<std::wstring,Entry> positions;for(const auto&e:current)positions[e.identity]=e;
    std::map<std::wstring,std::vector<std::wstring>> missing;std::vector<std::wstring> order;std::map<std::wstring,std::wstring> spelling;bool periodSeen=false;
    const auto addMissing=[&](const std::wstring& section,const std::wstring& line){const auto key=lower(section);if(!missing.count(key)){order.push_back(key);spelling[key]=section;}missing[key].push_back(line);};
    for(const auto&e:desired){auto value=e.value;const bool isPeriod=lower(e.section)==L"settings"&&lower(e.key)==L"sensorinterval";auto old=positions.find(e.identity);
        if(isPeriod){periodSeen=true;unsigned n=0;if(requested)value=std::to_wstring(requested);else if(old!=positions.end()&&number(old->second.value,n)&&n>=1000&&n<=3000)value=old->second.value;require(number(value,n)&&n>=1000&&n<=3000,"HWiNFO period exceeds the monitor freshness budget");}
        const auto line=e.key+L'='+value;if(old!=positions.end())lines[old->second.index]=line;else addMissing(e.section,line);
    }
    if(!periodSeen){const auto old=positions.find(L"settings\x1fsensorinterval");if(requested){const auto line=L"SensorInterval="+std::to_wstring(requested);if(old!=positions.end())lines[old->second.index]=line;else addMissing(L"Settings",line);}else if(old!=positions.end()){unsigned n=0;require(number(old->second.value,n)&&n>=1000&&n<=3000,"Unmanaged HWiNFO period exceeds the monitor freshness budget");}}
    std::vector<std::wstring> merged;std::map<std::wstring,bool> finished;std::wstring section;
    const auto flush=[&](){if(section.empty())return;const auto k=lower(section);if(missing.count(k))for(const auto& line:missing[k])merged.push_back(line);finished[k]=true;};
    for(const auto&line:lines){if(auto next=sectionOf(line)){flush();section=*next;}merged.push_back(line);}flush();for(const auto& key:order)if(!finished[key]){merged.push_back(L"["+spelling[key]+L"]");for(const auto&line:missing[key])merged.push_back(line);}
    require(merged.size()<=8192,"Merged HWiNFO INI exceeds the line limit");const std::wstring newline=target.text.find(L"\r\n")!=std::wstring::npos?L"\r\n":target.text.find(L'\r')!=std::wstring::npos?L"\r":L"\n";std::wstring text;
    for(std::size_t i=0;i<merged.size();++i){if(i)text+=newline;text+=merged[i];}return encodeIni(target,text);
}
}
