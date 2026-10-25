// Reuse the existing complete native parser/corpus harness, with this round's
// exact before headers and additional GPU aliases/adversarial continuations.
// CPU-work counts are reported separately from correctness; no CPU benchmark.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const out=path.resolve(process.env.YMCC_GPU_ROUTING_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/gpu-family-routing-20261016');fs.mkdirSync(out,{recursive:true});
let driver=fs.readFileSync('tools/monitor_cpu_unit_gates_selftest.mjs','utf8');
const gpuNames=['GPU Memory Allocated','GPU Memory Available','GPU Memory Usage','GPU Memory Total','GPU Memory Load','GPU Core Voltage','GPU Core Voltage (VDDCR_GFX)','GPU Core Voltage (VDDC)','GPU ASIC Voltage','GPU Voltage','GPU Temperature','GPU Core Temperature','GPU Temperature (Edge)','GPU Temperature (Diode)','GPU Power','GPU ASIC Power','GPU Chip Power','Total Graphics Power','GPU Power Limit','GPU Power Rated','GPU Power Maximum','GPU Power Capability','GPU Power TGP','GPU Power TBP','GPU Power Foo','GPU Memory GPU Power','GPU Core GPU Power','GPU Voltage GPU Power','GPU ASIC GPU Power','GPU Temperature GPU Power','GPU Utilization GPU Power','GPU D3D GPU Power','GPU Clock','GPU Core Clock','GPU Clock (Effective)','GPU Core Load','GPU Utilization','GPU D3D Usage','GPUPrefix GPU Power','GPU','GP','GPU Fan','GPU Memory Clock','GPU Core Clock Battery Power','GPU Core Load Remaining Time','GPU Power CPU Package Power','GPU Core Temperature CPU Tctl','gpu clock','gPu TeMpErAtUrE','GPU Voltage\0GPU Power','GPU \u00e9 GPU Power','GPU \u00e9emory allocated','GPU  GPU Power','GPU\tClock'];
const units='const units=';assert(driver.includes(units));driver=driver.replace(units,'names.push(...'+JSON.stringify(gpuNames)+');\n'+units);
const extra=String.raw`
 measure("GPU-memory-only",{{"GPU Memory Allocated","MB",2048,2},{"GPU Memory Available","MB",6144,2},{"GPU Memory Usage","%",25,2},{"GPU Memory Total","MB",8192,2},{"GPU Memory Load","%",25,2}});
 measure("GPU-clock-load-power",{{"GPU Clock","MHz",2475,2},{"GPU Core Load","%",95,2},{"GPU Power","W",82,2}});
 measure("GPU-unknown-rows",{{"GPU Fan1","RPM",1000,2},{"GPU Memory Clock","MHz",2000,2},{"GPU Memory Junction Temperature","C",65,2},{"GPU Hot Spot Temperature","C",70,2}});
`;
assert(driver.includes(' measure("RAM-and-network"'));driver=driver.replace(' measure("RAM-and-network"',extra+'\n measure("RAM-and-network"');
const strict="assert(a.foldedBytes<=b.foldedBytes,name+' must not increase ASCII folding work');if(name==='archived-original-values'||name==='CPU-and-drive-only'||name==='clock-and-battery')assert(a.foldedBytes<b.foldedBytes,name+' must reduce folding work');";
assert(driver.includes(strict));driver=driver.replace(strict,"assert(a.foldedBytes<=b.foldedBytes,name+' must not increase measured folding work');if(name.startsWith('GPU-')||name==='archived-original-values')assert(a.foldedBytes<b.foldedBytes,name+' must reduce measured folding work');");
const file=path.join(out,'native-corpus-driver.mjs');fs.writeFileSync(file,driver);
const run=spawnSync(process.execPath,[file],{env:{...process.env,YMCC_CPU_GATES_OUT:out},encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:5e6});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-7000));assert.equal(run.status,0);
