// R119 fixed normal-submit health projection cost. Runs only an inert fixture.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const out=path.join(workspace,'Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/recovery-submit-cost');
const opts={rounds:7,iterations:200000};
let boundaryOnly=false;
for(let i=2;i<process.argv.length;++i){
  const key=process.argv[i];
  if(key==='--ineligible-boundary-only'){boundaryOnly=true;continue;}
  if(key==='--help'){console.log('node tools/recovery_submit_cost_r119_selftest.mjs [--rounds 3..15] [--iterations 10000..1000000] [--ineligible-boundary-only]');process.exit(0);}
  assert(['--rounds','--iterations'].includes(key),'Unknown option '+key);opts[key.slice(2)]=Number(process.argv[++i]);
}
assert(Number.isInteger(opts.rounds)&&opts.rounds>=3&&opts.rounds<=15);
assert(Number.isInteger(opts.iterations)&&opts.iterations>=10000&&opts.iterations<=1000000);
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:hash(bytes)};};
const source=load(path.join(repo,'native/main.cpp'));
const fixture=load(path.join(repo,'tools/fixtures/recovery_submit_cost_r119/fixture.cpp'));
const runner=load(fileURLToPath(import.meta.url));
const deps=['native/input_host_recovery_health.h','native/virtual_output_status.h','deps/json/json.hpp'].map(p=>load(path.join(repo,p)));
const mask=source.text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' '));
const ranges=[];
function balanced(start,open='{',close='}'){
  let depth=0;for(let i=start;i<mask.length;++i){if(mask[i]===open)++depth;if(mask[i]===close&&--depth===0)return i+1;}
  throw Error('Unclosed source block '+start);
}
function record(name,start,end,executed=true){
  assert(start>=0&&end>start,name+' missing');const code=source.text.slice(start,end);
  ranges.push({name,file:source.file,startLine:source.text.slice(0,start).split('\n').length,endLine:source.text.slice(0,end).split('\n').length,sha256:hash(code),executed,code});return code;
}
function fn(name,executed=true){
  for(const m of source.text.matchAll(new RegExp('^static[^\\r\\n(;{}]*\\b'+name+'\\s*\\(','gm'))){
    const begin=mask.indexOf('(',m.index);let end=balanced(begin,'(',')');while(/\s/.test(mask[end]))++end;
    if(mask[end]==='{')return record(name,m.index,balanced(end),executed);
  }throw Error('Missing definition '+name);
}
function statement(name,needle,after=0){const start=source.text.indexOf(needle,after);return record(name,start,source.text.indexOf(';',start)+1);}
const snapshotStart=source.text.indexOf('struct InputCaptureGamepadSnapshot {');
const snapshot=record('InputCaptureGamepadSnapshot',snapshotStart,balanced(mask.indexOf('{',snapshotStart))+1);
const names=['currentPowerGeneration','inputHostConfiguredPersona','inputHostPersona','inputHostBoundPersonaValue',
  'inputHostExitFenceRaised','sgVirtualWriterAllowedNow','physicalReleaseWindowActive','msiClawKnownBoardAuthority',
  'msiControllerRepairScope','msiDinputMainSourceEligible','inputHostRecoveryHealthTrackingLocked',
  'inputHostRecoveryHealthContextLocked','inputHostRecoveryHealthSource','inputHostRecoveryHealthLatestSourceLocked',
  'inputHostRecoveryHealthObserveLocked','inputHostRecoveryHealthAcceptedLocked'];
const functions=Object.fromEntries(names.map(n=>[n,fn(n)]));
const submitStart=source.text.indexOf('static json inputHostSubmitPad(');
assert(submitStart>=0);
const requestStart=source.text.indexOf('            const auto recoveryHealthRequestContext =',submitStart);
const request=record('submit-request-context-and-source',requestStart,source.text.indexOf('            accepted = frameTextReady',requestStart));
const accepted=statement('submit-accepted-health-call','inputHostRecoveryHealthAcceptedLocked(recoveryHealthRequestContext,',requestStart);
const observe=statement('submit-observe-health-call','inputHostRecoveryHealthObserveLocked(physicalSourceAdmitted && !semanticNeutral);',submitStart);
fn('physicalReleaseWindowAdmissionBlocked',false);
fn('inputHostRequestPreparedLocked',false);
// Bind the caller lock regions without extracting the whole Submit or transport.
for(const [name,start,end]of [
  ['submit-observe-caller-lock',source.text.lastIndexOf('        std::lock_guard<InputHostMutex> lock(g_inputHostMx);',source.text.indexOf(observe,submitStart)),source.text.indexOf(observe,submitStart)+observe.length],
  ['submit-request-caller-lock',source.text.lastIndexOf('        std::lock_guard<InputHostMutex> lock(g_inputHostMx);',requestStart),requestStart+request.length]
])record(name,start,end,false);
assert(request.includes('inputHostRecoveryHealthContextLocked(false)'));
assert(functions.inputHostRecoveryHealthAcceptedLocked.includes('inputHostRecoveryHealthContextLocked(tracking)'));
assert(functions.inputHostRecoveryHealthAcceptedLocked.includes('tracking ? inputHostRecoveryHealthLatestSourceLocked(eligible) : source'));
assert(functions.inputHostRecoveryHealthSource.includes('xinput-any-connected'));
const eventMap={inputHostConfiguredPersona:'ConfiguredCopy',inputHostBoundPersonaValue:'BoundCopy',inputHostPersona:'Persona',
  msiClawKnownBoardAuthority:'Board',msiControllerRepairScope:'Repair',msiDinputMainSourceEligible:'Eligible',
  inputHostRecoveryHealthTrackingLocked:'Tracking',inputHostRecoveryHealthContextLocked:'Context',inputHostRecoveryHealthSource:'Source',
  inputHostRecoveryHealthLatestSourceLocked:'Latest',inputHostRecoveryHealthObserveLocked:'ObserveAdapter',inputHostRecoveryHealthAcceptedLocked:'AcceptedAdapter'};
const instrumentation=[];
function instrumentFunctions(counted,mutation){
  return names.map(name=>{
    let code=functions[name];
    if(mutation==='skip-idle-accepted'&&name==='inputHostRecoveryHealthAcceptedLocked')code=code.replace('const bool tracking = inputHostRecoveryHealthTrackingLocked();','const bool tracking = inputHostRecoveryHealthTrackingLocked();\n    if (!tracking) return;');
    if(mutation==='eligible-shortcut'&&name==='inputHostRecoveryHealthSource'){
      const before='if (!source) return {};';assert(code.includes(before));
      code=code.replace(before,'if (!source || !eligible) return {};');
    }
    // Mutex type alias is std::mutex in timing, a delegating probe in counts.
    code=code.replaceAll('std::mutex','FixtureMutex');
    if(counted){
      if(eventMap[name]){const i=code.indexOf('{');let extra=' R119_EVENT('+eventMap[name]+');';
        if(name==='inputHostConfiguredPersona')extra+=' counts.copiedBytes+=g_realStickTest.virtualTargetPersona.size();';
        if(name==='inputHostBoundPersonaValue')extra+=' counts.copiedBytes+=g_inputHostBoundPersona.size();';
        code=code.slice(0,i+1)+extra+code.slice(i+1);
      }
      code=code.replace(/source->inputSource == "[^"]+"/g,s=>'(R119_EVENT(RouteCompare), '+s+')');
      code=code.replace(/(?:g_machineIdentity\.manufacturer|p)\.find\("[^"]+"\)/g,s=>'(R119_EVENT(BoardFind), '+s+')');
    }
    return code;
  }).join('\n\n');
}
function instrumentHeader(dep,counted){
  if(!counted)return dep.bytes;
  let text=dep.text;
  const targets=path.basename(dep.file)==='input_host_recovery_health.h'
    ?[['inline bool sameSource(', 'SameSource'],['inline bool fresh(', 'Fresh'],['static bool valid(', 'WindowValid'],['void clear()', 'WindowClear'],['bool observe(', 'WindowObserve'],['void accepted(', 'WindowAccepted']]
    :path.basename(dep.file)==='virtual_output_status.h'?[['inline bool virtualOutputSourceCurrent(', 'CanonicalFresh']]:[];
  for(const [needle,event]of targets){const p=text.indexOf(needle);assert(p>=0,'Missing header counter seam '+needle);const brace=text.indexOf('{',p);text=text.slice(0,brace+1)+' R119_EVENT('+event+');'+text.slice(brace+1);instrumentation.push({file:dep.file,entry:needle,event});}
  if(path.basename(dep.file)==='input_host_recovery_health.h'){
    const advance='through_ = read.completed; ackSequence_ = read.sequence;';
    assert(text.includes(advance));text=text.replace(advance,'R119_EVENT(WindowAdvance); '+advance);
    const init='if (!since_) since_ = read.completed;';
    assert(text.includes(init));text=text.replace(init,'if (!since_) { R119_EVENT(SinceInit); since_ = read.completed; }');
  }
  return text;
}
fs.mkdirSync(out,{recursive:true});const run=path.join(out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
const save=(name,value)=>fs.writeFileSync(path.join(run,name),JSON.stringify(value,null,2)+'\n');
const raw=path.join(run,'source-exact');fs.mkdirSync(raw);
for(const r of ranges)fs.writeFileSync(path.join(raw,r.name+'.txt'),r.code);
for(const d of deps)fs.writeFileSync(path.join(raw,path.basename(d.file)),d.bytes);
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars),'Missing vcvars64');
const quote=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
function command(label,exe,args,cwd,expected=0){
  const r=spawnSync(exe,args,{cwd,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});
  fs.writeFileSync(path.join(cwd,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(cwd,label+'.stderr.log'),r.stderr??'');
  fs.writeFileSync(path.join(cwd,label+'.command.json'),JSON.stringify({exe,args,cwd,exitCode:r.status,expectedExitCode:expected,error:r.error?.message},null,2)+'\n');
  assert(!r.error&&r.status===expected,label+' failed '+r.status+'; see '+cwd);return r.stdout;
}
function build(label,counted,mutation){
  const dir=path.join(run,label);fs.mkdirSync(dir);const include=path.join(dir,'include');fs.mkdirSync(include);
  for(const d of deps)fs.writeFileSync(path.join(include,path.basename(d.file)),instrumentHeader(d,counted));
  let requestCode=request;
  if(mutation==='force-persona-copy')requestCode=request.replace('inputHostRecoveryHealthContextLocked(false)','inputHostRecoveryHealthContextLocked(true)');
  const parts={SNAPSHOT:snapshot,FUNCTIONS:instrumentFunctions(counted,mutation),REQUEST:requestCode,ACCEPTED:accepted,OBSERVE:observe};
  let text=fixture.text;for(const [name,code]of Object.entries(parts)){const marker='@@'+name+'@@';assert(text.includes(marker),name+' missing');text=text.replaceAll(marker,()=>code);}
  assert(!/@@\w+@@/.test(text));fs.writeFileSync(path.join(dir,'fixture.cpp'),text);
  const cmd=path.join(dir,'compile.cmd');fs.writeFileSync(cmd,'@echo off\r\ncall '+quote(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /O2 /FAs /Fa'+quote(path.join(dir,'fixture.asm'))+' /DR119_COUNT='+Number(counted)+' '+quote(path.join(dir,'fixture.cpp'))+' /I'+quote(include)+' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
  console.log('R119 compile '+label);command('build','cmd.exe',['/d','/c',cmd],dir);return dir;
}
if(boundaryOnly){
  const results={};
  for(const candidate of [false,true]){
    const label=candidate?'ineligible-candidate':'ineligible-actual',dir=build(label,true,candidate?'eligible-shortcut':undefined);
    const result=JSON.parse(command('execute',path.join(dir,'fixture.exe'),[],dir));assert(result.passed);results[label]=result;save(label+'.results.json',result);
  }
  const actual=results['ineligible-actual'],candidate=results['ineligible-candidate'];
  assert.deepEqual(actual.rows,candidate.rows,'eligible=true normal counts changed');
  assert.deepEqual(actual.trackingCalibration,candidate.trackingCalibration,'eligible=true calibration changed');
  assert.equal(actual.ineligibleConsumers.length,16);
  const pairs=actual.ineligibleConsumers.map((a,i)=>{
    const c=candidate.ineligibleConsumers[i];
    assert.deepEqual({route:a.route,tracking:a.tracking,consumer:a.consumer,outcome:a.outcome,nextNormalRead:a.nextNormalRead},
      {route:c.route,tracking:c.tracking,consumer:c.consumer,outcome:c.outcome,nextNormalRead:c.nextNormalRead});
    const fields=['sourceProjections','routeStringComparisons','boardQueries','boardStringFinds','atomicLoads','atomicStores','canonicalFreshChecks','personaLocks','snapshotLocks','hostLocks','allocations'];
    return {route:a.route,tracking:a.tracking,consumer:a.consumer,observableStateEqual:true,
      actual:Object.fromEntries(fields.map(k=>[k,a.counts[k]])),candidate:Object.fromEntries(fields.map(k=>[k,c.counts[k]]))};
  });
  const last=load(source.file);assert(ranges.every(r=>last.text.includes(r.code))&&deps.every(d=>load(d.file).sha256===d.sha256),'Related source drift');
  const callsites=source.text.split('\n').flatMap((line,i)=>line.includes('inputHostRecoveryHealthSource(')?[{line:i+1,code:source.text.split('\n').slice(Math.max(0,i-1),i+3).join('\n')}]:[]);
  assert.equal(callsites.length,5,'New Source caller needs consumer proof');
  const b={source:{file:source.file,sha256:source.sha256},ranges:ranges.map(({code,...r})=>r),dependencies:deps.map(({file,sha256})=>({file,sha256})),
    fixture:{file:fixture.file,sha256:fixture.sha256},runner:{file:runner.file,sha256:runner.sha256},productionSourceCallsites:callsites,
    candidate:{before:'if (!source) return {};',after:'if (!source || !eligible) return {};',productionWritten:false},
    scope:'Count-only actual-consumer known-invalid boundary. No new benchmark, producer execution, late-source or recovery correctness matrix.'};
  save('source-bindings.json',b);fs.writeFileSync(path.join(run,'candidate.patch'),'--- actual/inputHostRecoveryHealthSource\n+++ fixture-only/inputHostRecoveryHealthSource\n-if (!source) return {};\n+if (!source || !eligible) return {};\n');
  const result={passed:true,run,sourceSha256:source.sha256,checks:actual.checks+candidate.checks,pairs,normalEligibleCountsIdentical:true,
    decision:'fixture-only candidate: equivalent at current known-invalid health consumers; no normal eligible=true operation reduction or demonstrated need; production no-change',
    noNewBenchmark:true,productionChanged:false,restrictions:'Returning {} is not Source metadata equality; consumer-equivalence holds because both versions force Window.valid false before any identity/window reuse. Producer calls pass true.'};
  save('summary.json',result);fs.writeFileSync(path.join(out,'ineligible-boundary-summary.json'),JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify(result,null,2));process.exit(0);
}
const profiles={};
for(const counted of [true,false]){
  const label=counted?'actual-counted':'actual-timing',dir=build(label,counted);
  const args=counted?[]:['--bench',String(opts.rounds),String(opts.iterations)];
  const result=JSON.parse(command('execute',path.join(dir,'fixture.exe'),args,dir));assert(result.passed);profiles[label]=result;save(label+'.results.json',result);
}
const negatives=[];
for(const [name,expected]of [['skip-idle-accepted','no-obligation accepted still establishes T1 timestamp'],['force-persona-copy','normal health must not copy persona strings']]){
  const dir=build('negative-'+name,true,name),r=JSON.parse(command('execute',path.join(dir,'fixture.exe'),[],dir,1));
  assert.equal(r.passed,false);assert.equal(r.error,expected);negatives.push({name,compiled:true,failedAtRuntime:true,expected,result:r});
}
const bindings={source:{file:source.file,sha256:source.sha256},ranges:ranges.map(({code,...r})=>r),
  dependencies:deps.map(({file,sha256})=>({file,sha256})),fixture:{file:fixture.file,sha256:fixture.sha256},runner:{file:runner.file,sha256:runner.sha256},
  productionSubstitutions:[],countedSubstitutions:['function/method entry counters','delegating atomic and mutex probes','comma-expression counters around original route == and Board find expressions','persona copy byte counters'],
  timingSubstitutions:['adapter std::mutex spelled through an exact std::mutex alias','caller InputHostMutex modeled by std::mutex; combined timings include these stand-in lock pairs','supplied inert globals/snapshot; actual Win32 GetTickCount64','plain Host facts initialized from this fixture PID before measurement to prevent compile-time specialization'],
  counterEntries:[...new Map(instrumentation.map(i=>[i.file+':'+i.entry,i])).values()],
  scope:'Actual request/accepted/observe fragments and adapter/header bodies; no full Submit, reader, frame transport, receipt parser, Host, devices, Steam, services, sleep or product execution. No R116 candidate rerun.'};
save('source-bindings.json',bindings);
const latest=load(source.file);
const sourceStability={wholeFileChanged:latest.sha256!==source.sha256,relatedRangesStable:ranges.every(r=>latest.text.includes(r.code)),
  dependenciesStable:deps.every(d=>load(d.file).sha256===d.sha256),startSha256:source.sha256,endSha256:latest.sha256};
save('source-stability.json',sourceStability);
assert(sourceStability.relatedRangesStable&&sourceStability.dependenciesStable,'Related source/header changed during run; rerun against new parent revision');
const median=a=>{const s=[...a].sort((a,b)=>a-b);return s[Math.floor(s.length/2)];};
const timing=profiles['actual-timing'].benchmark.rows.map(r=>({route:r.route,stage:r.stage,medianThreadCycles:median(r.threadCyclesPerCall),minThreadCycles:Math.min(...r.threadCyclesPerCall),maxThreadCycles:Math.max(...r.threadCyclesPerCall),medianWallNs:median(r.wallNsPerCall)}));
const summary={passed:true,run,sourceSha256:source.sha256,sourceStability,checks:profiles['actual-counted'].checks+profiles['actual-timing'].checks,
  actualCounts:profiles['actual-counted'],timing,negativeControls:negatives,settings:opts,
  productionChanged:false,existingTestsChanged:false,productOrHostOrDeviceOrSteamOrServiceExecuted:false,
  decision:'no-change; no semantically equivalent profitable cache/early return established; no production candidate',
  caveat:'Deterministic operation counts and one-process isolated microbench only. Freshness-valid canonical sample with separate reused/new-sequence timing; no full CPU, scheduling, producer, transport, Host or device measurement. Combined includes two uncontended std::mutex pairs modeling existing caller Host locks (not the production InputHostMutex implementation); individual stages assume caller holds Host mutex.'};
save('summary.json',summary);fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify({passed:true,run,sourceSha256:source.sha256,checks:summary.checks,timing,decision:summary.decision},null,2));
