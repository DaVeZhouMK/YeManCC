#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "virtual_target_return_session.h"
// Product execution may reach only Environment ports. Session above retains
// real private file APIs; these fences catch accidental new direct OS/device IO.
#define DeviceIoControl forbidden_DeviceIoControl
#define CreateProcessW forbidden_CreateProcessW
#define OpenProcess forbidden_OpenProcess
#define GetProcessTimes forbidden_GetProcessTimes
#define SetupDiGetClassDevsW forbidden_SetupDiGetClassDevsW
#define CM_Get_DevNode_Status forbidden_CM_Get_DevNode_Status
#include "virtual_target_return_cold_execution.h"
namespace tr=ymcc::targetReturn;namespace cold=tr::coldExecution;using J=nlohmann::json;
namespace {
const std::string strong="windows-machine-guid:private130",weak="fixture-machine:weak";
const std::string A="HID\\VID_1234&PID_5678\\OWNED-A",C="HID\\VID_1234&PID_5678\\OWNED-C",B="HID\\VID_7777&PID_7777\\FOREIGN-B",root="USB\\VID_1234&PID_5678\\ROOT-A";
std::wstring outputRoot;J cases=J::array(),failures=J::array();unsigned assertions=0,caseSequence=0;std::string active;
void check(bool value,const char* assertion){++assertions;if(!value)failures.push_back({{"case",active},{"assertion",assertion}});}
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string read(const std::wstring& path){std::ifstream in(std::filesystem::path(path),std::ios::binary);need(bool(in),"read only own private file");return {std::istreambuf_iterator<char>(in),{}};}
void raw(const std::wstring& path,const std::string& bytes){std::ofstream out(std::filesystem::path(path),std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));need(bool(out),"write private test file");}
std::string encoded(const tr::Record& r){std::string bytes;need(tr::encode(r,bytes),"encode actual durable record");return bytes;}
tr::Origin origin(const std::string& id="origin130",const std::string& persona="elite",bool physical=true,unsigned protocol=1){tr::Origin o;o.transactionId=id;o.parent={1101,"12345"};o.host={2202,"23456"};o.hostInstanceId="host130";o.runId="run130";o.targetId="target130";o.epoch=1;o.persona=persona;o.physicalRoot=physical?root:"";o.cliWriterProtocol=protocol;return o;}
tr::AcceptedPsPin pin(bool lower=false){tr::AcceptedPsPin p;p.sdkRootInstanceId=lower?"swd\\hidmaestro_vid_054c_pid_0df2\\ps-root":"SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\PS-ROOT";p.hidInstanceIds={lower?"hid\\vid_054c&pid_0df2\\ps-hid":"HID\\VID_054C&PID_0DF2\\PS-HID"};p.bindingGeneration=1;return p;}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;explicit Handle(HANDLE value):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}Handle(const Handle&)=delete;};
enum class Fact {Proven,Alive,Unknown};
struct Environment {
    std::wstring directory,file;std::string binding;std::unique_ptr<tr::Session> session;
    bool active=true,ancestry=true,visibilityKnown=true,apply=true,ack=true,throwVisibility=false;
    Fact native=Fact::Proven,host=Fact::Proven,cli=Fact::Proven,target=Fact::Proven;
    unsigned failVisibilityFrom=0,visibilityCalls=0,targetCalls=0,proofCalls=0,unhideCalls=0,consumePaths=0,consumeOrigins=0,ioCount=0,expireAfter=0,lateWrites=0,failPathAt=0,failOriginAt=0,currentCalls=0,recordCalls=0;
    std::vector<std::string> hidden={A,B};J events=J::array(),mutations=J::array(),census=J::array(),storeFailures=J::array();
    explicit Environment(const std::string& machine=strong):binding(machine){directory=outputRoot+L"\\c"+std::to_wstring(++caseSequence);need(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"fresh private case directory");file=directory+L"\\record.json";reopen();}
    void reopen(){session.reset();session.reset(new tr::Session(tr::Session::open(file,binding)));}
    std::uint64_t revision()const{need(session&&session->record(),"actual ready Session record required");return session->record()->revision;}
    void addPath(const tr::Origin& o,const std::string& path=A){need(session->reservePath(revision(),o,path,{true,true,true}),"actual Session path seed commit");}
    void addTarget(const tr::Origin& o){need(session->reserveTarget(revision(),o,{true,true}),"actual Session target-only seed commit");}
    void addPin(const tr::Origin& o,const tr::AcceptedPsPin& p){need(session->attachAcceptedPsPin(revision(),o,p),"actual Session pin seed commit");}
    bool current(){++currentCalls;return active;}
    const tr::Record* record(){++recordCalls;return session->record();}
    void finish(const char* operation,bool mutation=false,J detail=J::object()){
        const bool before=active;if(mutation&&!before)++lateWrites;++ioCount;detail["operation"]=operation;detail["index"]=ioCount;detail["currentAtCall"]=before;detail["mutation"]=mutation;events.push_back(detail);if(expireAfter==ioCount)active=false;
    }
    bool nativeDead(const tr::ProcessIdentity& p){const bool ok=native==Fact::Proven;finish("nativeDead",false,{{"pid",p.pid},{"birth",p.creationTime},{"proven",ok}});return ok;}
    bool hostDead(const tr::ProcessIdentity& p){const bool ok=host==Fact::Proven;finish("hostDead",false,{{"pid",p.pid},{"birth",p.creationTime},{"proven",ok}});return ok;}
    bool writersQuiet(){const bool ok=cli==Fact::Proven;finish("writersQuiet",false,{{"proven",ok}});return ok;}
    bool targetAbsent(const std::vector<std::string>& known){++targetCalls;census.push_back(known);const bool ok=target==Fact::Proven;finish("targetAbsent",false,{{"knownIds",known},{"completeAbsence",ok}});return ok;}
    bool visibility(std::vector<std::string>& out){++visibilityCalls;if(throwVisibility){finish("visibility");throw std::runtime_error("controlled readback exception");}const bool ok=visibilityKnown&&(!failVisibilityFrom||visibilityCalls<failVisibilityFrom);if(ok)out=hidden;finish("visibility",false,{{"known",ok},{"hidden",hidden}});return ok;}
    bool pathProven(const std::string& physical,const std::string& path){++proofCalls;const bool ok=ancestry&&physical==root&&(tr::detail::fold(path)==tr::detail::fold(A)||tr::detail::fold(path)==tr::detail::fold(C));finish("pathProven",false,{{"root",physical},{"path",path},{"proven",ok}});return ok;}
    bool unhide(const std::string& path){++unhideCalls;mutations.push_back({{"operation","unhide"},{"path",path},{"current",active},{"ack",ack},{"applied",apply}});if(apply)hidden.erase(std::remove_if(hidden.begin(),hidden.end(),[&](const auto& p){return tr::detail::fold(p)==tr::detail::fold(path);}),hidden.end());finish("unhide",true,{{"path",path}});return ack;}
    bool consumePath(const tr::Origin& o,const std::string& path){
        ++consumePaths;std::unique_ptr<Handle> denyDelete;if(failPathAt==consumePaths){denyDelete.reset(new Handle(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)));need(denyDelete->valid(),"private destination handle denies replacement");}
        const auto before=encoded(*session->record()),diskBefore=read(file);const bool result=session->consumePath(revision(),o,path);if(!result)storeFailures.push_back({{"phase","path"},{"reason",session->error().reason},{"win32Error",session->error().win32Error},{"memoryUnchanged",before==encoded(*session->record())},{"diskUnchanged",diskBefore==read(file)}});finish("consumePath",true,{{"path",path},{"committed",result}});return result;
    }
    bool consumeOrigin(const tr::Origin& o){
        ++consumeOrigins;std::unique_ptr<Handle> denyDelete;if(failOriginAt==consumeOrigins){denyDelete.reset(new Handle(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)));need(denyDelete->valid(),"private origin destination handle denies replacement");}
        const auto before=encoded(*session->record()),diskBefore=read(file);const bool result=session->consumeOrigin(revision(),o);if(!result)storeFailures.push_back({{"phase","origin"},{"reason",session->error().reason},{"win32Error",session->error().win32Error},{"memoryUnchanged",before==encoded(*session->record())},{"diskUnchanged",diskBefore==read(file)}});finish("consumeOrigin",true,{{"origin",o.transactionId},{"committed",result}});return result;
    }
    J observe(const cold::Result& result){return {{"clean",result.clean()},{"reason",cold::reasonName(result.reason)},{"unhideAttempts",result.unhideAttempts},{"settledPaths",result.settledPaths},{"settledOrigins",result.settledOrigins},{"originKey",result.originKey},{"pathInstanceId",result.pathInstanceId},{"events",events},{"mutations",mutations},{"hiddenAfter",hidden},{"census",census},{"storeFailures",storeFailures},{"remaining",session->record()?J::parse(encoded(*session->record())):J(nullptr)},{"lateWrites",lateWrites},{"privateFile",std::string(file.begin(),file.end())}};}
    void verifySafety(){check(std::none_of(mutations.begin(),mutations.end(),[](const auto& m){return m.at("path")==B;}),"foreign path never reaches unhide");check(cold::containsPath(hidden,B),"foreign prehidden path remains");check(lateWrites==0,"no driver or persistent write after context expiry");}
};
J observation;
void runCase(const std::string& name,const std::function<void()>& body){active=name;observation=J::object();const auto old=assertions;const auto count=failures.size();try{body();}catch(const std::exception& e){check(false,(std::string("fixture-exception:")+e.what()).c_str());}observation["name"]=name;observation["assertions"]=assertions-old;observation["failures"]=failures.size()-count;cases.push_back(observation);}
void normalCase(const std::string& name,const std::function<void(Environment&)>& setup,cold::Reason reason,bool clean,unsigned unhide=0,bool preserve=false){runCase(name,[&]{Environment env;const auto o=origin();env.addPath(o);setup(env);const auto before=encoded(*env.session->record()),disk=read(env.file);const auto result=cold::run(env);check(result.reason==reason,"actual reason matches proof/settlement outcome");check(result.clean()==clean,"actual clean state matches committed duties");check(env.unhideCalls==unhide,"only bounded exact owned unhide attempted");if(preserve){check(encoded(*env.session->record())==before,"rejected proof leaves actual Session record unchanged");check(read(env.file)==disk,"rejected proof leaves actual file unchanged");}env.verifySafety();observation=env.observe(result);});}
void parserTests(){
    const std::vector<std::pair<std::string,std::string>> good={{"empty",""},{"whitespace"," \t\r\n\n"},{"one","--dev-hide \""+A+"\"\r\n"},{"two","--dev-hide \""+A+"\"\n--dev-hide \""+B+"\""},{"trim"," \t--dev-hide \""+A+"\" \r\n"}};
    for(const auto& [name,input]:good)runCase("parse/positive-"+name,[&]{std::vector<std::string> out={"preserve"};check(cold::parseHidden(input,out),"valid complete list accepted");check(out.size()==(name=="empty"||name=="whitespace"?0:name=="two"?2:1),"entire valid list parsed");});
    std::vector<std::pair<std::string,std::string>> bad={{"stderr","--dev-hide \""+A+"\"\nAccess denied\n"},{"stderr-before","ERROR: failed\n--dev-hide \""+A+"\"\n"},{"wrong-prefix","--dev-list \""+A+"\""},{"missing-quote","--dev-hide \""+A},{"bare-id",A},{"empty-id","--dev-hide \"\""},{"embedded-quote","--dev-hide \""+A+"\"stderr\""},{"duplicate","--dev-hide \""+A+"\"\n--dev-hide \""+A+"\""},{"duplicate-fold","--dev-hide \""+A+"\"\n--dev-hide \"hid\\vid_1234&pid_5678\\owned-a\""},{"nul",std::string("--dev-hide \"")+A+std::string("\0",1)+"\""},{"control","--dev-hide \"HID\\NODE\\BAD\tID\""},{"invalid-instance","--dev-hide \"HID\\..\\A\""},{"truncated-prefix","--dev-hide \"HID\\"},{"byte-limit",std::string(1024*1024+1,' ')},{"id-limit","--dev-hide \"HID\\NODE\\"+std::string(504,'A')+"\""}};
    for(const auto& [name,input]:bad)runCase("parse/reject-"+name,[&]{std::vector<std::string> out={"UNCHANGED"};const auto before=out;check(!cold::parseHidden(input,out),"malformed incomplete duplicate or bounded data rejected");check(out==before,"parser output unchanged on failure");});
    runCase("parse/exact-byte-bound",[]{std::vector<std::string> out={"old"};check(cold::parseHidden(std::string(1024*1024,' '),out)&&out.empty(),"exact byte limit remains valid whitespace");});
    runCase("parse/exact-id-bound",[]{const std::string id="HID\\NODE\\"+std::string(503,'A');std::vector<std::string> out;check(id.size()==512&&cold::parseHidden("--dev-hide \""+id+"\"",out)&&out==std::vector<std::string>{id},"exact instance-byte bound accepted");});
    runCase("parse/count-bound",[]{std::string input;for(unsigned n=0;n<4096;++n)input+="--dev-hide \"HID\\NODE\\ID-"+std::to_string(n)+"\"\n";std::vector<std::string> out;check(cold::parseHidden(input,out)&&out.size()==4096,"4096 complete list rows accepted");input+="--dev-hide \"HID\\NODE\\OVERFLOW\"\n";out={"UNCHANGED"};check(!cold::parseHidden(input,out)&&out==std::vector<std::string>{"UNCHANGED"},"4097th row rejected without output publication");});
}
void algorithmTests(){
    runCase("empty/missing-zero-environment-io",[]{Environment env;need(env.session->ready(),"actual Missing Session ready");const auto result=cold::run(env);check(result.clean()&&result.reason==cold::Reason::Complete,"empty record completes historical duty check");check(env.ioCount==0&&env.unhideCalls==0&&env.consumePaths==0&&env.consumeOrigins==0,"empty record performs zero Environment OS/driver/persistence IO");check(GetFileAttributesW(env.file.c_str())==INVALID_FILE_ATTRIBUTES,"empty run creates no record file");observation=env.observe(result);});
    runCase("empty/existing-zero-environment-io",[]{Environment env;need(tr::store(env.file,*env.session->record()).committed(),"real store empty existing record");env.reopen();const auto before=read(env.file);const auto result=cold::run(env);check(result.clean()&&env.ioCount==0,"existing empty record avoids every external leaf");check(read(env.file)==before,"existing empty file unchanged");observation=env.observe(result);});
    runCase("guard/store-unavailable",[]{Environment env;env.session.reset();raw(env.file,"{broken");env.reopen();need(!env.session->ready(),"actual Invalid Session retained");const auto result=cold::run(env);check(!result.clean()&&result.reason==cold::Reason::StoreUnavailable,"failed open is never an empty record");check(env.ioCount==0&&read(env.file)=="{broken","failed store open preserved without external work");observation=env.observe(result);});
    runCase("guard/weak-binding",[]{Environment env(weak);env.addPath(origin());const auto before=read(env.file);const auto result=cold::run(env);check(result.reason==cold::Reason::WeakBinding&&!result.clean(),"weak historical binding defers");check(env.ioCount==0&&env.unhideCalls==0&&read(env.file)==before,"weak binding authorizes no external work");observation=env.observe(result);});
    runCase("guard/legacy-protocol-zero",[]{Environment env;env.addPath(origin("legacy130","elite",true,0));const auto before=read(env.file);env.reopen();check(env.session->record()->obligations[0].origin.cliWriterProtocol==0,"real128 data roundtrip preserves protocol0");const auto result=cold::run(env);check(result.reason==cold::Reason::LegacyWriter&&!result.clean(),"legacy protocol never acquires writer proof");check(env.ioCount==0&&env.unhideCalls==0&&read(env.file)==before,"legacy duty stays durable with zero external IO");observation=env.observe(result);});
    normalCase("guard/context-retired",[](auto& e){e.active=false;},cold::Reason::ContextRetired,false,0,true);
    normalCase("guard/native-alive",[](auto& e){e.native=Fact::Alive;},cold::Reason::NativeWriterUnproven,false,0,true);
    normalCase("guard/native-unknown",[](auto& e){e.native=Fact::Unknown;},cold::Reason::NativeWriterUnproven,false,0,true);
    normalCase("guard/host-alive",[](auto& e){e.host=Fact::Alive;},cold::Reason::HostWriterUnproven,false,0,true);
    normalCase("guard/host-unknown",[](auto& e){e.host=Fact::Unknown;},cold::Reason::HostWriterUnproven,false,0,true);
    normalCase("guard/CLI-pending",[](auto& e){e.cli=Fact::Alive;},cold::Reason::CliWriterUnproven,false,0,true);
    normalCase("guard/CLI-unknown",[](auto& e){e.cli=Fact::Unknown;},cold::Reason::CliWriterUnproven,false,0,true);
    normalCase("guard/target-present",[](auto& e){e.target=Fact::Alive;},cold::Reason::TargetAbsenceUnproven,false,0,true);
    normalCase("guard/target-unknown",[](auto& e){e.target=Fact::Unknown;},cold::Reason::TargetAbsenceUnproven,false,0,true);
    normalCase("guard/visibility-unknown",[](auto& e){e.visibilityKnown=false;},cold::Reason::VisibilityUnproven,false,0,true);
    normalCase("guard/ancestry-unproven",[](auto& e){e.ancestry=false;},cold::Reason::PhysicalAncestryUnproven,false,0,true);
    normalCase("owned/exact-A-foreign-B",[](auto&){},cold::Reason::Complete,true,1);
    normalCase("owned/ACK-lost-readback-removed",[](auto& e){e.ack=false;},cold::Reason::Complete,true,1);
    normalCase("settlement/still-hidden-retains-duty",[](auto& e){e.apply=false;},cold::Reason::PathStillHidden,false,1,true);
    normalCase("settlement/readback-unknown-retains-duty",[](auto& e){e.failVisibilityFrom=2;},cold::Reason::VisibilityUnproven,false,1,true);
    normalCase("owned/already-not-hidden-no-unhide",[](auto& e){e.hidden={B};},cold::Reason::Complete,true,0);
    normalCase("guard/controlled-exception",[](auto& e){e.throwVisibility=true;},cold::Reason::Exception,false,0,true);
    runCase("target-only/complete-negative-census",[]{Environment env;env.addTarget(origin("target130","elite",false));const auto result=cold::run(env);check(result.clean()&&env.unhideCalls==0&&result.settledOrigins==1,"no-source target-only duty can settle by complete target/writer proof");check(env.targetCalls>=2&&!env.census.empty()&&env.census[0].empty(),"empty known pin set still reaches complete target census");check(env.session->record()->obligations.empty(),"actual Session target-only debt committed empty");env.verifySafety();observation=env.observe(result);});
    runCase("target-only/present-refuses-empty-pin-shortcut",[]{Environment env;env.addTarget(origin("target-present130","elite",false));env.target=Fact::Alive;const auto before=read(env.file);const auto result=cold::run(env);check(result.reason==cold::Reason::TargetAbsenceUnproven&&!result.clean(),"no known IDs does not prove every supported family absent");check(env.unhideCalls==0&&env.consumeOrigins==0&&read(env.file)==before,"present target-only debt remains on disk");env.verifySafety();observation=env.observe(result);});
    runCase("pin/repeated-historical-case-fold-dedup",[]{Environment env;const auto one=origin("pin-one130","dualsense-edge",false),two=origin("pin-two130","dualsense-edge",false);env.addPin(one,pin());env.addPin(two,pin(true));env.reopen();const auto result=cold::run(env);check(result.clean()&&result.settledOrigins==2,"historical duplicate pins settle separate exact origins");check(!env.census.empty()&&env.census[0].size()==2,"same historical root and HID case variants deduplicated");check(std::all_of(env.census.begin(),env.census.end(),[](const auto& ids){return ids.size()==2;}),"pins flow only as negative census witnesses");check(env.unhideCalls==0&&env.session->record()->obligations.empty(),"pins never authorize a path mutation or current admission");env.verifySafety();observation=env.observe(result);});
    runCase("store/path-failure-real-private-replace",[]{Environment env;env.addPath(origin());env.failPathAt=1;const auto before=read(env.file);const auto result=cold::run(env);check(result.reason==cold::Reason::PathCommitFailed&&!result.clean(),"actual failed store cannot publish settled path");check(read(env.file)==before&&!env.session->record()->obligations.empty(),"failed path commit preserves file and Session duty");check(!env.storeFailures.empty()&&env.storeFailures[0]["memoryUnchanged"]==true&&env.storeFailures[0]["diskUnchanged"]==true,"real Session preserves old candidate on private sharing violation");env.verifySafety();observation=env.observe(result);});
    runCase("store/origin-failure-real-private-replace",[]{Environment env;env.addTarget(origin("origin-only130","elite",false));env.failOriginAt=1;const auto before=read(env.file);const auto result=cold::run(env);check(result.reason==cold::Reason::OriginCommitFailed&&!result.clean(),"actual failed origin settlement defers");check(env.consumeOrigins==1&&read(env.file)==before&&!env.session->record()->obligations.empty(),"origin store failure keeps durable target duty");env.verifySafety();observation=env.observe(result);});
    runCase("store/partial-commit-reopen-resume",[]{Environment env;const auto o=origin("partial130");env.addPath(o);env.addPath(o,C);env.addTarget(o);env.hidden={A,C,B};env.failPathAt=2;const auto first=cold::run(env);check(first.reason==cold::Reason::PathCommitFailed&&first.settledPaths==1,"first real path commit survives second store failure");const auto committed=J::parse(read(env.file));check(committed["obligations"][0]["paths"].size()==1&&committed["obligations"][0]["paths"][0]["instanceId"]==C,"private file contains only uncommitted path duty");env.failPathAt=0;env.reopen();const auto previousUnhides=env.unhideCalls;const auto second=cold::run(env);check(second.clean()&&env.session->record()->obligations.empty(),"new actual Session resumes committed historical debt");check(env.unhideCalls==previousUnhides,"already-removed paths are not reissued on resume");env.verifySafety();observation=env.observe(second);observation["firstResult"]={{"reason",cold::reasonName(first.reason)},{"settledPaths",first.settledPaths}};observation["intermediateDisk"]=committed;});
}
void expiryMatrix(){
    Environment reference;const auto o=origin("matrix130");reference.addPath(o);reference.addTarget(o);const auto baseline=cold::run(reference);need(baseline.clean(),"reference IO sequence must complete");const auto trace=reference.events;
    for(unsigned index=1;index<=trace.size();++index){const auto operation=trace[index-1]["operation"].get<std::string>();runCase("context/expire-after-"+std::to_string(index)+"-"+operation,[&,index]{Environment env;const auto exact=origin("matrix130");env.addPath(exact);env.addTarget(exact);env.expireAfter=index;const auto result=cold::run(env);check(!env.active&&env.ioCount>=index,"controlled original context expires at selected real IO return");env.verifySafety();check(env.lateWrites==0,"expiry after any IO forbids later unhide or Session consumption");observation=env.observe(result);observation["expiredAfter"]=index;observation["referenceOperation"]=trace[index-1]["operation"];});}
}
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;outputRoot=argv[1];parserTests();algorithmTests();expiryMatrix();J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures},{"cases",cases},{"actualConsumer","native coldExecution::run(Environment&)"},{"actualSessionAndStore",true},{"realPrivateFileIo",true},{"controlledOSAndDriverPorts",true},{"ordinaryCliOrHostRuns",0},{"realDeviceCalls",0},{"currentAdmissionApiAvailable",false}};raw(argv[2],result.dump(2)+"\n");std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;}