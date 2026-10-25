// Execute actual native ordinary/fast serialization and snapshot publication.
// Output receives inert receipts; no RTSS mapping, process, HWiNFO or game APIs.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/');
const source=path.resolve(process.env.YMCC_FRAME_BUFFER_SOURCE||root).replaceAll('\\','/');
const out=path.resolve(process.env.YMCC_FRAME_BUFFER_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/frame-buffer-integration-selftest').replaceAll('\\','/');
const fixture=path.resolve(process.env.YMCC_FRAME_BUFFER_BEFORE||root+'/tools/fixtures/monitor_frame_buffer_before_20261017.inc');
fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync(source+'/native/main.cpp','utf8').replaceAll('\r\n','\n'),old=fs.readFileSync(fixture,'utf8').replaceAll('\r\n','\n');
function cut(text,a,b){const i=text.indexOf(a),j=text.indexOf(b,i);assert(i>=0&&j>i,a);return text.slice(i,j);}
function fn(sig){const start=main.indexOf(sig);assert(start>=0,sig);const tail=main.slice(start),masked=tail.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>' '.repeat(m.length));let depth=0;for(let i=masked.indexOf('{');i<masked.length;i++){if(masked[i]==='{')++depth;else if(masked[i]==='}'&&!--depth)return tail.slice(0,i+1);}throw Error(sig);}
const oldPart=name=>cut(old,'// '+name+'_BEGIN','// '+name+'_END').slice(('// '+name+'_BEGIN').length);
const state=cut(main,'struct MonitorSnapshotState {','\n};')+'\n};';
const ordinary=cut(main,'            monitorFpsText = frameOutput.packetTextView(nowMs);','            if (!monitorFpsText.empty())');
const fast=cut(main,'                monitorSnapshotPublishFramesBorrowed(iterGeneration,static_cast<long long>(frameNow),','\n            }\n            continue;');
assert(!ordinary.includes('packet(nowMs)')&&!fast.includes('framePacket'));
const publisher=fn('static void monitorSnapshotPublishFramesBorrowed(')+'\n'+fn('static void monitorSnapshotPublishFrames(');
const side=(name,ordinary,fast,publisher)=>`namespace ${name} {
static std::mutex g_monitorSnapshotMx;
static MonitorSnapshotState g_monitorSnapshot;
static std::atomic<unsigned long long> g_monitorSnapshotOversizeDrops{0};
static std::function<void()> barrier;
static void monitorTestBarrierPause(int) { if(barrier)barrier(); }
${publisher}
static std::string ordinary(Output& frameOutput,std::uint64_t nowMs){std::string monitorFpsText;${ordinary}\nreturn monitorFpsText;}
static void fast(Output& frameOutput,std::uint64_t frameNow,unsigned long long iterGeneration){${fast}\n}
}`;
const cpp=String.raw`
#define NOMINMAX
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <random>
#include <string>
#include "${root}/deps/json/json.hpp"
using json=nlohmann::json;
#include "${source}/native/monitor_frame_output.h"
#include "${source}/native/monitor_snapshot_contract.h"
using ymcc::monitorframe::Output;
using ymcc::monitorframe::RtssFrame;
static bool tracking=false;
static std::size_t allocations=0,allocationBytes=0,checks=0,comparisons=0;
void* operator new(std::size_t n){if(tracking){++allocations;allocationBytes+=n;}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static void check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
${state}
${side('before',oldPart('ORDINARY'),oldPart('FAST'),oldPart('PUBLISH'))}
${side('after',ordinary,fast,publisher)}
static constexpr std::uint64_t epoch=1700000000000ULL;
static MonitorSnapshotState cache(){MonitorSnapshotState s;s.valid=true;s.generation=7;s.seq=31;s.collectedAt=epoch;s.heartbeatAt=epoch;s.hwinfoOkAt=epoch;s.hwDown=false;s.topText="{\"temperature\":67}";s.fpsText="{\"fps\":120}";return s;}
static json meta(const MonitorSnapshotState&s){return {{"valid",s.valid},{"generation",s.generation},{"seq",s.seq},{"collectedAt",s.collectedAt},{"heartbeatAt",s.heartbeatAt},{"hwinfoOkAt",s.hwinfoOkAt},{"hwDown",s.hwDown},{"top",s.topText},{"fps",s.fpsText}};}
static void resetCache(){before::g_monitorSnapshot=after::g_monitorSnapshot=cache();before::g_monitorSnapshotOversizeDrops=after::g_monitorSnapshotOversizeDrops=0;before::barrier=after::barrier={};}
static bool sameTypes(const json&a,const json&b){if(a.type()!=b.type())return false;if(a.is_object()){if(a.size()!=b.size())return false;for(auto i=a.begin();i!=a.end();++i){auto j=b.find(i.key());if(j==b.end()||!sameTypes(i.value(),*j))return false;}return true;}if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!sameTypes(a[i],b[i]))return false;return true;}return a==b;}
static void compare(Output&a,Output&b,std::uint64_t now,unsigned long long gen=7){
 std::string x,y;bool threwA=false,threwB=false;
 try{x=before::ordinary(a,now);}catch(const json::exception&){threwA=true;}
 try{y=after::ordinary(b,now);}catch(const json::exception&){threwB=true;}
 check(threwA==threwB&&x==y,"ordinary lane retains bytes and strict UTF-8 error behavior");
 if(!threwA&&!x.empty())check(sameTypes(json::parse(x),json::parse(y)),"ordinary packet preserves recursive JSON types");
 const auto priorA=meta(before::g_monitorSnapshot),priorB=meta(after::g_monitorSnapshot);
 threwA=threwB=false;
 try{before::fast(a,now,gen);}catch(const json::exception&){threwA=true;}
 try{after::fast(b,now,gen);}catch(const json::exception&){threwB=true;}
 check(threwA==threwB,"fast lane retains strict UTF-8 exception behavior");
 check(meta(before::g_monitorSnapshot)==meta(after::g_monitorSnapshot)&&before::g_monitorSnapshotOversizeDrops==after::g_monitorSnapshotOversizeDrops,"complete cache bytes, health, timestamps, generation, sequence and drop counts match");
 check(before::g_monitorSnapshot.hwinfoOkAt==priorA["hwinfoOkAt"]&&after::g_monitorSnapshot.hwinfoOkAt==priorB["hwinfoOkAt"],"frame publication cannot renew HWiNFO proof");
 if(threwA)check(meta(before::g_monitorSnapshot)==priorA&&meta(after::g_monitorSnapshot)==priorB,"failed serialization cannot publish a partial packet");
 ++comparisons;
}
static void bind(Output&a,Output&b,std::uint64_t pid=2077,std::uint64_t birth=99,std::uint64_t at=epoch-10000){a.bind(pid,birth,at);b.bind(pid,birth,at);}
static void hardware(Output&a,Output&b,std::uint64_t poll,double fps,double low,double gpu,double power,const std::string&game,std::uint64_t now,double ms,std::uint64_t period=3000){a.hardware(poll,fps,low,gpu,power,game,now,ms,period);b.hardware(poll,fps,low,gpu,power,game,now,ms,period);}
static void rtss(Output&a,Output&b,std::uint64_t at,double fps=120,double ms=8.3){a.observeRtss({true,ms,fps,at});b.observeRtss({true,ms,fps,at});}
int main(int argc,char**argv){check(argc==2,"report path required");json cases=json::array(),work=json::array();
 resetCache();Output a,b;compare(a,b,epoch);bind(a,b);
 const std::string title="Cyberpunk 2077 / \xe4\xb8\xad\xe6\x96\x87 / \xf0\x9f\x90\x88 / \"\\\n";
 hardware(a,b,epoch/1000,180,101,75,30,title,epoch,5.5);compare(a,b,epoch);
 for(std::uint64_t age:{1ULL,4999ULL,5000ULL,5001ULL,6000ULL})compare(a,b,epoch+age);
 cases.push_back("missing, genuine primary, LOW, exact proof expiry and no synthetic renewal");
 resetCache();a.reset();b.reset();bind(a,b);rtss(a,b,epoch);compare(a,b,epoch);
 hardware(a,b,epoch/1000,180,90,80,31,title,epoch,5.5);rtss(a,b,epoch+100);compare(a,b,epoch+100);
 rtss(a,b,epoch+200,117,8.6);compare(a,b,epoch+200);
 compare(a,b,epoch+5100); // Hardware expires; genuine RTSS rescue is still valid.
 a.hardwareMetrics(0,-1,-1,title,epoch+5200,3000);b.hardwareMetrics(0,-1,-1,title,epoch+5200,3000);compare(a,b,epoch+5200);
 rtss(a,b,epoch+5300);compare(a,b,epoch+5300);compare(a,b,epoch+10400);
 cases.push_back("Presented priority, independent RTSS curve, rescue, hardware loss and family expiry");
 hardware(a,b,(epoch+11000)/1000,200,0,0,0,title,epoch+11000,5);compare(a,b,epoch+11000);
 compare(a,b,epoch+10999);bind(a,b,2077,100,epoch+12000);compare(a,b,epoch+12000);
 hardware(a,b,(epoch+11000)/1000,999,999,99,99,"old-generation",epoch+12500,1);compare(a,b,epoch+12500);
 hardware(a,b,(epoch+13000)/1000,60,10,55,22,title,epoch+13000,16.7);compare(a,b,epoch+13000);
 cases.push_back("clock rollback, zero LOW, PID reuse and old generation rejection");
 for(const auto& bad:{std::string(1,char(0xff)),std::string("valid-prefix")+char(0xc3),std::string("a\0b",3)+char(0x80)}){
  resetCache();a.reset();b.reset();bind(a,b);hardware(a,b,epoch/1000,100,80,70,29,bad,epoch,10);compare(a,b,epoch);
  hardware(a,b,(epoch+1000)/1000,101,81,71,30,title,epoch+1000,9.9);compare(a,b,epoch+1000);
 }
 cases.push_back("invalid UTF-8 throws identically, cannot partially publish, and retained serializer recovers");
 for(std::size_t length:{0U,8000U,12000U,16000U,16384U,17000U,40000U}){
  resetCache();a.reset();b.reset();bind(a,b);hardware(a,b,epoch/1000,60,30,50,25,std::string(length,'G'),epoch,16.7);compare(a,b,epoch);
  check(after::g_monitorSnapshot.fpsText.size()<=16384,"retained FPS document remains bounded as a whole");
 }
 cases.push_back("large titles exercise serializer growth, combined cache cap and full document rejection");
 // Caller-provided text is exercised directly, including stopped-generation barriers.
 for(std::size_t topSize:{0U,8192U,16384U})for(std::size_t size:{0U,8192U,16384U,16385U})for(bool valid:{false,true})for(bool currentGen:{false,true}){
  resetCache();before::g_monitorSnapshot.topText=after::g_monitorSnapshot.topText=std::string(topSize,'T');before::g_monitorSnapshot.valid=after::g_monitorSnapshot.valid=valid;
  std::string left(size,'F'),right=left;auto gen=currentGen?7:6;
  before::monitorSnapshotPublishFrames(gen,epoch+100,left);after::monitorSnapshotPublishFramesBorrowed(gen,epoch+100,right);
  check(meta(before::g_monitorSnapshot)==meta(after::g_monitorSnapshot)&&before::g_monitorSnapshotOversizeDrops==after::g_monitorSnapshotOversizeDrops,"borrowed publisher preserves all generation/validity and cap cases");
  check(after::g_monitorSnapshot.fpsText.size()<=16384,"retained raw FPS text is bounded");
  ++comparisons;
 }
 resetCache();before::barrier=[] {++before::g_monitorSnapshot.generation;};after::barrier=[] {++after::g_monitorSnapshot.generation;};
 {std::string x="late",y=x;before::monitorSnapshotPublishFrames(7,epoch+100,x);after::monitorSnapshotPublishFramesBorrowed(7,epoch+100,y);check(meta(before::g_monitorSnapshot)==meta(after::g_monitorSnapshot)&&after::g_monitorSnapshot.seq==31,"mode change at existing barrier rejects late borrowed publication");}
 resetCache();after::g_monitorSnapshot.fpsText.reserve(1024*1024);{std::string text="{}";after::monitorSnapshotPublishFramesBorrowed(7,epoch+100,text);check(after::g_monitorSnapshot.fpsText=="{}"&&text.capacity()<=16384,"old corrupt cache reserve cannot escape into recycled serializer buffer");}
 cases.push_back("publisher generation, invalid cache, stop barrier, document caps and recycled capacity are bounded");
 // Repeated source transitions: real receipt timestamps, no actual collection.
 resetCache();a.reset();b.reset();bind(a,b);
 std::mt19937 rng(0x594d4343U);
 for(unsigned i=0;i<2000;++i){const auto now=epoch+i*100ULL;
  if(i%200==0)bind(a,b,2077,i/200+100,now-1000);
  if(i%10==0){const auto poll=(now/3000)*3;double fps=i%70==0?0:double(60+rng()%200);hardware(a,b,poll,fps,i%30?-1:double(rng()%120),rng()%101,20+rng()%30,title,now,5+double(rng()%100)/10);}
  if(i%17!=0)rtss(a,b,now,70+rng()%140,5+double(rng()%100)/10);
  if(i%91==0){a.hardwareMetrics(0,-1,-1,title,now);b.hardwareMetrics(0,-1,-1,title,now);}
  compare(a,b,now);
 }
 cases.push_back("2000 mixed true receipts and missing/zero samples preserve complete packet bytes");
 // Warm realistic ordinary + 100 ms output paths before counting just actual output/publication work.
 resetCache();a.reset();b.reset();bind(a,b);std::size_t ordinaryA=0,ordinaryB=0,ordinaryBytesA=0,ordinaryBytesB=0,fastA=0,fastB=0,fastBytesA=0,fastBytesB=0;unsigned ordinaryCalls=0,fastCalls=0;
 for(unsigned i=0;i<5200;++i){const auto now=epoch+i*100ULL;
  if(i%10==0){const auto poll=(now/3000)*3;hardware(a,b,poll,180,101,75,30,title,now,5.5);}
  rtss(a,b,now,120,8.3+double(i%13)/10);
  const bool count=i>=200;
  if(i%10==0){
   allocations=allocationBytes=0;tracking=count;auto x=before::ordinary(a,now);tracking=false;ordinaryA+=allocations;ordinaryBytesA+=allocationBytes;
   allocations=allocationBytes=0;tracking=count;auto y=after::ordinary(b,now);tracking=false;ordinaryB+=allocations;ordinaryBytesB+=allocationBytes;
   check(x==y,"measured ordinary lane bytes match");if(count)++ordinaryCalls;
  }
  allocations=allocationBytes=0;tracking=count;before::fast(a,now,7);tracking=false;fastA+=allocations;fastBytesA+=allocationBytes;
  allocations=allocationBytes=0;tracking=count;after::fast(b,now,7);tracking=false;fastB+=allocations;fastBytesB+=allocationBytes;
  check(meta(before::g_monitorSnapshot)==meta(after::g_monitorSnapshot),"measured fast lane complete cache matches");if(count)++fastCalls;
 }
 check(ordinaryA>ordinaryB&&ordinaryBytesA>ordinaryBytesB,"ordinary production call removes JSON tree and serializer allocations");
 check(fastA>fastB&&fastBytesA>fastBytesB,"fast production call removes packet tree copy and reuses transferred string buffers");
 work.push_back({{"lane","ordinary"},{"calls",ordinaryCalls},{"before",{{"allocations",ordinaryA},{"bytes",ordinaryBytesA}}},{"after",{{"allocations",ordinaryB},{"bytes",ordinaryBytesB}}}});
 work.push_back({{"lane","fast"},{"calls",fastCalls},{"before",{{"allocations",fastA},{"bytes",fastBytesA}}},{"after",{{"allocations",fastB},{"bytes",fastBytesB}}}});
 json result={{"ok",true},{"checks",checks},{"comparisons",comparisons},{"cases",cases},{"operations",work},{"scope","Exact ordinary/fast callsites and publishers, actual current Output with inert deterministic receipts, complete output bytes/types/proofs/ownership/caps. Allocation work only for output paths after warmup; no hardware/RTSS mapping reads, IPC/CPU percentage/timing benchmark or deployed runtime."}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<result.dump(2)<<'\n';
}
`;
fs.writeFileSync(out+'/frame-buffer.cpp',cpp);
const batch=out+'/compile-run.bat';fs.writeFileSync(batch,['@echo off','call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul','if errorlevel 1 exit /b %ERRORLEVEL%',`cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE "${out}/frame-buffer.cpp" /I"${root}/native" /Fo"${out}/frame-buffer.obj" /Fe"${out}/frame-buffer.exe"`,'if errorlevel 1 exit /b %ERRORLEVEL%',`"${out}/frame-buffer.exe" "${out}/RESULT.json"`,'exit /b %ERRORLEVEL%',''].join('\r\n'));
const run=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsHide:true,windowsVerbatimArguments:true,encoding:'utf8',timeout:60000,maxBuffer:3e6});
fs.writeFileSync(out+'/COMPILE-RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,(run.stdout||'')+(run.stderr||''));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(out+'/SOURCE-PINS.json',JSON.stringify({source,before:{ordinary:sha(oldPart('ORDINARY')),fast:sha(oldPart('FAST')),publisher:sha(oldPart('PUBLISH'))},after:{ordinary:sha(ordinary),fast:sha(fast),publisher:sha(publisher)},frameOutput:sha(fs.readFileSync(source+'/native/monitor_frame_output.h')),contract:sha(fs.readFileSync(source+'/native/monitor_snapshot_contract.h')),test:sha(fs.readFileSync(new URL(import.meta.url)))},null,2));
const result=JSON.parse(fs.readFileSync(out+'/RESULT.json','utf8'));assert(result.ok);console.log(JSON.stringify({ok:result.ok,checks:result.checks,comparisons:result.comparisons,cases:result.cases,operations:result.operations},null,2));