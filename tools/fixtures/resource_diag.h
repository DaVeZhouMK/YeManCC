#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
namespace resource_diag {
enum Kind:unsigned {EventCreate=1,EventSet,EventReset,HandleClose,FileCreate,AlgorithmOpen,HashCreate,HashData,HashFinish,HashDestroy,AlgorithmClose,Random,ThreadCreate,ThreadJoin};
struct Api {unsigned kind=0;std::uintptr_t handle=0,parent=0;LONG status=0;DWORD error=0,thread=0;};
inline Api api[16384]{};inline std::atomic<unsigned> apiCount{0};
inline void note(unsigned kind,std::uintptr_t h,std::uintptr_t parent,LONG status,DWORD error)noexcept{
 const auto i=apiCount.fetch_add(1,std::memory_order_relaxed);
 if(i<16384)api[i]={kind,h,parent,status,error,GetCurrentThreadId()};
}
struct Row {
 std::uintptr_t handle=0,object=0;ULONG access=0;USHORT typeIndex=0;
 LONG typeStatus=0,nameStatus=0;DWORD fileType=0,pathError=0;
 bool typeKnown=false,nameKnown=false,pathKnown=false;
 wchar_t type[64]{},name[512]{},path[512]{};
};
struct Snapshot {
 Row rows[1024]{};unsigned count=0;bool overflow=false,complete=false;
 LONG queryStatus=0;ULONG systemNeeded=0;unsigned queryAttempts=0;
 DWORD pid=0,handleCountAtEntry=0,handleCountAtExit=0,countError=0;
 unsigned virtualAllocations=0,virtualFrees=0;DWORD memoryError=0;
 unsigned diagnosticHandlesCreated=0,diagnosticDuplicates=0;
};
inline Snapshot before{},after{};
struct Entry {void* object;ULONG_PTR pid,handle;ULONG access;USHORT backtrace,type;ULONG attributes,reserved;};
static_assert(sizeof(void*)==8&&sizeof(Entry)==40);
struct Unicode {USHORT bytes,maximum;PWSTR value;};
using QuerySystem=LONG(NTAPI*)(ULONG,PVOID,ULONG,PULONG);
using QueryObject=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
inline bool copyUnicode(wchar_t* out,std::size_t capacity,const void* buffer,std::size_t bufferBytes)noexcept{
 const auto* text=static_cast<const Unicode*>(buffer);
 const auto begin=reinterpret_cast<std::uintptr_t>(buffer),p=reinterpret_cast<std::uintptr_t>(text->value);
 if(!text->value||text->bytes%sizeof(wchar_t)||text->bytes/sizeof(wchar_t)>=capacity||
    p<begin||p>begin+bufferBytes||text->bytes>begin+bufferBytes-p)return false;
 std::memcpy(out,text->value,text->bytes);out[text->bytes/sizeof(wchar_t)]=0;return true;
}
inline void capture(Snapshot& s)noexcept{
 const auto saved=GetLastError();s.pid=GetCurrentProcessId();
 if(!GetProcessHandleCount(GetCurrentProcess(),&s.handleCountAtEntry))s.countError=GetLastError();
 const auto module=GetModuleHandleW(L"ntdll.dll");
 QuerySystem querySystem=nullptr;QueryObject queryObject=nullptr;
 const auto systemAddress=GetProcAddress(module,"NtQuerySystemInformation"),objectAddress=GetProcAddress(module,"NtQueryObject");
 static_assert(sizeof querySystem==sizeof systemAddress&&sizeof queryObject==sizeof objectAddress);
 std::memcpy(&querySystem,&systemAddress,sizeof querySystem);std::memcpy(&queryObject,&objectAddress,sizeof queryObject);
 if(!module||!querySystem||!queryObject){s.queryStatus=static_cast<LONG>(0xc0000002UL);SetLastError(saved);return;}
 ULONG size=2u*1024u*1024u;void* memory=nullptr;
 for(unsigned attempt=0;attempt<8&&size<=128u*1024u*1024u;++attempt){
  memory=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  if(!memory){s.memoryError=GetLastError();break;}++s.virtualAllocations;
  ULONG needed=0;s.queryStatus=querySystem(64,memory,size,&needed);s.systemNeeded=needed;++s.queryAttempts;
  if(s.queryStatus>=0)break;
  if(VirtualFree(memory,0,MEM_RELEASE))++s.virtualFrees;else s.memoryError=GetLastError();
  memory=nullptr;
  if(static_cast<ULONG>(s.queryStatus)!=0xc0000004UL)break;
  const auto next=needed<=128u*1024u*1024u-65536u?needed+65536u:128u*1024u*1024u;
  size=(std::max)(size*2,next);
 }
 if(memory&&s.queryStatus>=0){
  const auto count=*static_cast<ULONG_PTR*>(memory);
  if(count<=(size-2*sizeof(ULONG_PTR))/sizeof(Entry)){
   const auto* entries=reinterpret_cast<const Entry*>(static_cast<const unsigned char*>(memory)+2*sizeof(ULONG_PTR));
   for(ULONG_PTR i=0;i<count;++i){
    const auto& e=entries[i];if(e.pid!=s.pid)continue;
    if(s.count>=1024){s.overflow=true;break;}
    auto& row=s.rows[s.count++];row.handle=e.handle;row.object=reinterpret_cast<std::uintptr_t>(e.object);row.access=e.access;row.typeIndex=e.type;
    const auto handle=reinterpret_cast<HANDLE>(e.handle);alignas(void*) unsigned char buffer[8192]{};ULONG length=0;
    row.typeStatus=queryObject(handle,2,buffer,sizeof buffer,&length);
    if(row.typeStatus>=0)row.typeKnown=copyUnicode(row.type,64,buffer,sizeof buffer);
    if(row.typeKnown&&(std::wcscmp(row.type,L"Event")==0||std::wcscmp(row.type,L"Mutant")==0)){
     std::memset(buffer,0,sizeof buffer);row.nameStatus=queryObject(handle,1,buffer,sizeof buffer,&length);
     if(row.nameStatus>=0)row.nameKnown=copyUnicode(row.name,512,buffer,sizeof buffer);
    }else row.nameStatus=static_cast<LONG>(0xc00000bbUL);
    if(row.typeKnown&&std::wcscmp(row.type,L"File")==0){
     row.fileType=GetFileType(handle);
     if(row.fileType==FILE_TYPE_DISK){
      const auto chars=GetFinalPathNameByHandleW(handle,row.path,512,FILE_NAME_NORMALIZED);
      if(chars&&chars<512)row.pathKnown=true;else row.pathError=GetLastError();
     }else row.pathError=ERROR_NOT_SUPPORTED;
    }
   }
   s.complete=!s.overflow;
  }else s.queryStatus=static_cast<LONG>(0xc0000004UL);
 }
 if(memory){if(VirtualFree(memory,0,MEM_RELEASE))++s.virtualFrees;else s.memoryError=GetLastError();}
 if(!GetProcessHandleCount(GetCurrentProcess(),&s.handleCountAtExit))s.countError=GetLastError();
 SetLastError(saved);
}
inline HANDLE WINAPI eventCreate(LPSECURITY_ATTRIBUTES a,BOOL manual,BOOL initial,LPCWSTR name){
 const auto h=::CreateEventW(a,manual,initial,name);const auto e=GetLastError();note(EventCreate,reinterpret_cast<std::uintptr_t>(h),0,h?1:0,e);SetLastError(e);return h;
}
inline BOOL WINAPI eventSet(HANDLE h){const auto r=::SetEvent(h);const auto e=GetLastError();note(EventSet,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline BOOL WINAPI eventReset(HANDLE h){const auto r=::ResetEvent(h);const auto e=GetLastError();note(EventReset,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline BOOL WINAPI closeHandle(HANDLE h){const auto r=::CloseHandle(h);const auto e=GetLastError();note(HandleClose,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline HANDLE WINAPI fileCreate(LPCWSTR p,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES a,DWORD disposition,DWORD flags,HANDLE t){
 const auto h=::CreateFileW(p,access,share,a,disposition,flags,t);const auto e=GetLastError();note(FileCreate,reinterpret_cast<std::uintptr_t>(h),0,h!=INVALID_HANDLE_VALUE?1:0,e);SetLastError(e);return h;
}
inline NTSTATUS WINAPI algorithmOpen(BCRYPT_ALG_HANDLE* out,LPCWSTR name,LPCWSTR provider,ULONG flags){
 const auto r=::BCryptOpenAlgorithmProvider(out,name,provider,flags);const auto e=GetLastError();note(AlgorithmOpen,r>=0?reinterpret_cast<std::uintptr_t>(*out):0,0,r,e);SetLastError(e);return r;
}
inline NTSTATUS WINAPI hashCreate(BCRYPT_ALG_HANDLE a,BCRYPT_HASH_HANDLE* out,PUCHAR object,ULONG bytes,PUCHAR secret,ULONG secretBytes,ULONG flags){
 const auto r=::BCryptCreateHash(a,out,object,bytes,secret,secretBytes,flags);const auto e=GetLastError();note(HashCreate,r>=0?reinterpret_cast<std::uintptr_t>(*out):0,reinterpret_cast<std::uintptr_t>(a),r,e);SetLastError(e);return r;
}
inline NTSTATUS WINAPI hashData(BCRYPT_HASH_HANDLE h,PUCHAR b,ULONG n,ULONG f){const auto r=::BCryptHashData(h,b,n,f);const auto e=GetLastError();note(HashData,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline NTSTATUS WINAPI hashFinish(BCRYPT_HASH_HANDLE h,PUCHAR b,ULONG n,ULONG f){const auto r=::BCryptFinishHash(h,b,n,f);const auto e=GetLastError();note(HashFinish,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline NTSTATUS WINAPI hashDestroy(BCRYPT_HASH_HANDLE h){const auto r=::BCryptDestroyHash(h);const auto e=GetLastError();note(HashDestroy,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline NTSTATUS WINAPI algorithmClose(BCRYPT_ALG_HANDLE h,ULONG flags){const auto r=::BCryptCloseAlgorithmProvider(h,flags);const auto e=GetLastError();note(AlgorithmClose,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
inline NTSTATUS WINAPI random(BCRYPT_ALG_HANDLE h,PUCHAR b,ULONG n,ULONG f){const auto r=::BCryptGenRandom(h,b,n,f);const auto e=GetLastError();note(Random,reinterpret_cast<std::uintptr_t>(h),0,r,e);SetLastError(e);return r;}
}
#define CreateEventW resource_diag::eventCreate
#define SetEvent resource_diag::eventSet
#define ResetEvent resource_diag::eventReset
#define CloseHandle resource_diag::closeHandle
#define CreateFileW resource_diag::fileCreate
#define BCryptOpenAlgorithmProvider resource_diag::algorithmOpen
#define BCryptCreateHash resource_diag::hashCreate
#define BCryptHashData resource_diag::hashData
#define BCryptFinishHash resource_diag::hashFinish
#define BCryptDestroyHash resource_diag::hashDestroy
#define BCryptCloseAlgorithmProvider resource_diag::algorithmClose
#define BCryptGenRandom resource_diag::random
