import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {validateOriginalFactoryCounterexample} from './factory-counterexample-raw.mjs';

// These are acceptance controls over metadata already read by the reviewed PE reader.
// They never load a target assembly or count as a factory replay.
export function runFactoryCounterexampleRawControls({summaryPath,outDir}) {
  const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
  const pin=p=>({path:p,sha256:createHash('sha256').update(fs.readFileSync(p)).digest('hex')});
  const checked=p=>{assert.equal(pin(p.path).sha256,p.sha256,'CONTROL_INPUT_PIN_CHANGED');return read(p.path);};
  const summary=read(summaryPath);
  assert.equal(summary.status,'CURRENT_FACTORY_ORIGINAL_READINESS15_CONTINUATION7_OF24_ACTUAL_NEGATIVES_PASS');
  assert.equal(fs.existsSync(outDir),false);fs.mkdirSync(outDir);
  const generation=read(path.join(path.dirname(summaryPath),'prepared37','FACTORY-INITIALIZATION-GENERATION-PLAN.json'));
  const source=checked(generation.cleanRaw);
  const rows=summary.rows.map(row=>{
    const raw=checked(row.raw),request=read(path.join(path.dirname(summaryPath),row.mode+'-raw-inputs.json'));
    return {mode:row.mode,sourceReport:structuredClone(source),sourcePin:generation.sourceHost,
      faultReport:structuredClone(raw),faultPin:request.targets[0],portedReport:structuredClone(raw),portedPin:request.targets[1],
      mutationProof:checked(row.guardProof),portsProof:checked(row.portsProof)};
  });
  const results=[];
  const run=(name,fn)=>{fn();results.push({name,status:'PASS'});};
  const clone=mode=>structuredClone(rows.find(r=>r.mode===mode));
  const target=(report,p)=>report.rows.find(r=>r.sha256===p.sha256&&r.path.toLowerCase()===p.path.toLowerCase());
  const methods=(report,p)=>target(report,p).selections.flatMap(s=>s.methods);
  const guard=(report,p,mode)=>methods(report,p).find(m=>m.method.includes(mode==='readiness'?'::get_IsLoaded(':'::LoadAssemblyAndFactory('));
  const reject=(name,mode,change,code)=>run(name,()=>{const value=clone(mode);change(value);assert.throws(()=>validateOriginalFactoryCounterexample(value),{message:'FACTORY_COUNTEREXAMPLE_'+code});});
  for(const mode of ['readiness','continuation'])run(mode+' exact original guard and five ports accepted',()=>assert.equal(validateOriginalFactoryCounterexample(clone(mode)).status,'PASS_EXACT_ORIGINAL_FACTORY_GUARD_PLUS_FIVE_PORTS'));
  reject('readiness one wrong predicate refused','readiness',v=>{guard(v.faultReport,v.faultPin,v.mode).instructions.find(i=>i.opcode==='ldc.i4.0').opcode='ldc.i4.1';},'EXACT_ORIGINAL_GUARD_MUTATION');
  reject('continuation wrong completed-stage literal refused','continuation',v=>{const ins=guard(v.faultReport,v.faultPin,v.mode).instructions;const n=ins.findIndex((i,n)=>i.opcode==='pop'&&ins[n+1]?.opcode==='ldc.i4.0');assert(n>=0);assert.equal(ins[n+1].opcode,'ldc.i4.0');ins[n+1].opcode='ldc.i4.1';},'EXACT_ORIGINAL_GUARD_MUTATION');
  reject('clean source canonical drift refused','readiness',v=>{guard(v.sourceReport,v.sourcePin,v.mode).instructions[0].opcode='nop';},'LOCKED_CLEAN_SOURCE_METHOD');
  reject('second guard mutation refused','readiness',v=>{guard(v.faultReport,v.faultPin,'continuation').instructions[0].opcode='nop';},'NO_OTHER_GUARD_CHANGE');
  reject('sixth ported method change refused','readiness',v=>{guard(v.portedReport,v.portedPin,'continuation').instructions[0].opcode='nop';},'NO_OTHER_PORTED_CHANGE');
  reject('same method name with wrong runtime scope refused','readiness',v=>{const m=methods(v.portedReport,v.portedPin).find(m=>m.method.includes('::ResolveDependency('));const call=m.instructions.find(i=>i.opcode==='callvirt');assert(call.operand.includes('scope=System.Private.CoreLib'));call.operand=call.operand.replace('scope=System.Private.CoreLib','scope=System.Runtime');},'EXACT_SCOPED_PORT_BODY');
  reject('raw target execution flag refused','readiness',v=>{v.faultReport.targetCodeExecuted=true;},'RAW_METADATA_ONLY');
  reject('wrong image hash refused','readiness',v=>{v.faultPin.sha256='0'.repeat(64);},'EXACT_IMAGE');
  reject('missing selected method refused','readiness',v=>{target(v.faultReport,v.faultPin).selections.find(s=>s.methods.length).methods.pop();},'EXACT37_METHOD_SET');
  reject('duplicate full method signature refused','readiness',v=>{const s=target(v.faultReport,v.faultPin).selections.find(s=>s.methods.length);s.methods.push(structuredClone(s.methods[0]));},'DUPLICATE_SIGNATURE');
  reject('extra method in mutation proof refused','readiness',v=>{v.mutationProof.changedMethods.push('System.Void Foreign::Extra()');},'EXACT_ORIGINAL_ONE_METHOD_PROOF');
  reject('extra method in ports proof refused','readiness',v=>{v.portsProof.changedMethods.push('System.Void Foreign::Extra()');},'EXACT_FIVE_PORT_PROOF');
  reject('changed type base refused','readiness',v=>{target(v.portedReport,v.portedPin).selections[0].baseType='Foreign.Base';},'NO_FIELD_OR_BASE_CHANGE');
  reject('wrong assembly identity refused','readiness',v=>{target(v.portedReport,v.portedPin).identity='Foreign, Version=1.0.0.0';},'IDENTITY_OR_INITIALIZER');
  reject('introduced module initializer refused','readiness',v=>{target(v.portedReport,v.portedPin).moduleInitializers.push('Foreign::.cctor');},'IDENTITY_OR_INITIALIZER');
  reject('invalid mutation mode refused','readiness',v=>{v.mode='foreign';},'MODE');
  run('operand token RID changes do not weaken typed operand matching',()=>{const v=clone('continuation');for(const [report,p]of [[v.sourceReport,v.sourcePin],[v.faultReport,v.faultPin],[v.portedReport,v.portedPin]])for(const m of methods(report,p))for(const i of m.instructions)i.operandToken=i.operandToken===null?null:0x0a00ffff;assert.equal(validateOriginalFactoryCounterexample(v).status,'PASS_EXACT_ORIGINAL_FACTORY_GUARD_PLUS_FIVE_PORTS');});
  const result={schemaVersion:1,status:'PASS_FACTORY_COUNTEREXAMPLE_RAW_CONTROLS',total:results.length,passed:results.length,results,
    actualNegativeSummary:pin(summaryPath),test:pin(fileURLToPath(import.meta.url)),validator:pin(path.join(path.dirname(fileURLToPath(import.meta.url)),'factory-counterexample-raw.mjs')),
    usesActualReviewedRawMetadata:true,syntheticMutationsOnlyForValidatorControls:true,producerExecutions:0,targetExecutions:0,notBusinessEvidence:true};
  fs.writeFileSync(path.join(outDir,'SUMMARY.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){
  const result=runFactoryCounterexampleRawControls({summaryPath:process.argv[2],outDir:process.argv[3]});
  console.log(JSON.stringify({status:result.status,total:result.total,passed:result.passed,targetExecutions:0}));
}
