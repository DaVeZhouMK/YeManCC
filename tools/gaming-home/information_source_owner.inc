#pragma once
// Fixture-only owned namespace. Actual SourceOwner/OS object-existence rules
// remain product code; never contend with the installed public source slot.
#include <windows.h>
#include <memory>
#include <string>
#include <stdexcept>
namespace HomeInformationSourceFixture {
inline std::wstring name;
inline HANDLE WINAPI ownedMutex(LPSECURITY_ATTRIBUTES attributes,BOOL initialOwner,LPCWSTR requested){
 if(!requested||std::wstring_view(requested).find(L"Local\\YeManCC.SteamSource.v1.")!=0||
    name.find(L"Local\\YMCCFixture.HomeInformation.Source.")!=0){SetLastError(ERROR_INVALID_NAME);return nullptr;}
 return ::CreateMutexW(attributes,initialOwner,name.c_str());
}
}
#define CreateMutexW HomeInformationSourceFixture::ownedMutex
#include "steam_source_owner.h"
#undef CreateMutexW
namespace HomeInformationSourceFixture {
inline std::unique_ptr<ymcc::steamsession::SourceOwner> source,blocker;
inline void start(bool deny){
 name=L"Local\\YMCCFixture.HomeInformation.Source."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
 if(deny){blocker=std::make_unique<ymcc::steamsession::SourceOwner>();if(!blocker->claim())throw std::runtime_error("owned fixture source blocker");}
 source=std::make_unique<ymcc::steamsession::SourceOwner>();
 if(source->claim()==deny)throw std::runtime_error("actual fixture source claim");
 if(!deny)source->resumeReads();
}
inline auto read(){return source?source->read():ymcc::steamsession::SourceOwner::Read{};}
}
