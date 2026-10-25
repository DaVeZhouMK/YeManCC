import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {lockBundledNodeTools} from '../bundled-node-tool-authority.mjs';
export const MODULE='tools/ymcc_sim/global/tray-resident-consumer';
export const HERE=path.dirname(fileURLToPath(import.meta.url));
export const FILES=['adapter.mjs','contract.mjs','observe.cjs','run.mjs','output-evidence.mjs','contract.test.mjs','spec.json','README.md'];
export const AUTHORITY_FILES=['tools/ymcc_sim/global/bundled-node-tool-authority.mjs','tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/source-snapshot.mjs','tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs'];
export const spec=JSON.parse(fs.readFileSync(path.join(HERE,'spec.json'),'utf8'));
export const need=(ok,code)=>{if(!ok)throw Error(code);};
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const hash=file=>sha(fs.readFileSync(file));
export const key=file=>path.resolve(file).toLowerCase();
export const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export const json=file=>JSON.parse(fs.readFileSync(file,'utf8'));
export const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
export const fingerprint=(file,role)=>({path:path.resolve(file),sha256:hash(file),size:fs.statSync(file).size,status:'OK',...(role?{role}:{})});
export const unchanged=rows=>rows.every(row=>{try{return hash(row.path)===row.sha256&&fs.statSync(row.path).size===row.size;}catch{return false;}});
export function plain(file){for(let p=path.resolve(file);;p=path.dirname(p)){if(fs.existsSync(p))need(!fs.lstatSync(p).isSymbolicLink()&&key(fs.realpathSync(p))===key(p),'PATH_FORWARDING_REJECTED');if(path.dirname(p)===p)break;}}
export function checkedFile(root,relative){need(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'RELATIVE_INPUT_REQUIRED');const file=path.join(root,relative);plain(file);need(inside(fs.realpathSync(root),fs.realpathSync(file))&&fs.statSync(file).isFile(),'SOURCE_ESCAPE');return file;}
export function authorityInputs(repoRoot){
 const rows=new Map();
 for(const relative of AUTHORITY_FILES){
  const selected=checkedFile(repoRoot,relative),executing=checkedFile(path.resolve(HERE,'../../../..'),relative);
  need(hash(executing)===hash(selected),'STATIC_AUTHORITY_IMPLEMENTATION_MISMATCH:'+relative);
  for(const file of [selected,executing])rows.set(key(file),fingerprint(file,'authority-implementation'));
 }
 return [...rows.values()];
}
export function assertAuthorityPins(a){need(a.packageCount===26&&a.fileCount===749,'LOCK26_749_REQUIRED');need(key(a.nodeExecutable)===key(spec.node.path)&&hash(a.nodeExecutable)===spec.node.sha256&&process.version===spec.node.version,'BOUND_NODE_REQUIRED');return a;}
export function authority(repoRoot,options={}){authorityInputs(repoRoot);return assertAuthorityPins(lockBundledNodeTools(repoRoot,options));}
export function mutate(source){need(source.split(spec.fault.before).length===2,'FAULT_ANCHOR_NOT_UNIQUE');return source.replace(spec.fault.before,spec.fault.after);}
export function checkAliases(view,a){const nm=path.join(view,'node_modules');need(fs.readdirSync(nm).length===1&&fs.readdirSync(nm)[0]==='esbuild','UNAUTHORIZED_ALIAS');const alias=path.join(nm,'esbuild');need(fs.lstatSync(alias).isSymbolicLink(),'TOOL_ALIAS_MUST_BE_BOUND_LINK');need(key(fs.realpathSync(alias))===key(path.dirname(path.dirname(a.compilerEntry))),'TOOL_ALIAS_TARGET_NOT_BOUND');}
export function validateView(plan){plain(plan.view);checkAliases(plan.view,plan.authority);const found=[];function walk(root){for(const e of fs.readdirSync(root,{withFileTypes:true})){if(root===plan.view&&e.name==='node_modules')continue;const f=path.join(root,e.name);need(!e.isSymbolicLink(),'SOURCEVIEW_LINK_REJECTED');if(e.isDirectory())walk(f);else{need(e.isFile(),'SOURCEVIEW_SPECIAL_FILE');found.push(path.relative(plan.view,f).replaceAll('\\','/'));}}}walk(plan.view);need(JSON.stringify(found.sort())===JSON.stringify([...spec.sourceInputs].sort()),'ATOMIC_SOURCEVIEW_MEMBERS_CHANGED');need(unchanged(plan.viewInputs),'OWNED_SOURCE_CHANGED');}
export function semanticVerdict(plan,child,trace,stdout,stderr,controlsOk){
 if(!controlsOk||!trace?.finished||trace.forbidden.length||child.error||child.signal)return false;
 const expected=plan.faultId?spec.caseNames.slice(0,2):spec.caseNames;
 const passes=stdout.split(/\r?\n/).filter(l=>l.startsWith('PASS: ')).map(l=>l.slice(6));
 if(JSON.stringify(passes)!==JSON.stringify(expected))return false;
 if(!plan.faultId)return child.exitCode===0&&trace.assertions.length===0&&stdout.endsWith('\n11 checks passed. Preferences and IPC were mocked; no host state was queried or changed.\n');
 const failures=trace.assertions;
 return child.exitCode===1&&failures.length===1&&failures[0].method==='rejects'&&failures[0].code==='ERR_ASSERTION'&&failures[0].message===spec.fault.expectedAssertion&&failures[0].stack.includes('tray_resident_selftest.mjs:'+spec.fault.producerLine+':')&&stderr.includes('Missing expected rejection.')&&!stdout.includes('11 checks passed.');
}
export function verifyReceipt(receipt){try{need(receipt.accepted===true,'RECEIPT_NOT_ACCEPTED');validateView(receipt.plan);need(JSON.stringify(outputFiles(receipt.plan.caseDir).map(r=>r.path).filter(p=>path.basename(p)!=='receipt.json').sort())===JSON.stringify(receipt.outputAfter.map(r=>r.path).sort()),'OUTPUT_MEMBERS_CHANGED');need(unchanged([...receipt.plan.sourceInputs,...receipt.plan.viewInputs,...receipt.plan.tools,...receipt.plan.implementationInputs,...receipt.plan.controlInputs,...receipt.outputAfter]),'RECEIPT_IDENTITY_CHANGED');return true;}catch{return false;}}
export function outputFiles(root){const rows=[];function walk(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const f=path.join(dir,e.name);if(e.isSymbolicLink()){need(path.basename(f)==='esbuild'&&path.basename(dir)==='node_modules','OUTPUT_LINK_REJECTED');continue;}if(e.isDirectory())walk(f);else{need(e.isFile(),'OUTPUT_SPECIAL_FILE');rows.push(fingerprint(f,'owned-output'));}}}walk(root);return rows;}

/** Only this unchanged native text is consumed; main.cpp is never compiled by the tray producer. */
export function verifyTraySourceIdentity(row,bytes){
 const pin=spec.sourcePins.find(pin=>pin.path===row.relative);need(pin,'REVIEWED_SOURCE_IDENTITY_REQUIRED:'+row.relative);
 if(row.relative!=='native/main.cpp'){need(row.sha256===pin.sha256&&sha(bytes)===pin.sha256,'REVIEWED_SOURCE_IDENTITY_REQUIRED:'+row.relative);return {kind:'exact-reviewed-file',path:row.relative,sha256:row.sha256};}
 const reviews=spec.sourceReviewHistory??[],review=[...reviews].reverse().find(item=>item.path==='native/main.cpp'&&item.handlerByteEqualOriginalB48===true);need(review&&/^[a-f0-9]{64}$/.test(review.handlerSha256??''),'REVIEWED_NATIVE_TEXT_SCOPE_REQUIRED');
 const text=Buffer.isBuffer(bytes)?bytes.toString('utf8'):String(bytes),handlers=[...text.matchAll(/ipc_on\("tray\.setResident",[\s\S]*?\n    \}\);/g)];need(handlers.length===1&&sha(handlers[0][0])===review.handlerSha256,'REVIEWED_NATIVE_HANDLER_IDENTITY_REQUIRED');need(sha(bytes)===row.sha256,'SOURCE_BYTES_IDENTITY_REQUIRED');
 return {kind:'exact-reviewed-native-readonly-fragment',path:row.relative,sha256:row.sha256,handlerSha256:review.handlerSha256,nativeCompiledOrExecuted:false,unrelatedTextAccepted:true};
}
export function verifyTrayCapturedManifest(manifest,manifestFile,a,binding){
 need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(a.repoRoot),'CAPTURED_SOURCE_ROOT_REQUIRED');need(hash(manifestFile)===binding.expectedSnapshotManifestSha256&&key(binding.snapshotRoot)===key(a.repoRoot),'CAPTURED_SOURCE_MANIFEST_IDENTITY_REQUIRED');
 for(const proof of [binding.captureVerificationBefore,binding.captureVerification,binding.wholeCaptureVerification])need(proof?.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&proof.sourceApprovalGranted===false&&proof.manifestSha256===binding.expectedSnapshotManifestSha256&&key(proof.snapshotRoot)===key(a.repoRoot)&&proof.fileCount===manifest.files.length&&proof.copyVerifiedFiles===manifest.files.length,'COMPLETE_FROZEN_CAPTURE_PROOF_REQUIRED');
 need(manifest.status===binding.wholeCaptureVerification.creationStatus,'CAPTURE_CREATION_STATUS_MISMATCH');return {creationStatus:manifest.status,fullCaptureProofAtBind:binding.wholeCaptureVerification,wholeSnapshotReverifiedByLeaf:false};
}
