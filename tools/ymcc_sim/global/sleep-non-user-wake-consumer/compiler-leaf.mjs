import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
import {pathToFileURL} from 'node:url';
import fs from'node:fs';
import path from'node:path';
import vm from'node:vm';
import {createHash}from'node:crypto';
// Reuse the exact original evaluated-input rule; do not maintain a second policy.
export function loadOriginalCompilerLedger({sharedWorker,root,tool,write}){
 const source=fs.readFileSync(sharedWorker,'utf8');
 const start=source.indexOf('function archiveCompilerInputs(out){'),end=source.indexOf('\ntry{',start);
 if(start<0||end<=start)throw Error('NONUSER_SHARED_COMPILER_LEAF_NOT_FOUND');
 const exact=source.slice(start,end).trim();
 if(!exact.endsWith('}'))throw Error('NONUSER_SHARED_COMPILER_LEAF_TRUNCATED');
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex'),read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'')),need=(ok,code)=>{if(!ok)throw Error(code);},inside=(base,file)=>{const relative=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
 // Observation only: the original leaf's hash return and original need comparison remain authoritative.
 // Keep the latest comparison per normalized library, at most seven per original variant / eleven variants.
 const groups=new Map(),toolDeclaration={kind:'parsed-input-snapshot-tool',toolObjectSha256:createHash('sha256').update(JSON.stringify(tool)).digest('hex'),fileCount:tool.files.length,sharedManifest:tool.sharedManifest??null,sharedManifestSha256:tool.sharedManifestSha256??null};
 function observeLinkLibrary({searchLogBytes,...fields}){
  const row={...fields,searchLogSha256:createHash('sha256').update(searchLogBytes).digest('hex'),matched:fields.expectedSha256===fields.actualSha256,toolDeclaration};
  let group=groups.get(row.variant);
  if(!group&&groups.size<11){group=new Map();groups.set(row.variant,group);}
  if(group){if(!group.has(row.actualNormalizedKey)&&group.size>=7)group.delete(group.keys().next().value);group.set(row.actualNormalizedKey,row);}
  return row;
 }
 const factory=vm.compileFunction('return ('+exact+');',['fs','path','root','tool','hash','read','write','need','inside','observeLinkLibrary']);
 return {evaluate:factory(fs,path,root,tool,hash,read,write,need,inside,observeLinkLibrary),get observations(){return [...groups.values()].flatMap(group=>[...group.values()]);},provenance:{source:sharedWorker,sourceSha256:hash(sharedWorker),function:'archiveCompilerInputs',exactFunctionSha256:createHash('sha256').update(exact).digest('hex'),exactOriginalFunctionText:exact,policyCopiedOrModified:false}};
}
export function loadOriginalEnvironment({sharedWorker,root,tool,sharedModule}){
 const source=fs.readFileSync(sharedWorker,'utf8'),start=source.indexOf('function environment(out){'),end=source.indexOf('\nfunction execute(',start);
 if(start<0||end<=start)throw Error('NONUSER_SHARED_ENVIRONMENT_LEAF_NOT_FOUND');
 const exact=source.slice(start,end).trim();
 const factory=vm.compileFunction('return ('+exact+');',['fs','path','root','tool','mod']);
 return {environment:factory(fs,path,root,tool,sharedModule),provenance:{source:sharedWorker,sourceSha256:createHash('sha256').update(fs.readFileSync(sharedWorker)).digest('hex'),function:'environment',exactFunctionSha256:createHash('sha256').update(exact).digest('hex'),exactOriginalFunctionText:exact,policyCopiedOrModified:false}};
}

// Review the complete captured bytes, reconstruct every prior locked input and
// keep missing/changed inputs as a refusal before any compiler or worker starts.
export function verifyCurrentSleepSourceReview(repo){
 const relative="tools/ymcc_sim/global/sleep-non-user-wake-consumer/current-source-review.json",expected="b33c488d82958637d7a994b37c23c7122b79f698c77292e1d17c88f618a6f637";
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
 const need=(v,m)=>{if(!v)throw Error(m);};
 const bytes=fs.readFileSync(path.join(repo,relative));need(digest(bytes)===expected,'SLEEP_SOURCE_REVIEW_CHANGED');
 const review=JSON.parse(bytes);need(review.schemaVersion===1&&review.sourceApproval===false,'SLEEP_SOURCE_REVIEW_SCHEMA');
 for(const row of review.changes){
  const current=fs.readFileSync(path.join(repo,row.path));need(digest(current)===row.afterSha256,'SLEEP_REVIEWED_SOURCE_CHANGED:'+row.path);
  const pieces=[];let cursor=0,length=0;
  for(const edit of row.edits){const before=Buffer.from(edit.beforeBase64,'base64'),after=Buffer.from(edit.afterBase64,'base64');
   need(Number.isSafeInteger(edit.afterOffset)&&edit.afterOffset>=cursor&&edit.afterOffset+after.length<=current.length,'SLEEP_REVIEW_DELTA_RANGE');
   const unchanged=current.subarray(cursor,edit.afterOffset);pieces.push(unchanged);length+=unchanged.length;
   need(edit.beforeOffset===length&&current.subarray(edit.afterOffset,edit.afterOffset+after.length).equals(after),'SLEEP_REVIEW_DELTA_BYTES');
   pieces.push(before);length+=before.length;cursor=edit.afterOffset+after.length;
  }
  pieces.push(current.subarray(cursor));need(digest(Buffer.concat(pieces))===row.beforeSha256,'SLEEP_REVIEW_HISTORICAL_BYTES_CHANGED:'+row.path);
 }
 for(const row of review.contracts)need(digest(fs.readFileSync(path.join(repo,row.path)))===row.sha256,'SLEEP_REVIEW_CONTRACT_CHANGED:'+row.path);
 return {schemaVersion:1,status:'PASS',manifest:relative,sha256:expected,exactReverseDeltas:true,sourceApproval:false};
}

// Execute the original metadata helper in the existing guarded Node process.
// This is the sole added command; the C++/Python compiler rules stay inherited.
export const calibrationExpression='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));';
export function calibrationPrefix(tool){return calibrationToolchainPreamble(tool.vcvars64).trimEnd().split(/\r?\n/);}
export function prepareCalibrationNode({root,project,out,tool,env,write}){
 const helper=path.join(project,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs');
 const guard=path.join(root,'input-sources/tools/ymcc_sim/global/consumer/fan-global/runtime-guard.mjs');
 const sdk='C:/Program Files (x86)/Windows Kits/10',version=tool.windowsSdkVersion;
 const readFiles=[project,helper,path.join(sdk,'Include'),tool.vcvars64,
  ...['Include/'+version+'/ucrt/crtdbg.h','Include/'+version+'/um/Windows.h','Lib/'+version+'/ucrt/x64/ucrt.lib','Lib/'+version+'/um/x64/kernel32.lib'].map(x=>path.join(sdk,x))];
 const receiptPath=path.join(out,'calibration-node-boundary.json'),configurationPath=path.join(out,'calibration-node-config.json');
 write(configurationPath,{caseRoot:out,readFiles,receiptPath});
 env.PATH=path.dirname(tool.nodeExecutable)+';'+env.PATH;env.NODE_OPTIONS='--import="'+pathToFileURL(guard).href+'"';
 env.YMCC_FAN_GLOBAL_BOUNDARY=configurationPath;
 return {nodeExecutable:tool.nodeExecutable,nodeCommand:['node','--input-type=module','-e',calibrationExpression,tool.vcvars64],nodeOptions:env.NODE_OPTIONS,nodeBoundaryConfig:configurationPath,nodeBoundaryReceipt:receiptPath};
}
export function verifyCalibrationNode(config,pythonAudit){
 const need=(v,m)=>{if(!v)throw Error(m);};
 need(pythonAudit.commands.filter(x=>JSON.stringify(x.argv)===JSON.stringify(config.nodeCommand)).length===1,'NONUSER_EXACT_CALIBRATION_COMMAND_MISSING');
 const receipt=JSON.parse(fs.readFileSync(config.nodeBoundaryReceipt,'utf8'));
 need(receipt.status==='PASS'&&receipt.exitCode===0&&receipt.denied.length===0&&receipt.allowedWriteCount===0&&!receipt.explicitExitCalled,'NONUSER_CALIBRATION_NODE_BOUNDARY_FAILED');
 return {configuration:{path:config.nodeBoundaryConfig,sha256:createHash('sha256').update(fs.readFileSync(config.nodeBoundaryConfig)).digest('hex')},path:config.nodeBoundaryReceipt,sha256:createHash('sha256').update(fs.readFileSync(config.nodeBoundaryReceipt)).digest('hex'),...receipt};
}
