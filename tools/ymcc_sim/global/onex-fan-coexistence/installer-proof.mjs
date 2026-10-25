import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
export function runInstallerProof({repo,out,run}){
 const root=path.join(out,'installer-admission');fs.mkdirSync(root);
 const source=path.join(repo,'PowerControl/fan-host/install-fan-host-payload.ps1');
 const baseline=path.join(repo,'tools/ymcc_sim/global/fan-package-source/fixtures/legacy7f-payload/install-fan-host-payload.ps1');
 const baselineSha256='5a6d68f5ae5573f78cc77f147b295af802cc757d1855d0996239b14af6f3fbdf';
 if(hash(baseline)!==baselineSha256)throw Error('Original installer oracle changed');
 const positive=path.join(root,'current.ps1'),fault=path.join(root,'private-fault.ps1'),original=path.join(root,'original.ps1');
 fs.copyFileSync(source,positive);fs.copyFileSync(baseline,original);
 const text=fs.readFileSync(source,'utf8'),needle=", 'YeManFanBackend.dll'";
 if(text.split(needle).length!==2)throw Error('Single required-backend fault anchor differs');
 fs.writeFileSync(fault,text.replace(needle,''));
 const fixture=path.join(repo,'tools/ymcc_sim/global/onex-fan-coexistence/InstallerCases.ps1');
 const engine=path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe');
 const rows=[];
 for(const[mode,file]of[['positive',positive],['original',original],['fault',fault]]){
  const resultRoot=path.join(root,mode);run(engine,['-NoProfile','-ExecutionPolicy','Bypass','-File',fixture,'-InstallerSource',file,'-OutputDirectory',resultRoot],path.relative(out,path.join(root,mode+'.log')),30000,mode==='positive'?0:2);
  const result=read(path.join(resultRoot,'result.json')),failures=result.cases.filter(c=>!c.passed);
  if(result.caseCount!==9||result.sourceSha256!==hash(file)||result.fullInstallerExecuted!==false||result.deploymentPerformed!==false||result.hardwareIo!==false)throw Error('Installer actual-consumer scope/identity differs');
  if(mode==='positive'){if(result.status!=='PASS'||failures.length)throw Error('Current required-file consumer failed');}
  else if(result.status!=='FAIL'||failures.length!==1||failures[0].id!=='installer-required:YeManFanBackend.dll'||!failures[0].error?.includes('Mandatory member accepted'))throw Error('Installer original/private fault did not reproduce exact backend omission');
  rows.push({mode,cases:result.caseCount,failedCount:result.failedCount,resultPath:path.join(resultRoot,'result.json'),sourceSha256:result.sourceSha256,actualGateSha256:result.actualGateSha256});
 }
 const proof={status:'PASS',cases:9,sourceSha256:hash(source),originalSha256:baselineSha256,executions:rows,actualUniqueRequiredFileLoopExecuted:true,privateFaultChangesOnlyRequiredBackendDeclaration:true,fullInstallerExecuted:false,administratorGateExecuted:false,deploymentPerformed:false,hardwareIo:false};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2)+'\n');
 return{proof,evidence:tree(root).map(file=>({path:file,sha256:hash(file)})),inputs:[source,baseline,fixture].map(file=>({path:file,sha256:hash(file)}))};
}
