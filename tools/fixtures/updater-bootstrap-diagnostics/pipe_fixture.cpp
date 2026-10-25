#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <array>
int wmain(int argc,wchar_t** argv){
 if(argc<2)return 2;
 const std::wstring mode=argv[1];
 if(mode==L"burst"){
  std::array<char,8192> bytes{};bytes.fill('X');DWORD written=0;
  for(int i=0;i<128;++i)if(!WriteFile(GetStdHandle(STD_ERROR_HANDLE),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr))return 3;
  return 1;
 }
 if(argc!=4)return 2;
 if(mode==L"pipe-child"){
  {std::ofstream marker{std::filesystem::path(argv[2])};marker<<GetCurrentProcessId();}
  const auto deadline=GetTickCount64()+8000;
  while(!std::filesystem::exists(std::filesystem::path(argv[3]))&&GetTickCount64()<deadline)Sleep(20);
  return 0;
 }
 if(mode!=L"retain")return 2;
 wchar_t image[MAX_PATH]{};GetModuleFileNameW(nullptr,image,MAX_PATH);
 auto command=L"\""+std::wstring(image)+L"\" pipe-child \""+argv[2]+L"\" \""+argv[3]+L"\"";
 HANDLE error=GetStdHandle(STD_ERROR_HANDLE);if(!SetHandleInformation(error,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT))return 4;
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=error;
 PROCESS_INFORMATION child{};
 if(!CreateProcessW(image,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&child))return 5;
 CloseHandle(child.hThread);CloseHandle(child.hProcess);return 1;
}
