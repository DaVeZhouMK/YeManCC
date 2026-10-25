// Omit unused unused C-state and disk groups and renumber the actual SM2 catalog, then
// run the real parser/cache/publisher. No hardware API or sensor reader runs.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_HWINFO_CSTATE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-cstate-projection-20261016').replaceAll('\\','/');fs.mkdirSync(out+'/parser',{recursive:true});
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{env:{...process.env,YMCC_MONITOR_MEMORY_OUT:out+'/parser'},encoding:'utf8',windowsHide:true});assert.equal(gen.status,0,gen.stderr);
const generated=fs.readFileSync(out+'/parser/memory-publication.cpp','utf8'),at=generated.indexOf('struct Row{');assert(at>0);const prefix=generated.slice(0,at);
const test=String.raw`
#include <map>
#include <algorithm>
#include <stdexcept>
struct Row {std::string name,unit;double value;};
struct Group {std::string name;uint32_t id,instance;std::vector<Row>rows;};
template<class T>void put(std::vector<BYTE>&b,size_t i,T v){memcpy(b.data()+i,&v,sizeof(v));}
std::vector<BYTE> bytes(const std::vector<Group>&g){
 size_t count=0;for(const auto&v:g)count+=v.rows.size();const uint32_t so=48,ss=268,rs=316,ro=so+ss*uint32_t(g.size());std::vector<BYTE>b(ro+count*rs);
 put(b,0,uint32_t{0x53695748});put(b,4,uint32_t{2});put(b,8,uint32_t{1});put(b,12,1700000000ULL);put(b,20,so);put(b,24,ss);put(b,28,uint32_t(g.size()));put(b,32,ro);put(b,36,rs);put(b,40,uint32_t(count));put(b,44,uint32_t{3000});
 size_t i=0;for(uint32_t j=0;j<g.size();j++){const auto&v=g[j];assert(v.name.size()<128);put(b,so+j*ss,v.id);put(b,so+j*ss+4,v.instance);memcpy(b.data()+so+j*ss+8,v.name.c_str(),v.name.size()+1);
 for(const auto&r:v.rows){const size_t o=ro+i++*rs;put(b,o+4,j);memcpy(b.data()+o+12,r.name.c_str(),r.name.size()+1);memcpy(b.data()+o+268,r.unit.c_str(),r.unit.size()+1);put(b,o+284,r.value);put(b,o+300,r.value);}}
 return b;
}
bool unused(const Group&g){return (g.id==0xf0000302||g.id==0xf0000101)&&g.instance<=31;}
json identity(const std::vector<Group>&g,uint32_t n){if(n>=g.size())return nullptr;return {g[n].id,g[n].instance,g[n].name};}
json details(const NativeMonitorHw&h,const std::vector<Group>&g){return {{"batteryWatts",h.chargeW},{"batteryWattsFound",h.chargeWFound},{"remainingMinutes",h.remainMin},{"cpuVolts",h.cpuVoltageV},{"gpuVolts",h.gpuVoltageV},{"fps",h.fps},{"fps1",h.fps1},{"frametime",h.frametimeMs},{"fpsIdentity",identity(g,h.fpsSensorIndex)},{"lowIdentity",identity(g,h.fps1SensorIndex)},{"timeIdentity",identity(g,h.frametimeSensorIndex)}};}
json publish(const NativeMonitorHw&h){const ymcc::monitorfs::Rates fs{123.5,45.25,1700000000000LL};const auto battery=ymcc::monitorbattery::measured(h.chargeW,"hwinfo");const auto time=ymcc::monitorbattery::remainingTime(battery,{},true,true,false,false,h.remainMin);return json::parse(nativeMonitorTopPayloadText(h,false,true,50,60,25,1700000000000LL,true,json::array({20,30,40,50}),1700000000000LL,battery,1700000000000LL,1700000000000LL,time,{},{},fs));}
int main(int argc,char**argv){assert(argc==2);json checks=json::array(),packets=json::object();auto check=[&](std::string n,bool ok){if(!ok)throw std::runtime_error(n);checks.push_back(n);};
 std::vector<Group> common{
 {"CPU [#0]: AMD Ryzen",1001,0,{{"CPU Package Power","W",30},{"CPU (Tctl/Tdie)","C",65},{"Core 0 Clock","MHz",4000},{"CPU Core Voltage","V",1.1}}},
 {"iGPU [#1]: AMD Radeon",1002,1,{{"GPU Power","W",20},{"GPU Clock","MHz",900}}},
 {"dGPU [#0]: NVIDIA RTX 5080",1003,0,{{"GPU Power","W",82},{"GPU Clock","MHz",2475},{"GPU Temperature","C",65},{"GPU Core Load","%",95},{"GPU Core Voltage","V",1.05},{"GPU Memory Allocated","MB",4096},{"GPU Memory Available","MB",12288}}},
 {"PresentMon: fixture",0xf0000ff0,0,{{"Framerate Presented (avg)","FPS",180},{"Framerate Presented (1% Low)","FPS",101},{"Frame Time Presented (avg)","ms",5.5}}},
 {"System",1005,0,{{"Physical Memory Used","MB",8192},{"Physical Memory Available","MB",24576},{"Physical Memory Load","%",25}}},
 {"Network",1006,0,{{"Current DL rate","MB/s",12},{"Current UP rate","MB/s",4}}},
 {"Battery",1007,0,{{"Charge Rate","W",-42},{"Estimated Remaining Time","min",60}}}};
 auto cstate=[](uint32_t instance){Group group{"CPU C-State [#"+std::to_string(instance)+"]",0xf0000302,instance,{}};for(unsigned i=0;i<12;i++)group.rows.push_back({"Package C"+std::to_string(i)+" Residency","%",double(i*7)});for(unsigned i=0;i<4;i++)group.rows.push_back({"Core "+std::to_string(i)+" C6 Residency","%",75});return group;};
 auto disk=[](uint32_t instance){return Group{"Disk Activity [#"+std::to_string(instance)+"]",0xf0000101,instance,{{"Read Rate","MB/s",999},{"Write Rate","MB/s",888}}};};
 auto compare=[&](const std::string&name,std::vector<Group>before){auto after=before;after.erase(std::remove_if(after.begin(),after.end(),unused),after.end());auto a=bytes(before),b=bytes(after);NativeMonitorHw x,y,z;bool unstable=false;ymcc::monitorparse::Reuse<NativeMonitorHw>cache;cache.begin(1);
 check(name+" original catalog parses",monitorParseHWiNFOView(a.data(),a.size(),x,unstable,false,&cache)&&!unstable);
 check(name+" renumbered catalog parses through the same cache owner",monitorParseHWiNFOView(b.data(),b.size(),y,unstable,false,&cache)&&!unstable);
 check(name+" every top publication is identical",publish(x)==publish(y));check(name+" FPS LOW and frametime retain values and real source identity",details(x,before)==details(y,after));
 check(name+" unchanged post-trim catalog remains cache-safe",monitorParseHWiNFOView(b.data(),b.size(),z,unstable,false,&cache)&&!unstable&&publish(y)==publish(z));
 const auto p=publish(y);check(name+" actual Windows FileSystem readings remain 123.5/45.25",p["fileReadMBps"]==123.5&&p["fileWriteMBps"]==45.25);
 packets[name]={{"before",publish(x)},{"after",p},{"native",details(y,after)},{"beforeGroups",before.size()},{"afterGroups",after.size()}};
 };
 auto first=common;first.insert(first.begin(),cstate(0));compare("cstate-first-renumbers-all-consumers",first);
 auto middle=common;middle.insert(middle.begin()+3,cstate(7));compare("cstate-before-PresentMon",middle);
 auto last=common;last.push_back(cstate(31));compare("cstate-last",last);
 auto all=common;for(uint32_t i=0;i<32;i++)all.insert(all.begin()+i%all.size(),cstate(i));compare("all-cstate-instances",all);
 auto combined=all;for(uint32_t i=0;i<32;i++)combined.insert(combined.begin()+i%combined.size(),disk(i));compare("combined-64-managed-groups",combined);
 auto zero=first;for(auto&r:zero[0].rows)r.value=0;compare("zero-residency-values",zero);
 auto bad=first;for(auto&r:bad[0].rows)r.value=std::numeric_limits<double>::quiet_NaN();compare("invalid-unused-residency-values",bad);
 auto renamed=first;renamed[0].name="User renamed CPU residency";compare("group-label-is-not-its-identity",renamed);
 auto nearby=first;auto keep=cstate(2);keep.id=0xf0000301;nearby.push_back(keep);compare("adjacent-system-family-is-preserved",nearby);
 auto future=first;future.push_back(cstate(32));compare("unverified-instance-is-preserved",future);
 const auto&v=packets["combined-64-managed-groups"]["after"];const auto&native=packets["combined-64-managed-groups"]["native"];
 check("ordinary CPU GPU RAM network and FPS remain actual nonempty readings",v["tdpW"]==30&&v["gpuPowerW"]==82&&v["gpuClockMhz"]==2475&&v["physicalMemoryLoadPct"]==25&&v["networkDownMBps"]==12&&native["fps"]==180);
 check("all 64 managed groups are actually omitted",packets["combined-64-managed-groups"]["beforeGroups"]==71&&packets["combined-64-managed-groups"]["afterGroups"]==7);
 check("battery discharge and remaining-time input retain their own source",native["batteryWattsFound"]==true&&native["batteryWatts"]==-42&&native["remainingMinutes"]==60);
 check("Windows core usage values remain independent of C-state rows",v["cpuUsage"]==25&&v["coreUsage"]==json::array({20,30,40,50}));
 std::ofstream(argv[1])<<json({{"ok",true},{"checks",checks.size()},{"cases",checks},{"packets",packets},{"hardwareRead",false}}).dump(2);std::cout<<"CSTATE_DATA_EQUIVALENCE "<<checks.size()<<" checks\n";
}
`;
const file=out+'/parser/cstate-equivalence.cpp';fs.writeFileSync(file,prefix+test);
const batch=out+'/parser/compile.cmd';fs.writeFileSync(batch,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${file}" /Fo"${out}/parser/cstate-equivalence.obj" /Fe"${out}/parser/cstate-equivalence.exe"\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\n"${out}/parser/cstate-equivalence.exe" "${out}/CSTATE-DATA-EQUIVALENCE-RESULT.json"\r\nexit /b %ERRORLEVEL%\r\n`);
const run=spawnSync('cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(out+'/CSTATE-DATA-EQUIVALENCE.log',(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-3500));assert.equal(run.status,0);
const result=JSON.parse(fs.readFileSync(out+'/CSTATE-DATA-EQUIVALENCE-RESULT.json','utf8'));result.sourceSha256={};for(const f of ['native/main.cpp','native/monitor_filesystem_rates.h','native/monitor_gpu_selection.h'])result.sourceSha256[f]=crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');const current=fs.readFileSync('native/main.cpp','utf8');result.sourceRegions={};
const digest=s=>crypto.createHash('sha256').update(s).digest('hex');
for(const [name,start,end,originalEnd]of [['structure','struct NativeMonitorHw {','\n};','\n};'],['parser','static bool monitorParseHWiNFOView(','static std::string nativeMonitorTopPayloadText(','static bool monitorReadHWiNFO('],['publisher','static std::string nativeMonitorTopPayloadText(',null,'\nstatic void nativeMonitorLoop()']]){
 const i=prefix.indexOf(start),j=end?prefix.indexOf(end,i):prefix.length,ci=current.indexOf(start),cj=current.indexOf(originalEnd,ci);assert(i>=0&&j>i&&ci>=0&&cj>ci,name);
 const tested=prefix.slice(i,j).trim(),now=current.slice(ci,cj).trim();assert.equal(tested,now,name+' changed during fixture generation/compile');result.sourceRegions[name]=digest(tested);
}
fs.writeFileSync(out+'/CSTATE-DATA-EQUIVALENCE-RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:result.checks,groups:Object.keys(result.packets).length},null,2));
