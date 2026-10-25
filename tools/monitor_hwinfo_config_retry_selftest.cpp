// Real Windows locked-file checks plus deterministic failure/concurrency cases.
// The exact production configure body is inserted by the runner below.
#include <fstream>
#include <iostream>
#include <functional>
#include "../native/monitor_hwinfo_manager.h"
using namespace ymcc::hwinfo;
namespace hwinfo = ymcc::hwinfo;
static const std::string original="[Settings]\r\nSensorsSM=0\r\nKeep=original\r\n";
static const std::string desired="[Settings]\r\nSensorsSM=1\r\nKeep=original\r\n";
static const std::string foreign="[Settings]\r\nSensorsSM=0\r\nKeep=other-user-value\r\n";
struct Fixture {
 std::wstring destination;Handle lock;
 unsigned writes=0,waits=0,producerChecks=0,reads=0,failures=0,releaseAt=0;
 DWORD failureCode=ERROR_LOCK_VIOLATION;
 bool producerAppeared=false,cancel=false,postCommitVerifyLock=false;
 std::optional<std::string> planBase;
 std::vector<unsigned> delays;
 std::function<void(Fixture&)> onWait,onPlan;
 void assertStopped(){++producerChecks;if(producerAppeared)throw Error(4,"fixture producer appeared");}
 std::optional<std::string> readBytes(const std::wstring&p,std::size_t limit,bool optional=false){if(p==destination)++reads;return hwinfo::readBytes(p,limit,optional);}
 std::string planned(const Request&,const std::optional<std::string>&previous){planBase=previous;if(onPlan)onPlan(*this);return desired;}
 void writeAtomic(const std::wstring&p,const std::string&value,bool keep=false){
  if(p==destination){++writes;if(failures){--failures;throw Error(5,"fixture write failure",failureCode==ERROR_ACCESS_DENIED,failureCode);}}
  hwinfo::writeAtomic(p,value,keep);
  if(p==destination&&postCommitVerifyLock){postCommitVerifyLock=false;throw Error(8,"fixture verify read briefly locked",false,ERROR_SHARING_VIOLATION);}
 }
 bool wait(unsigned delay){++waits;delays.push_back(delay);if(releaseAt&&waits>=releaseAt)lock.reset();if(onWait)onWait(*this);return !cancel;}
 // YMCC_CONFIGURE_BODY
};
struct Outcome {int code=0;DWORD win32=0;bool elevation=false,conflict=false;};
static Outcome run(Fixture&f){try{f.configure(L"unused",Request{});return {};}catch(const ConfigConflict&e){return {e.code,e.win32,e.elevation,true};}catch(const Error&e){return {e.code,e.win32,e.elevation,false};}}
int main(int argc,char**argv){if(argc!=3)return 2;const fs::path root=argv[1];fs::create_directories(root);Json cases=Json::array();unsigned serial=0;bool all=true;
 const auto fixture=[&](bool exists=true){auto f=std::make_unique<Fixture>();const auto folder=root/std::to_wstring(++serial);fs::create_directories(folder);f->destination=(folder/L"HWiNFO64.INI").wstring();if(exists)hwinfo::writeAtomic(f->destination,original);return f;};
 const auto save=[&](const char*name,Fixture&f,Outcome result,bool passed){all=all&&passed;cases.push_back({{"name",name},{"ok",passed},{"code",result.code},{"win32",result.win32},{"writes",f.writes},{"waits",f.waits},{"readCalls",f.reads},{"producerChecks",f.producerChecks},{"delays",f.delays},{"conflict",result.conflict}});if(!passed)std::cerr<<name<<'\n';};
 const auto contents=[](Fixture&f){return hwinfo::readBytes(f.destination,262144,true);};
 const auto lock=[](Fixture&f){f.lock=Handle(CreateFileW(f.destination.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!f.lock)throw std::runtime_error("fixture lock failed");};
 {auto f=fixture();auto r=run(*f);save("ordinary config commits once without waits",*f,r,r.code==0&&f->writes==1&&f->waits==0&&contents(*f)==desired&&f->planBase==original);}
 {auto f=fixture();hwinfo::writeAtomic(f->destination,desired);auto r=run(*f);save("identical config retains first physical restore commit",*f,r,r.code==0&&f->writes==1&&contents(*f)==desired);}
 {auto f=fixture();lock(*f);f->releaseAt=1;auto r=run(*f);save("real delete-sharing lock released after first retry recovers",*f,r,r.code==0&&f->writes==2&&f->waits==1&&f->delays==std::vector<unsigned>{200}&&contents(*f)==desired);}
 {auto f=fixture();lock(*f);auto r=run(*f);save("permanent real lock stops after five attempts and four waits",*f,r,r.code==5&&r.win32==ERROR_SHARING_VIOLATION&&f->writes==5&&f->waits==4&&contents(*f)==original);save("permanent lock preserves first and last backups",*f,r,*hwinfo::readBytes(f->destination+L".ymcc-original.bak",262144)==original&&*hwinfo::readBytes(f->destination+L".ymcc-last.bak",262144)==original);}
 for(unsigned failures:{1u,3u,4u,5u}){auto f=fixture();f->failures=failures;auto r=run(*f);save("bounded record lock failures retain exact attempt limit",*f,r,f->writes==(std::min)(failures+1,5u)&&f->waits==(std::min)(failures,4u)&&(failures<5?(r.code==0&&contents(*f)==desired):(r.code==5&&contents(*f)==original)));}
 {auto f=fixture();f->failures=5;f->failureCode=ERROR_ACCESS_DENIED;auto r=run(*f);save("permission failure returns immediately for existing elevation handoff",*f,r,r.code==5&&r.elevation&&f->writes==1&&f->waits==0&&contents(*f)==original);}
 {auto f=fixture();f->failures=5;f->failureCode=ERROR_DISK_FULL;auto r=run(*f);save("permanent disk failure is not mislabeled a temporary lock",*f,r,r.code==5&&f->writes==1&&f->waits==0&&contents(*f)==original);}
 {auto f=fixture();lock(*f);f->releaseAt=1;f->onWait=[](Fixture&x){hwinfo::writeAtomic(x.destination,foreign);};auto r=run(*f);save("different user edit between attempts is preserved",*f,r,r.code==8&&r.conflict&&f->writes==1&&f->waits==1&&contents(*f)==foreign);}
 {auto f=fixture();lock(*f);f->releaseAt=1;f->onWait=[](Fixture&x){DeleteFileW(x.destination.c_str());};auto r=run(*f);save("concurrent deletion is not recreated by retry",*f,r,r.code==8&&r.conflict&&f->writes==1&&!contents(*f));}
 {auto f=fixture();lock(*f);f->releaseAt=1;f->onWait=[](Fixture&x){x.producerAppeared=true;};auto r=run(*f);save("new producer cancels further writes before next attempt",*f,r,r.code==4&&f->writes==1&&f->waits==1&&contents(*f)==original);}
 {auto f=fixture();lock(*f);f->cancel=true;auto r=run(*f);save("application exit cancels bounded wait without writing further",*f,r,r.code==15&&f->writes==1&&f->waits==1&&contents(*f)==original);}
 {auto f=fixture();f->onPlan=[](Fixture&x){hwinfo::writeAtomic(x.destination,foreign);};auto r=run(*f);save("concurrent edit after planning is rejected before any commit",*f,r,r.code==8&&r.conflict&&f->writes==0&&contents(*f)==foreign&&f->planBase==original);}
 {auto f=fixture();f->onPlan=[](Fixture&x){hwinfo::writeAtomic(x.destination,desired);};auto r=run(*f);save("concurrent desired bytes are not rolled back when no commit was attempted",*f,r,r.code==8&&r.conflict&&f->writes==0&&contents(*f)==desired);}
 {auto f=fixture(false);auto r=run(*f);save("missing original config may be created through the managed plan",*f,r,r.code==0&&f->writes==1&&contents(*f)==desired&&!f->planBase);}
 {auto f=fixture(false);f->onPlan=[](Fixture&x){hwinfo::writeAtomic(x.destination,foreign);};auto r=run(*f);save("new external config is not overwritten or deleted",*f,r,r.code==8&&r.conflict&&f->writes==0&&contents(*f)==foreign);}
 {auto f=fixture();f->postCommitVerifyLock=true;auto r=run(*f);save("verified planned bytes after temporary verification lock do not require another commit",*f,r,r.code==0&&f->writes==1&&f->waits==1&&contents(*f)==desired);}
 bool leftover=false;for(const auto&e:fs::recursive_directory_iterator(root))if(e.path().filename().wstring().find(L".ymcc-tmp-")!=std::wstring::npos)leftover=true;cases.push_back({{"name","successful and failed attempts leave no staged files"},{"ok",!leftover}});all=all&&!leftover;
 std::ofstream(argv[2])<<Json({{"ok",all},{"cases",cases},{"scope","Actual production configure body, actual atomic IO, real Windows file locks and inert producer/wait hooks in an isolated task directory. No real HWiNFO process, registry or settings changes; no CPU timing benchmark."}}).dump(2);return all?0:1;
}
