import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(process.argv.length!==4||process.argv[2]!=='--out')throw Error('Usage: --out isolated-evidence-directory');
const out=path.resolve(process.argv[3]),rel=path.relative(repo,out);
if(!rel||(!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel)))throw Error('Evidence must be outside production source');
fs.mkdirSync(out,{recursive:true});
const files=['capabilities/hc-device-catalog.json','capabilities/HcDeviceCapabilityCatalog.generated.cs'];
const sha=f=>createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const before=files.map(f=>({path:f,sha256:sha(path.join(repo,f))}));
const cases=[];
for(const id of ['current','wrong-source-count','wrong-consumer-alias','missing-generated-source']){
 const fixture=path.join(out,id,'generated');
 fs.mkdirSync(path.join(fixture,'capabilities'),{recursive:true});
 for(const f of files)if(id!=='missing-generated-source'||f.endsWith('.json'))fs.copyFileSync(path.join(repo,f),path.join(fixture,f));
 if(id==='wrong-source-count'){const p=path.join(fixture,files[0]),data=JSON.parse(fs.readFileSync(p,'utf8'));data.scope.factoryInstantiableClasses++;fs.writeFileSync(p,JSON.stringify(data,null,2)+'\n');}
 if(id==='wrong-consumer-alias'){const p=path.join(fixture,files[1]),s=fs.readFileSync(p,'utf8'),changed=s.replace('hc.device.ROGAlly"','hc.device.ROGAllyFake"');if(changed===s)throw Error('Mutation marker absent');fs.writeFileSync(p,changed);}
 const run=spawnSync(process.execPath,['tools/hc_capability_catalog_generate.mjs','--check','--generated-root',fixture,'--out',path.join(out,id,'extractor')],{cwd:repo,windowsHide:true,encoding:'utf8',timeout:120000,maxBuffer:2*1024*1024});
 const log=(run.stdout??'')+(run.stderr??'')+(run.error??'');fs.writeFileSync(path.join(out,id,'check.log'),log);
 const ok=id==='current'?run.status===0:run.status!==0&&log.includes('STALE_HC_CAPABILITY_CATALOG');
 cases.push({id,status:ok?'PASS':'FAIL',actualExitCode:run.status,expectedRejection:id!=='current'});
}
const after=files.map(f=>({path:f,sha256:sha(path.join(repo,f))}));
const productionUnchanged=JSON.stringify(before)===JSON.stringify(after);
const result={status:productionUnchanged&&cases.every(c=>c.status==='PASS')?'PASS':'FAIL',cases,productionUnchanged,sourceInputs:after,hardwareIo:false,assemblyLoaded:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result));if(result.status!=='PASS')process.exitCode=1;
