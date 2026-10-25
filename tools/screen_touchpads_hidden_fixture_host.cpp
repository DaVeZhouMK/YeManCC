#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    const auto desktopName=L"YMCC_HiddenModel_"+std::to_wstring(GetCurrentProcessId());
    HDESK desk=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desk)return 3;
    std::wstring command;for(int i=1;i<argc;++i){if(i>1)command+=L" ";command+=L"\""+std::wstring(argv[i])+L"\"";}
    STARTUPINFOW start{sizeof(start)};start.lpDesktop=const_cast<wchar_t*>(desktopName.c_str());
    start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)){CloseDesktop(desk);return 4;}
    CloseHandle(child.hThread);std::cout<<"HIDDEN_MODEL_PID="<<child.dwProcessId<<" visibleDesktopSwitched=false\n"<<std::flush;
    WaitForSingleObject(child.hProcess,INFINITE);DWORD exit=1;GetExitCodeProcess(child.hProcess,&exit);
    CloseHandle(child.hProcess);CloseDesktop(desk);return (int)exit;
}
