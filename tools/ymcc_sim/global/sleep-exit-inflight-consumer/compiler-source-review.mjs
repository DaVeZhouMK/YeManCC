// Source/receipt checks for the three existing indirect compiler consumers.
// The original compiler/environment functions and worker IO rules remain the executor.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const need=(ok,code)=>{if(!ok)throw Error(code);};
const reviewPath='tools/ymcc_sim/global/sleep-exit-inflight-consumer/compiler-source-review.json';
const reviewSha256='3b5b94a78e34ec77faae67bb8fca8c25bc4e8dc4bbfb48a02fd5643cd6bbb3ae';
const canonical=bytes=>bytes.toString('utf8').replace(/\r\n/g,'\n');
function exportedFunction(bytes,name){const source=canonical(bytes),start=source.indexOf('export function '+name+'('),end=source.indexOf('\n}',start);need(start>=0&&end>start,'COMPILER_REVIEW_EXPORT_MISSING:'+name);return source.slice(start,end+2);}
export function verifyCompilerSourceReview(repo){
 const bytes=fs.readFileSync(path.join(repo,reviewPath));need(digest(bytes)===reviewSha256,'COMPILER_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(bytes);need(review.schemaVersion===1&&review.kind==='exact-inherited-compiler-leaf-review'&&review.sourceApproval===false&&review.toolPermissionsExpanded===false,'COMPILER_REVIEW_SCHEMA');
 const current=fs.readFileSync(path.join(repo,review.leaf));need(digest(current)===review.afterSha256,'COMPILER_REVIEW_SOURCE_CHANGED');
 let cursor=0,length=0;const pieces=[];
 for(const edit of review.edits){const before=Buffer.from(edit.beforeBase64,'base64'),after=Buffer.from(edit.afterBase64,'base64');need(Number.isSafeInteger(edit.afterOffset)&&edit.afterOffset>=cursor&&edit.afterOffset+after.length<=current.length,'COMPILER_REVIEW_DELTA_RANGE');const unchanged=current.subarray(cursor,edit.afterOffset);pieces.push(unchanged);length+=unchanged.length;need(edit.beforeOffset===length&&current.subarray(edit.afterOffset,edit.afterOffset+after.length).equals(after),'COMPILER_REVIEW_DELTA_BYTES');pieces.push(before);length+=before.length;cursor=edit.afterOffset+after.length;}
 pieces.push(current.subarray(cursor));const prior=Buffer.concat(pieces);need(digest(prior)===review.beforeSha256,'COMPILER_REVIEW_OLD_BYTES_CHANGED');
 for(const row of review.inheritedExports){const before=exportedFunction(prior,row.name),after=exportedFunction(current,row.name);need(before===after&&digest(Buffer.from(after))===row.exactFunctionSha256,'COMPILER_REVIEW_INHERITED_FUNCTION_CHANGED:'+row.name);}
 need(hash(path.join(repo,review.sharedWorker.path))===review.sharedWorker.sha256,'COMPILER_REVIEW_ORIGINAL_WORKER_CHANGED');
 for(const row of review.staticDependencies)need(hash(path.join(repo,row.path))===row.sha256,'COMPILER_REVIEW_ESM_DEPENDENCY_CHANGED:'+row.path);
 return{schemaVersion:1,status:'PASS',manifest:reviewPath,manifestSha256:reviewSha256,beforeSha256:review.beforeSha256,afterSha256:review.afterSha256,exactReverseBytes:true,inheritedExports:review.inheritedExports,inheritedRules:review.inheritedRules,sharedWorker:review.sharedWorker,calibrationInvocationAllowed:false,sourceApproval:false};
}
export function verifyInheritedCompilerRules(compiler,environment,review){
 for(const [consumer,name] of [[compiler,'archiveCompilerInputs'],[environment,'environment']]){
  const expected=review.inheritedRules.find(row=>row.name===name),actual=consumer.provenance;
  need(expected&&actual.function===name&&actual.policyCopiedOrModified===false&&actual.sourceSha256===review.sharedWorker.sha256&&actual.exactFunctionSha256===expected.canonicalLfSha256&&digest(Buffer.from(actual.exactOriginalFunctionText.replace(/\r\n/g,'\n')))===expected.canonicalLfSha256,'COMPILER_REVIEW_ACTUAL_INHERITED_RULE_CHANGED:'+name);
 }
 return true;
}
// V8 writes this original coverage at natural Node exit. A declared file list
// cannot substitute for these actual evaluated modules and function call counts.
export function verifyWorkerEsmCoverage(scratch,snapshot){
 const directory=path.join(scratch,'esm-coverage'),names=fs.readdirSync(directory).filter(name=>/^coverage-.*\.json$/.test(name));need(names.length===1,'COMPILER_REVIEW_ESM_COVERAGE_MISSING_OR_EXTRA');const file=path.join(directory,names[0]);need(fs.statSync(file).size<16*1024*1024,'COMPILER_REVIEW_ESM_COVERAGE_LIMIT');const raw=JSON.parse(fs.readFileSync(file,'utf8'));need(Array.isArray(raw.result),'COMPILER_REVIEW_ESM_COVERAGE_SCHEMA');
 const required=[snapshot.module+'/worker.mjs','tools/ymcc_sim/global/sleep-exit-inflight-consumer/compiler-source-review.mjs','tools/ymcc_sim/global/sleep-native-recovery-consumer/scratch.mjs','tools/ymcc_sim/global/sleep-non-user-wake-consumer/compiler-leaf.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].sort(),observed=[];
 for(const entry of raw.result){if(!entry.url?.startsWith('file:'))continue;const actual=fileURLToPath(entry.url),relative=path.relative(path.join(scratch,'input-sources'),actual).replaceAll('\\','/'),source=snapshot.sources.find(row=>row.path===relative);need(required.includes(relative)&&source&&hash(actual)===source.sha256,'COMPILER_REVIEW_UNDECLARED_EVALUATED_ESM:'+relative);need(entry.functions?.[0]?.ranges?.[0]?.count>0,'COMPILER_REVIEW_ESM_NOT_EVALUATED:'+relative);observed.push({path:relative,sha256:source.sha256,moduleEvaluated:true,functions:entry.functions.map(row=>({name:row.functionName,calls:row.ranges[0].count}))});}
 need(JSON.stringify(observed.map(x=>x.path).sort())===JSON.stringify(required),'COMPILER_REVIEW_ESM_CLOSURE_INCOMPLETE');
 const count=(relative,name)=>{const row=observed.find(x=>x.path===relative)?.functions.find(x=>x.name===name);need(row,'COMPILER_REVIEW_ESM_FUNCTION_MISSING:'+name);return row.calls;},leaf='tools/ymcc_sim/global/sleep-non-user-wake-consumer/compiler-leaf.mjs',helper='tools/fixtures/hc_reviewed_calibration/toolchain.mjs';
 for(const name of ['loadOriginalCompilerLedger','loadOriginalEnvironment'])need(count(leaf,name)>0,'COMPILER_REVIEW_INHERITED_FUNCTION_NOT_CALLED:'+name);
 for(const name of ['verifyCurrentSleepSourceReview','calibrationPrefix','prepareCalibrationNode','verifyCalibrationNode'])need(count(leaf,name)===0,'COMPILER_REVIEW_UNRELATED_CALIBRATION_CALLED:'+name);
 need(count(helper,'calibrationToolchainPreamble')===0,'COMPILER_REVIEW_UNRELATED_CALIBRATION_CALLED:calibrationToolchainPreamble');
 return{schemaVersion:1,status:'PASS',method:'original Node V8 coverage emitted on worker exit',rawCoverage:{path:file,sha256:hash(file)},actualEsmSources:observed,calibrationModuleEvaluated:true,calibrationToolchainPreambleCalls:0,calibrationCommandExecuted:false,permissionsExpanded:false,sourceApproval:false};
}
