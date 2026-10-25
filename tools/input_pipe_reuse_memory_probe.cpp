// R45 memory/first-operation probe. Adds only a serial cache inspection method
// to header copies; not the timed binary and never production-integrated.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
static bool countAlloc=false;
static std::uint64_t requests=0,bytes=0,events=0,resets=0;
void* operator new(std::size_t n){if(countAlloc){++requests;bytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace io {
inline DWORD lastSize=0;
inline BOOL read(HANDLE,void* p,DWORD n,DWORD*,OVERLAPPED* ov){lastSize=n;if(n)std::memset(p,0x65,n);SetEvent(ov->hEvent);return TRUE;}
inline BOOL write(HANDLE,const void*,DWORD,DWORD*,OVERLAPPED*){return TRUE;}
inline BOOL connect(HANDLE,OVERLAPPED*){return TRUE;}
inline BOOL query(HANDLE,OVERLAPPED*,DWORD* n,BOOL){*n=lastSize;return TRUE;}
inline BOOL disconnect(HANDLE){return TRUE;}
inline HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){if(countAlloc)++events;return CreateEventW(a,b,c,d);}
inline BOOL reset(HANDLE h){if(countAlloc)++resets;return ResetEvent(h);}
}
#define ReadFile io::read
#define WriteFile io::write
#define ConnectNamedPipe io::connect
#define GetOverlappedResult io::query
#define DisconnectNamedPipe io::disconnect
#define CreateEventW io::event
#define ResetEvent io::reset
#include "memory_probe_transport.h"
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
#undef DisconnectNamedPipe
#undef CreateEventW
#undef ResetEvent
using json=nlohmann::json;
namespace transport=ymcc::inputHost::pipeTransport;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"before handle count");auto* owner=new transport::Runtime();HANDLE source=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD error=99;require(owner->adopt(source,L"memory-probe",error),"memory probe adopt");json rows=json::array();for(DWORD size:{12000,16384,1,16384}){std::vector<unsigned char> caller(size,0xA7);requests=bytes=events=resets=0;countAlloc=true;auto r=owner->run(source,transport::Kind::Read,caller.data(),size,1000);countAlloc=false;require(r.ok&&r.transferred==size&&caller==std::vector<unsigned char>(size,0x65),"probe read parity");const auto cache=owner->cacheProbe();const auto snapshot=owner->snapshot();DWORD handles=0;require(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"probe idle handles");rows.push_back({{"size",size},{"allocationRequests",requests},{"requestedBytes",bytes},{"eventCreates",events},{"eventResets",resets},{"pendingCount",snapshot.pending},{"pendingLogicalBytes",snapshot.payloadBytes},{"cachedObjects",cache[0]},{"cachedSize",cache[1]},{"cachedCapacity",cache[2]},{"processHandlesWhileIdle",handles}});}owner->close(source);owner->sealAndReap();const auto cleared=owner->cacheProbe();require(cleared[0]==0&&cleared[1]==0&&cleared[2]==0,"close retained cache");require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"probe final handles");std::cout<<json({{"pass",true},{"rows",rows},{"runtimeSizeofBytes",sizeof(transport::Runtime)},{"processHandlesBefore",before},{"processHandlesAfter",after},{"oneSerialAccessorAddedToHeader",true},{"pipeAPIResultsSynthetic",true},{"notRSSMeasurement",true},{"productionEdited",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){countAlloc=false;std::cerr<<e.what()<<'\n';return 1;}}
