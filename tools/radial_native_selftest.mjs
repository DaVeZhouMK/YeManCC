import fs from 'node:fs';
import path from 'node:path';
import {begin,fixture,instrument,compile,mergeRenderer,executeReviewed,verify,receipt,readJson,save,need,sha} from './fixtures/radial_private/scope.mjs';
const root=path.resolve(import.meta.dirname,'..'),args=process.argv.slice(2),arg=name=>{const i=args.indexOf(name);return i<0?'':args[i+1];};
need(arg('--web-root')&&arg('--out-dir'),'WEB_AND_OUT_REQUIRED');
const web=path.resolve(arg('--web-root')),out=path.resolve(arg('--out-dir')),resume=args.includes('--resume-prepared');
need(fs.existsSync(path.join(web,'radial-output.html')),'ACTUAL_RENDERER_REQUIRED');
const scope=begin(root,out,'tools/radial_native_selftest.mjs',{resume});
let report={pass:false,normalYmccStarted:false,systemInputInjected:false,hardwareWrites:0,physicalAcceptance:false,nativeExecuted:false};
try{
 mergeRenderer(scope,path.join(web,'renderer-fixture.json'));
 let exe;
 if(resume){const prepared=readJson(path.join(out,'prepared.json'));need(prepared.pass&&prepared.executableSha256===sha(prepared.executable),'PREPARED_EXE_CHANGED');for(const [relative,digest]of Object.entries(prepared.sourceHashes))need(sha(path.isAbsolute(relative)?relative:path.join(root,relative))===digest,'PREPARED_SOURCE_CHANGED');scope.sourceHashes={...scope.sourceHashes,...prepared.sourceHashes};scope.generated=prepared.compiledArtifacts;exe=prepared.executable;}
 else{await fixture(scope,'tools/radial_native_fixture.ts');const entry=instrument(scope,false);exe=await compile(scope,entry,'radial-native.exe');save(path.join(out,'prepared.json'),{...receipt(scope),pass:true,nativeExecuted:false,executable:exe,executableSha256:sha(exe),toolProof:verify(scope)});}
 if(args.includes('--prepare-only'))report={...report,status:'PREPARED_AWAITING_PARENT_REVIEW',prepared:true,toolProof:verify(scope)};
 else{
  report.executionIdentity=await executeReviewed(scope,exe,web);report.nativeExecuted=true;
  const consumer=readJson(path.join(out,'native-overlay.json')),launcher=readJson(path.join(out,'private-launch.json'));
  need(consumer.pass&&launcher.pass&&consumer.cases.length>0&&consumer.cases.every(row=>row.pass===true),'ORIGINAL_NATIVE_ASSERTIONS');
  report={...report,pass:true,cases:consumer.cases.length,toolProof:verify(scope),boundary:'Actual owned private desktop, Overlay, WebView2, Composition and independent-input UI infrastructure; synthetic packets enter owned consumers. Parent-reviewed runtime required.'};
 }
}catch(error){report.error=error.stack;report.status=error.blocked?'BLOCKED':'FAIL';process.exitCode=error.blocked?7:2;}
finally{report.nativeExecutionAttempted=scope.nativeLaunchAttempted===true;report.nativeExecuted=scope.nativeExecutionStarted===true;save(path.join(out,'verification.json'),receipt(scope,report));console.log(JSON.stringify({pass:report.pass,status:report.status,nativeExecuted:report.nativeExecuted,out,error:report.error}));}
