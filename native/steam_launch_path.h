#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include <filesystem>
#include <cwctype>
namespace ymcc::steamlaunch {
namespace pathfs=std::filesystem;
inline std::wstring lower(std::wstring value){std::replace(value.begin(),value.end(),L'/',L'\\');for(auto& c:value)c=std::towlower(c);return value;}
inline std::wstring trim(std::wstring value){const auto first=value.find_first_not_of(L" \r\n\t\ufeff");if(first==std::wstring::npos)return {};return value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1);}
inline bool validExe(const std::wstring& value){
    if(value.size()<7||value.find_first_of(L"\"\r\n\0",0,4)!=std::wstring::npos)return false;
    const pathfs::path path(value);if(!path.is_absolute()||lower(path.extension().wstring())!=L".exe")return false;
    const auto attr=GetFileAttributesW(path.c_str());return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY);
}
}
