import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {REPO,inputs} from './inputs.mjs';
import {ID,NODE_BUDGET_MS,PRODUCERS,BOUNDARY,REMAINING_GAPS,EXCLUDED_FAULT,need,same,requireNatural,validateToolSet,validateBundle} from './contracts.mjs';
import {key,freshRoot,writeNew,snapshot,inventory,pointer,validateSourceLocks,producerEnvironment,runProducer,validateExecution,validatePositiveProducerOutput,validateFaultExecutions} from './evidence.mjs';
function options(argv){
 const allowed=new Set(['evidence-dir','run-id','powershell-path','powershell-pid']),opt={};
 need(argv.length===8,'EXACT_ARGUMENTS');
 for(let i=0;i<argv.length;i+=2){const name=argv[i].slice(2);need(argv[i].startsWith('--')&&allowed.has(name)&&!Object.hasOwn(opt,name)&&typeof argv[i+1]==='string'&&argv[i+1].length>0,'ARGUMENT');opt[name]=argv[i+1];}
 need(/^[a-f0-9]{32}$/.test(opt['run-id'])&&Number.isSafeInteger(Number(opt['powershell-pid']))&&Number(opt['powershell-pid'])>0,'WRAPPER_ATTEMPT');
 return opt;
}
export async function main(argv=process.argv.slice(2)){
 const opt=options(argv),root=freshRoot(opt['evidence-dir'],REPO),runId=opt['run-id'];
 const started=Date.now(),startedAt=new Date(started).toISOString(),deadline=started+NODE_BUDGET_MS;
 writeNew(path.join(root,'attempt.json'),{schemaVersion:1,id:ID,runId,evidenceRoot:root,nodePid:process.pid,powershellPid:Number(opt['powershell-pid']),startedAt});
 const result={schemaVersion:1,id:ID,status:'FAIL',passed:false,runId,evidenceRoot:root,startedAt,
  node:{pid:process.pid,path:process.execPath},powershell:{pid:Number(opt['powershell-pid']),path:opt['powershell-path']},
  positive:null,faultSummary:null,faults:[],processes:[],excludedFault:EXCLUDED_FAULT,boundary:BOUNDARY,remainingGaps:[...REMAINING_GAPS],
  inputsUnchanged:false,toolsUnchanged:false,sourceHashes:[]};
 let spec=null,sourcesBefore=null,toolsBefore=null;
 try{
  spec=inputs(REPO);
  sourcesBefore=snapshot(spec.sources.map(name=>path.join(REPO,name)));toolsBefore=snapshot(spec.tools.files.map(row=>row.path));
  writeNew(path.join(root,'inputs-before.json'),sourcesBefore);writeNew(path.join(root,'tools-before.json'),toolsBefore);
  validateToolSet(spec.tools,toolsBefore);validateSourceLocks(REPO,sourcesBefore);
  need(key(process.execPath)===key(spec.tools.nodeExecutable)&&key(opt['powershell-path'])===key(spec.powershellExecutable),'EXECUTABLE_PATH');
  need([process.execPath,opt['powershell-path'],spec.tools.cmdExecutable,spec.tools.vcvars64].every(file=>toolsBefore.some(row=>key(row.path)===key(file))),'EXECUTABLE_NOT_IN_APPROVED_582');
  const env=producerEnvironment(spec);
  result.declarations={sources:spec.sources,toolFiles:spec.tools.files,toolManifestLayers:spec.manifests,toolCount:582,toolRole:'predeclared-only; actual links/loads not observed',producerPins:PRODUCERS};
  const common=['--repo',REPO,'--reference',REPO];
  const positiveCall=await runProducer({root,label:'positive-producer',exe:spec.tools.nodeExecutable,
   args:[path.join(REPO,PRODUCERS[0].path),...common,'--before',path.join(REPO,'tools/fixtures/hc_msi_rear_consumers/before'),'--out',path.join(root,'positive'),'--configurations','DefaultRelease'],cwd:REPO,env,deadline});
  result.processes.push(positiveCall);requireNatural(positiveCall);
  result.positive=validateExecution({root,dir:path.join(root,'positive'),repo:REPO,sourceRoot:REPO,sourceRows:sourcesBefore,toolRows:toolsBefore});
  validatePositiveProducerOutput(positiveCall,result.positive);
  need(same(sourcesBefore,snapshot(sourcesBefore.map(row=>row.path)))&&same(toolsBefore,snapshot(toolsBefore.map(row=>row.path))),'IDENTITY_CHANGED_BETWEEN_PRODUCERS');
  need(Date.now()<deadline,'OUTER_BUDGET_EXHAUSTED');
  const faultsCall=await runProducer({root,label:'faults-producer',exe:spec.tools.nodeExecutable,
   args:[path.join(REPO,PRODUCERS[1].path),...common,'--out',path.join(root,'faults')],cwd:REPO,env,deadline});
  result.processes.push(faultsCall);requireNatural(faultsCall);
  Object.assign(result,validateFaultExecutions({root,repo:REPO,sourceRows:sourcesBefore,toolRows:toolsBefore,call:faultsCall}));
 }catch(error){result.error={code:String(error.code??'CONTRACT_OR_EXECUTION_FAILED'),message:String(error.message),stack:String(error.stack)};}
 try{
  if(sourcesBefore){const after=snapshot(sourcesBefore.map(row=>row.path));writeNew(path.join(root,'inputs-after.json'),after);result.inputsUnchanged=same(sourcesBefore,after)&&after.every(row=>row.status==='OK');}
  if(toolsBefore){const after=snapshot(toolsBefore.map(row=>row.path));writeNew(path.join(root,'tools-after.json'),after);result.toolsUnchanged=same(toolsBefore,after)&&after.every(row=>row.status==='OK');if(spec)validateToolSet(spec.tools,after);}
  result.originalArtifacts=[...inventory(root,path.join(root,'positive')),...inventory(root,path.join(root,'faults'))];
  for(const name of ['attempt.json','inputs-before.json','inputs-after.json','tools-before.json','tools-after.json','positive-producer-terminal.json','faults-producer-terminal.json']){
   if(fs.existsSync(path.join(root,name)))result.sourceHashes.push(pointer(root,path.join(root,name)));
  }
  for(const row of result.originalArtifacts)result.sourceHashes.push(row);
  for(const call of result.processes)result.sourceHashes.push(call.stdout,call.stderr);
  need(!result.error,'EXECUTION_OR_VALIDATION_FAILED');
  validateBundle(result,{runId,evidenceRoot:root});
  need(Date.now()<deadline,'OUTER_BUDGET_EXHAUSTED');
  result.status='PASS';result.passed=true;
 }catch(error){result.finalizationError={message:String(error.message),stack:String(error.stack)};}
 result.inputConsumerStateReceipt={input:'frozen originals + current exact MSI model/endpoint/firmware/wire source and pinned producers',
  consumer:'original extracted msiClawEnsureM12Remap/msiClawPrepareForVirtualStart, total catalog and default-disabled native boundary, all endpoint APIs memory fakes',
  state:result.passed?'positive 78/67093 and all five exact compiled semantic faults accepted':'failure retained; no retry or relaxed source identity',
  receipt:'unaltered positive/fault summary/five child result pointers, complete raw trees and streams, hashes and producer terminal records',
  nativeExitOrigin:'original producer result; per-fault child natural0 proven by pinned fault producer rejected predicate; not independently observed native PIDs'};
 result.finishedAt=new Date().toISOString();result.durationMs=Date.now()-started;
 writeNew(path.join(root,'result.json'),result);
 console.log(JSON.stringify({id:ID,runId,status:result.status,result:path.join(root,'result.json')}));
 return result.passed?0:1;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{process.exitCode=await main();}catch(error){console.error(error.stack??String(error));process.exitCode=2;}
}
