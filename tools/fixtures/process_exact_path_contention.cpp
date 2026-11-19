#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "deps/json/json.hpp"
namespace heldquery {
inline HANDLE entered=nullptr,release=nullptr;
inline std::atomic<unsigned> calls{0};
inline std::atomic<bool> timedOut{false};
inline NTSTATUS NTAPI query(SYSTEM_INFORMATION_CLASS kind,PVOID buffer,ULONG capacity,PULONG returned){
 const auto serial=calls.fetch_add(1,std::memory_order_acq_rel)+1;
 if(static_cast<int>(kind)!=252)return static_cast<NTSTATUS>(0xC0000003L);
 if(serial==1){if(!SetEvent(entered)||WaitForSingleObject(release,5000)!=WAIT_OBJECT_0){timedOut=true;return static_cast<NTSTATUS>(0xC0000022L);}}
 constexpr wchar_t name[]=L"OwnedQuery.exe";const ULONG size=static_cast<ULONG>(sizeof(SYSTEM_BASICPROCESS_INFORMATION)+sizeof(name));*returned=size;
 if(capacity<size)return static_cast<NTSTATUS>(0xC0000004L);
 std::memset(buffer,0,size);SYSTEM_BASICPROCESS_INFORMATION row{};row.UniqueProcessId=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(GetCurrentProcessId()));row.SequenceNumber=1;
 row.ImageName.Buffer=reinterpret_cast<wchar_t*>(static_cast<unsigned char*>(buffer)+sizeof(row));row.ImageName.Length=sizeof(name)-sizeof(wchar_t);row.ImageName.MaximumLength=sizeof(name);
 std::memcpy(row.ImageName.Buffer,name,sizeof(name));std::memcpy(buffer,&row,sizeof(row));return 0;
}
inline FARPROC WINAPI resolve(HMODULE module,LPCSTR name){if(!std::strcmp(name,"NtQuerySystemInformation"))return reinterpret_cast<FARPROC>(&query);return ::GetProcAddress(module,name);}
}
#define GetProcAddress heldquery::resolve
#include "native/process_inventory.h"
#undef GetProcAddress
using json=nlohmann::json;
static std::atomic<unsigned> liveImageOpens{0};
static HANDLE WINAPI countedOpen(DWORD access,BOOL inherit,DWORD pid){++liveImageOpens;return ::OpenProcess(access,inherit,pid);}
#define OpenProcess countedOpen
#include "handlers.inc"
#undef OpenProcess
struct Event {
 HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Event(){if(!value)throw std::runtime_error("owned contention event create");}
 ~Event(){if(value)CloseHandle(value);}
 bool close(){if(!value)return true;if(!CloseHandle(value))return false;value=nullptr;return true;}
};
static int run(bool after){
 Event entered,release,returned;heldquery::entered=entered.value;heldquery::release=release.value;
 std::shared_ptr<const ymcc::processInventory::Snapshot> workerSnapshot;
 std::atomic<bool> workerOkay{false},controllerOkay{false},releasedByTimeout{false};
 std::thread worker([&]{try{workerSnapshot=ymcc::processInventory::read(&heldquery::query,1000,0,false);workerOkay=workerSnapshot&&workerSnapshot->status==ymcc::processInventory::Status::Complete;}catch(...){}SetEvent(entered.value);});
 std::thread controller;
 struct Join{std::thread& worker;std::thread& controller;HANDLE release;~Join(){if(release)SetEvent(release);if(controller.joinable())controller.join();if(worker.joinable())worker.join();}}join{worker,controller,release.value};
 if(WaitForSingleObject(entered.value,2000)!=WAIT_OBJECT_0)throw std::runtime_error("owned producer never entered");
 if(heldquery::calls.load()!=1||WaitForSingleObject(release.value,0)!=WAIT_TIMEOUT)throw std::runtime_error("actual owner lock precondition missing");
 controller=std::thread([&]{const auto state=WaitForSingleObject(returned.value,1000);if(state==WAIT_TIMEOUT){releasedByTimeout=true;SetEvent(release.value);controllerOkay=true;}else if(state==WAIT_OBJECT_0)controllerOkay=true;else SetEvent(release.value);});
 wchar_t image[32768]{};const auto size=GetModuleFileNameW(nullptr,image,32768);if(!size||size>=32768)throw std::runtime_error("owned self image");
 const json request={{"path",W2U(std::wstring(image,size))}};bool rejected=false;std::string reason;json result;
 const auto at=GetTickCount64();
 try{
#ifdef YMCC_EXACT_NONBLOCKING_AFTER
  result=after?lock_after::handler(request):lock_before::handler(request);
#else
  if(after)throw std::runtime_error("after variant not compiled");result=lock_before::handler(request);
#endif
 }catch(const std::runtime_error& error){rejected=true;reason=error.what();}
 const auto elapsed=GetTickCount64()-at;const bool returnedWhileHeld=WaitForSingleObject(release.value,0)==WAIT_TIMEOUT;
 bool busyReceipt=false,lastErrorPreserved=false;
#ifdef YMCC_EXACT_NONBLOCKING_AFTER
 if(after){SetLastError(0x4152);const auto busy=ymcc::processInventory::tryRead(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,0);const auto preserved=GetLastError();busyReceipt=busy&&busy->status==ymcc::processInventory::Status::Failed&&busy->error==ERROR_BUSY&&busy->rows.empty()&&busy->rejectionHints.empty();lastErrorPreserved=preserved==0x4152;}
#endif
 SetEvent(returned.value);controller.join();SetEvent(release.value);worker.join();join.release=nullptr;
 bool cachePreserved=false;
#ifdef YMCC_EXACT_NONBLOCKING_AFTER
 if(after){const auto previous=heldquery::calls.load();const auto cached=ymcc::processInventory::read(&heldquery::query,1000,250,false);cachePreserved=cached==workerSnapshot&&heldquery::calls.load()==previous;}
#endif
 const auto queries=heldquery::calls.load(),images=liveImageOpens.load();const bool allClosed=entered.close()&&release.close()&&returned.close();
 const json detail={{"schema","ymcc.exact-path-owner-contention.v1"},{"after",after},{"returnedWhileOwnerHeld",returnedWhileHeld},{"rejected",rejected},{"reason",reason},{"elapsedMsDiagnosticOnly",elapsed},{"releasedByTimeout",releasedByTimeout.load()},{"workerOkay",workerOkay.load()},{"controllerOkay",controllerOkay.load()},{"queryTimeout",heldquery::timedOut.load()},{"queryCalls",queries},{"liveImageOpens",images},{"busyReceipt",busyReceipt},{"lastErrorPreserved",lastErrorPreserved},{"cachePreserved",cachePreserved},{"threeOwnedEventsClosed",allClosed},{"threadsJoined",!worker.joinable()&&!controller.joinable()},{"realForeignProcessOrSteamActions",0},{"fullUiMessagePipeline",false},{"sourceReady",false}};
 std::cout<<"EXACT_CONTENTION_JSON "<<detail.dump()<<std::endl;
 if(!workerOkay||!controllerOkay||heldquery::timedOut||!allClosed)throw std::runtime_error("contention setup or normal cleanup failed");
 if(!returnedWhileHeld||!rejected||reason!="PROCESS_INVENTORY_UNAVAILABLE")throw std::runtime_error("EXACT_UI_WAITED_FOR_SHARED_OWNER");
 if(!after||!busyReceipt||!lastErrorPreserved||!cachePreserved||queries!=1||images!=0||releasedByTimeout)throw std::runtime_error("nonblocking owner contract failed");
 return 0;
}
int main(int argc,char** argv){try{lock_before::registration();
#ifdef YMCC_EXACT_NONBLOCKING_AFTER
 lock_after::registration();
#endif
 if(argc!=2)throw std::runtime_error("contention selector required");if(!std::strcmp(argv[1],"--contention-before"))return run(false);if(!std::strcmp(argv[1],"--contention-after"))return run(true);throw std::runtime_error("unknown contention selector");}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}}
