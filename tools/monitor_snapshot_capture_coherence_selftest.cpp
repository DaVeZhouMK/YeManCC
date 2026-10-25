#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
#include "__ROOT__/native/monitor_snapshot_contract.h"
#include "__ROOT__/native/monitor_detail_log.h"
using json=nlohmann::json;
static thread_local bool countAllocations=false;
static thread_local std::size_t allocations=0,allocationBytes=0,modeLocks=0,cacheLocks=0;
void* operator new(std::size_t n){if(countAllocations){++allocations;allocationBytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
enum class Role { None,Reader,Writer };
struct Gate {
 std::mutex mx;std::condition_variable cv;
 bool released=false,resume=false,readerHadSnapshot=false,writerTriedSnapshot=false,writerDone=false;
 template<class Pred> void wait(Pred pred){std::unique_lock<std::mutex> lock(mx);check(cv.wait_for(lock,std::chrono::seconds(5),pred),"bounded barrier timed out");}
 void releaseReader(bool held){std::unique_lock<std::mutex> lock(mx);readerHadSnapshot=held;released=true;cv.notify_all();check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return resume;}),"reader continuation timed out");}
 void writerAttempt(){std::lock_guard<std::mutex> lock(mx);writerTriedSnapshot=true;cv.notify_all();}
 void finished(){std::lock_guard<std::mutex> lock(mx);writerDone=true;cv.notify_all();}
};
static thread_local Role role=Role::None;
static thread_local Gate* gate=nullptr;
static thread_local unsigned snapshotLocks=0;
class AuditMutex {
 std::mutex mx_;bool snapshot_;
public:
 explicit AuditMutex(bool snapshot):snapshot_(snapshot){}
 void lock(){if(countAllocations)(snapshot_?cacheLocks:modeLocks)++;if(snapshot_&&role==Role::Writer&&gate)gate->writerAttempt();mx_.lock();if(snapshot_&&role==Role::Reader)++snapshotLocks;}
 bool try_lock(){if(!mx_.try_lock())return false;if(snapshot_&&role==Role::Reader)++snapshotLocks;return true;}
 void unlock(){if(snapshot_&&role==Role::Reader)--snapshotLocks;const bool held=snapshotLocks>0;mx_.unlock();if(!snapshot_&&role==Role::Reader&&gate)gate->releaseReader(held);}
};
__STATE__
__FUNCTIONS__
struct Lane {
 AuditMutex* modeMx;AuditMutex* snapshotMx;MonitorSnapshotState* state;bool* top;bool* fps;
 json(*serve)();MonitorDetailSnapshotCapture(*detail)(long long,unsigned,json);void(*setMode)(bool,bool,bool);
 void(*publish)(unsigned long long,bool,bool,bool,long long,std::string,std::string);
};
static Lane lane(bool changed){return changed?Lane{&after::g_nativeMonitorMx,&after::g_monitorSnapshotMx,&after::g_monitorSnapshot,&after::g_nativeMonitorTop,&after::g_nativeMonitorFps,&after::monitorSnapshotServeFn,&after::monitorSnapshotRecordForDetail,+[](bool top,bool fps,bool enabled){after::nativeMonitorSetMode(top,fps,enabled);},&after::monitorSnapshotPublish}:Lane{&before::g_nativeMonitorMx,&before::g_monitorSnapshotMx,&before::g_monitorSnapshot,&before::g_nativeMonitorTop,&before::g_nativeMonitorFps,&before::monitorSnapshotServeFn,&before::monitorSnapshotRecordForDetail,+[](bool top,bool fps,bool enabled){before::nativeMonitorSetMode(top,fps,enabled);},&before::monitorSnapshotPublish};}
static constexpr long long epoch=1710000000000LL;
static MonitorSnapshotState initial(bool active,std::size_t bytes){MonitorSnapshotState s;s.valid=active;s.generation=100;s.seq=9;s.collectedAt=epoch;s.heartbeatAt=epoch;s.hwinfoOkAt=epoch;s.hwDown=false;if(active){s.topText=std::string(bytes,'T');s.fpsText=std::string(bytes,'F');}return s;}
static void install(const Lane& l,const MonitorSnapshotState& s,bool top,bool fps){std::lock_guard<AuditMutex> mode(*l.modeMx);std::lock_guard<AuditMutex> cache(*l.snapshotMx);*l.state=s;*l.top=top;*l.fps=fps;}
static json read(const Lane& l,bool detail){if(!detail)return l.serve();auto value=l.detail(epoch,42,json::object());return json{{"row",std::move(value.row)},{"sameIteration",value.sameIteration}};}
struct Change {const char* name;bool top,fps,selectorTop,selectorFps,enabled,publish;};
static json runRace(bool changed,bool detail,const Change& c,std::size_t bytes){
 const auto l=lane(changed);install(l,initial(c.top||c.fps,bytes),c.top,c.fps);const auto old=read(l,detail);Gate barrier;json result;
 std::thread reader([&]{role=Role::Reader;gate=&barrier;result=read(l,detail);gate=nullptr;role=Role::None;});
 barrier.wait([&]{return barrier.released;});
 std::thread writer([&]{role=Role::Writer;gate=&barrier;l.setMode(c.selectorTop,c.selectorFps,c.enabled);if(c.publish)l.publish(101,true,true,true,epoch+1,"{\"source\":\"next\"}","{\"fps\":117}");barrier.finished();gate=nullptr;role=Role::None;});
 barrier.wait([&]{return barrier.writerTriedSnapshot;});
 if(!changed)barrier.wait([&]{return barrier.writerDone;});
 {std::lock_guard<std::mutex> lock(barrier.mx);check(barrier.readerHadSnapshot==changed,"snapshot capture coverage at mode unlock");if(changed)check(!barrier.writerDone,"writer must wait for coherent snapshot capture");barrier.resume=true;barrier.cv.notify_all();}
 reader.join();writer.join();const auto current=read(l,detail);const bool mixed=result!=old&&result!=current;
 check(changed?!mixed:mixed,changed?"new capture mixed two modes":"old source must reproduce mixed mode/cache");
 return {{"side",changed?"after":"before"},{"lane",detail?"detail":"ipc"},{"change",c.name},{"bytesPerPayload",bytes},{"mixed",mixed},{"modeReleasedWithSnapshot",barrier.readerHadSnapshot},{"initial",old},{"observed",result},{"final",current}};
}
static json stress(bool detail){
 const auto l=lane(true);auto s=initial(false,0);s.generation=0;install(l,s,false,false);
 std::atomic<bool> go{false};std::atomic<unsigned> mismatches{0},reads{0};
 std::thread writer([&]{while(!go.load())std::this_thread::yield();for(unsigned i=1;i<=2000;++i){l.setMode(true,false,(i&1)!=0);if(i&1)l.publish(i,true,false,true,epoch+i,"{\"sample\":1}",{});}});
 std::vector<std::thread> readers;for(unsigned n=0;n<4;++n)readers.emplace_back([&]{while(!go.load())std::this_thread::yield();for(unsigned i=0;i<2000;++i){const auto value=read(l,detail);const auto& data=detail?value.at("row").at("snapshot"):value;const auto generation=data.at("generation").get<unsigned long long>();if(data.at("topActive").get<bool>()!=bool(generation&1)||data.at("fpsActive")!=false)++mismatches;++reads;}});
 go.store(true);writer.join();for(auto& reader:readers)reader.join();check(mismatches==0,"concurrent mode generation must agree with live flags");return {{"lane",detail?"detail":"ipc"},{"reads",reads.load()},{"modeChanges",2000},{"mixed",mismatches.load()}};
}
static json steadyWork(){
 json rows=json::array();
 for(bool detail:{false,true})for(bool available:{false,true})for(std::size_t bytes:{std::size_t(64),std::size_t(12000),std::size_t(16385)}){
  json counts=json::array();
  for(bool changed:{false,true}){
   const auto l=lane(changed);install(l,initial(available,bytes),available,available);
   for(unsigned i=0;i<10;++i)(void)read(l,detail);
   allocations=allocationBytes=modeLocks=cacheLocks=0;countAllocations=true;
   for(unsigned i=0;i<500;++i){auto value=read(l,detail);}
   countAllocations=false;
   check(modeLocks==500&&cacheLocks==500,"one mode/cache acquisition per steady capture");
   counts.push_back({{"side",changed?"after":"before"},{"allocations",allocations},{"bytes",allocationBytes},{"modeLocks",modeLocks},{"cacheLocks",cacheLocks}});
  }
  check(counts[0]["allocations"]==counts[1]["allocations"]&&counts[0]["bytes"]==counts[1]["bytes"],"coherence capture adds no steady heap work");
  rows.push_back({{"lane",detail?"detail":"ipc"},{"available",available},{"bytesPerPayload",bytes},{"callsPerSide",500},{"counts",std::move(counts)}});
 }
 return rows;
}
int main(int argc,char**argv){
 const std::array<Change,5> changes{{{"stop-top",true,true,true,false,false,false},{"stop-fps",true,true,false,true,false,false},{"stop-both",true,true,true,true,false,false},{"start-top",false,true,true,false,true,false},{"start-and-publish",false,false,true,true,true,true}}};
 json races=json::array();unsigned beforeMixed=0,afterMixed=0;
 for(bool detail:{false,true})for(const auto& c:changes)for(std::size_t bytes:{std::size_t(64),std::size_t(8192),std::size_t(16385)})for(bool changed:{false,true}){auto row=runRace(changed,detail,c,bytes);if(row["mixed"]==true)(changed?afterMixed:beforeMixed)++;races.push_back(std::move(row));}
 json stresses=json::array();stresses.push_back(stress(false));stresses.push_back(stress(true));
 json report={{"ok",true},{"cases",races.size()},{"beforeMixed",beforeMixed},{"afterMixed",afterMixed},{"stress",std::move(stresses)},{"steadyWork",steadyWork()},{"races",std::move(races)},{"scope","Actual native mode/capture/publication functions; mutex wrappers provide deterministic barriers, collector/thread/notifications are inert; no app, device, hardware or installed helper execution"}};
 if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2);}std::cout<<"PASS coherence cases="<<report["cases"]<<" beforeMixed="<<beforeMixed<<" afterMixed="<<afterMixed<<'\n';
}
