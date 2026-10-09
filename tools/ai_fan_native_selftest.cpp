#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include "ai_fan_session.h"
#include "ai_fan_close_evidence.h"
static int checks=0;
static void check(bool ok,const char* what){++checks;if(!ok){std::cerr<<"FAIL "<<what<<std::endl;ExitProcess(1);}}
int wmain(int argc,wchar_t**argv){
 if(argc>1 && std::wstring(argv[1])==L"--child"){Sleep(150);return 0;}
 const std::wstring id=L"11111111-2222-3333-4444-555555555555";
 auto parse=[&](std::vector<std::wstring> args,DWORD pid=1234){std::vector<const wchar_t*> ptrs;for(auto&arg:args)ptrs.push_back(arg.c_str());ymcc::ai_fan::Session s;const bool ok=ymcc::ai_fan::parseSession(static_cast<int>(ptrs.size()),ptrs.data(),pid,s);return std::pair{ok,s};};
 check(parse({L"YMCC"}).first&&!parse({L"YMCC"}).second.enabled,"normal disabled");
 auto result=parse({L"YMCC",L"--ai-fan-mock-session",id});check(result.first&&result.second.enabled&&result.second.parentPid==1234,"explicit session");
 for(auto args:std::vector<std::vector<std::wstring>>{{L"YMCC",L"--ai-fan-mock-session"},{L"YMCC",L"--ai-fan-mock-session=bad"},{L"YMCC",L"--ai-fan-mock-session-other"},{L"YMCC",L"--ai-fan-mock-session",id,L"--ai-fan-mock-session",id},{L"YMCC",L"--ai-fan-mock-session",L"..\\x"}})check(!parse(args).first,"invalid session");
 check(!parse({L"YMCC",L"--ai-fan-mock-session",id},0).first,"zero parent");
 auto isolated=parse({L"YMCC",L"--ai-cpu-isolated-session",id,L"--ai-fan-mock-session",id});
 check(isolated.first&&isolated.second.isolated&&isolated.second.enabled,"paired isolated capability");
 for(auto x:std::vector<std::vector<std::wstring>>{{L"YMCC",L"--ai-cpu-isolated-session",id},{L"YMCC",L"--ai-cpu-isolated-session"},{L"YMCC",L"--ai-cpu-isolated-session=bad"},{L"YMCC",L"--ai-cpu-isolated-session-other"},{L"YMCC",L"--ai-fan-mock-session",id,L"--ai-cpu-isolated-session",L"22222222-2222-3333-4444-555555555555"},{L"YMCC",L"--ai-fan-mock-session",id,L"--ai-cpu-isolated-session",id,L"--ai-cpu-isolated-session",id}})check(!parse(x).first,"isolation fail closed");

 for(int i=0;i<1000;i++){auto x=id;x[static_cast<size_t>(i)%x.size()]=L'G';check(!ymcc::ai_fan::validSessionId(x),"non hex or dash");}
 const std::wstring token=L"C:\\test\\ai-fan-sessions\\"+id+L"\\fan-host\\YeManFanHost.session";
 std::vector<std::wstring> args={L"--mock-handshake",L"--mock-zero-hardware-evidence",L"--port",L"8765",L"--protocol-version",L"2",L"--parent-pid",L"1234",L"--session-token-file",token};
 check(ymcc::ai_fan::validMockHostArguments(result.second,args,token),"complete safe mock arguments");
 for(size_t i=0;i<args.size();i++){auto x=args;x.erase(x.begin()+i);check(!ymcc::ai_fan::validMockHostArguments(result.second,x,token),"missing field rejected");}
 for(auto injection:{L"--real-backend",L"--allow-hardware-writes",L"--authorization",L"--confirm",L"--mock-hc-open-hold-ms",L"--session-token",L"--successor-takeover"}){auto x=args;x.push_back(injection);x.push_back(L"secret");check(!ymcc::ai_fan::validMockHostArguments(result.second,x,token),"unallowed args rejected");}
 for(int i=0;i<1000;i++){auto x=args;x[7]=std::to_wstring(i+2345);check(!ymcc::ai_fan::validMockHostArguments(result.second,x,token),"parent pin");x=args;x[3]=std::to_wstring(i+9765);check(!ymcc::ai_fan::validMockHostArguments(result.second,x,token),"port pin");}
 using json=nlohmann::json;
 json receipt={{"ok",true},{"state",{{"hostMode","mock-handshake"},{"state","Stopped"},{"protocolVersion","2"},{"mockZeroHardwareEvidence",true},{"mockCloseCompleted",true},{"mockControlEnabled",false},{"hardwareWritesEnabled",false},{"hardwareWritesObserved",false},{"openCalled",false},{"openEventsCalled",false},{"unknownState",false},{"lease",nullptr}}}};
 check(ymcc::ai_fan::mockCloseReceiptAllowed(receipt),"actual mock close proof allowed");
 for(const auto* key:{"mockZeroHardwareEvidence","mockCloseCompleted","mockControlEnabled","hardwareWritesEnabled","hardwareWritesObserved","openCalled","openEventsCalled","unknownState"}){
  auto x=receipt;x["state"].erase(key);check(!ymcc::ai_fan::mockCloseReceiptAllowed(x),"missing proof refused");
  x=receipt;x["state"][key]="false";check(!ymcc::ai_fan::mockCloseReceiptAllowed(x),"string bool refused");
  x=receipt;x["state"][key]=!receipt["state"][key].get<bool>();check(!ymcc::ai_fan::mockCloseReceiptAllowed(x),"opposite bit refused");
 }
 for(auto bad:{json(),json(true),json("unknown"),json::array(),json{{"ok",true},{"state",7}}})check(!ymcc::ai_fan::mockCloseReceiptAllowed(bad),"bad shape safely refused");
 auto unsafe=receipt;unsafe["state"]["lease"]={{"leaseId","not-exported"}};check(!ymcc::ai_fan::mockCloseReceiptAllowed(unsafe),"live lease refused");
 unsafe=receipt;unsafe["state"]["protocolVersion"]=true;check(!ymcc::ai_fan::mockCloseReceiptAllowed(unsafe),"wrong protocol refused");
  wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
 for(int i=0;i<5;i++){
  std::wstring cmd=L"\""+std::wstring(path)+L"\" --child";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
  check(CreateProcessW(path,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"test child create");
  CloseHandle(pi.hThread);ymcc::ai_fan::OwnedProcess owned;owned.bind(pi.hProcess,pi.dwProcessId);
  check(owned.owns(pi.dwProcessId),"live actual handle pin");check(owned.creationTime()>0,"creation evidence");check(!owned.owns(pi.dwProcessId+1),"different pid refused");
  check(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0,"test child terminal");check(owned.livePid()==0&&!owned.owns(pi.dwProcessId),"terminated handle cannot be reused");
 }
 std::cout<<"AI fan native guard PASS "<<checks<<" checks; five isolated child processes exited; no hardware"<<std::endl;
}