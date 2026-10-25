import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {checkedBranchPin,reviewOldWriteBranchDocuments} from './old-write-branch-review.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export function runOldWriteBranchTests({inputsPin,evidenceDir}){
  checkedBranchPin(inputsPin);const input=read(inputsPin.path),prepared=read(checkedBranchPin(input.preparedPlan).path),sourcePins=input.sourcePins;
  const documents={basis:read(fileURLToPath(new URL('./old-write-branch-basis.json',import.meta.url))),reports:input.rawReports.map(p=>read(checkedBranchPin(p).path)),sources:Object.fromEntries(sourcePins.map(p=>[path.basename(p.path),fs.readFileSync(checkedBranchPin(p).path,'utf8')])),plan:prepared,policy:read(checkedBranchPin(prepared.policy).path),priorAnalysis:read(checkedBranchPin(input.priorAnalysis).path),priorNextReview:read(checkedBranchPin(input.priorNextReview).path)};
  assert(path.isAbsolute(evidenceDir)&&!fs.existsSync(evidenceDir));fs.mkdirSync(evidenceDir);const tests=[];
  const test=(id,fn)=>{fn();tests.push({id,status:'PASS',producerExecuted:false});};
  const modifyBody=(d,fragment,fn)=>{let count=0;for(const report of d.reports)for(const image of report.rows)for(const t of image.selections)for(const m of t.methods)if(m.method.includes(fragment)){fn(m);count++;}assert(count>0);};
  const refuse=(id,mutate,code)=>test(id,()=>{const d=structuredClone(documents);mutate(d);assert.throws(()=>reviewOldWriteBranchDocuments(d),e=>e.code===code);});
  let positive;
  test('exact-static-19-closure-and-eager-manager-path-positive',()=>{positive=reviewOldWriteBranchDocuments(documents);assert.equal(positive.staticNoDeviceIoConclusion,true);assert.equal(positive.nineteenCount,19);assert.equal(positive.executionApprovalGranted,false);assert.equal(positive.producerExecuted,false);assert.equal(positive.facts.find(f=>f.id==='eager-diagnostic').factoryEvaluated,true);assert.equal(positive.facts.find(f=>f.id==='close-manager-observation').actuallyExecuted,true);assert.equal(positive.untouchedConservativeMsiAsusEdges,37);assert.equal(positive.actualBranchLocalMissing.length,0);});
  refuse('original6-9-45s-gate-drift-refused',d=>d.plan.gate.goldenWrites=8,'OLD_WRITE_BRANCH_GATE_CHANGED');
  refuse('different-actual-device-newobj-refused',d=>modifyBody(d,'OldWriteProbe::Run(',m=>{for(const i of m.calls)if(i.opcode==='newobj'&&i.target.includes('OneXPlayerX1AMD::.ctor'))i.target=i.target.replace('OneXPlayerX1AMD','OneXPlayerX1Intel');}),'OLD_WRITE_BRANCH_DIFFERENT_ACTUAL_FACTORY');
  refuse('non-null-lazy-diagnostic-premise-refused',d=>modifyBody(d,'RealHcBackend::.ctor(',m=>m.instructions.push({offset:9999,opcode:'stfld',operand:'System.Object YeManFanHost.RealHcBackend::<DiagnosticLazy>k__BackingField [scope=YeManFanHost.dll]'})),'OLD_WRITE_BRANCH_UNEXPECTED_RESOURCE_ASSIGNMENT');
  refuse('monitor-timer-initialization-refused',d=>modifyBody(d,'HWiNFOTemperatureMonitor::.ctor(',m=>m.instructions.push({offset:9999,opcode:'stfld',operand:'System.Threading.Timer YeManFanHost.HWiNFOTemperatureMonitor::timer [scope=YeManFanHost.dll]'})),'OLD_WRITE_BRANCH_UNEXPECTED_RESOURCE_ASSIGNMENT');
  refuse('same-class-name-different-route-assignment-refused',d=>modifyBody(d,'RealHcBackend::BuildFanRoutes(',m=>{const row=m.instructions.find(i=>i.offset===23);row.opcode='ldc.i4.s';row.operand='11';}),'OLD_WRITE_BRANCH_IL_GUARD_CHANGED');
  refuse('manager-reflection-getter-with-extra-effect-refused',d=>modifyBody(d,'ManagerFactory::get_Managers(',m=>m.calls.push({offset:99,opcode:'call',target:'System.Void Foreign.Device::Open() [scope=Foreign]',token:167777777})),'OLD_WRITE_BRANCH_MANAGER_GETTER_NOT_INERT');
  refuse('new-candidate-close-API-refused',d=>{for(const report of d.reports)for(const image of report.rows)if(image.identity.startsWith('HandheldCompanion,'))for(const t of image.selections)if(t.type==='HandheldCompanion.Devices.OneXPlayerX1AMD')t.absentRequestedMethods=t.absentRequestedMethods.filter(n=>n!=='TryWaitCloseCleanupCompleted');},'OLD_WRITE_BRANCH_UNREVIEWED_CLOSE_API');
  refuse('unknown-Default-path-not-directory-approved',d=>d.policy.contexts[0].managed[6].path=path.join(path.dirname(d.policy.contexts[0].managed[6].path),'Unapproved.dll'),'OLD_WRITE_BRANCH_DEFAULT_POLICY_CHANGED');
  refuse('missing-required-nineteen-body-refused',d=>{const missing=d.basis.requiredNineteen[0].token;for(const report of d.reports)for(const image of report.rows)if(image.sha256===d.basis.images.host)for(const t of image.selections)t.methods=t.methods.filter(m=>m.token!==missing);},'OLD_WRITE_BRANCH_UNREVIEWED_BODY');
  const result={schemaVersion:1,status:'PASS_10_OLD_WRITE_BRANCH_REVIEW_GATES',checkCount:tests.length,tests,positive:{status:positive.status,nineteenCount:19,actualBranchLocalMissing:0,staticNoDeviceIoConclusion:true,producerExecuted:false,executionApprovalGranted:false},originalCasesExecuted:0,originalGoldensExecuted:0,targetExecutions:0};
  fs.writeFileSync(path.join(evidenceDir,'FINITE-GATES.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&path.resolve(process.argv[1]).toLowerCase()===fileURLToPath(import.meta.url).toLowerCase()){
  try{const args=process.argv.slice(2);assert.equal(args.length,6);assert.deepEqual(args.filter((_,i)=>i%2===0),['--inputs','--sha256','--evidence-dir']);console.log(JSON.stringify(runOldWriteBranchTests({inputsPin:{path:args[1],sha256:args[3]},evidenceDir:args[5]})));}catch(e){console.error(e.stack);process.exitCode=2;}
}
