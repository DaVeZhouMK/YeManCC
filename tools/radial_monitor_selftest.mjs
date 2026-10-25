import fs from 'node:fs';
import path from 'node:path';
import {begin,fixture,instrument,compile,mergeRenderer,executeReviewed,verify,receipt,readJson,save,need,sha,run} from './fixtures/radial_private/scope.mjs';
const root=path.resolve(import.meta.dirname,'..'),args=process.argv.slice(2),arg=name=>{const i=args.indexOf(name);return i<0?'':args[i+1];};
need(arg('--out-dir'),'OUT_REQUIRED');const out=path.resolve(arg('--out-dir')),resume=args.includes('--resume-prepared');
const scope=begin(root,out,'tools/radial_monitor_selftest.mjs',{resume}),web=arg('--web-root')?path.resolve(arg('--web-root')):path.join(out,'web');
let report={pass:false,normalYmccStarted:false,systemInputInjected:false,hardwareWrites:0,configurationWrites:0,physicalAcceptance:false,nativeExecuted:false};
try{
 // The existing global case owns both actual RadialOutput and HUD builds.
 // Legacy explicit web-root remains supported only with a complete build receipt.
 if(!resume&&!arg('--web-root')){const result=await run(process.execPath,[path.join(root,'tools/radial_renderer_fixture.mjs'),web],{cwd:root,windowsHide:true,timeout:30000,maxBuffer:2e6});fs.writeFileSync(path.join(out,'renderer.log'),result.stdout+result.stderr);}
 mergeRenderer(scope,path.join(web,'renderer-fixture.json'));need(fs.existsSync(path.join(web,'hud-output.html')),'HUD_REQUIRED');
 let exe;
 if(resume){const prepared=readJson(path.join(out,'prepared.json'));need(prepared.pass&&prepared.executableSha256===sha(prepared.executable),'PREPARED_EXE_CHANGED');for(const [relative,digest]of Object.entries(prepared.sourceHashes))need(sha(path.isAbsolute(relative)?relative:path.join(root,relative))===digest,'PREPARED_SOURCE_CHANGED');scope.sourceHashes={...scope.sourceHashes,...prepared.sourceHashes};scope.generated=prepared.compiledArtifacts;exe=prepared.executable;}
 else{
  await fixture(scope,'tools/radial_monitor_fixture.ts');
  if(args.includes('--feedback-only')){const file=path.join(out,'frame.json'),frame=readJson(file);frame.feedbackOnly=true;save(file,frame);scope.sourceHashes[file]=sha(file);scope.generated.find(row=>row.path===file).sha256=sha(file);}
  const entry=instrument(scope,true);exe=await compile(scope,entry,'monitor-join.exe');save(path.join(out,'prepared.json'),{...receipt(scope),pass:true,nativeExecuted:false,executable:exe,executableSha256:sha(exe),toolProof:verify(scope)});
 }
 if(args.includes('--prepare-only'))report={...report,status:'PREPARED_AWAITING_PARENT_REVIEW',prepared:true,toolProof:verify(scope)};
 else{report.executionIdentity=await executeReviewed(scope,exe,web);report.nativeExecuted=true;const consumer=readJson(path.join(out,'native-overlay.json')),launcher=readJson(path.join(out,'private-launch.json'));need(consumer.pass&&launcher.pass&&consumer.cases.length>0&&consumer.cases.every(row=>row.pass===true),'ORIGINAL_MONITOR_ASSERTIONS');report={...report,pass:true,cases:consumer.cases.length,toolProof:verify(scope)};}
}catch(error){report.error=error.stack;report.status=error.blocked?'BLOCKED':'FAIL';process.exitCode=error.blocked?7:2;}
finally{report.nativeExecutionAttempted=scope.nativeLaunchAttempted===true;report.nativeExecuted=scope.nativeExecutionStarted===true;save(path.join(out,'verification.json'),receipt(scope,report));console.log(JSON.stringify({pass:report.pass,status:report.status,nativeExecuted:report.nativeExecuted,out,error:report.error}));}
