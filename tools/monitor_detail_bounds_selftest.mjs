// Offline production recorder, file caps and export bounds. No app, games or hardware.
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_DETAIL_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-detail-bounds-20261016/tests');fs.mkdirSync(out,{recursive:true});
const native=String.raw`
#include <windows.h>
#include <future>
#include <iostream>
#include <vector>
#include <limits>
#include <thread>
#include <chrono>
#include "monitor_detail_log.h"
#include "monitor_export_state.h"
#include "log_export_evidence.h"
#include "steam_log_export.h"
using namespace ymcc::monitordetail;
using json=nlohmann::json;
int main(int argc,char**argv){if(argc<2)return 3;const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);int failures=0;json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
struct Harness{bool enabled=false,reject=false;std::vector<std::function<void()>>jobs;std::vector<std::string>written;int flushes=0,closes=0;Queue queue;Harness(Limits l={}):queue({[this]{return enabled;},[this](auto f){if(reject)return false;jobs.push_back(std::move(f));return true;},[this](auto line){written.emplace_back(line);return true;},[this]{++flushes;return true;},[this]{++closes;}},l){}void set(bool v){enabled=v;queue.gateChanged(v);}bool send(std::uint64_t now,std::string line="{}"){auto p=queue.permit(now);return p&&queue.offer(std::move(line),*p);}void run(){while(!jobs.empty()){auto f=std::move(jobs.front());jobs.erase(jobs.begin());f();}}};
{
 Harness h;check("detail off grants no permit, queues no work and writes nothing",!h.send(0)&&h.jobs.empty()&&h.written.empty());h.queue.gateChanged(false);check("repeated default-off state schedules no close/file job",h.jobs.empty());h.set(true);check("first detailed sample accepted",h.send(0));bool throttled=true;for(int i=100;i<1000;i+=100)throttled=throttled&&!h.send(i);check("100ms curve updates cannot increase 1Hz diagnostic sampling",throttled);check("next second can sample",h.send(1000));check("one pending pool job drains multiple rows",h.jobs.size()==1);h.run();check("both admitted rows reach sink",h.written.size()==2);check("all queue memory released after drain",h.queue.stats()["pendingBytes"]==0&&h.queue.stats()["pendingRows"]==0);
}
{
 Limits l;l.queueRows=3;Harness h(l);h.set(true);for(int n=0;n<100;++n)h.send(n*1000);check("row cap is strict during a stalled executor",h.queue.stats()["pendingRows"]==3&&h.jobs.size()==1);check("queue-full drops are visible",h.queue.stats()["droppedQueueFull"]==97);h.run();check("stall recovery writes only bounded retained rows",h.written.size()==3);
}
{
 Limits l;l.queueRows=16;l.queueBytes=80;Harness h(l);h.set(true);for(int n=0;n<10;++n)h.send(n*1000,std::string(24,'x'));check("memory cap accounts string capacity, not just payload length",h.queue.stats()["pendingBytes"].get<size_t>()<=80&&h.queue.stats()["droppedQueueFull"].get<size_t>()>0);
 auto full=h.queue.stats();const auto sample=h.queue.stats(false);
 const json limits={{"sampleIntervalMs",1000},{"recordMaxBytes",32768},{"queueMaxBytes",80},{"queueMaxRows",16},{"fileMaxBytes",2097152},{"retainedFileCount",2}};
 bool fullLimits=true;bool sampleOmitsLimits=true;
 for(const auto& [key,value]:limits.items()){fullLimits=fullLimits&&full.contains(key)&&full.at(key)==value;sampleOmitsLimits=sampleOmitsLimits&&!sample.contains(key);full.erase(key);}
 check("full stats retain actual queue limits, including a configured override",fullLimits);
 check("sample stats omit only fixed limits, keeping pending rows and queue drops",sampleOmitsLimits&&sample==full&&sample.at("pendingRows").get<size_t>()>0&&sample.at("droppedQueueFull").get<size_t>()>0);
 h.run();check("byte budget returns to zero",h.queue.stats()["pendingBytes"]==0);
 Harness huge(l);huge.set(true);std::string reserved="{}";reserved.reserve(1024*1024);check("a small row cannot smuggle an oversized reserved buffer into the queue",!huge.send(0,std::move(reserved))&&huge.queue.stats()["pendingBytes"]==0);
}
{
 Limits l;l.recordBytes=32;Harness h(l);h.set(true);check("oversized row is rejected without a pool job",!h.send(0,std::string(32,'x'))&&h.jobs.empty()&&h.queue.stats()["droppedOversize"]==1);
}
{
 Harness h;h.set(true);h.send(0);h.send(1000);const auto old=h.queue.permit(2000);h.set(false);h.set(true);check("an in-progress encoder from an earlier gate epoch cannot re-enter",old&&!h.queue.offer("{}",*old));h.run();check("off-on does not replay queued pre-off samples",h.written.empty()&&h.queue.stats()["droppedGate"]==3);check("off edge closes on executor only",h.closes==1);h.send(3000);h.run();check("fresh records work after re-enable",h.written.size()==1);
}
{
 Harness h;h.set(true);h.reject=true;h.send(0);check("pool rejection is bounded and observable",h.queue.stats()["pendingRows"]==1&&h.queue.stats()["submitFailures"]==1&&h.jobs.empty());h.reject=false;h.send(1000);h.run();check("a later allowed sample recovers a failed schedule",h.written.size()==2&&h.queue.stats()["pendingRows"]==0);
}
{
 Harness h;h.set(true);h.send(0);check("export drains and flushes on its caller worker",h.written.empty());h.queue.flushForExport();check("export includes the pending partial batch",h.written.size()==1&&h.flushes==1);h.send(1000);check("export cannot duplicate the already queued pool job",h.jobs.size()==1);h.run();check("queued worker after export does not double-write",h.written.size()==2);
}
{
 Harness h;h.set(true);h.send(0);h.queue.requestClose();check("idle close drains pending records on the executor",h.written.empty()&&h.closes==0);h.run();check("idle close preserves the detailed gate for later samples",h.written.size()==1&&h.closes==1&&h.send(1000));h.run();check("logging resumes after an idle batch close",h.written.size()==2);
}
{
 std::promise<void> entered,release;auto releaseFuture=release.get_future().share();std::function<void()>job;bool enabled=true;Limits l;l.queueRows=2;
 Queue q({[&]{return enabled;},[&](auto f){job=std::move(f);return true;},[&](auto){entered.set_value();releaseFuture.wait();return true;},[]{return true;},[]{}},l);
 auto p=q.permit(0);q.offer("{}",*p);auto sink=std::async(std::launch::async,[&]{job();});entered.get_future().wait();
 auto producer=std::async(std::launch::async,[&]{auto p1=q.permit(1000);const bool accepted=q.offer("{}",*p1);auto p2=q.permit(2000);const bool refused=!p2 || !q.offer("{}",*p2);return accepted&&refused;});
 const bool producerFree=producer.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;check("blocked disk sink does not block the monitor producer",producerFree);if(producerFree)check("in-flight row remains charged against the row budget",producer.get());
 // Switch gate off before releasing, so the second queued row is discarded.
 enabled=false;q.gateChanged(false);release.set_value();sink.get();if(!producerFree)producer.get();check("stalled-write recovery releases memory and drops retired samples",q.stats()["pendingRows"]==0&&q.stats()["pendingBytes"]==0);
}
{
 bool on=true;std::vector<std::function<void()>>jobs;Queue q({[&]{return on;},[&](auto f){jobs.push_back(std::move(f));return true;},[](auto){return false;},[]{return true;},[]{}});auto p=q.permit(0);q.offer("{}",*p);jobs.front()();check("I/O failure drops diagnostics and releases its entire budget",q.stats()["droppedIo"]==1&&q.stats()["pendingBytes"]==0);
}
{
 using namespace ymcc::logexport;
 const auto source=root/"monitor-detail.log", target=root/"monitor-staged.log";
 const auto bytes=[](const auto& path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 const std::string oldRow="{\"value\":\""+std::string(72,'a')+"\"}", newRow="{\"value\":\""+std::string(72,'b')+"\"}";
 Limits l;l.fileBytes=128;l.recordBytes=100;l.flushRows=16;l.flushMs=5000;
 StrictFile file([&]{return source;},l);
 std::promise<void> insideWrite, releaseWrite, captureRequested;
 auto release=releaseWrite.get_future();
 Queue q({[]{return true;},[](auto){return true;},[&](std::string_view row){
     if(row==oldRow){insideWrite.set_value();release.wait();}
     return file.append(row,0);
 },[&]{return file.flush();},[&]{file.close();}},l);
 auto first=q.permit(0);check("monitor row queues before concurrent export",first&&q.offer(oldRow,*first));
 auto writer=std::async(std::launch::async,[&]{q.drain();});insideWrite.get_future().wait();
 bool rotatedBeforeCopy=false;
 const SnapshotSourceSync sync=[&](const auto& capture){
     captureRequested.set_value();
     const bool flushed=q.withExportSourceBoundary(capture);
     auto second=q.permit(1000);
     const bool queued=second&&q.offer(newRow,*second);
     auto rotation=std::async(std::launch::async,[&]{q.flushForExport();});rotation.get();
     rotatedBeforeCopy=queued&&file.rotations()==1&&bytes(source)==newRow+"\n";
     return flushed;
 };
 SnapshotCopyEvidence observed;DWORD error=0;
 auto exporting=std::async(std::launch::async,[&]{return snapshotSharedLog(source,target,error,0,&observed,&sync);});
 captureRequested.get_future().wait();
 check("export waits for current writer before opening the file",exporting.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout);
 releaseWrite.set_value();writer.get();const bool copied=exporting.get();
 check("local writer rotates with a live export handle after the lock is released",rotatedBeforeCopy);
 check("snapshot reads the flushed old file through rotation, never the new active file",copied&&error==0&&bytes(target)==oldRow+"\n"&&observed.sourceBytesAtOpen==oldRow.size()+1&&snapshotCopyEvidence(observed)["monitorWriterBoundary"]=="captured-under-writer-lock");
 file.close();
 const auto prior=root/"prior-tail.log", priorTarget=root/"prior-tail-staged.log";
 {std::ofstream out(prior,std::ios::binary);out<<"{}\n{";}
 const SnapshotSourceSync noWrite=[&](const auto& capture){return q.withExportSourceBoundary(capture);};
 check("synchronized export retains existing partial tail byte for byte",snapshotSharedLog(prior,priorTarget,error,0,&observed,&noWrite)&&bytes(prior)=="{}\n{"&&bytes(priorTarget)==bytes(prior)&&stagedFileEvidence(priorTarget,"monitor-detail.log")["partialOrInvalidTrailingRecord"]==true);
 Queue failing({[]{return false;},[](auto){return false;},[](auto){return false;},[]{return false;},[]{}});
 const SnapshotSourceSync failedFlush=[&](const auto& capture){return failing.withExportSourceBoundary(capture);};
 check("flush failure preserves the normal best-effort copy and is reported",snapshotSharedLog(prior,root/"flush-failed.log",error,0,&observed,&failedFlush)&&bytes(root/"flush-failed.log")==bytes(prior)&&snapshotCopyEvidence(observed)["monitorWriterBoundary"]=="flush-failed");
 check("open failure retains the original Win32 error without creating a destination",!snapshotSharedLog(root/"missing.log",root/"missing-staged.log",error,0,&observed,&noWrite)&&error==ERROR_FILE_NOT_FOUND&&!std::filesystem::exists(root/"missing-staged.log")&&snapshotCopyEvidence(observed)["monitorWriterBoundary"]=="source-open-failed");
 check("unrelated shared copies retain their original observation shape",snapshotSharedLog(prior,root/"unrelated-staged.log",error,0,&observed)&&bytes(root/"unrelated-staged.log")==bytes(prior)&&!snapshotCopyEvidence(observed).contains("monitorWriterBoundary"));
}
{
 Limits l;l.fileBytes=256;l.recordBytes=128;l.flushRows=4;l.flushMs=5000;const auto path=root/"rotation.log";StrictFile file([&]{return path;},l);bool bounded=true;
 for(int n=0;n<100;++n){const auto line=json({{"seq",n},{"value",std::string(20,'v')}}).dump();bounded=bounded&&file.append(line,n*1000);file.flush();for(const auto& p:{path,std::filesystem::path(path.wstring()+L".1")})if(std::filesystem::exists(p))bounded=bounded&&std::filesystem::file_size(p)<=256;}
 file.close();check("active+one backup rotate before either file crosses its hard cap",bounded&&file.rotations()>0);size_t files=0,bytes=0;bool jsonl=true;for(const auto& p:{path,std::filesystem::path(path.wstring()+L".1")})if(std::filesystem::exists(p)){++files;bytes+=std::filesystem::file_size(p);std::ifstream in(p);std::string line;while(std::getline(in,line))jsonl=jsonl&&!json::parse(line,nullptr,false).is_discarded();}
 check("rotation retains exactly two bounded files with intact JSON rows",files==2&&bytes<=512&&jsonl);
}
{
 Limits l;l.fileBytes=128;l.recordBytes=64;const auto path=root/"rotation-blocked.log";{std::ofstream out(path);out<<std::string(120,'x');}auto backup=path;backup+=L".1";std::filesystem::create_directories(backup);{std::ofstream out(backup/"owned-obstacle");out<<"fixture";}
 StrictFile file([&]{return path;},l);bool refused=true;for(int n=0;n<10;++n)refused=refused&&!file.append("{\"sample\":1}",n*1000);file.close();check("failed backup replacement never permits unbounded appends",refused&&std::filesystem::file_size(path)==120&&file.failures()>0);auto failureFacts=file.failureEvidence();check("bounded first-last IO evidence retains real stage/code without paths",failureFacts["observed"]==true&&failureFacts["first"]["tick"]==0&&failureFacts["last"]["tick"]==9000&&failureFacts["first"]["stage"]=="active-rename"&&failureFacts["last"]["errorCodeObserved"]==true&&failureFacts["sourcePathsIncluded"]==false);
}
{
 // ACTIVE_RENAME_RETENTION_CONTROL_BEGIN
 Limits l;l.fileBytes=128;l.recordBytes=64;const auto path=root/"active-sharing-refusal.log";
 auto backup=path;backup+=L".1";const std::string activeBytes(120,'a'),backupBytes="{\"previous\":true}\n";
 {std::ofstream file(path,std::ios::binary);file<<activeBytes;}{std::ofstream file(backup,std::ios::binary);file<<backupBytes;}
 HANDLE reader=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 StrictFile file([&]{return path;},l);const bool refused=!file.append("{\"sample\":1}",1000);file.close();
 const auto read=[](const auto& p){std::ifstream in(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 check("active rename sharing refusal preserves both original files",reader!=INVALID_HANDLE_VALUE&&refused&&read(path)==activeBytes&&read(backup)==backupBytes&&file.rotations()==0);
 auto evidence=file.failureEvidence();check("sharing failure remains explicit without claiming a rotation",evidence["first"]["stage"]=="active-rename"&&evidence["first"]["errorCode"]==ERROR_SHARING_VIOLATION&&file.failures()==1);
 if(reader!=INVALID_HANDLE_VALUE)CloseHandle(reader);
 check("next permitted sample replaces backup only after sharing lock released",file.append("{\"sample\":1}",2000));file.close();
 check("successful replacement retains prior active bytes as backup",read(backup)==activeBytes&&read(path)=="{\"sample\":1}\n"&&file.rotations()==1);
 // ACTIVE_RENAME_RETENTION_CONTROL_END
}
{
 Limits l;l.fileBytes=128;l.recordBytes=64;const auto path=root/"partial-tail.log";{std::ofstream out(path);out<<"partial";}StrictFile file([&]{return path;},l);
 const auto read=[](const auto& p){std::ifstream in(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 check("unopened writer reports no separator observation",file.tailSeparatorEvidence()==json{{"schemaVersion",1},{"accepted",0},{"last",nullptr}});
 check("interrupted previous tail is separated from the new JSON record",file.append("{\"new\":1}",1000));file.close();
 check("tail repair preserves original bytes and remains capped",read(path)=="partial\n{\"new\":1}\n"&&std::filesystem::file_size(path)<=128);
 const auto observed=file.tailSeparatorEvidence();
 check("receipt locates the exact inserted LF in the original bytes",observed==json{{"schemaVersion",1},{"accepted",1},{"last",{{"tick",1000},{"offsetBytes",7}}}}&&read(path).at(7)=='\n');
 check("reopening an already LF-terminated file does not report a separator",file.append("{}",2000)&&file.tailSeparatorEvidence()==observed);file.close();
 {std::ofstream out(path,std::ios::binary|std::ios::app);out<<"{";}
 const auto offset=std::filesystem::file_size(path);
 const bool appended=file.append("{}",3000);file.close();
 check("another non-LF tail advances the count and replaces the bounded last receipt",appended&&file.tailSeparatorEvidence()==json{{"schemaVersion",1},{"accepted",2},{"last",{{"tick",3000},{"offsetBytes",offset}}}}&&read(path).substr(static_cast<std::size_t>(offset)-1)=="{\n{}\n");
}
{
 Limits l;l.fileBytes=9;l.recordBytes=8;const auto path=root/"partial-tail-rotate.log";
 {std::ofstream out(path,std::ios::binary);out<<"broken";}
 StrictFile file([&]{return path;},l);const bool appended=file.append("{}",1000);file.close();
 auto backup=path;backup+=L".1";std::ifstream in(backup,std::ios::binary);const std::string old(std::istreambuf_iterator<char>(in),{});
 check("rotation with no room for the LF preserves the old tail without reporting insertion",appended&&file.rotations()==1&&old=="broken"&&file.tailSeparatorEvidence()["last"].is_null()&&file.tailSeparatorEvidence()["accepted"]==0);
}
{
 Limits l;l.flushRows=16;l.flushMs=5000;const auto path=root/"batched.log";StrictFile file([&]{return path;},l);for(int n=0;n<5;++n)file.append("{}",n*1000);check("1Hz detailed samples are buffered instead of flushing each row",file.flushes()==0);file.append("{}",5000);check("time batch flushes after five seconds",file.flushes()==1);file.close();
}
{
 const json cache={{"available",true},{"collectedAt",1000},{"session",123},{"generation",4},{"seq",9},{"top","{\"tdpW\":30}"},{"fps","{\"fps\":120}"}};auto row=snapshotRecord(cache,2000,7);check("detail snapshots retain original inner timestamps/text without fabrication",row["snapshot"]["top"]==cache["top"]&&row["snapshot"]["collectedAt"]==1000&&row["observedAtMs"]==2000);check("periodic snapshots use the high-frequency monitor lane",row["lane"]=="high-frequency"&&row["event"]=="monitor-data-snapshot"&&row["payloadMaxBytes"]==12288);
 auto giant=cache;giant["top"]=std::string(1024*1024,'x');row=snapshotRecord(giant,2000,7);check("huge raw snapshots become bounded complete omission records",row["snapshot"]["rawPayloadOmitted"]==true&&row.dump().size()<32768&&!row["snapshot"].contains("top"));
 json settings={{"rtss",{{"monitorOutput",{{"enabled",true},{"output","ymcc"},{"ymcc",{{"layout","capsule"},{"fields",{{"fps",true},{"cpuTemp",true}}},{"underlayImage","data:image/png;base64,"+std::string(3*1024*1024,'A')}}}}}}}};
 const auto summary=ymcc::monitorexport::snapshot(cache,{{"processor","CPU"}},settings,2000,"parsed");const auto encoded=ymcc::monitorexport::encode(summary);
 check("export metadata does not duplicate raw monitoring snapshots",!summary["snapshot"].contains("top")&&summary["snapshot"]["topPresent"]==true&&summary["rawSnapshotIncluded"]==false);
 check("imported multi-megabyte background bytes never enter the summary",summary["savedMonitorPreferences"]["ymcc"]["underlayImageOmitted"]==true&&encoded.find("data:image/png;base64,")==std::string::npos&&encoded.size()+1<=ymcc::monitorexport::maxSummaryBytes);
 check("export keeps active field switches and style without images",summary["savedMonitorPreferences"]["ymcc"]["fields"]["fps"]==true&&summary["savedMonitorPreferences"]["ymcc"]["layout"]=="capsule");
 auto invalidFields=settings;auto& badFields=invalidFields["rtss"]["monitorOutput"]["ymcc"]["fields"];badFields=json::object();for(int n=0;n<200;++n)badFields["!invalid"+std::to_string(n)]=true;badFields[std::string(1024*1024,'z')]=true;badFields["zzValidBeyondBudget"]=true;const auto scanned=ymcc::monitorexport::snapshot(cache,json::object(),invalidFields,2000,"parsed");check("unknown field scans and key lengths are bounded",scanned["savedMonitorPreferences"]["ymcc"]["fields"].empty());
 auto oversized=summary;oversized["forged"]=std::string(1024*1024,'x');const auto fallback=ymcc::monitorexport::encode(oversized);check("final export cap yields complete JSON rather than byte-truncated JSON",fallback.size()+1<=32768&&!json::parse(fallback,nullptr,false).is_discarded()&&json::parse(fallback)["summaryOmitted"]==true);
 check("export inspector recognizes detailed monitor file and its rotation as JSONL",ymcc::logexport::jsonLinesEvidenceName("monitor-detail.log")&&ymcc::logexport::jsonLinesEvidenceName("monitor-detail.log.1"));
}
json result={{"ok",failures==0},{"cases",checks},{"failures",failures},{"hardwareReads",false},{"installedFilesChanged",false}};std::ofstream(root/"DETAIL-BOUNDS-RESULT.json")<<result.dump(2);std::cout<<"Detailed monitor: "<<checks.size()<<" cases, "<<failures<<" failed\n";return failures?2:0;
}
`;
fs.writeFileSync(out+'/detail-bounds.cpp',native);
const cmd=calibrationToolchainPreamble("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")+`cl /nologo /std:c++20 /utf-8 /EHsc /W4 "${out}/detail-bounds.cpp" /I"${root}/native" /I"${root}/deps/json" /Fo"${out}/detail-bounds.obj" /Fe"${out}/detail-bounds.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/detail-bounds.exe" "${out}/fixtures"\r\nexit /b %errorlevel%\r\n`;fs.writeFileSync(out+'/build-detail-bounds.cmd',cmd);
const run=spawnSync('cmd.exe',['/d','/c',out+'/build-detail-bounds.cmd'],{cwd:out,encoding:'utf8',timeout:45000,maxBuffer:3e6});fs.writeFileSync(out+'/DETAIL-BOUNDS-BUILD.log',(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-4000));if(run.status!==0){process.exitCode=2;}
