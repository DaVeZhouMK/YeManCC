/** Strict parent verifier for actual HTTP/WS cancellation. A callback-only PASS
 * never overrides failed parent handle/thread equality or calibration. */
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';
import {validate,originalContract} from './typed-legacy-async-request-regression.mjs';
const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();const inside=(root,file)=>key(file).startsWith(key(root)+path.sep);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
export function validateTypedAsyncParentReceipt(value,{repo,evidenceRoot,workRoot,sourcePins,toolPins}){
 assert.equal(value.schemaVersion,1);assert.equal(value.id,'sidecar.decky-typed-legacy-async-request');
 assert.equal(value.status,'PASS');assert.equal(value.passed,true);assert.equal(value.fullPersistentAcceptance,false);
 assert.deepEqual(value.sourceChanged,[]);assert.deepEqual(value.toolsChanged,[]);
 assert.equal(value.maxArtifactInputs,3072);assert.equal(value.budgets.compileMs,60000);assert.equal(value.budgets.nativeBindingCancelMs,20000);
 assert.equal(value.budgets.outerTypedGateMs,135000);assert.equal(value.budgets.shutdownMs,1500);assert.equal(value.budgets.callbackOwnerReleaseMs,1500);
 assert(value.elapsedMs>=0&&value.elapsedMs<=135000);assert(!value.error);
 const compilePins=new Map(),sources=new Set();
 const verify=(row,root)=>{assert(row&&/^[a-f0-9]{64}$/.test(row.sha256),'ASYNC_FILE_PIN');if(root)assert(inside(root,row.path)&&key(fs.realpathSync(row.path))===key(row.path),'ASYNC_OWNED_FILE');assert.equal(digest(row.path),row.sha256,'ASYNC_FILE_CHANGED:'+row.path);return row;};
 for(const row of value.sourceInputs){assert(!sources.has(row.path),'ASYNC_DUPLICATE_SOURCE');sources.add(row.path);assert.equal(sourcePins.get(row.path),row.sha256,'ASYNC_SOURCE_NOT_PREBOUND:'+row.path);assert.equal(digest(path.join(repo,row.path)),row.sha256);const frozen=path.join(workRoot,'s',row.path);if(fs.existsSync(frozen)){verify({...row,path:frozen},workRoot);compilePins.set(key(frozen),row);}}
 for(const file of ['typed-legacy-async-request-regression.mjs','typed-legacy-async-request-regression.cpp','typed-legacy-async-request-regression.test.mjs'])assert(sources.has('tools/ymcc_sim/global/decky-persistent-lifecycle/'+file),'ASYNC_REQUIRED_SOURCE');
 const tools=new Set();assert(value.toolArtifacts.length>0&&value.toolArtifacts.length<=3072);
 for(const row of value.toolArtifacts){assert(!tools.has(key(row.path)),'ASYNC_DUPLICATE_TOOL');tools.add(key(row.path));assert.equal(toolPins.get(key(row.path))?.sha256,row.sha256,'ASYNC_TOOL_NOT_PREBOUND');verify(row);compilePins.set(key(row.path),row);}
 assert.equal(value.generated.length,3);
 for(const row of value.generated){assert.equal(row.bytePreserved,true);assert.equal(sourcePins.get(row.origin),row.originSha256);const text=fs.readFileSync(path.join(repo,row.origin),'utf8');assert(Number.isSafeInteger(row.begin)&&Number.isSafeInteger(row.end)&&row.begin>=0&&row.end>row.begin&&row.end<=text.length);assert.equal(row.appendNewline,true);assert.equal(crypto.createHash('sha256').update(text.slice(row.begin,row.end)+'\n').digest('hex'),row.sha256);verify(row,workRoot);compilePins.set(key(row.path),row);}
 const graph=row=>{verify(row,workRoot);const value=read(row.path);assert.equal(value.Version,'1.2');for(const file of [value.Data.Source,...value.Data.Includes]){const pin=compilePins.get(key(file));assert(pin,'ASYNC_UNDECLARED_COMPILER_INPUT:'+file);assert.equal(digest(file),pin.sha256);}};
 assert.deepEqual(value.executions.map(row=>row.id),['legacy-compile','legacy-native','typed-compile','typed-native']);
 for(const row of value.executions){const limit=row.id.endsWith('compile')?60000:20000;assert.equal(row.originalBudgetMs,limit);assert(row.timeoutMs>0&&row.timeoutMs<=limit);assert(row.elapsedMs>=0&&row.elapsedMs<=row.timeoutMs);assert.equal(row.exitCode,0);assert.equal(row.signal,null);assert.equal(row.error,null);verify(row.program,row.id.endsWith('native')?workRoot:undefined);if(row.id.endsWith('compile'))assert.equal(toolPins.get(key(row.program.path))?.sha256,row.program.sha256);verify(row.stdout,evidenceRoot);verify(row.stderr,evidenceRoot);}
 graph(value.compilerGraph);for(const row of value.nativeArtifacts)verify(row,workRoot);
 verify(value.nativeImage,workRoot);const peImage=fs.readFileSync(value.nativeImage.path),pe=peImage.readUInt32LE(0x3c);assert(pe+94<=peImage.length&&peImage.readUInt32LE(pe)===0x4550,'ASYNC_NATIVE_PE');assert.equal(peImage.readUInt16LE(pe+4),0x8664);assert.equal(peImage.readUInt16LE(pe+24+68),2,'ASYNC_NATIVE_GUI_REQUIRED');assert.equal(value.nativeImage.subsystem,2);
 const legacy=value.legacyRegression;assert.equal(legacy.status,'PASS');assert.equal(legacy.source,'tools/decky_binding_cancel_selftest.cpp');assert.equal(legacy.historicalResultsReclassified,false);graph(legacy.compilerGraph);verify(legacy.raw,evidenceRoot);assert.equal(read(legacy.raw.path).ok,true);
 assert.deepEqual(value.originalContract,originalContract(fs.readFileSync(path.join(repo,legacy.source),'utf8')));
 verify(value.raw,evidenceRoot);verify(value.mapping,evidenceRoot);const raw=read(value.raw.path),summary=validate(raw);assert.deepEqual(summary,value.summary);
 const mapping=read(value.mapping.path);assert.equal(mapping.length,6);mapping.forEach((row,index)=>{const actual=raw.originalTypedRows[index];assert.equal(row.id,actual.id);assert.equal(row.kind,actual.kind);assert.equal(row.stop,actual.stop);assert.equal(row.status,'PASS');assert.deepEqual(row.raw,value.raw);assert.deepEqual(row.originalAssertions,actual.originalAssertions);assert.deepEqual(row.actualAsync,actual.afterStopAsync);assert.deepEqual(row.currentResources,actual.atExit);assert.equal(row.legacyEquivalentCallOrder,actual.stop==='shutdown');});
 return {status:'CURRENT_TYPED_ASYNC_CANCELLATION_INVARIANTS_EXECUTED',summary,raw:value.raw,mapping:value.mapping,originalLegacyStillRequired:true,fullPersistentAcceptance:false};
}
