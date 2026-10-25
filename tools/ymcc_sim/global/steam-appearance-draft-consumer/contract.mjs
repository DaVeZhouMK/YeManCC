import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation} from '../bundled-node-tool-authority.mjs';
export const MODULE='tools/ymcc_sim/global/steam-appearance-draft-consumer';
export const REPO=fileURLToPath(new URL('../../../../',import.meta.url));
export const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const need=(value,code)=>{if(!value)throw Error('STEAM_DRAFT_'+code);};
export const key=file=>path.resolve(file).toLowerCase();
export const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
export const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
export const SPEC=read(fileURLToPath(new URL('./spec.json',import.meta.url)));
export const MODULE_FILES=Object.freeze(['adapter.mjs','contract.mjs','run.mjs','observe.cjs','loader.mjs','spec.json','contract.test.mjs','README.md']);
export const SOURCE_INPUTS=Object.freeze([...new Set([SPEC.producer,SPEC.productSource,...MODULE_FILES.map(f=>MODULE+'/'+f),...BUNDLED_AUTHORITY_SOURCES])]);
export function sourceFile(repo,relative){
 need(typeof relative==='string'&&relative!==''&&!path.isAbsolute(relative)&&!relative.includes('\\')&&!relative.split('/').some(p=>p==='..'||p==='')&&!/[\0\r\n]/.test(relative),'EXACT_RELATIVE_SOURCE_REQUIRED');
 const root=fs.realpathSync(repo),file=path.resolve(root,relative),real=fs.realpathSync(file);
 need(inside(root,real)&&key(file)===key(real)&&fs.statSync(real).isFile(),'SOURCE_FORWARDING_REJECTED');return real;
}
export function lockContext(repo,{env=process.env}={}){
 need(!env.NODE_OPTIONS&&!env.NODE_PATH&&!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'AMBIENT_NODE_FORWARDING_REJECTED');
 const authority=lockBundledNodeTools(repo,{env});
 const helpers=verifyBundledImplementation(repo);
 const sources=SOURCE_INPUTS.map(relative=>({path:relative,sha256:hash(sourceFile(repo,relative)),status:'OK'}));
 need(hash(sourceFile(repo,SPEC.producer))===SPEC.producerSha256,'ORIGINAL_PRODUCER_NEEDS_REVIEW');
 const raw=fs.readFileSync(sourceFile(repo,SPEC.producer),'utf8');
 need(JSON.stringify([...raw.matchAll(/await check\('([^']+)'/g)].map(m=>m[1]))===JSON.stringify(SPEC.caseNames),'ORIGINAL_CASE_ORDER_CHANGED');
 const manifestFile=path.join(repo,'.ymcc-source-snapshot/manifest.json');let sourceSelection={mode:'explicit-selected-repository-or-owned-view',repoRoot:fs.realpathSync(repo),sourceApproval:false};
 if(fs.existsSync(manifestFile)){
  const pin=authority.tools.find(row=>key(row.path)===key(manifestFile));need(pin&&hash(manifestFile)===pin.sha256,'EXPLICIT_SNAPSHOT_MANIFEST_IDENTITY_REQUIRED');
  const manifest=read(manifestFile);need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(repo)&&Array.isArray(manifest.files),'SOURCE_MANIFEST_ROOT_OR_SCHEMA');
  for(const row of sources)need(manifest.files.some(pin=>pin.path===row.path&&pin.sha256===row.sha256),'SOURCE_NOT_IN_SELECTED_MANIFEST:'+row.path);
  sourceSelection={mode:'explicit-selected-snapshot-manifest',repoRoot:fs.realpathSync(repo),manifestFile,manifestSha256:pin.sha256,manifestStatus:manifest.status,sourceApproval:false,wholeSnapshotApprovalClaimed:false};
 }
 need(authority.tools.length<=3072,'ARTIFACT_BUDGET');
 return {authority,helpers,sources,sourceSelection};
}
export function applyFault(source,fault){need(typeof fault.before==='string'&&source.split(fault.before).length===2&&fault.before!==fault.after,'FAULT_OPERAND_NOT_UNIQUE');return source.replace(fault.before,fault.after);}
export function verifyPositive(raw,trace,exitCode){
 need(exitCode===0&&raw?.ok===true&&raw.passed===SPEC.caseNames.length&&JSON.stringify(raw.cases)===JSON.stringify(SPEC.caseNames),'ORIGINAL_POSITIVE_OR_ORDER_FAILED');
 need(raw.realSteamOperations===0&&raw.realCssOperations===0,'ORIGINAL_MEMORY_BOUNDARY_CHANGED');
 verifyExecution(trace);need(trace.failure===null,'POSITIVE_HAS_EXCEPTION');
 return {cases:raw.passed,originalNamesAndOrder:true,originalExitCode:exitCode};
}
export function verifyExecution(trace){
 need(trace&&trace.forbidden.length===0&&trace.transforms.length===1&&trace.vmRuns.length===1&&trace.compilerProcesses.length===1,'COMPILE_VM_OR_IO_PRECONDITION');
 const transform=trace.transforms[0],vm=trace.vmRuns[0],compiler=trace.compilerProcesses[0];
 need(transform.success===true&&vm.success===true&&vm.exportedCreateAppearanceDraft===true&&compiler.exitCode===0,'COMPILE_AND_VM_MUST_SUCCEED');
 need(vm.codeSha256===transform.outputSha256&&transform.inputSha256===trace.productSha256,'ACTUAL_VM_SOURCE_CHAIN_BROKEN');
}
export function verifyNegative(trace,fault,exitCode){
 verifyExecution(trace);const error=trace.failure;
 need(exitCode===1&&error?.code==='ERR_ASSERTION'&&error.name==='AssertionError'&&error.originalNamedCheck===fault.expectedName,'NAMED_BUSINESS_ASSERTION_NOT_DETECTED');
 need(error.producerFrame&&error.producerFrame.line>0&&error.stack.includes('AssertionError'),'ORIGINAL_ASSERTION_FRAME_MISSING');
 return {fault:fault.id,expectedName:fault.expectedName,actualName:error.originalNamedCheck,originalExitCode:exitCode,compiled:true,vmStarted:true,semanticDetection:true,error};
}
