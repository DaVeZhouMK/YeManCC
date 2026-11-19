#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <stdexcept>
namespace CssSourceFixture {
inline std::wstring sourceName;
inline HANDLE WINAPI sourceMutex(LPSECURITY_ATTRIBUTES a,BOOL b,LPCWSTR n){
 if(!n||std::wstring_view(n).find(L"Local\\YeManCC.SteamSource.v1.")!=0)return ::CreateMutexW(a,b,n);
 // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_MUTEX_BEGIN
#ifdef YMCC_CSS_SHARED_IDENTITY_ONLY
 if(sourceName.empty())sourceName=L"Local\\YMCCFixture.FormalDocumentConsumerR214."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
#else
 if(sourceName.empty())sourceName=L"Local\\YMCCFixture.CssInventorySource."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
#endif
 // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_MUTEX_END
 return ::CreateMutexW(a,b,sourceName.c_str());
}
}
#define CreateMutexW CssSourceFixture::sourceMutex
#include "steam_source_owner.h"
#undef CreateMutexW
namespace CssSourceFixture {
inline std::unique_ptr<ymcc::steamsession::SourceOwner> owner,blocker;
inline void start(bool denied=false){
 if(owner)return;
 if(denied){blocker=std::make_unique<ymcc::steamsession::SourceOwner>();if(!blocker->claim())throw std::runtime_error("actual owned source blocker");}
 owner=std::make_unique<ymcc::steamsession::SourceOwner>();if(owner->claim()==denied)throw std::runtime_error("actual fixture source ownership");
 if(!denied)owner->resumeReads();
}
inline auto read(){return owner?owner->read():ymcc::steamsession::SourceOwner::Read{};}
inline auto defaultRead(){start();return read();}
}
