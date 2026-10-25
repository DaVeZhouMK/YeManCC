import fs from 'node:fs';
import path from 'node:path';
import {need,key,sha256,hashFile,readJson,writeNew,owned,verifySources,naturalTerminal,executeStage,compilerEnvironment,checkActualCompilerInputs,inspectPeImports} from '../helper.mjs';
import {prepareMainReturn} from './prepare.mjs';

export function validateMainNative({native,terminal,plan,variant,stdout,stderr}){
 const fault=variant==='original'?null:plan.sourceFaults.find(f=>f.id===variant);need(variant==='original'||fault,'MAIN_RETURN_UNDECLARED_VARIANT');
 const expected=fault?fault.expectedNaturalExit:plan.positiveNaturalExit;
 need(naturalTerminal(terminal,expected)&&native.naturalExit===expected,'MAIN_RETURN_NATIVE_EXIT_SIGNAL_TIMEOUT_OR_OUTPUT');
 need(native.status===(fault?'ASSERTION_FAILED':'PASS')&&native.actualMainReturnAndAdmission===true&&native.realHostIntentExecuted===false&&native.postCreateSeedOnly===true,'MAIN_RETURN_NATIVE_SCOPE_OR_STATUS');
 need(native.cases?.length===(fault?1:12),'MAIN_RETURN_NATIVE_CASE_COUNT');
 const resourceKeys=['queuedMessages','regions','windows','wireAllocations'];
 const cases=native.cases.map((row,index)=>{
  need(row.id===(fault?fault.case:plan.positiveCases[index].id)&&row.status===(fault?'ASSERTION_FAILED':'PASS'),'MAIN_RETURN_FIXED_CASE_OUTCOME');
  if(fault)need(row.assertion===fault.expectedAssertion,'MAIN_RETURN_WRONG_SEMANTIC_ASSERTION');
  const after=row.afterCleanup;need(after&&JSON.stringify(Object.keys(after.resources??{}).sort())===JSON.stringify(resourceKeys)&&Object.values(after.resources).every(n=>n===0)&&after.queuedActions===0&&after.active===false&&after.mainOwned===false,'MAIN_RETURN_ENDPOINTS_NOT_RETURNED');
  need(Array.isArray(after.states)&&after.states.every(s=>s.closed===true&&s.ownerNull===true&&s.window===0&&s.inputWindow===0&&s.viewNull===true&&s.controllerNull===true),'MAIN_RETURN_NATIVE_STATE_NOT_RETURNED');
  need(row.before&&row.beforeCleanup&&Array.isArray(row.trace)&&row.trace.length>0&&!row.cleanupError&&!row.error,'MAIN_RETURN_TRACE_OR_CLEANUP_INCOMPLETE');
  return {id:row.id,status:row.status,assertion:row.assertion??null,resources:after.resources,queuedActions:after.queuedActions};
 });
 need(stderr===''&&stdout.replace(/\r\n/g,'\n')===cases.map(c=>c.id+':'+c.status+'\n').join(''),'MAIN_RETURN_OUTPUT_INCOMPLETE_OR_UNEXPECTED');return cases;
}
export function requireThisMainOriginal(receipt,{runId,stageRoot,sourceFingerprint}){
 need(receipt?.schemaVersion===1&&receipt.scopeVersion===2&&receipt.stage==='main-return'&&receipt.status==='PASS'&&receipt.variant==='original'&&receipt.runId===runId&&key(receipt.stageRoot)===key(stageRoot)&&key(receipt.directory)===key(path.join(stageRoot,'runs','original'))&&receipt.sourceFingerprint===sourceFingerprint&&receipt.cases?.length===12&&receipt.cases.every(c=>c.status==='PASS')&&receipt.nativeNaturalExit===0,'MAIN_RETURN_FAULT_REQUIRES_THIS_STAGE_THIS_RUN_ORIGINAL12');return true;
}
export async function runMainReturnStage({parentRoot,sourceRows,tools,runId}){
 const root=owned(parentRoot,path.join(parentRoot,'main-return'));need(!fs.existsSync(root),'MAIN_RETURN_STAGE_ROOT_MUST_BE_FRESH');fs.mkdirSync(root);
 const source=relative=>{const row=sourceRows.find(r=>r.path===relative);need(row&&hashFile(row.saved)===row.sha256,'MAIN_RETURN_REQUIRED_SNAPSHOT_MISSING');return row.saved;};
 const module='tools/ymcc_sim/global/radial-native-consumer/main-return/';
 let error=null,prepared=null,groups=[],contract=null,plan=null;
 try{
  contract=readJson(source(module+'contract.json'));plan=readJson(source(module+'plan.json'));
  need(plan.schemaVersion===1&&plan.scopeVersion===2&&plan.positiveIsCurrentPatchedProduct===true&&plan.positiveNaturalExit===0&&plan.positiveCases?.length===12&&plan.sourceFaults?.length===2&&plan.sourceFaults.every(f=>f.expectedNaturalExit===23),'MAIN_RETURN_PLAN_SCOPE');
  need(plan.sourceFaults.map(f=>f.id).join(',')==='neutral-and-to-or,queued-radial-gate-removed','MAIN_RETURN_FIXED_FAULT_IDS');
  verifySources(sourceRows);prepared=prepareMainReturn({evidenceRoot:root,sourceRows,contract});
  const sourceFingerprint=sha256(JSON.stringify(contract.productSources)),ownedInputs=[...prepared.files,...sourceRows.map(r=>({path:r.saved,sha256:r.sha256}))],env=compilerEnvironment(tools);
  for(const variant of ['original',...plan.sourceFaults.map(f=>f.id)]){
   if(variant!=='original'){
    const original=groups.find(g=>g.variant==='original');requireThisMainOriginal(original,{runId,stageRoot:root,sourceFingerprint});
    need(key(original.receipt.path)===key(path.join(root,'runs/original/receipt.json'))&&hashFile(original.receipt.path)===original.receipt.sha256,'MAIN_RETURN_ORIGINAL_RECEIPT_CHANGED_OR_OUTSIDE_STAGE');
    requireThisMainOriginal(readJson(original.receipt.path),{runId,stageRoot:root,sourceFingerprint});
   }
   verifySources(sourceRows);for(const row of prepared.files)need(hashFile(row.path)===row.sha256,'MAIN_RETURN_GENERATED_INPUT_CHANGED');
   const directory=owned(root,path.join(root,'runs',variant));fs.mkdirSync(directory,{recursive:true});
   const exe=path.join(directory,'main-return.exe'),dependency=path.join(directory,'source-dependencies.json');
   const args=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/WX','/MT','/O1','/X',...tools.includeDirs.map(d=>'/I'+d),'/I'+prepared.fixture,'/I'+prepared.jsonInclude,'/sourceDependencies',dependency,'/Fo'+path.join(directory,'main-return.obj'),'/Fe'+exe,path.join(prepared.fixture,'entry-'+variant+'.cpp'),'/link','/verbose:lib','/incremental:no','/subsystem:console'];
   const compiler=await executeStage({root:directory,exe:tools.executables.cl.path,args,label:'compiler',env,timeoutMs:60000});
   need(naturalTerminal(compiler,0),'MAIN_RETURN_COMPILE_FAILURE_IS_NOT_SEMANTIC_FAULT:'+variant);need(fs.existsSync(exe)&&fs.existsSync(dependency),'MAIN_RETURN_COMPILER_OUTPUT_MISSING');
   const closure=checkActualCompilerInputs({dependencyFile:dependency,compilerStdout:fs.readFileSync(compiler.stdout.path,'utf8'),tools,ownedInputs});writeNew(path.join(directory,'actual-tool-closure.json'),closure);
   const binary={path:exe,sha256:hashFile(exe),importedDlls:inspectPeImports(exe)},fault=plan.sourceFaults.find(f=>f.id===variant);
   const terminal=await executeStage({root:directory,exe,args:fault?[fault.case]:[],label:'native',env,timeoutMs:15000});
   need(naturalTerminal(terminal,fault?23:0),'MAIN_RETURN_NATIVE_NOT_EXPECTED_NATURAL_EXIT:'+variant);
   const rawFile=path.join(directory,'native-result.json');need(fs.existsSync(rawFile),'MAIN_RETURN_NATIVE_RESULT_MISSING');
   const native=readJson(rawFile),cases=validateMainNative({native,terminal,plan,variant,stdout:fs.readFileSync(terminal.stdout.path,'utf8'),stderr:fs.readFileSync(terminal.stderr.path,'utf8')});
   verifySources(sourceRows);for(const row of prepared.files)need(hashFile(row.path)===row.sha256,'MAIN_RETURN_GENERATED_INPUT_CHANGED');
   const receipt={schemaVersion:1,scopeVersion:2,stage:'main-return',status:'PASS',variant,runId,stageRoot:root,directory,sourceFingerprint,cases,compilerNaturalExit:compiler.naturalExit,nativeNaturalExit:terminal.naturalExit,compiler:{path:path.join(directory,'compiler.terminal.json'),sha256:hashFile(path.join(directory,'compiler.terminal.json'))},terminal:{path:path.join(directory,'native.terminal.json'),sha256:hashFile(path.join(directory,'native.terminal.json'))},rawResult:{path:rawFile,sha256:hashFile(rawFile)},binary,actualCompilerInputs:closure.includes.length,actualLinkLibraries:closure.libraries.length,deliberateSourceFault:!!fault,compileFailureCountsAsNegative:false};
   const receiptPath=path.join(directory,'receipt.json');writeNew(receiptPath,receipt);groups.push({...receipt,receipt:{path:receiptPath,sha256:hashFile(receiptPath)}});
   console.log(JSON.stringify({stage:'main-return:'+variant,status:'PASS',compilerPid:compiler.pid,nativePid:terminal.pid,naturalExit:terminal.naturalExit,cases:cases.length}));
  }
  need(groups.length===3,'MAIN_RETURN_ALL_THREE_GROUPS_MANDATORY');verifySources(sourceRows);
 }catch(caught){error={message:caught.message,stack:caught.stack};}
 const passed=!error&&groups.length===3,result={schemaVersion:1,scopeVersion:2,stage:'main-return',status:passed?'PASS':'FAIL',passed,runId,groups,error,extraction:prepared?{path:prepared.manifestFile,sha256:prepared.manifestSha256}:null,sourceHashes:[...sourceRows.map(({path,sha256})=>({path,sha256})),...(prepared?.files??[]),...groups.flatMap(g=>[g.compiler,g.terminal,g.rawResult,g.binary,g.receipt])],productionDefinitions:30,exactDeclarations:54,actualProductSources:contract?.productSources??[],positiveIsCurrentPatchedProduct:true,compileFailureCountsAsNegative:false,postCreateSeedOnly:true,fullMainTranslationUnitCompiled:false,actualHostIntentExecuted:false,realIOOrTdp:false,openCoverage:contract?.openCoverage??[]};
 const resultFile=path.join(root,'result.json');writeNew(resultFile,result);return {...result,resultFile,resultSha256:hashFile(resultFile)};
}
