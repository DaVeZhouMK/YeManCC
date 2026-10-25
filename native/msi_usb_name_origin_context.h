#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <tuple>
namespace ymcc::msi::usbName {
#if defined(YMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC) && defined(YMCC_MSI_VISIBILITY_DIAGNOSTIC)
inline constexpr bool contextBuildEnabled=true;
#else
inline constexpr bool contextBuildEnabled=false;
#endif
struct Context {
    uint32_t version=1,bytes=sizeof(Context),nativePid=0,boardAuthorized=0,sourceValid=0;
    uint64_t parentCreationTime=0,sourceGeneration=0,powerGeneration=0,hostEpoch=0,observedTick=0;
    std::array<char,96> board{};std::array<char,512> usbRoot{};
    std::array<char,1024> interfacePath{};std::array<char,48> phase{};
};
static_assert(std::is_trivially_copyable_v<Context>);
template<size_t N> inline bool text(std::array<char,N>& out,const std::string& s){out={};if(s.size()>=N)return false;std::memcpy(out.data(),s.data(),s.size());return s.find('\0')==std::string::npos;}
template<size_t N> inline std::string text(const std::array<char,N>& s){const auto* end=static_cast<const char*>(std::memchr(s.data(),0,N));return end?std::string(s.data(),end):std::string{};}
inline std::string asciiUpper(std::string s){for(char& c:s)if(c>='a'&&c<='z')c=char(c-'a'+'A');return s;}
inline bool eligible(const Context& c,uint32_t parent,uint64_t created){
    const auto root=asciiUpper(text(c.usbRoot)),iface=asciiUpper(text(c.interfacePath));
    return c.version==1&&c.bytes==sizeof(Context)&&c.nativePid==parent&&parent&&c.parentCreationTime==created&&created&&
        c.boardAuthorized==1&&c.sourceValid==1&&c.sourceGeneration&&c.powerGeneration&&!text(c.board).empty()&&
        root.starts_with("USB\\VID_0DB0&PID_1902\\")&&root.size()>std::string("USB\\VID_0DB0&PID_1902\\").size()&&root.find("&MI_")==std::string::npos&&
        iface.starts_with("\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL01#");
}
struct ObserverOptions {bool valid=true,rawRequested=false;std::wstring contextName;size_t positionalCount=0;};
inline ObserverOptions observerOptions(const std::vector<std::wstring>& args){
    ObserverOptions out;out.positionalCount=args.size();bool contextSeen=false;
    while(out.positionalCount>1){
        const auto n=out.positionalCount;
        if(args[n-1]==L"--raw-msi-receipts"){if(out.rawRequested){out.valid=false;return out;}out.rawRequested=true;--out.positionalCount;continue;}
        if(n>2&&args[n-2]==L"--msi-name-context"){if(contextSeen||args[n-1].empty()){out.valid=false;return out;}contextSeen=true;out.contextName=args[n-1];out.positionalCount-=2;continue;}
        if(args[n-1]==L"--msi-name-context")out.valid=false;
        break;
    }return out;
}
inline void bindPower(Context& c,uint64_t bindingPower,uint64_t observedPower){c.powerGeneration=bindingPower;if(!bindingPower||bindingPower!=observedPower)c.sourceValid=0;}
inline bool fresh(const Context& c,uint64_t now){return c.observedTick&&now>=c.observedTick&&now-c.observedTick<=2000;}
inline std::wstring contextMappingName(uint32_t parent,uint64_t created){return parent&&created?L"Local\\YeManCC-MSI-NameContext-"+std::to_wstring(parent)+L"-"+std::to_wstring(created):std::wstring{};}
// The capture join rejects a changed Host epoch even if the physical source is
// retained. A new epoch must have its own bounded diagnostic attempt.
using CaptureKey=std::tuple<uint64_t,uint64_t,uint64_t>;
inline CaptureKey captureKey(const Context& c){return{c.powerGeneration,c.sourceGeneration,c.hostEpoch};}
inline bool sameGeneration(const Context& a,const Context& b){return a.version==b.version&&a.bytes==b.bytes&&a.nativePid==b.nativePid&&a.parentCreationTime==b.parentCreationTime&&
    a.sourceGeneration==b.sourceGeneration&&a.powerGeneration==b.powerGeneration&&a.hostEpoch==b.hostEpoch&&a.boardAuthorized==b.boardAuthorized&&a.sourceValid==b.sourceValid&&a.board==b.board&&a.usbRoot==b.usbRoot&&a.interfacePath==b.interfacePath;}
}
#ifdef _WIN32
#include <windows.h>
#include <mutex>
namespace ymcc::msi::usbName {
struct alignas(8) ContextSlot{volatile LONG64 sequence=0;Context context{};};
static_assert(sizeof(void*)==8,"name provenance mapping requires the x64 observer ABI");
class ContextPublisher {
    HANDLE mapping=nullptr;ContextSlot* slot=nullptr;std::wstring mappingName;std::mutex mx;
    uint64_t creation=0;DWORD parent=0;
    bool ensure(){if(slot)return true;FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return false;
        ULARGE_INTEGER value{};value.LowPart=c.dwLowDateTime;value.HighPart=c.dwHighDateTime;creation=value.QuadPart;parent=GetCurrentProcessId();
        mappingName=contextMappingName(parent,creation);
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ContextSlot),mappingName.c_str());
        const DWORD err=mapping?GetLastError():0;if(!mapping||err==ERROR_ALREADY_EXISTS){if(mapping)CloseHandle(mapping);mapping=nullptr;mappingName.clear();return false;}
        slot=static_cast<ContextSlot*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(ContextSlot)));if(!slot){CloseHandle(mapping);mapping=nullptr;mappingName.clear();return false;}
        std::memset(slot,0,sizeof(*slot));return true;}
public:
    ~ContextPublisher(){if(slot)UnmapViewOfFile(slot);if(mapping)CloseHandle(mapping);}
    bool opened(){std::lock_guard<std::mutex> lock(mx);return slot!=nullptr;}
    bool publish(Context c){std::lock_guard<std::mutex> lock(mx);if(!ensure())return false;c.nativePid=parent;c.parentCreationTime=creation;if(!eligible(c,parent,creation))c.sourceValid=0;
        InterlockedIncrement64(&slot->sequence);MemoryBarrier();std::memcpy(&slot->context,&c,sizeof(c));MemoryBarrier();InterlockedIncrement64(&slot->sequence);return true;}
    std::wstring name(){std::lock_guard<std::mutex> lock(mx);return ensure()?mappingName:std::wstring{};}
};
class ContextReader {
    HANDLE mapping=nullptr;const ContextSlot* slot=nullptr;
public:
    ContextReader()=default;ContextReader(const ContextReader&)=delete;ContextReader&operator=(const ContextReader&)=delete;
    ~ContextReader(){if(slot)UnmapViewOfFile(slot);if(mapping)CloseHandle(mapping);}
    bool open(const std::wstring& name){if(name.empty()||name.size()>256||slot)return false;mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!mapping)return false;
        slot=static_cast<const ContextSlot*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(ContextSlot)));if(!slot){CloseHandle(mapping);mapping=nullptr;return false;}return true;}
    bool snapshot(Context& c)const{if(!slot)return false;for(unsigned attempt=0;attempt<3;++attempt){const LONG64 first=slot->sequence;MemoryBarrier();if(!first||(first&1))continue;
        std::memcpy(&c,&slot->context,sizeof(c));MemoryBarrier();const LONG64 last=slot->sequence;if(first==last&&!(last&1)&&c.version==1&&c.bytes==sizeof(Context))return true;}return false;}
};
}
#endif