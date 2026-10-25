import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
export const digest=v=>createHash('sha256').update(typeof v==='string'||Buffer.isBuffer(v)?v:JSON.stringify(v)).digest('hex');
export const need=(ok,code,detail)=>{if(!ok)throw Object.assign(new Error(code),{code,detail});};
export const filePin=p=>({path:p,sha256:digest(fs.readFileSync(p))});
export const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export function checkedPin(p){need(p&&path.isAbsolute(p.path??'')&&/^[0-9a-f]{64}$/.test(p.sha256??''),'CASE_PIN_SHAPE');need(fs.existsSync(p.path)&&fs.statSync(p.path).isFile(),'CASE_PIN_MISSING',p);need(path.resolve(fs.realpathSync(p.path)).toUpperCase()===path.resolve(p.path).toUpperCase()&&!fs.lstatSync(p.path).isSymbolicLink(),'CASE_PIN_REDIRECTED',p);need(filePin(p.path).sha256===p.sha256,'CASE_PIN_CHANGED',p);return p;}
const files=['Harness.cs','StrictWriteCases.cs','GenericEcWriteCases.cs','GenericCloseCases.cs','GenericOwnershipCases.cs','MixedEventsCases.cs','EventAdmissionCases.cs','InitializationCases.cs'];
// Parse only case-label expressions/constant loop domains, never C# consumers.
// The caller pins all eight compiler-source files. Unknown expressions refuse.
export function deriveDefaultCases(sources){
 const out=[],loopProofs=[];const writeModels=sources['StrictWriteCases.cs'].match(/WriteModels\s*=\s*\[([^\]]+)\]/)?.[1];need(writeModels,'CASE_WRITE_MODELS_SOURCE');
 const flags={Initializing:1,Initialized:2,Halting:4,Halted:8,Busy:16};
 function values(text){if(text==='WriteModels')text=writeModels;const types=[...text.matchAll(/typeof\(([^)]+)\)/g)].map(m=>({Name:m[1].split('.').at(-1)}));if(types.length)return types;const strings=[...text.matchAll(/"([^"\\]*)"/g)].map(m=>m[1]);if(strings.length)return strings;const inner=text.replace(/^new\s*\[\]\s*\{/,'').replace(/\}$/,'').trim();if(/^(false|true)(\s*,\s*(false|true))*$/.test(inner))return inner.split(',').map(s=>s.trim()==='true');if(inner.includes('ManagerStatus.'))return inner.split(',').map(s=>s.trim().split('|').map(v=>v.trim().replace('ManagerStatus.','')).sort((a,b)=>flags[a]-flags[b]).join(', '));throw Error('CASE_UNKNOWN_LOOP_DOMAIN '+text);}
 for(const file of files){const source=sources[file];need(typeof source==='string','CASE_SOURCE_REQUIRED',file);const lines=source.split('\n');const stop=file==='MixedEventsCases.cs'?lines.findIndex(s=>s.includes('static int RunMixedReproduction')):lines.length;const start=file==='Harness.cs'?lines.findIndex(s=>s.includes('foreach (var type in new[]')):0;need(start>=0&&stop>start,'CASE_DEFAULT_SOURCE_RANGE',file);
  for(let ix=start;ix<stop;ix++){const match=lines[ix].match(/(?<![.\w])Run\((.+?),\s*\(\)\s*=>/);if(!match)continue;const expression=match[1].trim(),before=lines.slice(0,ix+1).join('\n'),names=[...new Set([...expression.replace(/"(?:[^"\\]|\\.)*"/g,'').matchAll(/\b(type|deviceType|mode|fault|loss|foreign|mask|ready|manager|status|process|kind|prime|thrown|replace|trigger|point|diagnosticThrows)\b/g)].map(m=>m[1]))];let bindings=[{}];const proofs=[];
   for(const name of names){let domain,proof; if(name==='mask'){const m=[...before.matchAll(/for\s*\(\s*var\s+mask\s*=\s*(\d+)\s*;\s*mask\s*<\s*(\d+)\s*;\s*mask\+\+\s*\)/g)].at(-1);need(m,'CASE_MASK_LOOP_REQUIRED');domain=Array.from({length:Number(m[2])-Number(m[1])},(_,i)=>Number(m[1])+i);proof=m[0];}else{const m=[...before.matchAll(new RegExp('foreach\\s*\\(\\s*var\\s+'+name+'\\s+in\\s+(new\\s*\\[\\]\\s*\\{[^}]*\\}|WriteModels)\\s*\\)','g'))].at(-1);need(m,'CASE_LOOP_BINDING_REQUIRED',{file,line:ix+1,name});domain=values(m[1]);proof=m[0];}
    if(domain.every(v=>typeof v==='boolean')&&!new RegExp('\\b'+name+'\\s*\\?').test(expression))domain=domain.map(v=>v?'True':'False');proofs.push({name,domain,source:proof});bindings=bindings.flatMap(b=>domain.map(value=>({...b,[name]:value})));need(bindings.length<=32,'CASE_LABEL_EXPANSION_BUDGET');
   }
   // Expression surface is only string literals, known loop variables, .Name,
   // concatenation and fixed ternaries. No method invocation is admitted.
   const surface=expression.replace(/"(?:[^"\\]|\\.)*"/g,'""').replace(/\.Name/g,'');need(/^[\w\s"+:?()]+$/.test(surface),'CASE_LABEL_EXPRESSION_UNSUPPORTED',expression);
   for(const scope of bindings){const context=vm.createContext(Object.assign(Object.create(null),scope),{codeGeneration:{strings:false,wasm:false}}),id=new vm.Script('('+expression+')').runInContext(context,{timeout:100});need(typeof id==='string'&&id.length>0&&id.length<256,'CASE_LABEL_NOT_STRING');out.push({id,file,line:ix+1,expression,bindings:scope});}
   if(proofs.length)loopProofs.push({file,line:ix+1,expression,domains:proofs});
  }
 }
 need(new Set(out.map(r=>r.id)).size===out.length,'CASE_DUPLICATE_SOURCE_LABEL');return {cases:out,loopProofs};
}
export function auditDefaultCaseContract({sources,runSource,observed,historicalJoined,parentInitialFailure}){
 const derived=deriveDefaultCases(sources),expected=derived.cases.map(c=>c.id),actual=observed.cases.map(c=>c.id),old=historicalJoined.cases.map(c=>c.id),sort=a=>JSON.stringify([...a].sort());
 need(sort(expected)===sort(actual)&&new Set(actual).size===actual.length,'CASE_SOURCE_LIST_DIFFERS',{sourceCount:expected.length,observedCount:actual.length});
 need(runSource.includes("behavior.status==='PASS'&&behavior.cases.length>=69&&behavior.checks>=280"),'CASE_ORIGINAL_FINAL_GATE_CHANGED');
 const groups=[['oneX-lifecycle-and-strict-original69',r=>['Harness.cs','StrictWriteCases.cs'].includes(r.file)],['generic-ec',r=>r.file==='GenericEcWriteCases.cs'],['generic-close',r=>r.file==='GenericCloseCases.cs'],['generic-owner',r=>r.file==='GenericOwnershipCases.cs'],['mixed-events',r=>r.file==='MixedEventsCases.cs'],['events-admission',r=>r.file==='EventAdmissionCases.cs'],['initialization',r=>r.file==='InitializationCases.cs']].map(([name,test])=>({name,ids:derived.cases.filter(test).map(r=>r.id)}));
 const runConsumerSets={};for(const name of ['initializationIds','mixedIds','flagIds','genericIds','closeIds','ownerIds','eventIds']){const expression=runSource.match(new RegExp('const '+name+'=([\\s\\S]*?);'))?.[1];need(expression,'CASE_RUN_CONSUMER_ARRAY_MISSING',name);const eventExpression=runSource.match(/const eventTypes=([^;]+);/)?.[1];const context=vm.createContext(Object.create(null),{codeGeneration:{strings:false,wasm:false}});if(eventExpression)new vm.Script('var eventTypes='+eventExpression).runInContext(context,{timeout:100});new vm.Script('var '+name+'=('+expression+');').runInContext(context,{timeout:100});if(name==='eventIds'){const push=runSource.match(/eventIds\.push\(([^;]+)\);/)?.[0];need(push,'CASE_EVENT_PUSH_REQUIRED');new vm.Script(push).runInContext(context,{timeout:100});}const values=Array.from(new vm.Script(name).runInContext(context,{timeout:100}));need(values.every(id=>expected.includes(id)),'CASE_RUN_CONSUMER_ARRAY_UNKNOWN',name);runConsumerSets[name]={ids:values,count:values.length};}
 const overlap=actual.filter(id=>old.includes(id));need(historicalJoined.cases.length===123&&historicalJoined.cases.filter(c=>c.status==='FAIL').length===119,'CASE_HISTORICAL_JOINED_RECEIPT_CHANGED');
 return {schemaVersion:1,status:'SOURCE_CASE_CONTRACT_MATCHES_PARENT_RECEIPT',expectedBasis:'Pinned C# Run label expressions expanded over their exact constant source loops, before comparison with runtime data; not expected := observed',sourceDerivedCaseCount:expected.length,sourceDerivedIdsSha256:digest([...expected].sort()),sourceDerivedCases:derived.cases,loopProofs:derived.loopProofs,observedCaseIdsInOriginalOrder:actual,
  historicalJoined:{cases:123,failed:119,ids:old,failedIds:historicalJoined.cases.filter(c=>c.status==='FAIL').map(c=>c.id),overlapWithDefault:overlap,defaultDoesNotCoverJoined:old.filter(id=>!actual.includes(id)),meaning:'Separate later JoinedFactoryProbe positive consumer; 119 is the preserved observed FAIL count, not 119 expected-negative default cases'},
  runConsumerSets,originalDefaultGate:{minimumCases:69,minimumChecks:280,exact192AssertionAdded:false,exact123AssertionPresent:false},groups:groups.map(g=>({...g,count:g.ids.length})),
  arithmetic:{defaultOriginalOneX:groups[0].ids.length,otherDefaultConsumers:derived.cases.length-groups[0].ids.length,coincidentalAdded123IsNotJoined123:true},parentInitialFailurePreserved:{status:parentInitialFailure.status,checks:parentInitialFailure.checks,rewritten:false},
  targetExecutedByThisAudit:false,fullParentPassed:false};
}
