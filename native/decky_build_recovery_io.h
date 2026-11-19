#pragma once
// Existing protected native writer/maintenance lease and stable bounded reads.
// Product recovery has one running-build authority; this file supplies IO only.
#include "decky_build_recovery_contract.h"
#include "runtime_build_identity.h"
#include "update_install_recovery.h"
#include <functional>
namespace ymcc::deckyupgrade::store {
using Json=nlohmann::json;
inline constexpr std::size_t maximum=1048576;
struct Lease {
    HANDLE handle=nullptr;bool owned=false;
    explicit Lease(const std::wstring& exe){
        // Old outgoing installers still own ModuleMaintenance while the new
        // native starts and acknowledges readiness. Recovery history is outside
        // their payload and needs its own fixed per-installation writer lease.
        const auto key=std::filesystem::path(updaterecovery::pathFor(L"",exe)).stem().wstring();
        const auto name=std::wstring(L"Global\\YeManCC.DeckyBuildRecovery.")+key;
        handle=CreateMutexW(nullptr,FALSE,name.c_str());
        if(handle){const auto w=WaitForSingleObject(handle,0);owned=w==WAIT_OBJECT_0||w==WAIT_ABANDONED;}}
    ~Lease(){if(handle){if(owned)ReleaseMutex(handle);CloseHandle(handle);}}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
};
inline updateconfig::Image protectedRead(const std::wstring& root,const std::wstring& path){
    updateconfig::Image rejected;
    if(!updaterecovery::secureDirectory(root,false))return rejected;
    auto image=updateconfig::read(path,maximum);
    if(image.kind==updateconfig::Kind::missing)return image;
    if(image.kind!=updateconfig::Kind::ready)return rejected;
    HANDLE h=CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)return rejected;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=updaterecovery::secureHandle(h)&&GetFileInformationByHandle(h,&info)&&updateconfig::sameInfo(image.info,info);
    CloseHandle(h);return valid?image:rejected;
}
inline bool receipt(const Json& value){try{
    return detail::keys(value,{"schema","nativeBuildId","nativeSha256","nativeBytes"})&&value.at("schema")=="ymcc.native-build-identity.v1"&&
        value.at("nativeBuildId").is_string()&&detail::hash(value.at("nativeBuildId").get<std::string>())&&
        value.at("nativeSha256").is_string()&&detail::hash(value.at("nativeSha256").get<std::string>())&&
        updateconfig::integer(value.at("nativeBytes"),268435456)&&value.at("nativeBytes")>=64;
}catch(...){return false;}}
inline bool receiptImage(const updateconfig::Image& image){
    // Configs retain their numeric schema rule; only this bounded artifact
    // consumer recognizes the exact string schema used by the sealed receipt.
    return (image.kind==updateconfig::Kind::ready||image.kind==updateconfig::Kind::damaged)&&
        !image.info.nFileSizeHigh&&image.info.nFileSizeLow<=4096&&receipt(image.value);
}
inline bool publish(const std::wstring& root,const std::wstring& path,const updateconfig::Image& before,const Json& value,const std::function<bool()>& current){
    const auto bytes=value.dump();if(bytes.size()>maximum||!current())return false;
    const auto temp=path+L".tmp-"+updateconfig::suffix();
    bool ok=updaterecovery::writeProtectedFile(temp,bytes);
    if(ok){const auto check=protectedRead(root,temp);ok=check.kind==updateconfig::Kind::ready&&check.raw==bytes&&
        current()&&updateconfig::sameImage(before,protectedRead(root,path));}
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());return false;}
    const auto saved=protectedRead(root,path);return saved.kind==updateconfig::Kind::ready&&saved.raw==bytes&&current();
}

}
