import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {runBinding} from './run.mjs';
import {REPO,MODULE,CASE_ID,evidenceSources,artifactInputs} from './adapter.mjs';
import {sourcePath} from '../runner.mjs';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const need=(condition,code)=>{if(!condition)throw Error(code);};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const inside=(root,file)=>{const relative=path.relative(root,file);return relative===''||relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
function outputFiles(root){
 const result=[];
 for(const entry of fs.readdirSync(root,{withFileTypes:true})){
  const file=path.join(root,entry.name);need(!entry.isSymbolicLink(),'OEM_OUTPUT_LINK_NOT_ADMITTED');
  if(entry.isDirectory())result.push(...outputFiles(file));else if(entry.isFile())result.push(file);
 }
 return result;
}
export function validateProducerEvidence(raw,producerRoot){
 need(raw?.status==='PASS'&&raw.mainBindingPresent===true&&/^[a-f0-9]{64}$/.test(raw.gateFragmentSha256??''),'ACTUAL_MAIN_BINDING_NOT_EXECUTED');
 need(Number.isSafeInteger(raw.checks)&&raw.checks>0&&Number.isSafeInteger(raw.selections)&&raw.selections>0,'ORIGINAL_COUNTS_INVALID');
 const root=fs.realpathSync(producerRoot),bindingFile=path.join(root,'positive/source-binding.json');
 const binding=JSON.parse(fs.readFileSync(bindingFile,'utf8'));
 need(binding.mainBindingPresent===true&&binding.gateFragmentSha256===raw.gateFragmentSha256&&raw.selections===binding.cases*binding.families,'ACTUAL_COMPILED_GATE_BINDING_MISMATCH');
 const expected=new Map([
  ['positive',null],['canonical-consumer-class','actual original catalog selection'],
  ['family-rebind','family change invalidates same-class cache'],['class-rebind','class change invalidates original profile consumer'],
 ]);
 need(Array.isArray(raw.results)&&raw.results.length===expected.size&&new Set(raw.results.map(row=>row?.name)).size===expected.size,'ORIGINAL_CASE_SET_INVALID');
 for(const row of raw.results){
  need(expected.has(row?.name)&&Number.isInteger(row.exitCode)&&row.compileExitCode===0,'ORIGINAL_COMPILE_OR_EXIT_INVALID');
  const dir=fs.realpathSync(row.directory);need(inside(root,dir),'ORIGINAL_CASE_OUTSIDE_OWN_EVIDENCE');
  const stdout=fs.readFileSync(path.join(dir,'stdout.log'),'utf8'),stderr=fs.readFileSync(path.join(dir,'stderr.log'),'utf8');
  need(Buffer.byteLength(stdout)<1024*1024&&Buffer.byteLength(stderr)<1024*1024,'ORIGINAL_OUTPUT_UNBOUNDED');
  const compile=JSON.parse(fs.readFileSync(path.join(dir,'compile-result.json'),'utf8').replace(/^\uFEFF/,''));need(compile.exitCode===0,'SEMANTIC_NEGATIVE_MUST_COMPILE');
  if(row.name==='positive'){
   const summaries=[...stdout.matchAll(/^OEM_BINDING_SUMMARY checks=(\d+) selected=(\d+) hardwareAccess=false productionSelectors=true productionEvaluator=true\r?$/gm)];
   need(row.status==='PASS'&&row.exitCode===0&&summaries.length===1&&Number(summaries[0][1])===raw.checks&&Number(summaries[0][2])===raw.selections,'ORIGINAL_TERMINAL_SUMMARY_NOT_COMPLETE');
  }else{
   need(row.status==='EXPECTED_REJECTION'&&row.exitCode>0&&row.assertionFailure===expected.get(row.name),'ORIGINAL_BUSINESS_FAULT_NOT_DETECTED');
   need((stdout+'\n'+stderr).split(/\r?\n/).includes('OEM_BINDING_FAIL '+expected.get(row.name)),'ORIGINAL_FAULT_OUTPUT_NOT_RECORDED');
  }
 }
 return{checks:raw.checks,selections:raw.selections,compiledSemanticRejections:3,mainBindingPresent:true,gateFragmentSha256:raw.gateFragmentSha256};
}
export async function createOutputEvidence(evidenceDir){
 const root=path.resolve(evidenceDir);need(!fs.existsSync(root),'FRESH_EVIDENCE_DIRECTORY_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const sources=evidenceSources().map(relative=>{
  const file=sourcePath(REPO,relative),bytes=fs.readFileSync(file),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:relative,sha256:sha(bytes),copy};
 });
 const tools=artifactInputs().map(file=>({path:file,sha256:hash(file)}));
 try{
  const main=fs.readFileSync(path.join(root,'input-sources/native/main.cpp'),'utf8');
  need(main.includes('oemUnifiedInputBindingFor')&&main.includes('const int profileId = oemBoundChordProfileIdFor('),'PRODUCT_BINDING_PENDING');
  const original=await runBinding(path.join(root,'original'));
  const verified=validateProducerEvidence(original,path.join(root,'original'));
  need(sources.every(row=>hash(sourcePath(REPO,row.path))===row.sha256&&hash(row.copy)===row.sha256),'SOURCE_CHANGED_DURING_OEM_CONSUMPTION');
  need(tools.every(row=>hash(row.path)===row.sha256),'COMPILER_OR_RUNTIME_CHANGED_DURING_OEM_CONSUMPTION');
  const outputs=outputFiles(root);
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,kind:'actual-oem-unified-main-helper-consumption',verified,
   originalReport:{path:path.join(root,'original/result.json'),sha256:hash(path.join(root,'original/result.json')),unmodified:true},
   originalScopeRetained:original.scope,originalFullProductRegistrationClaim:original.registeredProductConsumer,
   sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...outputs.map(file=>({path:file,sha256:hash(file)}))],toolchain:tools,
   claims:{actualMainHelpersCompiled:true,actualGateSelectionExchangeEpochCompiled:true,actualOriginalProfileCatalogAndEvaluatorCompiled:true,fullNativeProgramBuilt:false,fullHookOrWmiLifecycleExecuted:false,physicalHardwareValidated:false},
   remainingGaps:['Original independent hook/WMI/source-switch-release regressions remain separately owned.','No complete native application or physical device execution.','Pinned compiler/runtime executables do not establish a separately enumerated standard-library header closure.','Whole-process CPU reduction has not been measured.'],
  };
  write(path.join(root,'result.json'),result);return result;
 }catch(error){const blocked=error.message==='PRODUCT_BINDING_PENDING';write(path.join(root,'failure.json'),{schemaVersion:1,id:CASE_ID,status:blocked?'BLOCKED':'FAIL',code:error.message,originalEvidencePreserved:true});if(blocked)error.exitCode=10;throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 if(process.argv.length!==4||process.argv[2]!=='--evidence-dir')throw Error('--evidence-dir <new absolute directory> required');
 createOutputEvidence(process.argv[3]).then(result=>console.log(JSON.stringify({id:result.id,status:result.status,verified:result.verified})),error=>{console.error(error.stack??error.message);process.exitCode=error.exitCode??2;});
}
