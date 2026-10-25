#pragma once
// One bounded settings migration. Never launches/stops Steam, CSS or Desktop.
#include "steam_cssloader_guard.h"
#include <fstream>
namespace ymcc::steamcss {
struct RetiredLinks { unsigned retired=0, failed=0; };
inline RetiredLinks retireLegacyLinks(const std::wstring& powerControl) {
    RetiredLinks result;
    try {
        const auto directory=(std::filesystem::absolute(powerControl)/L"YeManSteam").lexically_normal();
        const auto attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)return result;
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){++result.failed;return result;}
        const auto backend=(std::filesystem::absolute(powerControl)/L"steam-appearance"/L"backend.path").lexically_normal();
        unsigned visited=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)) {
            if(++visited>512){++result.failed;break;}
            const auto source=entry.path().lexically_normal();
            if(source.parent_path()!=directory){++result.failed;continue;}
            auto name=source.filename().wstring();for(auto& c:name)c=std::towlower(c);
            if(!name.ends_with(L".txt")&&!name.ends_with(L".txt.disabled"))continue;
            const auto config=readLink(source.wstring());
            if(config.executable.empty()&&name!=L"cssloader-standalone-headless.txt"&&name!=L"cssloader-standalone-headless.txt.disabled")continue;
            // Carry a portable user's backend path into the dedicated config;
            // do not replace an already customized appearance backend path.
            const auto current=readLink(backend.wstring());
            if(!config.executable.empty()&&(current.executable.empty()||_wcsicmp(current.executable.c_str(),LR"(C:\SOFT\CSSLoader Desktop\CssLoader-Standalone-Headless.exe)")==0)) {
                if(current.executable!=config.executable) {
                    if(GetFileAttributesW(backend.parent_path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT){++result.failed;continue;}
                    const auto temporary=backend.wstring()+L".migration-"+std::to_wstring(GetCurrentProcessId());
                    const int size=WideCharToMultiByte(CP_UTF8,0,config.executable.data(),static_cast<int>(config.executable.size()),nullptr,0,nullptr,nullptr);
                    std::string bytes(size,'\0');WideCharToMultiByte(CP_UTF8,0,config.executable.data(),static_cast<int>(config.executable.size()),bytes.data(),size,nullptr,nullptr);
                    HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(output==INVALID_HANDLE_VALUE){++result.failed;continue;}
                    DWORD written=0;const bool saved=WriteFile(output,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(output);CloseHandle(output);
                    if(!saved){DeleteFileW(temporary.c_str());++result.failed;continue;}
                    if(!MoveFileExW(temporary.c_str(),backend.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());++result.failed;continue;}
                }
            }
            // Outside *.txt and custom-*.txt.disabled discovery. Preserve raw
            // bytes for recovery; never overwrite an earlier retired record.
            auto retired=source.wstring()+L".ymcc-retired";
            if(GetFileAttributesW(retired.c_str())!=INVALID_FILE_ATTRIBUTES)retired+=L"-"+std::to_wstring(GetTickCount64());
            if(MoveFileExW(source.c_str(),retired.c_str(),MOVEFILE_WRITE_THROUGH))++result.retired;else ++result.failed;
        }
    }catch(...){++result.failed;}
    return result;
}
}
