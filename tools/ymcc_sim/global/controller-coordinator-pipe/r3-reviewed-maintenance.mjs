// Source-relative maintenance of already compiled, explicitly reviewed prerequisites.
// No build, manifest re-signing, registry edit or installed-product operation.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath,pathToFileURL} from 'node:url';
export async function verifyReviewedLeaves({repoRoot,evidenceRoot,selection='all'}){
 const repo=path.resolve(repoRoot),out=path.resolve(evidenceRoot),relative=path.relative(repo,out);
 if(relative===''||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative)))throw Error('OWNED_OUTPUT_MUST_BE_OUTSIDE_SOURCE');
 if(fs.existsSync(out))throw Error('FRESH_OWNED_EVIDENCE_REQUIRED');
 const keys=['positive','cancellation','psOwnership','psBinding','lifecycle'];if(selection!=='all'&&!keys.includes(selection))throw Error('ORIGINAL_LEAF_SELECTION_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const {runProcess}=await import(pathToFileURL(path.join(repo,'tools/ymcc_sim/global/runner.mjs'))),steps=[];
 for(const key of(selection==='all'?keys:[selection])){
  const lifecycle=key==='lifecycle',entry=path.join(repo,'tools/ymcc_sim/global',lifecycle?'controller-coordinator-lifecycle':'controller-coordinator-pipe','run.mjs'),target=path.resolve(out,key),args=[entry,...(lifecycle?[]:['--case',key]),'--evidence-dir',target];
  const p=await runProcess(process.execPath,args,{cwd:repo,env:process.env,timeoutMs:lifecycle?90000:120000,outputLimit:8*1024*1024});fs.writeFileSync(path.join(out,key+'.stdout.log'),p.stdout??'');fs.writeFileSync(path.join(out,key+'.stderr.log'),p.stderr??'');const{stdout,stderr,...terminal}=p,file=path.join(target,'result.json'),value=fs.existsSync(file)?JSON.parse(fs.readFileSync(file,'utf8')):null;
  steps.push({case:key,exe:process.execPath,args,...terminal,result:file,status:value?.status??'INCOMPLETE',error:value?.error??null});fs.writeFileSync(path.join(out,'LEAF-RESULTS.json'),JSON.stringify({status:steps.every(s=>s.exitCode===0&&s.status==='PASS')?'PASS':'FAIL',steps,compilerExecuted:false,registryEdited:false,physicalHardwareAccepted:false},null,2)+'\n');
 }
 return {passed:steps.every(s=>s.exitCode===0&&s.status==='PASS'),steps};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const a=process.argv.slice(2),o={};if(a.length%2)throw Error('Use --repo-root ABS --evidence-root ABS_NEW [--case all|positive|cancellation|psOwnership|psBinding|lifecycle]');for(let i=0;i<a.length;i+=2){if(!['--repo-root','--evidence-root','--case'].includes(a[i])||o[a[i]]||!a[i+1])throw Error('ARGUMENT_INVALID');o[a[i]]=a[i+1];}if(!o['--repo-root']||!o['--evidence-root'])throw Error('EXPLICIT_SOURCE_AND_NEW_OWNED_OUTPUT_REQUIRED');const r=await verifyReviewedLeaves({repoRoot:o['--repo-root'],evidenceRoot:o['--evidence-root'],selection:o['--case']??'all'});console.log(JSON.stringify({status:r.passed?'PASS':'FAIL',leaves:r.steps.length}));if(!r.passed)process.exitCode=1;
 }catch(e){console.error(e.stack??e.message);process.exitCode=1;}
}
