#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
#include "hidhide_cli_writer_record.h"

// All wrappers forward to real private-file I/O except an explicitly selected
// fault. Only this fixture can launch owned fixture children; production cannot.
namespace io {
enum class Role { Owner, Record, Temporary };
enum class Fault { None, TempCreate, Write, ShortWrite, Flush, Close, Move, Read, ShortRead,
    OwnerType, RecordType, TempType, OwnerInfoChange, ParentReparse, OwnerReparse, RecordReparse, RecordAttributes };
inline Fault fault=Fault::None;
inline std::map<HANDLE,Role> handles;
inline unsigned writes=0,flushes=0,moves=0,ownerWrites=0,ownerInfos=0,creates=0;
inline bool closeFailed=false,crashAfterCreate=false;
inline std::vector<std::string> order;
Role role(const std::wstring& p){if(p.ends_with(L".owner"))return Role::Owner;return p.find(L".cli-writer.tmp.")!=std::wstring::npos?Role::Temporary:Role::Record;}
Role role(HANDLE h){const auto i=handles.find(h);return i==handles.end()?Role::Record:i->second;}
void reset(Fault selected=Fault::None){fault=selected;writes=flushes=moves=ownerWrites=ownerInfos=creates=0;closeFailed=false;order.clear();}
HANDLE create(LPCWSTR p,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templateFile){
    ++creates;const auto r=role(p);
    if(r==Role::Temporary&&fault==Fault::TempCreate){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    const auto h=::CreateFileW(p,access,sharing,security,creation,flags,templateFile);
    if(h!=INVALID_HANDLE_VALUE){handles.emplace(h,r);if(r==Role::Temporary)order.push_back("create");}
    if(h!=INVALID_HANDLE_VALUE&&r==Role::Owner&&creation==CREATE_NEW&&crashAfterCreate)::ExitProcess(73);
    return h;
}
BOOL close(HANDLE h){
    const auto r=role(h);
    if(r==Role::Temporary&&fault==Fault::Close&&!closeFailed){closeFailed=true;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    const auto ok=::CloseHandle(h);if(ok){handles.erase(h);if(r==Role::Temporary)order.push_back("close");}return ok;
}
BOOL write(HANDLE h,LPCVOID data,DWORD count,LPDWORD written,LPOVERLAPPED overlap){
    if(role(h)==Role::Owner)++ownerWrites;else ++writes;
    order.push_back("write");
    if(fault==Fault::Write){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return ::WriteFile(h,data,fault==Fault::ShortWrite&&count?count-1:count,written,overlap);
}
BOOL flush(HANDLE h){++flushes;order.push_back("flush");if(fault==Fault::Flush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);}
BOOL move(LPCWSTR from,LPCWSTR to,DWORD flags){++moves;order.push_back("move");if(fault==Fault::Move){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::MoveFileExW(from,to,flags);}
BOOL read(HANDLE h,LPVOID data,DWORD count,LPDWORD got,LPOVERLAPPED overlap){
    if(fault==Fault::Read){SetLastError(ERROR_READ_FAULT);return FALSE;}
    return ::ReadFile(h,data,fault==Fault::ShortRead&&count>1?count-1:count,got,overlap);
}
DWORD attributes(LPCWSTR p){
    const auto a=::GetFileAttributesW(p);
    if(fault==Fault::RecordAttributes&&role(p)==Role::Record&&a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY)){SetLastError(ERROR_ACCESS_DENIED);return INVALID_FILE_ATTRIBUTES;}
    if(a==INVALID_FILE_ATTRIBUTES)return a;
    if(fault==Fault::ParentReparse&&(a&FILE_ATTRIBUTE_DIRECTORY)&&std::wstring(p).size()>3)return a|FILE_ATTRIBUTE_REPARSE_POINT;
    if(!(a&FILE_ATTRIBUTE_DIRECTORY)&&((fault==Fault::OwnerReparse&&role(p)==Role::Owner)||(fault==Fault::RecordReparse&&role(p)==Role::Record)))return a|FILE_ATTRIBUTE_REPARSE_POINT;
    return a;
}
DWORD type(HANDLE h){const auto r=role(h);if((r==Role::Owner&&fault==Fault::OwnerType)||(r==Role::Record&&fault==Fault::RecordType)||(r==Role::Temporary&&fault==Fault::TempType)){SetLastError(ERROR_INVALID_DATA);return FILE_TYPE_UNKNOWN;}return ::GetFileType(h);}
BOOL info(HANDLE h,LPBY_HANDLE_FILE_INFORMATION out){const auto ok=::GetFileInformationByHandle(h,out);if(role(h)==Role::Owner){++ownerInfos;if(ok&&fault==Fault::OwnerInfoChange&&ownerInfos>=2)++out->ftLastWriteTime.dwLowDateTime;}return ok;}
}
#define CreateFileW io::create
#define CloseHandle io::close
#define WriteFile io::write
#define FlushFileBuffers io::flush
#define MoveFileExW io::move
#define ReadFile io::read
#define GetFileAttributesW io::attributes
#define GetFileType io::type
#define GetFileInformationByHandle io::info
#include "hidhide_cli_writer_store.h"
#undef CreateFileW
#undef CloseHandle
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReadFile
#undef GetFileAttributesW
#undef GetFileType
#undef GetFileInformationByHandle

namespace cw=ymcc::hidhideCliWriter;
using J=nlohmann::json;
static_assert(!std::is_copy_constructible_v<cw::Session>&&!std::is_move_constructible_v<cw::Session>);
static_assert(!std::is_copy_assignable_v<cw::Session>&&!std::is_move_assignable_v<cw::Session>);
const std::string machine="private-writer-machine-119";
unsigned assertions=0,children=0,crashChildren=0;
std::string activeCase;
J failures=J::array(),cases=J::array(),childReports=J::array();
void check(bool ok,const std::string& label){++assertions;if(!ok)failures.push_back({{"case",activeCase},{"assertion",label}});}
void need(bool ok,const std::string& label){check(ok,label);if(!ok)throw std::runtime_error("fixture setup: "+label);}
void run(const std::string& name,const std::function<void()>& body){
    activeCase=name;const auto count=assertions;const auto bad=failures.size();io::reset();
    try{body();}catch(const std::exception& e){check(false,std::string("scenario exception: ")+e.what());}
    check(io::handles.empty(),"all private file handles released");io::reset();
    cases.push_back({{"name",name},{"assertions",assertions-count},{"failures",failures.size()-bad}});
}
cw::Attempt attempt(const std::string& id="attempt-119"){cw::Attempt a;a.attemptId=id;a.parent={11901,"134368044937265940"};a.jobName=std::string(cw::jobPrefix)+id;return a;}
cw::ProcessIdentity child(){return {11902,"134368044937266000"};}
cw::Record empty(){cw::Record r;r.machineBinding=machine;return r;}
cw::Record reserved(){auto r=empty();r.active=attempt();return r;}
cw::Record running(){auto r=reserved();r.active->stage=cw::Stage::MayRun;r.active->child=child();return r;}
std::string encoded(const cw::Record& r){std::string bytes;if(!cw::encode(r,bytes))throw std::runtime_error("fixture record encoding failed");return bytes;}
std::string snapshot(const cw::Session& s){if(!s.record())throw std::runtime_error("fixture session not ready");return encoded(*s.record());}
std::uint64_t rev(const cw::Session& s){return s.record()?s.record()->revision:0;}
void raw(const std::wstring& file,const std::string& bytes){std::ofstream f(std::filesystem::path(file),std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));if(!f)throw std::runtime_error("private raw file write failed");}
std::string bytes(const std::wstring& file){std::ifstream f(std::filesystem::path(file),std::ios::binary);if(!f)throw std::runtime_error("private raw file read failed");return {std::istreambuf_iterator<char>(f),{}};}
void rejected(const std::string& input){auto out=running();out.machineBinding="unchanged-on-parse-failure";const auto before=encoded(out);std::string error;check(!cw::decode(input,out,&error),"strict decode rejects input");check(encoded(out)==before,"failed decode preserves caller output");}
std::wstring executable(){std::wstring p(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));if(!n||n==p.size())throw std::runtime_error("owned fixture executable path unavailable");p.resize(n);return p;}
std::wstring quote(const std::wstring& p){if(p.find(L'"')!=std::wstring::npos||(!p.empty()&&p.back()==L'\\'))throw std::runtime_error("invalid controlled fixture argument");return L"\""+p+L"\"";}
DWORD ownedChild(const std::vector<std::wstring>& args){
    auto command=quote(executable());for(const auto& a:args)command+=L" "+quote(a);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("owned fixture child creation failed");
    ++children;const auto wait=WaitForSingleObject(process.hProcess,8000);DWORD code=0;
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,98);WaitForSingleObject(process.hProcess,2000);CloseHandle(process.hThread);CloseHandle(process.hProcess);throw std::runtime_error("owned fixture child bounded wait failed");}
    GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);if(code==73)++crashChildren;return code;
}
void probe(const std::wstring& file,const std::wstring& report,bool expectedReady){
    const auto result=ownedChild({L"--probe",file,report,expectedReady?L"ready":L"blocked"});
    check(result==0,"independent process observes expected exclusive owner state");
    childReports.push_back(J::parse(bytes(report)));
}
int wmain(int argc,wchar_t** argv){
    if(argc==5&&std::wstring(argv[1])==L"--probe"){
        auto s=cw::Session::open(argv[2],machine);const bool expected=std::wstring(argv[4])==L"ready";
        raw(argv[3],J({{"pass",s.ready()==expected},{"ready",s.ready()},{"expectedReady",expected},{"error",s.error().reason},{"hasRecord",s.record()!=nullptr}}).dump());return s.ready()==expected?0:1;
    }
    if(argc==3&&std::wstring(argv[1])==L"--crash-owner"){
        io::crashAfterCreate=true;auto s=cw::Session::open(argv[2],machine);return s.ready()?74:75;
    }
    if(argc!=3)return 64;
    const std::filesystem::path root(argv[1]);if(!root.is_absolute()||!std::filesystem::is_directory(root))return 65;
    const auto file=[&](const wchar_t* name){return (root/name).wstring();};
    run("record/empty-is-valid-not-missing",[]{auto r=empty();cw::Record out;check(cw::decode(encoded(r),out),"empty active round-trips");check(!out.active&&out.revision==1&&out.schemaVersion==1,"explicit empty writer envelope");});
    run("record/reserved-and-mayrun-roundtrip",[]{for(const auto& r:{reserved(),running()}){cw::Record out;check(cw::decode(encoded(r),out),"attempt round-trips");check(encoded(out)==encoded(r),"all exact process/job/stage bytes retained");}});
    run("record/identity-boundaries",[]{auto r=reserved();r.active->attemptId=std::string(100,'a');r.active->jobName=std::string(cw::jobPrefix)+r.active->attemptId;r.active->parent={0xffffffffu,"18446744073709551615"};check(cw::validRecord(r),"100-byte token and uint32/uint64 maxima valid");r.active->attemptId+='a';r.active->jobName+='a';check(!cw::validRecord(r),"101-byte token rejected");});
    const auto base=J::parse(encoded(reserved()));
    std::vector<std::pair<std::string,J>> malformed;
    for(const auto* key:{"schemaVersion","machineBinding","revision","active"}){auto j=base;j.erase(key);malformed.push_back({std::string("missing-")+key,j});}
    for(const auto* key:{"command","executablePath","targetId","cleanupAllowed"}){auto j=base;j[key]="forbidden";malformed.push_back({std::string("unknown-")+key,j});}
    for(const auto& value:std::vector<J>{nullptr,true,"1",-1,0,1.5,2}){auto j=base;j["schemaVersion"]=value;malformed.push_back({"schema-type-or-version",j});}
    for(const auto& value:std::vector<J>{nullptr,true,"1",-1,0,1.5}){auto j=base;j["revision"]=value;malformed.push_back({"revision-type",j});}
    for(const auto& value:std::vector<J>{nullptr,true,0,"",std::string(4097,'m'),"bad\ntext"}){auto j=base;j["machineBinding"]=value;malformed.push_back({"machine-type-or-text",j});}
    for(const auto* key:{"attemptId","parent","child","jobName","stage"}){auto j=base;j["active"].erase(key);malformed.push_back({std::string("attempt-missing-")+key,j});}
    for(const auto* key:{"command","executablePath","host","target","grantColdCleanup"}){auto j=base;j["active"][key]="forbidden";malformed.push_back({std::string("attempt-unknown-")+key,j});}
    for(const auto& value:std::vector<J>{nullptr,true,0,"","..","a/b","a\\b","a b",std::string(101,'a')}){auto j=base;j["active"]["attemptId"]=value;malformed.push_back({"attempt-token",j});}
    for(const auto& value:std::vector<J>{nullptr,true,0,"","local\\YMCC.HidHide.Cli.attempt-119","Global\\YMCC.HidHide.Cli.attempt-119","Local\\YMCC.HidHide.Cli.other"}){auto j=base;j["active"]["jobName"]=value;malformed.push_back({"exact-job-binding",j});}
    for(const auto& value:std::vector<J>{nullptr,true,0,"MayRun","unknown","may-run"}){auto j=base;j["active"]["stage"]=value;malformed.push_back({"stage-or-child-shape",j});}
    for(const auto& value:std::vector<J>{nullptr,true,"1",-1,0,1.5,4294967296ULL}){auto j=base;j["active"]["parent"]["pid"]=value;malformed.push_back({"parent-pid",j});}
    for(const auto& value:std::vector<J>{nullptr,123,"","0","01","-1","1.0","18446744073709551616"}){auto j=base;j["active"]["parent"]["creationTime"]=value;malformed.push_back({"parent-birth",j});}
    {auto j=base;j["active"]["parent"]["extra"]=1;malformed.push_back({"parent-unknown",j});}
    {auto j=J::parse(encoded(running()));j["active"]["child"]["pid"]=0;malformed.push_back({"child-invalid",j});j=base;j["active"]["child"]={{"pid",3},{"creationTime","4"}};malformed.push_back({"reserved-with-child",j});}
    unsigned index=0;for(const auto& [label,j]:malformed){const auto name="decode/"+label+"/"+std::to_string(++index);run(name,[&]{rejected(j.dump());});}
    run("decode/duplicate-root",[&]{auto s=base.dump();s.insert(1,"\"revision\":1,");rejected(s);});
    run("decode/duplicate-nested-and-escaped",[&]{auto s=base.dump();const auto at=s.find("\"parent\":{");s.insert(at+10,"\"pid\":11901,");rejected(s);s=base.dump();s.insert(1,"\"revis\\u0069on\":1,");rejected(s);});
    run("decode/bounded-and-exact-JSON",[&]{rejected("");rejected("null");rejected("[]");rejected(base.dump()+"{}");rejected(std::string("\xef\xbb\xbf")+base.dump());auto zero=base.dump();zero.push_back('\0');rejected(zero);rejected("{\"schemaVersion\":1,/*comment*/}");rejected("{\"schemaVersion\":1,}");rejected(std::string(65537,' '));auto padded=base.dump();padded.append(65536-padded.size(),' ');cw::Record out;check(cw::decode(padded,out),"exact64KiB is accepted");rejected(padded+" ");});
    run("record/invalid-encode-output-preserved",[]{auto r=reserved();r.active->stage=static_cast<cw::Stage>(73);std::string out="sentinel";check(!cw::encode(r,out)&&out=="sentinel","bad enum does not publish bytes");r=reserved();r.active->child=child();check(!cw::encode(r,out),"Reserved forbids child even before encoding");r=running();r.active->child.reset();check(!cw::encode(r,out),"MayRun requires child even before encoding");});
    run("transform/revision-and-exact-binding",[]{auto r=empty();cw::Record out;check(!cw::reserve(r,0,attempt(),out),"stale reserve rejected");need(cw::reserve(r,1,attempt(),out),"reserve candidate");check(out.revision==2&&out.active->stage==cw::Stage::Reserved,"reserve increments once");r=out;check(!cw::reserve(r,2,attempt(),out),"duplicate active reserve rejected");check(!cw::markMayRun(r,1,r.active->attemptId,child(),out),"stale mark rejected");check(!cw::markMayRun(r,2,"different",child(),out),"wrong attempt rejected");check(!cw::markMayRun(r,2,r.active->attemptId,{0,"1"},out),"MayRun before valid child rejected");need(cw::markMayRun(r,2,r.active->attemptId,child(),out),"mark candidate");check(out.revision==3&&out.active->jobName==r.active->jobName&&ymcc::targetReturn::detail::sameProcess(out.active->parent,r.active->parent),"exact parent/job retained");r=out;check(!cw::markMayRun(r,3,r.active->attemptId,{child().pid,"999"},out),"same PID different birth cannot rebind");check(cw::markMayRun(r,3,r.active->attemptId,child(),out)&&out.revision==3,"same child exact replay is no-op");check(!cw::settle(r,2,r.active->attemptId,out),"stale settle rejected");check(!cw::settle(r,3,"other",out),"other attempt cannot settle");check(cw::settle(r,3,r.active->attemptId,out)&&!out.active&&out.revision==4,"exact settle clears writer only");r=out;check(!cw::settle(r,4,"attempt-119",out),"empty active grants no exact settle");});
    run("transform/revision-overflow",[]{auto r=empty();r.revision=UINT64_MAX;cw::Record out;check(!cw::reserve(r,r.revision,attempt(),out),"reserve overflow rejected");r=reserved();r.revision=UINT64_MAX;check(!cw::markMayRun(r,r.revision,r.active->attemptId,child(),out),"mark overflow rejected");r=running();r.revision=UINT64_MAX;check(cw::markMayRun(r,r.revision,r.active->attemptId,child(),out)&&out.revision==UINT64_MAX,"no-op does not overflow");check(!cw::settle(r,r.revision,r.active->attemptId,out),"settle overflow rejected");});
    run("session/missing-versus-empty",[&]{const auto p=file(L"missing.json");check(cw::load(p,machine).state==cw::LoadState::Missing,"Missing is independent");{auto s=cw::Session::open(p,machine);need(s.ready(),"open missing session");check(s.openedFrom()==cw::LoadState::Missing&&rev(s)==1&&!s.record()->active,"Missing creates in-memory revision1 empty");check(!std::filesystem::exists(p)&&io::writes==0&&io::ownerWrites==0,"open writes no record or owner bytes");}{check(bytes(p+L".owner").empty(),"owner persists as zero-byte sentinel");}raw(p,encoded(empty()));auto s=cw::Session::open(p,machine);check(s.ready()&&s.openedFrom()==cw::LoadState::Valid&&!s.record()->active,"stored empty is Valid, not Missing");});
    run("session/commit-order-and-reopen",[&]{const auto p=file(L"committed.json");{auto s=cw::Session::open(p,machine);need(s.ready(),"ready");io::reset();need(s.reserve(1,attempt()),"persistent reserve");check(io::order==std::vector<std::string>({"create","write","flush","close","move"}),"full write then Flush then close then atomic Move");check(bytes(p)==snapshot(s),"file and memory publish same candidate");need(s.markMayRun(2,"attempt-119",child()),"persistent child mark");check(rev(s)==3,"two updates advance revisions");}auto s=cw::Session::open(p,machine);check(s.ready()&&rev(s)==3&&s.record()->active->stage==cw::Stage::MayRun,"reopen retains active child");check(s.settle(3,"attempt-119")&&!s.record()->active&&rev(s)==4,"settle publishes empty active record");});
    run("session/noop-and-rejected-requests-no-writes",[&]{const auto p=file(L"noop.json");auto s=cw::Session::open(p,machine);need(s.ready()&&s.reserve(1,attempt())&&s.markMayRun(2,"attempt-119",child()),"seed active MayRun");const auto before=bytes(p);io::reset(io::Fault::Write);check(s.markMayRun(3,"attempt-119",child()),"idempotent exact child succeeds");check(io::writes==0&&io::flushes==0&&io::moves==0&&bytes(p)==before,"idempotent replay never writes");check(!s.markMayRun(2,"attempt-119",child())&&!s.reserve(3,attempt())&&!s.settle(3,"other"),"stale/duplicate/wrong requests rejected");check(io::writes==0&&snapshot(s)==before,"rejections preserve file and memory");io::reset();});
    for(const auto f:{io::Fault::TempCreate,io::Fault::Write,io::Fault::ShortWrite,io::Fault::Flush,io::Fault::Close,io::Fault::Move,io::Fault::TempType}){
        const auto label=f==io::Fault::Flush?"store/flush-preserves":std::string("store/fault-preserves/")+std::to_string(int(f));
        run(label,[&]{const auto p=file((L"fault-"+std::to_wstring(int(f))+L".json").c_str());auto s=cw::Session::open(p,machine);need(s.ready()&&s.reserve(1,attempt()),"seed old file");const auto before=bytes(p),memory=snapshot(s);io::reset(f);check(!s.markMayRun(2,"attempt-119",child()),"fault update rejected");check(snapshot(s)==memory&&bytes(p)==before&&rev(s)==2,"fault preserves old file and memory");check(s.error().code==cw::SessionErrorCode::RetryableIo,"fault reported explicitly");io::reset();check(s.markMayRun(2,"attempt-119",child()),"same valid session can retry once explicitly");});
    }
    run("store/real-replacement-lock-preserves",[&]{const auto p=file(L"locked-destination.json");auto s=cw::Session::open(p,machine);need(s.ready()&&s.reserve(1,attempt()),"seed actual destination");const auto before=bytes(p);HANDLE reader=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(reader!=INVALID_HANDLE_VALUE,"real reader omits delete share");check(!s.markMayRun(2,"attempt-119",child()),"real replacement sharing conflict rejected");check(snapshot(s)==before&&bytes(p)==before,"real failure preserves memory and old inode bytes");CloseHandle(reader);});
    run("session/invalid-IO-and-machine-never-empty",[&]{unsigned n=0;for(const auto& value:std::vector<std::string>{"", "{}", "broken",encoded([]{auto r=empty();r.machineBinding="foreign";return r;}())}){const auto p=file((L"invalid-"+std::to_wstring(++n)+L".json").c_str());raw(p,value);auto s=cw::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==cw::SessionErrorCode::Invalid,"invalid/mismatch is not empty fallback");check(!s.reserve(1,attempt())&&bytes(p)==value,"failed session cannot publish");}for(const auto f:{io::Fault::Read,io::Fault::ShortRead,io::Fault::RecordAttributes}){const auto p=file((L"read-"+std::to_wstring(int(f))+L".json").c_str());raw(p,encoded(reserved()));io::reset(f);{auto s=cw::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==cw::SessionErrorCode::RetryableIo,"IO is not Missing or empty");}io::reset();}});
    run("session/import-historical-dead-identities-no-authority",[&]{const auto p=file(L"historical.json");auto r=running();r.active->parent={0xfffffffeu,"1"};r.active->child=cw::ProcessIdentity{0xfffffffdu,"2"};raw(p,encoded(r));auto s=cw::Session::open(p,machine);check(s.ready()&&snapshot(s)==encoded(r),"historical identities import without process probes or mutation");check(!s.reserve(1,attempt("new")),"historical active writer still blocks a second reservation");});
    run("path/explicit-absolute-and-existing-parent",[&]{for(const auto& p:std::vector<std::wstring>{L"relative.json",L"C:drive-relative.json",L"\\\\server\\share\\record.json",file(L"..\\escape.json"),file(L"ads:stream"),file(L"NUL.json"),file(L"trail. ")}){auto s=cw::Session::open(p,machine);check(!s.ready()&&!s.record(),"invalid explicit path rejected");}auto s=cw::Session::open(file(L"missing-parent\\writer.json"),machine);check(!s.ready()&&s.error().code==cw::SessionErrorCode::RetryableIo,"missing parent is IO, never auto-created");});
    run("owner/unknown-existing-content",[&]{const auto p=file(L"nonempty-owner.json");raw(p+L".owner","unknown-owner-content");auto s=cw::Session::open(p,machine);check(!s.ready()&&s.error().code==cw::SessionErrorCode::Invalid,"nonempty owner rejected");check(bytes(p+L".owner")=="unknown-owner-content"&&!std::filesystem::exists(p),"unknown owner is never truncated or repaired");});
    run("owner-and-record/regular-singlelink",[&]{const auto owner=file(L"hardlink-owner.json");raw(owner+L".owner","");need(CreateHardLinkW(file(L"owner-alias").c_str(),(owner+L".owner").c_str(),nullptr)!=FALSE,"private actual owner hardlink created");{auto s=cw::Session::open(owner,machine);check(!s.ready(),"multi-link owner rejected");}const auto record=file(L"hardlink-record.json");raw(record,encoded(reserved()));need(CreateHardLinkW(file(L"record-alias").c_str(),record.c_str(),nullptr)!=FALSE,"private actual record hardlink created");{auto s=cw::Session::open(record,machine);check(!s.ready()&&s.error().code==cw::SessionErrorCode::Invalid,"multi-link record rejected");}const auto directory=file(L"record-directory.json");std::filesystem::create_directory(directory);{auto s=cw::Session::open(directory,machine);check(!s.ready(),"directory record rejected");}});
    run("owner-and-record/unsafe-attributes-and-info",[&]{for(const auto f:{io::Fault::OwnerType,io::Fault::RecordType,io::Fault::OwnerInfoChange,io::Fault::ParentReparse,io::Fault::OwnerReparse,io::Fault::RecordReparse}){const auto p=file((L"unsafe-"+std::to_wstring(int(f))+L".json").c_str());raw(p,encoded(reserved()));raw(p+L".owner","");io::reset(f);{auto s=cw::Session::open(p,machine);check(!s.ready()&&!s.record(),"unknown/reparse/changed identity rejected");}io::reset();}});
    run("lock/other-process-writer-blocked",[&]{const auto p=file(L"process-lock.json");{auto s=cw::Session::open(p,machine);need(s.ready(),"first session owns missing file");probe(p,file(L"probe-missing.json"),false);need(s.reserve(1,attempt()),"first writer persists");probe(p,file(L"probe-existing.json"),false);{auto same=cw::Session::open(p,machine);check(!same.ready(),"second session in same process excluded");}}probe(p,file(L"probe-released.json"),true);});
    for(const bool existing:{false,true})run(existing?"crash/zero-owner-existing-record":"crash/zero-owner-missing-record",[&]{const auto p=file(existing?L"crash-existing.json":L"crash-missing.json");const auto before=encoded(running());if(existing)raw(p,before);check(ownedChild({L"--crash-owner",p})==73,"child abruptly exits immediately after zero-byte CREATE_NEW owner");check(bytes(p+L".owner").empty(),"crash leaves exact zero-byte regular owner");{auto s=cw::Session::open(p,machine);check(s.ready(),"new session acquires crashed owner without repair");if(existing)check(snapshot(s)==before&&bytes(p)==before,"existing record intact through creation crash");else check(rev(s)==1&&!s.record()->active&&!std::filesystem::exists(p),"missing record still loads fresh empty state");}probe(p,file(existing?L"probe-crash-existing.json":L"probe-crash-missing.json"),true);});
    const J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"realPrivateFileIo",true},{"realIndependentNativeProcesses",children},{"ownerCreationCrashProcesses",crashChildren},{"childRuns",childReports},{"normalProductHostRealCliOrDevicesExecuted",false},{"JobOrLivenessCallsInProduction",false},{"scope","Strict writer data + bounded synchronous owner session only; no lifecycle or cleanup authority"}};
    raw(argv[2],result.dump(2)+"\n");std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures.size()},{"children",children}}).dump()<<"\n";return failures.empty()?0:1;
}