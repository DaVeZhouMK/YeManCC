#pragma once
#include "sleep_power_state.h"
#include "sleep_power_revocation.h"
#include "sleep_power_remote_client.h"
#include "process_handle_inheritance.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
namespace ymcc::sleeppower::task {
inline constexpr const char* paused="media-shared-power-paused";
inline constexpr wchar_t argument[]=L"--shared-sleep-task-v1";
struct Access {Reader reader=nullptr;RevocationMirrors* mirrors=nullptr;remote::NativeRemoteTransport*(*transport)()noexcept=nullptr;};
inline std::atomic<const Access*> nativeAccess{nullptr};
inline bool bind(const Access& stable)noexcept{if(!stable.reader||!stable.mirrors||!stable.transport)return false;const Access* empty=nullptr;if(nativeAccess.compare_exchange_strong(empty,&stable))return true;return empty&&empty->reader==stable.reader&&empty->mirrors==stable.mirrors&&empty->transport==stable.transport;}
class Permit {public:virtual ~Permit()=default;virtual bool current()noexcept=0;virtual HANDLE event()const noexcept=0;};
inline bool allowed()noexcept{const auto* a=nativeAccess.load();SharedPowerV1 p;return a&&capturePower(a->reader,p);}
inline remote::NativeRemoteTransport* transport()noexcept{const auto* a=nativeAccess.load();return a?a->transport():nullptr;}
class Scope final:public Permit {
    Reader reader_;const SharedPowerV1 power_;RevocationMirrors::Registration registration_;std::atomic<bool> retired_{false};
    Scope(Reader r,SharedPowerV1 p,RevocationMirrors::Registration&& n)noexcept:reader_(r),power_(p),registration_(std::move(n)){}
public:
    static std::shared_ptr<Scope> capture()noexcept{try{const auto* a=nativeAccess.load();SharedPowerV1 p;if(!a||!capturePower(a->reader,p))return {};auto n=a->mirrors->observe(a->reader,p);if(!n||n.cancelled()||!powerCurrent(a->reader,p))return {};return std::shared_ptr<Scope>(new Scope(a->reader,p,std::move(n)));}catch(...){return {};}}
    HANDLE event()const noexcept override{return registration_.event();}
    bool current()noexcept override{if(retired_.load()||registration_.cancelled())return false;if(powerCurrent(reader_,power_)&&!registration_.cancelled())return true;retired_=true;registration_.cancel();return false;}
};
inline bool requestKey(const std::string& value,remote::Key128& output)noexcept{output={};if(value.size()!=32)return false;auto* bytes=reinterpret_cast<unsigned char*>(&output);for(unsigned i=0;i<16;++i){unsigned byte=0;for(unsigned k=0;k<2;++k){const auto c=value[2*i+k];const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(n<0)return false;byte=(byte<<4)|unsigned(n);}bytes[i]=static_cast<unsigned char>(byte);}return remote::nonzeroKey(output);}
struct Frame {std::uint32_t magic=0x31544d59,version=1,bytes=sizeof(Frame),reserved=0;remote::DescriptorV1 binding;std::array<char,32> request{};std::array<wchar_t,161> pipe{};};
static_assert(sizeof(Frame)==504&&offsetof(Frame,binding)==16&&offsetof(Frame,request)==144&&offsetof(Frame,pipe)==176);
// Owned by original SourceJob State before prepare. Last writable copy is
// sealed before ResumeThread;all failed handles/views remain in that State.
class Transfer final {
    HANDLE write_=nullptr,read_=nullptr;Frame* view_=nullptr;Frame frame_;
    std::unique_lock<std::recursive_mutex> inheritance_;
    static bool close(HANDLE& h)noexcept{if(!h)return true;if(!CloseHandle(h))return false;h=nullptr;return true;}
public:
    Transfer()noexcept=default;Transfer(const Transfer&)=delete;Transfer& operator=(const Transfer&)=delete;
    bool prepare(const std::string& token)noexcept{try{remote::Key128 key;if(write_||read_||view_||!requestKey(token,key))return false;std::memcpy(frame_.request.data(),token.data(),32);inheritance_=std::unique_lock<std::recursive_mutex>(ymcc::processInheritance::owner());write_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Frame),nullptr);if(!write_)return false;view_=static_cast<Frame*>(MapViewOfFile(write_,FILE_MAP_WRITE,0,0,sizeof(Frame)));if(!view_)return false;std::memcpy(view_,&frame_,sizeof frame_);return DuplicateHandle(GetCurrentProcess(),write_,GetCurrentProcess(),&read_,FILE_MAP_READ,TRUE,0)!=FALSE;}catch(...){return false;}}
    HANDLE handle()const noexcept{return read_;}
    std::wstring arguments()const{return std::wstring(L" ")+argument+L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(read_));}
    bool seal(const remote::RemoteOfferV1& offer)noexcept{if(!view_||!remote::validTransportStem(offer.pipeName)||offer.pipeName.size()>160||!powerAdmitted(offer.binding.captured))return false;frame_.binding=offer.binding;std::copy(offer.pipeName.begin(),offer.pipeName.end(),frame_.pipe.begin());std::memcpy(view_,&frame_,sizeof frame_);MemoryBarrier();if(!UnmapViewOfFile(view_))return false;view_=nullptr;if(!close(write_)||!SetHandleInformation(read_,HANDLE_FLAG_INHERIT,0))return false;if(inheritance_.owns_lock())inheritance_.unlock();return true;}
    void beginClose()noexcept{if(read_)(void)SetHandleInformation(read_,HANDLE_FLAG_INHERIT,0);if(inheritance_.owns_lock())inheritance_.unlock();}
    bool closeOwned()noexcept{beginClose();if(view_){if(!UnmapViewOfFile(view_))return false;view_=nullptr;}const bool a=close(write_),b=close(read_);return a&&b;}
};
class HandleList final {
    std::vector<unsigned char> bytes_;std::vector<HANDLE> handles_;STARTUPINFOEXW info_{};bool ready_=false;
public:
    explicit HandleList(std::vector<HANDLE> handles):handles_(std::move(handles)){if(handles_.empty()||handles_.size()>64)throw 0;for(size_t i=0;i<handles_.size();++i){if(!handles_[i]||handles_[i]==INVALID_HANDLE_VALUE)throw 0;for(size_t k=0;k<i;++k)if(handles_[i]==handles_[k])throw 0;}SIZE_T count=0;InitializeProcThreadAttributeList(nullptr,1,0,&count);if(!count)throw 0;bytes_.resize(count);info_.StartupInfo.cb=sizeof info_;info_.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;info_.StartupInfo.wShowWindow=SW_HIDE;info_.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes_.data());if(!InitializeProcThreadAttributeList(info_.lpAttributeList,1,0,&count))throw 0;ready_=true;if(!UpdateProcThreadAttribute(info_.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles_.data(),handles_.size()*sizeof(HANDLE),nullptr,nullptr)){DeleteProcThreadAttributeList(info_.lpAttributeList);ready_=false;throw 0;}}
    ~HandleList(){if(ready_)DeleteProcThreadAttributeList(info_.lpAttributeList);}HandleList(const HandleList&)=delete;HandleList& operator=(const HandleList&)=delete;STARTUPINFOW* startup()noexcept{return &info_.StartupInfo;}
};
class RemotePermit final:public Permit {std::shared_ptr<remote::NativeObservationClient> client_;public:explicit RemotePermit(std::shared_ptr<remote::NativeObservationClient> c)noexcept:client_(std::move(c)){}bool current()noexcept override{return client_&&client_->current();}HANDLE event()const noexcept override{return client_?client_->cancellationEvent():nullptr;}};
inline std::shared_ptr<Permit> import(HANDLE mapping,HANDLE parent,const std::string& token,bool trustedPair)noexcept{
    try{DWORD flags=0;Frame frame;remote::Key128 key;
        if(!trustedPair||!mapping||mapping==parent||!GetHandleInformation(mapping,&flags)||!(flags&HANDLE_FLAG_INHERIT)||!requestKey(token,key))return {};
        if(!remote::NativeObservationClient::readInheritedMetadata(mapping,&frame,sizeof frame))return {};
        if(frame.magic!=0x31544d59||frame.version!=1||frame.bytes!=sizeof frame||frame.reserved||frame.pipe.back()!=0||std::memcmp(frame.request.data(),token.data(),32)||!remote::sameKey(frame.binding.request,key))return {};
        remote::RemoteOfferV1 offer{frame.pipe.data(),frame.binding};auto client=remote::NativeObservationClient::import(offer,parent,trustedPair);if(!client)return {};return std::make_shared<RemotePermit>(std::move(client));
    }catch(...){return {};}
}
}
