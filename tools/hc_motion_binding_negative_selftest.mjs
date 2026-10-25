// Compiled faults against the actual total binding and production Bound/pair consumers.
// All product/OS/device terminals come from the original memory fixture.
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';
import {motionConsumerInputs,REPO_ROOT} from './ymcc_sim/global/motion-consumer-signals/inputs.mjs';
import {hashFile,inside,ownedTree} from './ymcc_sim/global/motion-consumer-signals/evidence.mjs';
const args=process.argv.slice(2);if(args.length!==2||args[0]!=='--out')throw Error('Usage --out fresh outside evidence directory');
const out=path.resolve(args[1]);if(inside(REPO_ROOT,out)||fs.existsSync(out)||out.length>165)throw Error('BINDING_FAULT_FRESH_SHORT_EXTERNAL_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
const inputs=motionConsumerInputs(),sources=[...new Set([...inputs.sources.map(f=>path.join(REPO_ROOT,f)),...inputs.artifacts,path.join(REPO_ROOT,'tools/hc_motion_binding_negative_selftest.mjs')])];
const before=sources.map(file=>({path:file,sha256:hashFile(file)})),header=fs.readFileSync(path.join(REPO_ROOT,'capabilities/HcMotionCapabilityCatalog.h'),'utf8'),cases=[];
const faults=[
 ['missing-registration-consumed',' || !descriptor(binding)',''],
 ['copied-recipe-consumed','return binding.recipe == recipe ? recipe : nullptr;','return recipe;'],
 ['wrong-accel-bound','recipe->accelMatrixId != row.accelMatrixId ||','false ||'],
 ['wrong-source-bound','recipe->sourceSha256 != row.sourceSha256','false'],
 ['controller-admitted-windows',' || recipe->controllerPreferred',''],
 ['copied-generic-consumed',' && binding.recipe == &recipe',''],
];
let failure=null;
try {
 for(const [index,[id,marker,replacement]] of faults.entries()) {
  const dir=path.join(out,String(index));fs.mkdirSync(dir);const mutated=header.replace(marker,()=>replacement);if(mutated===header)throw Error('BINDING_FAULT_MARKER_MISSING:'+id);
  const input=path.join(dir,'binding.h');fs.writeFileSync(input,mutated);const raw=path.join(dir,'r');
  const call=spawnSync(process.execPath,[path.join(REPO_ROOT,'tools/hc_model_motion_consumer_selftest.mjs'),'--collect-failures','--binding-header',input,'--out',raw],{cwd:REPO_ROOT,windowsHide:true,encoding:'utf8',timeout:180000,maxBuffer:2*1024*1024});
  fs.writeFileSync(path.join(dir,'producer.log'),(call.stdout??'')+(call.stderr??'')+(call.error?.message??''));
  const report=JSON.parse(fs.readFileSync(path.join(raw,'source-bindings.json'),'utf8'));
  const detected=call.status===1&&report.status==='fail'&&report.error==='Semantic failures in candidate'&&report.sourceAtFinish?.fixturesUnchanged===true&&report.configurations.length===2&&report.configurations.every(c=>c.exitCode===1&&c.report.status==='fail'&&c.report.failures.some(f=>f.group.startsWith('unified-')));
  cases.push({id,status:detected?'PASS':'FAIL',semanticDetection:detected,configurations:report.configurations.map(c=>({name:c.name,exitCode:c.exitCode,checks:c.report.checks,failedAssertions:c.report.failures.length,groups:[...new Set(c.report.failures.map(f=>f.group))]})),inputSha256:hashFile(input),reportSha256:hashFile(path.join(raw,'source-bindings.json'))});
  console.log(id+': '+(detected?'DETECTED in Release + Checked':'FAIL'));if(!detected)throw Error('BINDING_FAULT_NOT_SEMANTICALLY_DETECTED:'+id);
 }
}catch(error){failure=error.message;}
const sourceChanges=before.filter(row=>hashFile(row.path)!==row.sha256),closure=ownedTree(out).map(file=>({path:file,sha256:hashFile(file)}));
const result={schemaVersion:1,status:!failure&&!sourceChanges.length&&cases.length===faults.length&&cases.every(c=>c.status==='PASS')?'PASS':'FAIL',cases,failure,sourceInputs:before,sourceChanges,evidence:closure,hardwareIo:false,physicalAcceptance:false,completeMotionDomain:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,detected:cases.filter(c=>c.semanticDetection).length,failure}));if(result.status!=='PASS')process.exitCode=1;
