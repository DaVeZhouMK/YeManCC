#define NOMINMAX
#include "mouse_backend_process.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
static std::wstring desktop;
static BOOL WINAPI isolatedCreate(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION info){
 auto isolated=*startup;isolated.lpDesktop=desktop.data();std::wstring command=std::wstring(cmd)+L" --child";return CreateProcessW(app,command.data(),pa,ta,inherit,flags,env,cwd,&isolated,info);
}
int wmain(int argc,wchar_t** argv){
 if(argc==2&&std::wstring(argv[1])==L"--child"){
  wchar_t report[32768]{},eventName[256]{};GetEnvironmentVariableW(L"YMCC_JOY_SETTINGS_REPORT",report,32768);GetEnvironmentVariableW(L"YMCC_JOY_SETTINGS_STOP",eventName,256);
  STARTUPINFOW startup{sizeof(startup)};GetStartupInfoW(&startup);
  auto h=CreateWindowExW(0,L"STATIC",L"YMCC isolated settings show probe",WS_OVERLAPPEDWINDOW,10,10,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!h)return 2;ShowWindow(h,SW_SHOWNORMAL);
  {std::ofstream out{std::filesystem::path(report)};out<<"{\"firstShowVisible\":"<<(IsWindowVisible(h)?"true":"false")<<",\"startupFlags\":"<<startup.dwFlags<<",\"startupShow\":"<<startup.wShowWindow<<",\"privateDesktop\":true}";}
  auto stop=OpenEventW(SYNCHRONIZE,FALSE,eventName);if(stop){WaitForSingleObject(stop,6000);CloseHandle(stop);}DestroyWindow(h);return 0;
 }
 if(argc!=4)return 3;desktop=L"YMCC-JoySettings-"+std::to_wstring(GetCurrentProcessId());
 auto desk=CreateDesktopW(desktop.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desk){std::cerr<<"CreateDesktop failed "<<GetLastError();return 4;}
 const std::wstring eventName=L"Local\\"+desktop+L"-stop";auto stop=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
 SetEnvironmentVariableW(L"YMCC_JOY_SETTINGS_REPORT",argv[2]);SetEnvironmentVariableW(L"YMCC_JOY_SETTINGS_STOP",eventName.c_str());
 auto result=ymcc::mouseProcess::set(argv[1],true,&isolatedCreate);auto snapshot=ymcc::mouseProcess::inspect(argv[1]);
 std::ifstream in{std::filesystem::path(argv[2])};std::string report((std::istreambuf_iterator<char>(in)),{});
 const bool visible=report.find("\"firstShowVisible\":true")!=std::string::npos,expected=std::wstring(argv[3])==L"visible";
 SetEvent(stop);for(auto& p:snapshot.processes)WaitForSingleObject(p.value,3000);CloseHandle(stop);CloseDesktop(desk);
 bool pass=result.ok&&!report.empty()&&visible==expected;std::cout<<"{\"status\":\""<<(pass?"PASS":"FAIL")<<"\",\"firstShowVisible\":"<<(visible?"true":"false")<<",\"expectedVisible\":"<<(expected?"true":"false")<<",\"productionBackendStartOk\":"<<(result.ok?"true":"false")<<",\"childReport\":"<<(report.empty()?"null":report)<<"}";return pass?0:1;
}
