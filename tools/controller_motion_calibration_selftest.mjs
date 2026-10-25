// Offline borrowed-handle calibration transaction proof. Never loads a DLL.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..');
const workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionCalibration/Transaction');
const args={};
for(let i=2;i<process.argv.length;++i){const k=process.argv[i];assert(['--out','--vcvars'].includes(k)&&process.argv[i+1],k);args[k.slice(2)]=process.argv[++i];}
const out=path.resolve(args.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase(),'Evidence must be a fresh direct child');
fs.mkdirSync(root,{recursive:true});assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase());fs.mkdirSync(out);
const rel=p=>path.relative(out,p).replaceAll('\\','/');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const blob=b=>crypto.createHash('sha1').update(Buffer.from(`blob ${b.length}\0`)).update(b).digest('hex');
const load=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const json=(p,v)=>fs.writeFileSync(path.join(out,p),JSON.stringify(v,null,2)+'\n');
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),scope:'Pure C++17 CalibrationTransaction with memory effects and actual borrowed Session composition',actualHardware:false,actualPbt:false,actualDllLoaded:false,productionMainExecuted:false,productionPersistenceExecuted:false,inputs:[],configurations:[],mutations:[]};
const save=()=>json('summary.json',receipt);
const inventory=()=>json('FILES.json',{note:'Self inventory excluded; failed runs are retained',files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))}))});
const run=(exe,argv,log,cwd=out,env=process.env)=>{
  const r=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:12*1024*1024});
  fs.mkdirSync(path.dirname(path.join(out,log)),{recursive:true});fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||''));
  if(r.error)throw r.error;assert.equal(r.signal,null,log);return r;
};
const ok=(r,label)=>{assert.equal(r.status,0,label+'; evidence '+out);return r.stdout;};
const once=(text,from,to)=>{assert.equal(text.split(from).length-1,1,'Missing/ambiguous mutation '+from);return text.replace(from,to);};
save();
try {
  const fixture=path.join(repo,'tools/fixtures/controller_motion_calibration');
  const sourceFiles=[runner,...walk(fixture),...['controller_motion_calibration.h','controller_motion_processor.h','controller_motion_session.h'].map(n=>path.join(repo,'native',n))];
  for(const p of sourceFiles){const b=fs.readFileSync(p),s=path.join(out,'source',path.relative(repo,p));fs.mkdirSync(path.dirname(s),{recursive:true});fs.writeFileSync(s,b);receipt.inputs.push({path:path.relative(repo,p).replaceAll('\\','/'),snapshot:rel(s),bytes:b.length,sha256:sha(b)});}
  const frozen=path.join(out,'source/tools/fixtures/controller_motion_calibration');
  const reference=load(path.join(frozen,'reference/PRIMARY-SOURCE.json'));
  assert.equal(reference.commit,'06c0b9544db2b1f39abf9cd3796225ccfb096103');
  const treeFile=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/ProtocolRecovery/historical-tree.json');
  const treeBytes=fs.readFileSync(treeFile);assert.equal(sha(treeBytes),reference.fixedTreeSha256);fs.writeFileSync(path.join(out,'historical-tree.json'),treeBytes);
  const tree=load(treeFile);assert.equal(tree.truncated,false);
  for(const entry of reference.files){const b=fs.readFileSync(path.join(frozen,'reference',entry.file));const official=tree.tree.find(v=>v.path===entry.path);assert(official);assert.equal(b.length,entry.bytes);assert.equal(b.length,official.size);assert.equal(sha(b),entry.sha256);assert.equal(blob(b),entry.gitBlob);assert.equal(blob(b),official.sha);}
  const sensors=fs.readFileSync(path.join(frozen,'reference/SensorsManager.cs'),'utf8');
  const gamepad=fs.readFileSync(path.join(frozen,'reference/GamepadMotion.cs'),'utf8');
  const flow=['gamepadMotion.ResetContinuousCalibration();','gamepadMotion.SetCalibrationMode(CalibrationMode.Stillness | CalibrationMode.SensorFusion);','confidence = gamepadMotion.GetAutoCalibrationConfidence();','gamepadMotion.GetCalibrationOffset(out float xOffset, out float yOffset, out float zOffset);','gamepadMotion.SetCalibrationOffset(xOffset, yOffset, zOffset, (int)(confidence * 10.0f));','gamepadMotion.SetCalibrationMode(CalibrationMode.Manual);'];
  let previous=-1;const anchors=flow.map(statement=>{const index=sensors.indexOf(statement);assert(index>previous);assert.equal(sensors.lastIndexOf(statement),index);previous=index;return {statement,line:sensors.slice(0,index).split('\n').length,sha256:sha(statement)};});
  for(const [name,value]of[['Manual',0],['Stillness',1],['SensorFusion',2]])assert(new RegExp(`\\b${name}\\s*=\\s*${value}\\b`).test(gamepad));
  assert(sensors.includes('if (confidence == 1.0f)'));assert(sensors.includes('TimeSpan.FromSeconds(5)'));
  receipt.provenance={...reference,anchors,staticSourceOnly:true,hcGate:'confidence == 1.0f',ymccGate:'confidence > 0.99f (explicit user policy)',ymccSafetyPolicy:'Exact live key/borrowed handle/all callback identity; fresh once-only receipt; timeout/clock/nonfinite/process/persistence failure rollback; no destructor effects'};
  json('SOURCE-PROVENANCE.json',receipt.provenance);save();
  let vcvars=args.vcvars;
  if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');const install=ok(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log'),'MSVC discovery').trim();assert(install&&!/[\r\n]/.test(install));vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
  assert(fs.existsSync(vcvars)&&!/[\r\n"%&|<>^!]/.test(vcvars));const setup=path.join(out,'compiler-environment.cmd');assert(!/[\r\n"%&|<>^!]/.test(setup));
  fs.writeFileSync(setup,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const result=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',setup],{encoding:'utf8',windowsHide:true,timeout:30000});if(result.error)throw result.error;assert.equal(result.status,0);
  const env={...process.env};for(const line of result.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at<=0)continue;const k=line.slice(0,at),old=Object.keys(env).find(v=>v.toLowerCase()===k.toLowerCase());env[old||k]=line.slice(at+1);}for(const k of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(k.toLowerCase()))delete env[k];
  const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');receipt.compiler={path:compiler,sha256:sha(fs.readFileSync(compiler)),vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion};
  const native=path.join(out,'source/native'),harness=path.join(frozen,'harness.cpp');
  const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
  const build=(where,flags,headerDir=native)=>{
    fs.mkdirSync(where,{recursive:true});const exe=path.join(where,'calibration.exe'),dependencies=path.join(where,'dependencies.json');
    const argv=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise',...flags,'/sourceDependencies',dependencies,`/I${headerDir}`,`/I${native}`,harness,`/Fo${path.join(where,'harness.obj')}`,`/Fd${path.join(where,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
    ok(run(compiler,argv,`${rel(where)}/compile.log`,where,env),'C++ compile '+rel(where));
    const dep=load(dependencies).Data;assert.equal(path.resolve(dep.Source).toLowerCase(),harness.toLowerCase());
    const actual=[dep.Source,...dep.Includes];for(const expected of [path.join(headerDir,'controller_motion_calibration.h'),path.join(native,'controller_motion_processor.h'),path.join(native,'controller_motion_session.h')])assert(actual.some(p=>path.resolve(p).toLowerCase()===expected.toLowerCase()),'Missing actual compiler include '+expected);
    const compileInputs=actual.map(p=>({path:p.toLowerCase().startsWith(out.toLowerCase()+path.sep)?rel(p):p,sha256:sha(fs.readFileSync(p))}));
    json(`${rel(where)}/actual-compile-inputs.json`,compileInputs);
    const r=run(exe,[],`${rel(where)}/run.log`,where,env);
    const match=/^SUMMARY scenarios=(\d+) checks=(\d+) failures=(\d+)\r?$/m.exec(r.stdout);assert(match,'No semantic summary: crash is not a mutant kill');
    const cases=[...r.stdout.matchAll(/^CASE ([a-z-]+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({name:m[1],checks:+m[2],failures:+m[3]}));
    assert.equal(cases.length,+match[1]);assert.equal(cases.reduce((n,c)=>n+c.checks,0),+match[2]);assert.equal(cases.reduce((n,c)=>n+c.failures,0),+match[3]);
    return {exitCode:r.status,scenarios:+match[1],checks:+match[2],failures:+match[3],cases,buildArguments:argv,actualCompileInputs:rel(path.join(where,'actual-compile-inputs.json')),executableSha256:sha(fs.readFileSync(exe))};
  };
  for(const [configuration,flags]of configurations){const r=build(path.join(out,configuration),flags);receipt.configurations.push({configuration,...r});save();assert.equal(r.exitCode,0,configuration+' failed');assert.equal(r.failures,0);console.log(`${configuration}: PASS (${r.scenarios} scenarios, ${r.checks} checks)`);}
  const original=fs.readFileSync(path.join(native,'controller_motion_calibration.h'),'utf8');
  const mutations=[
    {name:'inclusive-confidence-threshold',from:'if (confidence_ > 0.99f)',to:'if (confidence_ >= 0.99f)',witness:'strict-confidence-boundary'},
    {name:'receipt-replay',from:'sampleReceipt <= lastReceipt_',to:'sampleReceipt < lastReceipt_',witness:'one-receipt-one-process'},
    {name:'source-key-bypass',from:'!(key == key_)',to:'(!(key == key_) && !key.valid())',witness:'exact-live-source-retirement'},
    {name:'handle-replacement-bypass',from:'handle != handle_',to:'(handle != handle_ && !handle)',witness:'exact-live-source-retirement'},
    {name:'callback-replacement-bypass',from:'!(ops == ops_)',to:'(!(ops == ops_) && !ops.complete())',witness:'exact-live-source-retirement'},
    {name:'persist-failure-keeps-candidate',from:'apply(baseline_);',to:'apply(candidate_.value_or(baseline_));',witness:'process-persistence-rollback'},
    {name:'nonfinite-offset-accepted',from:'if (!next.valid())',to:'if (!next.valid() && next.weight < 0)',witness:'offset-finiteness-and-zero'},
    {name:'deadline-exclusive',from:'now - startedAt_ >= windowMs_',to:'now - startedAt_ > windowMs_',witness:'timeout-and-clock'},
    {name:'destructor-callback',from:'~CalibrationTransaction() = default;',to:'~CalibrationTransaction() { if (active_) rollback(CalibrationOutcome::Aborted); }',witness:'borrowed-destructor-no-callbacks'}
  ];
  for(const fault of mutations){const dir=path.join(out,'mutations',fault.name);fs.mkdirSync(dir,{recursive:true});const text=once(original,fault.from,fault.to);fs.writeFileSync(path.join(dir,'controller_motion_calibration.h'),text);const row={...fault,headerSha256:sha(text),configurations:[]};receipt.mutations.push(row);
    for(const [configuration,flags]of configurations){const r=build(path.join(dir,configuration),flags,dir);row.configurations.push({configuration,...r});save();assert.equal(r.exitCode,1,fault.name+': only semantic failure is a kill');assert(r.failures>0);assert(r.cases.some(c=>c.name===fault.witness&&c.failures>0),'Missing named semantic witness '+fault.name);}
    console.log('Compiled semantic mutant killed in Release/Checked: '+fault.name);
  }
  const stable=receipt.inputs.every(i=>sha(fs.readFileSync(path.join(repo,i.path)))===i.sha256);assert(stable,'Frozen owned/header inputs changed during proof');
  receipt.stability={allInputsStable:stable,calibrationHeaderSha256:sha(fs.readFileSync(path.join(repo,'native/controller_motion_calibration.h'))),sessionHeaderSha256:sha(fs.readFileSync(path.join(repo,'native/controller_motion_session.h'))),processorHeaderSha256:sha(fs.readFileSync(path.join(repo,'native/controller_motion_processor.h'))),fixedTreeStable:sha(fs.readFileSync(treeFile))===reference.fixedTreeSha256};assert(receipt.stability.fixedTreeStable);
  receipt.status='passed';receipt.completedAt=new Date().toISOString();save();json('CURRENT-SOURCE-STABILITY.json',receipt.stability);
  fs.writeFileSync(path.join(out,'REPORT.md'),`# Controller borrowed calibration transaction\n\nStatus: PASSED.\n\n`+
    receipt.configurations.map(c=>`- ${c.configuration}: ${c.scenarios} scenarios, ${c.checks} checks, ${c.failures} failures.`).join('\n')+
    `\n- ${mutations.length} finite semantic mutants compiled and killed in both configurations; compilation failures and crashes never count.\n\n`+
    `Actual controller_motion_calibration.h plus unchanged Session/Processor headers compile and execute. A memory callback sink records exact reset/mode/process/poll/offset/apply ordering; a real Session borrows its handle to the transaction and remains the sole resource owner. Fixtures assert explicit expected events/values independently of the implementation. Invalid pointers cannot be dereferenced by the effect sink.\n\n`+
    `Coverage: >99% strict float boundary, 0..100% polls, nextafter, signed/zero/nonfinite confidence and offset; fresh/zero/future/old/repeated receipts; one receipt one process/observe; exact window/age boundaries, clock regression and uint64 upper edge; every Key field, changed/null handle and each changed/missing callback at prepare/observe/finish/cancel; process/persistence failure rollback, no-candidate commit, abort/reuse; sleep/new epoch/sensor source recovery; no destructor callbacks; actual Session stationary processing with finite orientation and no transaction ownership.\n\n`+
    `Fixed HC source: ${reference.commit}. SensorsManager.cs and GamepadMotion.cs SHA256/size/Git blob are checked against the frozen official historical tree. Reset -> Stillness|SensorFusion (3) -> confidence poll -> read/set offset -> Manual (0) is source evidence only. HC's confidence == 1.0f and timeout behavior are not reproduced as acceptance policy. YMCC user policy is strict confidence > 0.99f plus transactional source/receipt/time/finiteness/persistence guards. Weight follows int(confidence*10).\n\n`+
    `No main code, Windows calibration, request registry, actual JSON persistence, real DLL, HID, hardware, sleep/PBT or deployment is executed. The fake confidence/offset values do not establish convergence of a physical sensor. Callback library/borrowed-handle lifetime, serialization and nonthrowing callbacks are caller obligations. No main/Session/Owner/older test changes. All actual compiler dependency inputs, commands, sources, hashes, logs and mutants are retained.\n\n`+
    `Header/source stability: all inputs stable; Calibration header SHA256 ${receipt.stability.calibrationHeaderSha256}. See SOURCE-PROVENANCE.json, CURRENT-SOURCE-STABILITY.json and summary.json.\n`);
  inventory();console.log(JSON.stringify({status:receipt.status,evidence:out,configurations:receipt.configurations.map(({configuration,scenarios,checks,failures})=>({configuration,scenarios,checks,failures})),mutants:receipt.mutations.length,stability:receipt.stability},null,2));
} catch(e) {receipt.status='failed';receipt.error=e.stack||String(e);receipt.finishedAt=new Date().toISOString();save();inventory();console.error(receipt.error);console.error('Evidence: '+out);process.exitCode=1;}
