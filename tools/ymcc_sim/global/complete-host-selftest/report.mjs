import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {validateContract,assessAdapterExecution,digest} from './contract.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export function buildCompleteHostReport({contract,execution,stdout,stderr,sourceHashes=[]}){
 const outcome=assessAdapterExecution({contract,execution,stdout,stderr});
 return {schemaVersion:1,kind:'ymcc-complete-host-selftest',status:outcome.status,exitCode:outcome.status==='PASS'?0:outcome.status==='BLOCKED'?10:2,mode:'complete-host-selftest',actualHostSelfTest:execution.started===true,checksPassed:outcome.checksPassed,checkCountObserved:outcome.checkCountObserved,actualResult:outcome.result,issues:outcome.issues,process:execution,sourceHashes,evidenceScope:'Whole Host --self-test only; no candidate admission, installation, HC optional candidate groups, full-nine or physical acceptance',physicalHardwareValidated:false,operatingSystemSleepPerformed:false,installedApplicationExercised:false,candidateInstalled:false,fullGoalComplete:false};
}
export function finalizeReport({evidenceDir,contractPath}){
 const root=fs.realpathSync(evidenceDir),work=path.join(root,'complete-host-selftest');
 const safe=name=>{const file=path.join(work,name);if(!inside(root,fs.realpathSync(file)))throw Error('EVIDENCE_ESCAPES_OWN_CASE');return file;};
 const contract=validateContract(read(contractPath)),execution=read(safe('process.json'));
 const stdoutFile=safe('stdout.log'),stderrFile=safe('stderr.log');
 for(const file of [stdoutFile,stderrFile])if(fs.statSync(file).size>contract.maxLogBytes)throw Error('LOG_SIZE_LIMIT');
 const stdout=fs.readFileSync(stdoutFile),stderr=fs.readFileSync(stderrFile);
 const codeRoot=path.dirname(fileURLToPath(import.meta.url));const sources=['adapter.mjs','contract.mjs','report.mjs','validate-contract.mjs','Run-CompleteHost.ps1'].map(name=>({path:path.join(codeRoot,name),sha256:digest(fs.readFileSync(path.join(codeRoot,name)))}));
 // The global reader binds repository sources by relative name and case outputs by absolute own-case path.
 const codeRecords=sources.map(row=>({path:'tools/ymcc_sim/global/complete-host-selftest/'+path.basename(row.path),sha256:row.sha256}));
 const records=[...codeRecords,...['process.json','stdout.log','stderr.log','contract.json'].map(name=>({path:safe(name),sha256:digest(fs.readFileSync(safe(name)))}))];
 const result=buildCompleteHostReport({contract,execution,stdout,stderr,sourceHashes:records});
 fs.writeFileSync(path.join(root,'complete-host-selftest.json'),JSON.stringify(result,null,2)+'\n');return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);if(args.length!==4||args[0]!=='--evidence-dir'||args[2]!=='--contract')throw Error('FIXED_REPORT_ARGUMENTS_REQUIRED');
 const result=finalizeReport({evidenceDir:args[1],contractPath:args[3]});console.log(JSON.stringify({status:result.status,checksPassed:result.checksPassed,checkCountObserved:result.checkCountObserved}));process.exitCode=result.exitCode;
}