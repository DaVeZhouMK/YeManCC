/** GPU-local current execution authority plus immutable historical tool obligations. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {lockBundledNodeTools,verifyBundledToolScope,classifyBundledInput,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
import {captureSnapshotNodeToolGraph} from '../snapshot-node-tools.mjs';
import {loadUiToolDeclaration,verifyUiToolDeclaration} from '../ui-navigation/tool-inputs.mjs';
import {parseInputLibraries} from '../input-signals/inputs.mjs';
export const GPU_MODULE='tools/ymcc_sim/global/gpu-memory-signals';
export const GPU_SCOPE_SOURCES=[...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/input-signals/inputs.mjs','tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs',...['tool-scope.mjs','tool-inputs.json','observe.cjs','browser-tool-inputs.json','browser-runtime.mjs','output-evidence.mjs'].map(f=>GPU_MODULE+'/'+f)];
export const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export function verificationRead(action){const flag=Symbol.for('ymcc.gpu.verification'),previous=globalThis[flag];globalThis[flag]=true;try{return action();}finally{globalThis[flag]=previous;}}
export const hash=file=>verificationRead(()=>digest(fs.readFileSync(file)));
export const key=file=>path.resolve(file).toLowerCase();
export const need=(ok,code)=>{if(!ok)throw Error('GPU_SCOPE_'+code);};
export const inside=(root,file)=>{const rel=path.relative(root,file);return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
export const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function sourcePath(repo,relative){
 need(typeof relative==='string'&&!path.isAbsolute(relative)&&relative!==''&&!relative.includes('\\')&&!/[:\0\r\n]/.test(relative)&&!relative.split('/').some(p=>['','..','.','node_modules'].includes(p.toLowerCase())),'SOURCE_RELATIVE_PATH_REQUIRED');
 const root=fs.realpathSync(repo),file=path.resolve(root,relative),real=fs.realpathSync(file);
 need(inside(root,real)&&key(file)===key(real)&&fs.statSync(real).isFile(),'SOURCE_ESCAPE_OR_ALIAS');return real;
}
export function uniqueTools(rows){
 const map=new Map();for(const row of rows){need(path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'TOOL_ROW');const k=key(row.path),old=map.get(k);need(!old||old.sha256===row.sha256,'TOOL_CONFLICT');map.set(k,{path:path.resolve(row.path),sha256:row.sha256,status:'OK'});}
 need(map.size<=3072,'ARTIFACT_BUDGET');return [...map.values()].sort((a,b)=>a.path.localeCompare(b.path));
}
export function exactFile(row){need(key(fs.realpathSync(row.path))===key(row.path)&&hash(row.path)===row.sha256,'TOOL_CHANGED_OR_ALIAS:'+row.path);return row;}
// Historical pins describe original bytes; they do not select this run's Node authority.
export function currentGpuNodeGraph(repo,authority){
 need(key(authority.repoRoot)===key(fs.realpathSync(repo)),'NODE_AUTHORITY_REPO_FORWARDING');
 const captured=authority.mode==='snapshot-binding'?{graph:read(authority.proof.bindingManifestFile).graph}:captureSnapshotNodeToolGraph({primaryRepo:repo});
 need(digest(JSON.stringify(captured.graph))===authority.graphSha256,'CURRENT_NODE_GRAPH_CHANGED');return captured.graph;
}
export function gpuNodeAliases(repo,authority,graph){
 need(key(authority.repoRoot)===key(fs.realpathSync(repo))&&digest(JSON.stringify(graph))===authority.graphSha256,'NODE_ALIAS_AUTHORITY_FORWARDING');
 return graph.packages.flatMap(pkg=>pkg.files.map(row=>{const target=path.resolve(pkg.root,row.path);need(authority.tools.some(tool=>key(tool.path)===key(target)&&tool.sha256===row.sha256),'CURRENT_NODE_ALIAS_MEMBER_MISSING');return {path:path.resolve(repo,'node_modules',pkg.name,row.path),target,sha256:row.sha256};}));
}
export function historicalGpuNode(pin,authority,graph){
 exactFile({path:pin.bindingManifestFile,sha256:pin.bindingManifestSha256});const saved=read(pin.bindingManifestFile),manifestFile=path.join(pin.repoRoot,'.ymcc-source-snapshot/manifest.json');
 need(saved.schemaVersion===1&&saved.kind==='ymcc-snapshot-node-tools-binding'&&saved.status==='PASS'&&saved.sourceApprovalGranted===false&&saved.executionPerformed===false,'HISTORICAL_BINDING_SCHEMA');
 need(key(saved.snapshotRoot)===key(pin.repoRoot)&&key(saved.bindingManifestFile)===key(pin.bindingManifestFile)&&key(saved.snapshotManifestFile)===key(manifestFile),'HISTORICAL_BINDING_ROOT');
 exactFile({path:manifestFile,sha256:saved.expectedSnapshotManifestSha256});
 need(saved.graphBeforeSha256===pin.graphSha256&&saved.graphAfterSha256===pin.graphSha256&&digest(JSON.stringify(saved.graph))===pin.graphSha256,'HISTORICAL_GRAPH_CHANGED');
 need(saved.graph.packageCount===26&&saved.graph.fileCount===749&&authority.packageCount===26&&authority.fileCount===749,'FIXED_NODE_GRAPH');
 // Compare full membership, directories, links, resolutions and edges, not just a digest/count.
 need(digest(JSON.stringify(graph))===authority.graphSha256&&JSON.stringify(graph)===JSON.stringify(saved.graph),'HISTORICAL_CURRENT_GRAPH_DIFFERENT');
 const packages=saved.graph.packages.flatMap(pkg=>pkg.files.map(row=>({path:path.resolve(pkg.root,row.path),sha256:row.sha256})));
 need(packages.length===749&&new Set(packages.map(row=>key(row.path))).size===749,'HISTORICAL_PACKAGE_MEMBERSHIP');
 for(const row of packages)need(authority.tools.some(tool=>key(tool.path)===key(row.path)&&tool.sha256===row.sha256),'HISTORICAL_ACTUAL_PACKAGE_MEMBER_MISSING:'+row.path);
 const originalVerifier=saved.verifierSource;need(originalVerifier&&originalVerifier.sha256===saved.verifierCodeSha256&&inside(saved.sourceRoot,originalVerifier.path),'HISTORICAL_VERIFIER_IDENTITY');
 const relative=path.relative(saved.sourceRoot,originalVerifier.path).replaceAll('\\','/'),archivedVerifier={path:sourcePath(pin.repoRoot,relative),sha256:originalVerifier.sha256};exactFile(archivedVerifier);
 // The frozen copy discharges the old helper byte obligation; current source/helper verification stays independent.
 const metadata=[{path:pin.bindingManifestFile,sha256:pin.bindingManifestSha256},{path:manifestFile,sha256:saved.expectedSnapshotManifestSha256},saved.graph.primaryPackageJson];
 const node=authority.tools.find(row=>key(row.path)===key(authority.nodeExecutable));need(node,'NODE_EXECUTABLE');
 const originalTools=[...packages,...metadata,originalVerifier,node],tools=[...metadata,archivedVerifier];
 return {originalTools,tools,proof:{bindingManifestFile:pin.bindingManifestFile,bindingManifestSha256:pin.bindingManifestSha256,graphSha256:pin.graphSha256,packageCount:26,fileCount:749,fullGraphEqual:true,originalVerifier,archivedVerifier,meaning:'Historical static obligations only; current selected-repo authority is independently verified; no source approval.'}};
}
/** Forward only the already verified selected-repo Node binding to owned children. */
export function gpuChildEnvironment(scope,base={}){
 const env={...base};const fileKey='YMCC_BATTERY_UI_NODE_TOOLS_BINDING',shaKey='YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256';delete env[fileKey];delete env[shaKey];
 const authority=scope.authority;need(authority&&key(authority.repoRoot)===key(scope.repo),'CHILD_NODE_AUTHORITY_ROOT');
 if(authority.mode==='primary-captured-graph')return env;
 need(authority.mode==='snapshot-binding','CHILD_NODE_AUTHORITY_MODE');const proof=authority.proof;
 const expected=path.join(scope.repo,'.ymcc-source-snapshot/node-tools-binding.json');
 need(proof&&key(proof.snapshotRoot)===key(scope.repo)&&key(proof.bindingManifestFile)===key(expected),'CHILD_NODE_BINDING_ROOT');
 const pin=authority.tools.find(row=>key(row.path)===key(expected));
 need(pin&&pin.sha256===proof.expectedBindingSha256,'CHILD_NODE_BINDING_IDENTITY');exactFile(pin);
 env[fileKey]=proof.bindingManifestFile;env[shaKey]=proof.expectedBindingSha256;return env;
}
export function lockGpuTools(repo){return verificationRead(()=>captureGpuTools(repo));}
function captureGpuTools(repo){
 need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&!process.env.ESBUILD_BINARY_PATH,'AMBIENT_TOOL_OVERRIDE');
 const manifest=read(sourcePath(repo,GPU_MODULE+'/tool-inputs.json'));need(manifest.schemaVersion===1&&manifest.kind==='gpu-memory-reviewed-tools','MANIFEST');
 const authority=lockBundledNodeTools(repo),nodeGraph=currentGpuNodeGraph(repo,authority),historicalNode=historicalGpuNode(manifest.nodeAuthority,authority,nodeGraph);
 const selected=manifest.browserAuthority;need(hash(sourcePath(repo,selected.path))===selected.sha256&&hash(sourcePath(repo,selected.helper))===selected.helperSha256,'BROWSER_AUTHORITY_SOURCE_CHANGED');
 const uiDeclaration=loadUiToolDeclaration({repoRoot:repo,manifestPath:sourcePath(repo,selected.path)});need(uiDeclaration.files.length===2302&&uiDeclaration.directories.length===33,'BROWSER_AUTHORITY_MEMBERSHIP');const uiProof=verifyUiToolDeclaration(uiDeclaration);
 need(key(uiDeclaration.executables.node)===key(authority.nodeExecutable)&&key(uiDeclaration.executables.esbuild)===key(authority.compilerBinary),'BROWSER_NODE_COMPILER_IDENTITY');
 const historicalTools=uniqueTools([...historicalNode.originalTools,...manifest.files,...uiDeclaration.files]);need(historicalTools.length===3061,'HISTORICAL_TOOL_OBLIGATION_COUNT');
 const tools=uniqueTools([...authority.tools,...historicalNode.tools,...manifest.files,...uiDeclaration.files]);for(const row of tools)exactFile(row);
 need(key(authority.nodeExecutable)===key(process.execPath),'NODE_EXECUTABLE');
 return {manifest,authority,nodeGraph,historicalNode,historicalTools,tools,uiDeclaration,uiProof};
}
export function gpuToolDeclaration(repo){try{const locked=lockGpuTools(repo);return {artifactInputs:locked.tools.map(row=>row.path),...(locked.manifest.fullWorkflowBlockers.length?{blockedReason:locked.manifest.fullWorkflowBlockers.join('; ')}:{})};}catch(error){return {artifactInputs:[],blockedReason:error.message};}}
export function beginGpuScope(repo,out,sourceRows,{mode='full'}={}){
 const locked=lockGpuTools(repo),root=fs.realpathSync(out);need(key(root)===key(out)&&!inside(fs.realpathSync(repo),root),'OWNED_OUTPUT_ROOT');
 const scope={repo:fs.realpathSync(repo),out:root,...locked,sources:new Map(),observedTools:new Map(),outputs:new Map(),phases:[],bundles:[],native:[],mode};
 scope.toolMap=new Map(scope.tools.map(row=>[key(row.path),row]));
 scope.aliases=gpuNodeAliases(scope.repo,scope.authority,scope.nodeGraph);scope.aliasMap=new Map(scope.aliases.map(row=>[key(row.path),row]));
 for(const row of sourceRows){need(hash(sourcePath(repo,row.path))===row.sha256,'SOURCE_CHANGED_BEFORE:'+row.path);scope.sources.set(row.path,row);}
 verifyGpuScope(scope,'before');return scope;
}
export function requireFullGpuTools(scope){need(!scope.manifest.fullWorkflowBlockers.length&&scope.manifest.browser?.executable,'FULL_WORKFLOW_BLOCKED:'+scope.manifest.fullWorkflowBlockers.join('; '));exactFile(scope.toolMap.get(key(scope.manifest.browser.executable))??{});}
export function observeGpuInput(scope,file,bytes=verificationRead(()=>fs.readFileSync(file))){
 const actual=fs.realpathSync(file),pin=scope.toolMap.get(key(actual));
 if(pin){need(key(file)===key(actual)||(scope.aliasMap.get(key(file))?.sha256===pin.sha256&&key(scope.aliasMap.get(key(file)).target)===key(actual)),'TOOL_ALIAS_NOT_AUTHORIZED');need(digest(bytes)===pin.sha256&&hash(actual)===pin.sha256,'OBSERVED_TOOL_CHANGED');scope.observedTools.set(key(actual),pin);return {role:'tool',row:pin};}
 const row=classifyBundledInput(scope.repo,file,bytes,scope.authority);need(row.role==='source','UNDECLARED_TOOL');
 const before=scope.sources.get(row.row.path);need(before&&before.sha256===row.row.sha256,'SOURCE_NOT_IN_BEFORE:'+row.row.path);return row;
}
export function bindGpuOutput(scope,file,role){
 const target=path.resolve(file),real=fs.realpathSync(target);need(inside(scope.out,target)&&inside(scope.out,real)&&key(target)===key(real)&&fs.statSync(real).isFile(),'OUTPUT_ESCAPE');
 const row={path:target,sha256:hash(real),role,status:'OK'},old=scope.outputs.get(key(target));need(!old||old.sha256===row.sha256,'OUTPUT_CHANGED:'+file);scope.outputs.set(key(target),row);return row;
}
export function verifyGpuScope(scope,phase){return verificationRead(()=>verifyScopeFiles(scope,phase));}
function verifyScopeFiles(scope,phase){
 const authority=verifyBundledToolScope(scope.authority,scope.repo),graph=currentGpuNodeGraph(scope.repo,authority),historical=historicalGpuNode(scope.manifest.nodeAuthority,authority,graph);
 need(JSON.stringify(graph)===JSON.stringify(scope.nodeGraph)&&JSON.stringify(historical.proof)===JSON.stringify(scope.historicalNode.proof),'NODE_AUTHORITY_IDENTITY_CHANGED');const ui=verifyUiToolDeclaration(scope.uiDeclaration);need(ui.manifestSha256===scope.uiProof.manifestSha256,'BROWSER_DECLARATION_CHANGED');
 for(const row of scope.tools)exactFile(row);
 for(const row of scope.sources.values())need(hash(sourcePath(scope.repo,row.path))===row.sha256,'SOURCE_CHANGED:'+row.path);
 for(const row of scope.outputs.values())need(inside(scope.out,row.path)&&key(fs.realpathSync(row.path))===key(row.path)&&hash(row.path)===row.sha256,'OUTPUT_CHANGED:'+row.path);
 const record={phase,status:'PASS',toolCount:scope.tools.length,sourceCount:scope.sources.size,outputCount:scope.outputs.size};scope.phases.push(record);return record;
}
export function gpuObserverConfig(scope,file,{repo=scope.repo,out=scope.out,generated=[]}={}){
 const rows=[...scope.tools,...[...scope.sources.values()].map(row=>({...row,path:sourcePath(scope.repo,row.path)})),...generated.map(p=>bindGpuOutput(scope,p,'generated executable input'))];
 fs.writeFileSync(file,JSON.stringify({repo,out,tools:scope.tools,reads:rows,aliases:scope.aliases,browser:scope.manifest.browser,compiler:scope.authority.compilerBinary},null,2));bindGpuOutput(scope,file,'observer configuration');return file;
}
export function adoptGpuObservation(scope,file){
 const trace=read(file);need(trace.status==='PASS'&&key(trace.node)===key(scope.authority.nodeExecutable)&&Number.isSafeInteger(trace.pid)&&trace.pid>0&&Array.isArray(trace.reads)&&Array.isArray(trace.loads),'RUNTIME_OBSERVATION_FAILED');
 for(const row of trace.outputs??[]){need(row.role==='actual-owned-generated-output'&&hash(row.path)===row.sha256,'RUNTIME_OUTPUT_CHANGED');bindGpuOutput(scope,row.path,'runtime observed generated output');}
 const verificationIdentities=new Map();for(const row of trace.verificationReads??[]){const previous=verificationIdentities.get(key(row.path));need(!previous||previous===row.sha256,'VERIFICATION_IDENTITY_CONFLICT');if(previous)continue;verificationIdentities.set(key(row.path),row.sha256);need(hash(row.path)===row.sha256,'VERIFICATION_INPUT_CHANGED');const declared=scope.toolMap.get(key(row.path))??scope.outputs.get(key(row.path));if(declared)need(declared.sha256===row.sha256,'VERIFICATION_INPUT_UNDECLARED');else {const relative=path.relative(scope.repo,row.path).replaceAll('\\','/');need(scope.sources.get(relative)?.sha256===row.sha256,'VERIFICATION_SOURCE_UNDECLARED');}}
 for(const row of [...trace.reads,...trace.loads]){
  need(hash(row.path)===row.sha256,'RUNTIME_INPUT_CHANGED');
  if(inside(scope.out,row.path)){const pin=scope.outputs.get(key(row.path));need(pin?.sha256===row.sha256,'GENERATED_INPUT_NOT_BOUND');}
  else observeGpuInput(scope,row.path);
 }
 for(const process of trace.processes){const owned=scope.outputs.get(key(process.path));need(key(process.path)===key(scope.authority.compilerBinary)||process.path===scope.manifest.browser?.executable||owned?.sha256===process.sha256,'UNDECLARED_CHILD');if(!owned)exactFile(scope.toolMap.get(key(process.path)));}
 bindGpuOutput(scope,file,'actual runtime module/read/process observation');return trace;
}
export async function gpuBuild(scope,esbuild,options,label){
 const loaded=new Map(),plugins=options.plugins??[];
 const tracker={name:'gpu-declared-actual-bytes',setup(build){build.onLoad({filter:/\.(?:[cm]?js|jsx|tsx?|json)$/,namespace:'file'},({path:file})=>{const bytes=fs.readFileSync(file);observeGpuInput(scope,file,bytes);loaded.set(key(fs.realpathSync(file)),digest(bytes));return {contents:bytes.toString('utf8'),loader:/\.tsx?$/.test(file)?'ts':file.endsWith('.json')?'json':'js',resolveDir:path.dirname(file)};});}};
 need(!options.absWorkingDir||key(options.absWorkingDir)===key(scope.repo),'BUILD_ROOT_OVERRIDE');const result=await esbuild.build({...options,absWorkingDir:scope.repo,metafile:true,plugins:[...plugins,tracker]});
 for(const [file,metadata] of Object.entries(result.metafile.inputs)){
  if(file==='<stdin>'||file.startsWith('test-boundary:')||file.startsWith('mock:'))continue;
  const actual=path.resolve(scope.repo,file),bytes=fs.readFileSync(actual);observeGpuInput(scope,actual,bytes);
  if(loaded.has(key(fs.realpathSync(actual))))need(metadata.bytes===bytes.length,'METAFILE_BYTES_CHANGED');
 }
 const metafile=path.join(scope.out,label+'.metafile.json');fs.writeFileSync(metafile,JSON.stringify(result.metafile,null,2));bindGpuOutput(scope,metafile,'esbuild actual input graph');
 const outputs=result.outputFiles??(options.outfile?[{path:options.outfile}]:[]);for(const [i,output]of outputs.entries()){const file=output.path&&output.path!=='<stdout>'?output.path:path.join(scope.out,label+'-'+i+'.js');if(output.contents)fs.writeFileSync(file,output.contents);bindGpuOutput(scope,file,'actual emitted bundle');}
 scope.bundles.push({label,metafile,loadedInputs:loaded.size});return result;
}
export function observeNativeInputs(scope,name,deps,stdout,stderr){
 const graph=read(deps);need(graph?.Data?.Source&&Array.isArray(graph.Data.Includes)&&graph.Data.Includes.length,'NATIVE_DEPENDENCIES_MISSING');
 const sources=[graph.Data.Source,...graph.Data.Includes].map(file=>{
  if(inside(scope.out,file)){const pin=scope.outputs.get(key(file));need(pin&&pin.sha256===hash(file),'GENERATED_HEADER_NOT_BOUND:'+file);return {...pin,role:'generated-compile-input'};}
  return observeGpuInput(scope,file).row;
 });
 const libraries=parseInputLibraries(Buffer.from(stdout),Buffer.from(stderr)).map(row=>({...row,...observeGpuInput(scope,row.path).row}));
 bindGpuOutput(scope,deps,'MSVC actual source/include graph');const receipt={unit:name,sources,libraries};scope.native.push(receipt);return receipt;
}
export function nativeGpuEnvironment(scope,base){
 const cfg=scope.manifest.native;return {...base,PATH:path.dirname(cfg.compiler)+';'+path.join(process.env.SystemRoot||'C:/Windows','System32'),INCLUDE:cfg.include.join(';'),LIB:cfg.lib.join(';'),CL:'',_CL_:'',LINK:'',_LINK_:''};
}
export function finishGpuScope(scope){
 verifyGpuScope(scope,'after');
 return {schemaVersion:1,status:'PASS',repoInputIdentities:[...scope.sources.values()],browserAuthority:scope.uiProof,nodeAuthority:scope.authority,historicalNodeObligations:{...scope.historicalNode.proof,originalToolCount:scope.historicalTools.length,originalToolsSha256:digest(JSON.stringify(scope.historicalTools))},observationMeaning:'Consumer read requests, CJS require requests/cache members, esbuild metafiles and MSVC dependency/library reports; validation hashing is separate. ESM and OS loader graphs are not claimed.',staticTools:scope.tools,phases:scope.phases,actualToolInputs:[...scope.observedTools.values()],bundles:scope.bundles,native:scope.native,outputs:[...scope.outputs.values()],osLoadedModuleGraphObserved:false,fullToolClosureClaimed:false,outputAuditBinding:'output-evidence.json registered in original adapter resultFiles; public runner/audit independently rehash every owned generated-output row'};
}
export function runtimeGpuScope(){
 const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..'),file=process.env.YMCC_GPU_SCOPE_CONTEXT;need(file&&path.isAbsolute(file),'RUNTIME_SCOPE_REQUIRED');const cfg=read(file);
 need(key(repo)===key(cfg.repo),'RUNTIME_REPOSITORY');const scope=beginGpuScope(repo,cfg.out,[...fs.readdirSync(path.join(repo,'src'),{recursive:true}).filter(f=>/\.(ts|vue|css|json)$/.test(f)).map(f=>'src/'+f.replaceAll('\\','/')),...GPU_SCOPE_SOURCES,'tools/gpu_dedicated_memory_browser_selftest.mjs','package.json','pnpm-lock.yaml',GPU_MODULE+'/live-ui.mjs',GPU_MODULE+'/native-client.mjs'].map(relative=>({path:relative,sha256:hash(sourcePath(repo,relative))})),{mode:'full'});
 return scope;
}
