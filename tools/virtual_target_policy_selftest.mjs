import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';
const require=createRequire(import.meta.url),runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SteamDeckSelfOutput/Frontend');
// Reject malformed selection before loading the harness, freezing sources or executing tests.
const options={};
for(let i=2;i<process.argv.length;++i){
 const key=process.argv[i];
 assert(['--out','--baseline','--case','--before-state'].includes(key),'Unknown option '+key);
 const name=key.slice(2);assert(!Object.hasOwn(options,name),'Duplicate option '+key);
 const value=process.argv[++i];assert(value&&!value.startsWith('--'),'Missing value for '+key);options[name]=value;
}
const selected=options.case;
if(selected){
 assert.equal(selected,'cached-replay-and-live-subscription','Only the bounded original subscription case supports selection');
 assert(!options.baseline,'--baseline belongs to the full historical suite');
 assert(options['before-state'],'Selected comparison requires --before-state');
 assert(fs.statSync(path.resolve(options['before-state'])).isFile(),'Before source must be an existing file');
}else assert(!options['before-state'],'--before-state requires --case');
const evidenceRoot=path.join(workspace,'Testing/YMCC/Evidence');
const out=path.resolve(options.out||path.join(selected?evidenceRoot:root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
if(selected){const relative=path.relative(evidenceRoot,out);assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'Selected output must be inside the core Evidence directory');}
else assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase());
assert(!fs.existsSync(out),'Output already exists');fs.mkdirSync(path.dirname(out),{recursive:true});fs.mkdirSync(out);
const {suite,environment}=await import('./fixtures/virtual_target_policy/harness.mjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),rel=p=>path.relative(out,p).replaceAll('\\','/'),json=(p,v)=>fs.writeFileSync(path.join(out,p),JSON.stringify(v,null,2)+'\n');
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);
async function runSelected(){
 const started=performance.now(),receipt={schema:2,status:'running',startedAt:new Date().toISOString(),case:selected,scope:'Original cached/live subscription case only; real Vue with simulated IPC',executedThisRun:true,sharedCoreBudgetClaimed:false,browserExecuted:false,realNativeIpc:false,nativeModified:false,fullSuite:'NOT_RUN',typecheck:'NOT_RUN',historicalGyroGuard:'NOT_APPLICABLE_TO_SELECTED_CASE',inputs:[],runtimeDependencies:[],mutations:[],timingsMs:{}};
 const save=()=>json('summary.json',receipt);
 const time=async(name,run)=>{const begin=performance.now();try{return await run();}finally{receipt.timingsMs[name]=performance.now()-begin;save();}};
 const copy=(source,destination)=>{const bytes=fs.readFileSync(source),target=path.join(out,destination);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);return {path:source,snapshot:rel(target),bytes:bytes.length,sha256:sha(bytes)};};
 try{
  await time('freeze',()=>{
   for(const file of ['src/bridge/virtualTargetPolicyState.ts','src/bridge/virtualTargetPolicy.ts','src/bridge/ipc.ts','tools/fixtures/virtual_target_policy/harness.mjs','tools/virtual_target_policy_selftest.mjs']){
    const row=copy(path.join(repo,file),path.join('source',file));receipt.inputs.push({...row,repositoryPath:file});
   }
   receipt.beforeSource=copy(path.resolve(options['before-state']),'before/virtualTargetPolicyState.ts');
   for(const file of ['package.json','pnpm-lock.yaml'])if(fs.existsSync(path.join(repo,file)))receipt.inputs.push({...copy(path.join(repo,file),path.join('source',file)),repositoryPath:file,role:'resolution-context'});
   for(const file of Object.keys(require.cache).sort())receipt.runtimeDependencies.push(copy(file,path.join('dependencies',path.relative(repo,file))));
   receipt.runtime={executable:process.execPath,version:process.version,sha256:sha(fs.readFileSync(process.execPath)),vueVersion:require('vue').version,typescriptVersion:require('typescript').version,argv:process.argv,cwd:process.cwd()};
  });
  const frozen=path.join(out,'source'),stateFile='src/bridge/virtualTargetPolicyState.ts',before=fs.readFileSync(path.join(out,'before/virtualTargetPolicyState.ts'),'utf8');
  receipt.before=await time('beforeCase',()=>suite(frozen,{[stateFile]:before},{cases:[selected],expectedRepeatedNotifications:1000}));json('BEFORE-RESULTS.json',receipt.before);assert.equal(receipt.before.failures,0,'Original source reference failed');
  receipt.current=await time('currentCase',()=>suite(frozen,{}, {cases:[selected]}));json('CURRENT-RESULTS.json',receipt.current);assert.equal(receipt.current.failures,0,'Current subscription case failed');
  const source=fs.readFileSync(path.join(frozen,stateFile),'utf8');
  const faults=[
   {name:'reason-change-ignored',from:'next?.reason === previous?.reason',to:'true'},
   {name:'block-change-ignored',from:'next?.steamDeckSelfOutputBlocked === previous?.steamDeckSelfOutputBlocked',to:'true'},
   {name:'all-live-changes-dropped',from:'policy.value = next;',to:'void next;'},
   {name:'scope-unsubscribe-lost',from:'onScopeDispose(stop);',to:'void stop;'}
  ];
  await time('semanticNegatives',async()=>{
   for(const fault of faults){
    assert.equal(source.split(fault.from).length-1,1,'Unique mutation anchor');
    const mutated=source.replace(fault.from,fault.to),dir=path.join(out,'mutations',fault.name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'virtualTargetPolicyState.ts'),mutated);
    const begin=performance.now(),result=await suite(frozen,{[stateFile]:mutated},{cases:[selected]});
    json('mutations/'+fault.name+'/results.json',result);receipt.mutations.push({...fault,result,elapsedMs:performance.now()-begin});save();
    assert.equal(result.scenarios,1);assert.equal(result.failures,1,'Named original case must reject '+fault.name);
   }
  });
  await time('stability',()=>{
   const rows=[...receipt.inputs,...receipt.runtimeDependencies,receipt.beforeSource];
   receipt.stability={allInputsStable:rows.every(row=>sha(fs.readFileSync(row.path))===row.sha256),loadedCommonJsClosureStable:Object.keys(require.cache).every(file=>receipt.runtimeDependencies.some(row=>row.path===file)),guardFunctionUnchanged:before.slice(before.indexOf('export function currentVirtualTargetPolicy()'),before.indexOf('export function useVirtualTargetPolicy()'))===source.slice(source.indexOf('export function currentVirtualTargetPolicy()'),source.indexOf('export function useVirtualTargetPolicy()'))};
   assert(Object.values(receipt.stability).every(Boolean),'Source or runtime dependency drift');
  });
  receipt.status='passed';
 }catch(error){receipt.status='failed';receipt.error=error.stack||String(error);process.exitCode=1;}
 receipt.completedAt=new Date().toISOString();receipt.timingsMs.totalLocal=performance.now()-started;save();
 json('FILES.json',{note:'Self inventory excluded',files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))}))});
 console.log(JSON.stringify({status:receipt.status,evidence:out,before:receipt.before?.observations,current:receipt.current?.observations,negativeCaseExecutions:receipt.mutations.length,timingsMs:receipt.timingsMs,error:receipt.error},null,2));
}
if(selected){await runSelected();}else{

const changed=['src/bridge/virtualTargetPolicy.ts','src/bridge/virtualTargetPolicyState.ts','src/bridge/ipc.ts','src/bridge/virtualOutputStatus.ts','src/views/ButtonMappingView.vue','src/components/GameCustomProfilePanel.vue','src/views/ControllerShortcutEditorView.vue','src/views/PowerView.vue','src/bridge/deckyGlobalActions.ts','src/bridge/deckyGameActions.ts','src/bridge/deckyMirrorHost.ts'];
const receipt={status:'running',schema:1,startedAt:new Date().toISOString(),scope:'Physical Steam Deck self-output UI boundary only; explicit native policy, unknown stays unknown',browserExecuted:false,realNativeIpc:false,realSettingsWrites:false,nativeModified:false,inputs:[],mutations:[]};const save=()=>json('summary.json',receipt),inventory=()=>json('FILES.json',{note:'Self inventory excluded',files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),sha256:sha(fs.readFileSync(p)),bytes:fs.statSync(p).size}))});save();
try{
 const dependencies=['src/bridge/inputStartupPolicy.ts','src/bridge/gamePolicyHysteresis.ts','src/bridge/gyroPresetModel.ts','src/bridge/deckyInputOptions.ts','src/bridge/controllerShortcutRules.ts','src/components/Dropdown.vue','tools/virtual_output_status_selftest.ts'];
 const sources=[...changed,...dependencies,path.relative(repo,runner),...walk(path.join(repo,'tools/fixtures/virtual_target_policy')).map(p=>path.relative(repo,p))];
 for(const file of sources){const bytes=fs.readFileSync(path.join(repo,file)),dest=path.join(out,'source',file);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes);receipt.inputs.push({path:file.replaceAll('\\','/'),snapshot:rel(dest),bytes:bytes.length,sha256:sha(bytes)});}
 const frozen=path.join(out,'source');const baseline=path.resolve(options.baseline||path.join(root,'2026-10-17T07-13-56-891Z-758bff'));const before=JSON.parse(fs.readFileSync(path.join(baseline,'BEFORE.json'),'utf8'));fs.copyFileSync(path.join(baseline,'BEFORE.json'),path.join(out,'BEFORE.json'));
 receipt.preservation={baseline,protectedGyroSha256:sha(fs.readFileSync(path.join(repo,'src/views/GyroMotionView.vue'))),eol:[]};assert.equal(receipt.preservation.protectedGyroSha256,before.protectedGyroSha256,'Protected Gyro source changed');
 for(const row of before.inputs){const old=fs.readFileSync(path.join(baseline,'before',row.path)),current=fs.readFileSync(path.join(repo,row.path)),text=current.toString('utf8');assert.equal(sha(old),row.sha256);const dst=path.join(out,'before',row.path);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.writeFileSync(dst,old);if(row.bareLf===0)assert.equal((text.match(/(?<!\r)\n/g)||[]).length,0,'No new bare LF in CRLF source '+row.path);receipt.preservation.eol.push({path:row.path,beforeCrlf:row.crlf,beforeBareLf:row.bareLf,afterCrlf:(text.match(/\r\n/g)||[]).length,afterBareLf:(text.match(/(?<!\r)\n/g)||[]).length});}
 receipt.results=await suite(frozen);json('RESULTS.json',receipt.results);save();assert.equal(receipt.results.failures,0);console.log(`Isolated actual UI/actions: PASS (${receipt.results.scenarios} scenarios, ${receipt.results.checks} checks)`);
 const sfc=require('vue/compiler-sfc');receipt.sfc=[];
 for(const file of changed.filter(p=>p.endsWith('.vue'))){const text=fs.readFileSync(path.join(frozen,file),'utf8'),parsed=sfc.parse(text,{filename:file});assert.equal(parsed.errors.length,0);const script=sfc.compileScript(parsed.descriptor,{id:'virtual-target-policy'}),template=sfc.compileTemplate({source:parsed.descriptor.template.content,filename:file,id:'virtual-target-policy',compilerOptions:{bindingMetadata:script.bindings}});assert.equal(template.errors.length,0);const dir=path.join(out,'sfc',path.basename(file));fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'script.generated.ts'),script.content);fs.writeFileSync(path.join(dir,'template.generated.js'),template.code);receipt.sfc.push({file,scriptCompiled:true,templateCompiled:true});}
 const button=fs.readFileSync(path.join(frozen,'src/views/ButtonMappingView.vue'),'utf8'),game=fs.readFileSync(path.join(frozen,'src/components/GameCustomProfilePanel.vue'),'utf8'),power=fs.readFileSync(path.join(frozen,'src/views/PowerView.vue'),'utf8'),editor=fs.readFileSync(path.join(frozen,'src/views/ControllerShortcutEditorView.vue'),'utf8'),mirror=fs.readFileSync(path.join(frozen,'src/bridge/deckyMirrorHost.ts'),'utf8');
 assert(button.includes('!!virtualTargetSelectionError(o.value, virtualTargetPolicy)'));assert(button.includes('STEAM_DECK_SELF_OUTPUT_MESSAGE'));assert(game.includes(':options="availablePadOptions"'));assert(power.includes(':options="availableStartupPadOptions"'));assert(editor.includes('virtualTargetOptions(parameter.options || [], virtualTargetPolicy.value)'));assert(mirror.includes('virtualTargetOptions(pad,currentVirtualTargetPolicy())'));assert(mirror.includes('if(next!==observedTargetPolicy)'));
 receipt.selectionEntrypoints=['ButtonMapping persona/save gate','GameCustomProfile persona/enable/save gate','Power boot persona selection','ControllerShortcut virtual-toggle preset/new-save guard','Decky global persona and related activation','Decky game persona and dormant-profile activation'];
 const faults=[
  {name:'policy-block-ignored',file:'src/bridge/virtualTargetPolicy.ts',from:"enabled && persona === 'steamdeck' && policy?.steamDeckSelfOutputBlocked === true",to:"false && enabled && persona === 'steamdeck' && policy?.steamDeckSelfOutputBlocked === true",witness:'all-personas-and-disabled-escape'},
  {name:'unknown-treated-as-deck',file:'src/bridge/virtualTargetPolicy.ts',from:'policy?.steamDeckSelfOutputBlocked === true',to:'policy?.steamDeckSelfOutputBlocked !== false',witness:'all-personas-and-disabled-escape'},
  {name:'queued-save-policy-ignored',file:'src/views/ButtonMappingView.vue',from:'() => !virtualTargetSelectionError(nextPersona, currentVirtualTargetPolicy(), nextEnabled)',to:'() => true',witness:'button-queued-policy-change-cancels'},
  {name:'old-saved-rule-rejected',file:'src/bridge/virtualTargetPolicy.ts',from:'if (!old?.enabled || old.actionId !== rule.actionId',to:'if (true || !old?.enabled || old.actionId !== rule.actionId',witness:'shortcut-old-rule-preserved-new-rule-denied'},
  {name:'native-disabled-reason-ignored',file:'src/bridge/virtualOutputStatus.ts',from:"if (persona === 'steamdeck' && runtime?.requestedPersona === 'steamdeck' && runtime.reason === STEAM_DECK_SELF_OUTPUT_REASON)",to:"if (false && persona === 'steamdeck' && runtime?.requestedPersona === 'steamdeck' && runtime.reason === STEAM_DECK_SELF_OUTPUT_REASON)",witness:'blocked-runtime-never-pending-or-admitted'}
 ];
 for(const fault of faults){const text=fs.readFileSync(path.join(frozen,fault.file),'utf8');assert.equal(text.split(fault.from).length-1,1);const changed=text.replace(fault.from,fault.to),result=await suite(frozen,{[fault.file]:changed});const dir=path.join(out,'mutations',fault.name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,path.basename(fault.file)),changed);json(`mutations/${fault.name}/results.json`,result);receipt.mutations.push({...fault,result});save();assert(result.results.some(r=>r.name===fault.witness&&r.failures>0),'Mutant requires named semantic assertion failure');console.log('Semantic mutant detected: '+fault.name);}
 const esbuild=require('esbuild'),legacy=path.join(out,'virtual-output-status-regression.cjs');const bundled=esbuild.buildSync({entryPoints:[path.join(frozen,'tools/virtual_output_status_selftest.ts')],bundle:true,platform:'node',format:'cjs',outfile:legacy,metafile:true});json('status-regression-build-inputs.json',bundled.metafile);
 const old=spawnSync(process.execPath,[legacy],{encoding:'utf8',windowsHide:true,timeout:30000});fs.writeFileSync(path.join(out,'status-regression.log'),(old.stdout||'')+(old.stderr||''));if(old.error)throw old.error;assert.equal(old.status,0,'Existing status regression failed');receipt.existingStatusRegression={exitCode:old.status,output:old.stdout.trim()};
 const tsc=require.resolve('vue-tsc/bin/vue-tsc.js'),typed=spawnSync(process.execPath,[tsc,'--noEmit','--pretty','false','--incremental','false'],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'typecheck.log'),(typed.stdout||'')+(typed.stderr||''));if(typed.error)throw typed.error;receipt.typecheck={exitCode:typed.status,command:[process.execPath,tsc,'--noEmit','--pretty','false','--incremental','false']};save();assert.equal(typed.status,0,'Project typecheck failed');
 receipt.stability={ownedAndTestInputsStable:receipt.inputs.every(i=>sha(fs.readFileSync(path.join(repo,i.path)))===i.sha256),protectedGyroUnchanged:sha(fs.readFileSync(path.join(repo,'src/views/GyroMotionView.vue')))===before.protectedGyroSha256};assert(receipt.stability.ownedAndTestInputsStable&&receipt.stability.protectedGyroUnchanged);receipt.status='passed';receipt.completedAt=new Date().toISOString();save();inventory();console.log(JSON.stringify({status:receipt.status,evidence:out,results:receipt.results,mutants:receipt.mutations.length,sfc:receipt.sfc.length,typecheck:typed.status,existingStatusRegression:receipt.existingStatusRegression,stability:receipt.stability},null,2));
}catch(e){receipt.status='failed';receipt.error=e.stack||String(e);receipt.completedAt=new Date().toISOString();save();inventory();console.error(receipt.error);console.error('Evidence: '+out);process.exitCode=1;}

}
