import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import cp from 'node:child_process';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {runSemanticsCases} from './semantics-cases.mjs';

export const MODULE='tools/ymcc_sim/global/quick-app-file-icons-consumer';
const HERE=path.dirname(fileURLToPath(import.meta.url));
const FILES=['semantics-run.mjs','semantics-cases.mjs','adapter.mjs','semantics-spec.json','semantics-README.md'];
const AUTHORITY='tools/ymcc_sim/global/bundled-node-tool-authority.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const hash=p=>sha(fs.readFileSync(p));
const key=p=>path.resolve(p).toLowerCase();
const inside=(base,p)=>{const r=path.relative(path.resolve(base),path.resolve(p));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>fs.writeFileSync(p,typeof v==='string'?v:JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function noRedirect(p){const real=fs.realpathSync(p);assert.equal(key(real),key(p),'SOURCE_OR_OUTPUT_REDIRECTED');assert(!fs.lstatSync(p).isSymbolicLink(),'SOURCE_OR_OUTPUT_SYMLINK');return real;}
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(x=>{const p=path.join(root,x.name);assert(!x.isSymbolicLink(),'OWNED_OUTPUT_SYMLINK');return x.isDirectory()?walk(p):[p];});}

export async function execute({repoRoot,outDir}) {
  const began=Date.now(),repo=noRedirect(path.resolve(repoRoot)),out=path.resolve(outDir);
  assert(path.isAbsolute(outDir)&&!inside(repo,out)&&!fs.existsSync(out),'FRESH_OWNED_OUTPUT_OUTSIDE_SOURCE_REQUIRED');
  noRedirect(path.dirname(out)); fs.mkdirSync(out);
  const spec=read(path.join(HERE,'semantics-spec.json'));
  const prototype=key(HERE)!==key(path.join(repo,MODULE));
  const compilerRuns=[];
  const originalEnv=Object.fromEntries(['ESBUILD_WORKER_THREADS','ESBUILD_BINARY_PATH'].map(k=>[k,process.env[k]]));
  const originalChild=Object.fromEntries(['execFileSync','spawn','spawnSync','exec','execSync','execFile','fork'].map(k=>[k,cp[k]]));
  try {
    assert(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'AMBIENT_NODE_FORWARDING_REFUSED');
    assert.equal(spec.timeoutMs,45000,'ORIGINAL_45_SECOND_BUDGET_REQUIRED');
    assert.deepEqual(spec.allowedProductRequires,{quickAppIcons:['./quickAppDefaults'],quickAppDefaults:[]},'EXPLICIT_MEMORY_MODULE_CONTRACT_REQUIRED');
    // Import the existing authority from the selected source repository. A D-only
    // prototype executes that very implementation rather than copying a tool graph.
    const authorityFile=noRedirect(path.join(repo,AUTHORITY));
    const {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation,
      verifyBundledToolScope,bundledArtifacts}=await import(pathToFileURL(authorityFile).href);
    const authority=lockBundledNodeTools(repo),helpers=verifyBundledImplementation(repo);
    const inputs=[...new Set([...spec.sourceInputs,...spec.preservedLegacyFiles.map(f=>MODULE+'/'+f),...BUNDLED_AUTHORITY_SOURCES])].map(relative=>{
      const p=noRedirect(path.join(repo,relative));assert(inside(repo,p),'SOURCE_OUTSIDE_REPO');
      return {path:relative,sha256:hash(p),status:'OK'};
    });
    const own=FILES.map(name=>({path:prototype?path.join(HERE,name):MODULE+'/'+name,sha256:hash(noRedirect(path.join(HERE,name))),status:'OK'}));
    const manifestFile=path.join(repo,'.ymcc-source-snapshot/manifest.json');
    if(fs.existsSync(manifestFile)) {
      const pin=authority.tools.find(x=>key(x.path)===key(manifestFile));
      assert(pin&&pin.sha256===hash(manifestFile),'EXPLICIT_SNAPSHOT_MANIFEST_IDENTITY_REQUIRED');
      const manifest=read(manifestFile);assert.equal(key(manifest.snapshotRoot),key(repo));
      for(const row of [...inputs,...own])assert(manifest.files.some(x=>x.path===row.path&&x.sha256===row.sha256),'SOURCE_NOT_IN_SELECTED_MANIFEST:'+row.path);
    }
    const source={defaults:fs.readFileSync(path.join(repo,spec.defaultsSource),'utf8'),icons:fs.readFileSync(path.join(repo,spec.productSource),'utf8')};
    const view=fs.readFileSync(path.join(repo,'src/views/QuickAppView.vue'),'utf8');
    const component=fs.readFileSync(path.join(repo,'src/components/QuickAppIcon.vue'),'utf8');
    assert(view.includes('return quickAppIconKind(app);')&&view.includes('return quickAppIconVariant(app);')&&view.includes(':app-path="app.path"')&&component.includes('if (!path || isDefaultQuickAppPath(path)) return;'),'FORMAL_SEMANTICS_CONSUMER_NOT_FOUND');
    for(const fault of spec.faults)assert.equal(source[fault.source]?.split(fault.before).length,2,'EXACT_FAULT_ANCHOR_CHANGED:'+fault.id);
    write(out+'/inputs.json',{schemaVersion:1,prototypeOnly:prototype,sources:[...inputs,...own],authority,helpers,
      legacyCasesExecuted:0,actualUiConsumerExecuted:false,allowedProductRequires:spec.allowedProductRequires});
    process.env.ESBUILD_WORKER_THREADS='0';process.env.ESBUILD_BINARY_PATH=authority.compilerBinary;
    const forbidden=()=>{throw Error('QUICK_ICON_SEMANTICS_UNREVIEWED_CHILD_PROCESS_REFUSED');};
    for(const name of ['spawn','spawnSync','exec','execSync','execFile','fork'])cp[name]=forbidden;
    cp.execFileSync=function(file,args,options) {
      assert.equal(key(noRedirect(file)),key(authority.compilerBinary),'UNBOUND_COMPILER');
      assert(args.length===1&&args[0]==='--service='+authority.compilerVersion,'UNREVIEWED_COMPILER_ARGUMENTS');
      assert(options?.input,'COMPILER_PROTOCOL_INPUT_REQUIRED');
      const before=hash(file),row={path:file,sha256:before,args:[...args],inputSha256:sha(options.input),naturalExit:false};
      compilerRuns.push(row);
      try {const bytes=originalChild.execFileSync.call(cp,file,args,options);row.outputSha256=sha(bytes);row.naturalExit=true;row.exitCode=0;return bytes;}
      finally {assert.equal(hash(file),before,'COMPILER_CHANGED');}
    };
    const require=createRequire(path.join(repo,'package.json')),esbuild=require(authority.compilerEntry);
    assert.equal(esbuild.version,authority.compilerVersion);
    const steps=[];
    for(const selected of [{id:'positive',names:spec.caseNames},...spec.faults.map(f=>({...f,names:[f.case]}))]) {
      const stepRoot=path.join(out,selected.id);fs.mkdirSync(stepRoot);
      const current={...source};
      if(selected.before)current[selected.source]=current[selected.source].replace(selected.before,selected.after);
      const compiled={},compilation=[];
      // Transform both actual modules independently; no bundled/reimplemented defaults.
      for(const role of ['defaults','icons']) {
        const input=current[role],sourcefile=role==='defaults'?spec.defaultsSource:spec.productSource;
        write(path.join(stepRoot,role+'.input.ts'),input);
        const transformed=esbuild.transformSync(input,{loader:'ts',format:'cjs',target:'es2022',sourcefile,sourcemap:'external'});
        compiled[role]=transformed.code;
        write(path.join(stepRoot,role+'.cjs'),transformed.code);
        write(path.join(stepRoot,role+'.cjs.map'),transformed.map);
        compilation.push({role,sourcefile,inputSha256:sha(input),outputSha256:sha(transformed.code),compileSucceeded:true});
      }
      const observation={vmRuns:0,moduleGraphs:[]};
      const cases=runSemanticsCases(compiled,selected.names,observation),negative=Boolean(selected.before);
      const good=negative
        ? cases.length===1&&cases[0].status==='FAIL'&&cases[0].startupSucceeded&&cases[0].assertionName==='AssertionError'&&cases[0].error.includes(selected.expected)
        : cases.length===spec.caseNames.length&&cases.every(c=>c.status==='PASS'&&c.startupSucceeded);
      const row={label:selected.id,compileSucceeded:true,compilation,vmRuns:observation.vmRuns,moduleGraphs:observation.moduleGraphs,
        cases,expectedFailureDetected:negative?good:undefined,accepted:good,compileFailureCountsAsDetection:false,vmFailureCountsAsDetection:false};
      write(path.join(stepRoot,'raw-result.json'),row);steps.push(row);
    }
    const loaded=Object.keys(require.cache).map(p=>{
      const real=noRedirect(p),pin=authority.tools.find(row=>key(row.path)===key(real));
      assert(pin&&pin.sha256===hash(real),'ACTUAL_REQUIRED_TOOL_NOT_IN_BOUND_GRAPH:'+real);
      return {path:real,sha256:hash(real),status:'OK'};
    });
    assert.equal(compilerRuns.length,2*(1+spec.faults.length),'EXACT_COMPILER_PROCESS_COUNT');
    assert(compilerRuns.every(p=>p.naturalExit&&p.exitCode===0),'COMPILER_DID_NOT_EXIT_NATURALLY');
    assert(steps.every(s=>s.accepted),'SEMANTICS_CASE_OR_EXACT_NEGATIVE_FAILED');
    assert(Date.now()-began<spec.timeoutMs,'ORIGINAL_FINITE_OUTER_BUDGET_EXCEEDED');
    for(const row of inputs)assert.equal(hash(path.join(repo,row.path)),row.sha256,'SOURCE_CHANGED:'+row.path);
    for(const row of own)assert.equal(hash(prototype?row.path:path.join(repo,row.path)),row.sha256,'OWN_SOURCE_CHANGED:'+row.path);
    verifyBundledToolScope(authority,repo);
    write(out+'/compiler-processes.json',compilerRuns);
    write(out+'/tool-scope-after.json',{authority,helpers,actuallyRequiredTools:loaded});
    const outputs=walk(out).map(p=>({path:p,sha256:hash(p),status:'OK'}));
    const artifacts=bundledArtifacts(authority,[],outputs);
    assert(Date.now()-began<spec.timeoutMs,'ORIGINAL_FINITE_OUTER_BUDGET_EXCEEDED');
    const suite={schemaVersion:1,id:spec.id,status:prototype?'PROTOTYPE_PASS':'PASS',passed:!prototype,prototypeOnly:prototype,prototypePassed:true,
      caseNames:spec.caseNames,checks:spec.caseNames.length,semanticNegativeCount:spec.faults.length,legacyCasesExecuted:0,legacyCasesAdded:0,
      steps,sourceHashes:[...inputs,...own,...outputs],declaredToolAuthority:authority,mandatoryAuthorityHelpers:helpers,
      completeArtifactScope:artifacts,actuallyRequiredTools:loaded,compilerProcesses:compilerRuns,elapsedMs:Date.now()-began,originalBudgetMs:spec.timeoutMs,
      sourceApproval:false,inputConsumerStateFeedback:{input:'Own literal paths/names plus actual createDefaultQuickApps factory values',
        consumer:'Actual unchanged quickAppIcons.ts imports actual unchanged quickAppDefaults.ts',
        state:'Real initialized defaultIconPaths Set, normalized identity, ordered kind rules, selected variant and exported preview table',
        feedback:'Fixed path/name/kind/variant/preview assertions; two exact statement mutants must initialize and fail their original assertion'},
      boundary:{actualProductionTypeScriptExecuted:true,actualDefaultFactoryExecuted:true,actualUiComponentExecuted:false,
        allowedProductModules:['quickAppIcons','quickAppDefaults'],port:'Only the actual in-memory ./quickAppDefaults module; no substituted factory',
        realFilesystemIconRead:false,realNetwork:false,realBrowser:false,realProduct:false,realSdk:false,hardware:false},
      wholeGoalComplete:false,jointUserTestingReady:false,cpuPowerBenefit:null};
    write(out+'/result.json',suite);
    console.log(JSON.stringify({status:suite.status,checks:suite.checks,semanticNegativeCount:suite.semanticNegativeCount,
      legacyCasesExecuted:0,compilerProcesses:compilerRuns.length,elapsedMs:suite.elapsedMs,root:out}));
    return suite;
  } catch(error) {
    if(!fs.existsSync(out+'/result.json'))write(out+'/result.json',{schemaVersion:1,id:spec.id,status:'FAIL',passed:false,prototypeOnly:prototype,
      error:error.stack,compilerRuns,preparationFailureIsNotSemanticDetection:true,wholeGoalComplete:false});
    throw error;
  } finally {
    for(const [k,v] of Object.entries(originalChild))cp[k]=v;
    for(const [k,v] of Object.entries(originalEnv))if(v===undefined)delete process.env[k];else process.env[k]=v;
  }
}
if(process.argv[1]&&key(process.argv[1])===key(fileURLToPath(import.meta.url))) {
  try {
    const args=process.argv.slice(2);
    assert.equal(args.length,4,'EXACT_ARGUMENT_COUNT_REQUIRED');
    assert.deepEqual(args.filter((_,i)=>i%2===0),['--repo-root','--evidence-dir']);
    await execute({repoRoot:args[1],outDir:args[3]});
  } catch(error) {console.error(error.stack);process.exitCode=2;}
}
