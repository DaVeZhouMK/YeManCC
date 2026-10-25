import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,inputFiles} from './adapter.mjs';
import {need,key,hashFile,readJson,writeNew,owned,evidencePath,repositoryFile,verifyToolDeclaration,captureSources,verifySources,compilerEnvironment,executeStage,naturalTerminal,checkActualCompilerInputs,inspectPeImports,validateNativeResult,requireCurrentOriginal,walkFiles} from './helper.mjs';
import {prepareFixture} from './prepare.mjs';
import {runMainReturnStage} from './main-return/run.mjs';

export function publicStagesPassed({error,groups,postCreateStage,mainReturnStage,toolPre,toolPost,sourcePost}){
 const stagePassed=(stage,name)=>stage?.schemaVersion===1&&stage.scopeVersion===2&&stage.stage===name&&stage.status==='PASS'&&stage.passed===true&&stage.groups?.length===3;
 return !error&&groups?.length===3&&stagePassed(postCreateStage,'post-create')&&stagePassed(mainReturnStage,'main-return')&&toolPre?.status==='PASS'&&toolPost?.status==='PASS'&&sourcePost?.status==='PASS';
}

export async function runRadialNative({evidenceDir,repo=REPO,powershellPath}){
 const root=evidencePath(evidenceDir,repo);need(!fs.existsSync(root),'RADIAL_EVIDENCE_DIRECTORY_MUST_BE_FRESH');fs.mkdirSync(root,{recursive:true});
 const runId=crypto.randomUUID(),startedAtUtc=new Date().toISOString();let stage='capture',sourceRows=[],groups=[],toolPre=null,toolPost=null,sourcePost=null,prepared=null,error=null,tools=null,postCreateStage=null,mainReturnStage=null;
 const ledger=path.join(root,'stage-ledger.jsonl');fs.writeFileSync(ledger,'',{flag:'wx'});
 const note=value=>{fs.appendFileSync(ledger,JSON.stringify({atUtc:new Date().toISOString(),...value})+'\n');console.log(JSON.stringify(value));};
 try{
  sourceRows=captureSources(repo,root,inputFiles(repo));writeNew(path.join(root,'source-inputs-before.json'),{schemaVersion:1,runId,startedAtUtc,files:sourceRows});
  const saved=relative=>{const row=sourceRows.find(r=>r.path===relative);need(row,'RADIAL_REQUIRED_SOURCE_NOT_CAPTURED:'+relative);return row.saved;};
  tools=readJson(saved(MODULE+'/toolchain-inputs.json'));const plan=readJson(saved(MODULE+'/plan.json')),contract=readJson(saved(MODULE+'/extraction-contract.json'));
  need(key(process.execPath)===key(tools.executables.node.path)&&key(powershellPath??'')===key(tools.executables.powershell.path),'RADIAL_ACTUAL_NODE_OR_POWERSHELL_NOT_SELECTED');
  need(JSON.stringify(contract.productSources)===JSON.stringify(['native/radial_overlay.h','native/radial_input_region.h','deps/json/json.hpp']),'RADIAL_EXACTLY_THREE_PRODUCT_SOURCES');
  need(plan.positiveCases?.length===12&&plan.sourceFaults?.length===2&&plan.positiveNaturalExit===0&&plan.sourceFaults.every(f=>f.expectedNaturalExit===23),'RADIAL_FIXED_PLAN_CHANGED');
  for(const row of contract.sharedSources)need(hashFile(saved(row.path))===row.sha256,'RADIAL_SHARED_JOB_HELPER_NEEDS_EXPLICIT_MAINTENANCE');
  stage='approved-main-source-preflight';const approvedMainContract=readJson(saved(MODULE+'/main-return/contract.json'));
  need(approvedMainContract.scopeVersion===2&&approvedMainContract.stage==='main-return','RADIAL_MAIN_RETURN_CONTRACT_MANDATORY');
  for(const approved of approvedMainContract.productSources)need(hashFile(saved(approved.path))===approved.sha256,'MAIN_RETURN_WHOLE_SOURCE_NEEDS_EXPLICIT_MAINTENANCE:'+approved.path);
  stage='tool-preflight';toolPre=verifyToolDeclaration(tools);writeNew(path.join(root,'tool-preflight.json'),{runId,...toolPre});note({stage,status:'PASS',files:toolPre.verifiedFiles,directories:toolPre.completeDirectories});
  stage='prepare';prepared=prepareFixture({evidenceRoot:root,sourceRows,contract});
  const generated=prepared.files,ownedInputs=[...generated,...sourceRows.map(r=>({path:r.saved,sha256:r.sha256}))];const environment=compilerEnvironment(tools);
  for(const variant of ['original',...plan.sourceFaults.map(f=>f.id)]){
   if(variant!=='original'){
    const original=groups.find(g=>g.variant==='original');requireCurrentOriginal(original,{runId,evidenceRoot:root});
    need(insideThisRun(original.receipt.path)&&hashFile(original.receipt.path)===original.receipt.sha256,'RADIAL_THIS_RUN_ORIGINAL_RECEIPT_CHANGED');
    requireCurrentOriginal(readJson(original.receipt.path),{runId,evidenceRoot:root});
   }
   const directory=owned(root,path.join(root,'runs',variant));fs.mkdirSync(directory,{recursive:true});
   const exe=path.join(directory,'native-consumer.exe'),dependencyFile=path.join(directory,'source-dependencies.json');
   const args=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/WX','/MT','/O1','/X',...tools.includeDirs.map(d=>'/I'+d),'/I'+prepared.fixture,'/I'+prepared.jsonInclude,'/sourceDependencies',dependencyFile,'/Fo'+path.join(directory,'native-consumer.obj'),'/Fe'+exe,path.join(prepared.fixture,'entry-'+variant+'.cpp'),'/link','/verbose:lib','/incremental:no','/subsystem:console'];
   stage=variant+':compile';verifySources(sourceRows);for(const row of generated)need(hashFile(row.path)===row.sha256,'RADIAL_GENERATED_SOURCE_CHANGED');
   const compiler=await executeStage({root:directory,exe:tools.executables.cl.path,args,label:'compiler',env:environment,timeoutMs:60000});
   need(naturalTerminal(compiler,0),'RADIAL_COMPILE_FAILED_OR_NON_NATURAL:'+variant);need(fs.existsSync(exe)&&fs.existsSync(dependencyFile),'RADIAL_COMPILE_OUTPUT_MISSING');
   const closure=checkActualCompilerInputs({dependencyFile,compilerStdout:fs.readFileSync(compiler.stdout.path,'utf8'),tools,ownedInputs});writeNew(path.join(directory,'actual-tool-closure.json'),closure);
   const importedDlls=inspectPeImports(exe),binary={path:exe,sha256:hashFile(exe),importedDlls};
   stage=variant+':native';const fault=plan.sourceFaults.find(f=>f.id===variant);
   const terminal=await executeStage({root:directory,exe,args:fault?[fault.case]:[],label:'native',env:environment,timeoutMs:15000});
   need(naturalTerminal(terminal,fault?23:0),'RADIAL_NATIVE_EXIT_SIGNAL_TIMEOUT_OR_OUTPUT:'+variant);
   const rawFile=path.join(directory,'native-result.json');need(fs.existsSync(rawFile),'RADIAL_NATIVE_RESULT_MISSING');const native=readJson(rawFile);
   const cases=validateNativeResult({native,terminal,plan,variant,stdout:fs.readFileSync(terminal.stdout.path,'utf8'),stderr:fs.readFileSync(terminal.stderr.path,'utf8')});
   verifySources(sourceRows);for(const row of generated)need(hashFile(row.path)===row.sha256,'RADIAL_GENERATED_SOURCE_CHANGED');
   const receipt={schemaVersion:1,status:'PASS',runId,evidenceRoot:root,directory,variant,cases,compilerNaturalExit:compiler.naturalExit,nativeNaturalExit:terminal.naturalExit,compiler:{path:path.join(directory,'compiler.terminal.json'),sha256:hashFile(path.join(directory,'compiler.terminal.json'))},terminal:{path:path.join(directory,'native.terminal.json'),sha256:hashFile(path.join(directory,'native.terminal.json'))},rawResult:{path:rawFile,sha256:hashFile(rawFile)},binary,actualCompilerInputs:closure.includes.length,actualLinkLibraries:closure.libraries.length,deliberateSourceFault:!!fault,compileFailureCountsAsNegative:false};
   const receiptPath=path.join(directory,'receipt.json');writeNew(receiptPath,receipt);groups.push({...receipt,receipt:{path:receiptPath,sha256:hashFile(receiptPath)}});
   note({stage:variant,status:'PASS',compilerPid:compiler.pid,nativePid:terminal.pid,naturalExit:terminal.naturalExit,cases:cases.length});
  }
  need(groups.length===3,'RADIAL_THREE_GROUPS_REQUIRED');
  const postCreateResultFile=path.join(root,'post-create-result.json');
  postCreateStage={schemaVersion:1,scopeVersion:2,stage:'post-create',status:'PASS',passed:true,runId,groups,productionDefinitionCount:13,actualProductSources:['native/radial_overlay.h','native/radial_input_region.h','deps/json/json.hpp'],postCreateSeedOnly:true,compileFailureCountsAsNegative:false,sourceHashes:[...sourceRows.map(({path,sha256})=>({path,sha256})),...prepared.files,...groups.flatMap(g=>[g.compiler,g.terminal,g.rawResult,g.binary,g.receipt])]};
  writeNew(postCreateResultFile,postCreateStage);postCreateStage={...postCreateStage,resultFile:postCreateResultFile,resultSha256:hashFile(postCreateResultFile)};
  stage='main-return';mainReturnStage=await runMainReturnStage({parentRoot:root,sourceRows,tools,runId});
  need(mainReturnStage.scopeVersion===2&&mainReturnStage.passed===true&&mainReturnStage.groups.length===3,'RADIAL_MAIN_RETURN_STAGE_MANDATORY');
 }catch(caught){error={stage,message:caught.message,stack:caught.stack};note({stage,status:'FAIL',error:caught.message});}
 const postflightErrors=[];
 try{need(sourceRows.length>0,'RADIAL_SOURCE_CAPTURE_INCOMPLETE');sourcePost=verifySources(sourceRows);if(prepared)for(const row of prepared.files)need(hashFile(row.path)===row.sha256,'RADIAL_GENERATED_SOURCE_CHANGED');}
 catch(caught){postflightErrors.push(caught.message);sourcePost={status:'FAIL',error:caught.message};}
 writeNew(path.join(root,'source-inputs-after.json'),{runId,...sourcePost});
 try{need(tools,'RADIAL_TOOL_CAPTURE_INCOMPLETE');toolPost=verifyToolDeclaration(tools);}
 catch(caught){postflightErrors.push(caught.message);toolPost={status:'FAIL',error:caught.message};}
 writeNew(path.join(root,'tool-postflight.json'),{runId,...toolPost});
 if(postflightErrors.length){error={...(error??{stage:'postflight',message:'RADIAL_POSTFLIGHT_FAILED'}),postflightErrors};note({stage:'postflight',status:'FAIL',errors:postflightErrors});}
 const artifacts=walkFiles(root).map(file=>({path:file,sha256:hashFile(file)}));
 const passed=publicStagesPassed({error,groups,postCreateStage,mainReturnStage,toolPre,toolPost,sourcePost});
 const result={schemaVersion:2,scopeVersion:2,id:CASE_ID,status:passed?'PASS':'FAIL',passed,runId,startedAtUtc,finishedAtUtc:new Date().toISOString(),node:{pid:process.pid,path:process.execPath,sha256:hashFile(process.execPath)},powershellPath,error,groups,postCreateStage:postCreateStage?{status:postCreateStage.status,passed:postCreateStage.passed,resultFile:postCreateStage.resultFile,resultSha256:postCreateStage.resultSha256}:null,mainReturnStage:mainReturnStage?{status:mainReturnStage.status,passed:mainReturnStage.passed,resultFile:mainReturnStage.resultFile,resultSha256:mainReturnStage.resultSha256}:null,sourceHashes:[...sourceRows.map(({path,sha256})=>({path,sha256})),...artifacts],actualProductSources:['native/main.cpp','native/radial_overlay.h','native/radial_input_region.h','deps/json/json.hpp'],productionDefinitionCount:30,postCreateProductionDefinitionCount:13,mainReturnProductionDefinitionCount:30,extraction:prepared?{path:prepared.manifestFile,sha256:prepared.manifestSha256}:null,selectedTools:{files:1191,coreFiles:533,powershellPackageFiles:658,completeDirectories:4,preAndPostAccepted:toolPre?.status==='PASS'&&toolPost?.status==='PASS'},inputConsumerStateFeedback:{input:'Fresh approved current snapshots of main,radial overlay,region and JSON;original post-create stage retained',consumer:'Mandatory post-create receive/owns/send/region/proc-close plus actual main ownership callback,full gamepadEval,neutral return,native shortcut admission/queued closure through memory IO',state:'Original post-create12 plus main-return12;both stages reject late retired ownership and cleanup to zero',feedback:'Each mandatory stage retains two one-edit faults requiring its own same-run original12,compile0,natural23,exact assertions and complete output'},boundary:{postCreateSeedOnly:true,actualCreate:false,fullOverlayClassCompiled:false,actualMainDispatcher:false,actualMainReturnGate:true,actualGlobalNeutralReturn:true,actualHostActions:false,realCOM:false,realWinRT:false,realGdiGeometry:false,unicodeTransport:false,realWindows:false,hardwareOrTdpWrites:false,processTreeGuard:'existing global host-process.ps1 Job via powershell descriptor',osBinaryClosureClaimed:false,historicalDeliveryRuntimeDependency:false},remainingGaps:['Actual create/acquisition and complete Overlay class','COM ABI/refcount,WinRT/Surface,GDI geometry,Unicode','Pointer,complete parent navigation/OEM and other unselected branches','Actual queue concurrency beyond a whole radial lifecycle and unselected held controls','TypeScript host intent and actual action/TDP'],compileFailureCountsAsNegative:false,goalComplete:false};
 writeNew(path.join(root,'result.json'),result);return result;
 function insideThisRun(file){return key(file).startsWith(key(root)+path.sep);}
}
function cli(argv){need(argv.length===4&&argv[0]==='--evidence-dir'&&argv[2]==='--powershell-path','RADIAL_ARGUMENTS');return {evidenceDir:argv[1],powershellPath:argv[3]};}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const result=await runRadialNative(cli(process.argv.slice(2)));console.log(JSON.stringify({id:CASE_ID,status:result.status,error:result.error?.message??null}));process.exitCode=result.passed?0:2;}
 catch(error){console.error(error.stack);process.exitCode=2;}
}
