#define NOMINMAX
#include "update_install_recovery.h"
#include <iostream>
#include <fstream>
#include <limits>
#include "production-admission.h"
using namespace ymcc::updaterecovery;
static int checks=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;}
int wmain(int argc,wchar_t** argv){try{
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    Json j={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","owned"},{"phase","applying"},{"version","0.0.36"},{"sha256",std::string(64,'a')},{"exePath",utf8(image)},{"powerControl","D:\\fixture\\PowerControl"}};
    const std::wstring pc=L"D:\\fixture\\PowerControl";
    check(authorizes(j,image,pc,"0.0.36",std::string(64,'A')),"matching unfinished transaction");
    check(!authorizes(j,image,pc,"0.0.37",std::string(64,'a')),"version bound");
    check(!authorizes(j,image,pc,"0.0.36",std::string(64,'b')),"sha bound");
    check(!authorizes(j,L"D:\\other\\YeManCC.exe",pc,"0.0.36",std::string(64,'a')),"exe bound");
    check(!authorizes(j,image,L"D:\\other\\PowerControl","0.0.36",std::string(64,'a')),"PowerControl authority bound");
    for(const auto& phase:{"committed","rolled-back","failed","", "started"}){j["phase"]=phase;check(!authorizes(j,image,pc,"0.0.36",std::string(64,'a')),"terminal phase rejected");}
    j["phase"]="repair-required";check(authorizes(j,image,pc,"0.0.36",std::string(64,'a')),"failed repair retains authorization");
    j["phase"]=42;check(!unfinished(j),"malformed phase bounded");j["phase"]="applying";
    FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    j["helperPid"]=GetCurrentProcessId();j["helperCreation"]=std::to_string(filetime(c));j["helperImage"]=utf8(image);
    check(live(j),"live helper real process");j["helperCreation"]="0";check(!live(j),"reused pid rejected");
    j["helperCreation"]=std::to_string(filetime(c));j["helperImage"]="D:\\unrelated.exe";check(!live(j),"wrong image rejected");
    j.erase("helperPid");j["phase"]="authorized";j["authorizedAt"]=now();j["launcherPid"]=GetCurrentProcessId();j["launcherCreation"]=std::to_string(filetime(c));j["launcherImage"]=utf8(image);
    check(live(j),"bounded launch gap");j["authorizedAt"]=now()-16000;check(!live(j),"launch gap expires");
    j["authorizedAt"]=now()+60000;check(!live(j),"future timestamp cannot freeze");
    for(const wchar_t* sddl:{L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;BU)",L"O:BAG:BAD:P(D;;FA;;;BU)(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:P(A;;FA;;;BA)"}){
        PSECURITY_DESCRIPTOR sd=nullptr;check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&sd,nullptr)!=0,"parse actual OS descriptor");
        bool expect=std::wstring(sddl)==L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";check(protectedDescriptor(sd)==expect,"actual OS descriptor admission");LocalFree(sd);
    }
    if(argc>1){std::filesystem::path root=argv[1];std::filesystem::create_directories(root);auto path=root/L"forged.json";std::ofstream(path)<<j.dump();check(readAt(root.wstring(),path.wstring()).empty(),"real writable file never authorizes recovery");}
    auto rejected=[](const std::string& v,const std::string& sha,bool background=false){try{requireInstallableUpdateVersion(v,sha,background);return false;}catch(...){return true;}};
    fixtureJournal=Json::object();check(!rejected("0.0.37",std::string(64,'a')),"ordinary newer version admitted");
    check(rejected("0.0.36",std::string(64,'a')),"ordinary equal version rejected");
    j["phase"]="repair-required";j.erase("helperPid");fixtureJournal=j;
    check(!rejected("0.0.36",std::string(64,'a')),"production guard admits authorized same-version repair");
    check(rejected("0.0.35",std::string(64,'a')),"production guard rejects downgrade");
    check(rejected("0.0.36",std::string(64,'a'),true),"background never repairs equal version");
    check(rejected("0.0.36",std::string(64,'b')),"production guard rejects wrong package");
    fixtureJournal["phase"]="committed";check(rejected("0.0.36",std::string(64,'a')),"committed journal cannot authorize reinstallation");
    fixtureJournal=j;fixtureJournal["helperPid"]=GetCurrentProcessId();fixtureJournal["helperCreation"]=std::to_string(filetime(c));fixtureJournal["helperImage"]=utf8(image);
    check(rejected("0.0.37",std::string(64,'a')),"live helper blocks competing successor");
    std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"scope\":\"production policy, OS security descriptors and owned process identity; elevated writer untested\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
