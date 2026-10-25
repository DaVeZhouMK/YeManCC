// Production top publisher equivalence/allocation checks. No OS sampling,
// application interaction, CPU timing or process-CPU benchmark.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_TOP_CONSTRUCTION_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/top-payload-construction-20261016').replaceAll('\\','/');fs.mkdirSync(out,{recursive:true});
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j)};
const source=fs.readFileSync('native/main.cpp','utf8'),before=fs.readFileSync('tools/fixtures/monitor_top_payload_before_20261016.inc','utf8');
const current=cut(source,'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()'),original=before.slice(before.indexOf('static std::string nativeMonitorTopPayloadText('));const structure=cut(source,'struct NativeMonitorHw {','\n};')+'\n};';
const code=String.raw`
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
#include "__ROOT__/native/monitor_gpu_selection.h"
#include "__ROOT__/native/monitor_filesystem_rates.h"
#include "__ROOT__/native/monitor_battery_energy.h"
#include "__ROOT__/native/monitor_rtss_global.h"
#include "__ROOT__/native/monitor_fan_receipt.h"
using json=nlohmann::json;
static bool tracking=false;static std::size_t allocations=0,allocatedBytes=0;
void* operator new(std::size_t n){if(tracking){++allocations;allocatedBytes+=n;}auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p) noexcept{std::free(p);}void operator delete[](void*p) noexcept{std::free(p);}void operator delete(void*p,std::size_t) noexcept{std::free(p);}void operator delete[](void*p,std::size_t) noexcept{std::free(p);}
__STRUCTURE__
namespace baseline{__BEFORE__}
namespace candidate{__AFTER__}
struct Case{
 NativeMonitorHw hw;bool ac=true,hasBattery=false,healthy=true,cpuValid=true;int percent=-1;double remain=-1,cpu=27;long long ts=1700000000000LL,coreTs=0,batteryTs=0,statusTs=0,cpuTs=0;json cores=json::array();
 ymcc::monitorbattery::Power battery;ymcc::monitorbattery::RemainingTime batteryTime;ymcc::monitorrtss::Limit cap;ymcc::monitorfan::Receipt fan;ymcc::monitorfs::Rates files;
};
static std::string publish(const Case&c,bool optimized){
 if(optimized)return candidate::nativeMonitorTopPayloadText(c.hw,c.ac,c.hasBattery,c.percent,c.remain,c.cpu,c.ts,c.healthy,c.cores,c.coreTs,c.battery,c.batteryTs,c.statusTs,c.batteryTime,c.cap,c.fan,c.files,c.cpuValid,c.cpuTs);
 return baseline::nativeMonitorTopPayloadText(c.hw,c.ac,c.hasBattery,c.percent,c.remain,c.cpu,c.ts,c.healthy,c.cores,c.coreTs,c.battery,c.batteryTs,c.statusTs,c.batteryTime,c.cap,c.fan,c.files,c.cpuValid,c.cpuTs);
}
static unsigned checks=0,packets=0;static void check(bool v,const char*n){++checks;if(!v)throw std::runtime_error(n);}
static json compare(const Case&c){const auto a=publish(c,false),b=publish(c,true);check(a==b,"complete canonical publication bytes changed");check(json::parse(a)==json::parse(b),"publication types or fields changed");++packets;return json::parse(b);}
static double num(const json&t,const char*k,double fallback=-1){const auto i=t.find(k);return i!=t.end()&&i->is_number()?i->get<double>():fallback;}
static Case historical(const json&t){
 Case c;c.ac=t.value("ac",1)!=0;c.hasBattery=t.value("hasBattery",false);c.percent=t.value("batteryPercent",-1);c.remain=num(t,"remainMin");c.cpu=num(t,"cpuUsage",0);c.ts=t.value("ts",0LL);c.healthy=!t.value("hwDown",true);c.cpuValid=t.value("cpuUsageValid",true);c.cpuTs=t.value("cpuUsageTs",0LL);c.coreTs=t.value("coreUsageTs",0LL);c.cores=t.value("coreUsage",json::array());
 c.hw.packagePower=num(t,"tdpW",0);c.hw.freqMhz=num(t,"freqMhz",0);c.hw.tempC=num(t,"tempC",0);c.hw.cpuPowerValid=t.value("cpuPowerValid",false);c.hw.cpuClockValid=t.value("cpuClockValid",false);c.hw.cpuTempValid=t.value("cpuTempValid",false);c.hw.chargeW=num(t,"chargeW",0);c.hw.gpuPowerW=num(t,"gpuPowerW");c.hw.gpuClockMhz=num(t,"gpuClockMhz");c.hw.gpuTempC=num(t,"gpuTempC");c.hw.gpuLoad=num(t,"gpuUsagePct");c.hw.cpuVoltageV=num(t,"cpuVoltageV");c.hw.gpuVoltageV=num(t,"gpuVoltageV");
 c.hw.physicalMemory={num(t,"physicalMemoryUsedMb"),num(t,"physicalMemoryAvailableMb"),num(t,"physicalMemoryTotalMb"),num(t,"physicalMemoryLoadPct")};c.hw.gpuMemory={num(t,"gpuMemoryUsedMb"),num(t,"gpuMemoryAvailableMb"),num(t,"gpuMemoryTotalMb"),num(t,"gpuMemoryLoadPct")};c.hw.networkDownMBps=num(t,"networkDownMBps");c.hw.networkUpMBps=num(t,"networkUpMBps");c.hw.thermalThrottleFound=t.value("thermalThrottleFound",false);c.hw.thermalThrottleMax=num(t,"thermalThrottleMax",0);c.hw.virtualMemoryCommittedFound=t.value("virtualMemoryCommittedFound",false);c.hw.virtualMemoryCommittedMb=num(t,"virtualMemoryCommittedMb",0);c.hw.virtualMemoryLoadFound=t.value("virtualMemoryLoadFound",false);c.hw.virtualMemoryLoadPct=num(t,"virtualMemoryLoadPct",0);c.hw.gpuPinned=t.value("gpuSelectionPinned",false);c.hw.gpuSelectedPresent=t.value("gpuSelectionPresent",false);
 if(t.contains("gpuName")&&t["gpuName"].is_string()){const auto s=t["gpuName"].get<std::string>();std::memcpy(c.hw.gpuName.data(),s.data(),(std::min)(s.size(),c.hw.gpuName.size()-1));}
 if(t.value("gpuMemorySource",std::string{})=="hwinfo-d3d-dedicated")c.hw.gpuMemorySource="hwinfo-d3d-dedicated";else if(t.value("gpuMemorySource",std::string{})=="hwinfo-d3d-shared")c.hw.gpuMemorySource="hwinfo-d3d-shared";else if(t.value("gpuMemorySource",std::string{})=="hwinfo")c.hw.gpuMemorySource="hwinfo";
 c.files={num(t,"fileReadMBps"),num(t,"fileWriteMBps"),t.value("fileSystemTs",0LL)};c.cap={t.value("fpsLimitValid",false),num(t,"fpsLimit",0),t.value("fpsLimitTs",0LL)};c.fan={t.value("fanEnabled",false),t.value("fanSpeedValid",false),num(t,"fanSpeedPercent",0),t.value("fanSpeedTs",0LL)};
 c.battery={t.value("batteryPowerValid",false),t.value("batteryPowerEstimated",false),num(t,"chargeW",0),t.value("batteryPowerWindowMs",0ULL),"hwinfo"};c.batteryTs=t.value("batteryPowerTs",0LL);c.statusTs=t.value("batteryStatusTs",0LL);c.batteryTime={t.value("batteryTimeValid",false),t.value("batteryTimeEstimated",false),num(t,"remainMin"),"windows-estimated"};return c;
}
int main(int argc,char**argv){try{
 check(argc==3,"archive and result paths required");Case c;compare(c);
 c.hw.packagePower=30.12345;c.hw.freqMhz=2999.6;c.hw.tempC=91.4;c.hw.cpuPowerValid=c.hw.cpuClockValid=c.hw.cpuTempValid=true;c.hw.gpuPowerW=201.23456;c.hw.gpuClockMhz=2234.567;c.hw.gpuTempC=65.25;c.hw.gpuLoad=88.8;c.hw.cpuVoltageV=1.25;c.hw.gpuVoltageV=.95;c.hw.physicalMemory={8192,24576,32768,25};c.hw.gpuMemory={12288,4096,16384,75};c.hw.networkDownMBps=3.125;c.hw.networkUpMBps=0;c.hw.gpuSelectedPresent=c.hw.gpuPinned=true;std::strcpy(c.hw.gpuName.data(),"NVIDIA RTX 5080 / AMD Radeon / Intel Arc");c.hw.gpuMemorySource="hwinfo";c.cpuTs=c.ts;
 auto full=compare(c);check(full["tdpW"]==30.1&&full["freqMhz"]==3000,"legacy CPU rounding preserved");check(full["gpuPowerW"]==201.23456&&full["gpuClockMhz"]==2234.567,"GPU final precision preserved instead of dead initial rounding");
 const std::vector<double> vals={-1,0,.05,10.123,100,1000000,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
 for(bool healthy:{false,true})for(auto v:vals){Case x=c;x.healthy=healthy;x.hw.physicalMemory.used=v;x.hw.gpuMemory.load=v;x.hw.gpuVoltageV=v;x.hw.cpuVoltageV=v;x.hw.networkDownMBps=v;x.hw.gpuTempC=v;x.hw.gpuLoad=v;compare(x);}
 for(unsigned count:{0,1,16,32,64,128})for(bool stamp:{false,true}){Case x=c;x.coreTs=stamp?c.ts:0;for(unsigned i=0;i<count;++i)x.cores.push_back(i%7==0?json(nullptr):json((i*13)%101));auto p=compare(x);check(p.contains("coreUsage")==bool(count&&stamp),"core array availability gate unchanged");}
 for(const auto&cores:{json(nullptr),json::object(),json("invalid"),json(32)}){Case x=c;x.cores=cores;x.coreTs=c.ts;check(!compare(x).contains("coreUsage"),"non-array input does not invent cores");}
 for(bool valid:{false,true})for(long long stamp:{0LL,c.ts}){Case x=c;x.cpuValid=valid;x.cpuTs=stamp;auto p=compare(x);check(p.contains("cpuUsageValid")==bool(stamp>0||!valid),"legacy CPU proof omission unchanged");}
 for(bool present:{false,true})for(bool ac:{false,true})for(int pct:{-1,0,10,50,100,101})for(bool valid:{false,true})for(bool estimated:{false,true})for(double watts:{-42.123,0.,15.789}){
  Case x=c;x.hasBattery=present;x.ac=ac;x.percent=pct;x.remain=777;x.hw.chargeW=222;x.battery={valid,estimated,watts,60000,estimated?"ymcc-package-model":"windows-rate"};x.batteryTs=x.statusTs=c.ts;x.batteryTime={valid,estimated,123.45,estimated?"capacity-power-estimated":"hwinfo"};auto p=compare(x);const bool power=present&&pct>=0&&pct<=100&&valid,time=power&&!ac&&watts<-.05&&valid;check(p["batteryPowerValid"]==power&&p["batteryTimeValid"]==time,"battery power/time admission remains coherent");check(p["chargeW"]==(power?std::round(watts*10)/10:0)&&p["remainMin"]==(time?123:-1),"dead legacy battery arguments never overwrite effective receipts");
 }
 for(double value:vals)for(bool valid:{false,true}){Case x=c;x.cap={valid,value,c.ts};x.fan={true,valid,value,c.ts};x.files={value,value,c.ts};compare(x);}
 Case invalidName=c;invalidName.hw.gpuName[0]=char(0xff);invalidName.hw.gpuName[1]=char(0xc0);invalidName.hw.gpuName[2]=0;check(compare(invalidName)["gpuName"].is_string(),"invalid display-name bytes retain replacement serialization");
 std::ifstream input(argv[1]);json archives;input>>archives;unsigned archivePackets=0;for(const auto&row:archives){const auto reconstructed=historical(row["top"]);compare(reconstructed);++archivePackets;}
 json workloads=json::array();
 for(const auto mode:{0,1,2,3}){Case x=c;if(mode==1)x.healthy=false;if(mode==2){x.hasBattery=true;x.ac=false;x.percent=50;x.battery={true,true,-42.5,60000,"ymcc-package-model"};x.batteryTime={true,true,60,"capacity-power-estimated"};x.batteryTs=x.statusTs=c.ts;}if(mode==3){x.hw.gpuMemory={8192,-1,-1,-1};x.hw.gpuMemorySource="hwinfo-d3d-dedicated";}
  for(unsigned i=0;i<(mode==3?128u:32u);++i)x.cores.push_back(i%101);x.coreTs=c.ts;compare(x);std::size_t beforeAlloc=0,afterAlloc=0,beforeBytes=0,afterBytes=0;
  for(unsigned i=0;i<300;++i){x.ts+=1000;x.cpuTs=x.coreTs=x.ts;tracking=true;allocations=allocatedBytes=0;const auto a=publish(x,false);tracking=false;beforeAlloc+=allocations;beforeBytes+=allocatedBytes;tracking=true;allocations=allocatedBytes=0;const auto b=publish(x,true);tracking=false;afterAlloc+=allocations;afterBytes+=allocatedBytes;check(a==b,"allocation workload canonical bytes changed");++packets;}
  check(afterAlloc<beforeAlloc&&afterBytes<beforeBytes,"direct final-object construction did not reduce measured work");workloads.push_back({{"mode",mode},{"publications",300},{"beforeAllocations",beforeAlloc},{"afterAllocations",afterAlloc},{"beforeAllocatedBytes",beforeBytes},{"afterAllocatedBytes",afterBytes}});
 }
 json result={{"ok",true},{"checks",checks},{"canonicalPacketComparisons",packets},{"historicalReconstructedSourcePackets",archivePackets},{"workloads",workloads},{"wholeNativePublisherUsed",true},{"newSamplingOrUiIo",false},{"cpuPercentageOrTimingMeasured",false}};std::ofstream(argv[2])<<result.dump(2);std::cout<<"TOP_FINAL_OBJECT_PASS "<<checks<<" checks\n";
}catch(const std::exception&e){tracking=false;std::cerr<<e.what()<<"\n";return 1;}}
`.replaceAll('__ROOT__',root).replace('__STRUCTURE__',structure).replace('__BEFORE__',original).replace('__AFTER__',current);
fs.writeFileSync(out+'/top-final-object.cpp',code);if(!fs.existsSync(out+'/ARCHIVE-TOP-PUBLICATIONS.json'))fs.writeFileSync(out+'/ARCHIVE-TOP-PUBLICATIONS.json','[]');const bat=out+'/compile-top-final-object.bat';fs.writeFileSync(bat,`@echo off\r\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${out}/top-final-object.cpp" /Fo"${out}/top-final-object.obj" /Fe"${out}/top-final-object.exe"\r\nexit /b %ERRORLEVEL%\r\n`.replaceAll('\\r\\n','\r\n'));
const build=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+bat+'""'],{windowsVerbatimArguments:true,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:4e6});fs.writeFileSync(out+'/COMPILE.log',(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,build.stdout+build.stderr);
const run=spawnSync(out+'/top-final-object.exe',[out+'/ARCHIVE-TOP-PUBLICATIONS.json',out+'/TOP-FINAL-OBJECT-RESULT.json'],{windowsHide:true,encoding:'utf8',timeout:30000});fs.writeFileSync(out+'/RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');assert.equal(cut(fs.readFileSync('native/main.cpp','utf8'),'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()'),current);const r=JSON.parse(fs.readFileSync(out+'/TOP-FINAL-OBJECT-RESULT.json'));r.sourcePins={beforePublisher:sha(original),afterPublisher:sha(current),structure:sha(structure)};fs.writeFileSync(out+'/TOP-FINAL-OBJECT-RESULT.json',JSON.stringify(r,null,2));console.log(JSON.stringify({ok:r.ok,checks:r.checks,canonicalPacketComparisons:r.canonicalPacketComparisons,historicalReconstructedSourcePackets:r.historicalReconstructedSourcePackets,workloads:r.workloads},null,2));
