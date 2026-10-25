import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {toolInputs} from '../controller-signal-evidence/adapter.mjs';
import {compositionArtifactRows} from '../controller-signal-evidence/owned-cleanup-composition-receipts.mjs';
export const MODULE='tools/ymcc_sim/global/controller-host-exit';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export const contract=read(new URL('./contract.json',import.meta.url));
export const sourceInputs=Object.freeze([...new Set([...contract.sourceFiles,...['adapter.mjs','run.mjs','receipts.mjs','receipts.test.mjs','contract.json'].map(n=>MODULE+'/'+n)])].sort());
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export function artifactRows(repo=REPO){
 const scope=read(path.join(repo,contract.completionContract));
 const rows=[...toolInputs(repo).files,...compositionArtifactRows(repo),...contract.originalEvidence,contract.historicalFailure,scope.historicalMain,{path:scope.syntaxR2Provenance.receipt,sha256:scope.syntaxR2Provenance.receiptRawSha256}];
 const seen=new Map();for(const r of rows){if(hash(r.path)!==r.sha256)throw Error('NATIVE_EXIT_ARTIFACT_CHANGED:'+r.path);const k=path.win32.resolve(r.path).toLowerCase(),prior=seen.get(k);if(prior&&prior.sha256!==r.sha256)throw Error('NATIVE_EXIT_ARTIFACT_CONFLICT');seen.set(k,{path:r.path,sha256:r.sha256});}return [...seen.values()];
}
export function getControllerHostExitAdapters(){let artifacts=[],blockedReason;try{artifacts=artifactRows().map(r=>r.path);}catch(e){blockedReason=e.message;}return [{id:contract.id,domain:'controller',title:'Actual owned Host exit retirement, complete cleanup/Ensure and both completion callsites',kind:'node',path:MODULE+'/run.mjs',args:['--out','{outDir}/host-exit-owned'],profiles:['desktop','full'],timeoutMs:900000,scope:'behavioral-mock',evidenceInputs:sourceInputs,artifactInputs:artifacts,...(blockedReason?{blockedReason}:{}),resultFiles:['host-exit-owned/result.json'],lifecycleContract:{schemaVersion:1,features:[{id:'controller.native-host-local-exit',persistence:'persistent',rationale:'The virtual Host, pipe and callbacks own resident resources across focus/power events; this exit case proves scoped local returns and leaves persistent recovery incomplete.',sourceInputs:['native/main.cpp','native/input_host_exit_retirement.h','native/input_host_exit_owner_runtime.h','native/input_host_pipe_transport.h'],resources:['resident-processes','threads','streams','handles'],evidenceFile:'host-exit-owned/result.json'}]},boundary:'Actual source with private real child processes, creation threads, cleanup threads, checked Close and real pipe Runtime. SDK/physical protocol and other-module/UI cleanup are named memory leaves. Permanent OS stalls, complete main, driver/OEM/HidHide/physical return and ON sleep remain unaccepted.'}];}
