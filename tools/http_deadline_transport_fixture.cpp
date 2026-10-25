#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <iostream>
#include "http_request_deadline.h"
#pragma comment(lib, "winhttp.lib")
int main(int argc,char** argv) {
  if(argc!=5)return 9;
  const DWORD budget=ymcc::http::timeoutBudget(std::stoll(argv[3]));
  const ULONGLONG start=GetTickCount64();
  const ULONGLONG deadline=std::string(argv[4])=="queue"?start-1:start+budget;
  try {
    ymcc::http::requireDeadline(deadline,"queue");
    HINTERNET session=WinHttpOpen(L"YMCC-test",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
    HINTERNET connect=WinHttpConnect(session,L"127.0.0.1",static_cast<INTERNET_PORT>(std::stoi(argv[1])),0);
    std::wstring path; for(char c:std::string(argv[2]))path+=static_cast<wchar_t>(c);
    HINTERNET request=WinHttpOpenRequest(connect,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    ymcc::http::AsyncRequest transport(session,connect,request,deadline,[start, mode=std::string(argv[4])]{return mode=="cancel"&&GetTickCount64()-start>80;});
    transport.send("{}");transport.headers();
    DWORD status=0,bytes=sizeof(status);WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&bytes,nullptr);
    const auto body=transport.readAll(8u<<20);
    std::cout<<"OK status="<<status<<" bytes="<<body.size()<<" elapsed="<<GetTickCount64()-start<<"\n";
    return 0;
  } catch(const std::exception& e){std::cout<<"ERROR "<<e.what()<<" elapsed="<<GetTickCount64()-start<<"\n";return 7;}
}