#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_target_return_record.h"
#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
// devpkey.h intentionally permits re-inclusion for INITGUID. The product TU
// already defines these keys; do not initialize them a second time here.
#ifndef DEVPKEY_H_INCLUDED
#include <devpkey.h>
#endif
#include <array>
#include <set>

// A single bounded, read-only observation. Absent means only that this complete
// present-device snapshot has neither the exact IDs nor the supported families.
// It grants no removal, unhide, ownership, parent/provenance or recovery authority.
namespace ymcc::targetReturn::pnpAbsence {
inline constexpr unsigned maxNodes=1024,maxKnownExactIds=1024;
inline constexpr std::size_t maxHardwareIdBytes=16*1024;
enum class State { Unknown, Present, Absent };
enum class Reason {
    InvalidKnownIds, OpenFailed, EnumerationFailed, EnumerationDataInvalid,
    NodeLimit, MissingDevinst, RepeatedDevinst, DeviceIdReadFailed,
    DeviceIdMissing, DeviceIdTruncated, DeviceIdInvalid, RepeatedInstance,
    HardwareIdsReadFailed, HardwareIdsTruncated, HardwareIdsType,
    HardwareIdsShape, ExactIdPresent, SupportedFamilyPresent,
    CompleteAbsence, CleanupFailed, ReaderException
};
struct Result {
    State state=State::Unknown;
    Reason reason=Reason::ReaderException;
    unsigned scannedNodes=0;
    DWORD win32Error=ERROR_SUCCESS;
    CONFIGRET configurationReturn=CR_SUCCESS;
    // Bounded failure metadata, never input/ownership proof or write authority.
    std::string failedInstanceId;
    ULONG deviceIdChars=0;
    std::vector<unsigned> deviceIdHead,deviceIdTail;
    ULONG hardwareIdsBytes=0;
    DEVPROPTYPE hardwareIdsType=DEVPROP_TYPE_EMPTY;
    std::vector<unsigned> hardwareIdsHead,hardwareIdsTail;
    bool absent()const noexcept{return state==State::Absent;}
};
inline const char* stateName(State s)noexcept{
    switch(s){case State::Absent:return "absent";case State::Present:return "present";default:return "unknown";}
}
inline const char* reasonName(Reason r)noexcept{
    switch(r){
    case Reason::InvalidKnownIds:return "invalid-known-exact-ids";
    case Reason::OpenFailed:return "present-set-open-failed";
    case Reason::EnumerationFailed:return "enumeration-ended-with-error";
    case Reason::EnumerationDataInvalid:return "enumeration-data-invalid";
    case Reason::NodeLimit:return "node-limit-before-complete-end";
    case Reason::MissingDevinst:return "missing-devinst";
    case Reason::RepeatedDevinst:return "repeated-devinst";
    case Reason::DeviceIdReadFailed:return "device-id-read-failed";
    case Reason::DeviceIdMissing:return "device-id-empty";
    case Reason::DeviceIdTruncated:return "device-id-unterminated";
    case Reason::DeviceIdInvalid:return "device-id-invalid";
    case Reason::RepeatedInstance:return "repeated-instance-id";
    case Reason::HardwareIdsReadFailed:return "hardware-ids-read-failed";
    case Reason::HardwareIdsTruncated:return "hardware-ids-buffer-too-small";
    case Reason::HardwareIdsType:return "hardware-ids-type-invalid";
    case Reason::HardwareIdsShape:return "hardware-ids-multisz-invalid";
    case Reason::ExactIdPresent:return "exact-historical-id-present";
    case Reason::SupportedFamilyPresent:return "supported-target-family-present";
    case Reason::CompleteAbsence:return "complete-present-snapshot-without-target";
    case Reason::CleanupFailed:return "present-set-close-failed";
    default:return "reader-or-allocation-exception";
    }
}
struct ProductionReader {
    HDEVINFO getClassDevs(const GUID* category,PCWSTR enumerator,HWND window,DWORD flags){return SetupDiGetClassDevsW(category,enumerator,window,flags);}
    BOOL enumerate(HDEVINFO set,DWORD index,PSP_DEVINFO_DATA info){return SetupDiEnumDeviceInfo(set,index,info);}
    BOOL destroy(HDEVINFO set){return SetupDiDestroyDeviceInfoList(set);}
    CONFIGRET deviceId(DEVINST node,PWCHAR id,ULONG chars,ULONG flags){return CM_Get_Device_IDW(node,id,chars,flags);}
    CONFIGRET hardwareIds(DEVINST node,const DEVPROPKEY* key,DEVPROPTYPE* type,PBYTE buffer,PULONG bytes,ULONG flags){return CM_Get_DevNode_PropertyW(node,key,type,buffer,bytes,flags);}
    DWORD error(){return GetLastError();}
};
namespace leafDetail {
inline bool space(wchar_t c)noexcept{
    return (c>=L'\t'&&c<=L'\r')||c==L' '||c==0x85||c==0xa0||c==0x1680||
        (c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;
}
inline std::wstring_view trim(std::wstring_view text)noexcept{
    while(!text.empty()&&space(text.front()))text.remove_prefix(1);
    while(!text.empty()&&space(text.back()))text.remove_suffix(1);
    return text;
}
inline std::wstring fold(std::wstring_view text){std::wstring out(text);for(auto& c:out)if(c>=L'a'&&c<=L'z')c=wchar_t(c-L'a'+L'A');return out;}
inline bool supported(std::wstring_view text){
    const auto exact=fold(text);
    if(exact==L"ROOT\\HIDMAESTRO"||exact==L"ROOT\\HIDMAESTROXUSB")return true;
    const auto normalized=fold(trim(text));
    return normalized==L"USB\\VID_28DE&PID_1205"||
        (normalized.rfind(L"USB\\VID_28DE&PID_1205&",0)==0&&normalized.find(L"&MI_")==std::wstring::npos);
}
inline bool utf16(std::wstring_view text)noexcept{
    for(std::size_t i=0;i<text.size();++i){const auto c=std::uint32_t(text[i]);
        if((c<0x20||c==0x7f)&&!space(wchar_t(c)))return false;
        if(c>=0xd800&&c<=0xdbff){if(i+1>=text.size())return false;const auto low=std::uint32_t(text[++i]);if(low<0xdc00||low>0xdfff)return false;}
        else if(c>=0xdc00&&c<=0xdfff)return false;
    }return true;
}
// Parse the entire returned byte range before admitting a family match. A valid
// first entry cannot conceal an unterminated tail, empty middle or junk suffix.
inline bool multiSz(const wchar_t* data,ULONG bytes,bool& family){
    family=false;
    // CM can return a valid empty DEVPROP_TYPE_STRING_LIST as a single UTF-16
    // terminator (observed on Windows LampArray nodes). Nonempty lists still
    // require their member terminator plus the final empty-list terminator.
    if(bytes==sizeof(wchar_t))return data[0]==L'\0';
    if(bytes<2*sizeof(wchar_t)||bytes>maxHardwareIdBytes||bytes%sizeof(wchar_t))return false;
    const auto count=std::size_t(bytes/sizeof(wchar_t));
    if(data[count-1]!=L'\0'||data[count-2]!=L'\0')return false;
    if(data[0]==L'\0')return count==2;
    std::size_t index=0;
    while(index<count-1){
        const auto begin=index;while(index<count&&data[index]!=L'\0')++index;
        if(index==count||index==begin)return false;
        const std::wstring_view member(data+begin,index-begin);
        if(!utf16(member)||trim(member).empty())return false;
        family=family||supported(member);++index;
    }
    return index==count-1;
}
// OS-observed IDs are opaque Unicode identifiers, not persisted write targets.
// Keep the record/path authority validator strict; only this read-only census
// uses ordinal UTF-16 keys and continues to inspect every HardwareIds property.
struct OrdinalLess {
    bool operator()(const std::wstring& a,const std::wstring& b)const {
        const int order=CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE);
        if(!order)throw std::runtime_error("device-id-ordinal-compare-failed");
        return order==CSTR_LESS_THAN;
    }
};
inline bool observedInstance(std::wstring_view id)noexcept{
    if(id.empty()||id.size()>maxInstanceBytes||!utf16(id))return false;
    for(auto c:id)if(c<0x20||c==0x7f)return false;
    return true;
}
inline std::string instanceUtf8(std::wstring_view id){
    const int chars=int(id.size());
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id.data(),chars,nullptr,0,nullptr,nullptr);
    if(!bytes)throw std::runtime_error("device-id-utf8-size-failed");
    std::string result(std::size_t(bytes),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id.data(),chars,result.data(),bytes,nullptr,nullptr)!=bytes)
        throw std::runtime_error("device-id-utf8-conversion-failed");
    return result;
}
template<class Reader> struct Set {
    Reader& reader;HDEVINFO value;DWORD closeError=ERROR_SUCCESS;
    bool close()noexcept{
        if(value==INVALID_HANDLE_VALUE||!value)return true;
        const auto owned=value;value=INVALID_HANDLE_VALUE;
        try{if(reader.destroy(owned))return true;closeError=reader.error();}
        catch(...){closeError=ERROR_GEN_FAILURE;}
        return false;
    }
    ~Set()noexcept{close();}
};
}
template<class Reader>
inline Result verify(const std::vector<std::string>& knownExactIds,Reader& reader){
    unsigned scanned=0;
    try{
        if(knownExactIds.size()>maxKnownExactIds)return {State::Unknown,Reason::InvalidKnownIds};
        std::set<std::string> known;
        for(const auto& id:knownExactIds)
            if(!ymcc::targetReturn::detail::instance(id)||!known.insert(ymcc::targetReturn::detail::fold(id)).second)
                return {State::Unknown,Reason::InvalidKnownIds};
        const auto handle=reader.getClassDevs(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
        if(handle==INVALID_HANDLE_VALUE||!handle)return {State::Unknown,Reason::OpenFailed,0,reader.error()};
        leafDetail::Set<Reader> owner{reader,handle};
        const auto finish=[&](Result result){if(!owner.close())return Result{State::Unknown,Reason::CleanupFailed,scanned,owner.closeError};return result;};
        std::set<DEVINST> nodes;std::set<std::wstring,leafDetail::OrdinalLess> identities;
        for(unsigned index=0;index<=maxNodes;++index){
            SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
            if(!reader.enumerate(handle,index,&info)){
                const DWORD error=reader.error();
                if(error==ERROR_NO_MORE_ITEMS)return finish({State::Absent,Reason::CompleteAbsence,scanned});
                return finish({State::Unknown,Reason::EnumerationFailed,scanned,error});
            }
            if(index==maxNodes)return finish({State::Unknown,Reason::NodeLimit,scanned});
            ++scanned;
            if(info.cbSize!=sizeof(info))return finish({State::Unknown,Reason::EnumerationDataInvalid,scanned});
            if(!info.DevInst)return finish({State::Unknown,Reason::MissingDevinst,scanned});
            if(!nodes.insert(info.DevInst).second)return finish({State::Unknown,Reason::RepeatedDevinst,scanned});
            std::array<wchar_t,maxInstanceBytes+1> id;id.fill(wchar_t(0xffff));
            const auto idRead=reader.deviceId(info.DevInst,id.data(),ULONG(id.size()),0);
            if(idRead!=CR_SUCCESS)return finish({State::Unknown,Reason::DeviceIdReadFailed,scanned,0,idRead});
            const auto end=std::find(id.begin(),id.end(),L'\0');
            if(end==id.end())return finish({State::Unknown,Reason::DeviceIdTruncated,scanned});
            if(end==id.begin())return finish({State::Unknown,Reason::DeviceIdMissing,scanned});
            const std::wstring_view observed(id.data(),std::size_t(end-id.begin()));
            if(!leafDetail::observedInstance(observed)){
                Result invalid{State::Unknown,Reason::DeviceIdInvalid,scanned};invalid.deviceIdChars=ULONG(observed.size());
                for(std::size_t i=0;i<(std::min)(observed.size(),std::size_t{8});++i)invalid.deviceIdHead.push_back(unsigned(observed[i]));
                for(std::size_t i=observed.size()>8?observed.size()-8:0;i<observed.size();++i)invalid.deviceIdTail.push_back(unsigned(observed[i]));
                return finish(std::move(invalid));
            }
            const std::string instance=leafDetail::instanceUtf8(observed);
            const auto folded=ymcc::targetReturn::detail::fold(instance);
            if(!identities.insert(std::wstring(observed)).second)return finish({State::Unknown,Reason::RepeatedInstance,scanned});
            if(known.count(folded))return finish({State::Present,Reason::ExactIdPresent,scanned});
            std::array<wchar_t,maxHardwareIdBytes/sizeof(wchar_t)> hardware;hardware.fill(wchar_t(0xffff));
            DEVPROPTYPE type=DEVPROP_TYPE_EMPTY;ULONG bytes=ULONG(maxHardwareIdBytes);
            const auto property=reader.hardwareIds(info.DevInst,&DEVPKEY_Device_HardwareIds,&type,reinterpret_cast<PBYTE>(hardware.data()),&bytes,0);
            if(property==CR_NO_SUCH_VALUE)continue;
            if(property==CR_BUFFER_SMALL)return finish({State::Unknown,Reason::HardwareIdsTruncated,scanned,0,property});
            if(property!=CR_SUCCESS)return finish({State::Unknown,Reason::HardwareIdsReadFailed,scanned,0,property});
            const auto invalidProperty=[&](Reason reason){
                Result result{State::Unknown,reason,scanned};result.failedInstanceId=instance;
                result.hardwareIdsBytes=bytes;result.hardwareIdsType=type;
                const auto count=(std::min)(hardware.size(),std::size_t(bytes/sizeof(wchar_t)));
                for(std::size_t i=0;i<(std::min)(count,std::size_t{4});++i)result.hardwareIdsHead.push_back(unsigned(hardware[i]));
                for(std::size_t i=count>4?count-4:0;i<count;++i)result.hardwareIdsTail.push_back(unsigned(hardware[i]));
                return finish(std::move(result));
            };
            if(type!=DEVPROP_TYPE_STRING_LIST)return invalidProperty(Reason::HardwareIdsType);
            bool family=false;
            if(!leafDetail::multiSz(hardware.data(),bytes,family))return invalidProperty(Reason::HardwareIdsShape);
            if(family)return finish({State::Present,Reason::SupportedFamilyPresent,scanned});
        }
        return finish({State::Unknown,Reason::NodeLimit,scanned});
    }catch(...){return {State::Unknown,Reason::ReaderException,scanned};}
}
inline Result verify(const std::vector<std::string>& knownExactIds){ProductionReader reader;return verify(knownExactIds,reader);}
} // namespace ymcc::targetReturn::pnpAbsence
#endif
