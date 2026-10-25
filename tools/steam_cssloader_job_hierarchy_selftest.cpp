#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_cssloader_use.h"
#include <fstream>
#include <iostream>
using namespace ymcc;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';}
struct Child {HANDLE process=nullptr,thread=nullptr;~Child(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,1);CloseHandle(process);}if(thread)CloseHandle(thread);}};
static void child(Child& out,const std::wstring& exe){STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};auto cmd=L"\""+exe+L"\" --dummy";check(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"isolated suspended dummy created");out.process=pi.hProcess;out.thread=pi.hThread;}
static void rootJob(deckylaunch::Handle& job){job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"independent foreign job boundary created");}
int wmain(int argc,wchar_t** argv){if(argc>1&&std::wstring(argv[1])==L"--dummy"){Sleep(INFINITE);return 0;}try{
 check(argc>1,"output directory supplied");const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);const auto css=out/L"CssLoader-Standalone-Headless.exe",helper=out/L"fixture-helper.exe";std::filesystem::copy_file(self,css,std::filesystem::copy_options::overwrite_existing);std::filesystem::copy_file(self,helper,std::filesystem::copy_options::overwrite_existing);
 for(const bool editing:{false,true}){
  deckylaunch::Handle foreignCss,foreignHelper;rootJob(foreignCss);rootJob(foreignHelper);
  bool editor=false;steamcssuse::Group use({},[&]{return editor?steamcsseditor::State::Active:steamcsseditor::State::Absent;});use.open();Child backend,worker;child(backend,css.wstring());check(AssignProcessToJobObject(foreignCss.value,backend.process),"existing CSS is owned by one foreign job");check(ResumeThread(backend.thread)!=DWORD(-1),"fixture CSS starts under its foreign parent job");use.adopt(css.wstring());check(use.describe().verified==1,"current use adopts the verified foreign CSS identity");child(worker,helper.wstring());check(AssignProcessToJobObject(foreignHelper.value,worker.process),"new helper starts from a different foreign job hierarchy");SetLastError(0);const auto assigned=use.assign(worker.process);const auto error=assigned?0:GetLastError();std::cout<<"HELPER_ASSIGN "<<assigned<<" ERROR "<<error<<'\n';check(assigned,"helper admission never mixes the backend's populated job hierarchy");check(ResumeThread(worker.thread)!=DWORD(-1),"admitted helper resumes only after job ownership");
  editor=editing;const auto result=use.close();if(editing){check(result.editorProtected&&WaitForSingleObject(backend.process,0)==WAIT_TIMEOUT&&WaitForSingleObject(worker.process,0)==WAIT_TIMEOUT,"Desktop takeover preserves every launch job and the foreign backend");}else{check(result.ok()&&WaitForSingleObject(backend.process,2000)==WAIT_OBJECT_0&&WaitForSingleObject(worker.process,2000)==WAIT_OBJECT_0,"normal close cleans both fixed families across separate job trees");}
 }
 std::cout<<"CSS_JOB_HIERARCHY_OK actualSteamOperations=0 actualCssOperations=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
