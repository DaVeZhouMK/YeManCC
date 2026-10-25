// Actual frozen/current native parser + publisher, using original archived
// HWiNFO rows and synthetic boundaries only. No mappings, app, or hardware IO.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd(),task=path.resolve(process.env.YMCC_AMD_USED_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/amd-d3d-used-source-20261016'),out=task+'/native';fs.mkdirSync(out,{recursive:true});
const pins=Object.fromEntries(['native/monitor_gpu_selection.h','native/monitor_memory_network.h','native/monitor_gpu_memory_evidence.h'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')]));
fs.mkdirSync(task+'/native-after',{recursive:true});
const generation=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{encoding:'utf8',env:{...process.env,YMCC_MONITOR_MEMORY_OUT:task+'/native-after'}});assert.equal(generation.status,0,generation.stderr);
const harness=String.raw`
#include <cstdlib>
#include <new>
static bool countMemoryAllocations=false,failMemoryAllocation=false;
static unsigned memoryAllocations=0;
void* operator new(std::size_t n){if(failMemoryAllocation){failMemoryAllocation=false;throw std::bad_alloc();}if(countMemoryAllocations)++memoryAllocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static unsigned failures=0;static json checks=json::array(),cases=json::object();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok){++failures;std::cout<<"FAIL "<<name<<"\n";}}
static NativeMonitorHw observed(const std::vector<Row>& rows,const std::vector<std::pair<uint32_t,std::string>>& names,bool diagnostic=false){
 auto data=buffer(rows);constexpr uint32_t so=48,ss=268;
 for(const auto& [index,name]:names){assert(index<32);auto* p=data.data()+so+index*ss+8;std::memset(p,0,128);std::memcpy(p,name.data(),(std::min)(std::size_t(127),name.size()));}
 NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(data.data(),data.size(),hw,unstable,diagnostic)&&!unstable);return hw;
}
static json published(const NativeMonitorHw& hw,bool healthy=true){return json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,0,1700000000000LL,healthy,json::array(),0,{},0,0,{},{},{},{}));}
static bool usedIs(const json& p,double value){return p["gpuMemoryUsedMb"].is_number()&&p["gpuMemoryUsedMb"].get<double>()==value;}
static void unknownCapacity(const json& p){check("dedicated fallback never invents capacity or load",p["gpuMemoryTotalMb"].is_null()&&p["gpuMemoryAvailableMb"].is_null()&&p["gpuMemoryLoadPct"].is_null());}
int main(int argc,char**argv){assert(argc==3);const bool modern=MODERN!=0;json local;std::ifstream(argv[2])>>local;
 std::vector<std::string> labels,units;labels.reserve(local["rows"].size());units.reserve(local["rows"].size());
 std::vector<Row> amdRows,nvRows;std::string amdName,nvName;
 for(const auto& r:local["rows"]){const auto group=r.value("group",std::string());if(group.find("AMD Radeon")==std::string::npos&&group.find("NVIDIA")==std::string::npos)continue;labels.push_back(r["name"].get<std::string>());units.push_back(r["unit"].get<std::string>());const auto value=r["value"].is_number()?r["value"].get<double>():-1;
  if(group.find("AMD Radeon")!=std::string::npos){amdName=group;amdRows.push_back({labels.back().c_str(),units.back().c_str(),value,1});}else{nvName=group;nvRows.push_back({labels.back().c_str(),units.back().c_str(),value,2});}}
 const std::vector<std::pair<uint32_t,std::string>> names={{1,amdName},{2,nvName}};
 auto localAmd=observed(amdRows,names);auto p=published(localAmd);cases["local-AMD-original-rows"]=p;
 check("archived AMD labels reproduce the intended before/current source",usedIs(p,modern?14.734375:100));unknownCapacity(p);
 if(modern)check("AMD fallback provenance is explicit",p["gpuMemorySource"]=="hwinfo-d3d-dedicated");
 auto localNv=observed(nvRows,names);auto np=published(localNv);cases["local-NVIDIA-original-rows"]=np;check("NVIDIA keeps its coherent allocated/available tuple",usedIs(np,5723.859375)&&np["gpuMemoryTotalMb"]==16303);
 auto sample=[&](double driver,double d3d,const char* unit="MB"){return std::vector<Row>{{"GPU Memory Usage","MB",driver,1},{"GPU Clock","MHz",600,1},{"GPU D3D Memory Dedicated",unit,d3d,1},{"GPU D3D Memory Dynamic","MB",3000,1}};};
 auto synthetic=observed(sample(29803,3072),names);p=published(synthetic);cases["synthetic-large-driver-allocation"]=p;check("oversized partial driver allocation cannot masquerade as dedicated usage",usedIs(p,modern?3072:29803));unknownCapacity(p);
 auto zero=observed(sample(29803,0),names);p=published(zero);cases["dedicated-real-zero"]=p;check("a genuine dedicated zero is preserved",usedIs(p,modern?0:29803));
 for(const double invalid:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){p=published(observed(sample(29803,invalid),names));check("invalid dedicated counter uses shared rather than the generic driver allocation",modern?usedIs(p,3000):usedIs(p,29803));}
 p=published(observed({{"GPU Memory Usage","MB",29803,1},{"GPU Clock","MHz",600,1}},names));cases["dedicated-missing"]=p;check("missing both D3D counters remains unknown on affected AMD",modern?p["gpuMemoryUsedMb"].is_null():usedIs(p,29803));
 p=published(observed(sample(29803,80,"%"),names));check("percentage is not a dedicated amount; shared fallback applies",modern?usedIs(p,3000):usedIs(p,29803));
 p=published(observed(sample(29803,3,"GB"),names));check("dedicated amount conversion uses existing MiB convention",usedIs(p,modern?3072:29803));
 auto complete=sample(1024,512);complete.push_back({"GPU Memory Available","MB",3072,1});p=published(observed(complete,names));cases["complete-AMD-tuple"]=p;check("AMD dedicated priority applies before an unrelated driver capacity tuple",modern?(usedIs(p,512)&&p["gpuMemoryTotalMb"].is_null()&&p["gpuMemoryLoadPct"].is_null()):(usedIs(p,1024)&&p["gpuMemoryTotalMb"]==4096&&p["gpuMemoryLoadPct"]==25));
 auto direct=sample(100,14);direct.push_back({"GPU Memory Usage","%",40,1});p=published(observed(direct,names));check("AMD dedicated source does not inherit a driver percentage",modern?(usedIs(p,14)&&p["gpuMemoryLoadPct"].is_null()):(usedIs(p,100)&&p["gpuMemoryLoadPct"]==40));
 auto totalOnly=sample(-1,512);totalOnly.push_back({"GPU Memory Total","MB",4096,1});p=published(observed(totalOnly,names));check("D3D allocation is not fused into a differently sourced known total",modern?(usedIs(p,512)&&p["gpuMemoryTotalMb"].is_null()):(p["gpuMemoryUsedMb"].is_null()&&p["gpuMemoryTotalMb"]==4096));
 for(const auto* name:{"dGPU [#0]: NVIDIA GeForce RTX","iGPU [#0]: Intel Arc","iGPU [#0]","iGPU [#0]: AMD NVIDIA alias"}){p=published(observed(sample(100,14),{{1,name}}));check("other/ambiguous vendors keep their old partial contract",usedIs(p,100));}
 p=published(observed({{"GPU D3D Memory Dedicated","MB",4096,1},{"GPU D3D Memory Dynamic","MB",12000,1}},names));check("D3D-only rows never create a selectable GPU",p["gpuSelectionPresent"]==false&&p["gpuMemoryUsedMb"].is_null());
 p=published(localAmd,false);cases["hardware-down"]=p;check("hardware-down never publishes an old dedicated observation",p["gpuMemoryUsedMb"].is_null()&&p["gpuMemoryTotalMb"].is_null());if(modern)check("hardware-down clears memory provenance",p["gpuMemorySource"]=="unavailable");
 auto first=observed(sample(100,14),names),second=observed(sample(100,28),names);check("immutable old packets cannot change with a newer value payload",usedIs(published(first),modern?14:100)&&usedIs(published(second),modern?28:100));
 auto both=amdRows;both.insert(both.end(),nvRows.begin(),nvRows.end());both.push_back({"GPU Power","W",25,1});both.push_back({"GPU Power","W",180,2});auto mixed=observed(both,names);ymcc::monitorgpu::StickySelection latch;mixed.useGpu(latch.select(mixed.gpuReadings,true));p=published(mixed);cases["mixed-power-selected-NVIDIA"]=p;check("the memory fallback cannot change power-based GPU selection",p["gpuName"].get<std::string>().find("NVIDIA")!=std::string::npos&&usedIs(p,5723.859375));
 // Warm parser scratch before counting: no JSON construction is in the window.
 auto countRows=sample(100,901.125);auto countData=buffer(countRows);std::memcpy(countData.data()+48+268+8,"iGPU [#1]: AMD Radeon",21);bool unstable=false;NativeMonitorHw warm;monitorParseHWiNFOView(countData.data(),countData.size(),warm,unstable,false);
 // A different real value ensures that the first measured modern read is cold.
 const double uniqueValue=902.125;std::memcpy(countData.data()+(48+32*268)+2*316+284,&uniqueValue,8);
 memoryAllocations=0;countMemoryAllocations=true;for(unsigned i=0;i<200;++i){NativeMonitorHw hw;const bool ok=monitorParseHWiNFOView(countData.data(),countData.size(),hw,unstable,false);if(!ok)std::abort();}countMemoryAllocations=false;
 const auto counted=memoryAllocations;check("bounded payload allocation count matches the tested implementation",counted==(modern?(CACHE_ENABLED?1u:200u):0u));
 // Newer equal values are still owned by their current packet, never old time.
#if MODERN
 // Equal cached numeric payloads belong to each current parser observation.
 auto firstStamp=observed(sample(100,10),names);auto equalData=buffer(sample(100,10));
 const auto ownerName=std::string("iGPU [#1]: AMD Radeon current-owner");std::memcpy(equalData.data()+48+268+8,ownerName.c_str(),ownerName.size()+1);
 const auto laterStamp=firstStamp.producerPollTime+1;put(equalData,12,laterStamp);NativeMonitorHw equalStamp;
 memoryAllocations=0;countMemoryAllocations=true;const bool equalAccepted=monitorParseHWiNFOView(equalData.data(),equalData.size(),equalStamp,unstable,false);countMemoryAllocations=false;
 check("numeric payload reuse cannot carry an old producer timestamp or GPU identity",equalAccepted&&firstStamp.producerPollTime!=equalStamp.producerPollTime&&equalStamp.producerPollTime==laterStamp&&equalStamp.gpuReadings.best()->key.deviceName()=="AMD Radeon current-owner"&&usedIs(published(equalStamp),10));
 check("equal numeric payload with current identity reuses only its bounded value block",memoryAllocations==(CACHE_ENABLED?0u:1u));
 auto oldData=observed(sample(100,10),names);auto changedData=observed(sample(100,11),names);check("a changed value never aliases the cached old value",usedIs(published(oldData),10)&&usedIs(published(changedData),11));
 auto failureData=buffer(sample(100,12));std::memcpy(failureData.data()+48+268+8,"iGPU [#1]: AMD Radeon",21);NativeMonitorHw failed;failMemoryAllocation=true;const bool survived=monitorParseHWiNFOView(failureData.data(),failureData.size(),failed,unstable,false);check("allocation failure cannot break parsing or restore bogus driver usage",survived&&published(failed)["gpuMemoryUsedMb"].is_null());
 check("raw driver value remains available for diagnostics",failed.gpuReadings.best()->memory.used==100);
 auto recovered=observed(sample(100,12),names);check("next valid observation recovers after an allocation failure",usedIs(published(recovered),12));
#endif
 json result={{"ok",failures==0},{"checks",checks},{"cases",cases},{"twoHundredParserPayloadAllocations",counted},{"hardwareReads",false},{"newPollingTimers",false},{"source","actual-parser-and-publisher; archived-original-rows-plus-labelled-synthetic-boundaries"}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"AMD used source: "<<checks.size()<<" checks, "<<failures<<" failed; allocations="<<counted<<"\n";return failures?2:0;
}
`;
const results={};
for(const side of ['before','uncached','after']){
 let prefix=fs.readFileSync(task+(side==='before'?'/native-before':'/native-after')+'/memory-publication.cpp','utf8');prefix=prefix.slice(0,prefix.indexOf('int main(int argc,char**argv)'));
 if(side==='uncached'){
  const header=fs.readFileSync('native/monitor_gpu_selection.h','utf8'),mark='if(cached&&cachedCount==count_&&std::memcmp';assert.equal(header.split(mark).length,2);
  fs.writeFileSync(out+'/uncached-monitor-gpu.h',header.replace(mark,'if(false&&cached&&cachedCount==count_&&std::memcmp'));
  prefix=prefix.replace(root.replaceAll('\\','/')+'/native/monitor_gpu_selection.h',out.replaceAll('\\','/')+'/uncached-monitor-gpu.h');
 }
 const file=out+'/'+side;fs.writeFileSync(file+'.cpp',prefix+'\n#define MODERN '+(side==='before'?0:1)+'\n#define CACHE_ENABLED '+(side==='uncached'?0:1)+'\n'+harness);
 fs.writeFileSync(file+'.cmd',`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /I"${root}/native" "${file}.cpp" /Fo"${file}.obj" /Fe"${file}.exe"\r\nif errorlevel 1 exit /b 1\r\n"${file}.exe" "${file}.json" "${task}/LOCAL-ORIGINAL-ROWS.json"\r\nexit /b %errorlevel%\r\n`);
 const run=spawnSync('cmd.exe',['/d','/c',file+'.cmd'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(file+'.log',(run.stdout||'')+(run.stderr||''));console.log(side,((run.stdout||'')+(run.stderr||'')).slice(-3500));assert.equal(run.status,0,'native '+side+' failed');results[side]=JSON.parse(fs.readFileSync(file+'.json','utf8'));assert(results[side].ok);
}
assert.deepEqual(results.after.cases,results.uncached.cases,'cache must never change the corrected values');assert.equal(results.before.cases['local-AMD-original-rows'].gpuMemoryUsedMb,100);assert.equal(results.after.cases['local-AMD-original-rows'].gpuMemoryUsedMb,14.734375);
const work=Object.fromEntries(Object.entries(results).map(([name,r])=>[name,r.twoHundredParserPayloadAllocations]));for(const [f,pin]of Object.entries(pins))assert.equal(crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex'),pin,'changed tested header '+f);
const result={ok:true,nativeChecks:Object.fromEntries(Object.entries(results).map(([k,r])=>[k,r.checks.length])),work,sourcePins:pins,localAMD:{before:100,after:14.734375,unit:'MiB',scope:'reencoded archived original readings, not a new live sample'},vramPercentageFixed:false};fs.writeFileSync(task+'/NATIVE-RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
