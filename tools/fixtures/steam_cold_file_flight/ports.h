#pragma once
#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdexcept>
namespace cold_file_ports {
inline std::mutex mutex;
inline std::condition_variable changed;
inline BY_HANDLE_FILE_INFORMATION selected{};
inline bool active=false,blocking=false,released=false;
inline unsigned reads=0,fault=0;
inline bool matches(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){
 return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
inline void select(const wchar_t* path,bool block,unsigned failure=0){
 HANDLE h=::CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("owned gate file open");
 BY_HANDLE_FILE_INFORMATION identity{};const bool ok=::GetFileInformationByHandle(h,&identity)!=FALSE;::CloseHandle(h);
 if(!ok)throw std::runtime_error("owned gate file identity");
 std::lock_guard lock(mutex);selected=identity;active=true;blocking=block;released=false;reads=0;fault=failure;
}
inline bool waitFor(unsigned count,unsigned ms=2000){
 std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::milliseconds(ms),[&]{return reads>=count;});
}
inline unsigned count(){std::lock_guard lock(mutex);return reads;}
inline void release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
inline void stop(){std::lock_guard lock(mutex);active=false;released=true;changed.notify_all();}
inline BOOL read(HANDLE handle,LPVOID buffer,DWORD length,LPDWORD got,LPOVERLAPPED overlapped){
 BY_HANDLE_FILE_INFORMATION id{};const bool known=::GetFileInformationByHandle(handle,&id)!=FALSE;
 {
  std::unique_lock lock(mutex);
  if(active&&known&&matches(id,selected)){
   ++reads;changed.notify_all();
   if(fault){const auto kind=fault;fault=0;active=false;if(kind==2)throw std::runtime_error("controlled ReadFile exception");::SetLastError(ERROR_READ_FAULT);return FALSE;}
   if(!changed.wait_for(lock,std::chrono::seconds(8),[]{return !blocking||released;}))throw std::runtime_error("controlled ReadFile gate deadline");
  }
 }
 return ::ReadFile(handle,buffer,length,got,overlapped);
}
}
