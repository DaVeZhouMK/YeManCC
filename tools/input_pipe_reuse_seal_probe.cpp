// Deterministic cache-publication/seal interleaving. The probe header has one
// comma-expression hook immediately before cache publication; no other edit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
namespace ymcc::inputHost::pipeTransport {class Runtime;}
static HANDLE entered=nullptr,sealedDone=nullptr,ioEvent=nullptr;
static void r45CachePublicationHook(ymcc::inputHost::pipeTransport::Runtime&);
namespace io {
inline BOOL read(HANDLE,void* p,DWORD n,DWORD*,OVERLAPPED* ov){std::memset(p,0x55,n);ioEvent=ov->hEvent;SetEvent(ov->hEvent);return TRUE;}
inline BOOL write(HANDLE,const void*,DWORD,DWORD*,OVERLAPPED*){return TRUE;}
inline BOOL connect(HANDLE,OVERLAPPED*){return TRUE;}
inline BOOL query(HANDLE,OVERLAPPED*,DWORD* n,BOOL){*n=64;return TRUE;}
}
#define ReadFile io::read
#define WriteFile io::write
#define ConnectNamedPipe io::connect
#define GetOverlappedResult io::query
#include "probe_transport.h"
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
using json=nlohmann::json;
namespace transport=ymcc::inputHost::pipeTransport;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static void r45CachePublicationHook(transport::Runtime&){require(SetEvent(entered)!=FALSE,"probe signal");require(WaitForSingleObject(sealedDone,2000)==WAIT_OBJECT_0,"probe sealer did not finish while mutex held");}
int main(){try{DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"before handles");entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sealedDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(entered&&sealedDone,"probe events");auto* owner=new transport::Runtime();HANDLE original=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD error=99;require(owner->adopt(original,L"private-model",error),"probe adopt");std::thread sealer([&]{if(WaitForSingleObject(entered,2000)!=WAIT_OBJECT_0)std::abort();owner->sealAndReap();SetEvent(sealedDone);});std::vector<unsigned char> data(64,0xA7);auto result=owner->run(original,transport::Kind::Read,data.data(),64,1000);sealer.join();DWORD flags=0;const bool cachedEventOpen=GetHandleInformation(ioEvent,&flags)!=FALSE;
#ifdef R45_CORRECTED
 require(!cachedEventOpen,"corrected candidate retained newly published cache after observed seal");
#else
 require(cachedEventOpen,"initial candidate did not reproduce publication race");
#endif
 require(result.ok&&result.terminal&&!result.retired&&owner->snapshot().sealed,"already-admitted result changed by seal");owner->sealAndReap();CloseHandle(entered);CloseHandle(sealedDone);require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&before==after,"probe handles leaked");std::cout<<json({{"pass",true},{"cacheEventRetainedAfterBusySealerReturned",cachedEventOpen},{"firstOutcomeStillSuccessful",result.ok},{"oneProbeHookAddedToHeader",true},{"sealerIsRealSecondThread",true},{"pipeIOResultsAreSynthetic",true},{"processHandlesBefore",before},{"processHandlesAfter",after},{"noKernelFaultOrCPUClaim",true}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
