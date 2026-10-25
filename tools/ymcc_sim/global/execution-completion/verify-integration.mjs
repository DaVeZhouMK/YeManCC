import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

/** Run only owned Node fixture processes. Does not start YMCC, Host or hardware. */
export async function verifyIntegration(globalRoot,evidenceRoot){
  const shared=path.resolve(globalRoot),root=path.resolve(evidenceRoot);
  if(fs.existsSync(root))throw Error('FRESH_EVIDENCE_DIRECTORY_REQUIRED');
  const fixture=path.join(root,'fixture-repo'),runDir=path.join(root,'run');
  fs.mkdirSync(fixture,{recursive:true});fs.mkdirSync(runDir);
  fs.writeFileSync(path.join(fixture,'complete.mjs'),"console.log('producer: complete');\n");
  fs.writeFileSync(path.join(fixture,'incomplete.mjs'),"async function main(){await new Promise(()=>{});console.log('producer: complete');}main();\n");
  const runner=await import(pathToFileURL(path.join(shared,'runner.mjs')));
  const audit=await import(pathToFileURL(path.join(shared,'receipt-audit.mjs')));
  const contract={schemaVersion:1,rules:[{line:'producer: complete'}]};
  const adapters=['incomplete','complete'].map(name=>({id:'fixture.'+name,domain:'fixture',title:name,kind:'node',path:name+'.mjs',args:[],timeoutMs:3000,profiles:['full'],scope:'behavioral-mock',evidenceInputs:[],completionContract:contract}));
  runner.validateAdapters(adapters,fixture);
  const results=[];
  for(const adapter of adapters)results.push(await runner.executeAdapter(adapter,{repoRoot:fixture,runDir}));
  const incomplete=results[0],complete=results[1];
  assert.equal(incomplete.exitCode,0,'the negative must be a naturally completed zero exit');
  assert.equal(incomplete.status,'FAIL','missing terminal output must fail');
  assert.equal(complete.status,'PASS');assert.equal(complete.completionProof?.status,'PASS');
  const receipt=path.join(runDir,'result.json');
  const raw={schemaVersion:1,tool:'ymcc_sim-global',mode:'run',runId:path.basename(root),results};
  fs.writeFileSync(receipt,JSON.stringify(raw,null,2));
  const registered=report=>audit.auditRegisteredScope(report,adapters,{profile:'full',tests:['fixture.complete']});
  const current=registered(audit.auditReceipts([receipt],{repoRoot:fixture}));
  assert.equal(current.cases.find(row=>row.id==='fixture.complete').status,'PASS');
  const legacy=structuredClone(complete);delete legacy.completionProof;
  const legacyPath=path.join(runDir,'legacy-without-proof.json');
  fs.writeFileSync(legacyPath,JSON.stringify({...raw,runId:'copied-negative-without-proof',results:[legacy]}));
  const legacyAudit=registered(audit.auditReceipts([legacyPath],{repoRoot:fixture}));
  assert.equal(legacyAudit.cases[0].status,'STALE_REGISTRATION');
  assert.equal(runner.evaluateResult({exitCode:0,completionProof:null}), 'FAIL');
  const nullPath=path.join(runDir,'null-proof-negative.json');
  fs.writeFileSync(nullPath,JSON.stringify({...raw,runId:'null-proof-negative',results:[{...complete,completionProof:null}]}));
  const nullAudit=audit.auditReceipts([nullPath],{repoRoot:fixture});
  assert.equal(nullAudit.cases[0].status,'FAIL','a malformed proof must not crash the complete audit');
  const changed=audit.auditRegisteredScope(audit.auditReceipts([receipt],{repoRoot:fixture}),adapters.map(row=>({...row,completionContract:{schemaVersion:1,rules:[{line:'new terminal line'}]}})),{profile:'full',tests:['fixture.complete']});
  assert.equal(changed.cases.find(row=>row.id==='fixture.complete').status,'STALE_REGISTRATION');
  const stdout=path.join(runDir,'cases','fixture.complete','stdout.log'),original=fs.readFileSync(stdout);
  let changedLog;
  try{fs.appendFileSync(stdout,'changed after execution\n');changedLog=audit.auditReceipts([receipt],{repoRoot:fixture});}
  finally{fs.writeFileSync(stdout,original);}
  assert.equal(changedLog.cases.find(row=>row.id==='fixture.complete').status,'STALE_OR_INVALID_EVIDENCE');
  assert.equal(registered(audit.auditReceipts([receipt],{repoRoot:fixture})).cases.find(row=>row.id==='fixture.complete').status,'PASS');
  const malformed={...adapters[1],completionContract:{schemaVersion:1,rules:[{line:'producer: complete',stream:null}]}};
  assert.throws(()=>runner.validateAdapters([malformed],fixture),/COMPLETION_CONTRACT_INVALID/);
  assert.throws(()=>audit.auditRegisteredScope({status:'CURRENT_RECEIPTS_VALID',cases:[],diagnostics:[]},[malformed]),/COMPLETION_CONTRACT_INVALID/);
  const proof={schemaVersion:1,generatedAt:new Date().toISOString(),status:'PASS',scope:'actual runner/auditor on owned inert Node fixture processes',zeroExitIncomplete:incomplete.status,complete:complete.status,legacy:legacyAudit.cases[0].status,changedContract:changed.cases.find(row=>row.id==='fixture.complete').status,changedLog:changedLog.cases.find(row=>row.id==='fixture.complete').status,malformedContractsRejectedByBothEntries:true,nullProofRejectedWithoutAuditAbort:true,physicalHardwareValidated:false,globalRoot:shared,rawReceipt:receipt};
  fs.writeFileSync(path.join(root,'integration-proof.json'),JSON.stringify(proof,null,2)+'\n');
  return proof;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const args=process.argv.slice(2);
  if(args.length!==4||args[0]!=='--global-root'||args[2]!=='--evidence-root')throw Error('Use --global-root <global directory> --evidence-root <new owned scratch directory>');
  verifyIntegration(args[1],args[3]).then(proof=>console.log(JSON.stringify(proof)),error=>{console.error(error);process.exitCode=1;});
}
