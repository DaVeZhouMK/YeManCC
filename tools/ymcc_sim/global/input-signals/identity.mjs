import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {captureInputDeclaration,authorizeInputFile,verifyInputDeclaration,fileHash,fileKey,need} from './inputs.mjs';
export const save=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');return file;};
export function beginInputIdentity(repoRoot,workDir){const declaration=captureInputDeclaration(repoRoot);need(fileKey(process.execPath)===fileKey(declaration.tool.node),'INPUT_ACTUAL_NODE_NOT_DECLARED');const actualNode={...authorizeInputFile(declaration,process.execPath,'actual-node-process'),version:process.version,pid:process.pid,argv:process.argv},directory=path.join(workDir,'input-identity',crypto.randomUUID());fs.mkdirSync(directory,{recursive:true});const before=save(path.join(directory,'inputs.before.json'),{schemaVersion:1,actualNode,sources:declaration.sources,tools:declaration.tool.files,declarationIsNotObservedGraph:true,osLoadedModuleGraphObserved:false});return{repoRoot,workDir,directory,declaration,actualNode,before,nativeExecutions:[],generatedFiles:[],product:null,native:null};}
export function inputObservedFile(session,file,role){return authorizeInputFile(session.declaration,file,role);}
export function generatedFile(file){return{path:path.resolve(file),sha256:fileHash(file)};}
function evidenceFiles(root){const files=[];function walk(dir){for(const row of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,row.name);need(!row.isSymbolicLink(),'INPUT_EVIDENCE_REPARSE');if(row.isDirectory())walk(file);else files.push(generatedFile(file));}}walk(root);return files;}
export function finishInputIdentity(session){if(session.finished)return session.finished;const integrity=verifyInputDeclaration(session.declaration);need(session.product&&session.native,'INPUT_COMPILATION_EVIDENCE_MISSING');need(session.nativeExecutions.length>0&&session.nativeExecutions.every(r=>r.naturalExit&&r.exitCode===0&&r.executableSha256===session.native.executableSha256),'INPUT_NATIVE_CHILD_INCOMPLETE');const snapshot={schemaVersion:1,status:'PASS',meaning:'Pinned declaration and separately observed compiler/native records; not a complete OS loaded-module graph',integrity,actualNode:session.actualNode,declaredInputs:{sources:session.declaration.sources,tools:session.declaration.tool.files,declarationIsNotObservedGraph:true},product:session.product.identity,native:session.native.identity,nativeExecutions:session.nativeExecutions,osLoadedModuleGraphObserved:false};save(path.join(session.directory,'result.json'),snapshot);const artifacts=evidenceFiles(session.directory);session.finished={...snapshot,evidenceRoot:session.directory,sourceHashes:[...session.declaration.sources,...artifacts],toolArtifacts:session.declaration.tool.files};return session.finished;}

function inputError(error,stage) {return {stage,code:error?.message??String(error),name:error?.name??'Error',stack:error?.stack??null};}
function inputFailureRecord(session,name) {
 const file=path.join(session.directory,name);if(!fs.existsSync(file))return null;
 return {path:file,sha256:fileHash(file),value:JSON.parse(fs.readFileSync(file,'utf8'))};
}
function inputFailureBytes(session,name) {
 const file=path.join(session.directory,name);if(!fs.existsSync(file))return null;
 const bytes=fs.readFileSync(file);let text,encoding='utf-8';
 try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}catch{encoding='gb18030';text=new TextDecoder(encoding,{fatal:true}).decode(bytes);}
 return {path:file,sha256:fileHash(file),encoding,text};
}
export function recordInputSetupFailure(session,error) {
 if(session.setupFailure)return session.setupFailure;
 const nativeCompile=inputFailureRecord(session,'native-compile.process.json');
 const stage=nativeCompile?'native-compile-or-input-validation':session.product?'native-setup':'product-compilation';
 const failure={...inputError(error,stage),semanticExecution:false,nativeCompile,
  compilerStderr:inputFailureBytes(session,'native-compile.stderr.log'),
  compilerStdout:inputFailureBytes(session,'native-compile.stdout.log'),
  productCompilerFailure:inputFailureRecord(session,'product-compiler-failure.json')};
 save(path.join(session.directory,'setup-failure.json'),failure);session.setupFailure=failure;return failure;
}
export function finishInputConsumerIdentity(session,result) {
 let identity;
 try{identity=finishInputIdentity(session);}
 catch(error){
  const secondary=[inputError(error,'identity-finalization')];
  // Setup failures precede any missing-compilation-evidence error. Case errors
  // retain their original objects and raw compiler records remain hash-bound.
  const firstCase=result.inputConsumerStateFeedback?.find(row=>row.status==='FAIL');
  const primary=session.setupFailure??(firstCase?{stage:'scenario',code:firstCase.code,caseId:firstCase.id,semanticExecution:firstCase.execution?.semanticExecuted??null}:inputError(error,'identity-finalization'));
  let integrity;
  try{integrity=verifyInputDeclaration(session.declaration);}catch(drift){integrity={status:'FAIL',...inputError(drift,'input-integrity')};secondary.push(inputError(drift,'input-integrity'));}
  const failed={schemaVersion:1,status:'FAIL',primaryFailure:primary,secondaryFailures:secondary,integrity,
   actualNode:session.actualNode,declaredInputs:{sources:session.declaration.sources,tools:session.declaration.tool.files,declarationIsNotObservedGraph:true},
   product:session.product?.identity??null,native:session.native?.identity??null,nativeExecutions:session.nativeExecutions,
   osLoadedModuleGraphObserved:false,semanticSuccessInferred:false};
  save(path.join(session.directory,'result.json'),failed);
  identity={...failed,evidenceRoot:session.directory,sourceHashes:[...session.declaration.sources,...evidenceFiles(session.directory)],toolArtifacts:session.declaration.tool.files};
 }
 const outcome=identity.status==='PASS'?result:{...result,status:'FAIL',verdict:'INPUT_EXECUTION_OR_EVIDENCE_FAILURE',code:identity.primaryFailure.code,
  failure:{primary:identity.primaryFailure,secondary:identity.secondaryFailures},
  executionSummary:{prerequisiteBlocked:result.inputConsumerStateFeedback.filter(row=>row.execution?.phase==='setup').length,
   semanticStarted:result.inputConsumerStateFeedback.filter(row=>row.execution?.semanticExecuted===true||row.status==='PASS').length}};
 return {...outcome,inputIdentity:identity,sourceHashes:[...new Map([...result.sourceHashes,...identity.sourceHashes].map(row=>[row.path,row])).values()],toolArtifacts:identity.toolArtifacts};
}
