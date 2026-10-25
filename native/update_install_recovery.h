#pragma once
// A same-version retry is authorized only by an unfinished administrator-owned
// transaction. LocalAppData progress, manifests and helper hashes are not authority.
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <cctype>
#include <cwctype>
#include "json.hpp"
namespace ymcc::updaterecovery {
using Json = nlohmann::json;
inline std::string utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr); return r;
}
inline std::wstring wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring r(n,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n); return r;
}
inline std::wstring normalized(const std::wstring& path) {
    if(path.empty() || !std::filesystem::path(path).is_absolute()) return {};
    auto r=std::filesystem::path(path).lexically_normal().wstring();
    while(r.size()>3 && (r.back()==L'\\'||r.back()==L'/'))r.pop_back();
    std::transform(r.begin(),r.end(),r.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return r;
}
inline uint64_t filetime(FILETIME t){return (static_cast<uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
inline int64_t now(){FILETIME t{};GetSystemTimeAsFileTime(&t);return static_cast<int64_t>(filetime(t)/10000)-11644473600000LL;}
inline bool validSha(const std::string& s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return isxdigit(c)!=0;});}
inline bool unfinished(const Json& j){try{auto p=j.value("phase",std::string{});return p=="authorized"||p=="preparing"||p=="applying"||p=="repair-required";}catch(...){return false;}}
inline bool same(const std::string& a,const std::string& b){return _stricmp(a.c_str(),b.c_str())==0;}
inline bool matches(const Json& j,const std::wstring& exe,const std::wstring& pc){
    try{return j.is_object()&&j.value("schema",0)==1&&j.value("owner",std::string{})=="YMCC.FullInstall"&&
        !j.value("operationId",std::string{}).empty()&&j.at("operationId").get<std::string>().size()<=128&&
        !j.value("version",std::string{}).empty()&&validSha(j.value("sha256",std::string{}))&&
        normalized(wide(j.value("exePath",std::string{})))==normalized(exe)&&
        normalized(wide(j.value("powerControl",std::string{})))==normalized(pc);
    }catch(...){return false;}
}
inline bool authorizes(const Json& j,const std::wstring& exe,const std::wstring& pc,const std::string& version,const std::string& sha){
    return matches(j,exe,pc)&&unfinished(j)&&j.value("version",std::string{})==version&&same(j.value("sha256",std::string{}),sha);
}
inline bool liveProcess(const Json& j,const char* prefix){
    try{
        std::string p(prefix);DWORD pid=j.value(p+"Pid",0u);if(!pid)return false;
        HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!h)return false;
        FILETIME created{},exit{},kernel{},user{};wchar_t image[32768]{};DWORD length=32768;
        const bool live=WaitForSingleObject(h,0)==WAIT_TIMEOUT&&GetProcessTimes(h,&created,&exit,&kernel,&user)&&
            std::to_string(filetime(created))==j.value(p+"Creation",std::string{})&&QueryFullProcessImageNameW(h,0,image,&length)&&
            normalized(std::wstring(image,length))==normalized(wide(j.value(p+"Image",std::string{})));
        CloseHandle(h);return live;
    }catch(...){return false;}
}
inline bool live(const Json& j){
    if(!unfinished(j))return false;
    if(liveProcess(j,"helper"))return true;
    try { const auto age=now()-j.value("authorizedAt",int64_t{});
    return j.value("phase",std::string{})=="authorized"&&age>=0&&age<15000&&liveProcess(j,"launcher"); }catch(...){return false;}
}
inline bool sidIs(PSID sid,WELL_KNOWN_SID_TYPE kind){return sid&&IsWellKnownSid(sid,kind);}
inline bool protectedDescriptor(PSECURITY_DESCRIPTOR sd){
    PSID owner=nullptr;BOOL d=false,present=false;PACL acl=nullptr;SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;
    if(!GetSecurityDescriptorOwner(sd,&owner,&d)||(!sidIs(owner,WinBuiltinAdministratorsSid)&&!sidIs(owner,WinLocalSystemSid))||
        !GetSecurityDescriptorControl(sd,&control,&revision)||!(control&SE_DACL_PROTECTED)||
        !GetSecurityDescriptorDacl(sd,&present,&acl,&d)||!present||!acl||!acl->AceCount)return false;
    bool admin=false,system=false;
    for(DWORD i=0;i<acl->AceCount;++i){void* raw=nullptr;if(!GetAce(acl,i,&raw))return false;auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE)return false;
        auto sid=reinterpret_cast<PSID>(&ace->SidStart);bool a=sidIs(sid,WinBuiltinAdministratorsSid),s=sidIs(sid,WinLocalSystemSid);
        if(!a&&!s)return false;if(a)admin=true;if(s)system=true;
    }return admin&&system;
}
inline bool secureHandle(HANDLE h){
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    PSECURITY_DESCRIPTOR sd=nullptr;DWORD err=GetSecurityInfo(h,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&sd);
    bool ok=err==ERROR_SUCCESS&&protectedDescriptor(sd);if(sd)LocalFree(sd);return ok;
}
inline PSECURITY_DESCRIPTOR descriptor(){PSECURITY_DESCRIPTOR sd=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&sd,nullptr))throw std::runtime_error("UPDATE_RECOVERY_SECURITY_UNAVAILABLE");return sd;
}
inline bool secureDirectory(const std::wstring& root,bool create){
    if(create){auto sd=descriptor();SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};BOOL ok=CreateDirectoryW(root.c_str(),&sa);DWORD err=GetLastError();LocalFree(sd);if(!ok&&err!=ERROR_ALREADY_EXISTS)return false;}
    HANDLE h=CreateFileW(root.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;bool ok=secureHandle(h);CloseHandle(h);return ok;
}
inline std::wstring directory(){PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&p)))throw std::runtime_error("UPDATE_RECOVERY_DIRECTORY_UNAVAILABLE");
    std::wstring root=p;CoTaskMemFree(p);return root+L"\\YeManCC.UpdateRecovery";
}
inline std::wstring pathFor(const std::wstring& root,const std::wstring& exe){
    uint64_t hash=14695981039346656037ULL;for(wchar_t c:normalized(exe)){hash^=static_cast<uint16_t>(c);hash*=1099511628211ULL;}
    std::wostringstream name;name<<std::hex<<hash;return root+L"\\"+name.str()+L".json";
}
inline Json readAt(const std::wstring& root,const std::wstring& path){
    if(!secureDirectory(root,false))return Json::object();
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)return Json::object();Json result=Json::object();
    LARGE_INTEGER size{};if(secureHandle(h)&&GetFileSizeEx(h,&size)&&size.QuadPart>0&&size.QuadPart<=65536){
        std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0;
        if(ReadFile(h,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)&&count==text.size())try{result=Json::parse(text);}catch(...){}
    }CloseHandle(h);return result;
}
inline Json read(const std::wstring& exe,const std::wstring& pc){try{auto root=directory();auto j=readAt(root,pathFor(root,exe));return matches(j,exe,pc)?j:Json::object();}catch(...){return Json::object();}}
inline std::wstring write(const std::wstring& exe,const std::wstring& pc,const std::string& id,const std::string& version,const std::string& sha){
    if(id.empty()||id.size()>128||!validSha(sha))throw std::runtime_error("UPDATE_RECOVERY_ID_INVALID");
    auto root=directory();if(!secureDirectory(root,true))throw std::runtime_error("UPDATE_RECOVERY_DIRECTORY_NOT_PROTECTED");
    auto path=pathFor(root,exe);Json j={{"schema",1},{"owner","YMCC.FullInstall"},{"phase","authorized"},{"operationId",id},{"version",version},{"sha256",sha},
        {"exePath",utf8(normalized(exe))},{"powerControl",utf8(normalized(pc))},{"authorizedAt",now()},{"launcherPid",GetCurrentProcessId()},{"launcherImage",utf8(exe)}};
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("UPDATE_RECOVERY_LAUNCHER_UNKNOWN");j["launcherCreation"]=std::to_string(filetime(c));
    auto temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());auto sd=descriptor();SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE|READ_CONTROL,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);LocalFree(sd);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("UPDATE_RECOVERY_WRITE_FAILED");auto text=j.dump();DWORD written=0;
    bool ok=secureHandle(h)&&WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(h);CloseHandle(h);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("UPDATE_RECOVERY_COMMIT_FAILED");}return path;
}
}

