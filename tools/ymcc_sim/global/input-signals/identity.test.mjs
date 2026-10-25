import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
import {sourceDependenciesResponseArgument,linkerResponseArguments} from './native-identity.mjs';
import {recordInputSetupFailure,finishInputConsumerIdentity} from './identity.mjs';
import {INPUT_MODULE,INPUT_REPO,getInputSourceInputs,getInputArtifactInputs,getInputToolDeclaration,captureInputDeclaration,authorizeInputFile,verifyInputDeclaration,parseInputLibraries,fileHash} from './inputs.mjs';

function fixture(t) {
  const repo=fs.mkdtempSync(path.join(process.env.INPUT_IDENTITY_TEST_ROOT??os.tmpdir(),'input-identity-contract-'));
  const module=path.join(repo,INPUT_MODULE);fs.mkdirSync(module,{recursive:true});
  const source=path.join(repo,'product.ts'),tool=path.join(repo,'tool.exe');
  fs.writeFileSync(source,'export const x=1;');fs.writeFileSync(tool,'fixture tool bytes; never executable');
  fs.writeFileSync(path.join(module,'source-inputs.json'),JSON.stringify({schemaVersion:1,kind:'controller-signal-reviewed-source-inputs',productSources:['product.ts'],directReads:[]}));
  fs.writeFileSync(path.join(module,'tool-inputs.json'),JSON.stringify({schemaVersion:1,kind:'controller-signal-reviewed-windows-tool-inputs',files:[{path:tool,sha256:fileHash(tool)}]}));
  return {repo,module,source,tool};
}
test('undeclared same-name files cannot inherit an approved compiler input',t=>{
  const f=fixture(t),d=captureInputDeclaration(f.repo),other=path.join(f.repo,'other','product.ts');
  fs.mkdirSync(path.dirname(other));fs.copyFileSync(f.source,other);
  assert.equal(authorizeInputFile(d,f.source,'compiler-read').path,'product.ts');
  assert.throws(()=>authorizeInputFile(d,other,'compiler-read'),/INPUT_UNDECLARED_OR_CHANGED_ACTUAL_INPUT/);
});
test('changed actual bytes and changed before-after source are rejected',t=>{
  const f=fixture(t),d=captureInputDeclaration(f.repo);fs.appendFileSync(f.source,'// changed');
  assert.throws(()=>authorizeInputFile(d,f.source,'compiler-read'),/INPUT_UNDECLARED_OR_CHANGED_ACTUAL_INPUT/);
  assert.throws(()=>verifyInputDeclaration(d),/INPUT_SOURCE_CHANGED_DURING_RUN/);
});
test('a changed pinned tool is rejected before compilation and at completion',t=>{
  const f=fixture(t),d=captureInputDeclaration(f.repo);fs.appendFileSync(f.tool,'changed');
  assert.throws(()=>captureInputDeclaration(f.repo),/INPUT_DECLARED_TOOL_CHANGED/);
  assert.throws(()=>verifyInputDeclaration(d),/INPUT_TOOL_CHANGED_DURING_RUN/);
});
test('manifest source traversal cannot escape the source tree',t=>{
  const f=fixture(t),file=path.join(f.module,'source-inputs.json'),spec=JSON.parse(fs.readFileSync(file));
  spec.directReads=['../outside.ts'];fs.writeFileSync(file,JSON.stringify(spec));
  assert.throws(()=>getInputSourceInputs(f.repo),/INPUT_SOURCE_DECLARATION_PATH/);
});
test('link evidence preserves duplicate search and reference order',()=>{
  const lib='C:/Build Tools/VC/lib/libcmt.lib';
  const rows=parseInputLibraries(Buffer.from('Searching '+lib+':\r\n  Loaded libcmt.lib(startup.obj)\r\nSearching '+lib+':\r\n'),Buffer.alloc(0));
  assert.deepEqual(rows.map(r=>[r.stream,r.line,r.role]),[['stdout',1,'actual-link-library-search'],['stdout',2,'actual-link-library-reference'],['stdout',3,'actual-link-library-search']]);
  assert(rows.every(row=>row.path===path.resolve(lib)));
});
test('unresolved and mismatched linker references fail closed',()=>{
  assert.throws(()=>parseInputLibraries(Buffer.from('Loaded unknown.lib(a.obj)\n'),Buffer.alloc(0)),/INPUT_UNRESOLVED_LINK_LIBRARY_LINE/);
  assert.throws(()=>parseInputLibraries(Buffer.from('Searching C:/VC/lib/a.lib:\n Loaded b.lib(a.obj)\n'),Buffer.alloc(0)),/INPUT_UNRESOLVED_LINK_LIBRARY_LINE/);
});
test('actual required omissions are declared without granting system IO',()=>{
  const inputs=getInputSourceInputs(),tools=getInputArtifactInputs();
  for(const relative of ['src/bridge/monitorNoticeHost.ts','native/msi_frontkey_observation_runtime.h','deps/json/json.hpp','native/main.cpp','tools/ymcc_sim/global/fan-log-regression.mjs'])assert(inputs.includes(relative),relative);
  assert(tools.some(file=>file.endsWith('node.exe')));assert(tools.some(file=>file.endsWith('esbuild.exe')));assert(tools.some(file=>file.endsWith('cl.exe')));assert(tools.some(file=>file.endsWith('link.exe')));
});

test('current Vue browser entry and its static runtime dependencies have pinned actual paths',()=>{
  const declaration=captureInputDeclaration(INPUT_REPO);
  const graph=JSON.parse(fs.readFileSync(path.join(INPUT_REPO,INPUT_MODULE,'node-packages.json')));
  const packages=new Map(graph.packages.map(p=>[p.name,p])),visited=new Set();
  function visit(name) {
    if(visited.has(name))return;visited.add(name);
    const pkg=packages.get(name);assert(pkg,'missing package '+name);
    const file=path.join(pkg.root,pkg.metadata.module);authorizeInputFile(declaration,file,'static-browser-runtime-input');
    const text=fs.readFileSync(file,'utf8');
    for(const item of text.matchAll(/\b(?:from\s*|import\s*)['"](@vue\/[^'"]+)['"]/g))visit(item[1]);
  }
  visit('vue');assert.deepEqual([...visited].sort(),['@vue/reactivity','@vue/runtime-core','@vue/runtime-dom','@vue/shared','vue']);
  verifyInputDeclaration(declaration);
});

test('owned unrefed compiler-service completion survives standalone top-level await',()=>{
  const serviceModule=new URL('./service-lifecycle.mjs',import.meta.url).href;
  const code=[
    "import {spawn} from 'node:child_process';",
    "import {stopInputCompilerServices} from "+JSON.stringify(serviceModule)+";",
    "const child=spawn(process.execPath,['-e','process.stdin.resume()'],{stdio:['pipe','pipe','pipe'],windowsHide:true});",
    "const terminal=new Promise(resolve=>child.once('close',(exitCode,signal)=>{console.log(JSON.stringify({pid:child.pid,closeObserved:true,exitCode,signal}));resolve();}));",
    "child.unref();child.stdin.unref?.();child.stdout.unref?.();child.stderr.unref?.();",
    "await stopInputCompilerServices([child],()=>{child.stdin.destroy();child.stdout.destroy();child.stderr.destroy();child.kill();},[terminal]);"
  ].join('\n');
  const stdout=execFileSync(process.execPath,['--input-type=module','--eval',code],{windowsHide:true,encoding:'utf8',timeout:10000});
  const record=JSON.parse(stdout);assert(record.pid>0);assert.equal(record.closeObserved,true);
  if(process.env.INPUT_IDENTITY_TEST_ROOT)fs.writeFileSync(path.join(process.env.INPUT_IDENTITY_TEST_ROOT,'service-lifecycle-node.json'),JSON.stringify(record,null,2)+'\n');
});

test('MSVC response option keeps sourceDependencies and its quoted path on one physical line',()=>{
 const argument=sourceDependenciesResponseArgument('D:/input proof/native/source-dependencies.json');
 assert.equal(argument,'/sourceDependencies "D:/input proof/native/source-dependencies.json"');
 assert.equal(argument.split(/\r?\n/).length,1);
 for(const file of ['D:/bad\nname.json','D:/bad"name.json','D:/bad%name.json'])assert.throws(()=>sourceDependenciesResponseArgument(file),/INPUT_UNSAFE_BUILD_PATH/);
});
test('compiler prerequisite failure remains primary with identity error secondary and all cases retained',t=>{
 const f=fixture(t),declaration=captureInputDeclaration(f.repo),directory=path.join(f.repo,'evidence');fs.mkdirSync(directory);
 const processRecord={pid:45436,exitCode:2,error:null,signal:null,killed:false,outputTruncated:false};
 fs.writeFileSync(path.join(directory,'native-compile.process.json'),JSON.stringify(processRecord));
 fs.writeFileSync(path.join(directory,'native-compile.stderr.log'),'cl: error D8004: /sourceDependencies requires an argument');
 fs.writeFileSync(path.join(directory,'native-compile.stdout.log'),'');
 const session={directory,declaration,product:{identity:{status:'PASS'}},native:null,nativeExecutions:[]};
 const primary=recordInputSetupFailure(session,Error('INPUT_NATIVE_COMPILE_FAILED'));
 assert.equal(recordInputSetupFailure(session,Error('LATER_WRAPPER_ERROR')),primary);
 const cases=Array.from({length:26},(_,i)=>({id:'preserved-case-'+i,status:'FAIL',code:'INPUT_NATIVE_COMPILE_FAILED',execution:{phase:'setup',semanticExecuted:false}}));
 const result=finishInputConsumerIdentity(session,{status:'FAIL',summary:{total:26,passed:0,failed:26},sourceHashes:[],inputConsumerStateFeedback:cases});
 assert.equal(result.status,'FAIL');assert.equal(result.code,'INPUT_NATIVE_COMPILE_FAILED');
 assert.equal(result.failure.primary.nativeCompile.value.exitCode,2);
 assert.match(result.failure.primary.compilerStderr.text,/D8004/);
 assert(result.failure.secondary.some(row=>row.code==='INPUT_COMPILATION_EVIDENCE_MISSING'));
 assert.equal(result.inputIdentity.status,'FAIL');assert.equal(result.inputConsumerStateFeedback,cases);
 assert.deepEqual(result.executionSummary,{prerequisiteBlocked:26,semanticStarted:0});
 assert(result.sourceHashes.some(row=>row.path===path.join(directory,'native-compile.stderr.log')));
});
test('identity failure cannot approve a result whose scenario status was PASS',t=>{
 const f=fixture(t),declaration=captureInputDeclaration(f.repo),directory=path.join(f.repo,'evidence');fs.mkdirSync(directory);
 const session={directory,declaration,product:null,native:null,nativeExecutions:[]};
 const result=finishInputConsumerIdentity(session,{status:'PASS',summary:{total:1,passed:1,failed:0},sourceHashes:[],inputConsumerStateFeedback:[{id:'original',status:'PASS'}]});
 assert.equal(result.status,'FAIL');assert.equal(result.inputIdentity.status,'FAIL');
 assert.equal(result.code,'INPUT_COMPILATION_EVIDENCE_MISSING');
});

test('MSVC response linker marker and its options share one physical line',()=>{
 assert.equal(linkerResponseArguments(),'/link /verbose:lib');
 assert.equal(linkerResponseArguments().split(/\r?\n/).length,1);
});
