import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

export const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const approvedProductFiles = [
 'monitorData.ts','monitorDisplayFeed.ts','monitorHudSampleSource.ts','monitorHudFreshness.ts','monitorHudModel.ts',
 'monitorNativeDemand.ts','monitorSnapshotSignals.ts','monitorFrameTime.ts','monitorCoreTiles.ts','monitorHudFpsVisibility.ts',
 'monitorPayloadDecoder.ts','monitorSnapshotBudget.ts','monitorFpsHardware.ts','monitorTopProjection.ts','monitorHudPreview.ts',
 'monitorHudHost.ts','steamHudRelay.ts','monitorHudProtocol.ts','monitorHudWire.ts',
].map(name=>'src/bridge/'+name);
export const tests = [
 {id:'consumer-lifecycle',source:'tools/monitor_consumer_lifecycle_selftest.mjs',env:'YMCC_MONITOR_LIFECYCLE_OUT',receipt:'CONSUMER-LIFECYCLE-RESULT.json',timeoutMs:30000},
 {id:'clock-demand',source:'tools/monitor_clock_demand_selftest.mjs',env:'YMCC_CLOCK_DEMAND_OUT',receipt:'CLOCK-DEMAND-RESULT.json',timeoutMs:45000},
 {id:'bounded-identity',source:'tools/monitor_bounded_identity_selftest.mjs',env:'YMCC_BOUNDED_IDENTITY_OUT',receipt:'BOUNDED-IDENTITY-RESULT.json',timeoutMs:30000},
];
export const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');
export const readJson = file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function inside(root,target){const rel=path.relative(path.resolve(root),path.resolve(target));return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));}
export function sourceHashes(files=approvedProductFiles){return Object.fromEntries(files.map(f=>[f,sha256(fs.readFileSync(path.join(repoRoot,f)))]));}
export function assertCurrent(expected,root=repoRoot){const changed=[];for(const [relative,pin] of Object.entries(expected)){let actual;try{actual=sha256(fs.readFileSync(path.join(root,relative)));}catch{actual=null;}if(actual!==pin)changed.push({path:relative,expected:pin,actual});}if(changed.length)throw Object.assign(new Error('MONITOR_SOURCE_DRIFT'),{differences:changed});}
export function auditRegistration(){
 return Promise.all([import('../adapters.mjs'),import('../extra-adapters.mjs')]).then(([base,extra])=>{
 const adapters=[...base.getAdapters(repoRoot),...extra.getExtraAdapters()];
 return {adapterCount:adapters.length,targets:tests.map(t=>({source:t.source,directAdapterIds:adapters.filter(a=>a.path===t.source).map(a=>a.id),
  evidenceOnlyAdapterIds:adapters.filter(a=>a.evidenceInputs?.includes(t.source)).map(a=>a.id)})),
  auditMeaning:'Direct path registration is execution. Merely referencing product modules or test sources does not execute these assertions.',
  registrationInputs:['tools/ymcc_sim/global/adapters.mjs','tools/ymcc_sim/global/extra-adapters.mjs','tools/ymcc_sim/global/signal-adapters.mjs'],
 };
 });
}
export function originalAssertions(text){
 const ts=createRequire(path.join(repoRoot,'package.json'))('typescript');
 const tree=ts.createSourceFile('fixture.mjs',text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS),result=[];
 if(tree.parseDiagnostics.length)throw Error('FIXTURE_PARSE_FAILED');
 function walk(node){if(ts.isCallExpression(node)&&/^(check|assert(?:\.[a-zA-Z]+)?)$/.test(node.expression.getText(tree)))result.push(node.getText(tree));ts.forEachChild(node,walk);}
 walk(tree);return result;
}
export function verifyAssertions(before,after){const original=originalAssertions(before),adapted=originalAssertions(after);for(const value of original){const pos=adapted.indexOf(value);if(pos<0)throw new Error('ORIGINAL_ASSERTION_REMOVED:'+value.slice(0,120));adapted.splice(pos,1);}return {originalAssertionCalls:original.length,allOriginalAssertionsPreserved:true,additionalAssertionCalls:adapted.length};}

