#pragma once
// Consumer transfer only. The sole Steam factory publishes all Steam facts.
// This header is usable in the child without any producer/Observer implementation.
#include "steam_source_lease_contract.h"
#include "process_handle_inheritance.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#pragma comment(lib,"OneCore.lib") // CompareObjectHandles' actual SDK import library.

namespace ymcc::steaminfo::transfer {
inline constexpr std::uint32_t protocol=1,maximumPayload=256u*1024u;
inline constexpr wchar_t argument[]=L"--steam-source-v2";
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE h):value(h){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& h)noexcept:value(std::exchange(h.value,nullptr)){}
    Handle& operator=(Handle&& h)noexcept{if(this!=&h){reset();value=std::exchange(h.value,nullptr);}return *this;}
    ~Handle(){reset();}
    void reset()noexcept{if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
inline std::uint64_t birth(HANDLE process)noexcept{
    FILETIME b{},e{},k{},u{};
    return GetProcessTimes(process,&b,&e,&k,&u)?(std::uint64_t(b.dwHighDateTime)<<32)|b.dwLowDateTime:0;
}
inline bool live(HANDLE h)noexcept{return h&&h!=INVALID_HANDLE_VALUE&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;}
inline bool samePath(const std::wstring& a,const std::wstring& b)noexcept{
    return a.size()==b.size()&&CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
inline std::wstring image(HANDLE process){std::wstring s(32768,L'\0');DWORD n=static_cast<DWORD>(s.size());if(!QueryFullProcessImageNameW(process,0,s.data(),&n))return {};s.resize(n);return s;}
inline std::wstring module(){std::wstring s(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n||n>=s.size())return {};s.resize(n);return s;}
inline bool sameFile(const std::filesystem::path& a,const std::filesystem::path& b)noexcept{
    Handle left(CreateFileW(a.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    Handle right(CreateFileW(b.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    BY_HANDLE_FILE_INFORMATION l{},r{};
    return left.value&&right.value&&GetFileInformationByHandle(left.value,&l)&&GetFileInformationByHandle(right.value,&r)&&
        !(l.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(r.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
        l.dwVolumeSerialNumber==r.dwVolumeSerialNumber&&l.nFileIndexHigh==r.nFileIndexHigh&&l.nFileIndexLow==r.nFileIndexLow;
}
inline bool sameUser(HANDLE first,HANDLE second)noexcept{
    HANDLE a=nullptr,b=nullptr;if(!OpenProcessToken(first,TOKEN_QUERY,&a))return false;Handle ta(a);
    if(!OpenProcessToken(second,TOKEN_QUERY,&b))return false;Handle tb(b);
    DWORD na=0,nb=0;GetTokenInformation(a,TokenUser,nullptr,0,&na);GetTokenInformation(b,TokenUser,nullptr,0,&nb);
    if(!na||!nb||na>65536||nb>65536)return false;
    try{std::vector<unsigned char> va(na),vb(nb);
        return GetTokenInformation(a,TokenUser,va.data(),na,&na)&&GetTokenInformation(b,TokenUser,vb.data(),nb,&nb)&&
            IsValidSid(reinterpret_cast<TOKEN_USER*>(va.data())->User.Sid)&&IsValidSid(reinterpret_cast<TOKEN_USER*>(vb.data())->User.Sid)&&
            EqualSid(reinterpret_cast<TOKEN_USER*>(va.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(vb.data())->User.Sid);
    }catch(...){return false;}
}
// This trust shape is specific to the existing wallpaper headless launch. It
// is not permission for another launcher, arbitrary sender or installer action.
// directParentProven is the successful original power-channel attach (same
// held parent, birth, native creator and Windows session), never a CLI field.
inline bool trustedWallpaperParent(HANDLE parent,DWORD pid,std::uint64_t born,
        const std::filesystem::path& appRoot,bool directParentProven)noexcept{
    try{
        if(!directParentProven||!pid||!born||!live(parent)||GetProcessId(parent)!=pid||birth(parent)!=born)return false;
        const auto own=std::filesystem::path(module()),actual=std::filesystem::path(image(parent));
        if(own.empty()||actual.empty()||!samePath(own.filename().wstring(),L"CustomSteamLibrary.exe")||
           !samePath(own.parent_path().filename().wstring(),L"CustomSteamLibrary")||
           !sameFile(own,appRoot/L"CustomSteamLibrary.exe"))return false;
        const auto expected=own.parent_path().parent_path()/L"YeManCC.exe";
        if(!samePath(actual.lexically_normal().wstring(),expected.lexically_normal().wstring())||!sameFile(actual,expected))return false;
        DWORD a=MAXDWORD,b=MAXDWORD;
        if(!ProcessIdToSessionId(pid,&a)||!ProcessIdToSessionId(GetCurrentProcessId(),&b)||a!=b||!sameUser(parent,GetCurrentProcess()))return false;
        return live(parent)&&GetProcessId(parent)==pid&&birth(parent)==born;
    }catch(...){return false;}
}
struct Frame {
    std::uint32_t magic=0x32564c53,version=protocol,headerBytes=sizeof(Frame),payloadBytes=0;
    std::uint32_t providerPid=0,session=0;
    std::uint64_t providerBirth=0,reserved=0;
};
static_assert(sizeof(Frame)==40&&offsetof(Frame,providerBirth)==24);
inline std::uint64_t number(const Json& j,const char* name,std::uint64_t maximum){
    const auto& v=j.at(name);if(!v.is_number_integer()||(!v.is_number_unsigned()&&v.get<std::int64_t>()<0))throw 0;
    const auto n=v.get<std::uint64_t>();if(n>maximum)throw 0;return n;
}
inline bool usable(const SourceSnapshot& s)noexcept{
    const auto& f=s.facts;const bool ui=s.selection==Selection::UiLocal,registry=s.selection==Selection::RegistryCandidate;
    return f.available&&f.presence==1&&f.processPid&&f.processBirth&&!f.processPath.empty()&&f.rootError==ERROR_SUCCESS&&!f.root.empty()&&
        f.identityRevision&&f.processEpoch&&f.accountEpoch&&s.sourceRevision&&s.modeRevision&&s.powerGeneration&&
        (s.mode=="desktop"||s.mode=="bigpicture")&&!s.modeEpoch.empty()&&s.modeEpoch.size()<=96&&s.selectedAccount&&
        ((ui&&f.uiAccountKnown&&f.uiAccount==s.selectedAccount)||(registry&&f.accountError==ERROR_SUCCESS&&f.registryAccount==s.selectedAccount));
}
inline Json encodeSource(const SourceSnapshot& s){return {{"facts",encode(s.facts)},{"sourceRevision",s.sourceRevision},
    {"modeRevision",s.modeRevision},{"powerGeneration",s.powerGeneration},{"mode",s.mode},{"modeEpoch",s.modeEpoch},
    {"selection",static_cast<std::uint32_t>(s.selection)},{"selectedAccount",s.selectedAccount}};}
inline std::optional<SourceSnapshot> decodeSource(const Json& j)noexcept{
    try{
        if(!j.is_object()||j.size()!=8)return {};
        const auto& raw=j.at("facts");const auto requested=static_cast<std::uint32_t>(number(raw,"requested",All));
        const auto facts=decode(raw,requested);if(!facts)return {};
        SourceSnapshot s;s.facts=*facts;s.sourceRevision=number(j,"sourceRevision",UINT64_MAX);
        s.modeRevision=number(j,"modeRevision",UINT64_MAX);s.powerGeneration=number(j,"powerGeneration",UINT64_MAX);
        s.mode=j.at("mode").get<std::string>();s.modeEpoch=j.at("modeEpoch").get<std::string>();
        s.selection=static_cast<Selection>(number(j,"selection",static_cast<std::uint32_t>(Selection::UiLocal)));
        s.selectedAccount=static_cast<std::uint32_t>(number(j,"selectedAccount",UINT32_MAX));
        if(!usable(s)||s.mode.find('\0')!=std::string::npos||s.modeEpoch.find('\0')!=std::string::npos)return {};
        return s;
    }catch(...){return {};}
}
class Export final {
    std::shared_ptr<SourceLeaseV2> source_;
    std::unique_lock<std::recursive_mutex> inheritance_;
    std::array<Handle,5> owned_;
    bool closing_=false,inheritanceFailed_=false;
    bool prepare(){
        if(!source_||!source_->current()||!usable(source_->snapshot()))return false;
        if(!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&owned_[3].value,
                PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,TRUE,0)||!live(owned_[3].value)||
           !CompareObjectHandles(source_->providerProcess(),owned_[3].value))return false;
        const auto& events=source_->cancellationEvents();
        for(std::size_t i=0;i<3;++i){
            if(!live(events[i]))return false;
            for(std::size_t k=0;k<i;++k)if(CompareObjectHandles(events[i],events[k]))return false;
            if(!DuplicateHandle(GetCurrentProcess(),events[i],GetCurrentProcess(),&owned_[i].value,SYNCHRONIZE,TRUE,0))return false;
        }
        const auto text=encodeSource(source_->snapshot()).dump();if(text.empty()||text.size()>maximumPayload)return false;
        Frame header;header.payloadBytes=static_cast<std::uint32_t>(text.size());header.providerPid=GetCurrentProcessId();
        DWORD session=0;header.providerBirth=birth(owned_[3].value);if(!header.providerBirth||!ProcessIdToSessionId(header.providerPid,&session))return false;header.session=session;
        Handle map(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(sizeof(Frame)+text.size()),nullptr));
        if(!map.value)return false;
        auto* view=MapViewOfFile(map.value,FILE_MAP_WRITE,0,0,sizeof(Frame)+text.size());if(!view)return false;
        std::memcpy(view,&header,sizeof header);std::memcpy(static_cast<unsigned char*>(view)+sizeof header,text.data(),text.size());
        const bool unmapped=UnmapViewOfFile(view)!=FALSE;if(!unmapped)return false;
        if(!DuplicateHandle(GetCurrentProcess(),map.value,GetCurrentProcess(),&owned_[4].value,FILE_MAP_READ,TRUE,0))return false;
        // Close the last writable mapping handle before publishing the frame.
        map.reset();return current();
    }
    explicit Export(std::shared_ptr<SourceLeaseV2> source):source_(std::move(source)),inheritance_(ymcc::processInheritance::owner()){}
public:
    Export(const Export&)=delete;Export& operator=(const Export&)=delete;
    static std::unique_ptr<Export> capture(std::shared_ptr<SourceLeaseV2> source)noexcept{
        try{auto result=std::unique_ptr<Export>(new Export(std::move(source)));if(!result->prepare())return {};return result;}catch(...){return {};}
    }
    bool sealInheritance()noexcept{
        if(!inheritance_.owns_lock())return !inheritanceFailed_;
        bool ok=true;for(auto& h:owned_)if(h.value&&!SetHandleInformation(h.value,HANDLE_FLAG_INHERIT,0)){
            // Only our dedicated copy is affected. Failure seals this export.
            ok=false;
            if(CloseHandle(h.value))h.value=nullptr; // Keep a failed close in this owner.
        }
        if(!ok)inheritanceFailed_=true;
        if(inheritance_.owns_lock())inheritance_.unlock();return ok&&!inheritanceFailed_;
    }
    // The original cleanup thread ends the inheritance window before a failed
    // return can move this ledger to another reap thread. It grants no action.
    void beginClose()noexcept{
        closing_=true;
        if(inheritance_.owns_lock())(void)sealInheritance();
    }
    // Only after child/Job return; failed copies retain this exact lease.
    bool closeOwned()noexcept{
        beginClose();
        bool closed=true;
        for(auto& h:owned_)if(h.value){
            if(CloseHandle(h.value))h.value=nullptr;
            else closed=false;
        }
        return closed;
    }
    bool current()noexcept{
        if(closing_||inheritanceFailed_||!source_||!source_->current()||!live(owned_[3].value))return false;
        for(std::size_t i=0;i<3;++i)if(!live(owned_[i].value))return false;return true;
    }
    std::array<HANDLE,5> inheritanceHandles()const noexcept{return{owned_[0].value,owned_[1].value,owned_[2].value,owned_[3].value,owned_[4].value};}
    const std::shared_ptr<SourceLeaseV2>& lease()const noexcept{return source_;}
    std::wstring arguments()const{
        std::wstring result=L" ";result+=argument;for(const auto h:inheritanceHandles())result+=L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(h));return result;
    }
};
class HandleList final {
    std::array<HANDLE,5> inherited_{};
    std::vector<unsigned char> storage_;
    bool initialized_=false;
    STARTUPINFOEXW startup_{};
public:
    explicit HandleList(const Export& source):inherited_(source.inheritanceHandles()){
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);if(!bytes)throw 0;storage_.resize(bytes);
        startup_.StartupInfo.cb=sizeof(STARTUPINFOEXW);startup_.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup_.StartupInfo.wShowWindow=SW_HIDE;
        startup_.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if(!InitializeProcThreadAttributeList(startup_.lpAttributeList,1,0,&bytes))throw 0;
        initialized_=true;
        if(!UpdateProcThreadAttribute(startup_.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited_.data(),sizeof inherited_,nullptr,nullptr)){
            DeleteProcThreadAttributeList(startup_.lpAttributeList);initialized_=false;throw 0;
        }
    }
    ~HandleList(){if(initialized_)DeleteProcThreadAttributeList(startup_.lpAttributeList);}
    HandleList(const HandleList&)=delete;HandleList& operator=(const HandleList&)=delete;
    STARTUPINFOW* startup()noexcept{return &startup_.StartupInfo;}
};
class Imported final : public SourceLeaseV2 {
    SourceSnapshot snapshot_;
    std::array<Handle,4> owned_;
    std::array<void*,3> events_{};
    std::atomic<bool> revoked_{false};
public:
    Imported(SourceSnapshot s,std::array<Handle,4>&& handles):snapshot_(std::move(s)),owned_(std::move(handles)),
        events_{owned_[0].value,owned_[1].value,owned_[2].value}{}
    const SourceSnapshot& snapshot()const noexcept override{return snapshot_;}
    const std::array<void*,3>& cancellationEvents()const noexcept override{return events_;}
    void* providerProcess()const noexcept override{return owned_[3].value;}
    bool current()noexcept override{
        if(revoked_.load(std::memory_order_acquire))return false;
        for(const auto& h:owned_)if(!live(h.value)){revoked_.store(true,std::memory_order_release);return false;}
        return !revoked_.load(std::memory_order_acquire);
    }
};
inline bool parseHandle(const wchar_t* text,HANDLE& value)noexcept{
    if(!text||!*text)return false;std::uintptr_t number=0;
    for(const wchar_t* p=text;*p;++p){if(*p<L'0'||*p>L'9'||number>((std::numeric_limits<std::uintptr_t>::max)()-(*p-L'0'))/10)return false;number=number*10+(*p-L'0');}
    value=reinterpret_cast<HANDLE>(number);return value&&value!=INVALID_HANDLE_VALUE;
}
// Takes ownership only of the five exact dedicated inherited copies. The
// original parent handle is borrowed and never closed or made inheritable.
inline std::shared_ptr<SourceLeaseV2> importWallpaper(const std::array<HANDLE,5>& incoming,
        HANDLE parent,DWORD parentPid,std::uint64_t parentBirth,const std::filesystem::path& appRoot,
        bool directParentProven)noexcept{
    std::array<Handle,4> objects;Handle mapping;
    // Adopt every unique dedicated slot before returning any parameter error.
    // The held original parent is borrowed and must never enter this owner.
    bool invalid=false;
    for(std::size_t i=0;i<incoming.size();++i){
        if(!incoming[i]||incoming[i]==INVALID_HANDLE_VALUE||incoming[i]==parent){invalid=true;continue;}
        bool duplicate=false;for(std::size_t k=0;k<i;++k)if(incoming[i]==incoming[k])duplicate=true;
        if(duplicate){invalid=true;continue;}
        if(i<4)objects[i].value=incoming[i];else mapping.value=incoming[i];
    }
    for(auto& h:objects)if(h.value&&!SetHandleInformation(h.value,HANDLE_FLAG_INHERIT,0))invalid=true;
    if(mapping.value&&!SetHandleInformation(mapping.value,HANDLE_FLAG_INHERIT,0))invalid=true;
    if(invalid)return {};
    try{
        if(!trustedWallpaperParent(parent,parentPid,parentBirth,appRoot,directParentProven)||
           !CompareObjectHandles(objects[3].value,parent)||GetProcessId(objects[3].value)!=parentPid||birth(objects[3].value)!=parentBirth)return {};
        for(std::size_t i=0;i<4;++i){if(!live(objects[i].value))return {};for(std::size_t k=0;k<i;++k)if(CompareObjectHandles(objects[i].value,objects[k].value))return {};}
        const auto* head=static_cast<const Frame*>(MapViewOfFile(mapping.value,FILE_MAP_READ,0,0,sizeof(Frame)));if(!head)return {};
        Frame frame;std::memcpy(&frame,head,sizeof frame);UnmapViewOfFile(head);
        DWORD session=MAXDWORD;
        if(frame.magic!=0x32564c53||frame.version!=protocol||frame.headerBytes!=sizeof(Frame)||!frame.payloadBytes||
           frame.payloadBytes>maximumPayload||frame.reserved||frame.providerPid!=parentPid||frame.providerBirth!=parentBirth||
           !ProcessIdToSessionId(parentPid,&session)||frame.session!=session)return {};
        const auto* view=static_cast<const unsigned char*>(MapViewOfFile(mapping.value,FILE_MAP_READ,0,0,sizeof(Frame)+frame.payloadBytes));if(!view)return {};
        std::string text;bool same=false;
        try{text.assign(reinterpret_cast<const char*>(view+sizeof(Frame)),frame.payloadBytes);same=std::memcmp(view,&frame,sizeof frame)==0;}
        catch(...){UnmapViewOfFile(view);throw;}
        UnmapViewOfFile(view);if(!same)return {};
        const auto data=decodeSource(Json::parse(text));if(!data)return {};
        auto source=std::make_shared<Imported>(*data,std::move(objects));
        return trustedWallpaperParent(parent,parentPid,parentBirth,appRoot,directParentProven)&&source->current()?source:std::shared_ptr<SourceLeaseV2>{};
    }catch(...){return {};}
}
} // namespace ymcc::steaminfo::transfer
