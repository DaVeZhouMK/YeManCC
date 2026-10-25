// Compare frozen/current complete parsers and consumers of copied HWiNFO rows.
// Deterministic classification work only, no live hardware or timing benchmark.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_FOLD_LENGTH_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/folded-label-length-20261016').replaceAll('\\','/'),out=task+'/native';fs.mkdirSync(out,{recursive:true});fs.mkdirSync(task+'/parser',{recursive:true});
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j);};
const base=fs.readFileSync('tools/monitor_gpu_active_slots_selftest.mjs','utf8').replaceAll('\r\n','\n');let harness=cut(base,'const harness=String.raw`','\n`;\nconst results=').slice('const harness=String.raw`'.length).replace('constexpr bool modern=MODERN;','constexpr bool modern=true;');
fs.copyFileSync('tools/fixtures/monitor_label_rows_20261016.json',task+'/ORIGINAL-ROWS.json');
const beforeFold=fs.existsSync(task+'/before/native/monitor_text_fold.h')?task+'/before/native/monitor_text_fold.h':root+'/tools/fixtures/monitor_text_fold_before_20261016.h';
const gpuOld=fs.readFileSync('native/monitor_gpu_selection.h','utf8'),memoryOld=fs.readFileSync('native/monitor_memory_network.h','utf8');const families=[...new Set([...gpuOld.matchAll(/name=="([^"\n]+)"/g),...memoryOld.matchAll(/name=="([^"\n]+)"/g)].map(m=>m[1]))];
const names=[...new Set([...families,...families.map(n=>n.toUpperCase()),...families.map(n=>n+' extra'),...families.map(n=>'prefix '+n),'','GPU','GPU power limit','GPU Power Rated','GPU Power maximum','GPU Power capability','GPU Power TGP','GPU Power TBP','GPU powers','Total Graphics Power','CPU Package Power','Core 0 Usage','Drive Write Activity','Physical Memory','Physical memory-used','Current DL rateX','Current DL rate\0ignored','GPU Power\0limit','gPu MeMoRy AlLoCaTeD','\u00e9GPU Power','GPU \u00e9Power'])];
const units=['W','watts','MB','MiB','GB','%','V','mV','C','MHz','KB/s','MB/s','Mb/s','bad',''];fs.writeFileSync(task+'/CORPUS.json',JSON.stringify({names,units,scope:'All exact GPU and memory/network family names from frozen source plus case/prefix/suffix/exclusion/unit negatives; synthetic strings.'},null,2));
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{encoding:'utf8',env:{...process.env,YMCC_MONITOR_MEMORY_OUT:task+'/parser'}});assert.equal(gen.status,0,gen.stderr);const generated=fs.readFileSync(task+'/parser/memory-publication.cpp','utf8');
const added=String.raw`
 json corpusCases=json::object();std::ifstream corpusIn(argv[3]);json corpus;corpusIn>>corpus;std::size_t corpusIndex=0;
 const auto memoryValue=[](ymcc::monitormemory::Memory m){return json::array({m.used,m.available,m.total,m.load});};
 for(const auto& n:corpus["names"])for(const auto& u:corpus["units"]){
  const std::string name=n,unit=u;
  for(unsigned mode=0;mode<4;++mode){
   const double value=mode==0?50:mode==1?0:mode==2?-1:std::numeric_limits<double>::quiet_NaN();const bool readable=mode!=3;
   const MonitorFoldedText low{std::string_view(name)},unitLow{std::string_view(unit)};
   ymcc::monitorgpu::Readings g;const bool consumedGpu=g.add(2,low,unitLow,value,readable,identity);
   ymcc::monitormemory::Readings memory(true,4096);const bool consumedMemory=memory.add(1,low,unitLow,value,readable,unit);
   ymcc::monitormemory::Memory ram,vram;double down=-1,up=-1;memory.finish(ram,vram,down,up,[](uint32_t i){return i;});const auto d3d=memory.diagnosticD3D(1);
   json direct={{"gpuConsumed",consumedGpu},{"memoryConsumed",consumedMemory},{"gpus",all(g)},{"ram",memoryValue(ram)},{"vram",memoryValue(vram)},{"network",{down,up}},{"d3d",{d3d.dedicated,d3d.dynamic}}};
   if(mode==0){auto b=buffer({{name,unit,value,2}});NativeMonitorHw h;assert(parse(b,h));direct["top"]=top(h);direct["native"]=all(h.gpuReadings);}
   corpusCases[std::to_string(corpusIndex++)]=std::move(direct);
  }
 }
 check("complete exact-label/variant/unit/invalid-value corpus runs",corpusIndex>5000);
 json gateCases=json::array();
 for(uint32_t group:std::vector<uint32_t>{0,4095,4096})for(unsigned demand=0;demand<4;++demand){const MonitorFoldedText n{std::string_view("GPU D3D Memory Dedicated")},u{std::string_view("MB")};ymcc::monitormemory::Readings r(bool(demand&1),4096);const bool consumed=r.add(group,n,u,0,true,"MB",bool(demand&2));const auto d=r.diagnosticD3D(group);gateCases.push_back({consumed,d.dedicated,d.dynamic});}
 check("D3D demand and group bounds exercised",gateCases.size()==12);
 json rawReplay=json::object();
 {std::ifstream rawIn("__TASK__/RAW-SM2.bin",std::ios::binary);
  if(rawIn){std::vector<BYTE> raw((std::istreambuf_iterator<char>(rawIn)),std::istreambuf_iterator<char>());NativeMonitorHw initial;assert(parse(raw,initial));
   rawReplay["bytes"]=raw.size();rawReplay["top"]=top(initial);rawReplay["gpus"]=all(initial.gpuReadings);rawReplay["frames"]={initial.fps,initial.fps1,initial.frametimeMs,initial.fpsSensor,initial.producerPollTime,initial.producerPeriodMs};
   foldedBytes=equalCalls=prefixCalls=findCalls=0;allocations=0;trackAllocations=true;double sum=0;
   for(unsigned i=0;i<200;++i){put(raw,12,1700000000ULL+i);NativeMonitorHw hw;assert(parse(raw,hw));sum+=hw.packagePower+hw.gpuReadings.size();}
   trackAllocations=false;rawReplay["work"]={{"calls",200},{"foldedBytes",foldedBytes},{"equalCalls",equalCalls},{"prefixCalls",prefixCalls},{"findCalls",findCalls},{"heapAllocations",allocations},{"checksum",sum}};
  }
 }
 json classificationWork=json::object();
 const auto measure=[&](const std::string& name,const std::vector<Row>&rows){auto b=buffer(rows,1000);NativeMonitorHw warm;assert(parse(b,warm));foldedBytes=equalCalls=prefixCalls=findCalls=0;allocations=0;trackAllocations=true;double checksum=0;for(unsigned i=0;i<200;++i){put(b,12,1700000000ULL+i);NativeMonitorHw h;assert(parse(b,h));checksum+=h.packagePower+h.gpuReadings.size();}trackAllocations=false;classificationWork[name]={{"calls",200},{"rows",rows.size()},{"foldedBytes",foldedBytes},{"equalCalls",equalCalls},{"prefixCalls",prefixCalls},{"findCalls",findCalls},{"heapAllocations",allocations},{"checksum",checksum}};};
 measure("archived-original-values",archived);
 measure("CPU-and-drive-only",{{"CPU Package Power","W",30},{"Core 0 Usage","%",15},{"Drive Write Activity","%",10},{"CPU (Tctl/Tdie)","C",65}});
 measure("GPU-memory-voltage-temperature",{{"GPU Memory Allocated","MB",2048,2},{"GPU Memory Available","MB",6144,2},{"GPU Core Voltage","V",.9,2},{"GPU Temperature","C",70,2}});
 measure("RAM-and-network",{{"Physical Memory Used","MB",2048},{"Physical Memory Available","MB",6144},{"Physical Memory Load","%",25},{"Current DL rate","KB/s",1000,3},{"Current UP rate","KB/s",0,3}});
`.replaceAll('__TASK__',task);
assert(harness.includes(' json work=json::object();'));harness=harness.replace(' json work=json::object();',added+'\n json work=json::object();').replace('assert(argc==3);','assert(argc==4);').replace('{"work",work}','{"work",work},{"classificationWork",classificationWork},{"rawReplay",rawReplay},{"corpusCases",corpusCases},{"gateCases",gateCases}');
const results={};for(const side of ['before','after']){
 const source=fs.readFileSync('native/main.cpp','utf8');let header=generated.slice(0,generated.indexOf('struct NativeMonitorHw {'));
 for(const name of ['text_fold','voltage','memory_network','gpu_selection']){
  let text=fs.readFileSync(side==='before'&&name==='text_fold'?beforeFold:'native/monitor_'+name+'.h','utf8');
  if(name==='text_fold'){for(const [needle,insert]of [['    bool operator==(const char* literal) const {','++equalCalls;'],['    bool startsWith(const char* prefix) const {','++prefixCalls;'],['    size_t find(const char* needle) const {','++findCalls;'],['    static char monitorAsciiFold(char c) {','++foldedBytes;']]){assert(text.includes(needle));text=text.replace(needle,needle+insert);}}
  if(name==='gpu_selection')text=text.replace('double power=-1,clock=-1,load=-1,temperature=-1;','double power=(++deviceInitializations,-1.0),clock=-1,load=-1,temperature=-1;');
  for(const dependency of ['text_fold','voltage','memory_network'])text=text.replaceAll('#include "monitor_'+dependency+'.h"','#include "'+out+'/'+side+'-'+dependency+'.h"');
  fs.writeFileSync(out+'/'+side+'-'+name+'.h',text);header=header.replaceAll(root+'/native/monitor_'+name+'.h',out+'/'+side+'-'+name+'.h');
 }
 const prefix='#include <cstddef>\nstatic std::size_t deviceInitializations=0,foldedBytes=0,equalCalls=0,prefixCalls=0,findCalls=0;\n'+header+cut(source,'struct NativeMonitorHw {','\n};')+'\n};\n'+cut(source,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO(')+'\n'+cut(source,'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()');const file=out+'/'+side;fs.writeFileSync(file+'.cpp',prefix+'\n'+harness);
 fs.writeFileSync(file+'.cmd','@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /O2 "'+file+'.cpp" /Fo"'+file+'.obj" /Fe"'+file+'.exe"\r\nif errorlevel 1 exit /b 1\r\n"'+file+'.exe" "'+file+'.json" "'+task+'/ORIGINAL-ROWS.json" "'+task+'/CORPUS.json"\r\nexit /b %errorlevel%\r\n');
 const p=spawnSync('cmd.exe',['/d','/c',file+'.cmd'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(file+'.log',(p.stdout||'')+(p.stderr||''));console.log(side,((p.stdout||'')+(p.stderr||'')).slice(-2600));assert.equal(p.status,0);results[side]=JSON.parse(fs.readFileSync(file+'.json'));
}
for(const key of ['cases','nativeCases','work','corpusCases','gateCases'])assert.deepEqual(results.after[key],results.before[key],key+' must not change');
for(const name of Object.keys(results.before.classificationWork)){const a=results.after.classificationWork[name],b=results.before.classificationWork[name];assert.equal(a.checksum,b.checksum);assert.equal(a.heapAllocations,b.heapAllocations);assert(a.foldedBytes<b.foldedBytes,name+' must reduce ASCII folding work');}
if(results.before.rawReplay?.top){for(const key of ['top','gpus','frames'])assert.deepEqual(results.after.rawReplay[key],results.before.rawReplay[key],key+' from captured full SM2 must match');assert.equal(results.after.rawReplay.work.checksum,results.before.rawReplay.work.checksum);assert.equal(results.after.rawReplay.work.heapAllocations,results.before.rawReplay.work.heapAllocations);assert(results.after.rawReplay.work.foldedBytes<results.before.rawReplay.work.foldedBytes);}
const summary={ok:true,rawReplay:{before:results.before.rawReplay,after:results.after.rawReplay},checks:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.checks.length])),equalPublishedCases:Object.keys(results.after.cases).length,equalNativeCases:Object.keys(results.after.nativeCases).length,equalCorpusCases:Object.keys(results.after.corpusCases).length,equalDemandGateCases:results.after.gateCases.length,classificationWork:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.classificationWork])),packetBytes:results.after.packetBytes,gpuBytes:results.after.gpuBytes,sourceSha256:Object.fromEntries(['native/main.cpp','native/monitor_gpu_selection.h','native/monitor_memory_network.h','native/monitor_text_fold.h','native/monitor_voltage.h','tools/monitor_gpu_active_slots_selftest.mjs'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')])),hardwareSampled:false,scope:'Current full production parser/publisher/classifiers; only frozen/current exact equality helper differs. Archived values plus exhaustive exact-label variants/units/value corpus. Comparison and folding counts only, no CPU timing or percentage claims.'};fs.writeFileSync(task+'/FOLD-LENGTH-RESULT.json',JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary,null,2));
