#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "virtual_target_return_record.h"
namespace ioTest {
enum class Fault {None,Write,ShortWrite,Flush,Rename,Read,ShortRead,InfoChanged,ReparseLeaf,ReparseParent};
inline std::set<HANDLE> handles;
inline Fault fault=Fault::None;inline unsigned writes=0,flushes=0,renames=0,reads=0,infos=0;
inline void reset(Fault f=Fault::None){fault=f;writes=flushes=renames=reads=infos=0;}
HANDLE create(LPCWSTR p,DWORD a,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templateFile){const auto h=::CreateFileW(p,a,sharing,security,creation,flags,templateFile);if(h!=INVALID_HANDLE_VALUE)handles.insert(h);return h;}
BOOL close(HANDLE h){const auto ok=::CloseHandle(h);if(ok)handles.erase(h);return ok;}
DWORD attributes(LPCWSTR p){const auto a=::GetFileAttributesW(p);if(a==INVALID_FILE_ATTRIBUTES)return a;if(fault==Fault::ReparseLeaf&&!(a&FILE_ATTRIBUTE_DIRECTORY))return a|FILE_ATTRIBUTE_REPARSE_POINT;if(fault==Fault::ReparseParent&&(a&FILE_ATTRIBUTE_DIRECTORY)&&std::wstring(p).size()>3)return a|FILE_ATTRIBUTE_REPARSE_POINT;return a;}
BOOL write(HANDLE h,LPCVOID b,DWORD n,LPDWORD out,LPOVERLAPPED o){++writes;if(fault==Fault::Write){SetLastError(ERROR_WRITE_FAULT);return FALSE;}if(fault==Fault::ShortWrite)return ::WriteFile(h,b,n-1,out,o);return ::WriteFile(h,b,n,out,o);}
BOOL flush(HANDLE h){++flushes;if(fault==Fault::Flush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);}
BOOL rename(LPCWSTR a,LPCWSTR b,DWORD flags){++renames;if(fault==Fault::Rename){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::MoveFileExW(a,b,flags);}
BOOL read(HANDLE h,LPVOID b,DWORD n,LPDWORD out,LPOVERLAPPED o){++reads;if(fault==Fault::Read){SetLastError(ERROR_READ_FAULT);return FALSE;}if(fault==Fault::ShortRead&&n>1)return ::ReadFile(h,b,n-1,out,o);return ::ReadFile(h,b,n,out,o);}
BOOL info(HANDLE h,LPBY_HANDLE_FILE_INFORMATION out){++infos;const BOOL ok=::GetFileInformationByHandle(h,out);if(ok&&fault==Fault::InfoChanged&&infos>=2)++out->ftLastWriteTime.dwLowDateTime;return ok;}
}
#define CreateFileW ioTest::create
#define CloseHandle ioTest::close
#define GetFileAttributesW ioTest::attributes
#define WriteFile ioTest::write
#define FlushFileBuffers ioTest::flush
#define MoveFileExW ioTest::rename
#define ReadFile ioTest::read
#define GetFileInformationByHandle ioTest::info
#include "virtual_target_return_store.h"
#include "virtual_target_return_session.h"
#undef CreateFileW
#undef CloseHandle
#undef GetFileAttributesW
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReadFile
#undef GetFileInformationByHandle
namespace tr=ymcc::targetReturn;using J=nlohmann::json;
namespace {
unsigned assertions=0;J cases=J::array(),failures=J::array();std::string active;
void check(bool condition,const std::string&label){++assertions;if(!condition)failures.push_back({{"case",active},{"assertion",label}});}
void run(const std::string&name,const std::function<void()>&body){active=name;auto before=assertions;auto failed=failures.size();try{body();}catch(const std::exception&e){check(false,std::string("unexpected exception: ")+e.what());}check(ioTest::handles.empty(),"all store handles released after every scenario");cases.push_back({{"name",name},{"assertions",assertions-before},{"failures",failures.size()-failed}});}
tr::Origin origin(unsigned n=1,const std::string&persona="dualsense-edge"){
 tr::Origin o;o.transactionId="tx-"+std::to_string(n);o.parent={100+n,"134368044937265940"};o.host={200+n,"134368044937266000"};o.hostInstanceId="host-"+std::to_string(n);o.runId="run-"+std::to_string(n);o.epoch=n;o.targetId="target-"+std::to_string(n);o.persona=persona;o.physicalRoot="USB\\VID_0DB0&PID_1901\\MSI";return o;
}
const std::string pathA="HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL_A",pathB="HID\\VID_0B05&PID_1B4C&IG_00\\PHYSICAL_B";
const tr::ReservationProof proof{true,true,true};
const tr::NativeOwnedSetProof nativeSetProof{true,true,true};
tr::Origin nativeOrigin(){auto o=origin(303,"steamdeck");o.cliWriterProtocol=1;return o;}
std::vector<std::string> nativePaths(unsigned count){std::vector<std::string> paths;for(unsigned i=0;i<count;++i)paths.push_back("HID\\VID_1234&PID_5678\\NATIVE_"+std::to_string(i));return paths;}

tr::Record empty(){tr::Record r;r.machineBinding="RAW|MS-1T52|微星|ClawA2VM";return r;}
std::string encoded(const tr::Record&r){std::string s;if(!tr::encode(r,s))throw std::runtime_error("fixture record encoding failed");return s;}
tr::Record one(){auto r=empty();tr::Record c;if(!tr::reservePath(r,r.revision,origin(),pathA,proof,c))throw std::runtime_error("fixture reserve failed");return c;}
void raw(const std::wstring&p,const std::string&s){std::ofstream out(std::filesystem::path(p),std::ios::binary);out.write(s.data(),std::streamsize(s.size()));if(!out)throw std::runtime_error("fixture private file write failed");}
std::string readRaw(const std::wstring&p){std::ifstream in(std::filesystem::path(p),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});}
tr::AcceptedPsPin psPin(){return {"SWD\\HIDMAESTRO\\EXACT_ROOT",{"HID\\VID_054C&PID_0DF2\\EXACT_HID"},7};}

// Source-free PC/PS target provenance has no physical hide ancestry.
tr::Origin noSourceOrigin(unsigned n=103,const std::string&persona="dualsense-edge"){auto o=origin(n,persona);o.physicalRoot.clear();return o;}
tr::Record targetWitness(unsigned n=103,const std::string&persona="dualsense-edge"){
 auto r=empty();tr::Record c;if(!tr::reserveTargetCreation(r,r.revision,noSourceOrigin(n,persona),{true,true},c))throw std::runtime_error("fixture target reservation failed");return c;
}
}
int wmain(int argc,wchar_t**argv){
 if(argc==3&&std::wstring(argv[1])==L"--child-read"){
  auto r=tr::load(argv[2],empty().machineBinding);auto base=one();tr::Record expected;const bool built=tr::reservePath(base,base.revision,origin(2,"elite"),pathB,proof,expected);const bool pass=built&&r.state==tr::LoadState::Valid&&r.record&&encoded(*r.record)==encoded(expected);
  const auto targetFile=std::filesystem::path(argv[2]).parent_path()/L"target-witness-cross-process.json";
  auto target=tr::load(targetFile.wstring(),empty().machineBinding);const auto targetExpected=targetWitness();
  const bool targetPass=target.state==tr::LoadState::Valid&&target.record&&encoded(*target.record)==encoded(targetExpected)&&target.record->obligations[0].targetMayExist&&!target.record->obligations[0].acceptedPsPin&&target.record->obligations[0].paths.empty()&&target.record->obligations[0].origin.physicalRoot.empty();
  if(!targetPass){std::cout<<J({{"pass",false},{"mode","independent-process-target-witness-read"}}).dump()<<"\n";return 1;}
  const auto nativeFile=std::filesystem::path(argv[2]).parent_path()/L"native-owned-set-cross-process.json";
  auto native=tr::load(nativeFile.wstring(),empty().machineBinding);tr::Record nativeExpected;
  const bool nativeBuilt=tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),nativePaths(2),nativeSetProof,nativeExpected);
  const bool nativePass=nativeBuilt&&native.state==tr::LoadState::Valid&&native.record&&encoded(*native.record)==encoded(nativeExpected)&&native.record->schema==1&&native.record->obligations[0].paths.size()==2;
  std::cout<<J({{"pass",pass&&nativePass},{"mode","independent-process-read"},{"targetWitnessRoundtrip",targetPass},{"nativeOwnedSetRoundtrip",nativePass},{"obligationCount",r.record?r.record->obligations.size():0},{"historicalOnly",true}}).dump()<<"\n";return pass&&nativePass?0:1;
 }
 if(argc!=3)return 2;const std::wstring directory=argv[1];const std::wstring resultPath=argv[2];const auto file=directory+L"\\record.json";
 run("roundtrip/tombstone",[]{auto r=empty();tr::Record decoded=one();auto s=encoded(r);check(tr::decode(s,decoded),"valid empty record decodes");check(decoded.obligations.empty()&&decoded.machineBinding==r.machineBinding,"explicit tombstone retains raw machine binding");check(encoded(decoded)==s,"tombstone roundtrip exact");});
 run("reserve/single-path-idempotence",[]{auto r=one();auto before=encoded(r);tr::Record c;check(tr::reservePath(r,r.revision,origin(),pathA,proof,c),"reserve duplicate accepted");check(c.revision==r.revision&&encoded(c)==before,"duplicate is idempotent without revision change");auto lower=pathA;for(auto&v:lower)if(v>='A'&&v<='Z')v=char(v-'A'+'a');check(tr::reservePath(r,r.revision,origin(),lower,proof,c),"case variant idempotent");check(c.obligations[0].paths[0].instanceId==pathA,"exact original spelling retained");check(c.obligations[0].paths.size()==1,"no speculative batch tail");});
 run("reserve/prehidden-and-unreached-rejected",[]{auto r=empty();for(const auto&p:std::vector<tr::ReservationProof>{{false,true,true},{true,false,true},{true,true,false}}){auto c=one();const auto sentinel=encoded(c);check(!tr::reservePath(r,r.revision,origin(),pathA,p,c),"missing producer authority rejects");check(encoded(c)==sentinel,"rejection preserves output");}});
 run("origin/immutable-and-conflict",[]{auto r=one();auto sentinel=encoded(r);tr::Record c=r;auto o=origin();++o.epoch;check(!tr::reservePath(r,r.revision,o,pathB,proof,c),"same transaction cannot rewrite tuple");check(encoded(c)==sentinel,"rewrite leaves candidate");check(!tr::reservePath(r,r.revision,origin(2),pathA,proof,c),"different origin cannot claim same path");check(encoded(c)==sentinel,"conflict leaves debt");});
 run("multiple-origins-and-carry",[]{auto r=one();tr::Record c;check(tr::reservePath(r,r.revision,origin(2,"elite"),pathB,proof,c),"second origin accepted");auto target=empty();tr::Record carried;check(tr::mergeForCarry(target,target.revision,c,carried),"carry exact inherited origins");check(carried.obligations.size()==2,"both obligations retained");check(tr::sameOrigin(carried.obligations[0].origin,origin())&&tr::sameOrigin(carried.obligations[1].origin,origin(2,"elite")),"carry never replaces old owner with current run");auto old=encoded(carried);check(tr::mergeForCarry(carried,carried.revision,c,target),"duplicate carry accepted");check(encoded(target)==old,"duplicate borrow/carry idempotent");c.machineBinding="DIFFERENT";check(!tr::mergeForCarry(carried,carried.revision,c,target),"foreign machine carry rejected");});
 run("dispatch/stage-retains-reservation",[]{auto r=one();tr::Record c;check(tr::markMayHaveIssued(r,r.revision,origin(),pathA,c),"dispatch boundary stage advances");check(c.obligations[0].paths[0].stage==tr::DispatchStage::MayHaveIssued,"stage carries possible effect, not success");tr::Record carried;check(tr::mergeForCarry(r,r.revision,c,carried),"stage upgrades across carry");check(carried.obligations[0].paths[0].stage==tr::DispatchStage::MayHaveIssued,"carry never downgrades possible dispatch");});
 run("consume/exact-origin-revision",[]{auto r=one();tr::Record c=r;auto before=encoded(r);check(!tr::consumeOrigin(r,r.revision-1,origin(),c),"stale revision rejected");check(encoded(c)==before,"stale leaves original obligations");auto wrong=origin();++wrong.host.pid;check(!tr::consumeOrigin(r,r.revision,wrong,c),"wrong exact owner rejected");check(!tr::consumePath(r,r.revision,origin(),pathB,c),"wrong exact path rejected");check(tr::consumePath(r,r.revision,origin(),pathA,c),"proved exact path consumed as pure candidate");check(c.obligations.empty()&&c.revision==r.revision+1,"last debt yields explicit tombstone");check(encoded(r)==before,"candidate transform never publishes source");});
 run("revision/overflow-and-newer-debt",[]{auto r=one();r.revision=(std::numeric_limits<std::uint64_t>::max)();auto c=r;auto before=encoded(c);check(!tr::consumeOrigin(r,r.revision,origin(),c),"revision overflow cannot wrap");check(encoded(c)==before,"overflow output unchanged");r=one();auto originalRevision=r.revision;check(tr::reservePath(r,r.revision,origin(2),pathB,proof,c),"new debt increments revision");auto next=c;check(!tr::consumeOrigin(next,originalRevision,origin(),c),"old consumer cannot erase any newer debt");check(c.obligations.size()==2,"new responsibility retained");});
 run("PS/optional-pin-historical-only",[]{auto r=one();check(!r.obligations[0].acceptedPsPin,"missing PS identity stays unknown");auto pin=psPin();tr::Record c;check(tr::attachAcceptedPsPin(r,r.revision,origin(),pin,c),"accepted caller pin retained");auto wire=encoded(c);tr::Record loaded;check(tr::decode(wire,loaded),"PS provenance decodes");check(loaded.obligations[0].acceptedPsPin->bindingGeneration==7,"pin generation preserved");check(wire.find("complete")==std::string::npos,"disk has no current admission complete field");tr::Record consumed;check(tr::consumePath(c,c.revision,origin(),pathA,consumed),"path consumption possible");check(consumed.obligations.size()==1&&consumed.obligations[0].paths.empty(),"pin obligation remains after final path");check(tr::consumeOrigin(consumed,consumed.revision,origin(),loaded),"exact externally proved pin origin may be consumed");check(loaded.obligations.empty(),"pin consumed only by exact origin operation");auto changed=pin;++changed.bindingGeneration;check(!tr::attachAcceptedPsPin(c,c.revision,origin(),changed,loaded),"cannot silently rewrite old pin");});
 run("PS/pin-only-and-personas",[]{auto r=empty();tr::Record c;check(tr::attachAcceptedPsPin(r,r.revision,origin(),psPin(),c),"pin-only responsibility can be preserved");check(c.obligations[0].paths.empty(),"pin-only does not manufacture hide debt");for(const std::string p:{"steamdeck","elite"})check(!tr::attachAcceptedPsPin(r,r.revision,origin(1,p),psPin(),c),"non-PS pin rejected");});
 auto baseline=encoded(one());auto json=J::parse(baseline);
 auto reject=[&](const std::string&name,const std::string&bytes){run("decode/"+name,[&]{auto output=one();const auto sentinel=encoded(output);check(!tr::decode(bytes,output),"bad raw record rejected");check(encoded(output)==sentinel,"decode failure never turns into empty consumable record");});};
 for(const auto&[name,wire]:std::vector<std::pair<std::string,std::string>>{{"empty",""},{"truncated",baseline.substr(0,baseline.size()-1)},{"long",std::string(tr::maxRecordBytes+1,' ')},{"BOM","\xef\xbb\xbf"+baseline},{"invalid-utf8",std::string("{\"machineBinding\":\"")+std::string("\xc0\xaf",2)+"\"}"},{"trailing-garbage",baseline+"x"},{"embedded-NUL",baseline+std::string(1,'\0')},{"duplicate-root","{\"schemaVersion\":1,"+baseline.substr(1)}})reject(name,wire);
 auto mutate=[&](const std::string&name,const std::function<void(J&)>&f){auto j=json;f(j);reject(name,j.dump());};
 mutate("unknown-root",[](J&j){j["unexpected"]=true;});mutate("old-schema",[](J&j){j["schemaVersion"]=0;});mutate("future-schema",[](J&j){j["schemaVersion"]=2;});mutate("floating-schema",[](J&j){j["schemaVersion"]=1.0;});mutate("negative-revision",[](J&j){j["revision"]=-1;});mutate("zero-revision",[](J&j){j["revision"]=0;});
 mutate("owner-pid-overflow",[](J&j){j["obligations"][0]["origin"]["parent"]["pid"]=4294967296ULL;});mutate("owner-negative",[](J&j){j["obligations"][0]["origin"]["host"]["pid"]=-1;});mutate("owner-zero",[](J&j){j["obligations"][0]["origin"]["host"]["pid"]=0;});mutate("creation-overflow",[](J&j){j["obligations"][0]["origin"]["host"]["creationTime"]="18446744073709551616";});mutate("creation-alias",[](J&j){j["obligations"][0]["origin"]["host"]["creationTime"]="0001";});mutate("creation-number",[](J&j){j["obligations"][0]["origin"]["host"]["creationTime"]=17;});
 mutate("unknown-owner-field",[](J&j){j["obligations"][0]["origin"]["host"]["complete"]=true;});mutate("unknown-origin",[](J&j){j["obligations"][0]["origin"]["authority"]=true;});mutate("retired-persona",[](J&j){j["obligations"][0]["origin"]["persona"]="xbox360";});mutate("zero-epoch",[](J&j){j["obligations"][0]["origin"]["epoch"]=0;});
 mutate("empty-machine",[](J&j){j["machineBinding"]="";});mutate("long-machine",[](J&j){j["machineBinding"]=std::string(4097,'x');});mutate("control-machine",[](J&j){j["machineBinding"]="MSI\n";});mutate("unknown-obligation",[](J&j){j["obligations"][0]["claimed"]=true;});mutate("no-duty-group",[](J&j){j["obligations"][0]["paths"]=J::array();});
 for(const auto&bad:std::vector<std::string>{"HID\\VID_1234&PID_5678\\..","HID\\VID_1234&PID_5678\\*","\\\\?\\HID#interface","HID\\VID/PID\\x","HID\\VID\\x:stream","HID\\VID\\\"quoted\"","HID\\VID\\space x",std::string(513,'x')})mutate("illegal-instance-"+std::to_string(bad.size()),[&](J&j){j["obligations"][0]["paths"][0]["instanceId"]=bad;});
 mutate("unknown-stage",[](J&j){j["obligations"][0]["paths"][0]["stage"]="hidden-success";});mutate("duplicate-path",[](J&j){j["obligations"][0]["paths"].push_back(j["obligations"][0]["paths"][0]);});mutate("duplicate-origin",[](J&j){j["obligations"].push_back(j["obligations"][0]);});
 auto nestedDup=baseline;auto at=nestedDup.find("\"transactionId\":");nestedDup.insert(at,"\"transactionId\":\"tx-FAKE\",");reject("duplicate-nested-origin",nestedDup);
 auto escapedDup=baseline;escapedDup.insert(1,"\"schema\\u0056ersion\":1,");reject("duplicate-escaped-key",escapedDup);
 reject("excessive-json-depth",std::string(1000,'[')+"0"+std::string(1000,']'));
 auto maxNumber=baseline;at=maxNumber.find("\"revision\":2");if(at==std::string::npos)throw std::runtime_error("max-number counterexample missing");maxNumber.replace(at,12,"\"revision\":18446744073709551616");reject("numeric-overflow",maxNumber);
 run("bounds/maximum-creation-revision",[]{auto r=one();r.obligations[0].origin.parent.creationTime="18446744073709551615";r.obligations[0].origin.host.pid=4294967295U;r.revision=(std::numeric_limits<std::uint64_t>::max)();tr::Record decoded;check(tr::decode(encoded(r),decoded),"exact UINT maxima accepted without overflow");});
 run("bounds/counts-and-serialized-byte-limit",[]{
 auto r=one();const auto single=r.obligations[0];for(unsigned i=2;i<=17;++i){auto g=single;g.origin=origin(i);g.paths[0].instanceId="HID\\VID_0DB0&PID_1901\\UNIQUE_"+std::to_string(i);r.obligations.push_back(g);}check(!tr::validRecord(r),"origin count bounded");
 r=one();for(unsigned i=1;i<=32;++i)r.obligations[0].paths.push_back({"HID\\VID_0DB0&PID_1901\\MORE_"+std::to_string(i),tr::DispatchStage::Reserved});check(!tr::validRecord(r),"per-origin paths bounded");
 r=one();for(unsigned i=2;i<=5;++i){auto g=single;g.origin=origin(i);g.paths.clear();for(unsigned p=0;p<32;++p)g.paths.push_back({"HID\\VID_0DB0&PID_1901\\GROUP_"+std::to_string(i)+"_"+std::to_string(p),tr::DispatchStage::Reserved});r.obligations.push_back(g);}check(!tr::validRecord(r),"total hidden paths bounded");
 r=one();auto pin=psPin();pin.hidInstanceIds.clear();for(unsigned i=0;i<128;++i)pin.hidInstanceIds.push_back("HID\\VID_054C&PID_0DF2\\"+std::string(480,'A')+std::to_string(i));r.obligations[0].acceptedPsPin=pin;check(tr::validRecord(r),"bounded individual IDs still need wire-size gate");std::string output="sentinel";check(!tr::encode(r,output),"encoded total capped at 64KiB");check(output=="sentinel","encode failure leaves output unchanged");
 });
 run("carry/conflicts-and-exact-owner",[]{auto r=one(),inherited=one(),c=r;inherited.obligations[0].origin=origin(2);auto sentinel=encoded(c);check(!tr::mergeForCarry(r,r.revision,inherited,c),"carry cannot claim another origin path");check(encoded(c)==sentinel,"carry conflict preserves output");inherited=one();++inherited.obligations[0].origin.host.pid;check(!tr::mergeForCarry(r,r.revision,inherited,c),"carry rejects same transaction rewritten owner");});
 auto pinJson=json;pinJson["obligations"][0]["acceptedPsPin"]={{"sdkRootInstanceId","SWD\\HIDMAESTRO\\EXACT_ROOT"},{"hidInstanceIds",J::array({"HID\\VID_054C&PID_0DF2\\EXACT_HID"})},{"bindingGeneration",7}};
 auto badPin=[&](const std::string&name,const std::function<void(J&)>&f){auto j=pinJson;f(j["obligations"][0]["acceptedPsPin"]);reject(name,j.dump());};
 badPin("pin-unknown-complete",[](J&p){p["complete"]=true;});badPin("pin-generation-zero",[](J&p){p["bindingGeneration"]=0;});badPin("pin-generation-float",[](J&p){p["bindingGeneration"]=1.5;});badPin("pin-hid-empty",[](J&p){p["hidInstanceIds"]=J::array();});badPin("pin-hid-wrong-prefix",[](J&p){p["hidInstanceIds"][0]="USB\\VID_054C&PID_0DF2\\TARGET";});badPin("pin-hid-duplicate",[](J&p){p["hidInstanceIds"].push_back(p["hidInstanceIds"][0]);});badPin("pin-bad-root",[](J&p){p["sdkRootInstanceId"]="SWD\\..\\TARGET";});badPin("pin-too-many-hids",[](J&p){p["hidInstanceIds"]=J::array();for(unsigned i=0;i<129;++i)p["hidInstanceIds"].push_back("HID\\VID_054C&PID_0DF2\\"+std::to_string(i));});
 run("IO/missing-not-empty",[&]{ioTest::reset();auto r=tr::load(directory+L"\\missing.json",empty().machineBinding);check(r.state==tr::LoadState::Missing&&!r.record,"missing carries no record");});
 run("IO/new-commit-and-exact-load",[&]{ioTest::reset();auto source=one();auto original=encoded(source);auto r=tr::store(file,source);check(r.committed(),"private record committed");check(ioTest::writes==1&&ioTest::flushes==1&&ioTest::renames==1,"write flush rename used once");auto loaded=tr::load(file,source.machineBinding);check(loaded.state==tr::LoadState::Valid&&loaded.record,"valid load");check(encoded(*loaded.record)==original&&encoded(source)==original,"exact disk content and caller unmodified");});
 for(auto [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"write",ioTest::Fault::Write},{"short-write",ioTest::Fault::ShortWrite},{"flush",ioTest::Fault::Flush},{"rename",ioTest::Fault::Rename}})run("IO/failure-"+name,[&]{ioTest::reset();auto source=one();check(tr::store(file,source).committed(),"baseline committed");auto old=readRaw(file);tr::Record candidate;check(tr::consumeOrigin(source,source.revision,origin(),candidate),"consumption is only candidate");ioTest::reset(fault);auto r=tr::store(file,candidate);check(!r.committed(),"failed write not published");check(readRaw(file)==old,"old disk responsibility retained");check(encoded(source)==old,"old caller responsibility retained");check(ioTest::renames==(fault==ioTest::Fault::Rename?1U:0U),"failed write/flush never reaches publication");ioTest::reset();});
 run("IO/replace-and-tombstone",[&]{ioTest::reset();auto r=one();tr::Record c;check(tr::consumeOrigin(r,r.revision,origin(),c),"tombstone candidate");check(tr::store(file,c).committed(),"tombstone committed without deleting file");auto load=tr::load(file,c.machineBinding);check(load.state==tr::LoadState::Valid&&load.record&&load.record->obligations.empty(),"empty proved only by valid explicit tombstone");check(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES,"record file still exists");});
 for(auto [name,fault]:std::vector<std::pair<std::string,ioTest::Fault>>{{"read",ioTest::Fault::Read},{"short-read",ioTest::Fault::ShortRead},{"changing-info",ioTest::Fault::InfoChanged}})run("IO/retryable-"+name,[&]{ioTest::reset();check(tr::store(file,one()).committed(),"baseline");ioTest::reset(fault);auto r=tr::load(file,empty().machineBinding);check(r.state==tr::LoadState::RetryableIo&&!r.record,"IO uncertainty has no consumable record");ioTest::reset();});
 run("IO/sharing-failure-and-release",[&]{ioTest::reset();auto busy=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(busy!=INVALID_HANDLE_VALUE,"exclusive private test file opened");auto load=tr::load(file,empty().machineBinding);check(load.state==tr::LoadState::RetryableIo&&!load.record,"real sharing failure remains unknown");check(!tr::store(file,empty()).committed(),"sharing blocks publication");check(CloseHandle(busy),"private owner released");check(tr::load(file,empty().machineBinding).state==tr::LoadState::Valid,"later explicit load succeeds without auto retry");});
 run("IO/foreign-machine-invalid",[&]{auto load=tr::load(file,"different raw identity");check(load.state==tr::LoadState::Invalid&&!load.record,"foreign machine cannot be adopted");});
 run("IO/corrupt-and-oversize",[&]{for(const auto&s:std::vector<std::string>{"",baseline.substr(0,20),std::string(tr::maxRecordBytes+1,'x')}){raw(file,s);auto load=tr::load(file,empty().machineBinding);check(load.state==tr::LoadState::Invalid&&!load.record,"bad file cannot become tombstone");}});
 run("IO/directories-and-paths",[&]{auto load=tr::load(directory,empty().machineBinding);check(load.state==tr::LoadState::Invalid&&!load.record,"directory rejected as Invalid record");check(!tr::store(directory,empty()).committed(),"directory never replaced");for(const auto&name:std::vector<std::wstring>{L"relative.json",directory+L"\\..\\record.json",directory+L"\\record.json:alternate",directory+L"\\NUL.json"}){auto bad=tr::load(name,empty().machineBinding);check(bad.state==tr::LoadState::Invalid&&!bad.record,"unsafe explicit path rejected");check(!tr::store(name,empty()).committed(),"unsafe path not written");}});
 run("IO/hardlink-rejected",[&]{raw(file,baseline);auto link=directory+L"\\hardlink.json";check(CreateHardLinkW(link.c_str(),file.c_str(),nullptr),"private hardlink fixture created");auto load=tr::load(link,empty().machineBinding);check(load.state==tr::LoadState::Invalid&&!load.record,"multiply linked record rejected");check(!tr::store(link,empty()).committed(),"hardlink not replaced");check(DeleteFileW(link.c_str()),"only private fixture hardlink removed");});
 run("IO/reparse-metadata-rejected",[&]{ioTest::reset();check(tr::store(file,one()).committed(),"baseline");for(const auto fault:{ioTest::Fault::ReparseLeaf,ioTest::Fault::ReparseParent}){ioTest::reset(fault);auto loaded=tr::load(file,empty().machineBinding);check(loaded.state==tr::LoadState::Invalid&&!loaded.record,"observed reparse metadata refuses record");auto saved=tr::store(file,empty());check(saved.state==tr::StoreState::Invalid,"observed reparse metadata refuses replacement");check(ioTest::writes==0&&ioTest::renames==0,"unsafe observation never reaches write");}ioTest::reset();});
 run("IO/invalid-candidate-zero-mutation",[&]{ioTest::reset();auto invalid=one();invalid.obligations[0].origin.parent.pid=0;check(!tr::store(file,invalid).committed(),"invalid candidate refused");check(ioTest::writes==0&&ioTest::flushes==0&&ioTest::renames==0,"invalid candidate invokes no file mutation");});
 run("IO/no-orphan-temporary-files",[&]{std::size_t count=0;for(const auto&item:std::filesystem::directory_iterator(directory))if(item.path().filename().wstring().find(L".tmp.")!=std::wstring::npos)++count;check(count==0,"all failed/publication temporary files cleaned");});
 run("IO/independent-process-record",[&]{auto r=one();tr::Record c;check(tr::reservePath(r,r.revision,origin(2,"elite"),pathB,proof,c),"two exact old origins");check(tr::store(directory+L"\\cross-process.json",c).committed(),"cross-process test record published");});

 // Additive 103 coverage. All original 101 scenarios and checks above remain intact.
 run("target/default-false",[]{tr::Obligation g;check(!g.targetMayExist,"new in-memory field defaults false");check(!one().obligations[0].targetMayExist,"path-only producer does not invent target witness");});
 for(const std::string persona:{"steamdeck","dualsense-edge","elite"})run("target/no-source-lost-ACK-"+persona,[&]{
  auto r=empty();const auto before=encoded(r);const auto o=noSourceOrigin(103,persona);tr::Record c;
  check(tr::reserveTargetCreation(r,r.revision,o,{true,true},c),"authenticated dispatched PREPARE reserves possible target");
  check(c.revision==r.revision+1&&c.obligations.size()==1,"one new exact origin increments revision");
  check(c.obligations[0].targetMayExist&&c.obligations[0].paths.empty()&&!c.obligations[0].acceptedPsPin,"missing ACK needs neither paths nor pin");
  check(tr::sameOrigin(c.obligations[0].origin,o)&&c.obligations[0].origin.physicalRoot.empty(),"full owner and tuple retained without physical source");
  const auto wire=encoded(c);tr::Record decoded;check(tr::decode(wire,decoded)&&encoded(decoded)==wire,"lost ACK target duty roundtrips");
  check(J::parse(wire)["obligations"][0]["targetMayExist"].is_boolean()&&wire.find("complete")==std::string::npos,"wire boolean is historical duty without admission flag");
  check(encoded(r)==before,"reservation leaves caller current unchanged");
 });
 run("target/wrong-proof-preserves-output",[]{
  for(const auto& p:std::vector<tr::TargetCreationProof>{{},{false,true},{true,false}})for(const auto& r:std::vector<tr::Record>{empty(),targetWitness()}){
   auto c=one();const auto before=encoded(c);std::string error;
   check(!tr::reserveTargetCreation(r,r.revision,noSourceOrigin(),p,c,&error),"both producer proof facts required even on duplicate witness");
   check(encoded(c)==before&&!error.empty(),"wrong proof preserves output and reports failure");
  }
 });
 run("target/invalid-exact-origin",[]{
  const std::vector<std::function<void(tr::Origin&)>> corrupt={
   [](auto&o){o.transactionId.clear();},[](auto&o){o.parent.pid=0;},[](auto&o){o.parent.creationTime="0";},
   [](auto&o){o.host.pid=0;},[](auto&o){o.host.creationTime="0001";},[](auto&o){o.host.creationTime="18446744073709551616";},
   [](auto&o){o.hostInstanceId.clear();},[](auto&o){o.runId.clear();},[](auto&o){o.epoch=0;},[](auto&o){o.targetId.clear();},
   [](auto&o){o.persona="xbox360";},[](auto&o){o.physicalRoot="USB\\..\\BAD";}};
  const auto r=empty();for(const auto& change:corrupt){auto o=noSourceOrigin();change(o);auto c=one();const auto before=encoded(c);
   check(!tr::reserveTargetCreation(r,r.revision,o,{true,true},c),"target-only still requires valid complete origin");check(encoded(c)==before,"bad origin never changes candidate");}
 });
 run("target/idempotence-stale-overflow-alias",[]{
  auto r=targetWitness(),c=one();const auto before=encoded(r);check(tr::reserveTargetCreation(r,r.revision,noSourceOrigin(),{true,true},c),"same witness idempotent");check(encoded(c)==before,"idempotence retains revision");
  c=one();const auto sentinel=encoded(c);check(!tr::reserveTargetCreation(r,r.revision-1,noSourceOrigin(),{true,true},c),"stale witness update refused");check(encoded(c)==sentinel,"stale leaves output intact");
  r.revision=(std::numeric_limits<std::uint64_t>::max)();check(tr::reserveTargetCreation(r,r.revision,noSourceOrigin(),{true,true},r),"idempotent witness safe at maximum revision");check(r.revision==(std::numeric_limits<std::uint64_t>::max)(),"idempotence cannot wrap");
  auto fresh=empty();fresh.revision=r.revision;check(!tr::reserveTargetCreation(fresh,fresh.revision,noSourceOrigin(),{true,true},c),"new witness cannot wrap revision");check(encoded(c)==sentinel,"overflow preserves output");
  fresh=empty();check(tr::reserveTargetCreation(fresh,fresh.revision,noSourceOrigin(),{true,true},fresh),"supported aliased output");check(fresh.obligations.size()==1&&fresh.obligations[0].targetMayExist,"aliased update retains duty");
 });
 run("target/immutable-transaction-group",[]{
  const std::vector<std::function<void(tr::Origin&)>> change={
   [](auto&o){++o.parent.pid;},[](auto&o){o.parent.creationTime="134368044937265941";},[](auto&o){++o.host.pid;},[](auto&o){o.host.creationTime="134368044937266001";},
   [](auto&o){o.hostInstanceId="different-host";},[](auto&o){o.runId="different-run";},[](auto&o){++o.epoch;},[](auto&o){o.targetId="different-target";},[](auto&o){o.persona="elite";},[](auto&o){o.physicalRoot=origin().physicalRoot;}};
  const auto r=targetWitness();for(const auto& mutateOrigin:change){auto o=noSourceOrigin();mutateOrigin(o);auto c=one();const auto before=encoded(c);
   check(!tr::reserveTargetCreation(r,r.revision,o,{true,true},c),"same transaction cannot rewrite any origin member");check(encoded(c)==before,"immutable grouping leaves candidate intact");}
  auto o=noSourceOrigin();o.physicalRoot=origin().physicalRoot;auto c=one();check(!tr::reservePath(r,r.revision,o,pathA,proof,c),"cannot upgrade source-free witness into a different physical origin");
  check(!tr::attachAcceptedPsPin(r,r.revision,o,psPin(),c),"pin cannot rewrite source-free origin either");
  auto inherited=r;inherited.obligations[0].origin=o;const auto before=encoded(c);check(!tr::mergeForCarry(r,r.revision,inherited,c),"carry cannot replace empty physical root with another origin");check(encoded(c)==before,"carry rewrite rejected atomically");
 });
 run("target/last-path-retains-duty",[]{
  const auto original=one();tr::Record target;check(tr::reserveTargetCreation(original,original.revision,origin(),{true,true},target),"target witness adds to same physical origin");
  check(target.obligations[0].paths.size()==1&&target.obligations[0].targetMayExist&&!target.obligations[0].acceptedPsPin,"target witness retains original path without fabricating pin");
  const auto before=encoded(target);tr::Record c;check(tr::consumePath(target,target.revision,origin(),pathA,c),"caller-proved last path consumed");
  check(c.obligations.size()==1&&c.obligations[0].paths.empty()&&c.obligations[0].targetMayExist,"last path cannot erase possible target");
  check(c.revision==target.revision+1&&encoded(target)==before,"path consumption changes only candidate revision");tr::Record decoded;check(tr::decode(encoded(c),decoded)&&decoded.obligations[0].targetMayExist,"empty path group remains serializable");
 });
 run("target/path-requires-physical-root-and-proof",[]{
  for(const auto& r:std::vector<tr::Record>{empty(),targetWitness()}){auto c=one();const auto before=encoded(c);check(!tr::reservePath(r,r.revision,noSourceOrigin(),pathA,proof,c),"empty-root witness cannot claim any path despite all booleans");check(encoded(c)==before,"path refusal preserves candidate");}
  auto r=targetWitness();r.obligations[0].paths.push_back({pathA,tr::DispatchStage::Reserved});check(!tr::validRecord(r),"direct empty-root path construction also rejected");std::string bytes="sentinel";check(!tr::encode(r,bytes)&&bytes=="sentinel","encoder refuses source-free path debt");
  auto c=empty();check(tr::reserveTargetCreation(c,c.revision,origin(),{true,true},r),"target witness with actual physical source valid");
  for(const auto& p:std::vector<tr::ReservationProof>{{false,true,true},{true,false,true},{true,true,false}}){auto next=one();const auto before=encoded(next);check(!tr::reservePath(r,r.revision,origin(),pathA,p,next),"target witness never replaces path-specific ancestry or prior-hidden proof");check(encoded(next)==before,"bad path proof preserves output");}
  check(tr::reservePath(r,r.revision,origin(),pathA,proof,c),"same nonempty root can subsequently reserve a real path");check(c.obligations[0].targetMayExist&&c.obligations[0].paths.size()==1,"path addition retains target witness");
 });
 run("target/PS-no-source-pin-only",[]{
  auto r=empty();tr::Record c;const auto o=noSourceOrigin();check(tr::attachAcceptedPsPin(r,r.revision,o,psPin(),c),"accepted caller PS pin can create target-only origin");
  check(c.obligations.size()==1&&c.obligations[0].paths.empty()&&!c.obligations[0].targetMayExist&&c.obligations[0].acceptedPsPin.has_value(),"pin-only debt does not invent PREPARE witness");
  tr::Record decoded;check(tr::decode(encoded(c),decoded)&&tr::sameOrigin(decoded.obligations[0].origin,o),"source-free pin-only record roundtrips");
  auto wire=J::parse(encoded(c));wire["obligations"][0]["acceptedPsPin"]["complete"]=true;const auto before=encoded(decoded);check(!tr::decode(wire.dump(),decoded)&&encoded(decoded)==before,"source-free pin never accepts raw complete authority");
  auto target=targetWitness();check(tr::attachAcceptedPsPin(target,target.revision,o,psPin(),c),"accepted pin attaches to existing target witness");check(c.obligations[0].targetMayExist&&c.obligations[0].acceptedPsPin.has_value(),"pin addition retains possible target");
  check(!tr::reservePath(c,c.revision,o,pathA,proof,decoded),"pin also cannot replace physical source proof");
 });
 run("target/carry-OR-both-directions",[]{
  const auto without=one();tr::Record with;check(tr::reserveTargetCreation(without,without.revision,origin(),{true,true},with),"build target plus path origin");
  tr::Record c;check(tr::mergeForCarry(without,without.revision,with,c),"carry upgrades false to true");check(c.obligations[0].targetMayExist&&c.revision==without.revision+1&&c.obligations[0].paths.size()==1,"OR retains path and bumps once");
  const auto before=encoded(with);check(tr::mergeForCarry(with,with.revision,without,c),"carry old false into true");check(encoded(c)==before,"carry cannot downgrade true or needlessly bump");
  check(tr::mergeForCarry(with,with.revision,with,c),"duplicate carry accepted");check(encoded(c)==before,"duplicate true witness idempotent");
  const auto target=targetWitness();auto e=empty();check(tr::mergeForCarry(e,e.revision,target,c),"new source-free group carried intact");check(c.obligations[0].targetMayExist&&tr::sameOrigin(c.obligations[0].origin,noSourceOrigin()),"carry keeps original historical tuple");
  tr::Record unioned;check(tr::mergeForCarry(c,c.revision,with,unioned),"source-free and physical origins union");check(unioned.obligations.size()==2&&unioned.obligations[0].targetMayExist&&unioned.obligations[1].targetMayExist,"union loses neither target duty");
 });
 run("target/consume-exact-stale-caller-proof",[]{
  const auto r=targetWitness();auto c=one();const auto sentinel=encoded(c),before=encoded(r);check(!tr::consumeOrigin(r,r.revision-1,noSourceOrigin(),c),"stale consumer cannot clear possible target");check(encoded(c)==sentinel,"stale consume preserves output");
  const std::vector<std::function<void(tr::Origin&)>> changes={[](auto&o){o.transactionId="tx-other";},[](auto&o){++o.parent.pid;},[](auto&o){o.parent.creationTime="1";},[](auto&o){++o.host.pid;},[](auto&o){o.host.creationTime="1";},[](auto&o){o.hostInstanceId="other-host";},[](auto&o){o.runId="other-run";},[](auto&o){++o.epoch;},[](auto&o){o.targetId="other-target";},[](auto&o){o.persona="elite";},[](auto&o){o.physicalRoot=origin().physicalRoot;}};
  for(const auto& change:changes){auto o=noSourceOrigin();change(o);check(!tr::consumeOrigin(r,r.revision,o,c),"every exact origin component protects duty");check(encoded(c)==sentinel,"wrong-origin consume preserves output");}
  check(!tr::consumePath(r,r.revision,noSourceOrigin(),pathA,c),"no path operation can clear target-only group");check(encoded(c)==sentinel,"missing path retains output");
  check(tr::consumeOrigin(r,r.revision,noSourceOrigin(),c),"caller after external absence proof can consume exact origin");check(c.obligations.empty()&&c.revision==r.revision+1,"exact consume yields explicit tombstone");check(encoded(r)==before,"caller must separately publish candidate");
 });
 run("target/legacy-101-wire-default-false",[]{
  const std::string wire=R"legacy101({"machineBinding":"RAW|MS-1T52|微星|ClawA2VM","obligations":[{"acceptedPsPin":null,"origin":{"epoch":1,"host":{"creationTime":"134368044937266000","pid":201},"hostInstanceId":"host-1","parent":{"creationTime":"134368044937265940","pid":101},"persona":"dualsense-edge","physicalRoot":"USB\\VID_0DB0&PID_1901\\MSI","runId":"run-1","targetId":"target-1","transactionId":"tx-1"},"paths":[{"instanceId":"HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL_A","stage":"pre-dispatch-reservation"}]},{"acceptedPsPin":null,"origin":{"epoch":2,"host":{"creationTime":"134368044937266000","pid":202},"hostInstanceId":"host-2","parent":{"creationTime":"134368044937265940","pid":102},"persona":"elite","physicalRoot":"USB\\VID_0DB0&PID_1901\\MSI","runId":"run-2","targetId":"target-2","transactionId":"tx-2"},"paths":[{"instanceId":"HID\\VID_0B05&PID_1B4C&IG_00\\PHYSICAL_B","stage":"pre-dispatch-reservation"}]}],"revision":3,"schemaVersion":1})legacy101";tr::Record c;
  check(wire.find("targetMayExist")==std::string::npos&&tr::decode(wire,c),"immutable actual 101 file decodes without new field");
  check(c.obligations.size()==2&&!c.obligations[0].targetMayExist&&!c.obligations[1].targetMayExist,"each legacy group defaults false");
  const auto upgraded=encoded(c);const auto j=J::parse(upgraded);check(j["obligations"][0]["targetMayExist"]==false&&j["obligations"][1]["targetMayExist"]==false,"new encoder explicitly emits false");
  tr::Record reloaded;check(tr::decode(upgraded,reloaded)&&encoded(reloaded)==upgraded,"upgraded old record roundtrips");
  auto pinOnly=empty();check(tr::attachAcceptedPsPin(pinOnly,pinOnly.revision,noSourceOrigin(),psPin(),c),"pin-only compatibility setup");auto pinWire=J::parse(encoded(c));pinWire["obligations"][0].erase("targetMayExist");check(tr::decode(pinWire.dump(),reloaded)&&!reloaded.obligations[0].targetMayExist&&reloaded.obligations[0].acceptedPsPin.has_value(),"omitted field stays false when pin independently justifies target-only group");
 });
 const auto targetJson=J::parse(encoded(targetWitness()));
 for(const auto& [name,value]:std::vector<std::pair<std::string,J>>{{"null",nullptr},{"zero",0},{"one",1},{"float",1.0},{"string","true"},{"array",J::array()},{"object",J::object()}}){auto bad=targetJson;bad["obligations"][0]["targetMayExist"]=value;reject("target-bool-"+name,bad.dump());}
 {auto bad=targetJson;bad["obligations"][0]["targetMayExist"]=false;reject("target-empty-root-without-duty",bad.dump());bad["obligations"][0].erase("targetMayExist");reject("target-omitted-witness-without-duty",bad.dump());}
 {auto bad=targetJson;bad["obligations"][0]["paths"]=J::array({{{"instanceId",pathA},{"stage","pre-dispatch-reservation"}}});reject("target-empty-root-path",bad.dump());bad["obligations"][0]["acceptedPsPin"]=pinJson["obligations"][0]["acceptedPsPin"];reject("target-empty-root-path-even-with-pin",bad.dump());}
 {auto bad=targetJson;bad["obligations"][0]["currentOutputAccepted"]=true;reject("target-unknown-group-key",bad.dump());bad=targetJson;bad["obligations"][0]["origin"]["targetMayExist"]=true;reject("target-witness-in-origin",bad.dump());}
 {auto bad=targetJson;bad["obligations"][0]["origin"]["physicalRoot"]="USB\\..\\BAD";reject("target-malformed-nonempty-root",bad.dump());}
 {auto bytes=targetJson.dump();auto pos=bytes.find("\"targetMayExist\":");if(pos==std::string::npos)throw std::runtime_error("target duplicate anchor missing");bytes.insert(pos,"\"targetMayExist\":false,");reject("target-duplicate-bool-key",bytes);}
 {auto bytes=targetJson.dump();auto pos=bytes.find("\"targetMayExist\":");bytes.insert(pos,"\"targetMay\\u0045xist\":false,");reject("target-duplicate-escaped-bool-key",bytes);}
 run("target/origin-limit-and-union-overflow",[]{
  auto r=empty();for(unsigned i=1;i<=tr::maxOrigins;++i){tr::Record c;check(tr::reserveTargetCreation(r,r.revision,noSourceOrigin(103+i),{true,true},c),"bounded source-free origin added");r=std::move(c);}
  auto c=one();const auto before=encoded(c);check(!tr::reserveTargetCreation(r,r.revision,noSourceOrigin(200),{true,true},c),"source-free target does not bypass origin cap");check(encoded(c)==before,"origin limit preserves output");
  auto without=one();without.revision=(std::numeric_limits<std::uint64_t>::max)();auto with=without;with.obligations[0].targetMayExist=true;check(!tr::mergeForCarry(without,without.revision,with,c),"OR upgrade cannot wrap revision");check(encoded(c)==before,"carry overflow atomic");
 });
 run("IO/target-witness-lost-ACK-roundtrip",[&]{
  ioTest::reset();const auto r=targetWitness();const auto targetFile=directory+L"\\target-witness-cross-process.json";check(tr::store(targetFile,r).committed(),"source-free no-pin target duty actually committed");
  check(ioTest::writes==1&&ioTest::flushes==1&&ioTest::renames==1,"target witness uses original atomic store");auto loaded=tr::load(targetFile,r.machineBinding);check(loaded.state==tr::LoadState::Valid&&loaded.record&&encoded(*loaded.record)==encoded(r),"actual disk load retains exact lost-ACK duty");
 });

 // Task128: immutable CLI writer provenance is data only, not cleanup permission.

 run("native-set/complete-atomic-candidate",[]{
  const auto r=empty();const auto prior=encoded(r);tr::Record candidate;
  check(tr::reserveNativeOwnedSet(r,r.revision,nativeOrigin(),nativePaths(16),nativeSetProof,candidate),"complete 16-member existing native hidden set accepted");
  check(candidate.schema==1&&candidate.revision==r.revision+1&&candidate.obligations.size()==1&&candidate.obligations[0].paths.size()==16,"entire set changes one revision under one origin without schema change");
  check(std::all_of(candidate.obligations[0].paths.begin(),candidate.obligations[0].paths.end(),[](const auto& path){return path.stage==tr::DispatchStage::Reserved;}),"new native set members are Reserved only, never fabricated Issued");
  check(encoded(r)==prior,"pure transform leaves original caller record unchanged");
  tr::Record decoded;check(tr::decode(encoded(candidate),decoded)&&encoded(decoded)==encoded(candidate),"original schema cold readers roundtrip complete native set");
 });
 run("native-set/idempotent-same-origin",[]{
  tr::Record current,candidate;check(tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),nativePaths(2),nativeSetProof,current),"seed complete native set");
  const auto prior=encoded(current);auto reordered=nativePaths(2);std::reverse(reordered.begin(),reordered.end());
  for(auto& path:reordered)for(auto& c:path)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
  check(tr::reserveNativeOwnedSet(current,current.revision,nativeOrigin(),reordered,nativeSetProof,candidate),"same-origin set is case-insensitive and order independent");
  check(candidate.revision==current.revision&&encoded(candidate)==prior,"idempotent batch preserves original spelling, order and revision");
 });
 run("native-set/requires-distinct-typed-proof",[]{
  for(const auto p:std::vector<tr::NativeOwnedSetProof>{{false,true,true},{true,false,true},{true,true,false},{false,false,false}}){
   auto output=one();const auto prior=encoded(output);
   check(!tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),nativePaths(2),p,output),"every original-owned/current-hidden/ancestry proof bit mandatory");
   check(encoded(output)==prior,"failed batch proof cannot publish any candidate prefix");
  }
  auto output=one();const auto prior=encoded(output);
  check(!tr::reservePath(empty(),1,nativeOrigin(),pathA,{true,true,false},output)&&encoded(output)==prior,"ordinary scalar prior-hidden proof remains mandatory");
 });
 run("native-set/empty-and-over-16-rejected",[]{
  for(const auto count:{0U,17U}){auto output=one();const auto prior=encoded(output);
   check(!tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),nativePaths(count),nativeSetProof,output),"native set bounded to 1 through 16 members");
   check(encoded(output)==prior,"invalid batch count leaves output untouched");
  }
 });
 run("native-set/duplicate-casefold-rejected",[]{
  auto paths=nativePaths(2);auto duplicate=paths.front();for(auto& c:duplicate)if(c>='A'&&c<='Z')c=char(c-'A'+'a');paths.push_back(duplicate);
  auto output=one();const auto prior=encoded(output);
  check(!tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),paths,nativeSetProof,output),"same exact path with different case is a duplicate, not a second debt");
  check(encoded(output)==prior,"duplicate tail refuses complete batch");
 });
 run("native-set/invalid-tail-refuses-whole-set",[]{
  for(const auto& tail:std::vector<std::string>{"", "HID\\..\\TAIL", std::string(513,'X')}){
   auto paths=nativePaths(2);paths.push_back(tail);auto output=one();const auto prior=encoded(output);
   check(!tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),paths,nativeSetProof,output),"illegal tail rejects all valid leading members");
   check(encoded(output)==prior,"no partial candidate escapes invalid tail");
  }
 });
 run("native-set/cross-origin-foreign-tail-rejected",[]{
  const auto current=one();auto paths=nativePaths(2);paths.push_back(pathA);auto output=empty();const auto prior=encoded(output);
  check(!tr::reserveNativeOwnedSet(current,current.revision,nativeOrigin(),paths,nativeSetProof,output),"foreign conflicting member rejects whole native batch");
  check(encoded(output)==prior&&current.obligations.size()==1,"no leading native path claimed and foreign origin preserved");
 });
 run("native-set/origin-rewrite-and-stale-rejected",[]{
  tr::Record current;check(tr::reserveNativeOwnedSet(empty(),1,nativeOrigin(),nativePaths(2),nativeSetProof,current),"seed original native origin");
  for(unsigned change=0;change<3;++change){auto owner=nativeOrigin();if(change==0)++owner.host.pid;else if(change==1)owner.physicalRoot="USB\\VID_1111&PID_2222\\OTHER";else owner.cliWriterProtocol=0;
   auto output=one();const auto prior=encoded(output);
   check(!tr::reserveNativeOwnedSet(current,current.revision,owner,nativePaths(3),nativeSetProof,output)&&encoded(output)==prior,"same transaction cannot rewrite Host/root/protocol in a batch");
  }
  auto output=one();const auto prior=encoded(output);
  check(!tr::reserveNativeOwnedSet(current,current.revision-1,nativeOrigin(),nativePaths(3),nativeSetProof,output)&&encoded(output)==prior,"stale batch cannot bypass expected revision");
 });
 run("native-set/append-preserves-issued-and-target-duty",[]{
  auto owner=nativeOrigin();tr::Record current,next;const auto paths=nativePaths(3);
  check(tr::reserveTargetCreation(empty(),1,owner,{true,true},current),"original target provenance created");
  check(tr::reservePath(current,current.revision,owner,paths[0],proof,next),"original scalar path created");current=next;
  check(tr::markMayHaveIssued(current,current.revision,owner,paths[0],next),"existing scalar-issued evidence distinct");current=next;
  check(tr::reserveNativeOwnedSet(current,current.revision,owner,paths,nativeSetProof,next),"batch adds missing native members once");
  check(next.revision==current.revision+1&&next.obligations[0].targetMayExist&&next.obligations[0].paths.size()==3,"batch preserves independent target witness");
  check(next.obligations[0].paths[0].stage==tr::DispatchStage::MayHaveIssued&&next.obligations[0].paths[1].stage==tr::DispatchStage::Reserved&&next.obligations[0].paths[2].stage==tr::DispatchStage::Reserved,"existing Issued preserved and only new members Reserved");
 });
 run("native-set/limits-origin32-global128-and-origins16",[]{
  auto current=empty();auto owner=nativeOrigin();current.obligations.push_back({owner,{},{}});
  for(unsigned i=0;i<31;++i)current.obligations[0].paths.push_back({"HID\\VID_1234&PID_5678\\OLD_"+std::to_string(i),tr::DispatchStage::Reserved});
  check(tr::validRecord(current),"31-path original group valid");auto output=one();const auto prior=encoded(output);
  check(!tr::reserveNativeOwnedSet(current,current.revision,owner,nativePaths(2),nativeSetProof,output)&&encoded(output)==prior,"native set cannot exceed original max32 per origin");
  current=empty();for(unsigned i=1;i<=4;++i){tr::Obligation group;group.origin=origin(i);for(unsigned j=0;j<32;++j)group.paths.push_back({"HID\\VID_1234&PID_5678\\OLD_"+std::to_string(i)+"_"+std::to_string(j),tr::DispatchStage::Reserved});current.obligations.push_back(std::move(group));}
  check(tr::validRecord(current),"128-path source record valid");
  check(!tr::reserveNativeOwnedSet(current,current.revision,owner,nativePaths(1),nativeSetProof,output)&&encoded(output)==prior,"global max128 unchanged");
  current=empty();for(unsigned i=1;i<=16;++i){tr::Obligation group;group.origin=origin(i);group.paths.push_back({"HID\\VID_1234&PID_5678\\ORIGIN_"+std::to_string(i),tr::DispatchStage::Reserved});current.obligations.push_back(std::move(group));}
  check(tr::validRecord(current),"16-origin source record valid");
  check(!tr::reserveNativeOwnedSet(current,current.revision,owner,nativePaths(1),nativeSetProof,output)&&encoded(output)==prior,"max16 origins unchanged");
 });
 run("native-set/revision-overflow-keeps-output",[]{
  auto current=empty();current.revision=(std::numeric_limits<std::uint64_t>::max)();auto output=one();const auto prior=encoded(output);
  check(!tr::reserveNativeOwnedSet(current,current.revision,nativeOrigin(),nativePaths(2),nativeSetProof,output)&&encoded(output)==prior,"revision overflow cannot publish partial native set");
 });
 run("IO/native-set-session-one-store-and-idempotent",[&]{
  ioTest::reset();const auto nativeFile=directory+L"\\native-owned-set-cross-process.json";
  {auto session=tr::Session::open(nativeFile,empty().machineBinding);check(session.ready(),"actual Session opens missing private native ledger");
   ioTest::reset();check(session.reserveNativeOwnedSet(session.record()->revision,nativeOrigin(),nativePaths(2),nativeSetProof),"one-update Session persists whole native set");
   check(ioTest::writes==1&&ioTest::flushes==1&&ioTest::renames==1,"whole set uses exactly one Write/Flush/atomic rename, not a per-path loop");
   const auto prior=readRaw(nativeFile);ioTest::reset();
   check(session.reserveNativeOwnedSet(session.record()->revision,nativeOrigin(),nativePaths(2),nativeSetProof),"same complete set is idempotent in actual Session");
   check(ioTest::writes==0&&ioTest::flushes==0&&ioTest::renames==0&&readRaw(nativeFile)==prior,"idempotent call performs no durable store");
  }
  {auto reopened=tr::Session::open(nativeFile,empty().machineBinding);
   check(reopened.ready()&&reopened.openedFrom()==tr::LoadState::Valid&&reopened.record()->obligations.size()==1&&reopened.record()->obligations[0].paths.size()==2,"new actual Session reads both debts without any hide or test-side tail repair");
  }
 });
 for(const auto fault:{ioTest::Fault::Write,ioTest::Fault::ShortWrite,ioTest::Fault::Flush,ioTest::Fault::Rename}){
  const std::string label=fault==ioTest::Fault::Write?"write":fault==ioTest::Fault::ShortWrite?"short-write":fault==ioTest::Fault::Flush?"flush":"rename";
  run("IO/native-set-failure-"+label,[&,fault,label]{
   ioTest::reset();const auto nativeFile=directory+L"\\native-set-failed-"+std::wstring(label.begin(),label.end())+L".json";
   const auto initial=empty();check(tr::store(nativeFile,initial).committed(),"real prior ledger exists before fault");const auto prior=readRaw(nativeFile);
   {auto session=tr::Session::open(nativeFile,empty().machineBinding);check(session.ready(),"actual Session opens prior valid ledger");
    ioTest::reset(fault);check(!session.reserveNativeOwnedSet(session.record()->revision,nativeOrigin(),nativePaths(2),nativeSetProof),"actual batch store failure cannot publish half a set");
    check(session.record()->obligations.empty()&&session.record()->revision==initial.revision,"failed batch preserves Session data/revision");
    ioTest::reset();check(readRaw(nativeFile)==prior,"prior disk bytes survive failed whole-set publish");
   }
   check(tr::load(nativeFile,initial.machineBinding).record->obligations.empty(),"new reader sees no half-batch after failed atomic publish");
  });
 }

 run("writer-protocol/legacy-missing-and-explicit-zero",[]{
  const auto legacy=encoded(one());auto wire=J::parse(legacy);tr::Record decoded;
  check(!wire["obligations"][0]["origin"].contains("cliWriterProtocol"),"unknown provenance keeps legacy encoding without new field");
  check(tr::decode(legacy,decoded)&&decoded.obligations[0].origin.cliWriterProtocol==0,"missing legacy protocol decodes as unknown zero");
  check(encoded(decoded)==legacy,"legacy record remains byte-compatible without upgrading provenance");
  wire["obligations"][0]["origin"]["cliWriterProtocol"]=0;
  check(tr::decode(wire.dump(),decoded)&&decoded.obligations[0].origin.cliWriterProtocol==0,"explicit unsigned zero is accepted as unknown");
  check(encoded(decoded)==legacy,"explicit zero normalizes to legacy omitted-field encoding");
  check(tr::decode(encoded(targetWitness()),decoded)&&decoded.obligations[0].origin.cliWriterProtocol==0,"source-free legacy target also stays unknown");
 });
 run("writer-protocol/one-roundtrip-and-carry",[]{
  auto owner=origin();owner.cliWriterProtocol=1;auto r=empty();tr::Record current;
  check(tr::reservePath(r,r.revision,owner,pathA,proof,current),"new producer may reserve an explicit protocol-one origin");
  const auto bytes=encoded(current);const auto wire=J::parse(bytes);tr::Record decoded;
  check(wire["obligations"][0]["origin"]["cliWriterProtocol"].is_number_unsigned()&&wire["obligations"][0]["origin"]["cliWriterProtocol"]==1,"protocol one is emitted as integer provenance");
  check(tr::decode(bytes,decoded)&&decoded.obligations[0].origin.cliWriterProtocol==1&&tr::sameOrigin(decoded.obligations[0].origin,owner),"protocol one roundtrips with exact owner");
  check(encoded(decoded)==bytes,"protocol one serialization is stable");
  auto legacy=origin(2,"elite");tr::Record mixed;
  check(tr::reservePath(current,current.revision,legacy,pathB,proof,mixed),"different transaction may retain independent unknown provenance");
  tr::Record carried;check(tr::mergeForCarry(r,r.revision,mixed,carried),"mixed provenance carries without upgrading either origin");
  check(carried.obligations.size()==2&&carried.obligations[0].origin.cliWriterProtocol==1&&carried.obligations[1].origin.cliWriterProtocol==0,"carry preserves each immutable protocol value");
  auto sourceFree=origin(128);sourceFree.physicalRoot.clear();sourceFree.cliWriterProtocol=1;
  check(tr::reserveTargetCreation(r,r.revision,sourceFree,{true,true},decoded),"source-free target supports explicit new producer protocol");
  check(tr::decode(encoded(decoded),current)&&current.obligations[0].origin.cliWriterProtocol==1&&current.obligations[0].targetMayExist,"target-only duty keeps one without creating physical debt");
 });
 for(const unsigned originalProtocol:{0U,1U})run("writer-protocol/same-tx-rewrite-"+std::to_string(originalProtocol),[&]{
  auto owner=origin();owner.cliWriterProtocol=originalProtocol;auto r=empty();tr::Record current;
  if(!tr::reservePath(r,r.revision,owner,pathA,proof,current))throw std::runtime_error("protocol rewrite setup failed");
  auto changed=owner;changed.cliWriterProtocol=1-originalProtocol;
  check(!tr::sameOrigin(owner,changed),"sameOrigin includes immutable writer protocol");
  const auto before=encoded(current);auto output=current;
  check(!tr::reservePath(current,current.revision,changed,pathA,proof,output),"same path cannot silently change origin protocol");
  check(!tr::reservePath(current,current.revision,changed,pathB,proof,output),"new path in same transaction cannot rewrite protocol");
  check(!tr::reserveTargetCreation(current,current.revision,changed,{true,true},output),"target reserve rejects same transaction protocol rewrite");
  check(!tr::attachAcceptedPsPin(current,current.revision,changed,psPin(),output),"pin attach cannot rewrite origin protocol");
  check(!tr::markMayHaveIssued(current,current.revision,changed,pathA,output),"dispatch must use exact original protocol");
  check(!tr::consumePath(current,current.revision,changed,pathA,output)&&!tr::consumeOrigin(current,current.revision,changed,output),"protocol mismatch cannot consume old duty");
  auto inherited=current;inherited.obligations[0].origin=changed;
  check(!tr::mergeForCarry(current,current.revision,inherited,output),"same transaction carry rejects protocol rewrite");
  check(encoded(current)==before&&encoded(output)==before,"every rejected rewrite preserves source and output candidate");
 });
 run("writer-protocol/invalid-memory-provenance",[]{
  auto bad=origin();bad.cliWriterProtocol=2;
  check(!tr::detail::origin(bad),"unsupported protocol rejected before any reservation");
  auto r=one();r.obligations[0].origin=bad;std::string bytes="unchanged-output";
  check(!tr::validRecord(r)&&!tr::encode(r,bytes)&&bytes=="unchanged-output","invalid in-memory protocol cannot be persisted or alter output");
 });
 for(const auto& [name,value]:std::vector<std::pair<std::string,J>>{{"negative",-1},{"future",2},{"overflow",18446744073709551615ULL},{"float-zero",0.0},{"float-one",1.0},{"true",true},{"false",false},{"null",nullptr},{"string","1"},{"array",J::array()},{"object",J::object()}}){
  auto wire=J::parse(encoded(one()));wire["obligations"][0]["origin"]["cliWriterProtocol"]=value;reject("writer-protocol-"+name,wire.dump());
 }
 for(const std::string key:{"cliWriterProtocol","cliWriter\\u0050rotocol"}){
  auto wire=J::parse(encoded(one()));wire["obligations"][0]["origin"]["cliWriterProtocol"]=1;auto bytes=wire.dump();const auto pos=bytes.find("\"cliWriterProtocol\":");
  if(pos==std::string::npos)throw std::runtime_error("protocol duplicate anchor missing");
  bytes.insert(pos,"\""+key+"\":0,");reject("writer-protocol-duplicate-"+key,bytes);
 }
 run("IO/writer-protocol-one-atomic-persist",[&]{
  auto owner=origin(128);owner.cliWriterProtocol=1;auto r=empty();tr::Record candidate;
  if(!tr::reservePath(r,r.revision,owner,pathA,proof,candidate))throw std::runtime_error("protocol persist setup failed");
  const auto destination=directory+L"\\writer-protocol-one.json";ioTest::reset();
  check(tr::store(destination,candidate).committed(),"new protocol actually commits to private regular file");
  check(ioTest::writes==1&&ioTest::flushes==1&&ioTest::renames==1,"new field uses unchanged full-write flush atomic replace store");
  auto loaded=tr::load(destination,candidate.machineBinding);
  check(loaded.state==tr::LoadState::Valid&&loaded.record&&loaded.record->obligations[0].origin.cliWriterProtocol==1&&encoded(*loaded.record)==encoded(candidate),"actual file reload retains exact immutable protocol one");
 });

 J result={{"pass",failures.empty()},{"cases",cases},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures},{"realPrivateFileIo",true},{"deviceApiCalls",0},{"productMainRun",false},{"readmeAuthority","historical provenance only; caller runtime not connected"}};
 std::ofstream out(std::filesystem::path(resultPath),std::ios::binary);out<<result.dump(2)<<"\n";std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;
}
