// Offline unit controls for the exact branded-context recheck implementation.
// Only private files, the branding map and log-data port are supplied here.
// Admission, native processes, product startup and public acceptance do not run.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const args=process.argv.slice(2),options=Object.fromEntries(Array.from({length:args.length/2},(_,i)=>[args[i*2],args[i*2+1]]));
assert(args.length%2===0&&Object.keys(options).every(k=>['--out','--source'].includes(k))&&options['--out'],'Use --out FRESH_ABSOLUTE_PRIVATE_DIR [--source EXACT_SOURCE_FILE]');
const repo=path.resolve(import.meta.dirname,'../../../..'),out=path.resolve(options['--out']);
const inside=(root,file)=>{const r=path.relative(root,file);return !r||!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
assert(path.isAbsolute(options['--out'])&&!inside(repo,out)&&!fs.existsSync(out)&&out!==path.parse(out).root);
fs.mkdirSync(out,{recursive:true});
const file=path.resolve(options['--source']??path.join(import.meta.dirname,'r3-current-prepared.mjs')),bytes=fs.readFileSync(file),source=bytes.toString('utf8');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),sourcePin={path:file,sha256:sha(bytes)};
const start=source.indexOf('function liveState(context)'),end=source.indexOf('export function verifyCurrentPreparedExecution(context,raw,terminal)');
assert(start>=0&&end>start&&source.indexOf('function liveState(context)',start+1)<0);
const code=source.slice(start,end).replaceAll('export function ','function '),live=new WeakMap(),calls=[],dataCalls=[];
const hash=file=>sha(fs.readFileSync(file));
const sandbox={live,key:file=>path.resolve(file).toLowerCase(),need:(ok,code)=>{if(!ok)throw Error('R3_CURRENT_PREPARED_'+code);},
 fileHash:file=>{calls.push(path.resolve(file).toLowerCase());return hash(file);},
 readLifecycleLogData:(repo,pin)=>{dataCalls.push(pin.path);assert.equal(hash(pin.path),pin.sha256,'DATA_CONTRACT_CHANGED');},
 structuredClone};
vm.runInNewContext(code+'\n globalThis.api={one:recheckCurrentPrepared,batch:typeof recheckCurrentPreparedContexts===\'function\'?recheckCurrentPreparedContexts:contexts=>contexts.map(recheckCurrentPrepared)};',sandbox,{filename:file});
const api=sandbox.api,fixtures=path.join(out,'files');fs.mkdirSync(fixtures);
const a=path.join(fixtures,'a.bin'),b=path.join(fixtures,'b.bin'),data=path.join(fixtures,'data.json');
fs.writeFileSync(a,'aaa');fs.writeFileSync(b,'bbb');fs.writeFileSync(data,'ddd');
const pin=file=>({path:file,sha256:hash(file)}),aPin=pin(a),bPin=pin(b),dPin=pin(data),json=value=>JSON.parse(JSON.stringify(value));
const bind=(pinned=[],mapped=[],dataInputs=[])=>{const context=Object.freeze({kind:'UNIT_BRANDED_CONTEXT'});live.set(context,{repo,pinned,mapped,dataInputs});return context;};
const expected=(files,currentSources)=>({status:'ALL_CURRENT_COMPILED_INPUTS_REHASHED',files,currentSources});
const cases=[];
function check(id,action){calls.length=0;dataCalls.length=0;let error=null;try{action();}catch(e){error=String(e.stack??e);}cases.push({id,passed:error===null,error,fileReads:calls.length,logDataChecks:dataCalls.length});fs.writeFileSync(a,'aaa');fs.writeFileSync(b,'bbb');fs.writeFileSync(data,'ddd');}
check('single-api-retains-result-shape-and-independent-source-count',()=>{const c=bind([aPin],[{...bPin,relative:'b.bin'}]);assert.deepEqual(json(api.one(c)),expected(1,1));assert.equal(calls.length,2);});
check('five-contexts-share-byte-reads-with-all-five-results-and-log-contracts',()=>{const contexts=Array.from({length:5},()=>bind([aPin,bPin],[{...aPin,relative:'a.bin'}],[{dataPin:dPin}]));assert.deepEqual(json(api.batch(contexts)),contexts.map(()=>expected(2,1)));assert.equal(calls.length,2);assert.equal(dataCalls.length,5);});
check('duplicates-within-one-context-still-have-every-expected-digest-checked',()=>{const c=bind([aPin,aPin],[{...aPin,relative:'a.bin'}]);assert.deepEqual(json(api.one(c)),expected(2,1));assert.equal(calls.length,1);});
check('fresh-second-pass-rehashes-shared-bytes',()=>{const c=bind([aPin,bPin]);api.batch([c,c]);calls.length=0;api.batch([c,c]);assert.equal(calls.length,2);});
check('same-size-same-mtime-mutation-between-passes-is-rejected',()=>{const c=bind([aPin]),stat=fs.statSync(a);api.batch([c]);fs.writeFileSync(a,'zzz');fs.utimesSync(a,stat.atime,stat.mtime);assert.throws(()=>api.batch([c]),/BOUND_INPUT_CHANGED/);});
check('current-source-mutation-retains-original-specific-error',()=>{const c=bind([],[{...bPin,relative:'src/b.ts'}]);api.one(c);fs.writeFileSync(b,'xxx');assert.throws(()=>api.one(c),/CURRENT_SOURCE_CHANGED:src\/b\.ts/);});
check('conflicting-second-context-pin-cannot-use-first-context-hash-as-trust',()=>{const good=bind([aPin]),bad=bind([{...aPin,sha256:bPin.sha256}]);assert.throws(()=>api.batch([good,bad]),/BOUND_INPUT_CHANGED/);});
check('conflicting-mapped-expected-hash-is-rejected-after-shared-pin',()=>{const c=bind([aPin],[{...aPin,sha256:bPin.sha256,relative:'src/a.ts'}]);assert.throws(()=>api.one(c),/CURRENT_SOURCE_CHANGED:src\/a\.ts/);});
check('unbranded-second-context-refuses-entire-batch-before-file-reads',()=>{const c=bind([aPin]);assert.throws(()=>api.batch([c,Object.freeze({})]),/LIVE_BRANDED_CONTEXT_REQUIRED/);assert.equal(calls.length,0);});
check('failed-pass-does-not-poison-later-valid-byte-check',()=>{const c=bind([aPin]);fs.writeFileSync(a,'xxx');assert.throws(()=>api.one(c),/BOUND_INPUT_CHANGED/);fs.writeFileSync(a,'aaa');assert.deepEqual(json(api.one(c)),expected(1,0));});
check('log-contracts-are-revalidated-for-each-context-and-pass',()=>{const c=bind([aPin],[],[{dataPin:dPin}]);api.batch([c,c]);assert.equal(dataCalls.length,2);fs.writeFileSync(data,'bad');assert.throws(()=>api.batch([c]),/DATA_CONTRACT_CHANGED/);});
check('missing-file-errors-are-not-hidden-by-prior-success',()=>{const gone=path.join(fixtures,'gone.bin');fs.writeFileSync(gone,'gone');const c=bind([pin(gone)]);api.one(c);fs.unlinkSync(gone);assert.throws(()=>api.one(c),/ENOENT/);});
check('windows-case-aliases-share-bytes-without-skipping-digest-comparison',()=>{if(process.platform!=='win32')return;const c=bind([aPin,{...aPin,path:a.toUpperCase()}]);api.one(c);assert.equal(calls.length,1);});
check('separate-single-callers-never-share-a-digest-cache',()=>{const c=bind([aPin]);api.one(c);api.one(c);assert.equal(calls.length,2);});
assert.equal(sha(fs.readFileSync(file)),sourcePin.sha256,'SOURCE_CHANGED_DURING_UNIT_CONTROLS');
const result={schemaVersion:1,status:cases.every(c=>c.passed)?'PASS':'FAIL',sourcePin,exactSourceSlice:{start,end,sha256:sha(Buffer.from(source.slice(start,end)))},cases,counts:{cases:cases.length,passed:cases.filter(c=>c.passed).length,failed:cases.filter(c=>!c.passed).length},boundary:{onlyExactRecheckFunctionsExecuted:true,privateFiles:true,brandingAndLogDataPortSuppliedByUnit:true,prerequisiteAdmissionExecuted:false,nativeConsumerExecuted:false,installedApplicationControlled:false,manualSleepAcceptanceReady:false,wholeGoalComplete:false}};
fs.writeFileSync(out+'/result.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:result.status,counts:result.counts,failedIds:cases.filter(c=>!c.passed).map(c=>c.id),out}));if(result.status!=='PASS')process.exitCode=1;
