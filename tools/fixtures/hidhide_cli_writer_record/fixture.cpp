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
Role role(const std::wstring& p){if(p.ends_with(L".owner"))return Role::Owner;return (p.find(L".cli-writer.tmp.")!=std::wstring::npos||p.find(L".native-refresh-writer.tmp.")!=std::wstring::npos)?Role::Temporary:Role::Record;}
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
#include "native_refresh_writer_store.h"
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
namespace nw=ymcc::nativeRefreshWriter;
using J=nlohmann::json;
static_assert(!std::is_copy_constructible_v<cw::Session>&&!std::is_move_constructible_v<cw::Session>);
static_assert(!std::is_copy_assignable_v<cw::Session>&&!std::is_move_assignable_v<cw::Session>);
static_assert(std::is_same_v<nw::ProcessIdentity,ymcc::targetReturn::ProcessIdentity>);
static_assert(noexcept(std::declval<nw::Session&>().duplicateOwnerLeaseForChild(std::declval<HANDLE&>())));
static_assert(!std::is_copy_constructible_v<nw::Session>&&!std::is_move_constructible_v<nw::Session>);
static_assert(!std::is_copy_assignable_v<nw::Session>&&!std::is_move_assignable_v<nw::Session>);
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

nw::Attempt nativeAttempt(const std::string& id="native-attempt-119"){
    nw::Attempt a;a.attemptId=id;a.parent={11901,"134368044937265940"};a.jobName=std::string(nw::jobPrefix)+id;
    a.physicalRoot="USB\\VID_2F24&PID_0135\\private-root";
    a.deviceInstanceId="HID\\VID_2F24&PID_0135&MI_00\\private-node";
    a.locationPaths={"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(1)","ACPI(_SB_)#ACPI(XHC_)#USB(1)"};return a;
}
nw::Record nativeRecord(bool withAttempt=true){nw::Record r;r.machineBinding=machine;if(withAttempt)r.active=nativeAttempt();return r;}
std::string encoded(const nw::Record& r){std::string value;if(!nw::encode(r,value))throw std::runtime_error("native encoding failed");return value;}
std::string snapshot(const nw::Session& s){if(!s.record())throw std::runtime_error("native session not ready");return encoded(*s.record());}
std::uint64_t rev(const nw::Session& s){return s.record()?s.record()->revision:0;}
void nativeRejected(const std::string& input){auto out=nativeRecord();const auto before=encoded(out);std::string error;
    check(!nw::decode(input,out,&error),"strict native decode rejects input");check(!error.empty(),"decode supplies reason");
    check(encoded(out)==before,"failed native decode preserves output");}
void nativeProbe(const std::wstring& file,const std::wstring& report,bool expectedReady){
    check(ownedChild({L"--native-probe",file,report,expectedReady?L"ready":L"blocked"})==0,"native independent owner probe passes");
    childReports.push_back(J::parse(bytes(report)));
}
nw::Visibility nativeVisibility(){
    nw::Visibility v;v.transactionId="native-visibility-119";v.parent={11901,"134368044937265940"};
    v.physicalRoot="USB\\VID_045E&PID_028E\\private-root";
    v.locationPaths={"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(1)","ACPI(_SB_)#ACPI(XHC_)#USB(1)"};
    v.beforeCloaking=true;v.paths={{"HID\\VID_045E&PID_028E&IG_00\\private-node",nw::VisibilityPathPhase::Reserved}};return v;
}
nw::Record visibilityRecord(){auto r=nativeRecord(false);r.schemaVersion=nw::kVisibilitySchemaVersion;r.nativeVisibility=nativeVisibility();return r;}
void nativeCases(const std::filesystem::path& root){
    const auto file=[&](const std::wstring& name){return (root/(L"native-"+name)).wstring();};
    run("native/visibility/schema1-unchanged-schema2-roundtrip",[]{
        auto legacy=nativeRecord();const auto wire=J::parse(encoded(legacy));
        check(legacy.schemaVersion==1&&nw::kSchemaVersion==1&&nw::kVisibilitySchemaVersion==2&&wire.size()==5&&!wire.contains("nativeVisibility"),"schema1 original envelope unchanged");
        for(const bool active:{false,true})for(const bool cloak:{false,true}){
            auto r=visibilityRecord();if(active)r.active=nativeAttempt();r.nativeVisibility->beforeCloaking=cloak;
            nw::Record out;check(nw::decode(encoded(r),out)&&encoded(r)==encoded(out),"schema2 all native/writer facts roundtrip");
            r.nativeVisibility->paths[0].phase=nw::VisibilityPathPhase::MayHaveIssued;
            check(nw::decode(encoded(r),out)&&encoded(r)==encoded(out),"MayHaveIssued wire roundtrip");
            r.nativeVisibility->paths.clear();check(nw::decode(encoded(r),out),"empty paths retained until explicit complete");
        }
        auto r=visibilityRecord();r.nativeVisibility.reset();nw::Record out;
        check(nw::decode(encoded(r),out)&&out.schemaVersion==2&&!out.nativeVisibility,"schema2 null stays explicit schema2");
        r=visibilityRecord();r.schemaVersion=1;std::string unchanged="sentinel";
        check(!nw::encode(r,unchanged)&&unchanged=="sentinel","schema1 cannot silently encode or upgrade typed visibility");
    });
    run("native/visibility/strict-shape-types-duplicates-and-byte-limit",[]{
        const auto base=J::parse(encoded(visibilityRecord()));
        for(const auto* key:{"transactionId","parent","profile","physicalRoot","locationPaths","beforeCloaking","paths"}){
            auto j=base;j["nativeVisibility"].erase(key);nativeRejected(j.dump());
        }
        for(const auto* key:{"instanceId","phase"}){auto j=base;j["nativeVisibility"]["paths"][0].erase(key);nativeRejected(j.dump());}
        for(const auto& key:std::vector<std::string>{"host","origin","command","proof"}){
            auto j=base;j["nativeVisibility"][key]="forbidden";nativeRejected(j.dump());
        }
        {auto j=base;j["nativeVisibility"]["parent"]["host"]=true;nativeRejected(j.dump());}
        {auto j=base;j["nativeVisibility"]["paths"][0]["proof"]=true;nativeRejected(j.dump());}
        for(const auto& bad:std::vector<J>{nullptr,0,1,"true",J::array()}){auto j=base;j["nativeVisibility"]["beforeCloaking"]=bad;nativeRejected(j.dump());}
        for(const auto& bad:std::vector<J>{nullptr,0,1.0,"Reserved","may-run","MayHaveIssued"}){auto j=base;j["nativeVisibility"]["paths"][0]["phase"]=bad;nativeRejected(j.dump());}
        for(const auto& bad:std::vector<J>{0,1.0,-1,"11901",4294967296ull}){auto j=base;j["nativeVisibility"]["parent"]["pid"]=bad;nativeRejected(j.dump());}
        for(const auto& bad:std::vector<J>{0,"0","01","18446744073709551616"}){auto j=base;j["nativeVisibility"]["parent"]["creationTime"]=bad;nativeRejected(j.dump());}
        {auto j=base;j["schemaVersion"]=1;nativeRejected(j.dump());j=base;j.erase("nativeVisibility");nativeRejected(j.dump());}
        for(const auto& duplicate:std::vector<std::pair<std::string,std::string>>{
            {"\"nativeVisibility\":{","\"transactionId\":\"duplicate\","},
            {"\"parent\":{","\"pid\":11901,"},
            {"\"paths\":[{","\"instanceId\":\"duplicate\","},
            {"\"paths\":[{","\"ph\\u0061se\":\"reserved\","}}){
            auto bytes=base.dump();const auto pos=bytes.find(duplicate.first);need(pos!=std::string::npos,"duplicate insertion anchor");
            bytes.insert(pos+duplicate.first.size(),duplicate.second);nativeRejected(bytes);
        }
        auto exact=base.dump();exact.append(nw::maxRecordBytes-exact.size(),' ');nw::Record out;
        check(nw::decode(exact,out),"schema2 exact 64KiB accepted");nativeRejected(exact+" ");
        auto deep=base;deep["nativeVisibility"]["paths"][0]["phase"]=J::array({J::array({J::array({J::array({J::array({J::array({0})})})})})});nativeRejected(deep.dump());
    });
    run("native/visibility/strict-identity-gpd-and-bounds",[]{
        auto r=visibilityRecord();
        for(const auto& bad:std::vector<std::string>{"","HID\\VID_045E&PID_028E\\x","USB\\VID_045E&PID_028E&MI_00\\x","USB\\VID_045E&PID_028F\\x","USB\\VID_045E&PID_028E\\VIGEM","USB\\VID_045E&PID_028E\\HIDMAESTRO","USB\\VID_045E&PID_028E\\VIRTUAL","USB\\VID_045E&PID_028E\\x\\y"}){
            r=visibilityRecord();r.nativeVisibility->physicalRoot=bad;check(!nw::validRecord(r),"invalid or virtual GPD root rejected");
        }
        for(const auto& bad:std::vector<std::string>{"","ROOT\\VID_045E&PID_028E\\x","SWD\\VID_045E&PID_028E\\x","BTH\\VID_045E&PID_028E\\x","USB\\VID_045E&PID_028EX\\x","HID\\VID_045E&PID_028E&\\x","HID\\VID_045E&PID_028E&VID_0000\\x","HID\\VID_045E&PID_028F\\x","HID\\VID_045E&PID_028E\\VIGEM","HID\\VID_045E&PID_028E\\VIRTUAL","HID\\VID_045E&PID_028E\\bad\n","HID\\VID_045E&PID_028E\\wild*"}){
            r=visibilityRecord();r.nativeVisibility->paths[0].instanceId=bad;check(!nw::validRecord(r),"invalid/foreign/virtual/control path rejected");
        }
        for(unsigned n=0;n<16;++n){r=visibilityRecord();r.nativeVisibility->paths.clear();r.nativeVisibility->locationPaths.clear();
            for(unsigned k=0;k<=n;++k){r.nativeVisibility->paths.push_back({"USB\\VID_045E&PID_028E\\node-"+std::to_string(k),nw::VisibilityPathPhase::Reserved});
                r.nativeVisibility->locationPaths.push_back(std::string(1020,'x')+std::to_string(k));}
            check(nw::validRecord(r),"up to 16 exact paths and unique locations accepted");}
        r.nativeVisibility->paths.push_back({"USB\\VID_045E&PID_028E\\node-16"});check(!nw::validRecord(r),"17 paths rejected");
        r=visibilityRecord();r.nativeVisibility->locationPaths=std::vector<std::string>(17,"x");check(!nw::validRecord(r),"17 locations rejected");
        r=visibilityRecord();r.nativeVisibility->paths.push_back(r.nativeVisibility->paths[0]);check(!nw::validRecord(r),"duplicate paths rejected");
        r.nativeVisibility->paths.back().instanceId=ymcc::targetReturn::detail::fold(r.nativeVisibility->paths[0].instanceId);check(!nw::validRecord(r),"casefold duplicate paths rejected");
        r=visibilityRecord();r.nativeVisibility->locationPaths={"Location","LOCATION"};check(!nw::validRecord(r),"casefold duplicate locations rejected");
        for(const auto& bad:std::vector<std::string>{"",std::string(1025,'x'),"bad\n",std::string("\xc2\x85",2),std::string("\xc0\xaf",2)}){
            r=visibilityRecord();r.nativeVisibility->locationPaths={bad};check(!nw::validRecord(r),"empty/control/invalid UTF8/overlong visibility location rejected");}
        r=visibilityRecord();r.nativeVisibility->profile="rog";check(!nw::validRecord(r),"visibility is GPD only");
        r=visibilityRecord();r.nativeVisibility->parent.pid=0;check(!nw::validRecord(r),"native parent identity required");
        r=visibilityRecord();r.nativeVisibility->paths[0].phase=static_cast<nw::VisibilityPathPhase>(99);check(!nw::validRecord(r),"unknown typed phase rejected");
        r=visibilityRecord();r.nativeVisibility->transactionId=std::string(101,'a');check(!nw::validRecord(r),"transaction length bounded");
    });
    run("native/visibility/pure-transitions-exact-owner-facts-and-writer-preservation",[]{
        const auto v=nativeVisibility();const auto tx=v.transactionId,path=v.paths[0].instanceId,late=std::string("USB\\VID_045E&PID_028E\\late-node");
        nw::Record r,out; r.machineBinding=machine;need(nw::reserveVisibility(r,1,v,out),"explicit reserve upgrades to schema2");r=out;
        check(r.schemaVersion==2&&r.revision==2&&r.nativeVisibility->paths[0].phase==nw::VisibilityPathPhase::Reserved,"reserved durable candidate has no issue authority");
        const auto facts=J::parse(encoded(r))["nativeVisibility"];
        need(nw::reserve(r,2,nativeAttempt(),out),"writer reserve with visibility");r=out;
        need(nw::markChild(r,3,"native-attempt-119",child(),out),"writer child with visibility");r=out;
        check(J::parse(encoded(r))["nativeVisibility"]==facts,"writer reserve/mark preserve exact native facts");
        check(!nw::completeVisibility(r,4,tx,out)&&!nw::transferVisibility(r,4,tx,out),"active writer blocks complete and transfer");
        need(nw::settle(r,4,"native-attempt-119",out),"writer settle with visibility");r=out;
        check(J::parse(encoded(r))["nativeVisibility"]==facts,"writer settle preserves visibility");
        const auto before=encoded(r);out=nativeRecord();const auto sentinel=encoded(out);
        auto changed=v;changed.parent.creationTime="3";check(!nw::reserveVisibility(r,5,changed,out),"native identity cannot be replaced in place");
        check(!nw::reserveVisibilityPath(r,4,tx,late,out)&&!nw::reserveVisibilityPath(r,5,"other",late,out),"late path requires exact revision and transaction");
        check(!nw::reserveVisibilityPath(r,5,tx,path,out)&&!nw::reserveVisibilityPath(r,5,tx,ymcc::targetReturn::detail::fold(path),out),"duplicate late path rejected without replacing phase");
        check(!nw::markVisibilityMayHaveIssued(r,4,tx,path,out)&&!nw::markVisibilityMayHaveIssued(r,5,"other",path,out)&&!nw::markVisibilityMayHaveIssued(r,5,tx,late,out),"mark exact revision tx path");
        check(!nw::settleVisibilityPath(r,4,tx,path,out)&&!nw::settleVisibilityPath(r,5,"other",path,out)&&!nw::settleVisibilityPath(r,5,tx,late,out),"settle exact revision tx path");
        check(!nw::completeVisibility(r,4,tx,out)&&!nw::completeVisibility(r,5,"other",out)&&!nw::completeVisibility(r,5,tx,out),"complete requires exact revision tx and no paths");
        check(!nw::transferVisibility(r,4,tx,out)&&!nw::transferVisibility(r,5,"other",out),"transfer requires exact revision tx");
        check(encoded(out)==sentinel&&encoded(r)==before,"all rejections preserve both inputs and output");
        need(nw::reserveVisibilityPath(r,5,tx,late,out),"late same-source path is Reserved");r=out;
        check(r.revision==6&&r.nativeVisibility->paths.back().phase==nw::VisibilityPathPhase::Reserved,"late path reservation phase exact");
        need(nw::markVisibilityMayHaveIssued(r,6,tx,path,out),"mark before command may issue");r=out;
        check(nw::markVisibilityMayHaveIssued(r,7,tx,path,out)&&encoded(out)==encoded(r),"exact mark replay no bump");
        check(!nw::markVisibilityMayHaveIssued(r,6,tx,path,out),"mark replay still rejects stale revision");
        need(nw::settleVisibilityPath(r,7,tx,path,out),"caller-proved exact per-path return");r=out;
        need(nw::settleVisibilityPath(r,8,tx,late,out),"reserved can be retired after proof without granting unhide");r=out;
        check(r.nativeVisibility&&r.nativeVisibility->paths.empty(),"empty responsibility retained until explicit complete");
        auto active=r;active.active=nativeAttempt();check(!nw::completeVisibility(active,9,tx,out)&&!nw::transferVisibility(active,9,tx,out),"even empty visibility cannot clear active writer");
        need(nw::completeVisibility(r,9,tx,out),"complete exact empty visibility");
        check(out.revision==10&&out.schemaVersion==2&&!out.nativeVisibility&&!out.active,"complete clears visibility only and never downgrades schema");
        r=visibilityRecord();need(nw::transferVisibility(r,1,tx,out),"caller-proved Hosted transfer clears nonempty visibility");
        check(out.revision==2&&!out.nativeVisibility&&out.schemaVersion==2,"atomic transfer persists only removal");
        r=nativeRecord(false);changed=v;changed.paths[0].phase=nw::VisibilityPathPhase::MayHaveIssued;
        check(!nw::reserveVisibility(r,1,changed,out),"initial reservation cannot assert issued");changed=v;changed.paths.clear();check(!nw::reserveVisibility(r,1,changed,out),"initial reserve must contain paths");
    });
    run("native/visibility/revision-overflow-preserves",[]{
        auto r=visibilityRecord();r.revision=(std::numeric_limits<std::uint64_t>::max)();const auto& v=*r.nativeVisibility;
        nw::Record out=nativeRecord();const auto before=encoded(out);const auto rev=r.revision;const auto tx=v.transactionId,path=v.paths[0].instanceId;
        check(!nw::reserveVisibilityPath(r,rev,tx,"USB\\VID_045E&PID_028E\\late",out)&&!nw::markVisibilityMayHaveIssued(r,rev,tx,path,out)&&
            !nw::settleVisibilityPath(r,rev,tx,path,out)&&!nw::transferVisibility(r,rev,tx,out),"overflow blocks all changing path/transfer transitions");
        r.nativeVisibility->paths.clear();check(!nw::completeVisibility(r,rev,tx,out),"overflow blocks complete");
        r.nativeVisibility.reset();check(!nw::reserveVisibility(r,rev,nativeVisibility(),out),"overflow blocks initial reserve");
        check(encoded(out)==before,"overflow preserves output");
        r=visibilityRecord();r.revision=rev;r.nativeVisibility->paths[0].phase=nw::VisibilityPathPhase::MayHaveIssued;
        check(nw::markVisibilityMayHaveIssued(r,rev,tx,path,out)&&out.revision==rev,"exact phase replay succeeds at max revision without write");
    });
    run("native/visibility/session-shared-owner-commit-order-reopen-and-noops",[&]{
        const auto p=file(L"visibility-shared.json");const auto v=nativeVisibility();const auto tx=v.transactionId,path=v.paths[0].instanceId;
        {auto s=nw::Session::open(p,machine);need(s.ready(),"visibility shares existing owner");io::reset();need(s.reserveVisibility(1,v),"commit visibility reservation");
            check(io::order==std::vector<std::string>({"create","write","flush","close","move"}),"same atomic commit path");
            nativeProbe(p,(root/L"visibility-owner-blocked.json").wstring(),false);
            const auto facts=J::parse(snapshot(s))["nativeVisibility"];
            need(s.reserve(2,nativeAttempt())&&s.markChild(3,"native-attempt-119",child()),"one owner serializes native visibility and writer");
            check(J::parse(snapshot(s))["nativeVisibility"]==facts,"Session helper history preserves visibility");
            check(!s.transferVisibility(4,tx)&&!s.completeVisibility(4,tx),"session active writer blocks both removals");
            need(s.settle(4,"native-attempt-119")&&s.markVisibilityMayHaveIssued(5,tx,path),"same owner settles writer then records may issue");
            const auto before=bytes(p);io::reset(io::Fault::Write);
            check(s.markVisibilityMayHaveIssued(6,tx,path)&&!s.markVisibilityMayHaveIssued(5,tx,path)&&!s.settleVisibilityPath(6,"other",path),"exact no-op and rejections");
            check(io::writes==0&&io::moves==0&&snapshot(s)==before&&bytes(p)==before,"no-op and rejections do not write");io::reset();}
        {auto s=nw::Session::open(p,machine);need(s.ready()&&s.openedFrom()==nw::LoadState::Valid&&rev(s)==6,"schema2 reopens under same owner");
            check(s.record()->nativeVisibility->paths[0].phase==nw::VisibilityPathPhase::MayHaveIssued,"MayHaveIssued remains on disk across owner close");
            need(s.settleVisibilityPath(6,tx,path)&&s.completeVisibility(7,tx),"caller-directed settle and complete persist");}
        const auto loaded=nw::load(p,machine);check(loaded.state==nw::LoadState::Valid&&loaded.record->schemaVersion==2&&!loaded.record->nativeVisibility,"complete remains schema2 null on disk");
    });
    for(const auto fault:{io::Fault::TempCreate,io::Fault::Write,io::Fault::ShortWrite,io::Fault::Flush,io::Fault::Close,io::Fault::Move,io::Fault::TempType}){
        run("native/visibility/store/fault-preserves/"+std::to_string(int(fault)),[&]{
            for(unsigned operation=0;operation<6;++operation){
                const auto p=file(L"visibility-fault-"+std::to_wstring(int(fault))+L"-"+std::to_wstring(operation)+L".json");
                const auto v=nativeVisibility();const auto tx=v.transactionId,path=v.paths[0].instanceId;auto s=nw::Session::open(p,machine);need(s.ready(),"private fault owner ready");
                if(operation)need(s.reserveVisibility(1,v),"seed durable visibility");
                if(operation==3)need(s.markVisibilityMayHaveIssued(2,tx,path),"seed issued responsibility");
                if(operation==4)need(s.settleVisibilityPath(2,tx,path),"seed caller-proved empty responsibility");
                const auto priorRevision=rev(s);const auto memory=snapshot(s);const bool exists=std::filesystem::exists(p);const auto disk=exists?bytes(p):std::string();
                const auto apply=[&]{switch(operation){case 0:return s.reserveVisibility(priorRevision,v);
                    case 1:return s.reserveVisibilityPath(priorRevision,tx,"USB\\VID_045E&PID_028E\\late");
                    case 2:return s.markVisibilityMayHaveIssued(priorRevision,tx,path);
                    case 3:return s.settleVisibilityPath(priorRevision,tx,path);
                    case 4:return s.completeVisibility(priorRevision,tx);
                    default:return s.transferVisibility(priorRevision,tx);}};
                io::reset(fault);check(!apply(),"all six visibility commit operations reject injected IO fault");
                check(snapshot(s)==memory&&rev(s)==priorRevision&&std::filesystem::exists(p)==exists&&(!exists||bytes(p)==disk),"failed operation retains exact disk memory revision and responsibility");
                check(s.error().code==nw::SessionErrorCode::RetryableIo,"fault remains explicit retryable IO");io::reset();
                check(apply()&&rev(s)==priorRevision+1&&snapshot(s)==bytes(p),"caller explicit retry commits once");
            }
        });
    }
    run("native/record/typed-childless-and-child-roundtrip",[]{
        check(std::string(nw::jobPrefix)=="Local\\YMCC.Native.Refresh.","dedicated exact native job prefix");
        check(nativeRecord(false).writerFence=="inherited-exclusive-owner-file-v1","default native envelope declares exact writer fence");
        for(const bool active:{false,true}){auto r=nativeRecord(active);nw::Record out;
            check(nw::decode(encoded(r),out)&&encoded(out)==encoded(r),"typed empty/childless roundtrip preserves all facts");
            if(active){r.active->child=child();check(nw::decode(encoded(r),out)&&encoded(out)==encoded(r),"actual child roundtrip preserves all facts");}}
    });
    run("native/record/limits-and-exact-instances",[]{
        auto r=nativeRecord();r.active->attemptId=std::string(100,'a');r.active->jobName=std::string(nw::jobPrefix)+r.active->attemptId;
        r.active->parent={0xffffffffu,"18446744073709551615"};r.active->locationPaths=std::vector<std::string>(16,std::string(1024,'x'));
        check(nw::validRecord(r),"100-byte token/max process/16 paths of 1024 valid");nw::Record out;
        check(nw::decode(encoded(r),out)&&encoded(r)==encoded(out),"boundary bytes retained exactly");
        for(const auto& bad:std::vector<std::string>{"",".","..","a b","a/b","a\\b",std::string(101,'a')}){
            auto v=nativeRecord();v.active->attemptId=bad;v.active->jobName=std::string(nw::jobPrefix)+bad;check(!nw::validRecord(v),"invalid token rejected");}
        for(const auto& bad:std::vector<std::string>{"","HID","HID\\one","HID\\one\\two\\three","HID\\..\\two","HID\\one\\*","HID\\one\\two?","HID/one/two",std::string(513,'x')}){
            auto v=nativeRecord();v.active->physicalRoot=bad;check(!nw::validRecord(v),"physical root requires exact instance syntax");
            v=nativeRecord();v.active->deviceInstanceId=bad;check(!nw::validRecord(v),"device requires exact instance syntax");}
        r=nativeRecord();r.active->locationPaths.clear();check(!nw::validRecord(r),"zero locations rejected");
        r.active->locationPaths=std::vector<std::string>(17,"x");check(!nw::validRecord(r),"17 locations rejected");
        r.active->locationPaths={std::string(1025,'x')};check(!nw::validRecord(r),"1025 bytes rejected");
        for(const auto& bad:std::vector<std::string>{"","x\n",std::string("x\0y",3),std::string("\x7f",1),std::string("\xc2\x85",2),std::string("\xc0\xaf",2)}){
            r=nativeRecord();r.active->locationPaths={bad};check(!nw::validRecord(r),"empty/control/malformed UTF8 location rejected");
            r=nativeRecord();r.machineBinding=bad;check(!nw::validRecord(r),"empty/control/malformed UTF8 machine rejected");}
        r=nativeRecord();r.active->locationPaths={"location-\xe6\xb5\x8b\xe8\xaf\x95"};check(nw::validRecord(r),"well-formed UTF8 location retained");
    });
    const auto base=J::parse(encoded(nativeRecord()));
    run("native/decode/strict-shape-types-and-foreign-schemas",[&]{
        for(const auto* k:{"schemaVersion","writerFence","machineBinding","revision","active"}){auto j=base;j.erase(k);nativeRejected(j.dump());}
        for(const auto& v:std::vector<J>{nullptr,true,1,"","unknown","inherited-exclusive-owner-file-v2"}){auto j=base;j["writerFence"]=v;nativeRejected(j.dump());}
        {auto j=base;j.erase("writerFence");j["writerFencE"]=nw::kWriterFence;nativeRejected(j.dump());}
        for(const auto* k:{"attemptId","parent","child","jobName","physicalRoot","deviceInstanceId","locationPaths"}){auto j=base;j["active"].erase(k);nativeRejected(j.dump());}
        for(const auto* k:{"stage","host","origin","cleanupAllowed","targetMayExist","command"}){auto j=base;j["active"][k]=true;nativeRejected(j.dump());}
        for(const auto& v:std::vector<J>{nullptr,true,"1",-1,0,1.5,2}){auto j=base;j["schemaVersion"]=v;nativeRejected(j.dump());}
        for(const auto& v:std::vector<J>{nullptr,true,"1",-1,0,1.5}){auto j=base;j["revision"]=v;nativeRejected(j.dump());}
        for(const auto* k:{"attemptId","jobName","physicalRoot","deviceInstanceId"}){auto j=base;j["active"][k]=7;nativeRejected(j.dump());}
        for(const auto& v:std::vector<J>{nullptr,true,"path",J::array(),J::array({42}),J::array({std::string(1025,'x')})}){auto j=base;j["active"]["locationPaths"]=v;nativeRejected(j.dump());}
        for(const auto* role:{"parent","child"}){
            for(const auto& v:std::vector<J>{nullptr,true,"1",-1,0,1.5,4294967296ULL}){auto j=base;j["active"][role]={{"pid",v},{"creationTime","1"}};nativeRejected(j.dump());}
            for(const auto& v:std::vector<J>{nullptr,1,"","0","01","-1","1.0","18446744073709551616"}){auto j=base;j["active"][role]={{"pid",1},{"creationTime",v}};nativeRejected(j.dump());}
            auto j=base;j["active"][role]={{"pid",1},{"creationTime","2"},{"extra",true}};nativeRejected(j.dump());}
        for(const auto& wrong:std::vector<std::string>{"local\\YMCC.Native.Refresh.native-attempt-119","Global\\YMCC.Native.Refresh.native-attempt-119",std::string(nw::jobPrefix)+"other",std::string(cw::jobPrefix)+"native-attempt-119"}){auto j=base;j["active"]["jobName"]=wrong;nativeRejected(j.dump());}
        nativeRejected(encoded(reserved()));
        ymcc::targetReturn::Record host;host.machineBinding=machine;std::string hostBytes;
        need(ymcc::targetReturn::encode(host,hostBytes),"encode true Host schema");nativeRejected(hostBytes);
        cw::Record cli;check(!cw::decode(base.dump(),cli),"native active cannot decode as CLI");
        ymcc::targetReturn::Record target;check(!ymcc::targetReturn::decode(base.dump(),target),"native cannot decode as Host");
    });
    run("native/decode/duplicate-root",[&]{auto s=base.dump();s.insert(1,"\"revision\":1,");nativeRejected(s);});
    run("native/decode/duplicates-nested-escaped-and-bounds",[&]{
        auto s=base.dump();s.insert(s.find("\"parent\":{")+10,"\"pid\":11901,");nativeRejected(s);
        s=base.dump();s.insert(1,"\"revis\\u0069on\":1,");nativeRejected(s);
        s=base.dump();s.insert(s.find("\"physicalRoot\":"),"\"physicalRoot\":\"USB\\\\a\\\\b\",");nativeRejected(s);
        for(const auto& bad:std::vector<std::string>{"","null","[]",base.dump()+"{}",std::string("\xef\xbb\xbf")+base.dump(),base.dump()+std::string(1,'\0'),std::string(nw::maxRecordBytes+1,' '),"{\"schemaVersion\":1,/*comment*/}",std::string(40,'[')+"0"+std::string(40,']')})nativeRejected(bad);
        auto padded=base.dump();padded.append(nw::maxRecordBytes-padded.size(),' ');nw::Record boundary;
        check(nw::decode(padded,boundary),"exact 64KiB accepted");nativeRejected(padded+" ");
        auto invalid=nativeRecord();invalid.active->deviceInstanceId="wild*";std::string output="unchanged";
        check(!nw::encode(invalid,output)&&output=="unchanged","invalid typed encode preserves output");
        invalid=nativeRecord();invalid.writerFence="unknown";check(!nw::validRecord(invalid)&&!nw::encode(invalid,output)&&output=="unchanged","typed fence mismatch rejected before publication");
        auto j=base;j["active"]["locationPaths"]=J::array({"bad\ncontrol"});nativeRejected(j.dump());
        j=base;j["active"]["locationPaths"]=J::array({std::string("\xc2\x85",2)});nativeRejected(j.dump());
    });
    run("native/transform/revisions-immutable-wholefacts-and-child",[]{
        const auto initial=nativeRecord(false);nw::Record next;
        need(nw::reserve(initial,1,nativeAttempt(),next),"reserve typed childless");const auto reservedBytes=encoded(next);
        auto withChild=nativeAttempt();withChild.child=child();nw::Record sentinel=nativeRecord();const auto untouched=encoded(sentinel);
        check(!nw::reserve(initial,1,withChild,sentinel)&&encoded(sentinel)==untouched,"reserve requires childless and preserves output");
        check(!nw::reserve(initial,2,nativeAttempt(),sentinel),"stale reserve rejected");
        for(unsigned n=0;n<6;++n){auto rewritten=nativeAttempt();switch(n){case 0:rewritten.parent.pid++;break;case 1:rewritten.physicalRoot+="x";break;case 2:rewritten.deviceInstanceId+="x";break;case 3:rewritten.locationPaths[0]+="x";break;case 4:std::reverse(rewritten.locationPaths.begin(),rewritten.locationPaths.end());break;default:rewritten=nativeAttempt("replacement");}
            check(!nw::reserve(next,2,rewritten,sentinel)&&encoded(next)==reservedBytes,"active wholefacts cannot be replaced");}
        check(!nw::markChild(next,1,"native-attempt-119",child(),sentinel)&&!nw::markChild(next,2,"other",child(),sentinel)&&!nw::markChild(next,2,"native-attempt-119",{0,"1"},sentinel),"stale/wrong attempt/invalid child rejected");
        nw::Record marked;need(nw::markChild(next,2,"native-attempt-119",child(),marked),"mark actual child");
        auto expected=J::parse(reservedBytes);expected["revision"]=3;expected["active"]["child"]={{"pid",child().pid},{"creationTime",child().creationTime}};
        check(J::parse(encoded(marked))==expected,"markChild changes only revision and child; all wholefacts exact");
        nw::Record replay;check(nw::markChild(marked,3,"native-attempt-119",child(),replay)&&encoded(replay)==encoded(marked),"exact child idempotent at current revision");
        auto changed=child();changed.creationTime="3";check(!nw::markChild(marked,3,"native-attempt-119",changed,sentinel),"same PID different birth cannot replace child");
        changed=child();++changed.pid;check(!nw::markChild(marked,3,"native-attempt-119",changed,sentinel),"different PID cannot replace child");
        check(!nw::markChild(marked,2,"native-attempt-119",child(),sentinel),"idempotence still requires current revision");
        check(!nw::settle(marked,2,"native-attempt-119",sentinel)&&!nw::settle(marked,3,"other",sentinel),"settle requires exact revision and attempt");
        nw::Record settled;check(nw::settle(next,2,"native-attempt-119",settled)&&!settled.active&&settled.revision==3,"pure settle transition accepts childless; fixture obtains no Job proof");
        auto overflow=next;overflow.revision=(std::numeric_limits<std::uint64_t>::max)();
        check(!nw::markChild(overflow,overflow.revision,"native-attempt-119",child(),sentinel)&&!nw::settle(overflow,overflow.revision,"native-attempt-119",sentinel),"overflow rejects changed record");
        overflow=nativeRecord(false);overflow.revision=(std::numeric_limits<std::uint64_t>::max)();check(!nw::reserve(overflow,overflow.revision,nativeAttempt(),sentinel),"reserve overflow rejected");
        check(encoded(sentinel)==untouched,"all rejected pure transforms preserve output");
    });
    run("native/session/commit-order-childless-reopen-and-settle",[&]{
        const auto p=file(L"committed.json");check(nw::load(p,machine).state==nw::LoadState::Missing,"load absent record is Missing");
        {auto s=nw::Session::open(p,machine);need(s.ready()&&s.openedFrom()==nw::LoadState::Missing,"missing opens one current owner");
            check(rev(s)==1&&!s.record()->active&&!std::filesystem::exists(p),"open does not publish empty record");io::reset();
            need(s.reserve(1,nativeAttempt()),"persistent childless reservation precedes any helper creation");
            check(io::order==std::vector<std::string>({"create","write","flush","close","move"}),"full Write/Flush/close/atomic replace order");
            check(snapshot(s)==bytes(p)&&!s.record()->active->child&&rev(s)==2,"committed childless facts published");}
        {auto s=nw::Session::open(p,machine);need(s.ready()&&s.openedFrom()==nw::LoadState::Valid,"reopen valid reservation");
            check(!s.record()->active->child&&!s.reserve(2,nativeAttempt("new")),"unknown child still blocks a second launch");
            need(s.markChild(2,"native-attempt-119",child()),"optional actual child registered");}
        {auto s=nw::Session::open(p,machine);need(s.ready()&&rev(s)==3&&s.record()->active->child.has_value(),"reopen actual child");
            need(s.settle(3,"native-attempt-119"),"pure settle persistence; external Job proof is not exercised");check(rev(s)==4&&!s.record()->active,"settle persists empty");}
        auto s=nw::Session::open(p,machine);check(s.ready()&&s.openedFrom()==nw::LoadState::Valid&&rev(s)==4&&!s.record()->active,"empty persisted record is Valid, not Missing");
    });
    run("native/session/noops-and-rejections-never-write",[&]{
        const auto p=file(L"noop.json");auto s=nw::Session::open(p,machine);need(s.ready()&&s.reserve(1,nativeAttempt())&&s.markChild(2,"native-attempt-119",child()),"seed actual child");
        const auto before=bytes(p);io::reset(io::Fault::Write);check(s.markChild(3,"native-attempt-119",child()),"idempotent child succeeds without IO");
        check(!s.markChild(2,"native-attempt-119",child())&&!s.reserve(3,nativeAttempt())&&!s.settle(3,"other"),"stale/active/wrong requests rejected");
        check(io::writes==0&&io::moves==0&&io::flushes==0&&snapshot(s)==before&&bytes(p)==before,"no write and no mutation");io::reset();
    });
    for(const auto fault:{io::Fault::TempCreate,io::Fault::Write,io::Fault::ShortWrite,io::Fault::Flush,io::Fault::Close,io::Fault::Move,io::Fault::TempType}){
        run(fault==io::Fault::Flush?"native/store/flush-preserves":"native/store/fault-preserves/"+std::to_string(int(fault)),[&]{
            const auto p=file(L"fault-"+std::to_wstring(int(fault))+L".json");auto s=nw::Session::open(p,machine);
            need(s.ready()&&s.reserve(1,nativeAttempt()),"seed committed childless history");const auto before=bytes(p);
            io::reset(fault);check(!s.markChild(2,"native-attempt-119",child()),"failed commit rejected");
            check(snapshot(s)==before&&bytes(p)==before&&rev(s)==2,"commit failure preserves memory, disk and revision");
            check(s.error().code==nw::SessionErrorCode::RetryableIo,"explicit retryable IO error");io::reset();
            check(s.markChild(2,"native-attempt-119",child()),"caller can explicitly retry after IO failure");
            for(const auto& entry:std::filesystem::directory_iterator(root))check(entry.path().filename().wstring().find(L".native-refresh-writer.tmp.")==std::wstring::npos,"failed temp files removed");
        });
    }
    run("native/store/reserve-and-settle-failure-preserve",[&]{
        const auto p=file(L"reserve-failure.json");auto s=nw::Session::open(p,machine);need(s.ready(),"fresh owner");io::reset(io::Fault::Flush);
        check(!s.reserve(1,nativeAttempt())&&rev(s)==1&&!s.record()->active&&!std::filesystem::exists(p),"failed first reserve publishes no attempt");
        io::reset();const bool committed=s.reserve(1,nativeAttempt());check(committed,"commit reserve");
        if(!committed)return; // This is a tested retry failure, not a setup exception.
        const auto before=bytes(p);io::reset(io::Fault::Move);
        check(!s.settle(2,"native-attempt-119")&&snapshot(s)==before&&bytes(p)==before,"failed settle cannot clear obligation");io::reset();
    });
    run("native/store/real-replacement-lock-preserves",[&]{
        const auto p=file(L"replacement-lock.json");auto s=nw::Session::open(p,machine);need(s.ready()&&s.reserve(1,nativeAttempt()),"seed real destination");const auto before=bytes(p);
        HANDLE reader=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(reader!=INVALID_HANDLE_VALUE,"private reader blocks replacement");
        check(!s.markChild(2,"native-attempt-119",child())&&snapshot(s)==before&&bytes(p)==before,"real sharing conflict preserves committed state");CloseHandle(reader);
    });
    run("native/session/invalid-and-IO-never-empty",[&]{
        unsigned n=0;auto foreign=nativeRecord();foreign.machineBinding="other-machine";
        auto unfenced=J::parse(encoded(nativeRecord()));unfenced.erase("writerFence");
        auto wrongFence=J::parse(encoded(nativeRecord()));wrongFence["writerFence"]="unknown";
        for(const auto& bad:std::vector<std::string>{"","{}","broken",encoded(foreign),encoded(reserved()),unfenced.dump(),wrongFence.dump(),std::string(nw::maxRecordBytes+1,' ')}){
            const auto p=file(L"invalid-"+std::to_wstring(++n)+L".json");raw(p,bad);auto s=nw::Session::open(p,machine);
            check(!s.ready()&&!s.record()&&s.error().code==nw::SessionErrorCode::Invalid&&s.openedFrom()==nw::LoadState::Invalid,"invalid/foreign is never empty");
            check(!s.reserve(1,nativeAttempt())&&bytes(p)==bad,"failed session cannot publish");}
        for(const auto fault:{io::Fault::Read,io::Fault::ShortRead,io::Fault::RecordAttributes}){
            const auto p=file(L"read-"+std::to_wstring(int(fault))+L".json");raw(p,encoded(nativeRecord()));io::reset(fault);
            {auto s=nw::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==nw::SessionErrorCode::RetryableIo&&s.openedFrom()==nw::LoadState::RetryableIo,"read failure distinct from Missing/Invalid");}io::reset();}
    });
    run("native/session/historical-identities-block-without-probes",[&]{
        const auto p=file(L"historical.json");auto r=nativeRecord();r.active->parent={0xfffffffeu,"1"};raw(p,encoded(r));
        auto s=nw::Session::open(p,machine);check(s.ready()&&snapshot(s)==encoded(r),"childless historical parent imported without liveness judgement");
        check(!s.reserve(1,nativeAttempt("other")),"historical active never auto-settled");
    });
    run("native/path/unsafe-path-and-owner-content",[&]{
        for(const auto& p:std::vector<std::wstring>{L"relative.json",L"C:relative.json",L"\\\\server\\share\\r.json",(root/L"..\\escape.json").wstring(),file(L"ads:stream"),(root/L"NUL.json").wstring(),file(L"trailing. ")}){auto s=nw::Session::open(p,machine);check(!s.ready()&&s.error().code==nw::SessionErrorCode::Invalid,"unsafe explicit path rejected");}
        auto missing=nw::Session::open(file(L"missing\\record.json"),machine);check(!missing.ready()&&missing.error().code==nw::SessionErrorCode::RetryableIo,"missing parent is IO");
        const auto p=file(L"owner-content.json");raw(p+L".owner","foreign");auto s=nw::Session::open(p,machine);
        check(!s.ready()&&s.error().code==nw::SessionErrorCode::Invalid&&bytes(p+L".owner")=="foreign","unknown owner never repaired");
    });
    run("native/path/regular-singlelink-only",[&]{
        const auto p=file(L"record-link.json");raw(p,encoded(nativeRecord()));need(CreateHardLinkW(file(L"record-alias").c_str(),p.c_str(),nullptr)!=FALSE,"real private record hardlink");
        {auto s=nw::Session::open(p,machine);check(!s.ready()&&s.error().code==nw::SessionErrorCode::Invalid,"hardlinked record rejected");}
        const auto o=file(L"owner-link.json");raw(o+L".owner","");need(CreateHardLinkW(file(L"owner-alias").c_str(),(o+L".owner").c_str(),nullptr)!=FALSE,"real private owner hardlink");
        {auto s=nw::Session::open(o,machine);check(!s.ready()&&s.error().code==nw::SessionErrorCode::Invalid,"hardlinked owner rejected");}
        check(nw::store(p,nativeRecord()).state==nw::StoreState::Invalid,"store cannot replace hardlinked destination");
        const auto d=file(L"directory.json");std::filesystem::create_directory(d);auto s=nw::Session::open(d,machine);check(!s.ready(),"directory rejected");
    });
    run("native/path/unsafe-attributes-and-info",[&]{
        for(const auto fault:{io::Fault::OwnerType,io::Fault::RecordType,io::Fault::OwnerInfoChange,io::Fault::ParentReparse,io::Fault::OwnerReparse,io::Fault::RecordReparse}){
            const auto p=file(L"unsafe-"+std::to_wstring(int(fault))+L".json");raw(p,encoded(nativeRecord()));raw(p+L".owner","");io::reset(fault);
            {auto s=nw::Session::open(p,machine);check(!s.ready()&&!s.record(),"unsafe/unknown/changing file identity rejected");}io::reset();}
    });
    run("native/lease/duplicate-retains-owner-after-session-destruction",[&]{
        const auto p=file(L"duplicate-lease.json");HANDLE lease=INVALID_HANDLE_VALUE;std::string persisted;
        {auto s=nw::Session::open(p,machine);need(s.ready()&&s.reserve(1,nativeAttempt()),"seed active before duplicate");persisted=bytes(p);
            need(s.duplicateOwnerLeaseForChild(lease)&&lease!=INVALID_HANDLE_VALUE&&lease!=nullptr,"duplicate owner lease succeeds");
            const HANDLE original=lease;SetLastError(ERROR_SUCCESS);
            check(!s.duplicateOwnerLeaseForChild(lease)&&GetLastError()==ERROR_INVALID_PARAMETER&&lease==original,"occupied output rejected and original handle preserved");
            DWORD flags=0;check(GetHandleInformation(lease,&flags)!=FALSE&&(flags&HANDLE_FLAG_INHERIT)!=0,"duplicate explicitly inheritable");
            BY_HANDLE_FILE_INFORMATION info{};check(GetFileInformationByHandle(lease,&info)!=FALSE&&info.nNumberOfLinks==1&&info.nFileSizeHigh==0&&info.nFileSizeLow==0,"duplicate refers to zero-byte singlelink owner file");}
        {auto blocked=nw::Session::open(p,machine);check(!blocked.ready()&&!blocked.record()&&blocked.error().code==nw::SessionErrorCode::RetryableIo,"duplicate alone keeps reopened Session excluded");
            check(bytes(p)==persisted,"blocked reopen preserves original active bytes");}
        check(CloseHandle(lease)!=FALSE,"caller closes owned duplicate");
        {auto reopened=nw::Session::open(p,machine);need(reopened.ready()&&reopened.openedFrom()==nw::LoadState::Valid,"reopen succeeds after final duplicate closes");
            check(snapshot(reopened)==persisted&&reopened.record()->active.has_value()&&!reopened.record()->active->child,"original childless active remains exact after lease release");}
    });
    run("native/lock/other-process-writer-blocked",[&]{
        const auto p=file(L"owner-lock.json");{auto s=nw::Session::open(p,machine);need(s.ready(),"first native owner on missing file");
            nativeProbe(p,file(L"probe-missing.json"),false);need(s.reserve(1,nativeAttempt()),"first owner reserves");nativeProbe(p,file(L"probe-active.json"),false);
            {auto other=nw::Session::open(p,machine);check(!other.ready()&&!other.record()&&other.error().code==nw::SessionErrorCode::RetryableIo,"same-process second object excluded");}}
        nativeProbe(p,file(L"probe-released.json"),true);
    });
    for(const bool existing:{false,true})run(existing?"native/crash/zero-owner-existing-record":"native/crash/zero-owner-missing-record",[&]{
        const auto p=file(existing?L"crash-existing.json":L"crash-missing.json");const auto before=encoded(nativeRecord());if(existing)raw(p,before);
        check(ownedChild({L"--native-crash-owner",p})==73,"owned process exits just after zero-byte owner creation");
        check(bytes(p+L".owner").empty(),"zero-byte owner survives crash");{auto s=nw::Session::open(p,machine);check(s.ready(),"new owner after abrupt process exit");
            if(existing)check(snapshot(s)==before&&bytes(p)==before&&!s.record()->active->child,"childless active persists across parent exit");
            else check(rev(s)==1&&!s.record()->active&&!std::filesystem::exists(p),"missing record remains missing");}
        nativeProbe(p,file(existing?L"probe-crash-existing.json":L"probe-crash-missing.json"),true);
    });
}

int wmain(int argc,wchar_t** argv){
    if(argc==5&&std::wstring(argv[1])==L"--native-probe"){
        auto s=nw::Session::open(argv[2],machine);const bool expected=std::wstring(argv[4])==L"ready";
        raw(argv[3],J({{"pass",s.ready()==expected},{"ready",s.ready()},{"expectedReady",expected},{"error",s.error().reason},{"hasRecord",s.record()!=nullptr}}).dump());return s.ready()==expected?0:1;
    }
    if(argc==3&&std::wstring(argv[1])==L"--native-crash-owner"){io::crashAfterCreate=true;auto s=nw::Session::open(argv[2],machine);return s.ready()?74:75;}
    if(argc==5&&std::wstring(argv[1])==L"--probe"){
        auto s=cw::Session::open(argv[2],machine);const bool expected=std::wstring(argv[4])==L"ready";
        raw(argv[3],J({{"pass",s.ready()==expected},{"ready",s.ready()},{"expectedReady",expected},{"error",s.error().reason},{"hasRecord",s.record()!=nullptr}}).dump());return s.ready()==expected?0:1;
    }
    if(argc==3&&std::wstring(argv[1])==L"--crash-owner"){
        io::crashAfterCreate=true;auto s=cw::Session::open(argv[2],machine);return s.ready()?74:75;
    }
    const bool nativeOnly=argc==4&&std::wstring(argv[3])==L"--native-refresh-only";
    if(argc!=3&&!nativeOnly)return 64;
    const std::filesystem::path root(argv[1]);if(!root.is_absolute()||!std::filesystem::is_directory(root))return 65;
    const auto file=[&](const wchar_t* name){return (root/name).wstring();};
    if(!nativeOnly){
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
    }
    nativeCases(root);
    const J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"realPrivateFileIo",true},{"nativeRefreshOnly",nativeOnly},{"ownerLeaseDuplicateSameProcessTested",true},{"ownerLeaseCrossProcessInheritanceTested",false},{"jobQuietOrTerminalProofTested",false},{"realIndependentNativeProcesses",children},{"ownerCreationCrashProcesses",crashChildren},{"childRuns",childReports},{"normalProductHostRealCliOrDevicesExecuted",false},{"JobOrLivenessCallsInProduction",false},{"scope","Strict writer data + bounded synchronous owner session only; no lifecycle or cleanup authority"}};
    raw(argv[2],result.dump(2)+"\n");std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures.size()},{"children",children}}).dump()<<"\n";return failures.empty()?0:1;
}