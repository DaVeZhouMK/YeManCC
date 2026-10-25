// Actual unified parser/publisher: omitting only optional NVIDIA measured-clock
// rows must preserve every field. No second collector or live sensor read.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_NV_UNUSED_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-unused-clock-20261016').replaceAll('\\','/');fs.mkdirSync(out+'/parser',{recursive:true});
const current=fs.readFileSync('native/main.cpp','utf8');const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j);};
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{env:{...process.env,YMCC_MONITOR_MEMORY_OUT:out+'/parser'},encoding:'utf8',windowsHide:true});assert.equal(gen.status,0,gen.stderr);
const generated=fs.readFileSync(out+'/parser/memory-publication.cpp','utf8'),at=generated.indexOf('struct Row{');assert(at>0);const prefix=generated.slice(0,at);
const archive='../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-source-reuse-20261016/ORIGINAL-ROWS.json';fs.copyFileSync(archive,out+'/ORIGINAL-ROWS.json');
const harness=String.raw`
#include <map>
#include <algorithm>
#include <cctype>
#include <stdexcept>
struct Row {std::string name,unit;double value;uint32_t group=0;};
template<class T>void put(std::vector<BYTE>&b,size_t i,T value){memcpy(b.data()+i,&value,sizeof(value));}
using Names=std::map<uint32_t,std::string>;
std::vector<BYTE> data(const std::vector<Row>& rows,const Names&names){
 const uint32_t so=48,ss=268,sc=32,ro=so+ss*sc,rs=316;std::vector<BYTE>b(ro+rows.size()*rs);
 put(b,0,uint32_t{0x53695748});put(b,4,uint32_t{2});put(b,8,uint32_t{1});put(b,12,1700000000ULL);put(b,20,so);put(b,24,ss);put(b,28,sc);put(b,32,ro);put(b,36,rs);put(b,40,uint32_t(rows.size()));put(b,44,uint32_t{3000});
 for(const auto&[i,name]:names){assert(i<sc&&name.size()<128);put(b,so+i*ss,uint32_t{1000+i});put(b,so+i*ss+4,uint32_t{1});memcpy(b.data()+so+i*ss+8,name.c_str(),name.size()+1);}
 for(size_t i=0;i<rows.size();++i){const auto&r=rows[i];const size_t off=ro+i*rs;assert(r.name.size()<128&&r.unit.size()<16);put(b,off+4,r.group);memcpy(b.data()+off+12,r.name.c_str(),r.name.size()+1);memcpy(b.data()+off+268,r.unit.c_str(),r.unit.size()+1);put(b,off+284,r.value);put(b,off+300,r.value);}
 return b;
}
bool extra(const Row&r,const Names&names){
 const auto it=names.find(r.group);if(it==names.end()||it->second.find("NVIDIA")==std::string::npos)return false;
 std::string n=r.name;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
 return n=="gpu clock (measured)"||n=="gpu memory clock (measured)"||n=="gpu crossbar clock (measured)";
}
json details(const NativeMonitorHw&h){return {{"fps",h.fps},{"fps1",h.fps1},{"frametime",h.frametimeMs},{"fpsSensor",h.fpsSensor},{"fpsSensorIndex",h.fpsSensorIndex},{"frameSensorIndex",h.frametimeSensorIndex},{"cpuVoltage",h.cpuVoltageV},{"power",h.packagePower},{"clock",h.freqMhz},{"temp",h.tempC},{"thermal",h.thermalThrottleFound},{"thermalMax",h.thermalThrottleMax},{"sharedOk",h.sharedOk}};}
json publish(const NativeMonitorHw&h){return json::parse(nativeMonitorTopPayloadText(h,true,true,-1,-1,25,1700000000000LL,true,json::array({20,30,40,50}),1700000000000LL,{},0,0,{},{},{},{}));}
int main(int argc,char**argv){assert(argc==3);json checks=json::array(),packets=json::object();auto check=[&](const char*name,bool value){if(!value)throw std::runtime_error(name);checks.push_back(name);};
 Names names{{0,"CPU [#0]: AMD Ryzen"},{1,"iGPU [#1]: AMD Radeon"},{2,"dGPU [#0]: NVIDIA GeForce RTX 5080"},{3,"Network"},{4,"Battery"},{5,"PresentMon: fixture"}};
 std::vector<Row> common{{"CPU Package Power","W",30},{"CPU (Tctl/Tdie)","C",65},{"Core 0 Clock","MHz",4000},{"CPU Core Voltage","V",1.1},{"Physical Memory Used","MB",8192},{"Physical Memory Available","MB",24576},{"Physical Memory Load","%",25},{"Current DL rate","MB/s",12,3},{"Current UP rate","MB/s",4,3},{"Charge Rate","W",-42,4},{"Estimated Remaining Time","min",60,4},{"Framerate Presented (avg)","FPS",180,5},{"Framerate Presented (1% Low)","FPS",101,5},{"Frame Time Presented (avg)","ms",5.5,5}};
 std::vector<Row> gpu{{"GPU Power","W",82,2},{"GPU Clock","MHz",2475,2},{"GPU Core Load","%",95,2},{"GPU Temperature","C",65,2},{"GPU Core Voltage","V",1.05,2},{"GPU Memory Allocated","MB",4096,2},{"GPU Memory Available","MB",12288,2},{"GPU Memory Load","%",25,2}};
 const std::vector<Row> measured{{"GPU Clock (measured)","MHz",2500,2},{"GPU Memory Clock (measured)","MHz",1873,2},{"GPU Crossbar Clock (measured)","MHz",1200,2}};
 auto compare=[&](const std::string& label,std::vector<Row>rows,const Names&sensorNames){
  auto after=rows;after.erase(std::remove_if(after.begin(),after.end(),[&](const Row&r){return extra(r,sensorNames);}),after.end());
  NativeMonitorHw a,b;bool unstable=false;auto bytes=data(rows,sensorNames),filtered=data(after,sensorNames);ymcc::monitorparse::Reuse<NativeMonitorHw> cache;cache.begin(1);
  check((label+" original parses").c_str(),monitorParseHWiNFOView(bytes.data(),bytes.size(),a,unstable,false,&cache)&&!unstable);
  check((label+" optimized parses through the same cache owner").c_str(),monitorParseHWiNFOView(filtered.data(),filtered.size(),b,unstable,false,&cache)&&!unstable);
  auto pa=publish(a),pb=publish(b);check((label+" every published value is identical").c_str(),pa==pb);check((label+" native FPS and hardware proofs are identical").c_str(),details(a)==details(b));
  ymcc::monitorgpu::StickySelection sa,sb;a.useGpu(sa.select(a.gpuReadings,true));b.useGpu(sb.select(b.gpuReadings,true));check((label+" session GPU selection remains identical").c_str(),publish(a)==publish(b));
  check((label+" all ordinary GPU fields are retained").c_str(),a.gpuClockMhz==b.gpuClockMhz&&a.gpuPowerW==b.gpuPowerW&&a.gpuVoltageV==b.gpuVoltageV&&a.gpuTempC==b.gpuTempC&&a.gpuLoad==b.gpuLoad);
  if(label=="NVIDIA-5080-standard-data") {
   check("normal GPU clock/power/temperature/load/voltage retain actual expected readings",b.gpuClockMhz==2475&&b.gpuPowerW==82&&b.gpuTempC==65&&b.gpuLoad==95&&b.gpuVoltageV==1.05);
   check("required HWiNFO FPS/LOW/frametime remain actual nonempty source readings",b.fps==180&&b.fps1==101&&b.frametimeMs==5.5&&b.fpsSensor);
   check("RAM/network ordinary inputs remain actual nonempty published readings",pb["physicalMemoryUsedMb"]==8192&&pb["physicalMemoryLoadPct"]==25&&pb["networkDownMBps"]==12);
   check("only the three optional measured NVIDIA rows are omitted",rows.size()==after.size()+3);
  }
  if(label=="ordinary-clock-valid-zero")check("ordinary zero MHz stays valid zero instead of an optional measured clock",pb["gpuClockMhz"]==0);
  if(label=="ordinary-clock-missing-not-replaced-by-measured"||label=="ordinary-clock-invalid")check((label+" stays unknown").c_str(),pb["gpuClockMhz"].is_null());
  if(label=="AMD-sampling-is-unchanged")check("no AMD source rows are removed",rows.size()==after.size());
  packets[label]={{"before",pa},{"after",pb},{"beforeRows",rows.size()},{"afterRows",after.size()},{"native",details(b)}};
 };
 auto rows=common;rows.insert(rows.end(),gpu.begin(),gpu.end());rows.insert(rows.end(),measured.begin(),measured.end());compare("NVIDIA-5080-standard-data",rows,names);
 rows.push_back({"GPU Power","W",20,1});rows.push_back({"GPU Clock","MHz",900,1});compare("NVIDIA-plus-AMD-coherent",rows,names);
 auto zero=rows;for(auto&r:zero)if(r.name=="GPU Clock"&&r.group==2)r.value=0;compare("ordinary-clock-valid-zero",zero,names);
 auto missing=rows;missing.erase(std::remove_if(missing.begin(),missing.end(),[](const Row&r){return r.name=="GPU Clock"&&r.group==2;}),missing.end());compare("ordinary-clock-missing-not-replaced-by-measured",missing,names);
 auto bad=rows;for(auto&r:bad)if(r.name=="GPU Clock"&&r.group==2)r.value=std::numeric_limits<double>::quiet_NaN();compare("ordinary-clock-invalid",bad,names);
 auto aliases=rows;aliases.push_back({"GPU Core Clock","MHz",2500,2});aliases.push_back({"GPU Clock (Effective)","MHz",2400,2});compare("ordinary-clock-aliases",aliases,names);
 auto only=common;only.insert(only.end(),measured.begin(),measured.end());compare("only-optional-clock-rows-never-create-a-GPU",only,names);
 auto amdNames=names;amdNames[2]="iGPU [#0]: AMD Radeon 8060S";compare("AMD-sampling-is-unchanged",rows,amdNames);
 std::ifstream archived(argv[2]);json original;archived>>original;std::vector<Row> archiveRows;Names archiveNames;
 for(const auto&r:original["rows"])if(r["value"].is_number()){const auto group=r["sensorIndex"].get<uint32_t>();archiveRows.push_back({r["name"],r["unit"],r["value"],group});archiveNames[group]=r["group"];}
 compare("archived-local-raw-values",archiveRows,archiveNames);
 check("archived source contains a genuinely recorded optional NVIDIA measured clock",std::any_of(archiveRows.begin(),archiveRows.end(),[&](const Row&r){return extra(r,archiveNames);}));
 std::ofstream(argv[1])<<json({{"ok",true},{"checks",checks.size()},{"cases",checks},{"packets",packets},{"hardwareRead",false}}).dump(2);std::cout<<"UNUSED_CLOCK_DATA_EQUIVALENCE "<<checks.size()<<" checks\n";
}
`;
const file=out+'/parser/unused-clock.cpp';fs.writeFileSync(file,prefix+harness);
const batch=out+'/parser/compile.cmd';fs.writeFileSync(batch,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${file}" /Fo"${out}/parser/unused-clock.obj" /Fe"${out}/parser/unused-clock.exe"\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\n"${out}/parser/unused-clock.exe" "${out}/DATA-EQUIVALENCE-RESULT.json" "${out}/ORIGINAL-ROWS.json"\r\nexit /b %ERRORLEVEL%\r\n`);
const run=spawnSync('cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(out+'/DATA-EQUIVALENCE.log',(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-4500));assert.equal(run.status,0);
const templates=['W','L','JJ'].map(n=>'PowerControl/RTSS-Overlays/YeManOBS-'+n+'-1.ovl');const refs=[];for(const t of templates){const b=fs.readFileSync(t);const txt=b[0]===255&&b[1]===254?b.subarray(2).toString('utf16le'):b.toString('utf8');const readingNames=[...txt.matchAll(/^ReadingName=(.+)\r?$/gm)].map(m=>m[1].trim());assert(readingNames.length>0&&readingNames.includes('GPU Clock'),t+' must contain recognized ordinary GPU source names');assert(!readingNames.some(n=>/^GPU (?:Clock|Memory Clock|Crossbar Clock) \(measured\)$/i.test(n)),t);refs.push({path:t,readingNames,sha256:crypto.createHash('sha256').update(b).digest('hex')});}
fs.writeFileSync(out+'/TEMPLATE-DEPENDENCIES.json',JSON.stringify({ok:true,templates:refs},null,2)+'\n');
const results=JSON.parse(fs.readFileSync(out+'/DATA-EQUIVALENCE-RESULT.json','utf8'));results.templateChecks=refs.length;results.sourcePins={};for(const [name,start,end]of [['structure','struct NativeMonitorHw {','\n};'],['parser','static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO('],['publisher','static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()']])results.sourcePins[name]=crypto.createHash('sha256').update(cut(current,start,end)).digest('hex');fs.writeFileSync(out+'/DATA-EQUIVALENCE-RESULT.json',JSON.stringify(results,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:results.checks,templates:results.templateChecks,groups:Object.keys(results.packets).length},null,2));
