import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

export function runGpdRootReturn({repo,args,output,extract,calibrationToolchainPreamble,runnerPath}){
 const started=Date.now(),directory=path.dirname(fileURLToPath(import.meta.url));
 const allowed=new Set(['--case','--output','--variant','--runtime-source','--diagnostic-source','--baseline-runtime','--baseline-diagnostic']);
 const values=new Map();let prepareOnly=false;
 for(let i=0;i<args.length;i++){
  const key=args[i];
  if(key==='--prepare-only'){assert(!prepareOnly,'Duplicate --prepare-only');prepareOnly=true;continue;}
  assert(allowed.has(key),'Unsupported gpd-root-return option: '+key);
  assert(!values.has(key),'Duplicate option: '+key);
  const value=args[++i];assert(value&&!value.startsWith('--'),'Missing value: '+key);values.set(key,value);
 }
 assert.equal(values.get('--case'),'gpd-root-return');assert(values.has('--output'),'Explicit output required');
 const selection=values.get('--variant')||'candidate';assert(['baseline','candidate','both'].includes(selection),'Unknown variant');
 if(selection!=='candidate')assert(values.has('--baseline-runtime')&&values.has('--baseline-diagnostic'),'baseline/both require explicit --baseline-runtime and --baseline-diagnostic');
 const deadline=Number(process.env.YMCC_GPD_ROOT_RETURN_DEADLINE_MS||started+180000);
 assert(Number.isSafeInteger(deadline)&&deadline>started,'Preparation/execution deadline exhausted');
 const remaining=cap=>{const value=Math.min(cap,deadline-Date.now());assert(value>0,'Original gpd-root-return deadline exhausted');return value;};
 const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
 const option=(key,fallback)=>path.resolve(values.get(key)||fallback);
 const selected={
  baseline:selection==='candidate'?null:{runtime:option('--baseline-runtime'),diagnostic:option('--baseline-diagnostic')},
  candidate:{runtime:option('--runtime-source',path.join(repo,'native/msi_native_foreground_isolation_runtime.h')),
   diagnostic:option('--diagnostic-source',path.join(repo,'native/one_shot_helper_diagnostic.h'))},
 };
 const templatePath=path.join(directory,'gpd-root-return.cpp.in');
 const common={main:path.join(repo,'native/main.cpp'),json:path.join(repo,'deps/json/json.hpp'),
  guard:path.join(repo,'native/hidhide_cli_job_guard.h'),policy:path.join(repo,'native/native_foreground_source_policy.h'),
  nativePolicy:path.join(repo,'native/msi_native_foreground_isolation.h'),locationPolicy:path.join(repo,'native/msi_foreground_isolation.h'),
  extractor:path.join(repo,'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs'),
  toolchain:path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),
  originalRunner:path.join(repo,'tools/native_foreground_latency_selftest.mjs'),runner:runnerPath,leaf:fileURLToPath(import.meta.url),template:templatePath};
 const variants=(selection==='both'?['baseline','candidate']:[selection]);
 const paths=[...Object.values(common),...variants.flatMap(name=>Object.values(selected[name]))];
 const snapshots=new Map([...new Set(paths)].map(file=>[file,fs.readFileSync(file)]));
 const identities=[...snapshots].map(([file,bytes])=>({file,bytes:bytes.length,sha256:sha(bytes)}));
 assert(!fs.existsSync(output)||fs.readdirSync(output).length===0,'Output must be new or empty; raw files are never overwritten');
 fs.mkdirSync(output,{recursive:true});
 const write=(file,data)=>fs.writeFileSync(file,data,{flag:'wx'});
 const save=(name,value)=>write(path.join(output,name),JSON.stringify(value,null,2)+'\n');
 const expected=[...snapshots.get(templatePath).toString('utf8').matchAll(/check\("([^"]+)"/g)].map(match=>match[1]);
 assert.equal(new Set(expected).size,expected.length,'Duplicate original check name');assert(expected.length>=20);
 const baselineFailures=['root-cycle-diagnostic-accepted','root-cycle-returned-with-current-root',
  'prepare-failure-returned-with-current-root','revalidate-failure-returned-with-current-root','malformed-root-marker-decoder-rejected'];
 const result={schema:1,id:'controller.native-foreground-latency',mode:'gpd-root-return',status:'PREPARING',
  startedAt:new Date(started).toISOString(),deadlineMs:deadline,prepareOnly,selection,sourceInputs:identities,expectedCaseNames:expected,
  baselineExpectedFailures:baselineFailures,defaultLatencyExecuted:false,powerOrderExecuted:false,helperExitQueryExecuted:false,
  actualCompiles:0,actualFixtureExecutions:0,configurations:[],hardwareAccess:false};
 save('INPUT.json',result);
 const spans=[],sourceBindings=[];
 let error=null;
 try{
  const frozen=path.join(output,'source-inputs');fs.mkdirSync(frozen);
  identities.forEach((identity,index)=>write(path.join(frozen,String(index).padStart(2,'0')+'-'+path.basename(identity.file)),snapshots.get(identity.file)));
  for(const name of variants){
   remaining(60000);
   const d=path.join(output,name);fs.mkdirSync(d);const include=path.join(d,'include');fs.mkdirSync(include);
   const sources=selected[name];const runtime=snapshots.get(sources.runtime).toString('utf8');
   const take=(text,symbol,file)=>extract(text,'function',symbol,file,spans);
   const main=snapshots.get(common.main).toString('utf8');
   const bindings={
    '@@ASCII_LOWER@@':take(main,'ascii_lower',common.main),
    '@@UTF8_TO_WIDE@@':take(main,'U2W',common.main),
    '@@ROOT_CANDIDATE@@':take(runtime,'nativeForegroundRootCandidate',sources.runtime),
    '@@ROOT_STARTED@@':take(runtime,'msiPostHideRootStartedFor',sources.runtime),
    '@@TERMINAL_PREDICATE@@':take(runtime,'msiNativeForegroundGpdTerminalGameWriterReturnedLocked',sources.runtime),
   };
   const template=snapshots.get(templatePath).toString('utf8');let code=template;const edits=[];
   for(const [marker,body] of Object.entries(bindings)){
    assert.equal(code.split(marker).length,2,'Template marker '+marker);
    const offset=code.indexOf(marker);edits.push({marker,body,offset});code=code.slice(0,offset)+body+code.slice(offset+marker.length);
   }
   assert(!code.includes('@@'),'Unbound marker');
   let reversed=code;
   for(const edit of [...edits].reverse()){
    assert.equal(reversed.slice(edit.offset,edit.offset+edit.body.length),edit.body,'Reverse span '+edit.marker);
    reversed=reversed.slice(0,edit.offset)+edit.marker+reversed.slice(edit.offset+edit.body.length);
   }
   assert.equal(reversed,template,'Entire generated CPP reverse image differs');
   const headerInputs={'one_shot_helper_diagnostic.h':sources.diagnostic,'hidhide_cli_job_guard.h':common.guard,
    'native_foreground_source_policy.h':common.policy,'msi_native_foreground_isolation.h':common.nativePolicy,
    'msi_foreground_isolation.h':common.locationPolicy,'json.hpp':common.json};
   for(const [header,file] of Object.entries(headerInputs))write(path.join(include,header),snapshots.get(file));
   write(path.join(d,'Actual.cpp'),code);
   const binding={variant:name,runtime:sources.runtime,diagnostic:sources.diagnostic,generatedSha256:sha(code),
    templateSha256:sha(reversed),reverseExact:true,edits:edits.map(edit=>({...edit,bodySha256:sha(edit.body)})),
    headers:Object.entries(headerInputs).map(([header,file])=>({header,file,sha256:sha(snapshots.get(file))}))};
   sourceBindings.push(binding);write(path.join(d,'SOURCE-BINDING.json'),JSON.stringify(binding,null,2)+'\n');
   const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
   const command=calibrationToolchainPreamble(vcvars)+
    'for %%i in (cl.exe) do @echo YMCC_CL=%%~$PATH:i\r\nfor %%i in (link.exe) do @echo YMCC_LINK=%%~$PATH:i\r\n'+
    'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /O2 /MD /showIncludes /sourceDependencies dependencies.json /I"include" Actual.cpp /FoFixture.obj /FeFixture.exe /link /VERBOSE:LIB /INCREMENTAL:NO\r\nexit /b %errorlevel%\r\n';
   write(path.join(d,'build.cmd'),command);
   const entry={variant:name,status:'SOURCE_PREPARED',generatedSha256:sha(code),reverseExact:true};result.configurations.push(entry);
   if(prepareOnly)continue;
   const invoke=(program,argv,cap,phase)=>{
    const begin=Date.now(),timeout=remaining(cap);
    const raw=spawnSync(program,argv,{cwd:d,encoding:'utf8',windowsHide:true,timeout,maxBuffer:32*1024*1024});
    write(path.join(d,phase+'.log'),(raw.stdout||'')+(raw.stderr||''));
    write(path.join(d,phase+'.json'),JSON.stringify({program,argv,startedAt:new Date(begin).toISOString(),elapsedMs:Date.now()-begin,
     timeoutMs:timeout,exitCode:raw.status,signal:raw.signal,error:raw.error?{code:raw.error.code,message:raw.error.message}:null},null,2)+'\n');
    return raw;
   };
   ++result.actualCompiles;const built=invoke(process.env.ComSpec||'C:/Windows/System32/cmd.exe',['/d','/c',path.join(d,'build.cmd')],60000,'build');
   assert.equal(built.status,0,'Compile failure is not a valid baseline counterexample: '+d);
   const tools=[...built.stdout.matchAll(/^YMCC_(?:CL|LINK)=(.+)\r?$/gm)].map(match=>match[1].trim());assert.equal(tools.length,2);
   write(path.join(d,'TOOLS.json'),JSON.stringify({tools:[process.execPath,vcvars,...tools].map(file=>({file,sha256:sha(fs.readFileSync(file))})),
    fullToolClosureVerified:false,cacheEligible:false},null,2)+'\n');
   ++result.actualFixtureExecutions;const ran=invoke(path.join(d,'Fixture.exe'),[],10000,'run');
   write(path.join(d,'RESULT.raw.json'),ran.stdout||'');const report=JSON.parse(ran.stdout);
   assert.equal(report.mode,'gpd-root-return');assert.equal(report.infrastructureFailure,'','Spool/infrastructure failure is not semantic FAIL');
   assert.deepEqual(report.caseNames,expected,'Actual original check names/order differ');assert.equal(report.checks,expected.length);
   if(name==='baseline'){
    assert.equal(ran.status,1,'Old consumer must reach necessary semantic FAIL');assert.equal(report.passed,false);
    assert.deepEqual(report.failures,baselineFailures,'Unexpected baseline failure set');
   }else{assert.equal(ran.status,0);assert.equal(report.passed,true);assert.deepEqual(report.failures,[]);}
   Object.assign(entry,{status:name==='baseline'?'EXPECTED_SEMANTIC_FAIL':'PASS',exitCode:ran.status,report,
    binarySha256:sha(fs.readFileSync(path.join(d,'Fixture.exe')))});
  }
 }catch(caught){error=caught;save('FAIL.json',{...result,elapsedMs:Date.now()-started,error:caught.stack});}
 finally{
  const closing=identities.map(identity=>{try{return {file:identity.file,sha256:sha(fs.readFileSync(identity.file))};}catch(caught){return {file:identity.file,error:String(caught)};}});
  const drift=closing.filter((row,index)=>row.sha256!==identities[index].sha256);
  save('CLOSING.json',{elapsedMs:Date.now()-started,deadlineMs:deadline,closing,parallelDrift:drift,sourceBindings,
   actualCompiles:result.actualCompiles,actualFixtureExecutions:result.actualFixtureExecutions});
  if(drift.length && !error)error=new Error('Concurrent source drift: frozen raw retained; current-source acceptance incomplete');
 }
 if(error)throw error;
 result.status=prepareOnly?'PREPARED_NOT_COMPILED':selection==='baseline'?'BASELINE_COUNTEREXAMPLE_ONLY':selection==='candidate'?'CANDIDATE_ONLY_PASS':'PASS_GPD_ROOT_RETURN';
 result.elapsedMs=Date.now()-started;result.fullDefaultAcceptance=false;save('result.json',result);
 console.log(JSON.stringify({status:result.status,selection,checksPerVariant:expected.length,actualCompiles:result.actualCompiles,actualFixtureExecutions:result.actualFixtureExecutions}));
}
