#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include "virtual_target_return_record.h"
namespace ioTest {
enum class Role {Owner,Record,Temporary};
enum class Fault {None,Write,ShortWrite,Flush,Rename,Read,ShortRead,OwnerWrite,OwnerFlush,OwnerRead,OwnerShortRead,OwnerReparse,RecordReparse,ParentReparse,OwnerUnknownType,OwnerInfoChanged};
inline std::map<HANDLE,Role> handles;
inline Fault fault=Fault::None;
inline HANDLE pauseAfterOwnerCreateReady=nullptr,pauseAfterOwnerCreateRelease=nullptr;
inline unsigned ownerCreates=0,recordOpens=0,ownerReads=0,recordReads=0,ownerWrites=0,writes=0,ownerFlushes=0,flushes=0,renames=0,ownerInfos=0;
Role role(const std::wstring& p){if(p.size()>=6&&p.substr(p.size()-6)==L".owner")return Role::Owner;return p.find(L".tmp.")!=std::wstring::npos?Role::Temporary:Role::Record;}
void reset(Fault f=Fault::None){fault=f;ownerCreates=recordOpens=ownerReads=recordReads=ownerWrites=writes=ownerFlushes=flushes=renames=ownerInfos=0;}
HANDLE create(LPCWSTR p,DWORD a,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templateFile){
 const auto r=role(p);if(r==Role::Owner)++ownerCreates;else if(r==Role::Record)++recordOpens;
 const auto h=::CreateFileW(p,a,sharing,security,creation,flags,templateFile);if(h!=INVALID_HANDLE_VALUE)handles.emplace(h,r);
 // Exact native boundary: the new owner handle exists, but Session has not
 // received it and cannot have initialized an owner or loaded/stored a record.
 if(h!=INVALID_HANDLE_VALUE&&r==Role::Owner&&creation==CREATE_NEW&&pauseAfterOwnerCreateReady){
  if(!SetEvent(pauseAfterOwnerCreateReady))ExitProcess(31);
  if(WaitForSingleObject(pauseAfterOwnerCreateRelease,15000)!=WAIT_OBJECT_0)ExitProcess(32);
 }
 return h;
}
BOOL close(HANDLE h){const auto ok=::CloseHandle(h);if(ok)handles.erase(h);return ok;}
DWORD attributes(LPCWSTR p){const DWORD a=::GetFileAttributesW(p);if(a==INVALID_FILE_ATTRIBUTES)return a;
 if(fault==Fault::ParentReparse&&(a&FILE_ATTRIBUTE_DIRECTORY)&&std::wstring(p).size()>3)return a|FILE_ATTRIBUTE_REPARSE_POINT;
 if(!(a&FILE_ATTRIBUTE_DIRECTORY)&&((fault==Fault::OwnerReparse&&role(p)==Role::Owner)||(fault==Fault::RecordReparse&&role(p)==Role::Record)))return a|FILE_ATTRIBUTE_REPARSE_POINT;return a;
}
BOOL write(HANDLE h,LPCVOID b,DWORD n,LPDWORD out,LPOVERLAPPED o){const bool owner=handles.at(h)==Role::Owner;if(owner)++ownerWrites;else ++writes;
 if((owner&&fault==Fault::OwnerWrite)||(!owner&&fault==Fault::Write)){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
 if(!owner&&fault==Fault::ShortWrite)return ::WriteFile(h,b,n-1,out,o);return ::WriteFile(h,b,n,out,o);
}
BOOL flush(HANDLE h){const bool owner=handles.at(h)==Role::Owner;if(owner)++ownerFlushes;else ++flushes;
 if((owner&&fault==Fault::OwnerFlush)||(!owner&&fault==Fault::Flush)){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);
}
BOOL rename(LPCWSTR a,LPCWSTR b,DWORD flags){++renames;if(fault==Fault::Rename){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::MoveFileExW(a,b,flags);}
BOOL read(HANDLE h,LPVOID b,DWORD n,LPDWORD out,LPOVERLAPPED o){const bool owner=handles.at(h)==Role::Owner;if(owner)++ownerReads;else ++recordReads;
 if((owner&&fault==Fault::OwnerRead)||(!owner&&fault==Fault::Read)){SetLastError(ERROR_READ_FAULT);return FALSE;}
 if(n>1&&((owner&&fault==Fault::OwnerShortRead)||(!owner&&fault==Fault::ShortRead)))return ::ReadFile(h,b,n-1,out,o);return ::ReadFile(h,b,n,out,o);
}
DWORD type(HANDLE h){if(fault==Fault::OwnerUnknownType&&handles.at(h)==Role::Owner){SetLastError(ERROR_INVALID_DATA);return FILE_TYPE_UNKNOWN;}return ::GetFileType(h);}
BOOL info(HANDLE h,LPBY_HANDLE_FILE_INFORMATION out){const BOOL ok=::GetFileInformationByHandle(h,out);if(handles.at(h)==Role::Owner){++ownerInfos;if(ok&&fault==Fault::OwnerInfoChanged&&ownerInfos>=2)++out->ftLastWriteTime.dwLowDateTime;}return ok;}
}
#define CreateFileW ioTest::create
#define CloseHandle ioTest::close
#define GetFileAttributesW ioTest::attributes
#define WriteFile ioTest::write
#define FlushFileBuffers ioTest::flush
#define MoveFileExW ioTest::rename
#define ReadFile ioTest::read
#define GetFileType ioTest::type
#define GetFileInformationByHandle ioTest::info
#include "virtual_target_return_session.h"
#undef CreateFileW
#undef CloseHandle
#undef GetFileAttributesW
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReadFile
#undef GetFileType
#undef GetFileInformationByHandle
namespace tr=ymcc::targetReturn;
using J=nlohmann::json;
static_assert(!std::is_copy_constructible_v<tr::Session>&&!std::is_move_constructible_v<tr::Session>);
static_assert(std::is_same_v<decltype(std::declval<const tr::Session&>().record()),const tr::Record*>);
namespace {
const std::string machine="RAW|MS-1T52|微星|ClawA2VM";
const std::string pathA="HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL_A";
const std::string pathB="HID\\VID_0B05&PID_1B4C&IG_00\\PHYSICAL_B";
const tr::ReservationProof pathProof{true,true,true};
const tr::TargetCreationProof targetProof{true,true};
unsigned assertions=0,childCount=0,terminatedChildren=0;
J cases=J::array(),failures=J::array(),childRuns=J::array(),creationCrashReports=J::array();std::string active;
void check(bool condition,const std::string& label){++assertions;if(!condition)failures.push_back({{"case",active},{"assertion",label}});}
void need(bool condition,const std::string& label){check(condition,label);if(!condition)throw std::runtime_error("required fixture setup: "+label);}
void run(const std::string& name,const std::function<void()>& body){active=name;const auto before=assertions;const auto failed=failures.size();try{body();}catch(const std::exception&e){check(false,std::string("scenario exception: ")+e.what());}check(ioTest::handles.empty(),"all tracked file handles released after scenario");ioTest::reset();cases.push_back({{"name",name},{"assertions",assertions-before},{"failures",failures.size()-failed}});}
tr::Origin origin(unsigned n=104,bool source=false,const std::string& persona="dualsense-edge"){
 tr::Origin o;o.transactionId="session-tx-"+std::to_string(n);o.parent={100+n,"134368044937265940"};o.host={200+n,"134368044937266000"};o.hostInstanceId="host-"+std::to_string(n);o.runId="run-"+std::to_string(n);o.epoch=n;o.targetId="target-"+std::to_string(n);o.persona=persona;if(source)o.physicalRoot="USB\\VID_0DB0&PID_1901\\MSI";return o;
}
tr::AcceptedPsPin pin(){return {"SWD\\HIDMAESTRO\\EXACT_ROOT",{"HID\\VID_054C&PID_0DF2\\EXACT_HID"},7};}
std::uint64_t rev(const tr::Session& s){if(!s.record())throw std::runtime_error("no readable session record");return s.record()->revision;}
std::string encoded(const tr::Record& r){std::string s;if(!tr::encode(r,s))throw std::runtime_error("fixture encoding failed");return s;}
std::string snapshot(const tr::Session& s){if(!s.record())throw std::runtime_error("no record on failed session");return encoded(*s.record());}
void raw(const std::wstring& p,const std::string& bytes){std::ofstream f(std::filesystem::path(p),std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));if(!f)throw std::runtime_error("private raw write failed");}
std::string readRaw(const std::wstring& p){std::ifstream f(std::filesystem::path(p),std::ios::binary);if(!f)throw std::runtime_error("private raw read failed");return std::string(std::istreambuf_iterator<char>(f),{});}
J readJson(const std::wstring& p){return J::parse(readRaw(p));}
void jsonFile(const std::wstring& p,const J& j){raw(p,j.dump(2)+"\n");}
std::wstring executable(){std::wstring p(32768,L'\0');const DWORD n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));if(!n||n==p.size())throw std::runtime_error("fixture exe path unavailable");p.resize(n);return p;}
std::wstring quote(const std::wstring& s){if(s.find(L'"')!=std::wstring::npos||(!s.empty()&&s.back()==L'\\'))throw std::runtime_error("invalid controlled child argument");return L"\""+s+L"\"";}
struct Child {
 PROCESS_INFORMATION pi{};bool started=false;
 explicit Child(const std::vector<std::wstring>& args){auto cmd=quote(executable());for(const auto& a:args)cmd+=L" "+quote(a);STARTUPINFOW si{};si.cb=sizeof(si);started=CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;if(started)++childCount;}
 Child(const Child&)=delete;Child&operator=(const Child&)=delete;
 ~Child(){if(started){DWORD code=0;if(GetExitCodeProcess(pi.hProcess,&code)&&code==STILL_ACTIVE){TerminateProcess(pi.hProcess,97);WaitForSingleObject(pi.hProcess,3000);}CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}}
 DWORD wait(){if(!started||WaitForSingleObject(pi.hProcess,5000)!=WAIT_OBJECT_0)throw std::runtime_error("owned fixture child failed to exit");DWORD code=0;if(!GetExitCodeProcess(pi.hProcess,&code))throw std::runtime_error("child exit unavailable");return code;}
 bool crash(){if(!started)return false;const bool ok=TerminateProcess(pi.hProcess,83)!=FALSE;if(ok)++terminatedChildren;return ok;}
};
struct Event {HANDLE h=INVALID_HANDLE_VALUE;std::wstring name;explicit Event(const std::wstring& suffix){name=L"Local\\YMCC_TargetReturn104_"+std::to_wstring(GetCurrentProcessId())+suffix;h=CreateEventW(nullptr,TRUE,FALSE,name.c_str());if(!h)throw std::runtime_error("fixture event unavailable");}~Event(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Event(const Event&)=delete;Event&operator=(const Event&)=delete;};
DWORD probe(const std::wstring& file,const std::wstring& result,const std::wstring& expected){Child c({L"--probe",file,result,expected});need(c.started,"new dedicated fixture process launched");const auto code=c.wait();childRuns.push_back(readJson(result));return code;}
void seedTarget(const std::wstring& file){auto s=tr::Session::open(file,machine);if(!s.ready()||!s.reserveTarget(rev(s),origin(),targetProof))throw std::runtime_error("private target seed failed");}

void ownerCreationCrashCases(const std::wstring& directory){
 for(const bool oldRecord:{true,false})run(oldRecord?"lock/creation-crash-old-record":"lock/creation-crash-missing",[&]{
  const auto tag=oldRecord?L"existing":L"missing";const auto p=directory+L"\\creation-crash-"+tag+L".json";
  std::string before;
  if(oldRecord){tr::Record initial;initial.machineBinding=machine;tr::Record candidate;need(tr::reserveTargetCreation(initial,initial.revision,origin(),targetProof,candidate),"private historical target record built without an owner file");before=encoded(candidate);raw(p,before);}
  else need(GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES,"record starts Missing before owner creation");
  need(GetFileAttributesW((p+L".owner").c_str())==INVALID_FILE_ATTRIBUTES,"owner starts absent so child must take CREATE_NEW");
  Event ready(std::wstring(L"_create_ready_")+tag),release(std::wstring(L"_create_release_")+tag);
  Child holder({L"--pause-after-owner-create",p,ready.name,release.name});need(holder.started,"dedicated crash-boundary child launched");
  need(WaitForSingleObject(ready.h,5000)==WAIT_OBJECT_0,"real CREATE_NEW succeeded and paused before returning to Session");
  check(probe(p,directory+L"\\creation-busy-"+tag+L".json",L"busy")==0,"new empty owner is exclusively locked before Session receives handle");
  need(holder.crash(),"only this newly spawned boundary child force-terminated");check(holder.wait()==83,"boundary child died without C++ destructors");
  check(readRaw(p+L".owner").empty(),"post-crash owner is the actual zero-byte file from CREATE_NEW");
  const bool recordUnchanged=oldRecord?readRaw(p)==before:GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES;
  check(recordUnchanged,"child at CREATE_NEW boundary did not write or discard record");
  const auto report=directory+L"\\creation-reopened-"+tag+L".json";
  check(probe(p,report,oldRecord?L"target":L"missing")==0,"next independent process is ready with exact old record or Missing");
  const auto reopened=readJson(report);creationCrashReports.push_back({{"case",active},{"nativeBoundary","after successful owner CREATE_NEW, before return to Session"},{"ownerBytesAfterCrash",0},{"recordUnchangedBeforeReopen",recordUnchanged},{"nextProcess",reopened}});
  check(oldRecord?readRaw(p)==before:GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES,"reopen preserves original record bytes or Missing; no automatic empty commit");
 });
}
int finish(const std::wstring& resultPath){
 const J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"childRuns",childRuns},{"creationCrashReports",creationCrashReports},{"ownedFixtureProcesses",childCount},{"abnormallyTerminatedOwnedFixtureProcesses",terminatedChildren},{"realMultiProcessLock",true},{"deviceApiCalls",0},{"productOrHostLaunched",false},{"runtimeIntegrated",false}};jsonFile(resultPath,result);std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;
}
}
int wmain(int argc,wchar_t** argv){
 if(argc==5&&std::wstring(argv[1])==L"--probe"){
  auto s=tr::Session::open(argv[2],machine);const std::wstring expected=argv[4];bool pass=false;
  if(expected==L"busy")pass=!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::RetryableIo&&s.error().win32Error==ERROR_SHARING_VIOLATION;
  else if(expected==L"ready")pass=s.ready();
  else if(expected==L"missing")pass=s.ready()&&s.openedFrom()==tr::LoadState::Missing&&s.record()->machineBinding==machine&&s.record()->revision==1&&s.record()->obligations.empty();
  else if(expected==L"target")pass=s.ready()&&s.openedFrom()==tr::LoadState::Valid&&s.record()->obligations.size()==1&&s.record()->obligations[0].targetMayExist&&!s.record()->obligations[0].acceptedPsPin&&s.record()->obligations[0].paths.empty()&&tr::sameOrigin(s.record()->obligations[0].origin,origin());
  else if(expected==L"mixed")pass=s.ready()&&s.record()->obligations.size()==2&&s.record()->obligations[0].targetMayExist&&s.record()->obligations[0].acceptedPsPin&&s.record()->obligations[0].paths.size()==1&&s.record()->obligations[1].targetMayExist&&s.record()->obligations[1].paths.empty()&&!s.record()->obligations[1].acceptedPsPin;
  J r={{"pass",pass},{"mode","independent-session-probe"},{"expected",std::filesystem::path(expected).string()},{"ready",s.ready()},{"win32Error",s.error().win32Error},{"reason",s.error().reason},{"openedFrom",int(s.openedFrom())},{"obligations",s.record()?s.record()->obligations.size():0},{"historicalOnly",true}};jsonFile(argv[3],r);return pass?0:1;
 }
 if(argc==5&&std::wstring(argv[1])==L"--pause-after-owner-create"){
  HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[3]),release=OpenEventW(SYNCHRONIZE,FALSE,argv[4]);if(!ready||!release)return 33;
  ioTest::pauseAfterOwnerCreateReady=ready;ioTest::pauseAfterOwnerCreateRelease=release;
  auto s=tr::Session::open(argv[2],machine);CloseHandle(ready);CloseHandle(release);return s.ready()?0:34;
 }
 if(argc==4&&std::wstring(argv[1])==L"--owner-create-crash-tests"){ownerCreationCrashCases(argv[2]);return finish(argv[3]);}
 if(argc==5&&std::wstring(argv[1])==L"--hold"){
  auto s=tr::Session::open(argv[2],machine);if(!s.ready())return 3;
  HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[3]),release=OpenEventW(SYNCHRONIZE,FALSE,argv[4]);if(!ready||!release)return 4;
  if(!SetEvent(ready))return 5;const auto wait=WaitForSingleObject(release,15000);CloseHandle(ready);CloseHandle(release);return wait==WAIT_OBJECT_0?0:6;
 }
 if(argc!=3)return 2;const std::wstring directory=argv[1],resultPath=argv[2];const auto file=[&](const wchar_t* name){return directory+L"\\"+name+L".json";};
 run("open/missing-ready-without-record-write",[&]{ioTest::reset();const auto p=file(L"missing");{auto s=tr::Session::open(p,machine);check(s.ready()&&s.openedFrom()==tr::LoadState::Missing,"missing opens explicit new session");check(s.record()&&s.record()->machineBinding==machine&&s.record()->revision==1&&s.record()->obligations.empty(),"new record has exact binding and revision");check(s.error().code==tr::SessionErrorCode::None,"successful open has no error");check(ioTest::handles.size()==1,"exactly one owner handle held");check(ioTest::writes==0&&ioTest::renames==0&&GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES,"open missing never implicitly stores tombstone");check(ioTest::ownerWrites==0&&ioTest::ownerFlushes==0&&ioTest::ownerReads==0,"zero-byte owner needs no content initialization or payload IO");}check(GetFileAttributesW((p+L".owner").c_str())!=INVALID_FILE_ATTRIBUTES,"owner sentinel persists after destructor");});
 run("lock/same-process-writer-blocked",[&]{const auto p=file(L"same-process");auto first=tr::Session::open(p,machine);need(first.ready(),"first session ready");auto second=tr::Session::open(p,machine);check(!second.ready()&&!second.record(),"second writer has no ready or empty fallback record");check(second.error().code==tr::SessionErrorCode::RetryableIo&&second.error().win32Error==ERROR_SHARING_VIOLATION,"share0 conflict reported precisely");check(!DeleteFileW((p+L".owner").c_str()),"owner cannot be deleted under held writer handle");});
 run("lock/other-process-writer-blocked",[&]{const auto p=file(L"other-process");auto s=tr::Session::open(p,machine);need(s.ready(),"parent holds writer lock");check(probe(p,file(L"probe-busy"),L"busy")==0,"real independent process cannot open same file session");});
 run("lock/destructor-release-and-reuse-owner",[&]{const auto p=file(L"clean-release");std::string owner;{auto s=tr::Session::open(p,machine);need(s.ready(),"first lifetime starts");}owner=readRaw(p+L".owner");ioTest::reset();{auto s=tr::Session::open(p,machine);check(s.ready(),"later explicit open reuses owner file");check(ioTest::ownerWrites==0&&ioTest::ownerFlushes==0,"existing owner validated without rewriting");}check(readRaw(p+L".owner")==owner,"clean release preserves owner marker bytes");check(probe(p,file(L"probe-clean"),L"ready")==0,"new independent process acquires after destructor");});
 run("lock/crash-releases-kernel-lock-retains-debt",[&]{const auto p=file(L"crash");seedTarget(p);const auto before=readRaw(p),owner=readRaw(p+L".owner");Event ready(L"_ready"),release(L"_release");Child holder({L"--hold",p,ready.name,release.name});need(holder.started,"owned fixture lock holder started");need(WaitForSingleObject(ready.h,5000)==WAIT_OBJECT_0,"child signals only after session owns lock");check(probe(p,file(L"probe-during-crash"),L"busy")==0,"another process is excluded while holder lives");need(holder.crash(),"only owned fixture child force-terminated without C++ destructors");check(holder.wait()==83,"abnormal fixture exit observed");check(probe(p,file(L"probe-after-crash"),L"target")==0,"kernel releases lock and fresh process loads historical target-only duty");check(readRaw(p)==before&&readRaw(p+L".owner")==owner,"process death changes neither target debt nor sentinel");});
 run("history/lost-ACK-target-only-reopen",[&]{const auto p=file(L"target-only");{auto s=tr::Session::open(p,machine);need(s.ready(),"session ready");check(s.reserveTarget(rev(s),origin(),targetProof),"PREPARE boundary persisted without ACK or physical source");check(s.record()->obligations.size()==1&&s.record()->obligations[0].targetMayExist&&!s.record()->obligations[0].acceptedPsPin&&s.record()->obligations[0].paths.empty(),"target-only source-free debt retained");}const auto before=readRaw(p);{auto s=tr::Session::open(p,machine);check(s.ready()&&s.openedFrom()==tr::LoadState::Valid,"valid history opens");check(snapshot(s)==before,"reopen neither clears old origin nor substitutes current owner");}check(probe(p,file(L"probe-target"),L"target")==0,"independent session reads exact pin-free duty");});
 run("history/mixed-debt-and-explicit-consume",[&]{const auto p=file(L"mixed");{auto s=tr::Session::open(p,machine);need(s.ready(),"mixed session ready");need(s.reserveTarget(rev(s),origin(1,true),targetProof),"source-bearing target reserved");need(s.reservePath(rev(s),origin(1,true),pathA,pathProof),"actual path reserved");need(s.attachAcceptedPsPin(rev(s),origin(1,true),pin()),"accepted pin persisted");need(s.reserveTarget(rev(s),origin(2,false,"elite"),targetProof),"second source-free PC duty added");}check(probe(p,file(L"probe-mixed"),L"mixed")==0,"new process preserves mixed target path and PS pin");{auto s=tr::Session::open(p,machine);const auto count=s.record()->obligations.size();check(s.consumePath(rev(s),origin(1,true),pathA),"caller-proved physical path consumed and committed");check(s.record()->obligations.size()==count&&s.record()->obligations[0].paths.empty()&&s.record()->obligations[0].targetMayExist&&s.record()->obligations[0].acceptedPsPin,"last physical path retains target and pin debt");check(s.consumeOrigin(rev(s),origin(1,true)),"caller separately proved first origin settled");check(s.record()->obligations.size()==1&&tr::sameOrigin(s.record()->obligations[0].origin,origin(2,false,"elite")),"exact consume retains unrelated source-free responsibility");}});
 run("transform/rejections-never-store",[&]{const auto p=file(L"transform-reject");auto s=tr::Session::open(p,machine);need(s.reserveTarget(rev(s),origin(),targetProof),"baseline duty");const auto before=snapshot(s),disk=readRaw(p);ioTest::reset();check(!s.reserveTarget(rev(s)-1,origin(),targetProof),"stale reserve refused");auto wrong=origin();++wrong.host.pid;check(!s.consumeOrigin(rev(s),wrong),"wrong full origin refused");check(!s.consumeOrigin(rev(s)-1,origin()),"stale consume refused");check(!s.reserveTarget(rev(s),origin(),{true,false}),"missing authenticated-host proof refused");check(!s.reservePath(rev(s),origin(),pathA,pathProof),"empty-root witness cannot manufacture physical path");check(s.error().code==tr::SessionErrorCode::TransformRejected,"pure transform error exposed");check(s.ready()&&snapshot(s)==before&&readRaw(p)==disk,"rejection keeps ready session and both old copies");check(ioTest::writes==0&&ioTest::flushes==0&&ioTest::renames==0,"no file publication on transform rejection");});
 run("store/idempotent-no-writes",[&]{const auto p=file(L"idempotent");auto s=tr::Session::open(p,machine);need(s.reserveTarget(rev(s),origin(1,true),targetProof),"target seed");need(s.reservePath(rev(s),origin(1,true),pathA,pathProof),"path seed");need(s.attachAcceptedPsPin(rev(s),origin(1,true),pin()),"pin seed");const auto before=snapshot(s);ioTest::reset();check(s.reserveTarget(rev(s),origin(1,true),targetProof),"duplicate target accepted");check(s.reservePath(rev(s),origin(1,true),pathA,pathProof),"duplicate path accepted");check(s.attachAcceptedPsPin(rev(s),origin(1,true),pin()),"duplicate pin accepted");check(snapshot(s)==before&&readRaw(p)==before,"idempotence keeps revision and exact bytes");check(ioTest::writes==0&&ioTest::flushes==0&&ioTest::renames==0,"unchanged transforms issue zero record writes flushes or renames");});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"write",ioTest::Fault::Write},{"short-write",ioTest::Fault::ShortWrite},{"flush",ioTest::Fault::Flush},{"rename",ioTest::Fault::Rename}})run("store-fault/"+name+"-preserves-old",[&]{const auto p=file((L"failure-"+std::wstring(name.begin(),name.end())).c_str());auto s=tr::Session::open(p,machine);need(s.reserveTarget(rev(s),origin(),targetProof),"baseline target committed");const auto before=snapshot(s),disk=readRaw(p);ioTest::reset(fault);check(!s.consumeOrigin(rev(s),origin()),"failed store refuses candidate publication");check(s.ready()&&s.error().code==tr::SessionErrorCode::RetryableIo,"failed store exposes error while retaining writer lock");check(snapshot(s)==before,"failed store retains old in-memory obligation");check(readRaw(p)==disk,"failed store retains old record file");check(ioTest::renames==(fault==ioTest::Fault::Rename?1U:0U),"no retry or rename after earlier failure");check(ioTest::handles.size()==1,"only session owner handle remains after failed store");ioTest::reset();check(s.reserveTarget(rev(s),origin(),targetProof),"later explicit idempotent operation can succeed");check(s.error().code==tr::SessionErrorCode::None,"successful operation clears old operation error");});
 run("store-fault/new-reservation-keeps-missing",[&]{const auto p=file(L"new-reservation-fail");auto s=tr::Session::open(p,machine);const auto before=snapshot(s);ioTest::reset(ioTest::Fault::Flush);check(!s.reserveTarget(rev(s),origin(),targetProof),"new target reservation fails closed before caller dispatch");check(snapshot(s)==before&&s.record()->obligations.empty(),"failed first commit keeps original empty memory only");check(GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES,"failed first commit leaves record missing");ioTest::reset();check(s.reserveTarget(rev(s),origin(),targetProof),"caller may explicitly retry after separate failure handling");check(s.record()->obligations[0].targetMayExist,"only committed retry publishes duty");});
 run("store/tombstone-reopen",[&]{const auto p=file(L"tombstone");{auto s=tr::Session::open(p,machine);need(s.reserveTarget(rev(s),origin(),targetProof),"target exists in history");check(s.consumeOrigin(rev(s),origin()),"caller-proved exact origin settlement committed");check(s.record()->obligations.empty(),"in-memory tombstone published after commit");}{auto s=tr::Session::open(p,machine);check(s.ready()&&s.openedFrom()==tr::LoadState::Valid&&s.record()->obligations.empty()&&rev(s)==3,"tombstone is valid record with historical revision");}});
 for(const auto& name:std::vector<std::string>{"corrupt","foreign","zero-bytes","oversize"})run("open/invalid-"+name+"-not-empty",[&]{const auto p=file((L"invalid-"+std::wstring(name.begin(),name.end())).c_str());if(name=="foreign"){seedTarget(p);auto j=J::parse(readRaw(p));j["machineBinding"]="OTHER";raw(p,j.dump());}else raw(p,name=="zero-bytes"?"":name=="oversize"?std::string(tr::maxRecordBytes+1,'x'):"{broken");const auto before=readRaw(p);ioTest::reset();auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"invalid load never offers consumable empty record");const auto reason=s.error().reason;check(!s.reserveTarget(1,origin(),targetProof)&&s.error().reason==reason,"failed session refuses operations and preserves open error");check(readRaw(p)==before&&ioTest::writes==0&&ioTest::renames==0,"invalid original file remains byte exact");check(ioTest::recordOpens==1,"one bounded load attempt without retry");check(ioTest::handles.empty(),"open failure releases owner immediately");});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"read",ioTest::Fault::Read},{"short-read",ioTest::Fault::ShortRead}})run("open/retryable-"+name,[&]{const auto p=file((L"retryable-"+std::wstring(name.begin(),name.end())).c_str());seedTarget(p);const auto before=readRaw(p);ioTest::reset(fault);{auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::RetryableIo,"read uncertainty is no record");check(ioTest::recordOpens==1&&ioTest::recordReads==1,"one failed read with no retry");check(ioTest::handles.empty(),"failed open releases all owner and record handles");}ioTest::reset();auto later=tr::Session::open(p,machine);check(later.ready()&&snapshot(later)==before,"separate explicit open can recover after fault");});
 run("open/real-record-sharing-conflict",[&]{const auto p=file(L"record-sharing");seedTarget(p);HANDLE busy=CreateFileW(p.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(busy!=INVALID_HANDLE_VALUE,"fixture privately holds exclusive record handle");{auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::RetryableIo,"real record sharing failure is unknown not empty");check(ioTest::handles.empty(),"failed load releases acquired owner");}check(CloseHandle(busy)!=FALSE,"private record holder released");auto later=tr::Session::open(p,machine);check(later.ready(),"separate open acquires after sharing failure");});
 run("open/unsafe-explicit-paths-and-machine",[&]{for(const auto& p:std::vector<std::wstring>{L"relative.json",L"\\\\server\\share\\record.json",directory+L"\\..\\record.json",directory+L"\\record.json:ads",directory+L"\\NUL.json",directory+L"\\record. ",L"\\\\?\\G:\\record.json"}){ioTest::reset();auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"store path safety reused for explicit local path");check(ioTest::ownerCreates==0&&ioTest::recordOpens==0,"unsafe syntax rejected before file open");}ioTest::reset();auto s=tr::Session::open(file(L"bad-binding"),"");check(!s.ready()&&!s.record()&&ioTest::ownerCreates==0,"bad machine identity rejected before owner creation");});
 run("open/missing-parent-no-creation",[&]{const auto p=directory+L"\\absent-parent\\record.json";ioTest::reset();auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::RetryableIo,"missing parent is uncertain IO");check(ioTest::ownerCreates==0&&ioTest::recordOpens==0,"no implicit directory creation or retry");});
 run("owner/unknown-existing-content",[&]{const auto seed=file(L"owner-marker-seed");{auto s=tr::Session::open(seed,machine);need(s.ready(),"known owner marker generated");}check(readRaw(seed+L".owner").empty(),"new owner sentinel is zero bytes");unsigned n=0;for(const auto& bytes:std::vector<std::string>{"unknown",std::string(26,'?'),"YMCC_TARGET_RETURN_OWNER_V1\n"}){const auto p=file((L"unknown-owner-"+std::to_wstring(n++)).c_str());raw(p+L".owner",bytes);ioTest::reset();{auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"nonzero existing owner content refused");}check(readRaw(p+L".owner")==bytes&&ioTest::ownerWrites==0&&ioTest::recordOpens==0,"unknown owner content never overwritten or used for record access");}});
 run("owner/directories-and-hardlinks",[&]{const auto dirFile=file(L"owner-directory");need(CreateDirectoryW((dirFile+L".owner").c_str(),nullptr)!=FALSE,"private owner directory fixture created");{auto s=tr::Session::open(dirFile,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"directory cannot be owner sentinel");}const auto seed=file(L"owner-hardlink-source"),linked=file(L"owner-hardlink");{auto s=tr::Session::open(seed,machine);need(s.ready(),"owner hardlink source initialized");}need(CreateHardLinkW((linked+L".owner").c_str(),(seed+L".owner").c_str(),nullptr)!=FALSE,"real owner hardlink created");{auto s=tr::Session::open(linked,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"multiply linked owner rejected");}check(DeleteFileW((linked+L".owner").c_str())!=FALSE,"only private fixture hardlink removed");});
 run("record/directories-and-hardlinks",[&]{const auto p=file(L"record-directory");need(CreateDirectoryW(p.c_str(),nullptr)!=FALSE,"private record directory created");{auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"record directory rejected by existing store");}const auto source=file(L"record-link-source"),link=file(L"record-link");seedTarget(source);need(CreateHardLinkW(link.c_str(),source.c_str(),nullptr)!=FALSE,"real record hardlink created");{auto s=tr::Session::open(link,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"multiply linked record rejected");}check(DeleteFileW(link.c_str())!=FALSE,"private record hardlink removed");});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"owner-reparse",ioTest::Fault::OwnerReparse},{"record-reparse",ioTest::Fault::RecordReparse},{"parent-reparse",ioTest::Fault::ParentReparse},{"unknown-file-type",ioTest::Fault::OwnerUnknownType}})run("open/unsafe-metadata-"+name,[&]{const auto p=file((L"unsafe-"+std::wstring(name.begin(),name.end())).c_str());seedTarget(p);const auto before=readRaw(p);ioTest::reset(fault);{auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::Invalid,"explicit unsafe metadata observation rejects owner or record");check(readRaw(p)==before&&ioTest::writes==0&&ioTest::renames==0,"unsafe metadata never mutates record");check(ioTest::handles.empty(),"unsafe open releases acquired handles");}});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"write",ioTest::Fault::OwnerWrite},{"flush",ioTest::Fault::OwnerFlush}})run("owner/no-payload-"+name,[&]{const auto p=file((L"owner-no-"+std::wstring(name.begin(),name.end())).c_str());ioTest::reset(fault);auto s=tr::Session::open(p,machine);check(s.ready()&&s.record()&&s.openedFrom()==tr::LoadState::Missing,"owner opening requires no payload write or flush");check(ioTest::recordOpens==1&&ioTest::handles.size()==1,"one bounded load follows locked empty sentinel");check(ioTest::ownerWrites==0&&ioTest::ownerFlushes==0,"owner content IO is never attempted even when fault injection is armed");});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"read",ioTest::Fault::OwnerRead},{"short-read",ioTest::Fault::OwnerShortRead}})run("owner/no-payload-"+name,[&]{const auto p=file((L"owner-no-"+std::wstring(name.begin(),name.end())).c_str());{auto s=tr::Session::open(p,machine);need(s.ready(),"empty owner exists before read-fault control");}ioTest::reset(fault);auto s=tr::Session::open(p,machine);check(s.ready()&&s.record(),"existing zero-byte sentinel is validated without reading payload");check(ioTest::ownerReads==0&&ioTest::recordOpens==1&&ioTest::handles.size()==1,"no owner read or retry; one bounded record load");});
 run("owner/retryable-changed-info",[&]{const auto p=file(L"owner-changed-info");{auto s=tr::Session::open(p,machine);need(s.ready(),"empty owner created before metadata fault");}ioTest::reset(ioTest::Fault::OwnerInfoChanged);auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record()&&s.error().code==tr::SessionErrorCode::RetryableIo,"uncertain owner metadata refuses session");check(ioTest::ownerReads==0&&ioTest::recordOpens==0&&ioTest::handles.empty(),"no retries or load after uncertain owner; handle released");});
 run("owner/preexisting-zero-byte-sentinel",[&]{const auto p=file(L"preexisting-empty-owner");raw(p+L".owner","");ioTest::reset();auto s=tr::Session::open(p,machine);check(s.ready()&&s.record()&&s.openedFrom()==tr::LoadState::Missing,"preexisting zero-byte ordinary sentinel is valid without granting target history");check(s.record()->obligations.empty()&&ioTest::writes==0&&ioTest::ownerWrites==0&&ioTest::ownerReads==0&&ioTest::ownerFlushes==0,"empty sentinel causes no payload IO or fabricated duty");});
 run("history/legacy103-no-source-target",[&]{const auto p=file(L"legacy103");const std::string wire=R"legacy103({"machineBinding":"RAW|MS-1T52|微星|ClawA2VM","obligations":[{"acceptedPsPin":null,"origin":{"epoch":103,"host":{"creationTime":"134368044937266000","pid":303},"hostInstanceId":"host-103","parent":{"creationTime":"134368044937265940","pid":203},"persona":"dualsense-edge","physicalRoot":"","runId":"run-103","targetId":"target-103","transactionId":"tx-103"},"paths":[],"targetMayExist":true}],"revision":2,"schemaVersion":1})legacy103";raw(p,wire);ioTest::reset();auto s=tr::Session::open(p,machine);check(s.ready()&&s.openedFrom()==tr::LoadState::Valid,"actual delivered103 source-free target opens");check(snapshot(s)==wire&&s.record()->obligations[0].targetMayExist&&!s.record()->obligations[0].acceptedPsPin&&s.record()->obligations[0].origin.physicalRoot.empty(),"legacy103 missing-ACK target duty retained exactly");check(ioTest::writes==0&&ioTest::renames==0&&readRaw(p)==wire,"old record is not cleaned or rewritten on open");});
 run("history/legacy101-optional-witness",[&]{const auto p=file(L"legacy101");const std::string wire=R"legacy101({"machineBinding":"RAW|MS-1T52|微星|ClawA2VM","obligations":[{"acceptedPsPin":null,"origin":{"epoch":1,"host":{"creationTime":"134368044937266000","pid":201},"hostInstanceId":"host-1","parent":{"creationTime":"134368044937265940","pid":101},"persona":"dualsense-edge","physicalRoot":"USB\\VID_0DB0&PID_1901\\MSI","runId":"run-1","targetId":"target-1","transactionId":"tx-1"},"paths":[{"instanceId":"HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL_A","stage":"pre-dispatch-reservation"}]},{"acceptedPsPin":null,"origin":{"epoch":2,"host":{"creationTime":"134368044937266000","pid":202},"hostInstanceId":"host-2","parent":{"creationTime":"134368044937265940","pid":102},"persona":"elite","physicalRoot":"USB\\VID_0DB0&PID_1901\\MSI","runId":"run-2","targetId":"target-2","transactionId":"tx-2"},"paths":[{"instanceId":"HID\\VID_0B05&PID_1B4C&IG_00\\PHYSICAL_B","stage":"pre-dispatch-reservation"}]}],"revision":3,"schemaVersion":1})legacy101";raw(p,wire);tr::Record expected;need(tr::decode(wire,expected),"actual101 record decodes through delivered codec");ioTest::reset();auto s=tr::Session::open(p,machine);check(s.ready()&&snapshot(s)==encoded(expected),"legacy101 becomes readable historical record");check(s.record()->obligations.size()==2&&!s.record()->obligations[0].targetMayExist&&!s.record()->obligations[1].targetMayExist,"omitted witness defaults false without losing paths");check(ioTest::writes==0&&ioTest::renames==0&&readRaw(p)==wire,"opening legacy optional field does not reserialize disk");});

 run("store/no-orphan-temporaries",[&]{unsigned count=0;for(const auto& item:std::filesystem::directory_iterator(directory))if(item.path().filename().wstring().find(L".tmp.")!=std::wstring::npos)++count;check(count==0,"all failed and successful store temporaries cleaned");});

 // Task128: only the current producer may create a new protocol-one origin.
 run("writer-protocol/legacy-open-rewrite-rejected",[&]{
  const auto p=file(L"writer-protocol-legacy");auto owner=origin(128,true);tr::Record initial;initial.machineBinding=machine;tr::Record old;
  need(tr::reservePath(initial,initial.revision,owner,pathA,pathProof,old),"legacy protocol fixture has exact path debt");
  const auto wire=encoded(old);check(!J::parse(wire)["obligations"][0]["origin"].contains("cliWriterProtocol"),"legacy wire omits unknown provenance");raw(p,wire);ioTest::reset();
  auto s=tr::Session::open(p,machine);need(s.ready(),"legacy real file opens through actual Session");
  check(s.record()->obligations[0].origin.cliWriterProtocol==0&&readRaw(p)==wire,"open preserves legacy zero and exact file bytes");
  auto changed=owner;changed.cliWriterProtocol=1;
  check(!s.reservePath(rev(s),changed,pathB,pathProof),"same legacy transaction cannot gain new protocol through path reserve");
  check(!s.reserveTarget(rev(s),changed,targetProof)&&!s.attachAcceptedPsPin(rev(s),changed,pin()),"target/pin operations cannot promote old protocol");
  check(snapshot(s)==wire&&readRaw(p)==wire&&ioTest::writes==0&&ioTest::renames==0,"protocol conflict preserves both Session and file without IO");
 });
 run("writer-protocol/one-atomic-persist-reopen",[&]{
  const auto p=file(L"writer-protocol-one");auto owner=origin(129,true);owner.cliWriterProtocol=1;std::string wire;
  {auto s=tr::Session::open(p,machine);need(s.ready(),"new protocol private Session is ready");ioTest::reset();
   need(s.reserveTarget(rev(s),owner,targetProof),"current producer commits explicit protocol-one target");
   check(s.record()->obligations[0].origin.cliWriterProtocol==1&&ioTest::writes==1&&ioTest::flushes==1&&ioTest::renames==1,"protocol one uses original atomic store before publish");
   need(s.reservePath(rev(s),owner,pathA,pathProof)&&s.attachAcceptedPsPin(rev(s),owner,pin()),"same exact protocol supports path and accepted-pin data");
   check(s.record()->obligations[0].origin.cliWriterProtocol==1,"all original data transitions preserve producer protocol");
   wire=snapshot(s);check(readRaw(p)==wire&&J::parse(wire)["obligations"][0]["origin"]["cliWriterProtocol"]==1,"committed bytes explicitly retain protocol one");
   ioTest::reset();check(s.reserveTarget(rev(s),owner,targetProof)&&s.reservePath(rev(s),owner,pathA,pathProof),"exact repeats stay idempotent with protocol one");
   check(ioTest::writes==0&&ioTest::flushes==0&&ioTest::renames==0&&snapshot(s)==wire,"repeats neither rewrite nor manufacture revisions");}
  ioTest::reset();auto reopened=tr::Session::open(p,machine);need(reopened.ready(),"new Session reopens actual committed file");
  check(reopened.record()->obligations[0].origin.cliWriterProtocol==1&&snapshot(reopened)==wire&&ioTest::writes==0,"new lifetime reads exact protocol one without restamping");
  auto changed=owner;changed.cliWriterProtocol=0;
  check(!reopened.reserveTarget(rev(reopened),changed,targetProof)&&!reopened.reservePath(rev(reopened),changed,pathB,pathProof),"same transaction cannot downgrade protocol one");
  check(!reopened.consumeOrigin(rev(reopened),changed)&&!reopened.consumePath(rev(reopened),changed,pathA),"protocol mismatch cannot supply exact origin for consumption");
  check(snapshot(reopened)==wire&&readRaw(p)==wire&&ioTest::writes==0,"downgrade rejection preserves memory and bytes");
 });
 for(const auto& [name,value]:std::vector<std::pair<std::string,J>>{{"future",2},{"float",1.0},{"negative",-1},{"type",true}})run("writer-protocol/invalid-open-"+name,[&]{
  const auto p=file((L"writer-protocol-invalid-"+std::wstring(name.begin(),name.end())).c_str());tr::Record initial;initial.machineBinding=machine;tr::Record prior;
  need(tr::reserveTargetCreation(initial,initial.revision,origin(130),targetProof,prior),"invalid protocol has otherwise valid target data");
  auto bad=J::parse(encoded(prior));bad["obligations"][0]["origin"]["cliWriterProtocol"]=value;const auto wire=bad.dump();raw(p,wire);ioTest::reset();
  auto s=tr::Session::open(p,machine);check(!s.ready()&&!s.record(),"invalid or unsupported protocol never becomes an empty ready Session");
  check(readRaw(p)==wire&&ioTest::writes==0&&ioTest::renames==0,"invalid historical file stays intact without repair or cleanup");
 });
 for(const auto& [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"write",ioTest::Fault::Write},{"short-write",ioTest::Fault::ShortWrite},{"flush",ioTest::Fault::Flush},{"rename",ioTest::Fault::Rename}})run("writer-protocol/store-fault-"+name,[&]{
  const auto p=file((L"writer-protocol-fault-"+std::wstring(name.begin(),name.end())).c_str());auto s=tr::Session::open(p,machine);need(s.ready(),"protocol fault private Session opens");
  need(s.reserveTarget(rev(s),origin(131),targetProof),"preexisting legacy-zero target data commits");const auto before=snapshot(s);const auto previousRevision=rev(s);
  auto next=origin(132);next.cliWriterProtocol=1;ioTest::reset(fault);
  check(!s.reserveTarget(previousRevision,next,targetProof),"new independent protocol-one candidate fails injected original store boundary");
  check(rev(s)==previousRevision&&snapshot(s)==before&&readRaw(p)==before,"failed protocol-one store preserves exact old-zero memory and disk");
  ioTest::reset();check(s.reserveTarget(previousRevision,next,targetProof),"same finite candidate may retry after explicit store fault removed");
  check(s.record()->obligations.size()==2&&s.record()->obligations[0].origin.cliWriterProtocol==0&&s.record()->obligations[1].origin.cliWriterProtocol==1,"retry keeps each transaction protocol without reclassifying old origin");
  check(readRaw(p)==snapshot(s),"successful retry publishes only committed actual bytes");
 });
 ownerCreationCrashCases(directory);
 return finish(resultPath);
}
