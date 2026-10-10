// Source-built inert child only: actual normal/elevated-parent launch checks.
// No Steam, product YMCC, Loader, business settings, ACL edits or token elevation.
#define WIN32_LEAN_AND_MEAN
#include "../native/decky_sidebar_launch.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
using nlohmann::json;
using namespace ymcc::deckylaunch;
int wmain(int argc,wchar_t**argv){
 try {
    if(argc!=3)return 2;const std::filesystem::path directory=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\validation)";
    const auto output=std::filesystem::absolute(argv[2]).lexically_normal();if(output.parent_path()!=directory)throw std::runtime_error("fixture output scope");
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))throw std::runtime_error("fixture own token");
    TOKEN_ELEVATION elevation{};DWORD bytes=0;GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&bytes);
    if(std::wstring(argv[1])==L"--child"){
        const bool normal=unelevated(token.value);json report={{"elevated",elevation.TokenIsElevated!=0},{"mediumOrLower",normal},{"pid",GetCurrentProcessId()},{"sourceBuiltInert",true},{"settingsWrites",0}};
        std::ofstream file(output);file<<report.dump(2);return file.good()&&normal?0:5;
    }
    if(std::wstring(argv[1])!=L"--parent")return 3;
    wchar_t buffer[32768]{};GetModuleFileNameW(nullptr,buffer,static_cast<DWORD>(_countof(buffer)));const std::wstring executable=buffer;
    if(std::filesystem::path(executable).parent_path()!=directory.parent_path()/L"Build")throw std::runtime_error("fixture executable scope");
    const auto childOutput=output.wstring()+L".child.json";
    std::wstring command=L"\""+executable+L"\" --child \""+childOutput+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    const auto result=create(executable,command,nullptr,std::filesystem::path(executable).parent_path().wstring(),startup,child);
    json report={{"parentElevated",elevation.TokenIsElevated!=0},{"childCreated",result.created},{"error",result.error},{"stage",result.stage},{"mode",result.mode},{"thirdPartyStarted",false},{"productStarted",false},{"steamTouched",false},{"settingsWrites",0}};
    if(result.created){
        Handle job;job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        bool owned=job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&AssignProcessToJobObject(job.value,child.hProcess);
        report["jobOwnedBeforeResume"]=owned;
        if(owned&&ResumeThread(child.hThread)!=static_cast<DWORD>(-1)){
            const auto wait=WaitForSingleObject(child.hProcess,10000);DWORD code=99;GetExitCodeProcess(child.hProcess,&code);report["childExit"]=code;report["childFinished"]=wait==WAIT_OBJECT_0;
            if(wait==WAIT_OBJECT_0){std::ifstream file(childOutput);json observation;file>>observation;report["childObservation"]=observation;}
        }else{report["ownershipError"]=GetLastError();TerminateProcess(child.hProcess,1);}
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
    }
    const auto invalid=std::filesystem::path(executable).parent_path()/L"deliberately-absent.exe";std::wstring invalidCommand=L"\""+invalid.wstring()+L"\"";PROCESS_INFORMATION absent{};
    const auto denied=create(invalid.wstring(),invalidCommand,nullptr,invalid.parent_path().wstring(),startup,absent);
    report["missingExecutableRefused"]=!denied.created;report["missingExecutableError"]=denied.error;report["missingExecutableStage"]=denied.stage;
    report["callerIntegrityGate"]=unelevated(token.value)==!elevation.TokenIsElevated;report["sameTokenUser"]=equalUser(token.value,token.value);report["sameTokenSession"]=sameSession(token.value,token.value);report["sameTokenLogon"]=sameLogon(token.value,token.value);
    const bool pass=report.value("childCreated",false)&&report.value("jobOwnedBeforeResume",false)&&report.value("childFinished",false)&&report.value("childExit",99)==0&&report["childObservation"].value("mediumOrLower",false)&&!report["childObservation"].value("elevated",true)&&report.value("missingExecutableRefused",false)&&report.value("callerIntegrityGate",false);
    report["pass"]=pass;std::ofstream file(output);file<<report.dump(2);return pass?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
}
