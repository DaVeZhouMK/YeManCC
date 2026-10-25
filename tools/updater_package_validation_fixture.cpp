// OS adapters only; version/layout/archive checks below are extracted verbatim.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <set>
#include <limits>
#include <cctype>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;namespace fspath=std::filesystem;
struct RunOut{std::string out;DWORD exitCode=0;bool ran=false;};
static std::wstring captureRoot;
static RunOut runCapture(const std::wstring& command,DWORD budget){
    const auto file=captureRoot+L"\\tar-capture.txt";
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE output=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE)return {};
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=output;si.hStdError=output;
    PROCESS_INFORMATION process{};std::wstring text=command;
    const bool started=CreateProcessW(nullptr,text.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,captureRoot.c_str(),&si,&process)!=FALSE;
    CloseHandle(output);if(!started)return {};
    CloseHandle(process.hThread);RunOut result{};result.ran=true;
    const DWORD wait=WaitForSingleObject(process.hProcess,std::min<DWORD>(budget,10000));
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,125);WaitForSingleObject(process.hProcess,2000);result.exitCode=125;}
    else GetExitCodeProcess(process.hProcess,&result.exitCode);
    CloseHandle(process.hProcess);std::ifstream input{fspath::path(file),std::ios::binary};result.out={std::istreambuf_iterator<char>(input),{}};return result;
}
static std::wstring quote_windows_arg(const std::wstring& value){return L"\""+value+L"\"";}
static std::string trim_ascii(std::string value){const auto start=value.find_first_not_of(" \r\n\t");if(start==std::string::npos)return {};return value.substr(start,value.find_last_not_of(" \r\n\t")-start+1);}
static std::string sgReadFile(const std::wstring& file){std::ifstream input{fspath::path(file),std::ios::binary};return {std::istreambuf_iterator<char>(input),{}};}
//__PRODUCTION_VALIDATORS__
int wmain(int argc,wchar_t**argv){
 try{
  if(argc!=5)return 2;captureRoot=argv[3];
  const std::wstring zip=argv[1],stage=argv[2];const std::string target=fspath::path(argv[4]).string();
  if(!isStrictUpdateSha256(sha256File(zip)))throw std::runtime_error("SHA failed");
  if(!validateZipArchiveEntries(zip))throw std::runtime_error("Archive check failed");
  if(!unzipTar(zip,stage))throw std::runtime_error("Actual tar extraction failed");
  if(!updatePackageLayoutIsSafe(stage))throw std::runtime_error("Layout rejected");
  if(readPackagedUpdateVersion(stage)!=target)throw std::runtime_error("Version mismatch");
  if(!isSafeZipEntryName("YeManCC/module-package.json")||isSafeZipEntryName("../escape")||isSafeZipEntryName("C:/outside"))throw std::runtime_error("Path safety regression");
  std::cout<<"GIT035_NATIVE_PACKAGE_VALIDATION_PASS sha256="<<sha256File(zip)<<" version="<<target<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
