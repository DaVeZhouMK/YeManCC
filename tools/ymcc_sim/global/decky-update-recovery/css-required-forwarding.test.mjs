import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import test from 'node:test';import{fileURLToPath}from'node:url';
import{forwardCssRequiredReceipts}from'./css-required-forwarding.mjs';
import{checkStructuredEvidence}from'../runner.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const core='G:\\YeManCC-Work\\Testing\\YMCC';
const evidenceRoot=path.join(core,'Evidence');
const base=process.env.YMCC_CSS_FORWARDING_EVIDENCE_PARENT||evidenceRoot;
assert(path.isAbsolute(base),'CSS control evidence parent must be absolute');
const basePath=path.resolve(base),relativeBase=path.relative(evidenceRoot,basePath);
assert(basePath===evidenceRoot||relativeBase&&!relativeBase.startsWith('..'+path.sep)&&relativeBase!=='..'&&!path.isAbsolute(relativeBase),'CSS control evidence parent must stay in core Evidence');
assert.equal(fs.realpathSync(basePath),basePath,'CSS control evidence parent must be canonical');
const root=fs.mkdtempSync(path.join(basePath,'CssRequiredForwarding-'));let serial=0;
const capturedCase=process.env.YMCC_CSS_CAPTURED_CASE;
const capturedMapSha=process.env.YMCC_CSS_CAPTURED_MAP_SHA256;
const capturedRequested=capturedCase!==undefined||capturedMapSha!==undefined;
const captureSkip=capturedRequested?false:'NOT_RUN: explicit absolute YMCC_CSS_CAPTURED_CASE and YMCC_CSS_CAPTURED_MAP_SHA256 were not supplied';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const required=['appearance-retired-ui.json','appearance-observe-only-validation.json','appearance-refresh/result.json','appearance-native/result.json'];
const dependencies=['appearance-observe-only-result/observe-only-business-failure.json','appearance-observe-only-harness.ps1','appearance-observe-only-negative/steam-appearance.ps1'];
const write=(p,b)=>{const relative=path.relative(root,path.resolve(p));assert(relative&&relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative),'Only this invocation owns mutable scratch');fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b);};
const plainSource=(p)=>{assert(path.isAbsolute(p),'Captured source must be absolute');const absolute=path.resolve(p);assert.equal(fs.realpathSync(absolute),absolute,'Captured source alias');for(let current=absolute;;current=path.dirname(current)){assert(!fs.lstatSync(current).isSymbolicLink(),'Captured source reparse alias');if(path.dirname(current)===current)break;}return absolute;};
const json=(p,j)=>write(p,JSON.stringify(j,null,2)+'\n');
function fixture(captured=false){
 const caseDir=path.join(root,'case-'+(++serial)),out=path.join(caseDir,'decky-update-recovery'),canonical=path.join(out,'css-appearance-worker','css'),mapPath=path.join(out,'css-appearance-worker','CSS-RAW-MAP.json');
 fs.mkdirSync(canonical,{recursive:true});let source=path.join(root,'absent-original-source');let originalMap=null,originalCase=null;
 if(captured){
  assert(typeof capturedCase==='string'&&typeof capturedMapSha==='string','Explicit captured case and map SHA must be supplied together');
  assert(/^[a-f0-9]{64}$/.test(capturedMapSha),'Explicit captured map SHA256 required');
  originalCase=plainSource(capturedCase);assert.equal(path.basename(originalCase),'decky-update-recovery');
  const originalMapPath=plainSource(path.join(originalCase,'css-appearance-worker','CSS-RAW-MAP.json')),originalMapBytes=fs.readFileSync(originalMapPath);
  assert.equal(hash(originalMapBytes),capturedMapSha,'Captured map identity changed');
  originalMap=JSON.parse(originalMapBytes.toString('utf8').replace(/^\uFEFF/,''));
  assert.equal(originalMap.schemaVersion,1);assert.equal(originalMap.byteExact,true);assert(Array.isArray(originalMap.files));
  assert.equal(originalMap.canonical,path.join(originalCase,'css-appearance-worker','css'));plainSource(originalMap.canonical);
  assert(path.isAbsolute(originalMap.source));source=originalMap.source;
 }
 const contents=new Map();
 for(const relative of [...required,...dependencies]){
  let bytes;if(captured){const originalPath=plainSource(path.join(originalMap.canonical,relative)),rows=originalMap.files.filter(row=>row.path===originalPath);assert.equal(rows.length,1,'Unique captured map row required');const row=rows[0];assert.equal(row.source,path.join(originalMap.source,relative));bytes=fs.readFileSync(originalPath);assert.equal(bytes.length,row.bytes,'Captured bytes changed');assert.equal(hash(bytes),row.sha256,'Captured raw identity changed');}
  else if(relative===required[1])continue;
  else bytes=Buffer.from(relative===dependencies[0]?'{"status":"FAIL","originalNegative":true}\n':relative.endsWith('.json')?'{"status":"PASS","syntheticControl":true}\n':'# synthetic dependency '+relative+'\n');
  contents.set(relative,bytes);
 }
 if(!captured){const sourceHashes={};for(const dep of dependencies)sourceHashes[path.join(source,dep)]=hash(contents.get(dep));contents.set(required[1],Buffer.from(JSON.stringify({schemaVersion:1,status:'PASS',expectedRawStatus:'FAIL',proof:{originalNegativePreserved:true},sourceHashes},null,2)+'\n'));}
 const files=[];for(const [relative,bytes]of contents){const p=path.join(canonical,relative);write(p,bytes);files.push({source:path.join(source,relative),path:p,sha256:hash(bytes),bytes:bytes.length});}
 const map={schemaVersion:1,source,canonical,byteExact:true,files};json(mapPath,map);
 const cssRaw={canonical,files:structuredClone(files),map:{path:mapPath,sha256:hash(fs.readFileSync(mapPath))}};
 const refreshMap=()=>{json(mapPath,map);cssRaw.map.sha256=hash(fs.readFileSync(mapPath));cssRaw.files=structuredClone(map.files);};
 const refreshRow=relative=>{const p=path.join(canonical,relative),b=fs.readFileSync(p),row=map.files.find(x=>x.path===p);row.sha256=hash(b);row.bytes=b.length;refreshMap();};
 return{caseDir,out,canonical,mapPath,map,cssRaw,contents,refreshMap,refreshRow,originalCase,originalMap};
}
const run=f=>forwardCssRequiredReceipts({cssRaw:f.cssRaw,out:f.out});
const check=(f,rel)=>checkStructuredEvidence(f.caseDir,{resultFiles:['decky-update-recovery/'+rel]},repo);
test('correct forwarding preserves3 byte-exact receipts and the real public source gate accepts the derived wrapper',()=>{
 const f=fixture();assert(!fs.existsSync(f.map.source));const original=fs.readFileSync(path.join(f.canonical,required[1]));
 const prior=check(f,'css-appearance-worker/css/'+required[1]);assert.equal(prior.failed,true);assert.equal(prior.paths[0].sourceMismatches.length,3);
 const rows=run(f);assert.equal(rows.length,4);for(const relative of required.filter(x=>x!==required[1]))assert(fs.readFileSync(path.join(f.out,relative)).equals(f.contents.get(relative)));
 assert(fs.readFileSync(path.join(f.canonical,required[1])).equals(original));const derived=JSON.parse(fs.readFileSync(path.join(f.out,required[1])));
 assert.equal(derived.status,'PASS');assert.equal(derived.expectedRawStatus,'FAIL');assert.equal(derived.cssRequiredForwarding.originalValidation.sha256,hash(original));
 assert.equal(Object.keys(derived.sourceHashes).length,3);assert(Object.keys(derived.sourceHashes).every(p=>p.startsWith(f.canonical+path.sep)));
 assert.equal(JSON.parse(fs.readFileSync(path.join(f.canonical,dependencies[0]))).status,'FAIL');
 const after=check(f,required[1]);assert.equal(after.missing,false);assert.equal(after.failed,false);assert.equal(after.paths[0].reportedSourceCount,3);
});
test('FAIL wrapper cannot be converted to PASS',()=>{const f=fixture(),p=path.join(f.canonical,required[1]),v=JSON.parse(fs.readFileSync(p));v.status='FAIL';json(p,v);f.refreshRow(required[1]);run(f);assert.equal(JSON.parse(fs.readFileSync(path.join(f.out,required[1]))).status,'FAIL');assert.equal(check(f,required[1]).failed,true);});
test('changed pinned map refuses',()=>{const f=fixture();fs.appendFileSync(f.mapPath,' ');assert.throws(()=>run(f),/MAP_CHANGED/);});
test('changed canonical wrapper refuses',()=>{const f=fixture();fs.appendFileSync(path.join(f.canonical,required[1]),' ');assert.throws(()=>run(f),/CANONICAL_(BYTES|SHA)_CHANGED/);});
test('changed dependency refuses',()=>{const f=fixture();fs.appendFileSync(path.join(f.canonical,dependencies[0]),' ');assert.throws(()=>run(f),/CANONICAL_(BYTES|SHA)_CHANGED/);});
test('missing mapping refuses',()=>{const f=fixture();f.map.files=f.map.files.filter(x=>!x.path.endsWith('observe-only-business-failure.json'));f.refreshMap();assert.throws(()=>run(f),/SOURCE_MAPPING_MISSING/);});
test('duplicate mapping refuses',()=>{const f=fixture();f.map.files.push({...f.map.files[0]});f.refreshMap();assert.throws(()=>run(f),/DUPLICATE_MAPPING/);});
test('case alias mapping refuses',()=>{const f=fixture();const row={...f.map.files[0]};row.source=row.source.toUpperCase();row.path=row.path.toUpperCase();f.map.files.push(row);f.refreshMap();assert.throws(()=>run(f),/DUPLICATE_MAPPING|MAPPING_OUTSIDE|MAPPING_RELATIVE_ALIAS/);});
test('out-of-case mapping refuses',()=>{const f=fixture();f.map.files[0].path=path.join(root,'other-case.json');f.refreshMap();assert.throws(()=>run(f),/MAPPING_OUTSIDE/);});
test('authoritative map and in-memory files cannot disagree',()=>{const f=fixture();f.cssRaw.files[0].sha256='0'.repeat(64);assert.throws(()=>run(f),/FILES_MAP_MISMATCH/);});
test('duplicate JSON map keys refuse rather than last-value wins',()=>{const f=fixture();const s=fs.readFileSync(f.mapPath,'utf8').replace('"schemaVersion": 1','"schemaVersion": 1, "schemaVersion": 1');write(f.mapPath,s);f.cssRaw.map.sha256=hash(Buffer.from(s));assert.throws(()=>run(f),/DUPLICATE_JSON_KEY/);});
test('existing output is preserved and all destinations preflight before publication',()=>{const f=fixture();const p=path.join(f.out,required[3]);write(p,'sentinel');assert.throws(()=>run(f),/OUTPUT_EXISTS/);assert.equal(fs.readFileSync(p,'utf8'),'sentinel');assert(!fs.existsSync(path.join(f.out,required[0])));});
test('real junction outside exact case refuses',()=>{const f=fixture(),own=path.join(f.canonical,'appearance-observe-only-result'),foreign=path.join(root,'foreign-'+serial);fs.mkdirSync(foreign);fs.copyFileSync(path.join(own,'observe-only-business-failure.json'),path.join(foreign,'observe-only-business-failure.json'));fs.unlinkSync(path.join(own,'observe-only-business-failure.json'));fs.rmdirSync(own);fs.symlinkSync(foreign,own,'junction');assert.throws(()=>run(f),/REALPATH_ALIAS_OR_ESCAPE|REPARSE_ALIAS_OR_ESCAPE/);});
test('captured real receipts retain original bytes and negative while unmodified public gate accepts derived transfer',{skip:captureSkip},()=>{
 const f=fixture(true),originals=new Map([...f.contents].map(([rel])=>{const p=path.join(f.originalMap.canonical,rel);return[p,hash(fs.readFileSync(p))];}));
 const originalMapPath=path.join(f.originalCase,'css-appearance-worker','CSS-RAW-MAP.json');
 const validation=JSON.parse(f.contents.get(required[1]).toString('utf8').replace(/^\uFEFF/,''));
 assert.equal(validation.status,'PASS');assert.equal(Object.keys(validation.sourceHashes).length,3);
 // Original Workspace paths are outside this private case; the unchanged
 // public source gate must refuse all three before the explicit derivation.
 const expectedMismatches=Object.keys(validation.sourceHashes).sort();
 const prior=check(f,'css-appearance-worker/css/'+required[1]);assert.equal(prior.failed,true);assert.equal(prior.paths[0].sourceMismatches.length,3);assert.deepEqual(prior.paths[0].sourceMismatches.slice().sort(),expectedMismatches);
 const rows=run(f);const next=check(f,required[1]);assert.equal(next.failed,false);assert.equal(next.missing,false);assert.equal(rows.filter(x=>x.byteExact).length,3);
 const raw=JSON.parse(fs.readFileSync(path.join(f.canonical,dependencies[0])));assert.equal(raw.status,'FAIL');for(const[p,h]of originals)assert.equal(hash(fs.readFileSync(p)),h);
 assert.equal(hash(fs.readFileSync(originalMapPath)),capturedMapSha,'Original captured map must remain unchanged');
 fs.writeFileSync(path.join(root,'CAPTURED-TRANSFER-CONTROL.json'),JSON.stringify({schemaVersion:1,status:'TRANSFER_CONTROL_PASSED',ownedCase:f.caseDir,scope:'Seven genuine captured raw receipts/dependencies in a private derived seven-row map; original source read only; no CSS business/native/parent rerun.',originalMap:{path:originalMapPath,sha256:capturedMapSha,bytes:fs.statSync(originalMapPath).size},priorGate:prior,expectedSourceMismatches:expectedMismatches,derivedGate:next,forwarding:rows,rawBusinessStatus:raw.status,originalInputs:[...originals].map(([path,sha256])=>({path,sha256,bytes:fs.statSync(path).size})),newPublicRunExecuted:false},null,2)+'\n',{flag:'wx'});
});
test.after(()=>fs.writeFileSync(path.join(root,'CONTROL-SCOPE.json'),JSON.stringify({schemaVersion:1,root,syntheticRegistered:13,capturedRegistered:1,capturedRequested,capturedSource:capturedRequested?{case:capturedCase,mapSha256:capturedMapSha}:null,capturedSelectionAvailability:capturedRequested?'EXPLICIT_SOURCE_SUPPLIED':'NOT_RUN',noBusinessProducerExecuted:true},null,2)+'\n',{flag:'wx'}));
