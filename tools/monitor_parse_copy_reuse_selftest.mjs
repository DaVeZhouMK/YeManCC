// Actual frozen/current parser + wrapper; reuse the existing broad fixtures.
// Only count the existing row-scratch memcpy and semantic row visits.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_PARSE_COPY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/source-copy-reuse-20261016').replaceAll('\\','/');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.mkdirSync(task+'/parser',{recursive:true});fs.mkdirSync(task+'/native',{recursive:true});
const gen=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{env:{...process.env,YMCC_MONITOR_MEMORY_OUT:task+'/parser'},encoding:'utf8',windowsHide:true});assert.equal(gen.status,0,gen.stdout+gen.stderr);
const generated=fs.readFileSync(task+'/parser/memory-publication.cpp','utf8'),fixtureSource=fs.readFileSync('tools/monitor_parse_reuse_selftest.mjs','utf8');
const raw=name=>{const re=new RegExp('const '+name+'=String.raw`([\\s\\S]*?)`;');const m=fixtureSource.match(re);assert(m,'missing shared fixture '+name);return m[1]};
const controls=raw('controls')+'\nstatic std::uint64_t rowCopyBytes=0,rowCopyCalls=0;\n',mocks=raw('mocks');
let harness=raw('harness');
const extra=String.raw`
 // 1Hz ordinary worker input; stable contents still need the original stamp
 // and identity checks, but do not need a new scratch copy on a cache hit.
 json copyWork=json::array();
 for(const auto period:{1000u,1500u,2000u,3000u,6000u,60000u}){
  auto rows=simple;while(rows.size()<2048)rows.push_back({"Unused row "+std::to_string(rows.size()),"W",double(rows.size()),0});
  auto data=buffer(rows,period,268,64);Reuse copyOwner;
  std::uint64_t workloadCopies=0,workloadBytes=0;bool allValid=true;
  for(unsigned i=0;i<300;++i){
   const auto poll=1700000000ULL+((i*1000u)/period)*period/1000u;put(data,12,poll);
   auto h=std::make_unique<NativeMonitorHw>();rowCopyBytes=rowCopyCalls=0;
   const bool ok=parse(data,*h,&copyOwner);
   workloadCopies+=rowCopyCalls;workloadBytes+=rowCopyBytes;
   allValid=allValid&&ok&&h->sharedOk&&h->producerPollTime==poll&&h->cpuPowerValid;
  }
  check("300 ordinary inputs preserve exact producer time and actual CPU readings",allValid);
  const std::uint64_t expected=OWNED_BUFFER&&period>=1500?0u:(FAST_COPY?300000u/period:300u);
  check("normal workload executes exactly the required bounded scratch copies",workloadCopies==expected&&workloadBytes==expected*2048u*316u);
  copyWork.push_back({{"kind","advancing-polls"},{"periodMs",period},{"calls",300},{"rows",rows.size()},{"rowCopies",workloadCopies},{"rowCopyBytes",workloadBytes}});
 }
 // None of the unsupported/big/no-owner paths gains a live-value shortcut.
 for(unsigned mode=0;mode<4;++mode){
  const auto period=mode==0?1000u:mode==1?0u:3000u;
  auto rows=simple;if(mode==2)while(rows.size()<3400)rows.push_back({"Unused large row","W",1,0});
  auto data=buffer(rows,period);Reuse fallbackOwner;
  std::uint64_t copies=0,bytes=0;
  for(unsigned i=0;i<12;++i){rowCopyBytes=rowCopyCalls=0;NativeMonitorHw h;assert(parse(data,h,mode==3?nullptr:&fallbackOwner));copies+=rowCopyCalls;bytes+=rowCopyBytes;}
  check("unsupported or absent reuse owner retains its bounded copy path",copies==12&&bytes==12u*rows.size()*316u);
  copyWork.push_back({{"kind","ordinary-copy-required"},{"mode",mode},{"calls",12},{"rows",rows.size()},{"rowCopies",copies},{"rowCopyBytes",bytes}});
 }
 // Reused cache records must always describe the scratch bytes actually parsed.
 {
  auto data=buffer(simple,3000);Reuse correctionOwner;NativeMonitorHw old;
  assert(parse(data,old,&correctionOwner));rowCopyBytes=rowCopyCalls=0;NativeMonitorHw same;assert(parse(data,same,&correctionOwner));
  check("unchanged producer rows bypass only the scratch copy",rowCopyCalls==(FAST_COPY?0u:1u)&&capture(old)==capture(same));
  const auto ro=48u+32u*268u;put(data,ro+284,37.0);rowCopyBytes=rowCopyCalls=0;
  NativeMonitorHw correction;assert(parse(data,correction,&correctionOwner));
  check("same-poll CPU value correction forces reparsing through owned or scratch rows",rowCopyCalls==(OWNED_BUFFER?0u:1u)&&correction.packagePower==37.0&&correction.producerPollTime==old.producerPollTime);
  rowCopyBytes=rowCopyCalls=0;NativeMonitorHw correctedRepeat;assert(parse(data,correctedRepeat,&correctionOwner));
  check("corrected source becomes reusable without aliasing live bytes",rowCopyCalls==(FAST_COPY?0u:1u)&&capture(correction)==capture(correctedRepeat));
 }
`;
const marker=' json result={{"ok",failures==0}';assert(harness.includes(marker));harness=harness.replace(marker,extra+marker).replace('{"hardwareReads",false}', '{"copyWork",copyWork},{"hardwareReads",false}');
harness=harness.replace(/Reuse (\w+);/g,(_,n)=>'auto '+n+'Storage=std::make_unique<Reuse>();auto& '+n+'=*'+n+'Storage;');
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i);return s.slice(i,j)};
const original='../../../Build/Tasks/YMCC-Steam-HUD-Probe/amd-d3d-used-source-20261016/LOCAL-ORIGINAL-ROWS.json';fs.copyFileSync(original,task+'/ORIGINAL-ROWS.json');
const sides=process.argv.includes('--before-only')?['before']:['before','after'],results={};
for(const side of sides){
 const source=fs.readFileSync(side==='before'?task+'/before/native/main.cpp':'native/main.cpp','utf8');
 let parser=cut(source,'static bool monitorParseHWiNFOView(','static bool monitorReadHWiNFO(');
 const row='for (uint32_t i = 0; i < before.readingCount; ++i) {';assert.equal(parser.split(row).length,2);parser=parser.replace(row,row+'++primaryRows;');
 const copy='memcpy(parseScratch.data(), view + before.readingOffset, copyBytes);';assert.equal(parser.split(copy).length,2);parser=parser.replace(copy,copy+'rowCopyBytes+=copyBytes;++rowCopyCalls;');
 let header=generated.slice(0,generated.indexOf('struct NativeMonitorHw {'));assert(header.includes('static void monitorTestBarrierPause(int){}'));header=header.replace('static void monitorTestBarrierPause(int){}',controls);
 const body=header+cut(source,'struct NativeMonitorHw {','\n};')+'\n};\n'+parser+'\n'+mocks+'\n'+cut(source,'static bool monitorReadHWiNFO(','// Remains the sole CPU sampling entry')+'\n'+cut(source,'static std::string nativeMonitorTopPayloadText(','\nstatic void nativeMonitorLoop()')+'\n#define MODERN 1\n#define OWNED_BUFFER '+(source.includes('rows = reuse->stagedRows();')?1:0)+'\n#define FAST_COPY '+(side==='after'?1:0)+'\n'+harness;
 const dir=task+'/native/'+side;fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(dir+'/copy.cpp',body);
 const batch=dir+'/compile.bat',vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 fs.writeFileSync(batch,`@echo off\r\ncall "${vc}" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DUNICODE /D_UNICODE "${dir}/copy.cpp" /Fo"${dir}/copy.obj" /Fe"${dir}/copy.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
 const c=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(dir+'/COMPILE.log',(c.stdout||'')+(c.stderr||''));assert.equal(c.status,0,c.stdout+c.stderr);
 const run=spawnSync(dir+'/copy.exe',[dir+'/RESULT.json',task+'/ORIGINAL-ROWS.json'],{encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(dir+'/RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);results[side]=JSON.parse(fs.readFileSync(dir+'/RESULT.json'));assert(results[side].ok);fs.copyFileSync(dir+'/RESULT.json',task+'/native/'+side+'.json');
 console.log(JSON.stringify({side,checks:results[side].checks.length,copyWork:results[side].copyWork}));
}
if(results.after){
 assert.deepEqual(results.after.cases,results.before.cases,'all existing native output cases must match');
 assert.deepEqual(results.after.work,results.before.work,'original parser/allocator reuse work must not regress');
 fs.writeFileSync(task+'/NATIVE-RESULT.json',JSON.stringify({ok:true,checks:{before:results.before.checks.length,after:results.after.checks.length},equalCompleteCases:Object.keys(results.after.cases).length,work:{before:results.before.copyWork,after:results.after.copyWork},sourceSha256:{parser:sha(fs.readFileSync('native/main.cpp')),reuseHeader:sha(fs.readFileSync('native/monitor_parse_reuse.h')),sharedFixtureBuilder:sha(fs.readFileSync('tools/monitor_parse_reuse_selftest.mjs'))},hardwareReads:false,timingOrCpuPercentageMeasured:false,actualProductionParserAndWrapper:true},null,2)+'\n');
}
