// Compare frozen/current complete parsers and consumers of copied HWiNFO rows.
// Unit-first CPU admission; full packet precedence/threshold corpus.
// Deterministic classification work only, no live hardware or timing benchmark.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_CPU_GATES_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-unit-gates-20261016').replaceAll('\\','/'),out=task+'/native';fs.mkdirSync(out,{recursive:true});fs.mkdirSync(task+'/parser',{recursive:true});
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j);};
const base=fs.readFileSync('tools/monitor_gpu_active_slots_selftest.mjs','utf8').replaceAll('\r\n','\n');let harness=cut(base,'const harness=String.raw`','\n`;\nconst results=').slice('const harness=String.raw`'.length).replace('constexpr bool modern=MODERN;','constexpr bool modern=true;');
fs.copyFileSync('../../../Build/Tasks/YMCC-Steam-HUD-Probe/gpu-active-slots-20261016/ORIGINAL-ROWS.json',task+'/ORIGINAL-ROWS.json');

const exclusions=['effective','bus','memory','gpu','video','crossbar','vcn','soc','ref','ratio','boost','uncore','mesh','dram','display','encoder'];
const names=[...new Set(['CPU Package Power','CPU Pkg Power','CPU Package Core Power','CPU Package CCD Power','CPU Package SOC Power','CPU Package GPU Power','CPU Package Power Limit','CPU Package Power Maximum','CPU Package Power Minimum','CPU Package Power Rated','CPU Package Power Battery Power','CPU Package Power Remaining Time','Core Clock','Core 0 Clock','CPU Clock','CPU Core Clock',...exclusions.map(w=>'CPU '+w+' Clock'),'Core Clock Battery Power','CPU Clock remaining time','CPU Clock CPU Tctl','CPU (Tctl/Tdie)','CPU Tdie','CPU Package','CPU Tctl Power','CPU Tdie Voltage','CPU Tctl RPM','CPU Tdie Current','CPU Tctl Remaining Time','CPU Package Extra','Estimated Remaining Time','Remaining Time','Remaining Time Battery Power','Battery Power','Charge Rate','Discharge Rate','Charge/Discharge Rate','Battery Charge Level','Battery Charge Capacity','Core Thermal Throttling','Thermal Throttling (HTC)','Virtual Memory Committed','Virtual Memory Load','Frame Time Presented (avg)','Framerate Presented (avg)','Framerate Presented (1% low)','Physical Memory Used','Current DL rate','GPU Power','CPU Package Power\0Remaining Time','cPu CoRe ClOcK','CPU (TCTL/TDIE)','\u00e9CPU Package Power','Unknown',''])];
const units=['W','watts','mW','MHz','kHz','C','\u00b0C','\u2103','V','mV','%','MB','FPS','ms','h','min','s','yes/no','bad',''];
fs.writeFileSync(task+'/CORPUS.json',JSON.stringify({names,units,modes:['50','0','-1','NaN','Infinity','150','151','1999','2000','10000','10001','5000'],scope:'CPU power/clock/temperature exclusions and conflicting battery/frame/virtual/voltage labels, unit and value boundaries; synthetic strings.'},null,2));
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{encoding:'utf8',env:{...process.env,YMCC_MONITOR_MEMORY_OUT:task+'/parser'}});assert.equal(gen.status,0,gen.stderr);const generated=fs.readFileSync(task+'/parser/memory-publication.cpp','utf8');

const structure=cut(fs.readFileSync(task+'/before/native/main.cpp','utf8'),'struct NativeMonitorHw {','\n};')+'\n};',scalar=[];
for(const line of structure.split('\n')){const m=line.match(/^\s*(?:double|bool|uint32_t|unsigned long long)\s+([^;]+);/);if(m)for(const declaration of m[1].split(',')){const name=declaration.trim().match(/^(\w+)\s*(?:=|$)/)?.[1];if(name)scalar.push(name);}}
assert(scalar.length>30);assert.equal(new Set(scalar).size,scalar.length);
const complete='static json fullPacket(const NativeMonitorHw&h){json out={{"top",top(h)},{"nativeGpu",all(h.gpuReadings)}};out["scalar"]={'+scalar.map(n=>'{'+JSON.stringify(n)+',h.'+n+'}').join(',')+'};out["rawMemory"]={h.physicalMemory.used,h.physicalMemory.available,h.physicalMemory.total,h.physicalMemory.load,h.gpuMemory.used,h.gpuMemory.available,h.gpuMemory.total,h.gpuMemory.load};out["gpuSource"]=h.gpuMemorySource;out["d3d"]=json::array();for(size_t i=0;i<h.gpuReadings.size();++i){const auto d=h.gpuReadings.diagnosticD3D(&h.gpuReadings.at(i));out["d3d"].push_back({d.dedicated,d.dynamic});}return out;}\n';
harness=harness.replace('static std::vector<std::string> checks;',complete+'static std::vector<std::string> checks;');
const added=String.raw`

 json corpusCases=json::object();std::ifstream corpusIn(argv[3]);json corpus;corpusIn>>corpus;std::size_t corpusIndex=0;
 for(const auto& n:corpus["names"])for(const auto& u:corpus["units"])for(unsigned mode=0;mode<12;++mode){
  const std::string name=n,unit=u;const double values[]={50,0,-1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),150,151,1999,2000,10000,10001,5000};const double value=values[mode];
  auto b=buffer({{name,unit,value,1}});NativeMonitorHw h;assert(parse(b,h));corpusCases[std::to_string(corpusIndex++)]=fullPacket(h);
 }
 check("CPU name/unit/value/continuation corpus runs",corpusIndex>10000);
 json precedenceCases=json::object();
 const auto pair=[&](const std::string&key,std::vector<Row> rows){auto b=buffer(rows);NativeMonitorHw h;assert(parse(b,h));precedenceCases[key]=fullPacket(h);return h;};
 const auto conflict=pair("wrong-unit-and-valid-battery",{{"CPU Clock Remaining Time","min",60,1},{"Remaining Time","min",20,2}});
 check("wrong CPU-clock unit must retain battery group ambiguity",conflict.remainMin<0);
 const auto invalid=pair("invalid-power-and-valid-battery",{{"CPU Package Power Battery Power","W",-1,1},{"Battery Power","W",-20,2}});
 check("invalid accepted CPU power still consumes the row before battery ownership",invalid.chargeWFound&&invalid.chargeW==-20);
 const auto badUnit=pair("invalid-power-unit-provenance",{{"CPU Package Power Battery Power","bad",30,1},{"Battery Power","W",-20,2}});
 check("wrong power unit still participates in battery ambiguity",!badUnit.chargeWFound);
 const auto overlap=pair("clock-before-battery",{{"CPU Clock Remaining Time","MHz",3000,1},{"Remaining Time","min",20,2}});
 check("valid CPU clock still consumes before remaining-time rule",overlap.cpuClockValid&&overlap.freqMhz==3000&&overlap.remainMin==20);
 const auto invalidClock=pair("invalid-clock-falls-through",{{"CPU Clock Remaining Time","MHz",10001,1},{"Remaining Time","min",20,2}});
 check("out-of-range clock still marks ambiguous battery group without creating MHz runtime",!invalidClock.cpuClockValid&&invalidClock.remainMin<0);
 const auto temps=pair("temperature-priority",{{"CPU Package","C",90},{"CPU (Tctl/Tdie)","C",70}});
 check("Tctl priority and maxima remain unchanged",temps.cpuTempValid&&temps.tempC==70);
 pair("temperature-before-battery",{{"CPU Tctl Remaining Time","C",70,1},{"Remaining Time","min",20,2}});
 pair("invalid-temperature-falls-through",{{"CPU Tctl Remaining Time","C",151,1},{"Remaining Time","min",20,2}});
 pair("mixed-frame-cpu-battery",{{"Framerate Presented (avg)","FPS",60,3},{"Framerate Presented (1% low)","FPS",45,3},{"Frame Time Presented (avg)","ms",16,3},{"CPU Package Power","W",35},{"Core 0 Clock","MHz",4000},{"CPU Package","C",90},{"CPU (Tctl/Tdie)","C",70},{"Battery Power","W",-40,1},{"Estimated Remaining Time","min",90,1}});
 json gateCases=json::array();
 for(uint32_t group:std::vector<uint32_t>{0,4095,4096})for(unsigned demand=0;demand<4;++demand){const MonitorFoldedText n{std::string_view("GPU D3D Memory Dedicated")},u{std::string_view("MB")};ymcc::monitormemory::Readings r(bool(demand&1),4096);const bool consumed=r.add(group,n,u,0,true,"MB",bool(demand&2));const auto d=r.diagnosticD3D(group);gateCases.push_back({consumed,d.dedicated,d.dynamic});}
 check("D3D demand and group bounds exercised",gateCases.size()==12);
 json classificationWork=json::object();
 const auto measure=[&](const std::string& name,const std::vector<Row>&rows){auto b=buffer(rows,1000);NativeMonitorHw warm;assert(parse(b,warm));foldedBytes=equalCalls=prefixCalls=findCalls=0;allocations=0;trackAllocations=true;double checksum=0;for(unsigned i=0;i<200;++i){put(b,12,1700000000ULL+i);NativeMonitorHw h;assert(parse(b,h));checksum+=h.packagePower+h.gpuReadings.size();}trackAllocations=false;classificationWork[name]={{"calls",200},{"rows",rows.size()},{"foldedBytes",foldedBytes},{"equalCalls",equalCalls},{"prefixCalls",prefixCalls},{"findCalls",findCalls},{"heapAllocations",allocations},{"checksum",checksum}};};
 measure("archived-original-values",archived);
 measure("CPU-and-drive-only",{{"CPU Package Power","W",30},{"Core 0 Usage","%",15},{"Drive Write Activity","%",10},{"CPU (Tctl/Tdie)","C",65}});
 measure("GPU-memory-voltage-temperature",{{"GPU Memory Allocated","MB",2048,2},{"GPU Memory Available","MB",6144,2},{"GPU Core Voltage","V",.9,2},{"GPU Temperature","C",70,2}});
 measure("clock-and-battery",{{"Core 0 Clock","MHz",4000},{"Core Effective Clock","MHz",3500},{"Battery Power","W",-40,1},{"Estimated Remaining Time","min",90,1}});
 measure("RAM-and-network",{{"Physical Memory Used","MB",2048},{"Physical Memory Available","MB",6144},{"Physical Memory Load","%",25},{"Current DL rate","KB/s",1000,3},{"Current UP rate","KB/s",0,3}});
`;
assert(harness.includes(' json work=json::object();'));harness=harness.replace(' json work=json::object();',added+'\n json work=json::object();').replace('assert(argc==3);','assert(argc==4);').replace('{"work",work}','{"work",work},{"classificationWork",classificationWork},{"corpusCases",corpusCases},{"precedenceCases",precedenceCases},{"gateCases",gateCases}');
const results={};for(const side of ['before','after']){
 const source=fs.readFileSync(side==='before'?task+'/before/native/main.cpp':'native/main.cpp','utf8');let header=generated.slice(0,generated.indexOf('struct NativeMonitorHw {'));
 for(const name of ['text_fold','voltage','memory_network','gpu_selection']){
  let text=fs.readFileSync(side==='before'?task+'/before/native/monitor_'+name+'.h':'native/monitor_'+name+'.h','utf8');
  if(name==='text_fold'){for(const [needle,insert]of [['    bool operator==(const char* literal) const {','++equalCalls;'],['    bool startsWith(const char* prefix) const {','++prefixCalls;'],['    size_t find(const char* needle) const {','++findCalls;'],['    static char monitorAsciiFold(char c) {','++foldedBytes;']]){assert(text.includes(needle));text=text.replace(needle,needle+insert);}}
  if(name==='gpu_selection')text=text.replace('double power=-1,clock=-1,load=-1,temperature=-1;','double power=(++deviceInitializations,-1.0),clock=-1,load=-1,temperature=-1;');
  for(const dependency of ['text_fold','voltage','memory_network'])text=text.replaceAll('#include "monitor_'+dependency+'.h"','#include "'+out+'/'+side+'-'+dependency+'.h"');
  fs.writeFileSync(out+'/'+side+'-'+name+'.h',text);header=header.replaceAll(root+'/native/monitor_'+name+'.h',out+'/'+side+'-'+name+'.h');
 }
 const prefix='#include <cstddef>\nstatic std::size_t deviceInitializations=0,foldedBytes=0,equalCalls=0,prefixCalls=0,findCalls=0;\n'+header+cut(source,'struct NativeMonitorHw {','\n};')+'\n};\n'+cut(source,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO(')+'\n'+cut(source,'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()');const file=out+'/'+side;fs.writeFileSync(file+'.cpp',prefix+'\n'+harness);
 fs.writeFileSync(file+'.cmd','@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "'+file+'.cpp" /Fo"'+file+'.obj" /Fe"'+file+'.exe"\r\nif errorlevel 1 exit /b 1\r\n"'+file+'.exe" "'+file+'.json" "'+task+'/ORIGINAL-ROWS.json" "'+task+'/CORPUS.json"\r\nexit /b %errorlevel%\r\n');
 const p=spawnSync('cmd.exe',['/d','/c',file+'.cmd'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(file+'.log',(p.stdout||'')+(p.stderr||''));console.log(side,((p.stdout||'')+(p.stderr||'')).slice(-2600));assert.equal(p.status,0);results[side]=JSON.parse(fs.readFileSync(file+'.json'));
}
for(const key of ['cases','nativeCases','work','corpusCases','gateCases','precedenceCases'])assert.deepEqual(results.after[key],results.before[key],key+' must not change');
for(const name of Object.keys(results.before.classificationWork)){const a=results.after.classificationWork[name],b=results.before.classificationWork[name];assert.equal(a.checksum,b.checksum);assert.equal(a.heapAllocations,b.heapAllocations);assert(a.foldedBytes<=b.foldedBytes,name+' must not increase ASCII folding work');if(name==='archived-original-values'||name==='CPU-and-drive-only'||name==='clock-and-battery')assert(a.foldedBytes<b.foldedBytes,name+' must reduce folding work');}
const summary={ok:true,checks:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.checks.length])),equalPublishedCases:Object.keys(results.after.cases).length,equalNativeCases:Object.keys(results.after.nativeCases).length,equalCorpusCases:Object.keys(results.after.corpusCases).length,equalDemandGateCases:results.after.gateCases.length,equalPrecedenceCases:Object.keys(results.after.precedenceCases).length,classificationWork:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.classificationWork])),packetBytes:results.after.packetBytes,gpuBytes:results.after.gpuBytes,sourceSha256:Object.fromEntries(['native/main.cpp','native/monitor_gpu_selection.h','native/monitor_memory_network.h','native/monitor_text_fold.h','native/monitor_voltage.h','tools/monitor_gpu_active_slots_selftest.mjs'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')])),hardwareSampled:false,scope:'Actual frozen/current parser; complete published/scalar/GPU/memory output for CPU unit/value/exclusion and cross-family continuation corpus, archived inputs and deterministic work counts. No CPU timing or percentage claims.'};fs.writeFileSync(task+'/NATIVE-RESULT.json',JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary,null,2));
