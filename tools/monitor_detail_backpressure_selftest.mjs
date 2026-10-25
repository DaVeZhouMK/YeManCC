// Compile the actual detailed logger before/current headers. Synthetic records,
// deterministic work counters and executor barriers only; no CPU utilization test.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd(),task=path.resolve(process.env.YMCC_MONITOR_PRESSURE_TASK||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-detail-backpressure-20261016'),out=path.join(task,'native-tests');fs.mkdirSync(out,{recursive:true});
const baseline=fs.readFileSync(task+'/before/monitor_detail_log.h','utf8'),current=fs.readFileSync('native/monitor_detail_log.h','utf8');
for(const [name,header]of [['previous',baseline],['current',current]])fs.writeFileSync(out+'/'+name+'.h',header.replace('#pragma once','').replace('#include "../deps/json/json.hpp"',`#include "${root.replaceAll('\\','/')}/deps/json/json.hpp"`).replace('namespace ymcc::monitordetail {','namespace '+name+' {'));
const source=String.raw`
#include <windows.h>
#include <future>
#include <chrono>
#include <iostream>
#include <fstream>
#include <vector>
#include "previous.h"
#include "current.h"
using json=nlohmann::json;
template<class Q,class L>struct Harness {
 bool enabled=true,reject=false;unsigned submits=0,encodes=0,copies=0,closes=0;
 std::vector<std::function<void()>> jobs;std::vector<std::string>written;Q queue;
 Harness(L l={}):queue({[this]{return enabled;},[this](auto f){++submits;if(reject)return false;jobs.push_back(std::move(f));return true;},[this](auto line){written.emplace_back(line);return true;},[]{},[this]{++closes;}},l){}
 bool sample(std::uint64_t now,const json& cache){auto permit=queue.permit(now);if(!permit)return false;++copies;const auto view=cache;auto row=current::snapshotRecord(view,now,7);++encodes;return queue.offer(row.dump(),*permit);}
 bool tinyRow(std::uint64_t now){auto permit=queue.permit(now);if(!permit)return false;++copies;++encodes;return queue.offer("{}",*permit);}
 void run(){while(!jobs.empty()){auto f=std::move(jobs.front());jobs.erase(jobs.begin());f();}}
 void gate(bool value){enabled=value;queue.gateChanged(value);}
};
using Old=Harness<previous::Queue,previous::Limits>;using New=Harness<current::Queue,current::Limits>;
int main(int argc,char**argv){if(argc!=2)return 3;json checks=json::array(),work=json::object();int failures=0;auto check=[&](const char*name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok)++failures;};
 const json cache={{"available",true},{"seq",1},{"collectedAt",100000},{"top","{\"cpuPower\":30}"},{"fps","{\"fps\":120}"}};
 {
  previous::Limits a;a.queueRows=3;current::Limits b;b.queueRows=3;Old before(a);New after(b);
  for(unsigned i=0;i<100;++i){before.sample(i*1000,cache);after.sample(i*1000,cache);}
  check("baseline reproduces encoding all 100 records for a stalled three-row queue",before.encodes==100&&before.queue.stats()["pendingRows"]==3);
  check("full queue is rejected before copying or JSON encoding",after.encodes==3&&after.copies==3);
  check("all skipped full samples remain counted without retaining more data",after.queue.stats()["droppedQueueFull"]==97&&after.queue.stats()["pendingRows"]==3);
  check("backpressure keeps exactly one already-scheduled drain job",after.jobs.size()==1&&after.submits==1);
  before.run();after.run();check("admitted records and original payload text remain byte-identical",before.written==after.written&&after.written.size()==3);
  check("draining restores the full row and byte budget",after.queue.stats()["pendingRows"]==0&&after.queue.stats()["pendingBytes"]==0);
  check("a subsequent real sample is admitted after pressure clears",after.sample(100000,cache));after.run();check("resumed sample is written once",after.written.size()==4);
  work["hundredStalledSamples"]={{"beforeSnapshotCopies",before.copies},{"afterSnapshotCopies",after.copies-1},{"beforeJsonEncodes",before.encodes},{"afterJsonEncodes",after.encodes-1}};
 }
 {
  previous::Limits a;a.queueRows=3;current::Limits b;b.queueRows=3;Old before(a);New after(b);before.reject=after.reject=true;
  for(unsigned i=0;i<3;++i){before.tinyRow(i*1000);after.tinyRow(i*1000);}before.reject=after.reject=false;
  before.tinyRow(3000);after.tinyRow(3000);
  check("baseline reproduces an unscheduled full queue after submit recovers",before.jobs.empty()&&before.queue.stats()["pendingRows"]==3);
  check("full-queue admission retries scheduling without encoding another sample",after.jobs.size()==1&&after.encodes==3);
  after.run();check("recovered executor drains the original bounded backlog",after.written.size()==3&&after.queue.stats()["pendingRows"]==0);
  check("new data can flow after scheduler rejection and full-queue recovery",after.tinyRow(4000));after.run();check("recovery does not duplicate or lose admitted rows",after.written.size()==4);
 }
 {
  current::Limits l;l.queueRows=2;New h(l);h.reject=true;h.tinyRow(0);h.tinyRow(1000);const auto attempts=h.submits,encodes=h.encodes;
  for(unsigned t=2000;t<3000;t+=10)h.tinyRow(t);
  check("100 rapid pressure attempts use only one 1Hz scheduler retry",h.submits==attempts+1);
  check("rejected scheduling on a full queue does not allocate or encode rows",h.encodes==encodes&&h.queue.stats()["pendingRows"]==2&&h.queue.stats()["droppedQueueFull"]==1);
  h.reject=false;h.tinyRow(3000);h.run();check("a later existing collection tick recovers without a retry timer",h.written.size()==2&&h.queue.stats()["pendingRows"]==0);
 }
 {
  const auto charge=std::string("{}").capacity()+1;previous::Limits a;a.queueBytes=2*charge;current::Limits b;b.queueBytes=2*charge;Old before(a);New after(b);
  for(unsigned i=0;i<10;++i){before.tinyRow(i*1000);after.tinyRow(i*1000);}
  check("exact byte-cap pressure is rejected before encoding",after.encodes==2&&before.encodes==10&&after.queue.stats()["pendingBytes"]==2*charge);
  check("byte-cap skipped samples and strict limit are retained",after.queue.stats()["droppedQueueFull"]==8&&after.queue.stats()["pendingRows"]==2);after.run();check("byte-limited queue recovers all charged memory",after.queue.stats()["pendingBytes"]==0);
 }
 {
  current::Limits l;l.queueRows=2;New h(l);h.reject=true;h.tinyRow(0);h.tinyRow(1000);h.gate(false);const auto submitted=h.submits;const auto encoded=h.encodes;
  h.reject=false;for(unsigned i=2;i<10;++i)h.tinyRow(i*1000);
  check("detail-off produces no encode or retry even under pressure",h.encodes==encoded&&h.submits==submitted);
  h.gate(true);h.tinyRow(10000);h.run();check("gate-off/on cannot replay retired queued snapshots",h.written.empty()&&h.queue.stats()["droppedGate"]==2);
  check("new gate epoch resumes after stale pressure drains",h.tinyRow(11000));h.run();check("only new-epoch snapshot is written",h.written.size()==1);
 }
 {
  std::promise<void> entered,release;auto future=release.get_future().share();std::function<void()> job;bool on=true;unsigned writes=0;current::Limits l;l.queueRows=2;
  current::Queue q({[&]{return on;},[&](auto f){job=std::move(f);return true;},[&](auto){if(++writes==1){entered.set_value();future.wait();}return true;},[]{},[]{}},l);
  auto p=q.permit(0);q.offer("{}",*p);auto sink=std::async(std::launch::async,[&]{job();});entered.get_future().wait();
  auto producer=std::async(std::launch::async,[&]{auto next=q.permit(1000);if(!next||!q.offer("{}",*next))return false;const auto denied=q.permit(2000);return !denied&&q.stats()["pendingRows"]==2;});
  const bool free=producer.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;check("pre-encoding pressure check does not wait on blocked file writer",free);if(free)check("in-flight record stays charged before pre-encoding admission",producer.get());release.set_value();sink.get();if(!free)producer.get();check("writer release drains both records and releases budget",writes==2&&q.stats()["pendingRows"]==0&&q.stats()["pendingBytes"]==0);
 }
 json result={{"ok",failures==0},{"failures",failures},{"checks",checks},{"deterministicWork",work},{"hardwareReads",false},{"newPollingTimers",false},{"cpuUsageMeasured",false},{"installedFilesChanged",false}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"Detailed log backpressure: "<<checks.size()<<" checks, "<<failures<<" failed\n"<<work.dump(2)<<"\n";for(const auto& c:checks)if(!c["ok"].get<bool>())std::cout<<"FAIL "<<c["name"]<<"\n";return failures?2:0;
}
`;
fs.writeFileSync(out+'/backpressure.cpp',source);fs.writeFileSync(out+'/build.cmd',`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 "${out}/backpressure.cpp" /Fo"${out}/backpressure.obj" /Fe"${out}/backpressure.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/backpressure.exe" "${out}/RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=spawnSync('cmd.exe',['/d','/c',out+'/build.cmd'],{cwd:out,encoding:'utf8',timeout:45000,maxBuffer:2e6});fs.writeFileSync(out+'/BUILD.log',(run.stdout||'')+(run.stderr||''));fs.writeFileSync(out+'/SOURCE-PINS.json',JSON.stringify({before:crypto.createHash('sha256').update(baseline).digest('hex'),after:crypto.createHash('sha256').update(current).digest('hex')},null,2));console.log(((run.stdout||'')+(run.stderr||'')).slice(-4500));if(run.status!==0)process.exitCode=2;
