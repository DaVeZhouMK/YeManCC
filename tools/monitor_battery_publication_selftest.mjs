// Extract the actual sole publisher constructor, never hand-copy a payload fixture.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=process.cwd(),out=path.resolve(process.env.YMCC_BATTERY_TEST_OUT||path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/battery-generic-gamepad-20261011/tests'));
fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync('native/main.cpp','utf8');
const structure=source.slice(source.indexOf('struct NativeMonitorHw {'),source.indexOf('\n};',source.indexOf('struct NativeMonitorHw {'))+3);
const start=source.indexOf('static std::string nativeMonitorTopPayloadText('),end=source.indexOf('\nstatic void nativeMonitorLoop()',start);
if(start<0||end<start||!structure.startsWith('struct'))throw Error('publisher anchors missing');
const constructor=source.slice(start,end);
const header=`#include <string>\n#include <fstream>\n#include <iostream>\n#include <cmath>\n#include <cassert>\n#include "${root.replaceAll('\\','/')}/deps/json/json.hpp"\n#include "${root.replaceAll('\\','/')}/native/monitor_battery_energy.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_rtss_global.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_voltage.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_gpu_selection.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_fan_receipt.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_memory_network.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_filesystem_rates.h"\n#include "${root.replaceAll('\\','/')}/native/monitor_freshness.h"\nusing json=nlohmann::json;\n`;
const tests=`
int main(int argc,char** argv){
 assert(argc==4); NativeMonitorHw hw; hw.packagePower=15.5; hw.tempC=61; hw.chargeW=-8;
 const auto frozen=nativeMonitorTopPayloadText(hw,false,true,75,-1,12,1758350000000LL,false);
 std::ofstream(argv[1])<<frozen;const auto old=json::parse(frozen);
 assert(old["ts"]==1758350000000LL&&old["hwDown"]==true&&old["batteryPowerValid"]==false);
 const auto power=ymcc::monitorbattery::Power{true,true,-24,30000,"energy-delta"};
 const auto fresh=json::parse(nativeMonitorTopPayloadText(hw,false,true,75,-1,12,1758350000000LL,false,json::array(),0,power,1700000000000LL,1700000000000LL));
 assert(fresh["chargeW"]==-24&&fresh["batteryPowerValid"]==true&&fresh["batteryPowerEstimated"]==true);
 assert(fresh["batteryPowerWindowMs"]==30000&&fresh["batteryStatusTs"]==1700000000000LL&&fresh["batteryPowerTs"]==1700000000000LL);
 const auto desktop=json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,12,1700000000000LL,true));assert(desktop["batteryStatusTs"]==0&&desktop["batteryPowerValid"]==false);
 json bad=json::array();
 for(int i=0;i<3;++i){const auto ts=monitorDisplayTimestampMs(i!=0,i!=0,i==2?501:0,i!=2,1758350010000LL,1758350000000LL);bad.push_back({{"ts",ts},{"text",nativeMonitorTopPayloadText(hw,false,true,75,-1,12,ts,false)}});}
 std::ofstream(argv[2])<<bad.dump();
 json chain=json::array();
 auto chainCase=[&](const char* name,const ymcc::monitorbattery::Power& p,const ymcc::monitorbattery::EnergyReading& energy,bool ac,bool hasBattery,int percent,bool charging){
   const auto time=ymcc::monitorbattery::remainingTime(p,energy,hasBattery&&percent>=0&&percent<=100,true,ac,charging);
   chain.push_back({{"name",name},{"top",json::parse(nativeMonitorTopPayloadText(hw,ac,hasBattery,percent,time.minutes,12,1700000000000LL,false,json::array(),0,p,1700000000000LL,1700000000000LL,time))}});
 };
 ymcc::monitorbattery::PackagePowerModel model;
 const auto modeled=model.update({1000,true,true,false,false,true,30});
 chainCase("model",modeled,{true,false,60000},false,true,75,false);
 chainCase("real",ymcc::monitorbattery::measured(-24,"windows-rate"),{true,false,60000},false,true,75,false);
 chainCase("no-capacity",modeled,{},false,true,75,false);
 chainCase("percentage-with-reported-full-capacity",modeled,ymcc::monitorbattery::remainingEnergy(-1,80000,75),false,true,75,false);
 chainCase("no-battery-stale-power",modeled,{true,false,50000},false,false,-1,false);
 chainCase("unknown-percent",modeled,{true,false,50000},false,true,-1,false);
 chainCase("charging",ymcc::monitorbattery::measured(24,"windows-rate"),{true,false,50000},true,true,75,true);
 chainCase("empty",modeled,{true,false,0},false,true,0,false);
 assert(chain[0]["top"]["chargeW"]==-57&&chain[0]["top"]["remainMin"]==63&&chain[0]["top"]["batteryTimeEstimated"]==true);
 assert(chain[1]["top"]["chargeW"]==-24&&chain[1]["top"]["remainMin"]==150&&chain[1]["top"]["batteryPowerEstimated"]==false);
 assert(chain[2]["top"]["remainMin"]==-1&&chain[2]["top"]["batteryTimeValid"]==false);
 assert(chain[3]["top"]["remainMin"]==63&&chain[3]["top"]["batteryTimeValid"]==true);
 assert(chain[4]["top"]["chargeW"]==0&&chain[4]["top"]["batteryPowerValid"]==false&&chain[4]["top"]["batteryTimeValid"]==false);
 assert(chain[5]["top"]["batteryPowerValid"]==false);
 assert(chain[6]["top"]["remainMin"]==-1&&chain[6]["top"]["batteryTimeValid"]==false);
 assert(chain[7]["top"]["remainMin"]==0&&chain[7]["top"]["batteryTimeValid"]==true);
 std::ofstream(argv[3])<<chain.dump();
 std::cout<<"Actual sole publisher payload assertions PASS; frozen regression fixture generated\\n";
}
`;
fs.writeFileSync(path.join(out,'publisher-selftest.cpp'),header+structure+'\n'+constructor+tests);
fs.writeFileSync(path.join(out,'publisher-source-pin.json'),JSON.stringify({file:path.resolve('native/main.cpp'),constructorSha256:crypto.createHash('sha256').update(constructor).digest('hex'),structureSha256:crypto.createHash('sha256').update(structure).digest('hex'),scope:'extracted unchanged production constructor, synthetic arguments, no live battery'},null,2));
fs.writeFileSync(path.join(out,'build-publisher.cmd'),`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "${out}/publisher-selftest.cpp" /Fo"${out}/publisher.obj" /Fe"${out}/publisher-selftest.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/publisher-selftest.exe" "${out}/frozen-top-payload.json" "${out}/bad-states-top-payloads.json" "${out}/runtime-chain.json"\r\n`);
console.log('Publisher fixture prepared from actual source.');
