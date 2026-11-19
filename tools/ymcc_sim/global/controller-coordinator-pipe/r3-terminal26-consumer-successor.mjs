// Strict read-only terminal26 post-compile consumer source successor.
// Historical native receipts and every other tool pin remain authoritative.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {gunzipSync} from 'node:zlib';
const CONTRACT='current105-terminal26-sdk-lifetime-compact-v1';
const HERE=path.dirname(fileURLToPath(import.meta.url));
const EXPECTED=[
  {
    "name": "r3-terminal22-qualified-observe.mjs",
    "bytes": 46288,
    "sha256": "340d37c3df75ae1f99eb656173814168cd4341fcac7f9fc2899154e9ecdf74cb",
    "archive": {
      "bytes": 11817,
      "sha256": "fd49a38ae65db96eafe8b64b0364347cf2a57f07f1c46d4b00ca434131a6a1e0"
    }
  },
  {
    "name": "r3-terminal26-tuple-expectations.json",
    "bytes": 43690,
    "sha256": "d9ac72f8659f2ebeab3ea6bbb61c44efd99d9d0b94fce59bc77f3a5aded8f0d2",
    "archive": {
      "bytes": 5231,
      "sha256": "6350f4f1fa0b81a30f00ce72bb2270cadc487bc6abe26da8431e3e7859ca0612"
    }
  },
  {
    "name": "r3-terminal26-switch-contract.mjs",
    "bytes": 8146,
    "sha256": "185d638dbd99f848d7c328fb77958927b5f568e2dc87f114cfe6c008d8892226",
    "archive": {
      "bytes": 3105,
      "sha256": "e752a054395cb368ff596e28537cee45b1df4a9f5eb73d283749c9aa9861f1ed"
    }
  }
];
const TUPLE_SUCCESSORS=[
  {
    "id": "ordinary-switch-ports-msi-cleanup-r2-20261115",
    "inertSha256": "e5c42c7fe7c2aa539038e2854a89e8fe312e41bf591a162c4e905b5b34de6198",
    "productSourceSuccessorId": "terminal-cleanup-handoff-20261115",
    "productHeaderSuccessorId": "msi-quarantine-cleanup-r2-20261115",
    "headerRelative": "native/msi_native_foreground_isolation_runtime.h",
    "compiledHeaderRelative": "source/native/msi_native_foreground_isolation_runtime.reviewed.h",
    "compiledHeaderSha256": "e6a9ef846f9629c664d2cd7a628be417281a13285d5f67a63cad263035055944",
    "compiledHeaderBytes": 155556
  }
];
const OBSERVER_HUNKS=[
  {
    "label": "trusted-readonly-helper-import",
    "before": "import fs from 'node:fs';",
    "after": "import {terminal26ConsumerToolSuccessor} from './r3-terminal26-consumer-successor.mjs';\nimport fs from 'node:fs';"
  },
  {
    "label": "explicit-optional-predecessor-argument",
    "before": "['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms']",
    "after": "['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms','--consumer-predecessors']"
  },
  {
    "label": "native-only-predecessor-argument",
    "before": " if(options['--lifecycle-preparations'])for(const value of Object.values(options))",
    "after": " // TERMINAL26_CONSUMER_SUCCESSOR_BEGIN parser\n if(options['--consumer-predecessors']!==undefined){\n  assert(options['--native-preparation']&&!options['--lifecycle-preparations'],'consumer predecessor proof requires one native preparation');\n  assert(path.isAbsolute(options['--native-preparation'])&&!/[\\0\\r\\n]/.test(options['--native-preparation']),'absolute native preparation required for consumer predecessor proof');\n  assert(path.isAbsolute(options['--consumer-predecessors'])&&!/[\\0\\r\\n]/.test(options['--consumer-predecessors']),'absolute consumer predecessor declaration required');\n }\n // TERMINAL26_CONSUMER_SUCCESSOR_END parser\n if(options['--lifecycle-preparations'])for(const value of Object.values(options))"
  },
  {
    "label": "verify-exact-post-compile-source",
    "before": "const manifest=read(n.native.sourceManifest.path),managed=read(preparation.managed.manifestPath);",
    "after": "const manifest=read(n.native.sourceManifest.path),managed=read(preparation.managed.manifestPath);\n// TERMINAL26_CONSUMER_SUCCESSOR_BEGIN binding\nconst postCompileConsumers=options['--consumer-predecessors']===undefined?null:terminal26ConsumerToolSuccessor(n,path.resolve(options['--consumer-predecessors']));\nassert(!postCompileConsumers||observedContract.signalEvidence===true,'explicit26 consumer source successor required');\n// TERMINAL26_CONSUMER_SUCCESSOR_END binding"
  },
  {
    "label": "runtime-helper-in-actual-control-closure",
    "before": "const controlFiles=[fileURLToPath(import.meta.url),",
    "after": "const controlFiles=[fileURLToPath(import.meta.url),fileURLToPath(new URL('./r3-terminal26-consumer-successor.mjs',import.meta.url)),"
  },
  {
    "label": "separate-compiled-and-current-consumer-epochs",
    "before": "for(const row of [...manifest.compiledGenerated,...n.batchAfter.actualTools,...managed.evaluatedCompilerInputs,...managed.runtimeArtifacts])addPin(row);",
    "after": "// TERMINAL26_CONSUMER_SUCCESSOR_BEGIN merge\nconst actualCompileTools=postCompileConsumers?postCompileConsumers.currentTools:n.batchAfter.actualTools;\nfor(const row of [...manifest.compiledGenerated,...actualCompileTools,...managed.evaluatedCompilerInputs,...managed.runtimeArtifacts])addPin(row);\nif(postCompileConsumers)for(const row of postCompileConsumers.sourceInputs)addPin(row);\n// TERMINAL26_CONSUMER_SUCCESSOR_END merge"
  },
  {
    "label": "persist-both-real-source-epochs",
    "before": "\nconst resultFile=path.join(root,'OBSERVATION.json'),save=",
    "after": "\n// TERMINAL26_CONSUMER_SUCCESSOR_BEGIN result\nif(postCompileConsumers)result.postCompileConsumerSuccessor={kind:postCompileConsumers.kind,legacyConsumers:postCompileConsumers.legacyConsumers,currentConsumers:postCompileConsumers.currentConsumers,sourceProofs:postCompileConsumers.sourceProofs,compilerReceiptsUnmodified:true};\n// TERMINAL26_CONSUMER_SUCCESSOR_END result\n\nconst resultFile=path.join(root,'OBSERVATION.json'),save="
  }
];

const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
function plain(file){
 assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'absolute plain consumer input required');
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  const stat=fs.lstatSync(cursor);assert(!stat.isSymbolicLink(),'consumer reparse input rejected');
  assert.equal(key(fs.realpathSync(cursor)),key(cursor),'consumer redirected input rejected');
  if(path.dirname(cursor)===cursor)break;
 }
 assert(fs.lstatSync(file).isFile(),'regular consumer input required');return path.resolve(file);
}
function keys(value,names,label){assert(value&&typeof value==='object'&&!Array.isArray(value),label);assert.deepEqual(Object.keys(value).sort(),names.slice().sort(),label);}
function replaceOnce(text,before,after,label){
 const start=text.indexOf(before);assert(start>=0&&text.indexOf(before,start+before.length)===-1,'unique source span required: '+label);
 return text.slice(0,start)+after+text.slice(start+before.length);
}
function span(text,start,end){
 const a=text.indexOf(start);assert(a>=0&&text.indexOf(start,a+start.length)===-1,'unique source start required');
 const b=text.indexOf(end,a+start.length);assert(b>a,'exact source end required');return text.slice(a,b);
}
function observerProof(before,current){
 // Only these two recorded byte ranges change CR to LF; all other EOLs stay exact.
 let base=Buffer.from(before);for(const [start,end]of [[15718,16950],[33513,34109]]){
  const fixed=Buffer.from(base.subarray(start,end).toString('utf8').replace(/\r/g,'\n'));
  assert.equal(fixed.length,end-start,'recorded EOL range length changed');fixed.copy(base,start);
 }
 assert.equal(digest(base),'c9d7c6ce74c5259ee8649499ed76a44ac3ccc9a2e1c4f9c7129e8116d3c23da6','recorded EOL-only successor required');
 const begin='export function terminal26CompiledTupleSource(n,source){',end='\nexport function terminal26PostOnlyConsumerBindings(legacyFiles){';
 const old=span(base.toString('utf8'),begin,end),now=span(current,begin,end);
 assert.equal(digest(old),'3096fa74115fbc76783532610e176ab268d2304cbbce16c03169a97642c8b1e1','original tuple source function changed');
 assert.equal(digest(now),'304fe06d760dd224a713e2ae5ae477cd77728bc22df25b07f5e479a9168b0487','reviewed tuple source function changed');
 let expected=replaceOnce(base.toString('utf8'),old,now,'reviewed tuple source function');
 assert.equal(digest(expected),'b1f8cc96a5f8365d2cae7290295f0e3b548692a0eb7315be1dd243408da926d5','tuple source successor remainder mismatch');
 for(const h of OBSERVER_HUNKS)expected=replaceOnce(expected,h.before,h.after,h.label);
 assert.equal(current,expected,'observer outside exact reviewed source changes differs');
 let inverse=current;for(const h of [...OBSERVER_HUNKS].reverse())inverse=replaceOnce(inverse,h.after,h.before,h.label);
 assert.equal(digest(inverse),'b1f8cc96a5f8365d2cae7290295f0e3b548692a0eb7315be1dd243408da926d5','observer integration inverse mismatch');
 return {method:'EXACT_TWO_EOL_SPANS_AND_PINNED_TUPLE_FUNCTION_PLUS_SEVEN_EXACT_INVERTIBLE_HUNKS',eolByteRanges:[[15718,16950],[33513,34109]],tupleFunctionSha256:digest(now),integrationHunks:OBSERVER_HUNKS.map(h=>({label:h.label,beforeSha256:digest(h.before),afterSha256:digest(h.after)})),wholeRemainderByteEqual:true,inverseVerified:true};
}
function switchProof(before,current){
 const a='function readbackTargets(c){',b='function cleanupAwaiting(c){';
 const old=span(before,a,b),now=span(current,a,b);
 assert.equal(digest(now),'acd8009508e4b114e314b2f7048090016e72a9893bd5a70c62412b54b9002df6','reviewed readback evidence consumer required');
 assert.equal(replaceOnce(current,now,old,'readback evidence consumer'),before,'switch source outside readback evidence consumer changed');
 return {method:'EXACT_READBACK_TARGETS_SOURCE_REPLACEMENT',functionSha256:digest(now),wholeRemainderByteEqual:true,eolNormalization:'NONE'};
}
export function terminal26ConsumerToolSuccessor(n,predecessorDeclarationFile){
 const inputs=new Map();
 function load(file,expected){
  file=plain(file);const fd=fs.openSync(file,'r');let bytes;
  try{const first=fs.fstatSync(fd);bytes=fs.readFileSync(fd);const last=fs.fstatSync(fd);assert(first.isFile()&&last.isFile()&&first.ino===last.ino&&first.dev===last.dev&&first.size===last.size&&first.mtimeMs===last.mtimeMs&&first.ctimeMs===last.ctimeMs&&bytes.length===last.size,'consumer input changed while reading');}finally{fs.closeSync(fd);}
  const pin={path:file,bytes:bytes.length,sha256:digest(bytes)};
  if(expected){assert.equal(pin.sha256,expected.sha256,'consumer input digest mismatch: '+file);if(expected.bytes!==undefined)assert.equal(pin.bytes,expected.bytes,'consumer input size mismatch: '+file);}
  const old=inputs.get(key(file));assert(!old||old.sha256===pin.sha256&&old.bytes===pin.bytes,'consumer input changed between reads');inputs.set(key(file),pin);return bytes;
 }
 const read=(file,expected)=>JSON.parse(load(file,expected).toString('utf8').replace(/^\uFEFF/,''));
 const original=structuredClone(n);
 assert(n?.batchAfter?.status==='AUTHORITY_STABLE'&&n.batchAfter.source?.snapshotIntegrityVerified===true&&n.batchAfter.source.creationStatus==='PASS'&&n.batchAfter.fullSourceVerificationCalls===2,'stable actual source authority and two source verifications required');
 assert(n?.schemaVersion===1&&n.status==='NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED'&&n.managedInputContract===CONTRACT&&n.managedAdmissions===26,'explicit managed26 native preparation required');
 assert(n.role==='positive'&&n.mutation===null,'positive unmutated consumer source role required');
 assert(n.nativeExecuted===false&&n.publicRuntimeAccepted===false&&n.normalProductMainExecuted===false&&n.realSdkLoaded===false&&n.deviceIo===false&&Array.isArray(n.errors)&&n.errors.length===0,'unexecuted compiled native preparation required');
 assert(n.native?.status==='COMPILED_NOT_EXECUTED'&&n.native.compileOnly===true&&n.native.mutation===null&&n.native.compilerExitCode===0&&n.native.compilerSignal===null&&n.native.compilerError===null&&n.native.sourceFullTranslationUnit===true&&n.native.fixtureExecuted===false&&n.native.nativeExecuted===false&&n.native.runBranchReached===false&&Array.isArray(n.native.changed)&&n.native.changed.length===0,'stable successful native compile role required');
 const nativeDir=path.dirname(plain(n.native.exe.path)),nativeFile=path.join(path.dirname(nativeDir),'NATIVE-PREPARATION.json');
 assert.deepEqual(read(nativeFile),n,'actual native preparation required');
 assert.deepEqual(read(path.join(nativeDir,'COMPILE-ONLY.json')),n.native,'actual compile-only receipt required');
 load(n.native.exe.path,n.native.exe);load(n.native.buildScript.path,n.native.buildScript);
 const processReceipt=read(n.native.compilerProcess.path,n.native.compilerProcess);assert(processReceipt.exitCode===0,'actual compiler success required');
 const source=read(n.native.sourceManifest.path,n.native.sourceManifest),ledger=read(n.compilerLedger.path,n.compilerLedger);
 assert(Array.isArray(source.records)&&Array.isArray(source.compiledGenerated),'actual native source manifest arrays required');
 assert(ledger.schemaVersion===1&&ledger.status==='ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED'&&ledger.method==='ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE'&&Array.isArray(ledger.inputs)&&ledger.inputs.length===n.actualObservedCompilerInputs&&ledger.inputs.length>0,'actual observed compiler ledger required');
 assert.equal(key(ledger.sourceManifest.path),key(n.native.sourceManifest.path));assert.equal(ledger.sourceManifest.sha256,n.native.sourceManifest.sha256);
 assert.equal(key(ledger.buildScript.path),key(n.native.buildScript.path));assert.equal(ledger.buildScript.sha256,n.native.buildScript.sha256);
 load(ledger.buildScript.path,ledger.buildScript);load(ledger.buildLog.path,ledger.buildLog);
 assert(Array.isArray(ledger.explicitTranslationUnits)&&ledger.explicitTranslationUnits.length===3&&Array.isArray(ledger.observedIncludePaths)&&ledger.observedIncludePaths.length>0,'actual three translation unit ledger required');
 const preparation=read(n.preparation.path,n.preparation),layout=read(preparation.layout.path,preparation.layout);
 assert(preparation.status==='MANAGED26_COMPILED_NOT_RUNTIME_ACCEPTED'&&preparation.managedInputContract===CONTRACT&&preparation.managedMemberCount===26&&preparation.managed?.semanticMutation===null&&preparation.managed.sourceStable===true&&preparation.managed.toolchainStable===true,'paired stable managed26 preparation required');
 assert(layout.managedInputContract===CONTRACT&&Array.isArray(layout.expectedManagedSources)&&layout.expectedManagedSources.length===26&&key(layout.sourceRoot)===key(n.sourceRoot)&&key(preparation.sourceRoot)===key(n.sourceRoot),'actual managed26 source layout required');
 const managed=read(preparation.managed.manifestPath,{sha256:preparation.managed.manifestSha256});
 assert(Array.isArray(managed.inputBindings?.sources)&&managed.inputBindings.sources.length===26,'actual managed26 source bindings required');
 assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources,'managed26 ordered source membership changed');
 assert.equal(key(n.native.managedProof.manifest),key(preparation.managed.manifestPath));assert.equal(n.native.managedProof.manifestSha256,preparation.managed.manifestSha256);
 const declaration=read(predecessorDeclarationFile);keys(declaration,['schemaVersion','kind','files'],'strict predecessor declaration required');
 assert(declaration.schemaVersion===1&&declaration.kind==='TERMINAL26_POST_COMPILE_CONSUMER_PREDECESSORS_V1'&&Array.isArray(declaration.files)&&declaration.files.length===3,'exact three consumer predecessors required');
 assert.deepEqual(declaration.files.map(row=>row?.name).sort(),EXPECTED.map(row=>row.name).sort(),'unknown missing or duplicate predecessor role');
 assert(Array.isArray(n.fixtureToolImplementation?.localReviewedToolFiles)&&Array.isArray(n.batchAfter?.actualTools),'actual reviewed and batch tool rows required');
 const currentConsumers=[],legacyConsumers=[],proofs=[];
 for(const fixed of EXPECTED){
  const row=declaration.files.find(row=>row.name===fixed.name);keys(row,['name','path','bytes','sha256','archive'],'strict predecessor role required');keys(row.archive,['path','bytes','sha256'],'strict predecessor archive pin required');
  assert.equal(row.bytes,fixed.bytes,'fixed predecessor size required');assert.equal(row.sha256,fixed.sha256,'fixed predecessor digest required');
  assert.equal(row.archive.bytes,fixed.archive.bytes);assert.equal(row.archive.sha256,fixed.archive.sha256);
  const before=load(row.path,fixed),archive=load(row.archive.path,fixed.archive);assert(gunzipSync(archive).equals(before),'predecessor archive content differs');
  const currentFile=path.join(HERE,fixed.name),current=load(currentFile),currentPin=inputs.get(key(currentFile));
  for(const rows of [n.fixtureToolImplementation.localReviewedToolFiles,n.batchAfter.actualTools]){
   const matching=rows.filter(tool=>key(tool.path)===key(currentFile));assert.equal(matching.length,1,'unique original consumer tool row required');
   assert.equal(matching[0].bytes,fixed.bytes,'original consumer tool size differs');assert.equal(matching[0].sha256,fixed.sha256,'original consumer tool digest differs');
   assert.equal(rows.filter(tool=>path.basename(tool.path).toLowerCase()===fixed.name.toLowerCase()).length,1,'ambiguous consumer tool role');
  }
  legacyConsumers.push({name:fixed.name,path:path.resolve(row.path),bytes:fixed.bytes,sha256:fixed.sha256,originalToolPath:currentFile});currentConsumers.push({name:fixed.name,...currentPin});
  if(fixed.name==='r3-terminal26-tuple-expectations.json'){
   const old=JSON.parse(before.toString('utf8')),now=JSON.parse(current.toString('utf8'));
   assert(!Object.hasOwn(old.sourceBindings,'inertSuccessors'),'old tuple contract unexpectedly has successor');assert.deepEqual(now.sourceBindings.inertSuccessors,TUPLE_SUCCESSORS,'only exact reviewed tuple successor allowed');
   delete now.sourceBindings.inertSuccessors;assert.deepEqual(now,old,'tuple contract outside sole sourceBindings.inertSuccessors changed');proofs.push({name:fixed.name,method:'DEEP_EQUAL_EXCEPT_EXACT_SINGLE_INERT_SUCCESSOR_PROFILE',wholeRemainderDeepEqual:true});
  }else if(fixed.name==='r3-terminal26-switch-contract.mjs')proofs.push({name:fixed.name,...switchProof(before.toString('utf8'),current.toString('utf8'))});
  else proofs.push({name:fixed.name,...observerProof(before,current.toString('utf8'))});
 }
 // Reject content identities too: renaming a consumer cannot turn it into a compiler input.
 function noConsumer(value,label){
  if(typeof value==='string')assert(!EXPECTED.some(row=>path.basename(value).toLowerCase()===row.name.toLowerCase()||value===row.sha256)&&!currentConsumers.some(row=>value===row.sha256),'post-compile consumer appears in compiled inputs: '+label);
  else if(Array.isArray(value))for(const item of value)noConsumer(item,label);
  else if(value&&typeof value==='object')for(const item of Object.values(value))noConsumer(item,label);
 }
 for(const [label,rows]of [['compiler.inputs',ledger.inputs],['compiler.observedIncludes',ledger.observedIncludePaths],['compiler.translationUnits',ledger.explicitTranslationUnits],['manifest.records',source.records],['manifest.compiledGenerated',source.compiledGenerated],['managed.evaluatedCompilerInputs',managed.evaluatedCompilerInputs]]){
  assert(Array.isArray(rows),'compiled source graph array required: '+label);noConsumer(rows,label);
 }
 load(fileURLToPath(import.meta.url));
 const replacements=new Map(currentConsumers.map(({name,...pin})=>[key(pin.path),pin]));
 const currentTools=n.batchAfter.actualTools.map(row=>replacements.has(key(row.path))?{...structuredClone(row),...replacements.get(key(row.path))}:structuredClone(row));
 assert.deepEqual(n,original,'native preparation mutated during consumer verification');
 for(const pin of [...inputs.values()])load(pin.path,pin);
 return {kind:'TERMINAL26_EXACT_POST_COMPILE_CONSUMER_SUCCESSOR_V1',currentTools,sourceInputs:[...inputs.values()],legacyConsumers,currentConsumers,sourceProofs:{consumers:proofs,compilerInputCount:ledger.inputs.length,compiledConsumersAbsent:true,actualNativePreparation:inputs.get(key(nativeFile)),sourceManifest:inputs.get(key(n.native.sourceManifest.path)),compilerLedger:inputs.get(key(n.compilerLedger.path)),allInputsRechecked:true,compilerReceiptsUnmodified:true,nativeRuntimeAccepted:false},wholeGoalComplete:false};
}
