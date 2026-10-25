// Existing unified GPU packets: copy only occupied values. Uses the prior slot
// lifecycle corpus plus direct byte probes; no hardware or timing benchmark.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_GPU_COPY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/gpu-active-copy-20261016').replaceAll('\\','/'),out=task+'/native';fs.mkdirSync(out,{recursive:true});fs.mkdirSync(task+'/parser',{recursive:true});
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j);};
const base=fs.readFileSync('tools/monitor_gpu_active_slots_selftest.mjs','utf8').replaceAll('\r\n','\n');let harness=cut(base,'const harness=String.raw`','\n`;\nconst results=').slice('const harness=String.raw`'.length).replace('constexpr bool modern=MODERN;','constexpr bool modern=true;');
fs.copyFileSync('../../../Build/Tasks/YMCC-Steam-HUD-Probe/gpu-active-slots-20261016/ORIGINAL-ROWS.json',task+'/ORIGINAL-ROWS.json');
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{encoding:'utf8',env:{...process.env,YMCC_MONITOR_MEMORY_OUT:task+'/parser'}});assert.equal(gen.status,0,gen.stderr);const generated=fs.readFileSync(task+'/parser/memory-publication.cpp','utf8');
const probes=String.raw`
    // Harness-only access to object bytes of the fixed trivial slot array.
    static constexpr std::size_t monitorTestSlotBytes() noexcept {return sizeof(Slot);}
    void monitorTestPoisonUnused(unsigned char byte) noexcept {
        std::memset(devices_.data()+count_,byte,(maxDevices-count_)*sizeof(Slot));
    }
    std::size_t monitorTestUnusedMatches(unsigned char byte) const noexcept {
        const auto* p=reinterpret_cast<const unsigned char*>(devices_.data());
        std::size_t matches=0;
        for(std::size_t i=count_*sizeof(Slot);i<sizeof(devices_);++i)if(p[i]==byte)++matches;
        return matches;
    }
`;
const added=String.raw`
 json copyWork=json::object();
 for(unsigned count:std::vector<unsigned>{0,1,2,7,63,64}){
  ymcc::monitorgpu::Readings source;for(unsigned i=0;i<count;++i)add(source,i,"GPU Power","W",double(i));source.monitorTestPoisonUnused(0xA5);
  const auto unused=(64u-count)*source.monitorTestSlotBytes();
  for(unsigned operation=0;operation<4;++operation){
   alignas(ymcc::monitorgpu::Readings) std::array<unsigned char,sizeof(ymcc::monitorgpu::Readings)> storage;
   std::size_t overwritten=0;bool values=true;allocations=0;trackAllocations=true;
   for(unsigned i=0;i<200;++i){
    std::memset(storage.data(),0x5A,storage.size());
    if(count)add(source,0,"GPU Power","W",double(operation*200+i));
    ymcc::monitorgpu::Readings* target=nullptr;
    if(operation==0)target=::new(static_cast<void*>(storage.data()))ymcc::monitorgpu::Readings(source);
    if(operation==1){target=::new(static_cast<void*>(storage.data()))ymcc::monitorgpu::Readings;target->monitorTestPoisonUnused(0x5A);*target=source;}
    if(operation==2)target=::new(static_cast<void*>(storage.data()))ymcc::monitorgpu::Readings(std::move(source));
    if(operation==3){target=::new(static_cast<void*>(storage.data()))ymcc::monitorgpu::Readings;target->monitorTestPoisonUnused(0x5A);*target=std::move(source);}
    overwritten+=target->monitorTestUnusedMatches(0xA5);
    values=values&&target->size()==count&&(!count||target->at(0).power==double(operation*200+i));
    target->~Readings();
   }
   trackAllocations=false;
   check(("copy/move operation "+std::to_string(operation)+" preserves "+std::to_string(count)+" values without allocation").c_str(),values&&allocations==0);
   check(("copy/move operation "+std::to_string(operation)+" touches no unused slot bytes for "+std::to_string(count)+" GPUs").c_str(),overwritten==(COPY_ACTIVE?0u:200u*unused));
   copyWork[std::to_string(count)][std::to_string(operation)]={{"operations",200},{"unusedOverwrittenBytes",overwritten},{"heapAllocations",allocations},{"slotBytes",source.monitorTestSlotBytes()}};
  }
 }
 {ymcc::monitorgpu::Readings source;add(source,2,"GPU Power","W",50);source.retainMemoryEvidence([](uint32_t){return ymcc::monitormemory::D3DMemory{0,256};});const auto expected=all(source);source=source;source=std::move(source);check("self copy/move preserves values and D3D evidence",all(source)==expected&&source.diagnosticD3D(&source.at(0)).dedicated==0);ymcc::monitorgpu::Readings destination;for(unsigned i=0;i<64;++i)add(destination,i,"GPU Power","W",double(i));destination=source;check("shrinking assignment hides all old destination slots",destination.size()==1&&destination.find(identity(3))==nullptr&&all(destination)==expected);auto moved=std::move(destination);check("moved source remains safely usable and can be reset",destination.size()==1&&destination.at(0).power==50&&moved.diagnosticD3D(&moved.at(0)).dedicated==0);destination={};add(destination,1,"GPU Core Load","%",27);check("admitting after shrink/reset cannot reuse old metrics",destination.size()==1&&destination.at(0).load==27&&destination.at(0).power==-1&&!destination.hasMemoryEvidence());}
 {NativeMonitorHw source;add(source.gpuReadings,1,"GPU Power","W",5);add(source.gpuReadings,2,"GPU Power","W",50);source.gpuReadings.monitorTestPoisonUnused(0xA5);NativeMonitorHw target;target.gpuReadings.monitorTestPoisonUnused(0x5A);target=source;check("actual native packet assignment copies only active GPU fields",all(target.gpuReadings)==all(source.gpuReadings)&&target.gpuReadings.monitorTestUnusedMatches(0xA5)==(COPY_ACTIVE?0u:62u*source.gpuReadings.monitorTestSlotBytes()));auto cache=std::make_unique<ymcc::monitorparse::Reuse<NativeMonitorHw>>();auto b=buffer({{"GPU Power","W",5,1},{"GPU Power","W",50,2}},2000);NativeMonitorHw cold;assert(parse(b,cold,cache.get()));NativeMonitorHw hit;hit.gpuReadings.monitorTestPoisonUnused(0x5A);assert(parse(b,hit,cache.get()));check("real cache commit/restore preserves valid values after active copies",all(cold.gpuReadings)==all(hit.gpuReadings));}
`;
assert(harness.includes(' json work=json::object();'));harness=harness.replace(' json work=json::object();',added+'\n json work=json::object();').replace('{"work",work}','{"work",work},{"copyWork",copyWork}');
const results={};for(const side of ['before','after']){
 const source=fs.readFileSync(side==='before'?task+'/before/native/main.cpp':'native/main.cpp','utf8');let header=generated.slice(0,generated.indexOf('struct NativeMonitorHw {'));
 let gpu=fs.readFileSync(side==='before'?task+'/before/native/monitor_gpu_selection.h':'native/monitor_gpu_selection.h','utf8');assert(gpu.includes('double power=-1,clock=-1,load=-1,temperature=-1;'));gpu=gpu.replace('double power=-1,clock=-1,load=-1,temperature=-1;','double power=(++deviceInitializations,-1.0),clock=-1,load=-1,temperature=-1;');const location='    std::size_t size()const{return count_;}';assert(gpu.includes(location));gpu=gpu.replace(location,probes+'\n'+location).replace('#include "monitor_memory_network.h"','#include "'+root+'/native/monitor_memory_network.h"').replace('#include "monitor_voltage.h"','#include "'+root+'/native/monitor_voltage.h"');fs.writeFileSync(out+'/'+side+'-gpu.h',gpu);header=header.replace(root+'/native/monitor_gpu_selection.h',out+'/'+side+'-gpu.h');
 const prefix='#include <cstddef>\nstatic std::size_t deviceInitializations=0;\n'+header+cut(source,'struct NativeMonitorHw {','\n};')+'\n};\n'+cut(source,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO(')+'\n'+cut(source,'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()');const file=out+'/'+side;fs.writeFileSync(file+'.cpp',prefix+'\n#define COPY_ACTIVE '+(side==='after'?1:0)+'\n'+harness);
 fs.writeFileSync(file+'.cmd','@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "'+file+'.cpp" /Fo"'+file+'.obj" /Fe"'+file+'.exe"\r\nif errorlevel 1 exit /b 1\r\n"'+file+'.exe" "'+file+'.json" "'+task+'/ORIGINAL-ROWS.json"\r\nexit /b %errorlevel%\r\n');
 const p=spawnSync('cmd.exe',['/d','/c',file+'.cmd'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(file+'.log',(p.stdout||'')+(p.stderr||''));console.log(side,((p.stdout||'')+(p.stderr||'')).slice(-2600));assert.equal(p.status,0);results[side]=JSON.parse(fs.readFileSync(file+'.json'));
}
assert.deepEqual(results.after.cases,results.before.cases);assert.deepEqual(results.after.nativeCases,results.before.nativeCases);assert.deepEqual(results.after.work,results.before.work);assert.equal(results.after.packetBytes,results.before.packetBytes);assert.equal(results.after.gpuBytes,results.before.gpuBytes);
const summary={ok:true,checks:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.checks.length])),equalPublishedCases:Object.keys(results.after.cases).length,equalNativeCases:Object.keys(results.after.nativeCases).length,copyWork:Object.fromEntries(Object.entries(results).map(([s,r])=>[s,r.copyWork])),packetBytes:results.after.packetBytes,gpuBytes:results.after.gpuBytes,sourceSha256:Object.fromEntries(['native/main.cpp','native/monitor_gpu_selection.h','native/monitor_memory_network.h','native/monitor_parse_reuse.h','tools/monitor_gpu_active_slots_selftest.mjs'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')])),hardwareSampled:false,scope:'Frozen/current production GPU copy operations plus actual parser/publisher and cache; harness-only byte probes observe overwritten unused slot bytes. These are destination-byte observations, not CPU timings or instruction counts.'};fs.writeFileSync(task+'/NATIVE-RESULT.json',JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:summary.checks,equalPublishedCases:summary.equalPublishedCases,equalNativeCases:summary.equalNativeCases,assignmentTwoGpuBefore:summary.copyWork.before['2']['1'],assignmentTwoGpuAfter:summary.copyWork.after['2']['1'],packetBytes:summary.packetBytes},null,2));
