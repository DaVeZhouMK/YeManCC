// Actual parser/publisher across explicitly synthetic machine-shaped packets.
// Existing recorded local rows are also reencoded; no live sensor reads.
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_MACHINE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cross-machine-audit-20261015/parser');fs.mkdirSync(out,{recursive:true});process.env.YMCC_MONITOR_MEMORY_OUT=out;await import('./monitor_memory_publication_selftest.mjs');
let source=fs.readFileSync(out+'/memory-publication.cpp','utf8');
if(process.env.YMCC_MONITOR_MACHINE_BASELINE){const prior=fs.readFileSync(process.env.YMCC_MONITOR_MACHINE_BASELINE,'utf8'),current=fs.readFileSync('native/main.cpp','utf8');const cut=text=>{const a=text.indexOf('static bool monitorParseHWiNFOView('),b=text.indexOf('static bool monitorReadHWiNFO(',a);if(a<0||b<a)throw Error('parser boundary');return text.slice(a,b);};source=source.replace(cut(current),cut(prior));}
const marker=' if(argc>2){std::ifstream in(argv[2]);';if(!source.includes(marker))throw Error(marker);
const cases=String.raw`
 int faultFailures=0;auto evidence=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";if(!ok)++faultFailures;};
 p=parse({{"CPU Package Power","W",30},{"CPU Package","C",70},{"Core Clock","MHz",3500},{"CPU Core Voltage","mV",850}});assert(p["cpuPowerValid"]==true&&p["tdpW"]==30&&p["cpuTempValid"]==true&&p["tempC"]==70&&p["cpuClockValid"]==true&&p["cpuVoltageV"]==.85);save("synthetic-Intel-package-sensors-not-G3E-live-rows",p);
 p=parse({{"CPU Package Power","W",120},{"CPU (Tctl/Tdie)","C",65},{"CPU Core Voltage (SVI3 TFN)","V",1.1}});assert(p["tdpW"]==120&&p["tempC"]==65&&p["cpuVoltageV"]==1.1);save("synthetic-AMD-package-and-SVI-sensors",p);
 p=parse({{"GPU Clock","MHz",1800,1},{"GPU Core Load","%",80,1},{"GPU Temperature","C",65,1}});assert(p["gpuSelectionPresent"]==true&&p["gpuPowerW"].is_null()&&p["gpuUsagePct"]==80&&p["gpuClockMhz"]==1800);save("single-iGPU-without-power-never-invents-zero-W",p);
 p=parse({{"Core VIDs","V",1.2},{"GPU VID","V",1.1}});assert(p["cpuVoltageV"].is_null()&&p["gpuVoltageV"].is_null());save("requested-voltage-not-used-as-measured-rail",p);
 {
  auto b=buffer({{"Charge Rate","mW",-24000,6},{"Estimated Remaining Time","s",7200,6}});NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(b.data(),b.size(),hw,unstable));assert(hw.chargeWFound&&hw.chargeW==-24&&hw.remainMin==120);save("single-battery-mW-and-seconds",{{"chargeW",hw.chargeW},{"remainMin",hw.remainMin}});
 }
 {
  auto b=buffer({{"Discharge Rate","W",24,6}});NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(b.data(),b.size(),hw,unstable));assert(hw.chargeWFound&&hw.chargeW==-24);save("positive-discharge-magnitude-becomes-signed-W",{{"chargeW",hw.chargeW}});
 }
 {
  auto b=buffer({{"Charge Rate","W",-8,6},{"Charge Rate","W",-4,7}});NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(b.data(),b.size(),hw,unstable));evidence("multiple HWiNFO battery groups cannot prove system watts",!hw.chargeWFound);save("multi-battery-partial-last-row-never-claims-system-power",{{"chargeWFound",hw.chargeWFound}});
 }
 {
  auto b=buffer({{"Estimated Remaining Time","min",60,6},{"Estimated Remaining Time","min",120,7}});NativeMonitorHw hw;bool unstable=false;assert(monitorParseHWiNFOView(b.data(),b.size(),hw,unstable));evidence("multiple HWiNFO battery groups cannot prove system runtime",hw.remainMin<0);save("multi-battery-last-pack-duration-never-claims-system-duration",{{"remainMin",hw.remainMin}});
 }
`;
const main=fs.readFileSync('native/main.cpp','utf8');
if(!main.includes('hwHealthy && hw.chargeWFound && !batteryFallback.hasMultipleSystemBatteries()')||!main.includes('hw.remainMin >= 0 && !batteryFallback.hasMultipleSystemBatteries()'))throw Error('sole worker must fence partial HWiNFO data when existing Windows cache confirms multiple packs');
source=source.replace(marker,cases+" if(faultFailures)return 2;\n"+marker);fs.writeFileSync(out+'/memory-publication.cpp',source);const run=spawnSync('cmd.exe',['/d','/c',out+'/build-memory-test.cmd'],{cwd:out,encoding:'utf8',maxBuffer:2e6});fs.writeFileSync(out+'/MACHINE-PARSER-BUILD.log',(run.stdout||'')+(run.stderr||''));const report={ok:run.status===0,exitCode:run.status,scope:'Actual HWiNFO parser/sole publisher; machine-shaped synthetic label fixtures are not attributed to missing G3E sensor logs.'};if(report.ok)report.scenarios=JSON.parse(fs.readFileSync(out+'/NATIVE-MEMORY-PAYLOADS.json')).length;fs.writeFileSync(out+'/MACHINE-PARSER-RESULT.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));if(!report.ok){console.log(((run.stdout||'')+(run.stderr||'')).slice(-2000));process.exitCode=2;}
