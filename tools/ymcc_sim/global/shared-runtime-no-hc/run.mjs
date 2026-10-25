import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {runProcess} from '../runner.mjs';
import {REPO,PYTHON,sources} from './adapter.mjs';
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const inside=(a,b)=>{const r=path.relative(a,b);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
async function main(out){
 out=path.resolve(out);if(fs.existsSync(out)||inside(REPO,out)||inside(out,REPO))throw Error('FRESH_EXTERNAL_EVIDENCE_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const before=sources().map(p=>({path:p,sha256:hash(path.join(REPO,p))}));
 const result={schemaVersion:1,status:'FAIL',sourceHashes:before,hardwareIo:false,productStarted:false,commands:[]};
 try{
  for(const [name,args]of [['controls',['tools/ymcc-shared-runtime/selftest.py','--out',path.join(out,'controls')]],['build',['tools/ymcc-shared-runtime/verify.py','--verify-original','--build','--scratch',path.join(out,'build')]]]){
   const call=await runProcess(PYTHON,args,{cwd:REPO,env:process.env,timeoutMs:180000,outputLimit:1024*1024});fs.writeFileSync(path.join(out,name+'.stdout.log'),call.stdout);fs.writeFileSync(path.join(out,name+'.stderr.log'),call.stderr);const {stdout,stderr,...proof}=call;result.commands.push({name,...proof});if(call.exitCode!==0||call.timedOut||call.launchError||call.outputTruncated)throw Error('SHARED_'+name.toUpperCase()+'_FAILED');
  }
  const controls=read(path.join(out,'controls/result.json')),build=read(path.join(out,'build/verification.json'));
  if(controls.status!=='PASS'||controls.cases.length!==9||controls.cases.filter(c=>c.rejected).length!==7||build.status!=='SHARED_RUNTIME_NO_HC_BUILD_AND_POLICY_PASS'||build.build.hcDirectoriesPresent!==false||build.build.linkedSources.length!==5||build.lock.originalVerified!==true)throw Error('SHARED_REQUIRED_CONSUMERS_NOT_PROVEN');
  result.sourceChanges=before.filter(r=>hash(path.join(REPO,r.path))!==r.sha256);if(result.sourceChanges.length)throw Error('SHARED_SOURCE_CHANGED');
  result.status='PASS';result.summary={sharedFiles:build.lock.files.length,linkedProductionSources:build.build.linkedSources.length,actualConsumers:Object.keys(build.build.published),negativeControls:7,dependencyPolicy:build.build.policy.status,original:build.lock.originalIdentity};result.receipts=['controls/result.json','build/verification.json'].map(p=>({path:path.join(out,p),sha256:hash(path.join(out,p))}));
 }catch(error){result.failure=error.message;}
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,summary:result.summary,failure:result.failure}));return result.status==='PASS'?0:2;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){if(process.argv.length!==4||process.argv[2]!=='--evidence-dir')throw Error('EVIDENCE_ARGUMENT_REQUIRED');process.exitCode=await main(process.argv[3]);}
