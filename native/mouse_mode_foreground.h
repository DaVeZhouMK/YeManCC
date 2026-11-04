#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include <cwctype>
#include "mouse_mode_policy.h"
#include "mouse_mode_foreground_rules.h"
namespace ymcc::mousePolicy {
inline std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s;}
inline std::string foregroundUtf8(const std::wstring& text){
 if(text.empty())return {};
 const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
 if(size<=0)return {};std::string result(size,'\0');
 if(WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr)!=size)return {};
 return result;
}
// One lease belongs to the existing policy worker. Image and native birth are
// facts about its held process object; a cached PID alone never admits a window.
class ForegroundObserver {
 HANDLE process_=nullptr;DWORD pid_=0;std::uint64_t created_=0;
 std::wstring image_,windows_;std::string imageUtf8_;
 bool live()const{
  return process_&&GetProcessId(process_)==pid_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;
 }
 bool sameSession()const{
  DWORD session=0,ownSession=0;
  return ProcessIdToSessionId(pid_,&session)&&ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)&&session==ownSession;
 }
 bool acquire(DWORD pid){
  reset();process_=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process_)return false;
  pid_=pid;wchar_t path[32768]{};DWORD size=32768;FILETIME birth{},end{},kernel{},user{};
  if(!QueryFullProcessImageNameW(process_,0,path,&size)||!size||
     !GetProcessTimes(process_,&birth,&end,&kernel,&user)||!live()){
   reset();return false;
  }
  created_=(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
  image_=lower(std::wstring(path,size));imageUtf8_=foregroundUtf8(image_);
  if(imageUtf8_.empty()){reset();return false;}
  return true;
 }
 public:
 ForegroundObserver()=default;
 ForegroundObserver(const ForegroundObserver&)=delete;
 ForegroundObserver& operator=(const ForegroundObserver&)=delete;
 ~ForegroundObserver(){reset();}
 void reset(){
  if(process_)CloseHandle(process_);
  process_=nullptr;pid_=0;created_=0;image_.clear();imageUtf8_.clear();
 }
 Context observe(bool steam,bool inspectIdentity=true){
  Context c{};c.steam=steam;
  // Off/manual On need only the ownership boundary. Drop any former Auto lease.
  if(steam||!inspectIdentity){reset();return c;}
  const HWND h=GetForegroundWindow();
  if(!h||!IsWindow(h)||!IsWindowVisible(h)||IsIconic(h)){reset();return c;}
  DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(!pid){reset();return c;}
  c.window=reinterpret_cast<std::uintptr_t>(h);c.pid=pid;c.ymcc=pid==GetCurrentProcessId();
  if(c.ymcc){reset();return c;}
  if((pid_!=pid||!live())&&!acquire(pid))return c;
  if(!sameSession()){reset();return c;}
  if(windows_.empty()){
   wchar_t windows[MAX_PATH]{};const auto size=GetWindowsDirectoryW(windows,MAX_PATH);
   if(!size||size>=MAX_PATH){reset();return c;}windows_=lower(std::wstring(windows,size));
  }
  // Window/root/class are not process identity and can change within one lease.
  const HWND root=GetAncestor(h,GA_ROOT);wchar_t cls[128]{};GetClassNameW(root?root:h,cls,128);
  c.created=created_;c.identityKnown=true;c.foregroundImage=imageUtf8_;c.foregroundClass=foregroundUtf8(cls);
  c.desktop=desktopSurface(image_,windows_,cls);
  DWORD finalPid=0;
  if(GetForegroundWindow()!=h||!GetWindowThreadProcessId(h,&finalPid)||finalPid!=pid||!live()){
   reset();return Context{0,0,0,false,false,steam};
  }
  return c;
 }
};
inline Context observeForeground(bool steam,bool inspectIdentity=true){
 static thread_local ForegroundObserver observer;
 return observer.observe(steam,inspectIdentity);
}
}
