#pragma once
// Additive memory API leaves for the original complete Native IPC consumer.
// The actual inventory owner, complete lambdas and path helpers are unmodified.
#include <string_view>
namespace exactfacts {
inline std::vector<std::pair<std::wstring,std::wstring>> files;
inline unsigned imageOpens=0,fileOpens=0;
inline const std::wstring wanted=L"Q:\\FixtureOnly\\Steam\\steam.exe";
inline void resetFiles(){files.clear();files.push_back({wanted,wanted});files.push_back({L"Q:\\FixtureOnly\\Alias\\link.exe",wanted});files.push_back({L"Q:\\FixtureOnly\\Elsewhere\\steam.exe",L"Q:\\FixtureOnly\\Elsewhere\\steam.exe"});for(const auto& row:osfacts::s.rows)if(!row.path.empty())files.push_back({row.path,row.path});imageOpens=fileOpens=0;}
inline HANDLE WINAPI openProcess(DWORD access,BOOL inherit,DWORD pid){
 ++imageOpens;if(access!=PROCESS_QUERY_LIMITED_INFORMATION||inherit){++osfacts::s.violations;return nullptr;}
 for(const auto& row:osfacts::s.rows)if(row.pid==pid){if(row.openFail){osfacts::s.error=ERROR_ACCESS_DENIED;return nullptr;}++osfacts::s.opens;osfacts::Held held;held.row=row;return osfacts::hold(held);}osfacts::s.error=ERROR_INVALID_PARAMETER;return nullptr;
}
inline HANDLE WINAPI openFile(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE model){
 ++fileOpens;if(!path||access||share!=(FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE)||security||creation!=OPEN_EXISTING||flags!=FILE_ATTRIBUTE_NORMAL||model){++osfacts::s.violations;return INVALID_HANDLE_VALUE;}
 for(const auto& [name,canonical]:files)if(_wcsicmp(name.c_str(),path)==0){osfacts::Held held;held.row.path=canonical;return osfacts::hold(held);}osfacts::s.error=ERROR_FILE_NOT_FOUND;return INVALID_HANDLE_VALUE;
}
inline DWORD WINAPI finalPath(HANDLE handle,LPWSTR output,DWORD capacity,DWORD flags){
 auto* held=osfacts::lookup(handle);if(!held||flags!=FILE_NAME_NORMALIZED){++osfacts::s.violations;return 0;}
 const auto value=std::wstring(L"\\\\?\\")+held->row.path;if(value.size()>=capacity)return static_cast<DWORD>(value.size()+1);std::copy(value.begin(),value.end(),output);output[value.size()]=0;return static_cast<DWORD>(value.size());
}
}
#undef OpenProcess
#define OpenProcess exactfacts::openProcess
#define CreateFileW exactfacts::openFile
#define GetFinalPathNameByHandleW exactfacts::finalPath
