#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "steam_installation_facts.h"
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"user32.lib")

namespace ymcc::steaminfo {
inline constexpr ULONG_PTR messageTag=0x594d5346;
inline constexpr wchar_t property[]=L"YeManCC.SharedSteamInformation.v1";
inline constexpr std::size_t payloadCapacity=65536;
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct Peer {
    Handle process;
    DWORD pid=0,session=0;
    ULONGLONG birth=0;
    std::wstring path;
    std::vector<unsigned char> user;
    bool load(DWORD id){
        if(process.value||!id)return false;
        process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,id);if(!process.value)return false;
        FILETIME born{},ended{},kernel{},cpu{};wchar_t image[32768]{};DWORD length=static_cast<DWORD>(std::size(image));
        if(GetProcessId(process.value)!=id||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT||!GetProcessTimes(process.value,&born,&ended,&kernel,&cpu)||!QueryFullProcessImageNameW(process.value,0,image,&length))return false;
        pid=id;birth=(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime;path.assign(image,length);if(!birth)return false;
        Handle token;if(!OpenProcessToken(process.value,TOKEN_QUERY,&token.value))return false;
        DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);if(!bytes||bytes>65536)return false;user.resize(bytes);
        if(!GetTokenInformation(token.value,TokenUser,user.data(),bytes,&bytes)||!GetTokenInformation(token.value,TokenSessionId,&session,sizeof(session),&bytes))return false;
        return live();
    }
    bool live()const{return process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;}
    bool sameUser(const Peer& other)const{return live()&&other.live()&&session==other.session&&!user.empty()&&!other.user.empty()&&EqualSid(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid,reinterpret_cast<const TOKEN_USER*>(other.user.data())->User.Sid);}
};
inline std::wstring pathKey(std::wstring value){std::replace(value.begin(),value.end(),L'/',L'\\');std::transform(value.begin(),value.end(),value.begin(),::towlower);while(!value.empty()&&value.back()==L'\\')value.pop_back();return value;}
struct ProviderEndpoint {
    HWND window=nullptr;
    std::unique_ptr<Peer> peer;
    bool current() const noexcept {
        DWORD pid=0;
        return window&&peer&&peer->live()&&GetWindowThreadProcessId(window,&pid)&&pid==peer->pid&&
            reinterpret_cast<ULONG_PTR>(GetPropW(window,property))==protocol;
    }
};
inline ProviderEndpoint findProvider(const Peer& caller,const std::wstring& expected) {
    struct Search {const Peer& caller;const std::wstring& expected;ProviderEndpoint found;};
    Search search{caller,expected,{}};
    const auto complete=EnumWindows([](HWND window,LPARAM opaque)->BOOL{
        auto& state=*reinterpret_cast<Search*>(opaque);
        if(reinterpret_cast<ULONG_PTR>(GetPropW(window,property))!=protocol)return TRUE;
        try{
            DWORD pid=0;
            if(!GetWindowThreadProcessId(window,&pid))return TRUE;
            auto candidate=std::make_unique<Peer>();
            if(!candidate->load(pid)||!candidate->sameUser(state.caller)||pathKey(candidate->path)!=state.expected)return TRUE;
            if(state.found.window)return FALSE; // Two matching windows, even from one PID, are ambiguous.
            state.found={window,std::move(candidate)};
            return TRUE;
        }catch(...){return FALSE;}
    },reinterpret_cast<LPARAM>(&search));
    if(!complete||!search.found.current())return {};
    return std::move(search.found);
}
inline std::wstring utf16(const std::string& value){if(value.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!n)throw 0;std::wstring result(n,0);if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n)!=n)throw 0;return result;}
inline std::string utf8(const std::wstring& value){if(value.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!n)throw 0;std::string result(n,0);if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr)!=n)throw 0;return result;}
struct Request {
    std::uint32_t version=protocol,size=sizeof(Request),mask=0,callerPid=0;
    std::uint64_t callerBirth=0;
    std::array<unsigned char,16> nonce{};
};
struct Wire {
    Request request;
    std::uint32_t capacity=payloadCapacity,bytes=0,providerPid=0;
    std::uint64_t providerBirth=0;
    volatile LONG committed=0;
    char payload[payloadCapacity]{};
};
static_assert(sizeof(Request)==40);
static_assert(sizeof(Wire)==65608&&offsetof(Wire,providerBirth)==56&&offsetof(Wire,committed)==64&&offsetof(Wire,payload)==68);
inline std::wstring mappingName(const Request& request){
    static constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring result=L"Local\\YeManCC.SteamInformation.v1."+std::to_wstring(request.callerPid)+L".";
    for(auto c:request.nonce){result+=hex[c>>4];result+=hex[c&15];}return result;
}
inline bool matches(const Request& a,const Request& b){return a.version==b.version&&a.size==b.size&&a.mask==b.mask&&a.callerPid==b.callerPid&&a.callerBirth==b.callerBirth&&a.nonce==b.nonce;}
struct View {void* value=nullptr;~View(){if(value)UnmapViewOfFile(value);}View()=default;View(const View&)=delete;};
inline bool gamingHomePackagePeer(const Peer&) noexcept;
} // namespace ymcc::steaminfo
#include "steam_home_client_identity.h"
namespace ymcc::steaminfo {
namespace information_read_detail {
inline Facts readEndpoint(const Peer& caller,const ProviderEndpoint& endpoint,std::uint32_t mask,
        DWORD expectedPid,std::uint64_t expectedBirth){
        if(!endpoint.peer)return {};
        const auto& provider=*endpoint.peer;
        if((expectedPid||expectedBirth)&&(!expectedPid||!expectedBirth||provider.pid!=expectedPid||provider.birth!=expectedBirth))return {};
        Request request;request.mask=mask;request.callerPid=caller.pid;request.callerBirth=caller.birth;
        if(BCryptGenRandom(nullptr,request.nonce.data(),static_cast<ULONG>(request.nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
        const auto name=mappingName(request);SetLastError(ERROR_SUCCESS);
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Wire),name.c_str()));
        if(!mapping.value||GetLastError()==ERROR_ALREADY_EXISTS)return {};
        {View initial;initial.value=MapViewOfFile(mapping.value,FILE_MAP_WRITE,0,0,sizeof(Wire));if(!initial.value)return {};Wire wire{};wire.request=request;std::memcpy(initial.value,&wire,sizeof(wire));}
        View result;result.value=MapViewOfFile(mapping.value,FILE_MAP_READ,0,0,sizeof(Wire));if(!result.value)return {};
        HWND source=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);if(!source)return {};
        struct Window{HWND value;~Window(){DestroyWindow(value);}} closeWindow{source};
        COPYDATASTRUCT data{messageTag,sizeof(request),&request};DWORD_PTR acknowledged=0;const auto began=GetTickCount64();
        if(!caller.live()||!endpoint.current())return {};
        if(!SendMessageTimeoutW(endpoint.window,WM_COPYDATA,reinterpret_cast<WPARAM>(source),reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,1500,&acknowledged)||acknowledged!=protocol||!caller.live()||!endpoint.current())return {};
        const auto* wire=static_cast<const Wire*>(result.value);const LONG committed=wire->committed;MemoryBarrier();
        if(committed!=1||!matches(wire->request,request)||wire->capacity!=payloadCapacity||wire->providerPid!=provider.pid||wire->providerBirth!=provider.birth||!wire->bytes||wire->bytes>=payloadCapacity)return {};
        const auto decoded=decode(Json::parse(wire->payload,wire->payload+wire->bytes),mask);const auto now=GetTickCount64();
        if(!decoded||decoded->observedAt<began||decoded->observedAt>now||now-began>2000)return {};
        return *decoded;
}
inline Facts read(std::uint32_t mask,DWORD expectedPid,std::uint64_t expectedBirth)noexcept{
    try{
        if(!mask||(mask&~Supported))return {};
        Peer caller;if(!caller.load(GetCurrentProcessId()))return {};
        const auto own=std::filesystem::path(caller.path);const auto leaf=pathKey(own.filename().wstring());
        const bool home=mask==StartupWindow&&leaf==L"yemangaminghome.exe"&&gamingHomePackagePeer(caller);
        if(!home&&(leaf!=L"customsteamlibrary.exe"&&leaf!=L"steamartworklab.exe"))return {};
        if((mask&StartupWindow)&&!home)return {};
        const auto expected=home?std::wstring(LR"(c:\soft\yeman\yemancc\yemancc.exe)"):pathKey((own.parent_path().parent_path()/L"YeManCC.exe").wstring());
        return readEndpoint(caller,findProvider(caller,expected),mask,expectedPid,expectedBirth);
    }catch(...){return {};}
}
} // namespace information_read_detail
inline Facts read(std::uint32_t mask)noexcept{return information_read_detail::read(mask,0,0);}
inline Facts readHomeStartupWindow(DWORD expectedPid,std::uint64_t expectedBirth)noexcept{
    if(!expectedPid||!expectedBirth)return {};
    return information_read_detail::read(StartupWindow,expectedPid,expectedBirth);
}
} // namespace ymcc::steaminfo
