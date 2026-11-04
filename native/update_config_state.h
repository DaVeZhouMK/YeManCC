#pragma once
// Writable updater preferences/diagnostics are never installation authority.
// Consumers supply their existing atomic writer; no new worker or idle timer.
#include <windows.h>
#include "json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
namespace ymcc::updateconfig {
using Json=nlohmann::json;
enum class Kind { missing,ready,damaged,unsupported,unavailable };
struct Image {Kind kind=Kind::unavailable;std::string raw;Json value;BY_HANDLE_FILE_INFORMATION info{};};
inline bool integer(const Json& v,std::uint64_t max=(std::numeric_limits<std::int64_t>::max)()) {
    if(!v.is_number_integer())return false;
    if(v.is_number_unsigned())return v.get<std::uint64_t>()<=max;
    const auto n=v.get<std::int64_t>();return n>=0&&static_cast<std::uint64_t>(n)<=max;
}
inline bool sameInfo(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow&&
        a.nFileSizeHigh==b.nFileSizeHigh&&a.nFileSizeLow==b.nFileSizeLow&&a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime&&a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime;
}
inline bool sameImage(const Image& a,const Image& b){return a.kind==b.kind&&(a.kind==Kind::missing||(sameInfo(a.info,b.info)&&a.raw==b.raw));}
inline bool text(const Json& v,std::size_t max=4096){return v.is_string()&&v.get_ref<const std::string&>().size()<=max&&v.get_ref<const std::string&>().find('\0')==std::string::npos;}
inline bool version(const std::string& value){unsigned parts=0;std::uint64_t n=0;std::size_t digits=0;char first=0;
    for(char c:value){if(c=='.'){if(!digits||(digits>1&&first=='0')||++parts>2)return false;n=0;digits=0;}
        else{if(c<'0'||c>'9')return false;if(!digits)first=c;if(++digits>10)return false;n=n*10+static_cast<unsigned>(c-'0');if(n>2147483647ULL)return false;}}
    return parts==2&&digits>0&&(digits==1||first!='0');
}
inline bool sha(const std::string& value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
inline std::string decode(const std::string& raw){
    if(raw.size()>=2&&((static_cast<unsigned char>(raw[0])==255&&static_cast<unsigned char>(raw[1])==254)||(static_cast<unsigned char>(raw[0])==254&&static_cast<unsigned char>(raw[1])==255))){
        if((raw.size()-2)%2)throw std::runtime_error("odd-utf16");const bool be=static_cast<unsigned char>(raw[0])==254;std::wstring wide;
        for(std::size_t i=2;i<raw.size();i+=2){const auto a=static_cast<unsigned char>(raw[i]),b=static_cast<unsigned char>(raw[i+1]);wide+=static_cast<wchar_t>(be?(a<<8)|b:a|(b<<8));}
        const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);if(count<=0)throw std::runtime_error("invalid-utf16");std::string out(count,'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),out.data(),count,nullptr,nullptr)!=count)throw std::runtime_error("invalid-utf16");return out;
    }
    if(raw.starts_with(std::string("\xef\xbb\xbf",3)))return raw.substr(3);return raw;
}
inline Json parse(const std::string& raw,bool* future=nullptr) noexcept {
    struct Invalid{};if(future)*future=false;
    try{std::array<std::set<std::string>,33> keys;std::string rootKey;const auto bytes=decode(raw);
        return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& v){if(depth<0||depth>32)throw Invalid{};
            if(event==Json::parse_event_t::object_start)keys[depth].clear();
            if(event==Json::parse_event_t::key){const auto key=v.get<std::string>();if(depth==0||!keys[depth-1].insert(key).second)throw Invalid{};if(depth==1)rootKey=key;}
            if(future&&event==Json::parse_event_t::value&&depth==1&&(rootKey=="schema"||rootKey=="schemaVersion")&&integer(v,(std::numeric_limits<std::uint64_t>::max)())&&v>1)*future=true;
            return true;},false);
    }catch(...){return Json(Json::value_t::discarded);}
}
inline bool ordinaryParents(const std::filesystem::path& path){auto parent=path.parent_path();unsigned count=0;
    while(!parent.empty()){if(++count>64)return false;const auto flags=GetFileAttributesW(parent.c_str());if(flags!=INVALID_FILE_ATTRIBUTES){if(!(flags&FILE_ATTRIBUTE_DIRECTORY)||(flags&FILE_ATTRIBUTE_REPARSE_POINT))return false;}
        else{const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return false;}const auto next=parent.parent_path();if(next==parent)break;parent=next;}return true;
}
inline Image read(const std::wstring& input,std::size_t maximum) noexcept {
    Image out;try{const std::filesystem::path path(input);if(!path.is_absolute()||!ordinaryParents(path))return out;
        HANDLE file=CreateFileW(input.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)out.kind=Kind::missing;return out;}
        struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{file};
        if(GetFileType(file)!=FILE_TYPE_DISK||!GetFileInformationByHandle(file,&out.info)||(out.info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return out;
        const bool oversized=out.info.nFileSizeHigh||out.info.nFileSizeLow>maximum;out.raw.resize(oversized?maximum:out.info.nFileSizeLow);DWORD got=0;
        if(!ReadFile(file,out.raw.data(),static_cast<DWORD>(out.raw.size()),&got,nullptr)||got!=out.raw.size())return out;
        BY_HANDLE_FILE_INFORMATION after{};if(!GetFileInformationByHandle(file,&after)||!sameInfo(out.info,after))return out;
        bool future=false;out.value=parse(out.raw,&future);if(future){out.kind=Kind::unsupported;return out;}
        if(oversized||!out.value.is_object()){out.kind=Kind::damaged;return out;}
        for(const auto* key:{"schema","schemaVersion"})if(out.value.contains(key)&&(!integer(out.value[key],1)||out.value[key]!=1)){out.kind=Kind::damaged;return out;}
        out.kind=Kind::ready;return out;
    }catch(...){return out;}
}
inline std::wstring suffix(){static std::atomic<unsigned long long> serial{0};return std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial);}
inline void archive(const std::wstring& path,std::size_t maximum){const auto before=read(path,maximum);
    if(before.kind==Kind::missing)return;if(before.kind==Kind::unavailable)throw std::runtime_error("Cannot read updater preimage");
    const auto target=path+L".ymcc-invalid-"+suffix();if(!sameImage(before,read(path,maximum)))throw std::runtime_error("Updater preimage changed");
    if(!MoveFileExW(path.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve updater preimage");
    if(!sameImage(before,read(target,maximum))||read(path,maximum).kind!=Kind::missing)throw std::runtime_error("Updater preimage preservation unconfirmed");
}
inline bool policyValid(const Json& p){try{
    if(!p.is_object()||!p.contains("schemaVersion")||!integer(p["schemaVersion"],1)||p["schemaVersion"]!=1)return false;
    if(p.contains("enabled")&&!p["enabled"].is_boolean())return false;
    for(const auto* key:{"failureCount","nextCheckAt","cacheQuietUntil"})if(p.contains(key)&&!integer(p[key],std::string(key)=="failureCount"?32:(std::numeric_limits<std::int64_t>::max)()))return false;
    if(p.contains("lastNotifiedBoot")&&!text(p["lastNotifiedBoot"],255))return false;
    if(p.contains("targetVersion")&&(!text(p["targetVersion"],128)||!version(p["targetVersion"].get<std::string>())))return false;
    if(p.contains("targetSha256")&&(!text(p["targetSha256"],64)||!sha(p["targetSha256"].get<std::string>())))return false;
    return p.contains("targetVersion")==p.contains("targetSha256");
}catch(...){return false;}}
inline Json policyDefaults(bool enabled){return{{"schemaVersion",1},{"enabled",enabled},{"failureCount",0},{"nextCheckAt",0},{"lastNotifiedBoot",""}};}
inline bool diagnosticsValid(const Json& p,bool transaction=false){try{
    if(!p.is_object())return false;
    for(const auto* key:{"phase","stage","version","sha256","operationId","message","error","url","pauseReason","token"})if(p.contains(key)&&!text(p[key],std::string(key)=="message"||std::string(key)=="error"?16384:4096))return false;
    if(p.contains("version")&&!p["version"].get_ref<const std::string&>().empty()&&!version(p["version"].get<std::string>()))return false;
    if(p.contains("sha256")&&!p["sha256"].get_ref<const std::string&>().empty()&&!sha(p["sha256"].get<std::string>()))return false;
    if(p.contains("percent")&&(!p["percent"].is_number()||!std::isfinite(p["percent"].get<double>())||p["percent"]<0||p["percent"]>100))return false;
    for(const auto* key:{"downloadedBytes","totalBytes","resumedBytes","etaSeconds","updatedAt","pid","attempt","retryMax","retryInSeconds"})if(p.contains(key)&&!integer(p[key],std::string(key)=="pid"?0xffffffffULL:(std::numeric_limits<std::int64_t>::max)()))return false;
    if(p.contains("speedBps")&&(!p["speedBps"].is_number()||!std::isfinite(p["speedBps"].get<double>())||p["speedBps"]<0))return false;
    // Writable records cannot self-grant protected installation permission.
    if(p.contains("repairAuthorized")||p.contains("installerActive"))return false;
    if(transaction&&p.contains("phase")&&p["phase"]=="committed"&&(!p.contains("version")||!p.contains("pid")||p["pid"]==0))return false;
    return true;
}catch(...){return false;}}
inline bool resumeValid(const Json& p,const std::string& url,const std::string& hash,const std::string& v){try{
    if(!p.is_object()||!p.contains("url")||!text(p["url"],8192)||p["url"]!=url||!p.contains("sha256")||!text(p["sha256"],64)||!sha(p["sha256"].get<std::string>())||
        !p.contains("version")||!text(p["version"],128)||p["version"]!=v)return false;
    auto a=p["sha256"].get<std::string>(),b=hash;const auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};std::transform(a.begin(),a.end(),a.begin(),lower);std::transform(b.begin(),b.end(),b.begin(),lower);if(a!=b)return false;
    for(const auto* key:{"etag","lastModified"})if(p.contains(key)&&(!text(p[key],4096)||p[key].get_ref<const std::string&>().find_first_of("\r\n")!=std::string::npos))return false;
    return !p.contains("totalBytes")||integer(p["totalBytes"],(std::numeric_limits<std::uint64_t>::max)());
}catch(...){return false;}}
// Atomically preserve damaged files before an explicit repair. Valid preference
// preimages are copied and verified; caller's existing writer owns durable commit.
template<class Validator,class Writer>
inline void save(const std::wstring& path,Json next,std::size_t maximum,Validator valid,Writer writer,bool preserveValid=false){
    next.erase("configReadError");if(!valid(next))throw std::runtime_error("Invalid updater configuration");
    const auto before=read(path,maximum);if(before.kind==Kind::unsupported||before.kind==Kind::unavailable)throw std::runtime_error("Updater configuration source is protected");
    const bool damaged=before.kind==Kind::damaged||(before.kind==Kind::ready&&!valid(before.value));std::wstring archive;bool moved=false;
    if(before.kind==Kind::ready&&!damaged){
        const std::set<std::string> owned={"schema","schemaVersion","enabled","failureCount","nextCheckAt","cacheQuietUntil","lastNotifiedBoot","targetVersion","targetSha256","url","sha256","version","etag","lastModified","totalBytes","phase","stage","operationId","message","error","pauseReason","token","percent","downloadedBytes","resumedBytes","speedBps","etaSeconds","updatedAt","pid","repairAuthorized","installerActive","configReadError","snapshotPersistenceError","attempt","retryMax","retryInSeconds"};
        for(auto it=before.value.begin();it!=before.value.end();++it)if(!owned.contains(it.key())&&!next.contains(it.key()))next[it.key()]=*it;
    }
    const auto text=next.dump();if(text.size()>maximum||!valid(next))throw std::runtime_error("Merged updater configuration invalid");
    if(damaged||preserveValid&&before.kind==Kind::ready){archive=path+(damaged?L".ymcc-invalid-":L".ymcc-before-")+suffix();
        if(!sameImage(before,read(path,maximum)))throw std::runtime_error("Updater source changed before preservation");
        if(damaged){if(!MoveFileExW(path.c_str(),archive.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve damaged updater configuration");moved=true;
            if(!sameImage(before,read(archive,maximum))||read(path,maximum).kind!=Kind::missing)throw std::runtime_error("Updater archive not confirmed");}
        else{if(!CopyFileW(path.c_str(),archive.c_str(),TRUE))throw std::runtime_error("Cannot preserve updater preference preimage");const auto copy=read(archive,maximum);if(copy.kind!=before.kind||copy.raw!=before.raw)throw std::runtime_error("Updater preference backup differs");}
    }
    if((!moved&&!sameImage(before,read(path,maximum)))||(moved&&read(path,maximum).kind!=Kind::missing))throw std::runtime_error("Updater source changed before publication");
    bool written=false;try{written=writer(path,text);}catch(...){if(moved&&read(path,maximum).kind==Kind::missing)MoveFileExW(archive.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH);throw;}
    if(!written){if(moved&&read(path,maximum).kind==Kind::missing)MoveFileExW(archive.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH);throw std::runtime_error("Cannot persist updater configuration");}
    const auto actual=read(path,maximum);if(actual.kind!=Kind::ready||actual.raw!=text||!valid(actual.value))throw std::runtime_error("Updater configuration readback failed");
}
} // namespace ymcc::updateconfig
