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
inline Context observeForeground(bool steam){
 Context c{};c.steam=steam;const HWND h=GetForegroundWindow();if(!h||!IsWindow(h)||!IsWindowVisible(h)||IsIconic(h))return c;
 const HWND root=GetAncestor(h,GA_ROOT);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(!pid)return c;
 c.window=reinterpret_cast<std::uintptr_t>(h);c.pid=pid;c.ymcc=pid==GetCurrentProcessId();if(c.ymcc)return c;
 const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return c;
 wchar_t path[32768]{};DWORD size=32768;FILETIME birth{},end{},kernel{},user{};DWORD session=0,ownSession=0;
 const bool known=QueryFullProcessImageNameW(process,0,path,&size)&&GetProcessTimes(process,&birth,&end,&kernel,&user)&&WaitForSingleObject(process,0)==WAIT_TIMEOUT&&ProcessIdToSessionId(pid,&session)&&ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)&&session==ownSession;CloseHandle(process);if(!known)return c;
 c.created=(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
 wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return c;
 const auto image=lower(path),win=lower(windows);wchar_t cls[128]{};GetClassNameW(root?root:h,cls,128);
 const std::wstring type=cls;
 c.foregroundImage=foregroundUtf8(image);c.foregroundClass=foregroundUtf8(type);
 // The path/session/process-lifetime checks above remain mandatory. Do not
 // reject a verified Explorer surface solely because its popup class is new.
 c.desktop=desktopSurface(image,win,type);
 if(GetForegroundWindow()!=h)return Context{0,0,0,false,false,steam};return c;
}
}
