// Explicit variation of the existing native_process_cost_selftest.cpp only.
// Actual Decky Sampler; deterministic Windows query ports, no real process IO.
#include <array>
#include <map>
#include <set>
#include <mutex>
#include <algorithm>
#include <cwctype>
namespace deckycostfixture {
struct Process {std::wstring image=L"C:\\INERT\\Worker.exe";DWORD session=7;bool open=true,alive=true,imageOk=true,sessionOk=true,timesOk=true;};
static std::map<DWORD,Process> processes;static std::set<DWORD> handles;
static DWORD lastError=0;static unsigned ownCalls=0,currentPidCalls=0,ownFailures=0,frame=0;
static json calls=json::object();
static void count(const char* name){calls[name]=calls.value(name,0u)+1;}
static DWORD pid(HANDLE h){return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(h));}
static HANDLE open(DWORD access,BOOL inherit,DWORD value){count("open");require(access==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)&&!inherit,"sampler requested process control");if(!processes.at(value).open){lastError=ERROR_ACCESS_DENIED;return nullptr;}require(handles.insert(value).second,"sampler opened an already held target");return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));}
static BOOL close(HANDLE h){count("close");require(handles.erase(pid(h))==1,"sampler closed an unowned port handle");return TRUE;}
static DWORD wait(HANDLE h,DWORD timeout){count("wait");require(timeout==0&&handles.contains(pid(h)),"sampler waited on unowned handle");return processes.at(pid(h)).alive?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static DWORD error(){count("lastError");return lastError;}
static DWORD ownPid(){++currentPidCalls;return 99999;}
static BOOL session(DWORD value,DWORD* out){if(value==99999){++ownCalls;if(ownFailures){--ownFailures;lastError=ERROR_ACCESS_DENIED;return FALSE;}*out=7;return TRUE;}count("targetSession");const auto& p=processes.at(value);if(!p.sessionOk){lastError=ERROR_ACCESS_DENIED;return FALSE;}*out=p.session;return TRUE;}
static BOOL image(HANDLE h,DWORD flags,wchar_t* output,DWORD* units){count("image");require(flags==0&&handles.contains(pid(h)),"sampler image port misuse");const auto& p=processes.at(pid(h));if(!p.imageOk){lastError=ERROR_ACCESS_DENIED;return FALSE;}require(*units>p.image.size(),"sampler image capacity");std::copy(p.image.begin(),p.image.end(),output);output[p.image.size()]=0;*units=static_cast<DWORD>(p.image.size());return TRUE;}
static void fill(FILETIME* out,ULONGLONG value){out->dwLowDateTime=static_cast<DWORD>(value);out->dwHighDateTime=static_cast<DWORD>(value>>32);}
static BOOL times(HANDLE h,FILETIME* born,FILETIME* exit,FILETIME* kernel,FILETIME* user){count("times");require(handles.contains(pid(h)),"sampler times unowned handle");if(!processes.at(pid(h)).timesOk){lastError=ERROR_ACCESS_DENIED;return FALSE;}fill(born,10000+pid(h));fill(exit,0);fill(kernel,1000000+frame*100000);fill(user,2000000+frame*50000);return TRUE;}
}
#define OpenProcess deckycostfixture::open
#define CloseHandle deckycostfixture::close
#define WaitForSingleObject deckycostfixture::wait
#define GetLastError deckycostfixture::error
#define GetCurrentProcessId deckycostfixture::ownPid
#define ProcessIdToSessionId deckycostfixture::session
#define QueryFullProcessImageNameW deckycostfixture::image
#define GetProcessTimes deckycostfixture::times
#include "decky_wallpaper_process_cost.h"
#undef OpenProcess
#undef CloseHandle
#undef WaitForSingleObject
#undef GetLastError
#undef GetCurrentProcessId
#undef ProcessIdToSessionId
#undef QueryFullProcessImageNameW
#undef GetProcessTimes
static int deckyCostOwnSessionCases(){
 using namespace deckycostfixture;json cases=json::array();
 try{
  for(const std::string name:{"twelve-healthy","duplicate-role","own-fails-once","own-fails-then-returns","early-target-faults","target-session-fails","target-cpu-fails","power-edge"}){
   require(handles.empty(),"previous sampler retained a handle");processes.clear();calls=json::object();ownCalls=currentPidCalls=ownFailures=frame=lastError=0;
   std::array<ymcc::deckycost::Target,12> targets{};const unsigned countTargets=name=="twelve-healthy"?12:name=="early-target-faults"?6:3;
   for(unsigned n=0;n<countTargets;++n){const DWORD value=200+n;processes[value]=Process{};targets[n]={value,10000+value,L"C:\\INERT\\Worker.exe",false,"cost-target","isolated-ports"};}
   if(name=="duplicate-role"){targets[1]=targets[0];targets[2]=targets[0];}
   if(name=="own-fails-once")ownFailures=1;
   if(name=="own-fails-then-returns")ownFailures=100;
   if(name=="early-target-faults"){processes[200].open=false;processes[201].alive=false;processes[202].image=L"C:\\INERT\\Foreign.exe";processes[203].session=8;targets[4].birth+=1;}
   if(name=="target-session-fails")processes[200].sessionOk=false;
   if(name=="target-cpu-fails")processes[200].timesOk=false;
   ymcc::deckycost::Sampler sampler;json samples=json::array(),ownReads=json::array(),pidReads=json::array();
   for(unsigned step=0;step<2;++step){const auto ownBefore=ownCalls,pidBefore=currentPidCalls;frame=step;if(step==1&&name=="own-fails-then-returns")ownFailures=0;
    samples.push_back(sampler.sample(true,1000+step*1000,name=="power-edge"&&step==1?2:1,8,targets,countTargets));ownReads.push_back(ownCalls-ownBefore);pidReads.push_back(currentPidCalls-pidBefore);
   }
   const auto ownBeforeOff=ownCalls,pidBeforeOff=currentPidCalls;const auto portsBeforeOff=calls;
   require(sampler.sample(false,3000,9,8,targets,countTargets).is_null(),"OFF sampler result");require(ownCalls==ownBeforeOff&&currentPidCalls==pidBeforeOff,"OFF sampled identity");require(handles.empty(),"OFF retained query handles");
   auto withoutClose=calls;withoutClose.erase("close");auto priorWithoutClose=portsBeforeOff;priorWithoutClose.erase("close");require(withoutClose==priorWithoutClose,"OFF performed query IO");
   const auto frozen=calls;require(sampler.sample(false,4000,10,8,targets,countTargets).is_null()&&calls==frozen,"closed steady state performed IO");
   cases.push_back({{"name",name},{"samples",samples},{"ownSessionCalls",ownReads},{"ownPidCalls",pidReads},{"otherPorts",calls},{"handlesAfterOff",handles.size()},{"offSteadyReads",0}});
  }
  std::cout<<json{{"kind","actual-decky-cost-own-session"},{"cases",cases},{"checks",checks},{"realProcessQueries",0},{"realSteamActions",0},{"defaultNativeCostCasesRun",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Same native-cost canonical consumer; exact explicit target-order variation.
static int deckyCostTargetOrderCases(bool identityControls=false,bool aliasControls=false){
 using namespace deckycostfixture;json evidence=json::array();unsigned lost=0;
 try{
  const std::vector<std::string> names=aliasControls?std::vector<std::string>{"alias-front-churn","alias-canonical-missing","alias-birth-appears","alias-normalized-path","alias-full-capacity","alias-earlier-zero"}:identityControls?std::vector<std::string>{"retained-exited","retained-image-changed","retained-session-changed","retained-birth-changed","retained-cpu-failed"}:std::vector<std::string>{"existing-reorder","new-first","replace-tail-with-first","insert-middle","full-capacity-first","remove-then-return","power-new-first","failed-open-first"};
  for(const std::string& name:names){
   require(handles.empty(),"previous target-order fixture retained a handle");processes.clear();calls=json::object();ownCalls=currentPidCalls=ownFailures=frame=lastError=0;
   for(DWORD n=200;n<=220;++n)processes[n]=Process{};
   std::vector<std::vector<DWORD>> rounds;std::vector<std::set<DWORD>> expectedRates;
   if(name=="existing-reorder"){rounds={{200,201,202},{202,200,201}};expectedRates={{},{200,201,202}};}
   if(name=="new-first"||name=="power-new-first"||name=="failed-open-first"){rounds={{200,201},{203,200,201}};expectedRates={{},name=="power-new-first"?std::set<DWORD>{}:std::set<DWORD>{200,201}};}
   if(name=="replace-tail-with-first"){rounds={{200,201,202},{203,200,201}};expectedRates={{},{200,201}};}
   if(name=="insert-middle"){rounds={{200,201,202},{200,203,201,202}};expectedRates={{},{200,201,202}};}
   if(name=="full-capacity-first"){rounds={{},{220}};for(DWORD n=200;n<212;++n)rounds[0].push_back(n);for(DWORD n=200;n<211;++n)rounds[1].push_back(n);expectedRates={{},{}};for(DWORD n=200;n<211;++n)expectedRates[1].insert(n);}
   if(name=="remove-then-return"){rounds={{200,201,202},{201,202},{200,201,202}};expectedRates={{},{201,202},{201,202}};}
   if(name=="failed-open-first")processes[203].open=false;
   if(identityControls){rounds={{200,201},{203,200,201},{203,200,201}};expectedRates={{},{201},{203,201}};}
   if(aliasControls){
    if(name=="alias-full-capacity"){rounds={{200,200},{220,200,200}};for(DWORD v=201;v<=210;++v)rounds[0].push_back(v);for(DWORD v=201;v<=209;++v)rounds[1].push_back(v);expectedRates={{},{}};for(DWORD v=200;v<=209;++v)expectedRates[1].insert(v);}
    else if(name=="alias-canonical-missing"){rounds={{200,200,201},{200,201},{203,200,200,201}};expectedRates={{},{200,201},{200,201}};}
    else if(name=="alias-birth-appears"){rounds={{200,200,201},{200,200,201},{200,201},{200,200,201}};expectedRates={{},{200,201},{201},{201}};}
    else{rounds={{200,200,201},{203,200,200,201}};expectedRates={{},{200,201}};}
   }
   ymcc::deckycost::Sampler sampler;json samples=json::array(),steps=json::array();
   for(std::size_t n=0;n<rounds.size();++n){std::array<ymcc::deckycost::Target,12> targets{};for(std::size_t k=0;k<rounds[n].size();++k){const auto value=rounds[n][k];targets[k]={value,10000+value,L"C:\\INERT\\Worker.exe",false,"cost-target","isolated-ports"};}
    if(aliasControls){
     bool seen200=false;
     for(std::size_t k=0;k<rounds[n].size();++k)if(rounds[n][k]==200){
      targets[k].role=seen200?"cost-alias":"cost-canonical";
      if(seen200)targets[k].birth=0;
      if(name=="alias-canonical-missing"&&n==1)targets[k].birth=0;
      if(name=="alias-birth-appears"&&n==0)targets[k].birth=seen200?10200:0;
      if(name=="alias-birth-appears"&&n==2)targets[k].birth=10201;
      if(name=="alias-earlier-zero"&&n==1)targets[k].birth=seen200?10200:0;
      if(name=="alias-normalized-path"&&seen200)targets[k].expectedImage=L"c:/inert/WORKER.exe";
      seen200=true;
     }
    }
    if(identityControls){processes[200]=Process{};if(n==1){
     if(name=="retained-exited")processes[200].alive=false;
     if(name=="retained-image-changed")processes[200].image=L"C:\\INERT\\Foreign.exe";
     if(name=="retained-session-changed")processes[200].session=8;
     if(name=="retained-birth-changed")targets[1].birth+=1;
     if(name=="retained-cpu-failed")processes[200].timesOk=false;
    }}
    const auto before=calls;frame=static_cast<unsigned>(n);const auto ownBefore=ownCalls;const auto sample=sampler.sample(true,1000+1000*n,name=="power-new-first"&&n?2:1,8,targets,rounds[n].size());json actualRates=json::array();
    const auto& rows=sample.at("processes");require(rows.size()==rounds[n].size(),"target list changed cardinality");
    for(std::size_t k=0;k<rounds[n].size();++k){const auto value=rounds[n][k];const auto& row=rows[k];require(row.at("pid")==value,"target order changed");const bool want=expectedRates[n].contains(value),actual=row.at("cpuRateAvailable")==true;
     if(actual)actualRates.push_back(value);if(actual!=want)++lost;
     if(aliasControls){
      std::size_t prior=0;while(prior<k&&rounds[n][prior]!=value)++prior;
      if(prior<k){require(row.contains("sameProcessAsRole"),"same verified process role opened an independent observation");auto expectedAlias=rows[prior];expectedAlias["role"]=row.at("role");expectedAlias["attribution"]=row.at("attribution");expectedAlias["sameProcessAsRole"]=rows[prior].at("role");require(row==expectedAlias,"alias row changed verified process counters");}
     }
     if(name=="failed-open-first"&&value==203)require(row.at("cpuStatus")=="process-open-failed"&&!row.contains("identityVerified"),"bad new target bypassed original process proof");
     else if(aliasControls&&name=="alias-birth-appears"&&n==2&&value==200){require(row.at("cpuStatus")=="process-birth-mismatch"&&!row.contains("identityVerified")&&!actual,"new nonzero birth expectation bypassed actual process birth proof");}
     else if(identityControls&&n==1&&value==200){
      const std::string reason=name=="retained-exited"?"process-exited":name=="retained-birth-changed"?"process-birth-mismatch":name=="retained-cpu-failed"?"cpu-read-failed":"process-identity-mismatch";
      require(row.at("cpuStatus")==reason&&!row.contains("identityVerified")&&!actual,"retained target failed to recheck current Windows identity/liveness/cpu proof");
     }else require(row.value("identityVerified",false),"original identity proof missing");
     if(actual)require(std::abs(row.at("cpuMsPerSec").get<double>()-15.0)<0.000001,"retained target included an invalid time or counter interval");
    }
    json expected=json::array();for(const auto value:expectedRates[n])expected.push_back(value);
    steps.push_back({{"frame",n},{"expectedRatePids",expected},{"actualRatePids",actualRates},{"opens",calls.value("open",0u)-before.value("open",0u)},{"closes",calls.value("close",0u)-before.value("close",0u)},{"ownReads",ownCalls-ownBefore}});samples.push_back(sample);
   }
   const auto queries=calls;const auto ownBefore=ownCalls;require(sampler.sample(false,10000,9,8,{},0).is_null(),"OFF target order result");require(handles.empty()&&ownCalls==ownBefore,"OFF leaked target handle or queried identity");
   auto afterQueries=calls,priorQueries=queries;afterQueries.erase("close");priorQueries.erase("close");require(afterQueries==priorQueries,"OFF executed non-close queries");
   const auto after=calls;require(sampler.sample(false,11000,10,8,{},0).is_null()&&calls==after,"closed target-order sampler performed IO");
   evidence.push_back({{"name",name},{"samples",samples},{"steps",steps},{"allPortsIncludingOff",calls},{"handlesAfterOff",handles.size()}});
  }
  std::cout<<json{{"kind",aliasControls?"actual-decky-cost-target-alias":identityControls?"actual-decky-cost-target-identity":"actual-decky-cost-target-order"},{"cases",evidence},{"lostOrIncorrectRates",lost},{"checks",checks},{"realProcessQueries",0},{"realSteamActions",0},{"defaultNativeCostCasesRun",false}}.dump()<<'\n';
  if(lost){std::cerr<<"retained sampler targets lost continuity after target-list churn\n";return 1;}return 0;
 }catch(const std::exception& error){if(aliasControls)std::cout<<json{{"kind","actual-decky-cost-target-alias"},{"failed",true},{"completedCases",evidence},{"error",error.what()},{"checks",checks},{"realProcessQueries",0},{"realSteamActions",0},{"defaultNativeCostCasesRun",false}}.dump()<<'\n';std::cerr<<error.what()<<'\n';return 2;}
}
