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
#include "update_config_state.h"
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
// A protected ACL establishes origin, not JSON validity. Keep the byte and
// field contracts strict before any phase or process identity is consumed.
inline bool journalKeys(const Json& j,unsigned depth=0){
    if(depth>32)return false;
    if(j.is_object()){std::set<std::string> seen;for(auto it=j.begin();it!=j.end();++it){auto key=it.key();for(char& c:key)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');if(!seen.insert(key).second||!journalKeys(*it,depth+1))return false;}}
    else if(j.is_array()){for(const auto& item:j)if(!journalKeys(item,depth+1))return false;}return true;
}
inline bool journalValid(const Json& j){try{
    using namespace ymcc::updateconfig;
    if(!j.is_object()||!journalKeys(j)||!j.contains("schema")||!integer(j["schema"],1)||j["schema"]!=1||j.value("owner",std::string{})!="YMCC.FullInstall")return false;
    if(j.contains("schemaVersion")&&(!integer(j["schemaVersion"],1)||j["schemaVersion"]!=1))return false;
    for(const auto* key:{"operationId","version","sha256","exePath","powerControl","phase"})if(!j.contains(key)||!text(j[key],key==std::string("operationId")?128:32768)||j[key].get_ref<const std::string&>().empty())return false;
    if(!version(j["version"].get<std::string>())||!validSha(j["sha256"].get<std::string>())||normalized(wide(j["exePath"].get<std::string>())).empty()||normalized(wide(j["powerControl"].get<std::string>())).empty())return false;
    const auto phase=j["phase"].get<std::string>();if(phase!="authorized"&&phase!="preparing"&&phase!="applying"&&phase!="repair-required"&&phase!="committed"&&phase!="rolled-back"&&phase!="failed")return false;
    for(const auto* key:{"authorizedAt","updatedAt"})if(j.contains(key)&&!integer(j[key]))return false;
    for(const auto* key:{"launcherPid","helperPid"})if(j.contains(key)&&!integer(j[key],MAXDWORD))return false;
    for(const auto* key:{"launcherCreation","helperCreation"})if(j.contains(key)&&(!text(j[key],20)||j[key].get_ref<const std::string&>().empty()||!std::all_of(j[key].get_ref<const std::string&>().begin(),j[key].get_ref<const std::string&>().end(),[](char c){return c>='0'&&c<='9';})))return false;
    for(const auto* key:{"launcherImage","helperImage"})if(j.contains(key)&&(!text(j[key],32768)||normalized(wide(j[key].get<std::string>())).empty()))return false;
    return true;
}catch(...){return false;}}
inline bool matches(const Json& j,const std::wstring& exe,const std::wstring& pc){
    try{return journalValid(j)&&!normalized(exe).empty()&&!normalized(pc).empty()&&
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
    if(h==INVALID_HANDLE_VALUE)return Json::object();
    struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{h};
    try{
        LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION before{},after{};
        if(!secureHandle(h)||GetFileType(h)!=FILE_TYPE_DISK||!GetFileInformationByHandle(h,&before)||(before.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!GetFileSizeEx(h,&size)||size.QuadPart<=0||size.QuadPart>65536)return Json::object();
        std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0,extraCount=0;char extra{};
        if(!ReadFile(h,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)||count!=text.size()||!ReadFile(h,&extra,1,&extraCount,nullptr)||extraCount||!GetFileInformationByHandle(h,&after)||!ymcc::updateconfig::sameInfo(before,after))return Json::object();
        auto result=ymcc::updateconfig::parse(text);return journalValid(result)?result:Json::object();
    }catch(...){return Json::object();}
}
inline Json read(const std::wstring& exe,const std::wstring& pc){try{auto root=directory();auto j=readAt(root,pathFor(root,exe));return matches(j,exe,pc)?j:Json::object();}catch(...){return Json::object();}}
// New authorizations and phase records always use an explicit protected DACL.
inline bool writeProtectedFile(const std::wstring& path,const std::string& bytes){
    auto sd=descriptor();SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE|READ_CONTROL,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);LocalFree(sd);
    if(h==INVALID_HANDLE_VALUE)return false;
    struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{h};DWORD written=0;
    return secureHandle(h)&&WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(h);
}
inline std::wstring writeAt(const std::wstring& root,const std::wstring& exe,const std::wstring& pc,const std::string& id,const std::string& version,const std::string& sha){
    if(id.empty()||id.size()>128||id.find('\0')!=std::string::npos||!validSha(sha)||!ymcc::updateconfig::version(version)||normalized(exe).empty()||normalized(pc).empty())throw std::runtime_error("UPDATE_RECOVERY_ID_INVALID");
    // The user-authorized incoming installation is the new baseline. Legacy
    // journals cannot veto it; an actually active installer still owns the lock.
    struct MaintenanceLease{
        HANDLE handle=nullptr;bool held=false;
        MaintenanceLease(){handle=CreateMutexW(nullptr,FALSE,L"Global\\YeManCC.ModuleMaintenance");if(!handle)throw std::runtime_error("UPDATE_RECOVERY_MAINTENANCE_UNAVAILABLE");
            const auto wait=WaitForSingleObject(handle,0);held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;if(!held){CloseHandle(handle);handle=nullptr;throw std::runtime_error("UPDATE_RECOVERY_INSTALLER_ACTIVE");}}
        ~MaintenanceLease(){if(handle){if(held)ReleaseMutex(handle);CloseHandle(handle);}}
    } maintenance;
    const auto retire=[](const std::wstring& old){const auto retired=old+L".ymcc-retired-"+ymcc::updateconfig::suffix();
        for(unsigned attempt=0;attempt<6;++attempt){if(MoveFileExW(old.c_str(),retired.c_str(),MOVEFILE_WRITE_THROUGH))return;
            const auto error=GetLastError();if((error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION&&error!=ERROR_ACCESS_DENIED)||attempt==5)throw std::runtime_error("UPDATE_RECOVERY_LEGACY_PATH_BUSY");Sleep(50);}};
    if(normalized(root).empty()||std::filesystem::path(root).parent_path().empty()||!ymcc::updateconfig::ordinaryParents(std::filesystem::path(root)))throw std::runtime_error("UPDATE_RECOVERY_PATH_UNSAFE");
    if(!secureDirectory(root,true)){
        if(GetFileAttributesW(root.c_str())!=INVALID_FILE_ATTRIBUTES)retire(root);
        if(!secureDirectory(root,true))throw std::runtime_error("UPDATE_RECOVERY_DIRECTORY_NOT_PROTECTED");
    }
    const auto path=pathFor(root,exe);
    const auto flags=GetFileAttributesW(path.c_str());
    if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))retire(path);
    auto before=ymcc::updateconfig::read(path,65536);
    for(unsigned attempt=0;before.kind==ymcc::updateconfig::Kind::unavailable&&attempt<5;++attempt){Sleep(50);before=ymcc::updateconfig::read(path,65536);}
    if(before.kind==ymcc::updateconfig::Kind::unavailable)throw std::runtime_error("UPDATE_RECOVERY_SOURCE_UNAVAILABLE");
    if(before.kind!=ymcc::updateconfig::Kind::missing&&(before.info.dwFileAttributes&FILE_ATTRIBUTE_READONLY)){
        if(!SetFileAttributesW(path.c_str(),before.info.dwFileAttributes&~FILE_ATTRIBUTE_READONLY))throw std::runtime_error("UPDATE_RECOVERY_LEGACY_ATTRIBUTES_UNAVAILABLE");
    }
    if(before.kind==ymcc::updateconfig::Kind::ready&&matches(before.value,exe,pc)&&readAt(root,path)==before.value&&live(before.value))throw std::runtime_error("UPDATE_RECOVERY_INSTALLER_ACTIVE");
    Json j={{"schema",1},{"owner","YMCC.FullInstall"},{"phase","authorized"},{"operationId",id},{"version",version},{"sha256",sha},
        {"exePath",utf8(normalized(exe))},{"powerControl",utf8(normalized(pc))},{"authorizedAt",now()},{"launcherPid",GetCurrentProcessId()},{"launcherImage",utf8(exe)}};
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("UPDATE_RECOVERY_LAUNCHER_UNKNOWN");j["launcherCreation"]=std::to_string(filetime(c));
    // Never merge historical fields or require a historical preimage backup.
    // Corrupt/oversized/future records are overwritten only by this fresh,
    // protected authorization for the verified incoming package.
    if(!journalValid(j))throw std::runtime_error("UPDATE_RECOVERY_ID_INVALID");const auto text=j.dump();if(text.size()>65536)throw std::runtime_error("UPDATE_RECOVERY_JOURNAL_TOO_LARGE");
    const auto temp=path+L".tmp-"+ymcc::updateconfig::suffix();
    bool ok=writeProtectedFile(temp,text);
    if(ok)ok=readAt(root,temp)==j&&ymcc::updateconfig::read(temp,65536).raw==text&&ymcc::updateconfig::sameImage(before,ymcc::updateconfig::read(path,65536))&&secureDirectory(root,false);
    if(ok){
        ok=false;
        for(unsigned attempt=0;attempt<6;++attempt){
            // A temporary reader may deny replacement. Revalidate the captured
            // original on every retry; never replace a changed transaction.
            const auto current=ymcc::updateconfig::read(path,65536);
            if(!ymcc::updateconfig::sameImage(before,current)||!secureDirectory(root,false))break;
            if(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ok=true;break;}
            const auto error=GetLastError();
            if((error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION&&error!=ERROR_ACCESS_DENIED)||attempt==5)break;
            Sleep(50);
        }
    }
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("UPDATE_RECOVERY_COMMIT_FAILED");}
    if(readAt(root,path)!=j||ymcc::updateconfig::read(path,65536).raw!=text)throw std::runtime_error("UPDATE_RECOVERY_READBACK_FAILED");
    return path;
}
inline std::wstring write(const std::wstring& exe,const std::wstring& pc,const std::string& id,const std::string& version,const std::string& sha){return writeAt(directory(),exe,pc,id,version,sha);}
}
