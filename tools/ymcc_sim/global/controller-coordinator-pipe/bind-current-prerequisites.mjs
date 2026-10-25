// Exact current-source identity binding only. Original builders, fixtures, scenarios and
// assertions remain unchanged. The nominated proof is an exact mode projection
// of the original builder receipt, retaining its original path/hash as evidence.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {bindR3Fixture} from './bind-r3-fixtures.mjs';
import {R3_LAYOUT,fileHash,within} from './r3-source-equivalence.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const write=(p,j)=>fs.writeFileSync(p,JSON.stringify(j,null,2)+'\n',{flag:'wx'});
const need=(v,m)=>{if(!v)throw Error('CURRENT_BIND_'+m)};
export function bindCurrent(){
 const root=path.resolve(R3_LAYOUT.authorizedBuildRoot),workspace=path.resolve(R3_LAYOUT.workspaceRoot);
 const binding=R3_LAYOUT.fixedRunInputBinding;need(fileHash(binding.path)===binding.sha256,'FIXED_RUN_BINDING_CHANGED');const fixed=read(binding.path);need(fileHash(fixed.manifest)===fixed.manifestSha256,'FIXED_SOURCE_MANIFEST_CHANGED');
 const names=['positive','cancellation','psOwnership','psBinding','admission','inheritance','switch-return','switch-owner'];
 const contracts={};for(const name of names){
  const dir=path.join(root,name);need(within(root,dir),'BUILD_ESCAPE');const sm=read(path.join(dir,'source-manifest.json'));
  for(const row of sm.records){need(fileHash(path.join(fixed.snapshotRoot,row.path))===row.sha256&&fileHash(path.join(repo,row.path))===row.sha256&&fileHash(row.snapshot)===row.sha256,'EXACT_SOURCE:'+row.path);}
  const resultFile=path.join(dir,'RESULT.json'),proof=read(resultFile);need(proof.sourceFullTranslationUnit===true&&proof.changed.length===0,'UNSTABLE_BUILD:'+name);
  if(name==='positive'){
   need(proof.results.length===13&&proof.results.at(-1).mode==='full-pipe','ORIGINAL_ALL_SUITE_SEQUENCE');
   const proofDir=path.join(dir,'pipe-mode-proof');fs.mkdirSync(proofDir);const selected=proof.results.filter(r=>r.mode==='full-pipe');need(selected.length===1,'ONE_PIPE_MODE');
   const proofFile=path.join(proofDir,'RESULT.json');write(proofFile,{...proof,results:selected,projection:{source:resultFile,sha256:fileHash(resultFile),selectedOriginalIndices:[12],meaning:'Exact original mode result; original full receipt is retained. No result, assertion, status or timeout is rewritten.'}});
   contracts.positive=bindR3Fixture({repoRoot:repo,buildRoot:dir,mode:'pipe',behaviorProof:proofFile});
   contracts.lifecycle=bindR3Fixture({repoRoot:repo,buildRoot:dir,mode:'lifecycle'});
  }else contracts[name]=bindR3Fixture({repoRoot:repo,buildRoot:dir,mode:['cancellation','psOwnership','psBinding'].includes(name)?'pipe':'lifecycle'});
 }
 const pin=c=>({manifest:c.manifest,sha256:c.sha256});const lc='tools/ymcc_sim/global/controller-coordinator-lifecycle/precompiled-fixtures.json';
 const publish=(rel,value)=>{const p=path.join(repo,rel),before=path.join(workspace,'before-files',rel);fs.mkdirSync(path.dirname(before),{recursive:true});need(!fs.existsSync(before),'BEFORE_ALREADY_EXISTS');fs.copyFileSync(p,before);fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n');};
 publish(lc,{schemaVersion:3,lifecycleVersion:'VC-LIFECYCLE-20261020-R3',sourceVersion:R3_LAYOUT.sourceVersion,fixtures:['lifecycle','admission','inheritance','switch-return','switch-owner'].map(n=>pin(contracts[n])),pipe:pin(contracts.positive),meaning:'Fresh D-only exact B46 compiled prerequisites. Raw behavioral results and original scenario gates remain authoritative; no old artifact is renamed or resigned.'});
 for(const [name,file] of [['cancellation','cancellation-negative.json'],['psOwnership','ps-ownership-negative.json'],['psBinding','ps-binding-negative.json']])publish('tools/ymcc_sim/global/controller-coordinator-pipe/'+file,{schemaVersion:3,lifecycleVersion:'VC-LIFECYCLE-20261020-R3',sourceVersion:R3_LAYOUT.sourceVersion,...pin(contracts[name]),meaning:'Fresh exact B46 build of the unchanged original mutation recipe. A raw FAIL or original contract rejection remains unchanged.'});
 const summary=Object.fromEntries(Object.entries(contracts).map(([name,c])=>[name,{...pin(c),behaviorStatus:c.value.behaviorProof.status,exe:c.value.exe,sourceCount:c.value.sources.length,compilerInputs:c.value.compilerInputs.length,compilerIncludes:c.value.compilerIncludes.length}]));write(path.join(workspace,'bound-prerequisites.json'),summary);return summary;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{console.log(JSON.stringify(bindCurrent(),null,2));}catch(e){console.error(e.stack);process.exitCode=1;}}
