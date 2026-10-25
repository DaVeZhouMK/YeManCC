import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {MODULE,REPO,ENTRY,common,reviewedSources,reviewedTools,baseTools} from './adapter.mjs';import {runProcess,sourcePath} from '../runner.mjs';
const sha=data=>crypto.createHash('sha256').update(data).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n'),read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const need=(ok,code)=>{if(!ok)throw Error(code);};const sorted=values=>[...new Set(values)].sort();
const walk=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(entry=>{const file=path.join(root,entry.name);need(!entry.isSymbolicLink(),'OUTPUT_LINK_REFUSED');return entry.isDirectory()?walk(file):[file];});
export function verifyUi(stdout,stderr,exitCode,{negative=false}={}){
 need(Number.isInteger(exitCode),'UI_EXIT_MISSING');
 if(negative){need(exitCode!==0&&/AssertionError|ERR_ASSERTION/.test(stderr)&&/current-ui-selftest\.mjs:/.test(stderr),'UI_ORIGINAL_ASSERTION_DID_NOT_REJECT');return{status:'EXPECTED_REJECTION',originalAssertionFailed:true};}
 const marker=stdout.indexOf('{\n  "suite": "External controller priority UI"');need(marker>=0,'UI_ORIGINAL_REPORT_MISSING');const row=JSON.parse(stdout.slice(marker).trim());
 need(exitCode===0&&Number.isSafeInteger(row.passed)&&row.passed>0&&Array.isArray(row.cases)&&row.passed===row.cases.length&&row.cases.every(name=>typeof name==='string'&&name.length>0),'UI_ORIGINAL_REPORT_INVALID');
 need(row.fixtureSchemaVersion===2&&row.legacyLayoutCompatibility==='INCOMPATIBLE_UI_STRUCTURE_RETAINED_FAILURE','UI_CURRENT_FIXTURE_SCHEMA_REQUIRED');need(typeof row.focusHookIntegrated==='boolean','UI_FOCUS_OBSERVATION_MISSING');return{...row,status:'PASS'};
}
export async function runPriorityUi(evidenceDir,{repoRoot=REPO,discover=false}={}){
 const root=path.resolve(evidenceDir);need(!fs.existsSync(root),'EVIDENCE_DIRECTORY_ALREADY_EXISTS');fs.mkdirSync(root,{recursive:true});
 const sources=sorted([...common(),...reviewedSources(repoRoot),ENTRY,'package.json','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs']);
 const snapshots=sources.map(relative=>{const file=sourcePath(repoRoot,relative),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.copyFileSync(file,copy);return{path:relative,sha256:hash(file),copy};});
 const toolFiles=sorted([...baseTools(repoRoot),...reviewedTools(repoRoot)]).map(file=>({path:file,sha256:hash(file)}));
 const pwsh=process.env.YMCC_SIM_PWSH||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';const steps=[];
 async function execute(label,sourceRoot){const dir=path.join(root,label);fs.mkdirSync(dir);const trace=path.join(dir,'trace'),config=path.join(dir,'config.json'),argumentsPath=path.join(dir,'arguments.json');
  write(config,{sourceRoot,trace,entry:ENTRY,node:process.execPath,observer:path.join(repoRoot,MODULE,'observe.cjs')});write(argumentsPath,[config]);const env={...process.env,ESBUILD_BINARY_PATH:baseTools(repoRoot)[2],NO_COLOR:'1'};delete env.NODE_OPTIONS;delete env.NODE_PATH;
  const p=await runProcess(pwsh,['-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(repoRoot,MODULE,'Run-Original.ps1'),'-ArgumentsPath',argumentsPath],{cwd:sourceRoot,env,timeoutMs:60000});
  fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr??'');steps.push({label,pid:p.pid,exitCode:p.exitCode,signal:p.signal,timedOut:p.timedOut,launchError:p.launchError,outputTruncated:p.outputTruncated,ownedJob:true});
  need(!p.signal&&!p.timedOut&&!p.launchError&&!p.outputTruncated,'UI_PROCESS_INCOMPLETE');const original=verifyUi(p.stdout??'',p.stderr??'',p.exitCode,{negative:label.startsWith('fault')});
  const observation=read(path.join(trace,'observed-inputs.json')),build=read(path.join(trace,'build.json'));need(build.outputFiles===1&&fs.existsSync(path.join(trace,'output-0.js')),'ACTUAL_COMPILER_OUTPUT_MISSING');
  const relativeSources=sorted(observation.sources.map(row=>path.relative(sourceRoot,row.path).replaceAll('\\','/')));
  need(observation.sources.every(row=>hash(row.path)===row.sha256)&&observation.tools.every(row=>hash(row.path)===row.sha256),'UI_OBSERVED_INPUT_CHANGED');
  if(!discover){need(relativeSources.every(file=>reviewedSources(repoRoot).includes(file)),'UI_EXECUTED_SOURCE_SET_NOT_REVIEWED');need(observation.tools.every(row=>reviewedTools(repoRoot).includes(row.path)),'UI_EXECUTED_TOOL_SET_NOT_REVIEWED');}
  return{original,observed:observation,relativeSources,build:{esbuildVersion:build.esbuildVersion,outputSha256:hash(path.join(trace,'output-0.js')),output:path.join(trace,'output-0.js')}};
 }
 try{
  const positive=await execute('positive',repoRoot);
  if(discover){write(path.join(root,'discovery.json'),{status:'DISCOVERY_ONLY',executedOriginalCases:positive.original.passed,sourceFiles:positive.relativeSources,toolFiles:positive.observed.tools.map(row=>row.path),positive,productAcceptance:false});return{status:'DISCOVERY_ONLY',sources:positive.relativeSources.length,tools:positive.observed.tools.length};}
  const faults=[];
  for (const definition of [
    {label:'fault-receipt',file:'src/bridge/virtualOutputStatus.ts',before:"if (runtime.builtinHidden !== true || runtime.virtualSuspended !== true || runtime.state !== 'active') {",after:"if (runtime.builtinHidden !== true || runtime.state !== 'active') {",count:1},
    {label:'fault-stale-read',file:'src/bridge/settingsRepository.ts',before:'if (cache && cache !== cacheAtReadStart) return cache;',after:'',count:2},
  ]) {
    const mirror=path.join(root,definition.label+'-source');
    for(const relative of sorted([...reviewedSources(repoRoot),ENTRY,'package.json'])){const to=path.join(mirror,relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(root,'input-sources',relative),to);}
    const dependency=path.join(mirror,'node_modules');fs.symlinkSync(path.join(repoRoot,'node_modules'),dependency,process.platform==='win32'?'junction':'dir');
    const file=path.join(mirror,definition.file),before=fs.readFileSync(file,'utf8');need(before.split(definition.before).length===definition.count+1,'UI_ACTUAL_FAULT_ANCHOR_CHANGED');
    const changed=before.replaceAll(definition.before,definition.after);fs.writeFileSync(file,changed);
    const executed=await execute(definition.label,mirror);need(hash(path.join(mirror,ENTRY))===hash(path.join(root,'input-sources',ENTRY)),'UI_CURRENT_ASSERTIONS_CHANGED');
    faults.push({...executed.original,compiled:executed.build,fault:definition.label});
    write(path.join(root,definition.label+'-proof.json'),{file:definition.file,beforeSha256:sha(before),afterSha256:sha(changed),before:definition.before,after:definition.after,assertionsUnchanged:true,sourceDerivedFault:true,historicalBefore:false});
    need(fs.lstatSync(dependency).isSymbolicLink(),'DEPENDENCY_LINK_WAS_REPLACED');fs.unlinkSync(dependency);
  }
  need(snapshots.every(row=>hash(sourcePath(repoRoot,row.path))===row.sha256&&hash(row.copy)===row.sha256),'UI_SOURCE_CHANGED_DURING_RUN');need(toolFiles.every(row=>hash(row.path)===row.sha256),'UI_TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const files=walk(root);const result={schemaVersion:1,id:'controller.external-priority-ui',status:'PASS',passed:true,positive:{...positive.original,compiled:positive.build},faults,sourceHashes:[...snapshots.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],executedProductModules:positive.relativeSources.filter(file=>/\.(?:ts|vue)$/.test(file)&&file!=='src/gamepad/engine.ts'),inputConsumerStateFeedback:{input:'Current actual UI fixture actions; obsolete old layout failure retained separately',consumer:'Actual ButtonMappingView, settingsRepository CAS and virtualOutputStatus',state:'Original state and concurrent-save cases',feedback:'Actual Vue text/attributes and persisted in-memory settings'},toolchain:{files:toolFiles},boundary:{browser:'Owned headless browser with original loopback-only page',settingsAndIpc:'Original in-memory fixtures',productAppStarted:false,physicalHardwareVerified:false},remainingGaps:['Native/Host/physical passthrough and installed UI remain separate.','Original engine initial-focus hook is a source-only observation, not execution of the full gamepad engine.']};write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',code:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2);const discover=args.includes('--discover');const index=args.indexOf('--evidence-dir');need(index>=0&&args[index+1]&&args.length===(discover?3:2),'UI_ARGUMENTS_INVALID');const result=await runPriorityUi(args[index+1],{discover});console.log(JSON.stringify({status:result.status,positive:result.positive?.passed,sources:result.sources,tools:result.tools}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
