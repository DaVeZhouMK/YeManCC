#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ymcc::gamerules {
// Both YMCC and the auxiliary library writer use the same directory identity.
// This is an action/read boundary, not a poller, watcher or retry worker.
class FilesGuard {
    HANDLE handle_ = nullptr;
    bool abandoned_ = false;
public:
    static std::wstring name(const std::wstring& directory) {
        const DWORD count=GetFullPathNameW(directory.c_str(),0,nullptr,nullptr);
        if(!count||count>32768)throw std::runtime_error("Cannot resolve game-rule directory identity");
        std::wstring path(count,L'\0');const auto actual=GetFullPathNameW(directory.c_str(),count,path.data(),nullptr);
        if(!actual||actual>=count)throw std::runtime_error("Cannot resolve game-rule directory identity");path.resize(actual);
        std::replace(path.begin(),path.end(),L'/',L'\\');while(path.size()>3&&path.back()==L'\\')path.pop_back();
        const auto foldedCount=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr,0);
        if(foldedCount<=0)throw std::runtime_error("Cannot normalize game-rule directory identity");
        std::wstring folded(static_cast<std::size_t>(foldedCount),L'\0');
        if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),static_cast<int>(path.size()),folded.data(),foldedCount,nullptr,nullptr,0)!=foldedCount)throw std::runtime_error("Cannot normalize game-rule directory identity");
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto ch:folded){hash^=static_cast<unsigned char>(ch&255);hash*=1099511628211ULL;hash^=static_cast<unsigned char>((ch>>8)&255);hash*=1099511628211ULL;}
        return L"Local\\YMCC.GameRules.Files."+std::to_wstring(hash);
    }
    explicit FilesGuard(const std::wstring& directory,DWORD timeoutMs=2000) {
        const auto identity=name(directory);handle_=CreateMutexW(nullptr,FALSE,identity.c_str());
        if(!handle_)throw std::runtime_error("Cannot open game-rule files coordination");
        const auto waited=WaitForSingleObject(handle_,timeoutMs);
        if(waited!=WAIT_OBJECT_0&&waited!=WAIT_ABANDONED){CloseHandle(handle_);handle_=nullptr;throw std::runtime_error("Game-rule files are busy; retry after the active operation");}
        abandoned_=waited==WAIT_ABANDONED;
    }
    FilesGuard(const FilesGuard&)=delete;FilesGuard& operator=(const FilesGuard&)=delete;
    ~FilesGuard(){if(handle_){ReleaseMutex(handle_);CloseHandle(handle_);}}
    bool abandoned() const noexcept{return abandoned_;}
};
// Only the native owner can interpret/complete its two-record recovery journal.
// Auxiliary writers never turn unknown debt into fresh ownership or data.
inline void requireNativeRecoveryComplete(const std::wstring& directory) {
    std::wstring root=directory;if(!root.empty()&&root.back()!=L'\\'&&root.back()!=L'/')root+=L'\\';
    for(const auto* file:{L"game-rules.transaction.json",L"game-rules.transaction.before.json"}){
        if(GetFileAttributesW((root+file).c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("YMCC game-rule recovery is pending; retry after the native owner restores its files");
        const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Cannot inspect YMCC game-rule recovery; original files retained");
    }
}
} // namespace ymcc::gamerules
