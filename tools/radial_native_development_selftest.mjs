import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {begin,run,readJson,save,mergeRenderer,receipt,verify,need,sha} from './fixtures/radial_private/scope.mjs';
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2]&&path.resolve(process.argv[2]),args=process.argv.slice(3),resume=args.includes('--resume-prepared');
need(out,'OUT_REQUIRED');const scope=begin(root,out,'tools/radial_native_development_selftest.mjs',{resume});
let report={pass:false,userEntryAvailable:false,ordinaryYmccLaunched:false,systemInputInjected:false,hardwareWrites:0,physicalAcceptance:false,nativeExecuted:false,scope:'Actual native Overlay, controlled input and actual RadialOutput with an explicit private fixture bootstrap; does not enable product policy.'};
try{
 assert.match(fs.readFileSync(path.join(root,'src/radial/availability.ts'),'utf8'),/RADIAL_USER_AVAILABLE\s*=\s*true/);
 assert.match(fs.readFileSync(path.join(root,'src/radial/testingState.ts'),'utf8'),/radialTestingEnabled = shallowRef\(false\)/);
 for(const relative of ['src/radial/availability.ts','src/radial/testingState.ts','native/radial_release.h'])scope.sourceHashes[relative]=sha(path.join(root,relative));
 const web=path.join(out,'web'),native=path.join(out,'native');
 if(!resume){const build=await run(process.execPath,[path.join(root,'tools/radial_renderer_fixture.mjs'),web],{cwd:root,windowsHide:true,timeout:30000,maxBuffer:2e6});fs.writeFileSync(path.join(out,'renderer.log'),build.stdout+build.stderr);}
 mergeRenderer(scope,path.join(web,'renderer-fixture.json'));
 const result=await run(process.execPath,[path.join(root,'tools/radial_native_selftest.mjs'),'--web-root',web,'--out-dir',native,...args],{cwd:root,windowsHide:true,timeout:150000,maxBuffer:2e6});fs.writeFileSync(path.join(out,'native.log'),result.stdout+result.stderr);
 const fixture=readJson(path.join(web,'renderer-fixture.json')),nativeReceipt=readJson(path.join(native,'verification.json'));
 for(const [relative,digest]of Object.entries(nativeReceipt.sourceHashes))scope.sourceHashes[relative]=digest;
 report.nativeBuild=path.join(native,'native-build-graph.json');
 if(args.includes('--prepare-only'))report={...report,status:'PREPARED_AWAITING_PARENT_REVIEW',prepared:true,toolProof:verify(scope)};
 else{
  const consumer=readJson(path.join(native,'native-overlay.json'));assert.ok(fixture.pass&&nativeReceipt.pass&&consumer.pass);assert.ok(consumer.cases.every(item=>item.pass));
  report={...report,pass:true,nativeExecuted:true,nativeChecks:consumer.cases.length,rendererProductInputs:Object.keys(fixture.sourceHashes).filter(file=>!path.isAbsolute(file)).length,caseNames:consumer.cases.map(item=>item.name),toolProof:verify(scope)};
 }
}catch(error){report.error=error.stack;report.status=error.code===7?'BLOCKED':'FAIL';process.exitCode=error.code===7?7:2;}
finally{const nested=path.join(out,'native/verification.json');if(fs.existsSync(nested)){const value=readJson(nested);report.nativeExecuted=value.nativeExecuted===true;report.nativeExecutionAttempted=value.nativeExecutionAttempted===true;}else if(report.error){report.nativeExecuted=null;report.nativeExecutionStateUnknown=true;}save(path.join(out,'verification.json'),receipt(scope,report));console.log(JSON.stringify({pass:report.pass,status:report.status,nativeExecuted:report.nativeExecuted,out,error:report.error}));}
